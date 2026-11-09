// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The gamecube plugin's request (riftwii/plugin_gamecube.h,
// riftwii/gcrequest.hpp): a packs' plan flattened by the DOL comes back
// whole in the plugin, and the plugin refuses a request it cannot read
// before it uses any of it (docs/PLUGINS.md, "Stage (b)").
#include "riftwii/gcrequest.hpp"
#include "riftwii/checkedalloc.hpp"
#include "rthid.h"

#include <cstddef>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

// The crash screen's size the plugin is built with (gc_crash.h's
// GC_CRASH_SCREEN_BYTES); any fixed number does for the check.
constexpr std::uint32_t kScreenBytes = 0x1000;

static GcOverlayPlan sample_plan() {
    GcOverlayPlan plan;
    plan.fst.assign(64, 0xAB);
    plan.header.assign(0x20, 0x11);
    plan.fst_offset = 0x57068000ull;  // after the files, as plan_gc_overlay puts it
    GcOverlayPlan::Entry image;
    image.vstart = 0x57060000ull;
    image.length = 0x4000;
    image.piece = GcPiece{GcPiece::Kind::Image, 0x4000, 0x1230000ull, 0};
    GcOverlayPlan::Entry device;
    device.vstart = 0x57064000ull;
    device.length = 0x1F0;
    device.piece = GcPiece{GcPiece::Kind::Device, 0x1F0, 0x123456789ull, 0x1A0};
    GcOverlayPlan::Entry zero;  // the padding place() adds, a Zero piece of no length of its own
    zero.vstart = 0x570641F0ull;
    zero.length = 0x10;
    GcOverlayPlan::Entry usb;  // a pack's bytes on the USB drive
    usb.vstart = 0x57064200ull;
    usb.length = 0x300;
    usb.piece = GcPiece{GcPiece::Kind::UsbDevice, 0x300, 0x2345678ull, 0x40};
    plan.entries = {image, device, zero, usb};
    return plan;
}

// A request that passes the check: a game, an error buffer, nothing else.
struct Fixture {
    rwp_gc_launch request{};
    char error[64] = "";
    Fixture() {
        request.size = sizeof(request);
        request.path = "sd:/games/Luigi/game.iso";
        request.id = "GLME01";
        request.language = -1;
        request.error = error;
        request.error_bytes = sizeof(error);
    }
    bool check(std::string& why) const { return gc_request_check(&request, sizeof(request), kScreenBytes, why); }
};

static void test_layout() {
    // Its size first, as every table and request across the plugin line.
    EXPECT_EQ(offsetof(rwp_gc_launch, size), 0u);
    // Fixed-width fields only: the Wii's layout does not depend on a
    // compiler's choice for bool or enum.
    EXPECT_EQ(sizeof(rwp_gc_pack_entry), 32u);
    EXPECT_TRUE(offsetof(rwp_gc_launch, path) < offsetof(rwp_gc_launch, pack_fst));
    EXPECT_TRUE(offsetof(rwp_gc_launch, pack_entry_count) < offsetof(rwp_gc_launch, rumble_off));
    EXPECT_TRUE(offsetof(rwp_gc_launch, code_handler_bytes) < offsetof(rwp_gc_launch, crash_screen));
    EXPECT_TRUE(offsetof(rwp_gc_launch, way_back_view_bytes) < offsetof(rwp_gc_launch, error));
    // The out code is last, and a request is checked against the whole struct, so a DOL
    // and a plugin of different builds still refuse each other.
    EXPECT_EQ(offsetof(rwp_gc_launch, error_code) + sizeof(void*), sizeof(rwp_gc_launch));
}

static void test_plan_round_trip() {
    const GcOverlayPlan plan = sample_plan();
    Fixture f;
    std::vector<rwp_gc_pack_entry> entries;
    gc_plan_to_request(plan, entries, f.request);
    EXPECT_EQ(f.request.pack_fst_bytes, 64u);
    EXPECT_EQ(f.request.pack_header_bytes, 0x20u);
    EXPECT_EQ(f.request.pack_entry_count, 4u);
    EXPECT_TRUE(f.request.pack_entries == entries.data());
    EXPECT_EQ(entries[1].kind, RWP_GC_PIECE_DEVICE);
    EXPECT_EQ(entries[2].kind, RWP_GC_PIECE_ZERO);
    EXPECT_EQ(entries[3].kind, RWP_GC_PIECE_USB);
    EXPECT_TRUE(gc_plan_uses_usb(plan));
    std::string why;
    EXPECT_TRUE(f.check(why));

    GcOverlayPlan back;
    EXPECT_TRUE(gc_plan_from_request(f.request, back, why));
    EXPECT_TRUE(back.fst == plan.fst);
    EXPECT_TRUE(back.header == plan.header);
    EXPECT_EQ(back.fst_offset, plan.fst_offset);
    EXPECT_EQ(back.entries.size(), plan.entries.size());
    for (std::size_t i = 0; i < back.entries.size() && i < plan.entries.size(); ++i) {
        const GcOverlayPlan::Entry& a = plan.entries[i];
        const GcOverlayPlan::Entry& b = back.entries[i];
        EXPECT_EQ(b.vstart, a.vstart);
        EXPECT_EQ(b.length, a.length);
        EXPECT_TRUE(b.piece.kind == a.piece.kind);
        EXPECT_EQ(b.piece.source, a.piece.source);
        EXPECT_EQ(b.piece.skip, a.piece.skip);
    }
    // The runs the loader serves are the same from either plan.
    std::vector<GcRun> runs_before, runs_after;
    EXPECT_TRUE(gc_disc_runs_rvz(0x57058000ull, &plan, runs_before, why));
    EXPECT_TRUE(gc_disc_runs_rvz(0x57058000ull, &back, runs_after, why));
    EXPECT_EQ(runs_after.size(), runs_before.size());
    // The USB piece is a Usb run: its sector, of the drive.
    bool usb_run = false;
    for (const GcRun& r : runs_after)
        if (r.kind == GcRun::Kind::Usb && r.sector == 0x2345678ull && r.skip == 0x40 && r.length == 0x300) usb_run = true;
    EXPECT_TRUE(usb_run);
    for (std::size_t i = 0; i < runs_after.size() && i < runs_before.size(); ++i) {
        EXPECT_TRUE(runs_after[i].kind == runs_before[i].kind);
        EXPECT_EQ(runs_after[i].disc, runs_before[i].disc);
        EXPECT_EQ(runs_after[i].length, runs_before[i].length);
        EXPECT_EQ(runs_after[i].sector, runs_before[i].sector);
        EXPECT_EQ(runs_after[i].skip, runs_before[i].skip);
        EXPECT_EQ(runs_after[i].image_offset, runs_before[i].image_offset);
    }

    // An unknown kind is refused, by number.
    entries[1].kind = 7;
    EXPECT_FALSE(gc_plan_from_request(f.request, back, why));
    EXPECT_TRUE(why.find("unknown kind 7") != std::string::npos);
}

static void test_no_packs() {
    // A game with no packs: no plan, and none of its fields set.
    Fixture f;
    std::string why;
    EXPECT_TRUE(f.check(why));
    EXPECT_TRUE(f.request.pack_fst == nullptr);
    // A plan's parts without its FST are refused.
    f.request.pack_entry_count = 1;
    EXPECT_FALSE(f.check(why));
    EXPECT_TRUE(why.find("without its FST") != std::string::npos);
}

static void test_memory_round_trip() {
    // Every kind of <memory> patch comes back as it went: a plain one with
    // an original, a search with an odd-sized value (padding), an ocarina
    // one, and one with no offset.
    std::vector<MemoryPatch> patches(4);
    patches[0].has_offset = true;
    patches[0].offset = 0x80001820;
    patches[0].value = {0x94, 0x21, 0xFF, 0xF0};
    patches[0].original = {0x7C, 0x08, 0x02, 0xA6};
    patches[1].search = true;
    patches[1].align = 4;
    patches[1].value = {1, 2, 3, 4, 5};
    patches[1].original = {9, 9, 9};
    patches[2].ocarina = true;
    patches[2].has_offset = true;
    patches[2].offset = 0x80002400;
    patches[2].value = {0x38, 0x60, 0x00, 0x01};
    patches[3].value = {0xAA};
    std::vector<std::uint8_t> bytes;
    std::string why;
    EXPECT_TRUE(gc_memory_to_bytes(patches, bytes, why));
    EXPECT_EQ(bytes.size() % 4, 0u);
    Fixture f;
    f.request.pack_memory = bytes.data();
    f.request.pack_memory_bytes = static_cast<std::uint32_t>(bytes.size());
    f.request.pack_memory_count = static_cast<std::uint32_t>(patches.size());
    EXPECT_TRUE(f.check(why));
    std::vector<MemoryPatch> back;
    EXPECT_TRUE(gc_memory_from_request(f.request, back, why));
    EXPECT_EQ(back.size(), patches.size());
    for (std::size_t i = 0; i < back.size() && i < patches.size(); ++i) {
        EXPECT_EQ(back[i].offset, patches[i].offset);
        EXPECT_EQ(back[i].has_offset, patches[i].has_offset);
        EXPECT_EQ(back[i].search, patches[i].search);
        EXPECT_EQ(back[i].ocarina, patches[i].ocarina);
        EXPECT_EQ(back[i].align, patches[i].align);
        EXPECT_TRUE(back[i].value == patches[i].value);
        EXPECT_TRUE(back[i].original == patches[i].original);
    }
    // A count that disagrees with the bytes, either way, is refused.
    f.request.pack_memory_count = 5;
    EXPECT_FALSE(gc_memory_from_request(f.request, back, why));
    f.request.pack_memory_count = 3;
    EXPECT_FALSE(gc_memory_from_request(f.request, back, why));
    // So are bytes without a count, a count without bytes, and more
    // patches than the bytes could hold heads for.
    f.request.pack_memory_count = 0;
    EXPECT_FALSE(f.check(why));
    f.request.pack_memory_count = 4;
    f.request.pack_memory = nullptr;
    EXPECT_FALSE(f.check(why));
    f.request.pack_memory = bytes.data();
    f.request.pack_memory_count = static_cast<std::uint32_t>(bytes.size() / 20 + 1);
    EXPECT_FALSE(f.check(why));
    // A length that runs past the end.
    std::vector<std::uint8_t> cut(bytes.begin(), bytes.begin() + 24);
    f.request.pack_memory = cut.data();
    f.request.pack_memory_bytes = static_cast<std::uint32_t>(cut.size());
    f.request.pack_memory_count = 1;
    EXPECT_FALSE(gc_memory_from_request(f.request, back, why));
    // The DOL side refuses an offset past 32 bits and a valuefile not read.
    std::vector<MemoryPatch> wide(1);
    wide[0].has_offset = true;
    wide[0].offset = 0x100000000ull;
    wide[0].value = {1};
    EXPECT_FALSE(gc_memory_to_bytes(wide, bytes, why));
    std::vector<MemoryPatch> unread(1);
    unread[0].valuefile = "/mod/code.bin";
    EXPECT_FALSE(gc_memory_to_bytes(unread, bytes, why));
    // No patches: no bytes, and a request without them still checks.
    EXPECT_TRUE(gc_memory_to_bytes({}, bytes, why));
    EXPECT_TRUE(bytes.empty());
    Fixture none;
    EXPECT_TRUE(none.check(why));
    EXPECT_TRUE(gc_memory_from_request(none.request, back, why));
    EXPECT_TRUE(back.empty());
}

static void test_refusals() {
    std::string why;
    // The size: shorter (an older DOL), or a size field that disagrees.
    {
        Fixture f;
        EXPECT_FALSE(gc_request_check(&f.request, sizeof(f.request) - 4, kScreenBytes, why));
        EXPECT_FALSE(gc_request_check(nullptr, sizeof(f.request), kScreenBytes, why));
        f.request.size = sizeof(f.request) + 4;
        EXPECT_FALSE(f.check(why));
        EXPECT_TRUE(why.find("size") != std::string::npos);
    }
    // No game, no room for a reason.
    {
        Fixture f;
        f.request.path = "";
        EXPECT_FALSE(f.check(why));
        f.request.path = nullptr;
        EXPECT_FALSE(f.check(why));
        Fixture g;
        g.request.id = nullptr;
        EXPECT_FALSE(g.check(why));
        Fixture h;
        h.request.error_bytes = 0;
        EXPECT_FALSE(h.check(why));
    }
    // A plan with a short header, no FST bytes, or entries without a count.
    {
        const GcOverlayPlan plan = sample_plan();
        Fixture f;
        std::vector<rwp_gc_pack_entry> entries;
        gc_plan_to_request(plan, entries, f.request);
        f.request.pack_header_bytes = 0x1C;
        EXPECT_FALSE(f.check(why));
        EXPECT_TRUE(why.find("not whole") != std::string::npos);
        gc_plan_to_request(plan, entries, f.request);
        f.request.pack_fst_bytes = 0;
        EXPECT_FALSE(f.check(why));
        gc_plan_to_request(plan, entries, f.request);
        f.request.pack_entry_count = 0;
        EXPECT_FALSE(f.check(why));
    }
    // A corrupt or mismatched DOL's huge counts are refused before anything
    // is copied from them or read through them.
    {
        const std::uint8_t fst[64] = {};
        const std::uint8_t header[0x20] = {};
        const rwp_gc_pack_entry entries[2] = {};
        Fixture f;
        f.request.pack_fst = fst;
        f.request.pack_fst_bytes = 0xFFFFFFFFu;
        f.request.pack_header = header;
        f.request.pack_header_bytes = sizeof(header);
        f.request.pack_entries = entries;
        f.request.pack_entry_count = 2;
        EXPECT_FALSE(f.check(why));
        EXPECT_TRUE(why.find("not whole") != std::string::npos);
        f.request.pack_fst_bytes = sizeof(fst);
        f.request.pack_entry_count = 0x40000000u;
        EXPECT_FALSE(f.check(why));
        // A count the request's own entries fit under the cap, but the
        // plan's larger entries would not.
        static_assert(sizeof(GcOverlayPlan::Entry) > sizeof(rwp_gc_pack_entry), "the plan's entries are the larger");
        f.request.pack_entry_count = static_cast<std::uint32_t>(kMaxCheckedBytes / sizeof(rwp_gc_pack_entry));
        EXPECT_FALSE(f.check(why));
        f.request.pack_entry_count = static_cast<std::uint32_t>(kMaxCheckedBytes / sizeof(GcOverlayPlan::Entry));
        EXPECT_TRUE(f.check(why));
        f.request.pack_entry_count = 2;
        EXPECT_TRUE(f.check(why));
    }
    // Cheats and the code handler: a pointer with its bytes, or neither.
    {
        const std::uint8_t gct[16] = {};
        Fixture f;
        f.request.cheat_gct = gct;
        EXPECT_FALSE(f.check(why));
        f.request.cheat_gct_bytes = sizeof(gct);
        EXPECT_TRUE(f.check(why));
        f.request.code_handler_bytes = 0x1000;
        EXPECT_FALSE(f.check(why));
    }
    // The crash screen must be whole.
    {
        std::vector<std::uint8_t> screen(kScreenBytes);
        Fixture f;
        f.request.crash_screen = screen.data();
        f.request.crash_screen_bytes = kScreenBytes;
        EXPECT_TRUE(f.check(why));
        f.request.crash_screen_bytes = kScreenBytes - 1;
        EXPECT_FALSE(f.check(why));
        EXPECT_TRUE(why.find("crash screen") != std::string::npos);
    }
    // The way back: a known kind, and a view with any kind but none.
    {
        const std::uint8_t view[0xD8] = {};
        Fixture f;
        f.request.way_back_kind = RWP_GC_WAY_BACK_CHANNEL;
        EXPECT_FALSE(f.check(why));
        f.request.way_back_view = view;
        f.request.way_back_view_bytes = sizeof(view);
        EXPECT_TRUE(f.check(why));
        f.request.way_back_kind = RWP_GC_WAY_BACK_HBC + 1;
        EXPECT_FALSE(f.check(why));
    }
    // A second disc: its USB pieces with their count, and only with its path.
    {
        const rwp_fragment pieces[2] = {{100, 8}, {300, 8}};
        Fixture f;
        f.request.disc2_path = "usb:/games/Two Disc [GWYE41]/disc2.iso";
        EXPECT_TRUE(f.check(why));
        f.request.disc2_usb_fragments = pieces;
        EXPECT_FALSE(f.check(why));
        EXPECT_TRUE(why.find("second disc") != std::string::npos);
        f.request.disc2_usb_fragment_count = 2;
        f.request.disc2_usb_file_bytes = 8192;
        EXPECT_TRUE(f.check(why));
        f.request.disc2_path = nullptr;
        EXPECT_FALSE(f.check(why));
    }
}

static void test_external_card_request() {
    Fixture fixture;
    std::string why;
    std::vector<std::uint8_t> card(512u << 10);
    rwp_fragment part{100, card.size() / 512};
    fixture.request.external_card = card.data();
    fixture.request.external_card_bytes = static_cast<std::uint32_t>(card.size());
    fixture.request.card_fragments = &part;
    fixture.request.card_fragment_count = 1;
    EXPECT_TRUE(fixture.check(why));
    part.sector_count--;
    EXPECT_FALSE(fixture.check(why));
    part.sector_count++;
    fixture.request.card_on_usb = 1;
    fixture.request.usb_fd = -1;
    EXPECT_FALSE(fixture.check(why));
    fixture.request.usb_fd = 4;
    EXPECT_TRUE(fixture.check(why));
    fixture.request.real_card = RWP_GC_REAL_CARD_A;
    EXPECT_FALSE(fixture.check(why));
    // Slot B's real card goes beside the external card in slot A.
    fixture.request.real_card = RWP_GC_REAL_CARD_B;
    EXPECT_TRUE(fixture.check(why));
    // But not in its place: slot A is then the real card.
    fixture.request.real_card = RWP_GC_REAL_CARD_B_AS_A;
    EXPECT_FALSE(fixture.check(why));
    fixture.request.real_card = RWP_GC_REAL_CARD_NONE;
    part.sector = 0xFFFFFFFFull;
    EXPECT_FALSE(fixture.check(why));
    part.sector = 100;
    fixture.request.external_card_bytes++;
    EXPECT_FALSE(fixture.check(why));
}

static void test_screen_offset_request() {
    Fixture fixture;
    std::string why;
    fixture.request.screen_offset_set = 1;
    for (int offset : {-32, 0, 32}) {
        fixture.request.screen_offset = offset;
        EXPECT_TRUE(fixture.check(why));
    }
    for (int offset : {-33, 33}) {
        fixture.request.screen_offset = offset;
        EXPECT_FALSE(fixture.check(why));
    }
    // Not set: the value is not used, so it is not checked.
    fixture.request.screen_offset_set = 0;
    fixture.request.screen_offset = 100;
    EXPECT_TRUE(fixture.check(why));
}

static void test_slot_b_as_a_request() {
    Fixture fixture;
    std::string why;
    fixture.request.real_card = RWP_GC_REAL_CARD_B_AS_A;
    EXPECT_TRUE(fixture.check(why));
    // Slot A is the real card: no shared SD card there too.
    fixture.request.shared_card = 1;
    EXPECT_FALSE(fixture.check(why));
    fixture.request.shared_card = 0;
    fixture.request.real_card = RWP_GC_REAL_CARD_B_AS_A + 1;
    EXPECT_FALSE(fixture.check(why));
}

static void test_volume_boost_request() {
    Fixture fixture;
    std::string why;
    for (std::uint32_t boost : {0u, 3u, 6u}) {
        fixture.request.volume_boost_db = boost;
        EXPECT_TRUE(fixture.check(why));
    }
    // Only the option's own values: a damaged request is refused rather
    // than played louder than any choice the menu offers.
    for (std::uint32_t boost : {1u, 9u, 0xFFFFFFFFu}) {
        fixture.request.volume_boost_db = boost;
        EXPECT_FALSE(fixture.check(why));
    }
}

int main() {
    test_external_card_request();
    test_screen_offset_request();
    test_volume_boost_request();
    test_slot_b_as_a_request();
    {
        Fixture mappings;
        std::string error;
        rthid_catalog catalog{};
        mappings.request.hid_catalog = &catalog;
        EXPECT_FALSE(mappings.check(error));
        mappings.request.hid_catalog_bytes = sizeof(catalog) - 1;
        EXPECT_FALSE(mappings.check(error));
        mappings.request.hid_catalog_bytes = sizeof(catalog);
        EXPECT_TRUE(mappings.check(error));
        mappings.request.hid_catalog = nullptr;
        EXPECT_FALSE(mappings.check(error));
    }
    Fixture card_choice;
    std::string why;
    card_choice.request.real_card = 1;
    EXPECT_TRUE(card_choice.check(why));
    card_choice.request.shared_card = 1;
    EXPECT_FALSE(card_choice.check(why));
    card_choice.request.shared_card = 0;
    // Slot B's real card goes with the emulated card in slot A, shared or not.
    card_choice.request.real_card = RWP_GC_REAL_CARD_B;
    EXPECT_TRUE(card_choice.check(why));
    card_choice.request.shared_card = 1;
    EXPECT_TRUE(card_choice.check(why));
    card_choice.request.shared_card = 0;
    card_choice.request.real_card = RWP_GC_REAL_CARD_B_AS_A + 1;
    EXPECT_FALSE(card_choice.check(why));
    test_layout();
    test_plan_round_trip();
    test_no_packs();
    test_memory_round_trip();
    test_refusals();
    if (g_failures == 0) std::cout << "gcrequest: all passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
