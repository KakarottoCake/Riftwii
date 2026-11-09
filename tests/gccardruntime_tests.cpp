// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the real card protocol and writeback without a console. The
// storage calls record destinations; no emulated address is dereferenced.
#include <cstdint>

#define RT_TARGET_PPC
#define GC_HOST_COLD
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

void gc_set_alarm(gc_rt_context*, void*, std::uint32_t) {}
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
}
