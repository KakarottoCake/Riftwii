// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// GameCube memory card images: a card formatted as the GameCube loader
// formats one (pinned by its checksums and hash, so the loader's cards
// stay the same), made-up saves imported, listed, exported back, deleted,
// their icons decoded, and damaged cards and save files refused.
#include "riftwii/gccard.hpp"
#include "riftwii/gcbackup.hpp"
#include "riftwii/gcimport.hpp"
#include "riftwii/sha1.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;
using Bytes = std::vector<std::uint8_t>;

constexpr std::uint32_t kBlock = kGcCardBlockBytes;

static std::uint16_t be16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] << 8 | p[1]); }
static void poke16(std::uint8_t* p, std::uint32_t v) {
    p[0] = std::uint8_t(v >> 8);
    p[1] = std::uint8_t(v);
}
static void poke32(std::uint8_t* p, std::uint32_t v) {
    poke16(p, v >> 16);
    poke16(p + 2, v);
}

static std::string hex(const Sha1Digest& digest) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (std::uint8_t b : digest) {
        out += digits[b >> 4];
        out += digits[b & 15];
    }
    return out;
}

// The format inputs the pinned values below were made with (and checked
// against the loader's own format_card before it moved here).
static GcCardFormat sample_format(bool shift_jis) {
    GcCardFormat format;
    for (unsigned i = 0; i < 12; ++i) format.flash_id[i] = std::uint8_t(0x10 + i * 3);
    format.counter_bias = 0x12345678;
    format.language = 2;
    format.format_time = 0x0123456789ABCDEFull;
    format.dtv = 1;
    format.size_mbits = 16;
    format.shift_jis = shift_jis;
    return format;
}

static Bytes blank_card() {
    Bytes card;
    gc_card_format(card, sample_format(false));
    return card;
}

struct SaveSpec {
    std::string game = "GZLE";
    std::string maker = "01";
    std::string name = "riftwii test";
    std::uint16_t blocks = 3;
    std::uint16_t first_block = 5;
    std::uint32_t comment_offset = 0x1FF0;  // across the first two blocks
    std::uint32_t image_offset = 0x2400;
    std::uint8_t banner_format = 0;
    std::uint16_t icon_format = 2;          // one RGB5A3 frame
    std::uint8_t seed = 1;
};

// A .gci: its directory entry, then its blocks.
static Bytes make_gci(const SaveSpec& spec) {
    Bytes gci(kGcCardEntryBytes + std::size_t(spec.blocks) * kBlock, 0);
    std::uint8_t* e = gci.data();
    std::memset(e, 0, kGcCardEntryBytes);
    std::memcpy(e + 0x00, spec.game.data(), 4);
    std::memcpy(e + 0x04, spec.maker.data(), 2);
    e[0x06] = 0xFF;
    e[0x07] = spec.banner_format;
    std::memcpy(e + 0x08, spec.name.data(), spec.name.size());
    poke32(e + 0x28, 0x2A3B4C5D);
    poke32(e + 0x2C, spec.image_offset);
    poke16(e + 0x30, spec.icon_format);
    poke16(e + 0x32, 0x0003);
    e[0x34] = 0x04;
    e[0x35] = 0x00;
    poke16(e + 0x36, spec.first_block);
    poke16(e + 0x38, spec.blocks);
    poke16(e + 0x3A, 0xFFFF);
    poke32(e + 0x3C, spec.comment_offset);
    for (std::size_t i = kGcCardEntryBytes; i < gci.size(); ++i)
        gci[i] = std::uint8_t(spec.seed + i * 13 + (i >> 9));
    const std::string title = "RiftWii Test Save";
    const std::string detail = "Second line";
    if (std::size_t(spec.comment_offset) + 64 <= gci.size() - kGcCardEntryBytes) {
        std::uint8_t* comment = gci.data() + kGcCardEntryBytes + spec.comment_offset;
        std::memset(comment, 0, 64);
        std::memcpy(comment, title.data(), title.size());
        std::memcpy(comment + 32, detail.data(), detail.size());
    }
    return gci;
}

static GcCardInfo read_ok(const Bytes& card) {
    GcCardInfo info;
    std::string error;
    EXPECT_TRUE(gc_card_read(card, info, error));
    if (!error.empty()) std::cerr << "  read: " << error << std::endl;
    return info;
}

// Rewrites one word of the BAT in use and its checksum, as a card damaged
// by something that still kept its checksums would look.
static void set_bat_word(Bytes& card, std::uint32_t block, std::uint16_t value) {
    const GcCardInfo info = read_ok(card);
    std::uint8_t* bat = card.data() + (3 + info.bat_copy) * kBlock;
    poke16(bat + 0xA + (block - 5) * 2, value);
    gc_card_checksum(bat + 4, kBlock - 4, bat);
}

static void set_entry_word(Bytes& card, unsigned slot, std::uint32_t field, std::uint16_t value) {
    const GcCardInfo info = read_ok(card);
    std::uint8_t* dir = card.data() + (1 + info.directory_copy) * kBlock;
    poke16(dir + slot * kGcCardEntryBytes + field, value);
    gc_card_checksum(dir, 0x1FFC, dir + 0x1FFC);
}

static void test_format() {
    const Bytes card = blank_card();
    EXPECT_EQ(card.size(), 0x200000u);
    // Pinned: the loader's cards must not change (see sample_format).
    EXPECT_EQ(be16(card.data() + 0x1FC), 0x9858);
    EXPECT_EQ(be16(card.data() + 0x1FE), 0x66AA);
    for (unsigned copy = 0; copy < 2; ++copy) {
        const std::uint8_t* dir = card.data() + (1 + copy) * kBlock;
        EXPECT_EQ(be16(dir + 0x1FFA), 0xFFFF);
        EXPECT_EQ(be16(dir + 0x1FFC), 0xF002);
        EXPECT_EQ(be16(dir + 0x1FFE), 0x0000);
        const std::uint8_t* bat = card.data() + (3 + copy) * kBlock;
        EXPECT_EQ(be16(bat + 4), copy);
        EXPECT_EQ(be16(bat + 6), 251u);
        EXPECT_EQ(be16(bat + 8), 4u);
    }
    EXPECT_EQ(be16(card.data() + 3 * kBlock), 0x00FF);
    EXPECT_EQ(be16(card.data() + 3 * kBlock + 2), 0xEF03);
    EXPECT_EQ(be16(card.data() + 4 * kBlock), 0x0100);
    EXPECT_EQ(be16(card.data() + 4 * kBlock + 2), 0xEF02);
    EXPECT_EQ(hex(sha1(card.data(), card.size())), std::string("0f3dcc23b88677e1dae489fbbd9f3ecdaa9e6c5d"));
    EXPECT_EQ(hex(sha1(blank_card().data(), 0x200)), hex(sha1(card.data(), 0x200)));

    Bytes japanese;
    gc_card_format(japanese, sample_format(true));
    EXPECT_EQ(be16(japanese.data() + 0x24), 1u);

    const GcCardInfo info = read_ok(card);
    EXPECT_EQ(info.blocks, 256u);
    EXPECT_EQ(info.free_blocks, 251u);
    EXPECT_EQ(info.free_entries, 127u);
    EXPECT_EQ(info.directory_copy, 0u);
    EXPECT_EQ(info.bat_copy, 1u);
    EXPECT_FALSE(info.shift_jis);
    EXPECT_TRUE(info.saves.empty());
    EXPECT_TRUE(read_ok(japanese).shift_jis);

    // A smaller card: 4 Mbit, 59 blocks for saves.
    GcCardFormat small = sample_format(false);
    small.size_mbits = 4;
    Bytes small_card;
    gc_card_format(small_card, small);
    EXPECT_EQ(small_card.size(), 0x80000u);
    EXPECT_EQ(read_ok(small_card).free_blocks, 59u);
}

static void test_import_export_delete() {
    Bytes card = blank_card();
    std::string error;
    const SaveSpec spec;
    const Bytes gci = make_gci(spec);
    unsigned slot = 99;
    EXPECT_TRUE(gc_card_import(card, gci, error, &slot));
    EXPECT_EQ(slot, 0u);

    GcCardInfo info = read_ok(card);
    // The update went into the other copies: directory 1 (counter 0xFFFF
    // + 1 wraps to 0, still newer) and BAT 0.
    EXPECT_EQ(info.directory_copy, 1u);
    EXPECT_EQ(info.bat_copy, 0u);
    EXPECT_EQ(be16(card.data() + 2 * kBlock + 0x1FFA), 0u);
    EXPECT_EQ(be16(card.data() + 3 * kBlock + 4), 2u);
    EXPECT_EQ(info.free_blocks, 248u);
    EXPECT_EQ(be16(card.data() + 3 * kBlock + 6), 248u);
    EXPECT_EQ(info.free_entries, 126u);
    EXPECT_EQ(info.saves.size(), 1u);
    if (info.saves.size() == 1) {
        const GcSave& s = info.saves[0];
        EXPECT_EQ(s.game_code, std::string("GZLE"));
        EXPECT_EQ(s.maker, std::string("01"));
        EXPECT_EQ(s.file_name, std::string("riftwii test"));
        EXPECT_EQ(s.comment_title, std::string("RiftWii Test Save"));
        EXPECT_EQ(s.comment_detail, std::string("Second line"));
        EXPECT_EQ(s.first_block, 5u);
        EXPECT_EQ(s.block_count, 3u);
        EXPECT_EQ(s.modified, 0x2A3B4C5Du);
        EXPECT_EQ(s.image_offset, 0x2400u);
        EXPECT_EQ(s.icon_format, 2u);
        EXPECT_EQ(s.icon_speed, 3u);
        EXPECT_EQ(s.permissions, 4u);
        EXPECT_EQ(s.comment_offset, 0x1FF0u);
        EXPECT_TRUE(s.intact);
    }
    // The blocks went to 5, 6 and 7, chained, the data as given.
    const std::uint8_t* bat = card.data() + 3 * kBlock;
    EXPECT_EQ(be16(bat + 0xA), 6u);
    EXPECT_EQ(be16(bat + 0xC), 7u);
    EXPECT_EQ(be16(bat + 0xE), 0xFFFFu);
    EXPECT_EQ(be16(bat + 8), 7u);
    EXPECT_TRUE(std::memcmp(card.data() + 5 * kBlock, gci.data() + 64, 3 * kBlock) == 0);

    // Exported back byte for byte (its first block was already 5).
    Bytes out;
    EXPECT_TRUE(gc_card_export(card, 0, out, error));
    EXPECT_TRUE(out == gci);

    // The same save again is refused, the card untouched.
    const Bytes before = card;
    EXPECT_FALSE(gc_card_import(card, gci, error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(card == before);

    // A second save goes after the last allocated block.
    SaveSpec second_spec;
    second_spec.name = "second";
    second_spec.blocks = 2;
    second_spec.first_block = 0x1234;  // whatever the other card had; replaced
    second_spec.seed = 77;
    const Bytes second = make_gci(second_spec);
    EXPECT_TRUE(gc_card_import(card, second, error, &slot));
    EXPECT_EQ(slot, 1u);
    info = read_ok(card);
    EXPECT_EQ(info.directory_copy, 0u);
    EXPECT_EQ(info.bat_copy, 1u);
    EXPECT_EQ(info.free_blocks, 246u);
    EXPECT_EQ(info.saves.size(), 2u);
    if (info.saves.size() == 2) EXPECT_EQ(info.saves[1].first_block, 8u);
    EXPECT_TRUE(gc_card_export(card, 1, out, error));
    EXPECT_EQ(out.size(), second.size());
    EXPECT_EQ(be16(out.data() + 0x36), 8u);
    EXPECT_TRUE(std::memcmp(out.data() + 0x38, second.data() + 0x38, out.size() - 0x38) == 0);
    EXPECT_TRUE(std::memcmp(out.data(), second.data(), 0x36) == 0);

    // Deleting the first frees its three blocks; the second is untouched.
    EXPECT_TRUE(gc_card_delete(card, 0, error));
    info = read_ok(card);
    EXPECT_EQ(info.free_blocks, 249u);
    EXPECT_EQ(info.free_entries, 126u);
    EXPECT_EQ(info.saves.size(), 1u);
    if (info.saves.size() == 1) EXPECT_EQ(info.saves[0].file_name, std::string("second"));
    EXPECT_TRUE(gc_card_export(card, 1, out, error));
    EXPECT_TRUE(std::memcmp(out.data() + 64, second.data() + 64, out.size() - 64) == 0);
    EXPECT_FALSE(gc_card_delete(card, 0, error));
    EXPECT_FALSE(gc_card_export(card, 0, out, error));

    // The next save takes slot 0 again and blocks after 9, not the freed 5-7.
    EXPECT_TRUE(gc_card_import(card, gci, error, &slot));
    EXPECT_EQ(slot, 0u);
    info = read_ok(card);
    if (!info.saves.empty()) EXPECT_EQ(info.saves[0].first_block, 10u);

    // A save as large as what is left wraps around to the freed blocks.
    SaveSpec big_spec;
    big_spec.name = "big";
    big_spec.blocks = static_cast<std::uint16_t>(info.free_blocks);
    big_spec.seed = 5;
    const Bytes big = make_gci(big_spec);
    EXPECT_TRUE(gc_card_import(card, big, error, &slot));
    info = read_ok(card);
    EXPECT_EQ(info.free_blocks, 0u);
    for (const GcSave& s : info.saves) EXPECT_TRUE(s.intact);
    EXPECT_TRUE(gc_card_export(card, slot, out, error));
    EXPECT_TRUE(std::memcmp(out.data() + 64, big.data() + 64, out.size() - 64) == 0);
    SaveSpec one_more;
    one_more.name = "one more";
    one_more.blocks = 1;
    EXPECT_FALSE(gc_card_import(card, make_gci(one_more), error));
    EXPECT_TRUE(error.find("free blocks") != std::string::npos);

    // Deleting everything gives every block back.
    for (unsigned s = 0; s < kGcCardMaxEntries; ++s) gc_card_delete(card, s, error);
    info = read_ok(card);
    EXPECT_EQ(info.free_blocks, 251u);
    EXPECT_EQ(info.free_entries, 127u);
}

static void test_directory_full() {
    Bytes card = blank_card();
    std::string error;
    for (unsigned i = 0; i < kGcCardMaxEntries; ++i) {
        SaveSpec spec;
        spec.name = "save " + std::to_string(i);
        spec.blocks = 1;
        spec.comment_offset = 0;
        EXPECT_TRUE(gc_card_import(card, make_gci(spec), error));
    }
    const GcCardInfo info = read_ok(card);
    EXPECT_EQ(info.free_entries, 0u);
    EXPECT_EQ(info.saves.size(), 127u);
    EXPECT_EQ(info.free_blocks, 251u - 127u);
    SaveSpec spec;
    spec.name = "one too many";
    spec.blocks = 1;
    EXPECT_FALSE(gc_card_import(card, make_gci(spec), error));
    EXPECT_TRUE(error.find("directory") != std::string::npos);
}

// How full a card is from its first five blocks alone, as gc_card_read
// counts it: an empty card, one with no directory slot left, one with no
// block left, and refusals.
static void test_card_space() {
    const std::size_t head_bytes = kGcCardSystemBlocks * kBlock;
    const auto space_of = [&](const Bytes& card, GcCardSpace& space) {
        std::string error;
        const Bytes head(card.begin(), card.begin() + static_cast<std::ptrdiff_t>(head_bytes));
        return gc_card_space(head, card.size(), space, error);
    };
    std::string error;
    Bytes card = blank_card();
    GcCardSpace space;
    EXPECT_TRUE(space_of(card, space));
    EXPECT_EQ(space.free_blocks, 251u);
    EXPECT_EQ(space.free_entries, 127u);
    EXPECT_FALSE(space.full());

    // Every directory slot taken by a one-block save: full, blocks to spare.
    Bytes slots = card;
    for (unsigned i = 0; i < kGcCardMaxEntries; ++i) {
        SaveSpec spec;
        spec.name = "save " + std::to_string(i);
        spec.blocks = 1;
        spec.comment_offset = 0;
        gc_card_import(slots, make_gci(spec), error);
    }
    EXPECT_TRUE(space_of(slots, space));
    const GcCardInfo info = read_ok(slots);
    EXPECT_EQ(space.free_entries, info.free_entries);
    EXPECT_EQ(space.free_blocks, info.free_blocks);
    EXPECT_EQ(space.free_entries, 0u);
    EXPECT_TRUE(space.full());

    // Every block taken by one large save: full, slots to spare.
    Bytes blocks = card;
    SaveSpec big;
    big.name = "everything";
    big.blocks = 251;
    EXPECT_TRUE(gc_card_import(blocks, make_gci(big), error));
    EXPECT_TRUE(space_of(blocks, space));
    EXPECT_EQ(space.free_blocks, 0u);
    EXPECT_EQ(space.free_entries, 126u);
    EXPECT_TRUE(space.full());

    // A card whose length is not its header's, or too little read of it.
    EXPECT_FALSE(gc_card_space(Bytes(card.begin(), card.begin() + static_cast<std::ptrdiff_t>(head_bytes)),
                               card.size() / 2, space, error));
    EXPECT_FALSE(gc_card_space(Bytes(card.begin(), card.begin() + kBlock), card.size(), space, error));
}

static void test_copies() {
    Bytes card = blank_card();
    std::string error;
    EXPECT_TRUE(gc_card_import(card, make_gci(SaveSpec()), error));
    EXPECT_EQ(read_ok(card).saves.size(), 1u);

    // A newer directory cut off halfway (its checksum broken): the older
    // copy, without the save, is the card's again.
    Bytes torn = card;
    torn[2 * kBlock + 0x10] ^= 0x55;
    GcCardInfo info = read_ok(torn);
    EXPECT_EQ(info.directory_copy, 0u);
    EXPECT_TRUE(info.saves.empty());

    // A torn BAT: the older one, where the save's blocks are free, so the
    // save reads as damaged rather than being trusted.
    torn = card;
    torn[3 * kBlock + 0x20] ^= 0x01;
    info = read_ok(torn);
    EXPECT_EQ(info.bat_copy, 1u);
    EXPECT_EQ(info.free_blocks, 251u);
    EXPECT_EQ(info.saves.size(), 1u);
    if (!info.saves.empty()) EXPECT_FALSE(info.saves[0].intact);

    // Both copies broken: refused.
    torn = card;
    torn[1 * kBlock + 5] ^= 1;
    torn[2 * kBlock + 5] ^= 1;
    EXPECT_FALSE(gc_card_read(torn, info, error));
    EXPECT_TRUE(error.find("directory") != std::string::npos);
    torn = card;
    torn[3 * kBlock + 0x100] ^= 1;
    torn[4 * kBlock + 0x100] ^= 1;
    EXPECT_FALSE(gc_card_read(torn, info, error));
    EXPECT_TRUE(error.find("block table") != std::string::npos);
    EXPECT_FALSE(gc_card_import(torn, make_gci(SaveSpec()), error));
    EXPECT_FALSE(gc_card_delete(torn, 0, error));
}

static void test_damaged_cards() {
    std::string error;
    GcCardInfo info;
    Bytes card = blank_card();

    EXPECT_FALSE(gc_card_read(Bytes(), info, error));
    EXPECT_FALSE(gc_card_read(Bytes(5 * kBlock - 1, 0), info, error));
    Bytes erased(0x200000, 0xFF);
    EXPECT_FALSE(gc_card_read(erased, info, error));
    Bytes short_card(card.begin(), card.end() - kBlock);
    EXPECT_FALSE(gc_card_read(short_card, info, error));
    EXPECT_TRUE(error.find("length") != std::string::npos);
    Bytes bad_header = card;
    bad_header[3] ^= 1;
    EXPECT_FALSE(gc_card_read(bad_header, info, error));
    Bytes odd_size = card;
    poke16(odd_size.data() + 0x22, 12);
    gc_card_checksum(odd_size.data(), 0x1FC, odd_size.data() + 0x1FC);
    EXPECT_FALSE(gc_card_read(odd_size, info, error));

    EXPECT_TRUE(gc_card_import(card, make_gci(SaveSpec()), error));
    SaveSpec other;
    other.name = "other";
    other.blocks = 2;
    EXPECT_TRUE(gc_card_import(card, make_gci(other), error));  // blocks 8, 9

    // A loop: block 6 points back at 5.
    Bytes looped = card;
    set_bat_word(looped, 6, 5);
    info = read_ok(looped);
    EXPECT_EQ(info.saves.size(), 2u);
    if (info.saves.size() == 2) {
        EXPECT_FALSE(info.saves[0].intact);
        EXPECT_TRUE(info.saves[0].comment_title.empty());
        EXPECT_TRUE(info.saves[1].intact);
    }
    Bytes out;
    EXPECT_FALSE(gc_card_export(looped, 0, out, error));
    std::vector<std::uint8_t> rgba;
    EXPECT_FALSE(gc_card_icon(looped, 0, rgba, error));
    EXPECT_TRUE(gc_card_delete(looped, 0, error));
    info = read_ok(looped);
    EXPECT_EQ(info.saves.size(), 1u);
    EXPECT_EQ(info.free_blocks, 248u);  // 5 and 6 back; 7, cut off by the loop, stays taken

    // Out of range and into a free block.
    Bytes wild = card;
    set_bat_word(wild, 5, 0x1234);
    EXPECT_FALSE(read_ok(wild).saves[0].intact);
    EXPECT_TRUE(gc_card_delete(wild, 0, error));
    Bytes into_free = card;
    set_bat_word(into_free, 6, 200);
    EXPECT_FALSE(read_ok(into_free).saves[0].intact);
    Bytes ends_early = card;
    set_bat_word(ends_early, 6, 0xFFFF);
    EXPECT_FALSE(read_ok(ends_early).saves[0].intact);

    // The first save's chain runs on into the second's (5 6 7 8 9, its
    // count made 5): both follow to their ends but share 8 and 9, so both
    // are listed as damaged. Deleting the first frees only 5 to 7: 8 and 9
    // lie on the second's clean chain, which is whole again afterwards.
    Bytes crossed = card;
    set_bat_word(crossed, 7, 8);
    set_entry_word(crossed, 0, 0x38, 5);
    info = read_ok(crossed);
    EXPECT_FALSE(info.saves[0].intact);
    EXPECT_FALSE(info.saves[1].intact);
    EXPECT_TRUE(gc_card_delete(crossed, 0, error));
    info = read_ok(crossed);
    EXPECT_EQ(info.saves.size(), 1u);
    EXPECT_EQ(info.free_blocks, 249u);
    if (!info.saves.empty()) EXPECT_TRUE(info.saves[0].intact);

    // A damaged save whose walk breaks off inside a good save's chain
    // (5 6, then 8, the second save's first block, where its count of 3
    // wants an end): only the second's chain followed cleanly. Deleting
    // the damaged one must leave 8 and 9 taken, and the second save whole
    // and exported as it was; 7, cut off, stays taken.
    Bytes into_good = card;
    Bytes second_before;
    EXPECT_TRUE(gc_card_export(card, 1, second_before, error));
    set_bat_word(into_good, 6, 8);
    info = read_ok(into_good);
    EXPECT_FALSE(info.saves[0].intact);
    EXPECT_FALSE(info.saves[1].intact);
    EXPECT_TRUE(gc_card_delete(into_good, 0, error));
    info = read_ok(into_good);
    EXPECT_EQ(info.saves.size(), 1u);
    EXPECT_EQ(info.free_blocks, 248u);
    if (!info.saves.empty()) {
        EXPECT_TRUE(info.saves[0].intact);
        Bytes second_after;
        EXPECT_TRUE(gc_card_export(into_good, info.saves[0].slot, second_after, error));
        EXPECT_TRUE(second_after == second_before);
    }
    // An import afterwards takes free blocks only, never 8 or 9.
    SaveSpec third;
    third.name = "third";
    third.blocks = 4;
    EXPECT_TRUE(gc_card_import(into_good, make_gci(third), error));
    Bytes second_again;
    EXPECT_TRUE(gc_card_export(into_good, 1, second_again, error));
    EXPECT_TRUE(second_again == second_before);

    // First block, block count out of range.
    Bytes entry_bad = card;
    set_entry_word(entry_bad, 0, 0x36, 3);
    EXPECT_FALSE(read_ok(entry_bad).saves[0].intact);
    set_entry_word(entry_bad, 0, 0x36, 300);
    EXPECT_FALSE(read_ok(entry_bad).saves[0].intact);
    entry_bad = card;
    set_entry_word(entry_bad, 0, 0x38, 0);
    EXPECT_FALSE(read_ok(entry_bad).saves[0].intact);
    set_entry_word(entry_bad, 0, 0x38, 0xFFFF);
    EXPECT_FALSE(read_ok(entry_bad).saves[0].intact);

    // A comment past the save's end reads as empty; the save is fine.
    Bytes far_comment = card;
    set_entry_word(far_comment, 0, 0x3C, 0xFFFF);
    set_entry_word(far_comment, 0, 0x3E, 0xFFF0);
    info = read_ok(far_comment);
    EXPECT_TRUE(info.saves[0].intact);
    EXPECT_TRUE(info.saves[0].comment_title.empty());

    // A BAT whose free count drifted: imports count the table, and fix it.
    Bytes drifted = card;
    {
        const unsigned copy = read_ok(drifted).bat_copy;
        std::uint8_t* bat = drifted.data() + (3 + copy) * kBlock;
        poke16(bat + 6, 3);
        gc_card_checksum(bat + 4, kBlock - 4, bat);
    }
    SaveSpec ten;
    ten.name = "ten";
    ten.blocks = 10;
    EXPECT_TRUE(gc_card_import(drifted, make_gci(ten), error));
    info = read_ok(drifted);
    EXPECT_EQ(be16(drifted.data() + (3 + info.bat_copy) * kBlock + 6), info.free_blocks);
}

static void test_bad_gci() {
    Bytes card = blank_card();
    const Bytes before = card;
    std::string error;
    EXPECT_FALSE(gc_card_import(card, Bytes(), error));
    EXPECT_FALSE(gc_card_import(card, Bytes(63, 0x41), error));
    Bytes gci = make_gci(SaveSpec());
    Bytes cut(gci.begin(), gci.end() - 1);
    EXPECT_FALSE(gc_card_import(card, cut, error));
    EXPECT_TRUE(error.find("length") != std::string::npos);
    Bytes longer = gci;
    longer.push_back(0);
    EXPECT_FALSE(gc_card_import(card, longer, error));
    Bytes zero_blocks = gci;
    poke16(zero_blocks.data() + 0x38, 0);
    zero_blocks.resize(64);
    EXPECT_FALSE(gc_card_import(card, zero_blocks, error));
    Bytes blank = gci;
    blank[0] = 0xFF;
    EXPECT_FALSE(gc_card_import(card, blank, error));
    SaveSpec huge;
    huge.blocks = 252;
    EXPECT_FALSE(gc_card_import(card, make_gci(huge), error));
    EXPECT_TRUE(card == before);
}

// Encodes 32x32 RGBA-ish test colours the way the card stores them.
static std::uint16_t test_colour(unsigned x, unsigned y) {
    if ((x + y) % 3 == 0) return static_cast<std::uint16_t>(((x & 7) << 12) | ((y & 15) << 8) | (x & 15) << 4 | 9);
    return static_cast<std::uint16_t>(0x8000 | (x << 10) | (y << 5) | ((x + y) & 31));
}

static void expect_colour(const std::vector<std::uint8_t>& rgba, unsigned x, unsigned y, std::uint16_t v) {
    const std::uint8_t* p = rgba.data() + (y * 32 + x) * 4;
    if (v & 0x8000) {
        const unsigned r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
        EXPECT_EQ(unsigned(p[0]), (r << 3) | (r >> 2));
        EXPECT_EQ(unsigned(p[1]), (g << 3) | (g >> 2));
        EXPECT_EQ(unsigned(p[2]), (b << 3) | (b >> 2));
        EXPECT_EQ(unsigned(p[3]), 255u);
    } else {
        EXPECT_EQ(unsigned(p[0]), ((v >> 8) & 15) * 17u);
        EXPECT_EQ(unsigned(p[1]), ((v >> 4) & 15) * 17u);
        EXPECT_EQ(unsigned(p[2]), (v & 15) * 17u);
        const unsigned a = (v >> 12) & 7;
        EXPECT_EQ(unsigned(p[3]), (a << 5) | (a << 2) | (a >> 1));
    }
}

static void test_icons() {
    std::string error;
    std::vector<std::uint8_t> rgba;

    // RGB5A3, no banner, the icon across the first block's end.
    {
        Bytes card = blank_card();
        SaveSpec spec;
        spec.image_offset = 0x1C00;
        spec.comment_offset = 0x40;
        Bytes gci = make_gci(spec);
        std::uint8_t* icon = gci.data() + 64 + spec.image_offset;
        for (unsigned y = 0; y < 32; ++y)
            for (unsigned x = 0; x < 32; ++x) {
                const unsigned tile = (y / 4) * 8 + x / 4;
                const unsigned inside = (y % 4) * 4 + x % 4;
                poke16(icon + (tile * 16 + inside) * 2, test_colour(x, y));
            }
        EXPECT_TRUE(gc_card_import(card, gci, error));
        EXPECT_TRUE(gc_card_icon(card, 0, rgba, error));
        EXPECT_EQ(rgba.size(), 4096u);
        for (unsigned y = 0; y < 32; ++y)
            for (unsigned x = 0; x < 32; ++x) expect_colour(rgba, x, y, test_colour(x, y));
    }

    // CI8 after a CI8 banner: frame 0 shared-palette CI8, frame 1 RGB5A3,
    // so the palette sits after both frames. Then the same frame with its
    // own palette right after it.
    for (unsigned own = 0; own < 2; ++own) {
        Bytes card = blank_card();
        SaveSpec spec;
        spec.image_offset = 0x100;
        spec.banner_format = 1;
        spec.icon_format = static_cast<std::uint16_t>((own ? 3 : 1) | (2 << 2));
        spec.comment_offset = 0x3000;
        Bytes gci = make_gci(spec);
        std::uint8_t* save = gci.data() + 64;
        const std::uint32_t frame0 = spec.image_offset + 96 * 32 + 512;
        const std::uint32_t palette = own ? frame0 + 1024 : frame0 + 1024 + 2048;
        for (unsigned i = 0; i < 256; ++i) poke16(save + palette + i * 2, test_colour(i & 31, i >> 3));
        for (unsigned y = 0; y < 32; ++y)
            for (unsigned x = 0; x < 32; ++x) {
                const unsigned tile = (y / 4) * 4 + x / 8;
                const unsigned inside = (y % 4) * 8 + x % 8;
                save[frame0 + tile * 32 + inside] = std::uint8_t(x * 8 + y);
            }
        EXPECT_TRUE(gc_card_import(card, gci, error));
        EXPECT_TRUE(gc_card_icon(card, 0, rgba, error));
        for (unsigned y = 0; y < 32; ++y)
            for (unsigned x = 0; x < 32; ++x) {
                const unsigned index = (x * 8 + y) & 0xFF;
                expect_colour(rgba, x, y, test_colour(index & 31, index >> 3));
            }
    }

    // No icon, an icon past the save's end, an unknown banner format.
    Bytes card = blank_card();
    SaveSpec none;
    none.icon_format = 0;
    EXPECT_TRUE(gc_card_import(card, make_gci(none), error));
    EXPECT_FALSE(gc_card_icon(card, 0, rgba, error));
    SaveSpec past;
    past.name = "past";
    past.blocks = 1;
    past.image_offset = 0x1F00;
    past.comment_offset = 0;
    EXPECT_TRUE(gc_card_import(card, make_gci(past), error));
    EXPECT_FALSE(gc_card_icon(card, 1, rgba, error));
    SaveSpec wrapping;
    wrapping.name = "wrapping";
    wrapping.blocks = 1;
    wrapping.image_offset = 0xFFFFFF00;
    wrapping.comment_offset = 0;
    EXPECT_TRUE(gc_card_import(card, make_gci(wrapping), error));
    EXPECT_FALSE(gc_card_icon(card, 2, rgba, error));
    SaveSpec odd_banner;
    odd_banner.name = "odd banner";
    odd_banner.banner_format = 3;
    EXPECT_TRUE(gc_card_import(card, make_gci(odd_banner), error));
    EXPECT_FALSE(gc_card_icon(card, 3, rgba, error));
    EXPECT_FALSE(gc_card_icon(card, 50, rgba, error));
}

static void test_icon_texture() {
    Bytes rgba(4096, 0);
    // Pixel (5, 2): opaque red; pixel (0, 0): half see-through green.
    rgba[(2 * 32 + 5) * 4 + 0] = 0xFF;
    rgba[(2 * 32 + 5) * 4 + 3] = 0xFF;
    rgba[1] = 0xF0;
    rgba[3] = 0x80;
    const Bytes tiles = gc_icon_rgb5a3(rgba);
    EXPECT_EQ(tiles.size(), 2048u);
    // Pixel (5, 2) is in tile column 1, row 0 (tile 1), at (1, 2) inside it.
    const std::size_t red = (1 * 16 + 2 * 4 + 1) * 2;
    EXPECT_EQ(be16(&tiles[red]), 0xFC00);
    EXPECT_EQ(be16(&tiles[0]), 0x40F0);
    EXPECT_TRUE(gc_icon_rgb5a3(Bytes(100)).empty());
}

static void test_card_paths() {
    EXPECT_EQ(gc_card_path("GMSE01", false), std::string("sd:/riftwii/gc/cards/GMSE01.raw"));
    EXPECT_EQ(gc_card_path("GMSE01", true), std::string("sd:/riftwii/gc/cards/shared.raw"));
    EXPECT_EQ(gc_card_path("GMSP01", true), std::string("sd:/riftwii/gc/cards/shared.raw"));
    EXPECT_EQ(gc_card_path("GMSJ01", true), std::string("sd:/riftwii/gc/cards/shared-jp.raw"));
    // Not a plain ID: by its bytes, so no slash or dot reaches the name.
    EXPECT_EQ(gc_card_path("GM/../J", false), std::string("sd:/riftwii/gc/cards/x474d2f2e2e2f4a.raw"));
    EXPECT_FALSE(gc_card_shift_jis("GMS"));
}

// A card's text as UTF-8: Shift-JIS on a Japanese card, Windows-1252 on
// any other, '?' for what is not a character.
static void test_card_text() {
    // "ゼルダ" (katakana), "伝説" (first-level kanji), "ＡＢ" (full width).
    EXPECT_EQ(gc_card_text_utf8("\x83\x5B\x83\x8B\x83\x5F", true), std::string("\xE3\x82\xBC\xE3\x83\xAB\xE3\x83\x80"));
    EXPECT_EQ(gc_card_text_utf8("\x93\x60\x90\xE0", true), std::string("\xE4\xBC\x9D\xE8\xAA\xAC"));
    EXPECT_EQ(gc_card_text_utf8("\x82\x60\x82\x61", true), std::string("\xEF\xBC\xA1\xEF\xBC\xA2"));
    // Half-width katakana "ｱ", ASCII kept, the ideographic space.
    EXPECT_EQ(gc_card_text_utf8("A\xB1z", true), std::string("A\xEF\xBD\xB1z"));
    EXPECT_EQ(gc_card_text_utf8("\x81\x40", true), std::string("\xE3\x80\x80"));
    // A lead byte at the end, a bad trail byte, second-level kanji (not in
    // the table), a pair that is not a character, a control byte.
    EXPECT_EQ(gc_card_text_utf8("ab\x82", true), std::string("ab?"));
    EXPECT_EQ(gc_card_text_utf8("\x82\x20x", true), std::string("? x"));
    EXPECT_EQ(gc_card_text_utf8("\x9F\x40", true), std::string("?"));
    EXPECT_EQ(gc_card_text_utf8("\x85\x40", true), std::string("?"));
    EXPECT_EQ(gc_card_text_utf8("\xFD\x01", true), std::string("??"));
    // Windows-1252: Latin-1 letters, its own 0x80-0x9F, an unused byte.
    EXPECT_EQ(gc_card_text_utf8("Pok\xE9mon", false), std::string("Pok\xC3\xA9mon"));
    EXPECT_EQ(gc_card_text_utf8("\x80\x99", false), std::string("\xE2\x82\xAC\xE2\x84\xA2"));
    EXPECT_EQ(gc_card_text_utf8("\x81", false), std::string("?"));
    EXPECT_EQ(gc_card_text_utf8("", true), std::string());
}

// Backup names: made from the date and time, read back, sorted newest
// first, numbered within a minute, and anything else in the folder left alone.
static void test_backup_names() {
    EXPECT_EQ(gc_backup_folder("GMSE01", false), std::string("sd:/riftwii/gc/backups/GMSE01"));
    EXPECT_EQ(gc_backup_folder("GMSE01", true), std::string("sd:/riftwii/gc/backups/shared"));
    EXPECT_EQ(gc_backup_folder("GMSJ01", true), std::string("sd:/riftwii/gc/backups/shared-jp"));
    EXPECT_EQ(gc_backup_folder("GM/../J", false), std::string("sd:/riftwii/gc/backups/x474d2f2e2e2f4a"));

    GcBackupStamp stamp;
    stamp.year = 2026;
    stamp.month = 10;
    stamp.day = 7;
    stamp.hour = 9;
    stamp.minute = 5;
    EXPECT_EQ(gc_backup_name(stamp), std::string("2026-10-07_0905.raw"));
    stamp.serial = 3;
    EXPECT_EQ(gc_backup_name(stamp), std::string("2026-10-07_0905-3.raw"));

    std::tm local{};
    local.tm_year = 126;
    local.tm_mon = 9;
    local.tm_mday = 17;
    local.tm_hour = 14;
    local.tm_min = 32;
    EXPECT_EQ(gc_backup_name(gc_backup_stamp(local)), std::string("2026-10-17_1432.raw"));

    GcBackupStamp back;
    EXPECT_TRUE(gc_backup_parse("2026-10-17_1432.raw", back));
    EXPECT_EQ(back.year, 2026u);
    EXPECT_EQ(back.month, 10u);
    EXPECT_EQ(back.day, 17u);
    EXPECT_EQ(back.hour, 14u);
    EXPECT_EQ(back.minute, 32u);
    EXPECT_EQ(back.serial, 0u);
    EXPECT_TRUE(gc_backup_parse("2026-10-17_1432-12.raw", back));
    EXPECT_EQ(back.serial, 12u);
    EXPECT_FALSE(gc_backup_parse("._2026-10-17_1432.raw", back));
    EXPECT_FALSE(gc_backup_parse("2026-10-17_1432.gci", back));
    EXPECT_FALSE(gc_backup_parse("2026-13-17_1432.raw", back));
    EXPECT_FALSE(gc_backup_parse("2026-10-17_2460.raw", back));
    EXPECT_FALSE(gc_backup_parse("2026-10-17_1432-1.raw", back));
    EXPECT_FALSE(gc_backup_parse("2026-10-17_1432-02.raw", back));
    EXPECT_FALSE(gc_backup_parse("2026-10-17_1432 copy.raw", back));
    EXPECT_FALSE(gc_backup_parse("GMSE01.raw", back));

    // Newest first by date, time and number, never by the names' spelling
    // ("-2" sorts before ".raw" as text).
    const std::vector<std::string> names = {"2026-10-17_1432.raw", "notes.txt", "2026-10-17_1432-2.raw",
                                            "2025-12-31_2359.raw", "2026-10-18_0001.raw", "."};
    const std::vector<std::string> sorted = gc_backups_newest_first(names);
    EXPECT_EQ(sorted.size(), 4u);
    if (sorted.size() == 4) {
        EXPECT_EQ(sorted[0], std::string("2026-10-18_0001.raw"));
        EXPECT_EQ(sorted[1], std::string("2026-10-17_1432-2.raw"));
        EXPECT_EQ(sorted[2], std::string("2026-10-17_1432.raw"));
        EXPECT_EQ(sorted[3], std::string("2025-12-31_2359.raw"));
    }

    // A minute already taken gets the next free number.
    GcBackupStamp now;
    now.year = 2026;
    now.month = 10;
    now.day = 17;
    now.hour = 14;
    now.minute = 32;
    EXPECT_EQ(gc_backup_new_name({}, now), std::string("2026-10-17_1432.raw"));
    EXPECT_EQ(gc_backup_new_name(names, now), std::string("2026-10-17_1432-3.raw"));

    // Six backups and a stray file: the oldest one goes, the stray stays.
    const std::vector<std::string> six = {"2026-10-12_1000.raw", "2026-10-13_1000.raw", "2026-10-11_1000.raw",
                                          "2026-10-14_1000.raw", "2026-10-15_1000.raw", "2026-10-16_1000.raw",
                                          "readme.txt"};
    const std::vector<std::string> pruned = gc_backups_to_prune(six);
    EXPECT_EQ(pruned.size(), 1u);
    if (!pruned.empty()) EXPECT_EQ(pruned[0], std::string("2026-10-11_1000.raw"));
    EXPECT_TRUE(gc_backups_to_prune({"2026-10-12_1000.raw"}).empty());
}

static void write_file(const std::filesystem::path& path, const Bytes& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

static Bytes read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Backups on disk: made from a card, skipped while nothing changed, the
// oldest deleted past five, the new one kept even when the clock went
// back, and a card written over in place.
static void test_backup_files() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "riftwii_gcbackup_test";
    fs::remove_all(root);
    fs::create_directories(root);
    const std::string card = (root / "GMSE01.raw").string();
    const std::string folder = (root / "backups" / "GMSE01").string();

    GcBackupStamp now;
    now.year = 2026;
    now.month = 10;
    now.day = 17;
    now.hour = 14;
    now.minute = 32;

    // No card yet: nothing to back up, and no folder made.
    EXPECT_TRUE(gc_backup_card(card, folder, now).what == GcBackupResult::What::NoCard);
    EXPECT_FALSE(fs::exists(folder));

    // A card larger than one piece of a copy, its last byte changed per run.
    Bytes bytes(200000, 0x5A);
    write_file(card, bytes);
    GcBackupResult made = gc_backup_card(card, folder, now);
    EXPECT_TRUE(made.what == GcBackupResult::What::Made);
    EXPECT_EQ(made.name, std::string("2026-10-17_1432.raw"));
    EXPECT_EQ(made.bytes, 200000u);
    EXPECT_TRUE(read_file(fs::path(folder) / made.name) == bytes);
    EXPECT_TRUE(gc_files_same(card, (fs::path(folder) / made.name).string()));

    // Started again with the card unchanged: no second copy.
    now.minute = 40;
    GcBackupResult same = gc_backup_card(card, folder, now);
    EXPECT_TRUE(same.what == GcBackupResult::What::Same);
    EXPECT_EQ(same.name, std::string("2026-10-17_1432.raw"));
    EXPECT_EQ(gc_backup_list(folder).size(), 1u);

    // Five more changed starts: six would be kept, so the first goes.
    for (unsigned day = 18; day <= 22; ++day) {
        bytes.back() = static_cast<std::uint8_t>(day);
        write_file(card, bytes);
        now.day = day;
        made = gc_backup_card(card, folder, now);
        EXPECT_TRUE(made.what == GcBackupResult::What::Made);
    }
    EXPECT_EQ(made.pruned.size(), 1u);
    if (!made.pruned.empty()) EXPECT_EQ(made.pruned[0], std::string("2026-10-17_1432.raw"));
    std::vector<std::string> kept = gc_backup_list(folder);
    EXPECT_EQ(kept.size(), kGcBackupsKept);
    if (!kept.empty()) EXPECT_EQ(kept.front(), std::string("2026-10-22_1440.raw"));
    EXPECT_FALSE(fs::exists(fs::path(folder) / "2026-10-17_1432.raw"));

    // The same card at a minute that already has a backup but differs:
    // numbered, not written over.
    bytes.back() = 0x01;
    write_file(card, bytes);
    made = gc_backup_card(card, folder, now);
    EXPECT_EQ(made.name, std::string("2026-10-22_1440-2.raw"));

    // A clock set back to 2000: the new backup sorts oldest but stays,
    // and the oldest of the others makes room for it.
    bytes.back() = 0x02;
    write_file(card, bytes);
    GcBackupStamp old_clock;
    old_clock.year = 2000;
    old_clock.month = 1;
    old_clock.day = 1;
    made = gc_backup_card(card, folder, old_clock);
    EXPECT_TRUE(made.what == GcBackupResult::What::Made);
    EXPECT_TRUE(fs::exists(fs::path(folder) / made.name));
    EXPECT_EQ(gc_backup_list(folder).size(), kGcBackupsKept);

    // A stray file in the folder is neither listed nor deleted.
    write_file(fs::path(folder) / "notes.txt", Bytes(3, 1));
    bytes.back() = 0x03;
    write_file(card, bytes);
    now.day = 23;
    gc_backup_card(card, folder, now);
    EXPECT_TRUE(fs::exists(fs::path(folder) / "notes.txt"));
    EXPECT_EQ(gc_backup_list(folder).size(), kGcBackupsKept);

    // A restore of the oldest backup: its undo copy does not prune the very
    // backup being restored (BUG-18); the next oldest goes instead.
    {
        const std::vector<std::string> listed = gc_backup_list(folder);
        const std::string oldest = listed.back();
        const std::string next_oldest = listed[listed.size() - 2];
        bytes.back() = 0x04;
        write_file(card, bytes);
        now.day = 24;
        now.undo = true;
        made = gc_backup_card(card, folder, now, oldest);
        now.undo = false;
        EXPECT_TRUE(made.what == GcBackupResult::What::Made);
        EXPECT_TRUE(fs::exists(fs::path(folder) / oldest));
        EXPECT_FALSE(fs::exists(fs::path(folder) / next_oldest));
        EXPECT_EQ(gc_backup_list(folder).size(), kGcBackupsKept);
    }

    // Card names (MIN-04): a disc's ID as it is; one with other characters
    // by its bytes, never empty and never shared with another.
    EXPECT_EQ(gc_card_path("GALE01", false), std::string("sd:/riftwii/gc/cards/GALE01.raw"));
    EXPECT_EQ(gc_card_path("....", false), std::string("sd:/riftwii/gc/cards/x2e2e2e2e.raw"));
    EXPECT_TRUE(gc_card_path("GA-E01", false) != gc_card_path("GA_E01", false));
    EXPECT_TRUE(gc_card_path("", false) != std::string("sd:/riftwii/gc/cards/.raw"));
    EXPECT_EQ(gc_backup_folder("....", false), std::string("sd:/riftwii/gc/backups/x2e2e2e2e"));

    // Files that differ only in length, or are missing, are not the same.
    write_file(root / "short.raw", Bytes(199999, 0x5A));
    EXPECT_FALSE(gc_files_same(card, (root / "short.raw").string()));
    EXPECT_FALSE(gc_files_same(card, (root / "missing.raw").string()));

    // Written over in place, and made anew at another length.
    std::string error;
    const Bytes restored(200000, 0x11);
    EXPECT_TRUE(gc_overwrite_file(card, restored, error));
    EXPECT_TRUE(read_file(card) == restored);
    EXPECT_TRUE(gc_overwrite_file(card, Bytes(10, 0x22), error));
    EXPECT_EQ(read_file(card).size(), 10u);
    EXPECT_FALSE(fs::exists(card + ".tmp"));  // put in place, nothing left beside it

    // A copy whose source cannot be read leaves nothing behind.
    EXPECT_FALSE(gc_copy_file((root / "missing.raw").string(), (root / "out.raw").string(), error));
    EXPECT_FALSE(fs::exists(root / "out.raw"));
    fs::remove_all(root);
}

// A restore's undo copy: "-undo" written and read back, sorted among the
// others by its time, never sharing a minute and number with a plain one,
// and pruned with them.
static void test_backup_undo() {
    GcBackupStamp stamp;
    stamp.year = 2026;
    stamp.month = 10;
    stamp.day = 17;
    stamp.hour = 14;
    stamp.minute = 15;
    stamp.undo = true;
    EXPECT_EQ(gc_backup_name(stamp), std::string("2026-10-17_1415-undo.raw"));
    stamp.serial = 2;
    EXPECT_EQ(gc_backup_name(stamp), std::string("2026-10-17_1415-2-undo.raw"));

    GcBackupStamp back;
    EXPECT_TRUE(gc_backup_parse("2026-10-17_1415-undo.raw", back));
    EXPECT_TRUE(back.undo);
    EXPECT_EQ(back.serial, 0u);
    EXPECT_EQ(back.minute, 15u);
    EXPECT_TRUE(gc_backup_parse("2026-10-17_1415-2-undo.raw", back));
    EXPECT_TRUE(back.undo);
    EXPECT_EQ(back.serial, 2u);
    EXPECT_TRUE(gc_backup_parse("2026-10-17_1415-2.raw", back));
    EXPECT_FALSE(back.undo);
    EXPECT_FALSE(gc_backup_parse("2026-10-17_1415-undo-2.raw", back));
    EXPECT_FALSE(gc_backup_parse("2026-10-17_1415-1-undo.raw", back));
    EXPECT_FALSE(gc_backup_parse("2026-10-17_1415undo.raw", back));
    EXPECT_FALSE(gc_backup_parse("-undo.raw", back));

    // A plain backup at 14:15 takes the minute: the undo copy is numbered 2,
    // and sorts after it.
    stamp.serial = 0;
    const std::vector<std::string> names = {"2026-10-17_1415.raw", "2026-10-16_2000.raw"};
    const std::string undo_name = gc_backup_new_name(names, stamp);
    EXPECT_EQ(undo_name, std::string("2026-10-17_1415-2-undo.raw"));
    std::vector<std::string> with_undo = names;
    with_undo.push_back(undo_name);
    const std::vector<std::string> sorted = gc_backups_newest_first(with_undo);
    EXPECT_EQ(sorted.size(), 3u);
    if (!sorted.empty()) EXPECT_EQ(sorted[0], undo_name);
    // And the other way round: a plain one after an undo copy is numbered too.
    stamp.undo = false;
    EXPECT_EQ(gc_backup_new_name({"2026-10-17_1415-undo.raw"}, stamp), std::string("2026-10-17_1415-2.raw"));

    // Undo copies count among the five kept.
    const std::vector<std::string> six = {"2026-10-11_1000.raw", "2026-10-12_1000-undo.raw", "2026-10-13_1000.raw",
                                          "2026-10-14_1000.raw", "2026-10-15_1000-undo.raw", "2026-10-16_1000.raw"};
    const std::vector<std::string> pruned = gc_backups_to_prune(six);
    EXPECT_EQ(pruned.size(), 1u);
    if (!pruned.empty()) EXPECT_EQ(pruned[0], std::string("2026-10-11_1000.raw"));

    // An undo copy made on disk, and the plain backup after it numbered.
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "riftwii_gcbackup_undo_test";
    fs::remove_all(root);
    fs::create_directories(root);
    const std::string card = (root / "card.raw").string();
    const std::string folder = (root / "backups").string();
    write_file(card, Bytes(1000, 0x33));
    stamp.undo = true;
    GcBackupResult made = gc_backup_card(card, folder, stamp);
    EXPECT_EQ(made.name, std::string("2026-10-17_1415-undo.raw"));
    write_file(card, Bytes(1000, 0x44));
    stamp.undo = false;
    made = gc_backup_card(card, folder, stamp);
    EXPECT_EQ(made.name, std::string("2026-10-17_1415-2.raw"));
    const std::vector<std::string> listed = gc_backup_list(folder);
    EXPECT_EQ(listed.size(), 2u);
    if (listed.size() == 2) EXPECT_EQ(listed[1], std::string("2026-10-17_1415-undo.raw"));
    fs::remove_all(root);
}

// Bringing Dolphin's saves in: which regions a card takes, which names may
// be joined onto a folder, the scan of a made-up GC folder (both layouts,
// the region filter, junk files, the limits), and the counting of saves
// added, skipped and not fitted.
static void test_dolphin_import() {
    namespace fs = std::filesystem;
    const auto regions_of = [](const char* id, bool shared) {
        std::string names;
        for (GcDolphinRegion region : gc_dolphin_regions(id, shared)) names += std::string(gc_dolphin_region_name(region)) + " ";
        return names;
    };
    EXPECT_EQ(regions_of("GALE01", false), std::string("USA "));
    EXPECT_EQ(regions_of("GALP01", false), std::string("EUR "));
    EXPECT_EQ(regions_of("GALJ01", false), std::string("JAP "));
    EXPECT_EQ(regions_of("GALK01", false), std::string("JAP "));
    EXPECT_EQ(regions_of("GALE01", true), std::string("USA EUR "));
    EXPECT_EQ(regions_of("GALJ01", true), std::string("JAP "));
    EXPECT_EQ(regions_of("", false), std::string("EUR "));

    EXPECT_TRUE(gc_import_name_ok("01-GALE-Super Smash Bros.gci"));
    EXPECT_FALSE(gc_import_name_ok(""));
    EXPECT_FALSE(gc_import_name_ok("."));
    EXPECT_FALSE(gc_import_name_ok(".."));
    EXPECT_FALSE(gc_import_name_ok("._01-GALE.gci"));
    EXPECT_FALSE(gc_import_name_ok("../escape.gci"));
    EXPECT_FALSE(gc_import_name_ok("a/b.gci"));
    EXPECT_FALSE(gc_import_name_ok("a\\b.gci"));
    EXPECT_FALSE(gc_import_name_ok("sd:evil.gci"));
    EXPECT_FALSE(gc_import_name_ok(std::string("bad\x01name.gci")));
    EXPECT_FALSE(gc_import_name_ok(std::string(256, 'a')));

    const fs::path root = fs::temp_directory_path() / "riftwii_dolphin_import_test";
    fs::remove_all(root);
    EXPECT_FALSE(gc_dolphin_folder_exists((root / "none").string()));
    EXPECT_FALSE(gc_dolphin_scan((root / "none").string(), {GcDolphinRegion::Usa}).folder_exists);

    // Dolphin's own layout, as copied: dolphin/GC/USA/Card A and so on.
    const fs::path gc = root / "dolphin" / "GC";
    fs::create_directories(gc / "USA" / "Card A");
    fs::create_directories(gc / "USA" / "Card B");
    fs::create_directories(gc / "EUR" / "Card A");
    fs::create_directories(gc / "JAP" / "Card A");
    SaveSpec a, b;
    a.name = "alpha";
    a.blocks = 2;
    b.name = "beta";
    b.blocks = 1;
    write_file(gc / "USA" / "Card A" / "01-GZLE-alpha.gci", make_gci(a));
    write_file(gc / "USA" / "Card A" / "01-GZLE-beta.GCI", make_gci(b));
    write_file(gc / "USA" / "Card A" / "notes.txt", Bytes(4, 0));
    write_file(gc / "USA" / "Card A" / "._01-GZLE-alpha.gci", Bytes(4, 0));
    write_file(gc / "USA" / "Card B" / "01-GZLE-other.gci", make_gci(a));
    write_file(gc / "EUR" / "Card A" / "01-GZLP-eur.gci", make_gci(a));
    write_file(gc / "JAP" / "Card A" / "01-GZLJ-jap.gci", make_gci(a));
    write_file(gc / "MemoryCardA.USA.raw", blank_card());
    write_file(gc / "MemoryCardA.EUR.raw", blank_card());
    write_file(gc / "MemoryCardA.JAP.raw", blank_card());
    write_file(gc / "mine.raw", blank_card());
    write_file(gc / "readme.md", Bytes(4, 0));

    const auto leaf = [&](const std::vector<std::string>& paths) {
        std::string out;
        for (const std::string& path : paths) out += fs::path(path).filename().string() + "|";
        return out;
    };
    GcDolphinFiles usa = gc_dolphin_scan((root / "dolphin").string(), {GcDolphinRegion::Usa});
    EXPECT_TRUE(usa.folder_exists);
    EXPECT_EQ(leaf(usa.gci), std::string("01-GZLE-alpha.gci|01-GZLE-beta.GCI|01-GZLE-other.gci|"));
    EXPECT_EQ(leaf(usa.raw), std::string("MemoryCardA.USA.raw|mine.raw|"));
    EXPECT_FALSE(usa.truncated);
    // A shared Western card takes USA and Europe; a Japanese one only Japan's.
    GcDolphinFiles west = gc_dolphin_scan((root / "dolphin").string(), {GcDolphinRegion::Usa, GcDolphinRegion::Eur});
    EXPECT_EQ(west.gci.size(), 4u);
    EXPECT_EQ(west.raw.size(), 3u);
    GcDolphinFiles jap = gc_dolphin_scan((root / "dolphin").string(), {GcDolphinRegion::Jap});
    EXPECT_EQ(leaf(jap.gci), std::string("01-GZLJ-jap.gci|"));
    EXPECT_EQ(leaf(jap.raw), std::string("MemoryCardA.JAP.raw|mine.raw|"));
    // The GC folder itself put in the place, and a folder with no saves for the region.
    GcDolphinFiles direct = gc_dolphin_scan(gc.string(), {GcDolphinRegion::Eur});
    EXPECT_EQ(leaf(direct.gci), std::string("01-GZLP-eur.gci|"));
    fs::create_directories(root / "empty");
    GcDolphinFiles none = gc_dolphin_scan((root / "empty").string(), {GcDolphinRegion::Usa});
    EXPECT_TRUE(none.folder_exists);
    EXPECT_TRUE(none.gci.empty() && none.raw.empty());

    // More files than the limit: the first kGcDolphinMaxGci, and truncated.
    fs::create_directories(root / "many" / "USA" / "Card A");
    for (std::size_t i = 0; i < kGcDolphinMaxGci + 5; ++i)
        write_file(root / "many" / "USA" / "Card A" / ("save" + std::to_string(1000 + i) + ".gci"), Bytes(1, 0));
    GcDolphinFiles many = gc_dolphin_scan((root / "many").string(), {GcDolphinRegion::Usa});
    EXPECT_EQ(many.gci.size(), kGcDolphinMaxGci);
    EXPECT_TRUE(many.truncated);
    fs::remove_all(root);

    // Counting: two new saves, one again (skipped), a bad file, a short one.
    Bytes card = blank_card();
    GcImportCount count;
    gc_import_one(card, make_gci(a), count);
    gc_import_one(card, make_gci(b), count);
    gc_import_one(card, make_gci(a), count);
    EXPECT_EQ(count.added, 2u);
    EXPECT_EQ(count.skipped, 1u);
    EXPECT_EQ(count.unusable, 0u);
    EXPECT_FALSE(count.full);
    gc_import_one(card, Bytes(10, 0), count);
    Bytes wrong_length = make_gci(b);
    wrong_length.pop_back();
    gc_import_one(card, wrong_length, count);
    EXPECT_EQ(count.unusable, 2u);
    EXPECT_EQ(read_ok(card).saves.size(), 2u);
    // Same game and name from another maker is another save.
    SaveSpec other_maker = a;
    other_maker.maker = "08";
    gc_import_one(card, make_gci(other_maker), count);
    EXPECT_EQ(count.added, 3u);

    // A Dolphin card file: its saves come in, the ones already there are skipped.
    Bytes dolphin_card = blank_card();
    std::string error;
    SaveSpec c;
    c.name = "gamma";
    c.blocks = 4;
    EXPECT_TRUE(gc_card_import(dolphin_card, make_gci(a), error));
    EXPECT_TRUE(gc_card_import(dolphin_card, make_gci(c), error));
    GcImportCount from_card;
    gc_import_card(card, dolphin_card, from_card);
    EXPECT_EQ(from_card.added, 1u);
    EXPECT_EQ(from_card.skipped, 1u);
    EXPECT_EQ(read_ok(card).saves.size(), 4u);
    GcImportCount unreadable;
    gc_import_card(card, Bytes(100, 0), unreadable);
    EXPECT_EQ(unreadable.unusable, 1u);

    // A card that fills: the saves that fit go in, then it stops, and the
    // rest, even a small one, is not tried.
    Bytes small = blank_card();
    GcImportCount filling;
    SaveSpec huge;
    huge.blocks = 200;
    huge.name = "huge";
    gc_import_one(small, make_gci(huge), filling);
    EXPECT_EQ(filling.added, 1u);
    SaveSpec too_big;
    too_big.blocks = 60;
    too_big.name = "too big";
    gc_import_one(small, make_gci(too_big), filling);
    EXPECT_TRUE(filling.full);
    EXPECT_EQ(filling.added, 1u);
    SaveSpec tiny;
    tiny.blocks = 1;
    tiny.name = "tiny";
    gc_import_one(small, make_gci(tiny), filling);
    EXPECT_EQ(filling.added, 1u);
    EXPECT_EQ(read_ok(small).saves.size(), 1u);
    // A card with every directory slot taken is full too.
    Bytes slots = blank_card();
    GcImportCount many_saves;
    for (unsigned i = 0; i < kGcCardMaxEntries + 3; ++i) {
        SaveSpec s;
        s.name = "s" + std::to_string(i);
        s.blocks = 1;
        gc_import_one(slots, make_gci(s), many_saves);
    }
    EXPECT_EQ(many_saves.added, kGcCardMaxEntries);
    EXPECT_TRUE(many_saves.full);
}

// The limits on a folder that is not trusted: raw cards capped, a flood of
// directory entries cut off (and reported), results sorted, and input over
// the size caps refused by the library itself.
static void test_dolphin_limits() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "riftwii_dolphin_limits_test";
    fs::remove_all(root);
    fs::create_directories(root / "raw");
    for (std::size_t i = 0; i < kGcDolphinMaxRaw + 3; ++i)
        write_file(root / "raw" / ("card" + std::to_string(i) + ".raw"), Bytes(1, 0));
    GcDolphinFiles raws = gc_dolphin_scan((root / "raw").string(), {GcDolphinRegion::Usa});
    EXPECT_EQ(raws.raw.size(), kGcDolphinMaxRaw);
    EXPECT_TRUE(raws.truncated);
    EXPECT_TRUE(std::is_sorted(raws.raw.begin(), raws.raw.end()));

    // More entries than are looked at: the folder is cut off and says so.
    fs::create_directories(root / "flood" / "USA" / "Card A");
    for (std::size_t i = 0; i < kGcDolphinMaxEntries + 10; ++i)
        write_file(root / "flood" / "USA" / "Card A" / ("n" + std::to_string(i) + ".txt"), Bytes());
    GcDolphinFiles flood = gc_dolphin_scan((root / "flood").string(), {GcDolphinRegion::Usa});
    EXPECT_TRUE(flood.truncated);
    EXPECT_TRUE(flood.gci.empty());

    // Sorted across Card A and Card B and the two layouts.
    fs::create_directories(root / "order" / "GC" / "USA" / "Card B");
    fs::create_directories(root / "order" / "USA" / "Card A");
    write_file(root / "order" / "GC" / "USA" / "Card B" / "a.gci", Bytes(1, 0));
    write_file(root / "order" / "USA" / "Card A" / "z.gci", Bytes(1, 0));
    GcDolphinFiles order = gc_dolphin_scan((root / "order").string(), {GcDolphinRegion::Usa});
    EXPECT_EQ(order.gci.size(), 2u);
    EXPECT_TRUE(std::is_sorted(order.gci.begin(), order.gci.end()));
    fs::remove_all(root);

    // Oversize input is unusable, not read.
    Bytes card = blank_card();
    GcImportCount count;
    gc_import_one(card, Bytes(kGcDolphinMaxGciBytes + 1, 0x41), count);
    EXPECT_EQ(count.unusable, 1u);
    EXPECT_EQ(count.added, 0u);
    gc_import_card(card, Bytes(kGcDolphinMaxRawBytes + 1, 0), count);
    EXPECT_EQ(count.unusable, 2u);
    EXPECT_FALSE(count.full);
}

int main() {
    test_backup_undo();
    test_dolphin_import();
    test_dolphin_limits();
    test_backup_names();
    test_backup_files();
    test_card_text();
    test_card_paths();
    test_icon_texture();
    test_format();
    test_import_export_delete();
    test_directory_full();
    test_card_space();
    test_copies();
    test_damaged_cards();
    test_bad_gci();
    test_icons();
    if (g_failures == 0) std::cout << "gccard: all passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
