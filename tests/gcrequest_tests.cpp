// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The gamecube plugin's request (riftwii/plugin_gamecube.h,
// riftwii/gcrequest.hpp): a packs' plan flattened by the DOL comes back
// whole in the plugin, and the plugin refuses a request it cannot read
// before it uses any of it (docs/PLUGINS.md, "Stage (b)").
#include "riftwii/gcrequest.hpp"

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
}

int main() {
    test_layout();
    test_plan_round_trip();
    test_no_packs();
    test_refusals();
    if (g_failures == 0) std::cout << "gcrequest: all passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
