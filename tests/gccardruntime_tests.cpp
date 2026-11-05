// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the real card protocol and writeback without a console. The
// storage calls record destinations; no emulated address is dereferenced.
#include <cstdint>

#define RT_TARGET_PPC
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

int32_t gc_usb_write(gc_rt_context*, std::uint32_t sector, std::uint32_t, std::uint32_t) {
    usb_writes++;
    return record_write(sector);
}

int main() {
    static gc_card card{};
    gc_rt_context context{};
    active_card = &card;
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
        std::memset(card.dirty, 0, sizeof(card.dirty));
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
}
