// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the real card protocol and writeback without a console. The
// storage calls record destinations; no emulated address is dereferenced.
#include <cstdint>

#define RT_TARGET_PPC
#define GC_HOST_COLD
#define GC_CARD_FAKE_EXI
#include "../runtime/gc/gc_card.c"

#include <cstdio>

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #condition); return 1; } } while (0)
#include <cstring>

static gc_card* active_card;
static unsigned sd_writes;
static unsigned usb_writes;
static std::uint32_t written_sector;
static int write_result;
static bool change_during_write;

static unsigned alarms_set;
static std::uint32_t last_alarm_ticks;
void gc_set_alarm(gc_rt_context*, void*, std::uint32_t ticks) {
    alarms_set++;
    last_alarm_ticks = ticks;
}

// EXI's registers for the real card in slot B (card->real_slot_b): three
// channels of five words. A transfer finishes as soon as it starts; an
// immediate read finds `fake_read` in DATA.
static std::uint32_t exi_regs[3][5];
static std::uint32_t exi_starts[3];
static std::uint32_t fake_read;
std::uint32_t card_exi_read(std::uint32_t chan, std::uint32_t reg) { return exi_regs[chan][reg]; }
void card_exi_write(std::uint32_t chan, std::uint32_t reg, std::uint32_t value) {
    if (reg == GC_EXI_CSR) {
        // The status bits clear where a 1 is written; the rest is set.
        const std::uint32_t status = GC_EXI_CSR_EXIINT | GC_EXI_CSR_TCINT | 0x800u;
        const std::uint32_t kept = exi_regs[chan][reg] & status & ~value;
        exi_regs[chan][reg] = (value & ~status) | kept | (exi_regs[chan][reg] & GC_EXI_CSR_EXT);
        return;
    }
    exi_regs[chan][reg] = value;
    if (reg == GC_EXI_CR && (value & GC_EXI_CR_START)) {
        exi_starts[chan]++;
        if (((value >> 2) & 3u) != 1u && !(value & GC_EXI_CR_DMA)) exi_regs[chan][GC_EXI_DATA] = fake_read;
        exi_regs[chan][GC_EXI_CSR] |= GC_EXI_CSR_TCINT;
        exi_regs[chan][reg] = value & ~GC_EXI_CR_START;
    }
}

// The game's EXI calls with the real card in slot B seen as slot A.
static int test_real_slot_b() {
    static gc_card card{};
    gc_rt_context context{};
    card.real_slot_b = 1;
    std::uint32_t pass = 1;
    auto call = [&](std::uint32_t fn, std::uint32_t chan, std::uint32_t a1 = 0, std::uint32_t a2 = 0,
                    std::uint32_t a3 = 0, std::uint32_t a4 = 0) {
        pass = 1;
        return real_on_exi(&context, &card, fn, chan, a1, a2, a3, a4, &pass);
    };
    // The game's own slot B is empty, answered without the hardware.
    exi_regs[1][GC_EXI_CSR] = GC_EXI_CSR_EXT;
    CHECK(call(GC_EXI_PROBE, 1) == 0 && pass == 0);
    CHECK(call(GC_EXI_PROBE_EX, 1) == 0xFFFFFFFFu && pass == 0);
    CHECK(call(GC_EXI_ATTACH, 1) == 0 && pass == 0);
    CHECK(call(GC_EXI_SELECT, 1, 0, 4) == 0 && pass == 0);
    CHECK(call(GC_EXI_LOCK, 1, 0) == 0 && pass == 0);
    CHECK(exi_starts[1] == 0 && (exi_regs[1][GC_EXI_CSR] & GC_EXI_CSR_CS0) == 0);
    // Slot A's clock and SRAM (device 1) and the serial port stay the hardware's.
    CHECK(call(GC_EXI_SELECT, 0, 1, 3) == 0 && pass == 1);
    CHECK(call(GC_EXI_PROBE, 2) == 0 && pass == 1);
    // Slot A is the card in slot B: there while one is plugged in.
    CHECK(call(GC_EXI_PROBE, 0) == 1 && pass == 0);
    CHECK(call(GC_EXI_PROBE_EX, 0) == 1);
    exi_regs[1][GC_EXI_CSR] = 0;
    CHECK(call(GC_EXI_PROBE, 0) == 0);
    CHECK(call(GC_EXI_PROBE_EX, 0) == 0xFFFFFFFFu);
    CHECK(real_id(&card) == 0);
    exi_regs[1][GC_EXI_CSR] = GC_EXI_CSR_EXT | GC_EXI_CSR_MASKS;
    // Its ID, read as EXIGetID does: two bytes out, four in.
    fake_read = 0x00000010u;
    CHECK(real_id(&card) == 0x10u);
    CHECK(exi_starts[1] == 2);
    CHECK((exi_regs[1][GC_EXI_CSR] & GC_EXI_CSR_CS0) == 0);
    // A select goes to channel 1 at the game's clock, masks kept.
    CHECK(call(GC_EXI_LOCK, 0, 0) == 1 && card.locked == 1);
    CHECK(call(GC_EXI_SELECT, 0, 0, 4) == 1 && pass == 0);
    CHECK((exi_regs[1][GC_EXI_CSR] & 0x7F5u) == (GC_EXI_CSR_MASKS | GC_EXI_CSR_CS0 | (4u << 4)));
    CHECK(exi_regs[0][GC_EXI_CSR] == 0);
    // Interrupt on (0x81 0x01): two bytes left-aligned in DATA, a 2-byte write.
    std::uint8_t on[2] = {GC_CARD_SET_INTERRUPT, 1};
    CHECK(real_imm(&card, on, 2, GC_EXI_WRITE) == 1);
    CHECK(exi_regs[1][GC_EXI_DATA] == 0x81010000u);
    CHECK(exi_regs[1][GC_EXI_CR] == ((1u << 2) | (1u << 4)));
    CHECK(card.irq_on == 1);
    CHECK((exi_regs[1][GC_EXI_CSR] & GC_EXI_CSR_TCINT) == 0);  // cleared after the transfer
    CHECK(call(GC_EXI_DESELECT, 0) == 1 && card.selected == 0 && card.real_wait == 0);
    // A page program: the command, its address, the page by DMA, then the
    // deselect starts the wait for the card's interrupt.
    CHECK(call(GC_EXI_SELECT, 0, 0, 4) == 1);
    std::uint8_t program[4] = {GC_CARD_PROGRAM, 0, 0x10, 0};
    CHECK(real_imm(&card, program, 4, GC_EXI_WRITE) == 1);
    std::uint8_t last[1] = {0};
    CHECK(real_imm(&card, last, 1, GC_EXI_WRITE) == 1);
    CHECK(call(GC_EXI_DMA, 0, 0x80123460u, 128, GC_EXI_WRITE) == 1);
    CHECK(exi_regs[1][GC_EXI_MAR] == 0x00123460u && exi_regs[1][GC_EXI_LENGTH] == 128);
    CHECK(exi_regs[1][GC_EXI_CR] == (GC_EXI_CR_DMA | (1u << 2)));
    CHECK(call(GC_EXI_SYNC, 0) == 1);
    const unsigned alarms_before = alarms_set;
    CHECK(call(GC_EXI_DESELECT, 0) == 1);
    CHECK(card.programs == 1 && card.real_wait == GC_CARD_REAL_WAIT_POLLS);
    CHECK(alarms_set == alarms_before + 1 && last_alarm_ticks == GC_CARD_REAL_POLL_TICKS);
    // Not done yet: looked at again later.
    gc_card_on_alarm(&context, &card, 0);
    CHECK(card.real_wait == GC_CARD_REAL_WAIT_POLLS - 1 && card.exi_pending == 0);
    CHECK(alarms_set == alarms_before + 2);
    // Done: the interrupt bit is cleared and the game's EXI callback is due
    // (none registered here, so nothing runs).
    exi_regs[1][GC_EXI_CSR] |= GC_EXI_CSR_EXIINT;
    gc_card_on_alarm(&context, &card, 0);
    CHECK(card.real_wait == 0);
    CHECK((exi_regs[1][GC_EXI_CSR] & GC_EXI_CSR_EXIINT) == 0);
    CHECK((exi_regs[1][GC_EXI_CSR] & GC_EXI_CSR_MASKS) == GC_EXI_CSR_MASKS);
    // A card that never answers: the polls run out and are counted.
    card.real_wait = 1;
    gc_card_on_alarm(&context, &card, 0);
    CHECK(card.real_wait == 0 && card.real_timeouts == 1);
    // A read (no program or erase) starts no wait.
    CHECK(call(GC_EXI_SELECT, 0, 0, 4) == 1);
    std::uint8_t read[4] = {GC_CARD_READ, 0, 0, 0};
    CHECK(real_imm(&card, read, 4, GC_EXI_WRITE) == 1);
    CHECK(call(GC_EXI_DMA, 0, 0x80200000u, 512, GC_EXI_READ) == 1 && card.reads == 1);
    CHECK(call(GC_EXI_DESELECT, 0) == 1 && card.real_wait == 0);
    CHECK(call(GC_EXI_UNLOCK, 0) == 1 && card.locked == 0);
    // Nothing reached channel 0's registers.
    CHECK(exi_regs[0][GC_EXI_CR] == 0 && exi_starts[0] == 0);
    return 0;
}
void gc_flush(std::uint32_t, std::uint32_t) {}

static int record_write(std::uint32_t sector) {
    written_sector = sector;
    if (change_during_write) active_card->activity++;
    return write_result;
}

int32_t gc_sd_write(gc_rt_context*, std::uint32_t sector, std::uint32_t, std::uint32_t) {
    sd_writes++;
    return record_write(sector);
}

static std::uint32_t gc_physical(std::uint32_t address) { return address & 0x3fffffffu; }

// The blob's MEM2 state (gc_rt.h's gc_rt_cold), here a static of the test's.
static gc_rt_cold cold_state;
gc_rt_cold* gc_cold(const gc_rt_context*) { return &cold_state; }

static int32_t gc_ioctlv(gc_rt_context* context, std::uint32_t fd, std::uint32_t request,
                        std::uint32_t inputs, std::uint32_t outputs) {
    const gc_rt_cold* cold = gc_cold(context);
    if (fd != context->usb_fd || request != 0x554D5304u || inputs != 3 || outputs != 0 ||
        cold->vec[0] != gc_physical(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&cold->request[0]))) ||
        cold->vec[2] != gc_physical(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&cold->request[1]))) ||
        cold->vec[1] != 4 || cold->vec[3] != 4 ||
        cold->vec[4] != gc_physical(active_card->image + (cold->request[0] - 1000) * 512) ||
        cold->vec[5] != cold->request[1] * 512) return -999;
    usb_writes++;
    return record_write(cold->request[0]);
}

#define GC_SECTOR 512u
#include "../runtime/gc/gc_usb_write.h"

int main() {
    static gc_card card{};
    static std::uint8_t dirty[GC_CARD_FILE_SECTORS / 8];
    card.dirty = dirty;
    static_assert(sizeof(gc_card) < 2048, "The card state must stay small in MEM1.");
    gc_rt_context context{};
    active_card = &card;
    context.usb_fd = 37;
    for (std::uint32_t bytes = 512u << 10; bytes <= GC_CARD_MAX_BYTES; bytes *= 2) {
        card.bytes = bytes;
        card.image = GC_CARD_IMAGE;
        card.cmd[0] = GC_CARD_GET_ID;
        card.count = 2;
        card.address = 0;
        std::uint32_t id = 0;
        for (unsigned i = 0; i < 4; ++i) id = (id << 8) | card_give(&card);
        CHECK(id == bytes >> 17);
        CHECK(card_byte(&card, bytes + 5) == reinterpret_cast<std::uint8_t*>(GC_CARD_IMAGE + 5));
        std::memset(card.dirty, 0, sizeof(dirty));
        card.dirty_count = 0;
        card_dirty(&card, bytes - 512, 1024);
        CHECK(card.dirty_count == 1);
        card.run_count = 1;
        card.runs[0] = {0, 1000, bytes / 512};
        card.on_usb = 1;
        write_result = -1;
        gc_card_flush(&context, &card, 1);
        CHECK(card.dirty_count == 1);
        CHECK(card.flush_error == -1);
        write_result = 0;
        change_during_write = true;
        gc_card_flush(&context, &card, 1);
        CHECK(card.dirty_count == 1);
        change_during_write = false;
        gc_card_flush(&context, &card, 1);
        CHECK(card.dirty_count == 0);
        CHECK(written_sector == 1000 + bytes / 512 - 1);
        card.on_usb = 0;
        card_dirty(&card, 0, 512);
        gc_card_flush(&context, &card, 1);
        CHECK(card.dirty_count == 0);
    }
    CHECK(usb_writes == 18);
    CHECK(sd_writes == 6);
    write_result = -77;
    CHECK(gc_usb_write(&context, 1007, 3, card.image + 7 * 512) == -77);
    CHECK(usb_writes == 19);
    return test_real_slot_b();
}
