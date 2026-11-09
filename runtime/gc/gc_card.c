/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The memory card in slot A (gc_card.h). Same rules as the rest of the
 * blob: no globals, no constant data, no library calls. */
#include "gc_card.h"
#include "gc_rt.h"

#define GC_EXI_READ 0u
#define GC_EXI_WRITE 1u
#define GC_EXI_READ_WRITE 2u

#define GC_CARD_READ 0x52u
#define GC_CARD_ERASE_SECTOR 0xF1u
#define GC_CARD_PROGRAM 0xF2u
#define GC_CARD_ERASE_ALL 0xF4u
#define GC_CARD_STATUS 0x83u
#define GC_CARD_SET_INTERRUPT 0x81u
#define GC_CARD_GET_ID 0x00u  /* two zero bytes, then the 32-bit ID is read */
#define GC_CARD_STATUS_READY_UNLOCKED 0x41u
#define GC_CARD_PAGE 128u

typedef void (*gc_exi_callback_fn)(int32_t chan, uint32_t os_context);

static struct gc_card* card_of(struct gc_rt_context* ctx) { return (struct gc_card*)(uintptr_t)ctx->card; }

static void card_dirty(struct gc_card* card, uint32_t address, uint32_t length) {
    uint32_t s = address / 512u;
    const uint32_t end = (address + length + 511u) / 512u;
    for (; s < end && s < card->bytes / 512u; ++s) {
        uint8_t* bit = &card->dirty[s >> 3];
        const uint8_t mask = (uint8_t)(1u << (s & 7u));
        if (*bit & mask) continue;
        *bit |= mask;
        card->dirty_count++;
    }
}

static void card_alarm(struct gc_rt_context* ctx, struct gc_card* card) {
    if (card->alarm_set) return;  /* one in flight runs whatever is pending */
    card->alarm_set = 1;
    gc_set_alarm(ctx, card->alarm, ctx->delay_ticks);
}

/* The four address bytes after a read or program command. */
static uint32_t card_address(const struct gc_card* card) {
    return ((uint32_t)(card->cmd[1] & 0x7Fu) << 17) | ((uint32_t)card->cmd[2] << 9) |
           ((uint32_t)(card->cmd[3] & 3u) << 7) | (card->cmd[4] & 0x7Fu);
}

static uint8_t* card_byte(const struct gc_card* card, uint32_t address) {
    return (uint8_t*)(uintptr_t)(card->image + (address & (card->bytes - 1u)));
}

static void card_take(struct gc_card* card, uint32_t value) {
    const uint32_t n = card->count++;
    if (n < sizeof(card->cmd)) card->cmd[n] = (uint8_t)value;
    if (n == 0) card->last_cmd = value;
    if (n == 4 && (card->cmd[0] == GC_CARD_READ || card->cmd[0] == GC_CARD_PROGRAM)) card->address = card_address(card);
    if (n == 1 && card->cmd[0] == GC_CARD_SET_INTERRUPT) card->irq_on = value & 1u;
    if (n == 1 && card->cmd[0] == GC_CARD_GET_ID) card->address = 0;  /* the ID's next byte */
    if (n >= 5 && card->cmd[0] == GC_CARD_PROGRAM) {
        /* A page wraps within itself. */
        *card_byte(card, card->address) = (uint8_t)value;
        card_dirty(card, card->address & (card->bytes - 1u), 1);
        card->address = (card->address & ~(GC_CARD_PAGE - 1u)) | ((card->address + 1u) & (GC_CARD_PAGE - 1u));
    }
}

static uint32_t card_give(struct gc_card* card) {
    if (card->cmd[0] == GC_CARD_READ && card->count >= 5) return *card_byte(card, card->address++);
    if (card->cmd[0] == GC_CARD_STATUS) return GC_CARD_STATUS_READY_UNLOCKED;
    /* The ID, most significant byte first. Later SDKs ask through EXIGetID,
     * which the blob answers itself; the 2001 SDK's card library (Luigi's
     * Mansion USA) sends the command on its own, and read 0 as no card. */
    if (card->cmd[0] == GC_CARD_GET_ID && card->count >= 2) {
        const uint32_t i = card->address++;
        return i < 4 ? ((card->bytes >> 17) >> (24u - 8u * i)) & 0xFFu : 0;
    }
    return 0;
}

/* Deselect ends a command: an erase happens now, and a program or erase
 * raises the card's interrupt (the game then reads and clears the status). */
static void card_finish(struct gc_rt_context* ctx, struct gc_card* card) {
    const uint32_t cmd = card->count ? card->cmd[0] : 0;
    if (cmd == GC_CARD_ERASE_SECTOR && card->count >= 3) {
        const uint32_t at = (((uint32_t)(card->cmd[1] & 0x7Fu) << 17) | ((uint32_t)card->cmd[2] << 9)) &
                            (card->bytes - 1u) & ~(GC_CARD_SECTOR - 1u);
        uint32_t i;
        for (i = 0; i < GC_CARD_SECTOR; i += 4) *(uint32_t*)(uintptr_t)(card->image + at + i) = 0xFFFFFFFFu;
        card_dirty(card, at, GC_CARD_SECTOR);
        card->erases++;
    } else if (cmd == GC_CARD_ERASE_ALL) {
        uint32_t i;
        for (i = 0; i < card->bytes; i += 4) *(uint32_t*)(uintptr_t)(card->image + i) = 0xFFFFFFFFu;
        card_dirty(card, 0, card->bytes);
        card->erases++;
    } else if (cmd == GC_CARD_PROGRAM && card->count > 5) {
        card->programs++;
    } else {
        return;
    }
    if (card->irq_on) {
        card->exi_pending = 1;
        card_alarm(ctx, card);
    }
}

static uint32_t card_imm(struct gc_card* card, uint8_t* buf, uint32_t length, uint32_t type) {
    uint32_t i;
    for (i = 0; i < length && i < 4; ++i) {
        if (type != GC_EXI_READ) card_take(card, buf[i]);
        if (type != GC_EXI_WRITE) buf[i] = (uint8_t)card_give(card);
    }
    return 1;
}

static uint32_t card_dma(struct gc_rt_context* ctx, struct gc_card* card, uint32_t buf, uint32_t length, uint32_t type,
                         uint32_t callback) {
    uint8_t* p = (uint8_t*)(uintptr_t)((buf & 0x3FFFFFFFu) | 0x80000000u);
    uint32_t i;
    if (type == GC_EXI_READ) {
        if (card->cmd[0] == GC_CARD_READ) card->reads++;
        for (i = 0; i < length; ++i) p[i] = (uint8_t)card_give(card);
        gc_flush((uint32_t)(uintptr_t)p, length);  /* in memory, as a DMA leaves it: the caller may invalidate */
    } else {
        for (i = 0; i < length; ++i) card_take(card, p[i]);
    }
    if (callback) {
        card->tc_callback = callback;
        card_alarm(ctx, card);
    }
    return 1;
}

/* --- The real card in slot B, seen as slot A (card->real_slot_b) ---------
 * Each EXI call the game makes for slot A's card is carried out on channel
 * 1's registers at once, the way the SDK's own functions program them, and
 * waited for: a transfer is a few bytes or one 128-byte page or 512-byte
 * block, so the wait is short. Callbacks still run from the card's alarm,
 * with channel 0, so the game's card library finds its slot A state. */

/* Polls of the card's interrupt after a program or erase: every half a
 * millisecond or so, for about a second. The SDK's own timeout says what
 * happened if the card never answers. */
#define GC_CARD_REAL_POLL_TICKS 30000u
#define GC_CARD_REAL_WAIT_POLLS 2000u
/* Turns of a transfer's busy loop before it is given up as failed. */
#define GC_CARD_REAL_SPINS 1000000u

#ifdef GC_CARD_FAKE_EXI
/* Host tests: their own registers (tests/gccardruntime_tests.cpp). */
uint32_t card_exi_read(uint32_t chan, uint32_t reg);
void card_exi_write(uint32_t chan, uint32_t reg, uint32_t value);
#else
static uint32_t card_exi_read(uint32_t chan, uint32_t reg) {
    return *(volatile uint32_t*)(uintptr_t)(GC_EXI_REG_BASE + chan * 0x14u + reg * 4u);
}
static void card_exi_write(uint32_t chan, uint32_t reg, uint32_t value) {
    *(volatile uint32_t*)(uintptr_t)(GC_EXI_REG_BASE + chan * 0x14u + reg * 4u) = value;
}
#endif

static int real_present(void) { return (card_exi_read(GC_CARD_REAL_CHAN, GC_EXI_CSR) & GC_EXI_CSR_EXT) != 0; }

/* Starts a transfer and waits for its end, then clears its status bit
 * (masked on this channel, but left set it would show in every look). */
static int real_transfer(uint32_t control) {
    uint32_t spins = 0;
    card_exi_write(GC_CARD_REAL_CHAN, GC_EXI_CR, control | GC_EXI_CR_START);
    while (card_exi_read(GC_CARD_REAL_CHAN, GC_EXI_CR) & GC_EXI_CR_START) {
        if (++spins > GC_CARD_REAL_SPINS) return 0;
    }
    {
        const uint32_t csr = card_exi_read(GC_CARD_REAL_CHAN, GC_EXI_CSR);
        card_exi_write(GC_CARD_REAL_CHAN, GC_EXI_CSR, (csr & GC_EXI_CSR_SETUP) | GC_EXI_CSR_TCINT);
    }
    return 1;
}

/* The command's first bytes, as the emulated card keeps them: which
 * command it is, and whether the card's interrupt was turned on. */
static void real_note(struct gc_card* card, uint32_t value) {
    const uint32_t n = card->count++;
    if (n < sizeof(card->cmd)) card->cmd[n] = (uint8_t)value;
    if (n == 0) card->last_cmd = value;
    if (n == 1 && card->cmd[0] == GC_CARD_SET_INTERRUPT) card->irq_on = value & 1u;
}

static void real_select(struct gc_card* card, uint32_t freq) {
    const uint32_t csr = card_exi_read(GC_CARD_REAL_CHAN, GC_EXI_CSR);
    card_exi_write(GC_CARD_REAL_CHAN, GC_EXI_CSR,
                   (csr & GC_EXI_CSR_MASKS) | GC_EXI_CSR_CS0 | ((freq & 7u) << 4));
    card->selected = 1;
    card->count = 0;
    card->cmd[0] = 0;
}

static void real_deselect(struct gc_rt_context* ctx, struct gc_card* card) {
    const uint32_t csr = card_exi_read(GC_CARD_REAL_CHAN, GC_EXI_CSR);
    const uint32_t cmd = card->count ? card->cmd[0] : 0;
    card_exi_write(GC_CARD_REAL_CHAN, GC_EXI_CSR, csr & GC_EXI_CSR_MASKS);
    card->selected = 0;
    if (cmd == GC_CARD_PROGRAM) card->programs++;
    if (cmd == GC_CARD_ERASE_SECTOR || cmd == GC_CARD_ERASE_ALL) card->erases++;
    /* The card raises its interrupt when the program or erase is done. Its
     * channel's interrupt stays masked (the game's SDK never set up slot
     * B), so the bit is polled from the alarm instead. */
    if (card->irq_on && (cmd == GC_CARD_PROGRAM || cmd == GC_CARD_ERASE_SECTOR || cmd == GC_CARD_ERASE_ALL)) {
        card->real_wait = GC_CARD_REAL_WAIT_POLLS;
        if (!card->alarm_set) {
            card->alarm_set = 1;
            gc_set_alarm(ctx, card->alarm, GC_CARD_REAL_POLL_TICKS);
        }
    }
}

static uint32_t real_imm(struct gc_card* card, uint8_t* buf, uint32_t length, uint32_t type) {
    uint32_t data = 0;
    uint32_t i;
    if (length == 0 || length > 4) return 0;
    if (type != GC_EXI_READ) {
        for (i = 0; i < length; ++i) {
            data |= (uint32_t)buf[i] << (24u - 8u * i);
            real_note(card, buf[i]);
        }
    } else {
        card->count += length;
    }
    card_exi_write(GC_CARD_REAL_CHAN, GC_EXI_DATA, data);
    if (!real_transfer((type << 2) | ((length - 1u) << 4))) return 0;
    if (type != GC_EXI_WRITE) {
        data = card_exi_read(GC_CARD_REAL_CHAN, GC_EXI_DATA);
        for (i = 0; i < length; ++i) buf[i] = (uint8_t)(data >> (24u - 8u * i));
    }
    return 1;
}

static uint32_t real_dma(struct gc_card* card, uint32_t buf, uint32_t length, uint32_t type) {
    card_exi_write(GC_CARD_REAL_CHAN, GC_EXI_MAR, buf & 0x03FFFFE0u);
    card_exi_write(GC_CARD_REAL_CHAN, GC_EXI_LENGTH, length);
    card->count += length;
    if (type == GC_EXI_READ && card->cmd[0] == GC_CARD_READ) card->reads++;
    return (uint32_t)real_transfer(GC_EXI_CR_DMA | (type << 2));
}

/* The card's ID, read as EXIGetID does: two zero bytes out, four in. */
static uint32_t real_id(struct gc_card* card) {
    uint8_t bytes[4] = {0, 0, 0, 0};
    uint32_t ok;
    if (!real_present()) return 0;
    real_select(card, 0);
    ok = real_imm(card, bytes, 2, GC_EXI_WRITE) && real_imm(card, bytes, 4, GC_EXI_READ);
    {
        const uint32_t csr = card_exi_read(GC_CARD_REAL_CHAN, GC_EXI_CSR);
        card_exi_write(GC_CARD_REAL_CHAN, GC_EXI_CSR, csr & GC_EXI_CSR_MASKS);
    }
    card->selected = 0;
    card->count = 0;
    if (!ok) return 0;
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | bytes[3];
}

static uint32_t real_on_exi(struct gc_rt_context* ctx, struct gc_card* card, uint32_t fn, uint32_t a0, uint32_t a1,
                            uint32_t a2, uint32_t a3, uint32_t a4, uint32_t* pass) {
    const uint32_t chan = a0;
    if (chan == GC_CARD_REAL_CHAN) {
        /* The game's own slot B: empty, since its card is slot A now. */
        if ((fn == GC_EXI_SELECT || fn == GC_EXI_LOCK || fn == GC_EXI_GET_ID) && a1 != 0) return 0;
        if (fn == GC_EXI_SELECT || fn == GC_EXI_LOCK || fn == GC_EXI_GET_ID || fn == GC_EXI_PROBE ||
            fn == GC_EXI_PROBE_EX || fn == GC_EXI_ATTACH) {
            *pass = 0;
            if (fn == GC_EXI_GET_ID) *(uint32_t*)(uintptr_t)a2 = 0;
            return fn == GC_EXI_PROBE_EX ? 0xFFFFFFFFu : 0;  /* EXIProbeEx: -1, nothing there */
        }
        return 0;  /* the rest only follow a select, which never succeeds */
    }
    if (chan != 0) return 0;  /* the serial port: the hardware's */
    if (fn == GC_EXI_SELECT || fn == GC_EXI_LOCK || fn == GC_EXI_GET_ID) {
        if (a1 != 0) return 0;  /* the clock and SRAM (device 1), the rest of channel 0 */
    } else if (fn == GC_EXI_IMM || fn == GC_EXI_DMA || fn == GC_EXI_SYNC || fn == GC_EXI_DESELECT) {
        if (!card->selected) return 0;
    } else if (fn == GC_EXI_UNLOCK) {
        if (!card->locked) return 0;
    }
    *pass = 0;
    if (fn != GC_EXI_PROBE && fn != GC_EXI_PROBE_EX) card->activity++;
    if (fn == GC_EXI_SELECT) {
        if (card->selected) return 0;
        real_select(card, a2);
        return 1;
    }
    if (fn == GC_EXI_DESELECT) {
        real_deselect(ctx, card);
        return 1;
    }
    if (fn == GC_EXI_IMM || fn == GC_EXI_DMA) {
        const uint32_t ok = fn == GC_EXI_IMM ? real_imm(card, (uint8_t*)(uintptr_t)a1, a2, a3)
                                             : real_dma(card, a1, a2, a3);
        if (ok && a4) {
            card->tc_callback = a4;
            card_alarm(ctx, card);
        }
        return ok;
    }
    if (fn == GC_EXI_SYNC) return 1;  /* every transfer has finished already */
    if (fn == GC_EXI_LOCK) {
        card->locked = 1;
        return 1;
    }
    if (fn == GC_EXI_UNLOCK) {
        card->locked = 0;
        return 1;
    }
    if (fn == GC_EXI_GET_ID) {
        const uint32_t id = real_id(card);
        *(uint32_t*)(uintptr_t)a2 = id;
        return id != 0;
    }
    if (fn == GC_EXI_PROBE_EX) return real_present() ? 1u : 0xFFFFFFFFu;
    return (uint32_t)real_present();  /* probe and attach */
}

uint32_t gc_on_exi(struct gc_rt_context* ctx, uint32_t fn, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                   uint32_t a4, uint32_t* pass) {
    struct gc_card* card = card_of(ctx);
    const uint32_t chan = a0;
    (void)a4;
    *pass = 1;
    if (card != 0 && card->real_slot_b) return real_on_exi(ctx, card, fn, a0, a1, a2, a3, a4, pass);
    if (card == 0 || chan != 0) return 0;  /* slot B and the serial port: the hardware's */
    if (fn == GC_EXI_SELECT || fn == GC_EXI_LOCK || fn == GC_EXI_GET_ID) {
        if (a1 != 0) return 0;  /* the clock and SRAM (device 1), the rest of channel 0 */
    } else if (fn == GC_EXI_IMM || fn == GC_EXI_DMA || fn == GC_EXI_SYNC || fn == GC_EXI_DESELECT) {
        if (!card->selected) return 0;
    } else if (fn == GC_EXI_UNLOCK) {
        if (!card->locked) return 0;
    }
    *pass = 0;
    /* What keeps the card from being written back. A probe is not: the
     * 2001 SDK's card library probes about 30 times a second while the
     * game runs, and the card file was then never written until HOME. */
    if (fn != GC_EXI_PROBE && fn != GC_EXI_PROBE_EX) card->activity++;
    if (fn == GC_EXI_SELECT) {
        if (card->selected) return 0;
        card->selected = 1;
        card->count = 0;
        card->cmd[0] = 0;
        return 1;
    }
    if (fn == GC_EXI_DESELECT) {
        card->selected = 0;
        card_finish(ctx, card);
        return 1;
    }
    if (fn == GC_EXI_IMM) {
        card_imm(card, (uint8_t*)(uintptr_t)a1, a2, a3);
        if (a4) {
            card->tc_callback = a4;
            card_alarm(ctx, card);
        }
        return 1;
    }
    if (fn == GC_EXI_DMA) return card_dma(ctx, card, a1, a2, a3, a4);
    if (fn == GC_EXI_SYNC) return 1;
    if (fn == GC_EXI_LOCK) {
        card->locked = 1;
        return 1;
    }
    if (fn == GC_EXI_UNLOCK) {
        card->locked = 0;
        return 1;
    }
    if (fn == GC_EXI_GET_ID) {
        *(uint32_t*)(uintptr_t)a2 = (card->bytes >> 17);
        return 1;
    }
    return 1;  /* probe, probe-ex and attach: a card is there */
}

void gc_card_on_alarm(struct gc_rt_context* ctx, struct gc_card* card, uint32_t os_context) {
    const uint32_t tc = card->tc_callback;
    card->alarm_set = 0;
    card->tc_callback = 0;
    if (tc) ((gc_exi_callback_fn)(uintptr_t)tc)(0, os_context);
    if (card->real_wait) {
        /* The real card in slot B: its program or erase is done when it
         * raises its interrupt; until then, look again shortly. */
        const uint32_t csr = card_exi_read(GC_CARD_REAL_CHAN, GC_EXI_CSR);
        if (csr & GC_EXI_CSR_EXIINT) {
            card_exi_write(GC_CARD_REAL_CHAN, GC_EXI_CSR, (csr & GC_EXI_CSR_SETUP) | GC_EXI_CSR_EXIINT);
            card->real_wait = 0;
            card->exi_pending = 1;
        } else if (--card->real_wait != 0) {
            card->alarm_set = 1;
            gc_set_alarm(ctx, card->alarm, GC_CARD_REAL_POLL_TICKS);
        } else {
            card->real_timeouts++;  /* the game's own timeout reports it */
        }
    }
    if (card->exi_pending) {
        /* The game's handler, as EXISetExiCallback left it in channel 0's block. */
        const uint32_t exi = card->ecb ? *(const uint32_t*)(uintptr_t)card->ecb : 0;
        card->exi_pending = 0;
        if (exi) ((gc_exi_callback_fn)(uintptr_t)exi)(0, os_context);
    }
    (void)ctx;
}

/* The dirty sectors, a run of them per write, once the card has had a
 * heartbeat without traffic (a save is many programs in a row). */
void gc_card_flush(struct gc_rt_context* ctx, struct gc_card* card, int force) {
    uint32_t r;
    if (card->dirty_count == 0) return;
    if (!force && card->activity != card->seen_activity) {
        card->seen_activity = card->activity;
        return;
    }
    for (r = 0; r < card->run_count; ++r) {
        const struct gc_card_run* run = &card->runs[r];
        uint32_t i = 0;
        while (i < run->count) {
            const uint32_t s = run->card_sector + i;
            uint32_t n = 0;
            while (i + n < run->count && n < 16 && (card->dirty[(s + n) >> 3] & (1u << ((s + n) & 7u)))) ++n;
            if (n == 0) {
                ++i;
                continue;
            }
            {
                const uint32_t activity = card->activity;
                uint32_t source = card->image + s * 512u;
                int32_t result;
                uint32_t k;
#ifdef RT_TARGET_PPC
                /* The game can program the RAM card while an async SD
                 * write is in flight. Give IOS an immutable snapshot,
                 * and retain dirty bits if any card access overlapped. */
                if (ctx->io_running) {
                    uint8_t* dst = (uint8_t*)(uintptr_t)ctx->bounce;
                    const uint8_t* src = (const uint8_t*)(uintptr_t)source;
                    for (k = 0; k < n * 512u; ++k) dst[k] = src[k];
                    source = ctx->bounce;
                }
#endif
                result = card->on_usb ? gc_usb_write(ctx, run->sd_sector + i, n, source)
                                      : gc_sd_write(ctx, run->sd_sector + i, n, source);
                if (result < 0) {
                    card->flush_error = result;
                    return;  /* kept dirty: the next heartbeat tries again */
                }
                for (k = 0; k < n && card->activity == activity; ++k) {
                    card->dirty[(s + k) >> 3] &= (uint8_t)~(1u << ((s + k) & 7u));
                    card->dirty_count--;
                }
                card->flushes++;
                if (!force) return;
            }
            i += n;
        }
    }
}
