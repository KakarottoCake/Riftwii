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
    for (; s < end && s < GC_CARD_FILE_SECTORS; ++s) {
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
    return (uint8_t*)(uintptr_t)(card->image + (address & (GC_CARD_BYTES - 1u)));
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
        card_dirty(card, card->address & (GC_CARD_BYTES - 1u), 1);
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
        return i < 4 ? (GC_CARD_ID >> (24u - 8u * i)) & 0xFFu : 0;
    }
    return 0;
}

/* Deselect ends a command: an erase happens now, and a program or erase
 * raises the card's interrupt (the game then reads and clears the status). */
static void card_finish(struct gc_rt_context* ctx, struct gc_card* card) {
    const uint32_t cmd = card->count ? card->cmd[0] : 0;
    if (cmd == GC_CARD_ERASE_SECTOR && card->count >= 3) {
        const uint32_t at = (((uint32_t)(card->cmd[1] & 0x7Fu) << 17) | ((uint32_t)card->cmd[2] << 9)) &
                            (GC_CARD_BYTES - 1u) & ~(GC_CARD_SECTOR - 1u);
        uint32_t i;
        for (i = 0; i < GC_CARD_SECTOR; i += 4) *(uint32_t*)(uintptr_t)(card->image + at + i) = 0xFFFFFFFFu;
        card_dirty(card, at, GC_CARD_SECTOR);
        card->erases++;
    } else if (cmd == GC_CARD_ERASE_ALL) {
        uint32_t i;
        for (i = 0; i < GC_CARD_BYTES; i += 4) *(uint32_t*)(uintptr_t)(card->image + i) = 0xFFFFFFFFu;
        card_dirty(card, 0, GC_CARD_BYTES);
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

uint32_t gc_on_exi(struct gc_rt_context* ctx, uint32_t fn, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                   uint32_t a4, uint32_t* pass) {
    struct gc_card* card = card_of(ctx);
    const uint32_t chan = a0;
    (void)a4;
    *pass = 1;
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
        *(uint32_t*)(uintptr_t)a2 = GC_CARD_ID;
        return 1;
    }
    return 1;  /* probe, probe-ex and attach: a card is there */
}

void gc_card_on_alarm(struct gc_rt_context* ctx, struct gc_card* card, uint32_t os_context) {
    const uint32_t tc = card->tc_callback;
    card->alarm_set = 0;
    card->tc_callback = 0;
    if (tc) ((gc_exi_callback_fn)(uintptr_t)tc)(0, os_context);
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
                const int32_t result = gc_sd_write(ctx, run->sd_sector + i, n, card->image + s * 512u);
                uint32_t k;
                if (result < 0) {
                    card->flush_error = result;
                    return;  /* kept dirty: the next heartbeat tries again */
                }
                for (k = 0; k < n; ++k) {
                    card->dirty[(s + k) >> 3] &= (uint8_t)~(1u << ((s + k) & 7u));
                    card->dirty_count--;
                }
                card->flushes++;
            }
            i += n;
        }
    }
}
