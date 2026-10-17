// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// GameCube packs: a made-up image on a made-up SD card, a pack's files
// planned over it, and a reader that serves disc reads from the runs the
// way the GameCube loader's blob does (runtime/gc/gc_rt.c). The game's
// view (the FST, every file) is the oracle.
#include "riftwii/fst.hpp"
#include "riftwii/gcoverlay.hpp"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;
using Bytes = std::vector<std::uint8_t>;

static std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}
static void poke32(Bytes& b, std::size_t at, std::uint32_t v) {
    b[at] = std::uint8_t(v >> 24);
    b[at + 1] = std::uint8_t(v >> 16);
    b[at + 2] = std::uint8_t(v >> 8);
    b[at + 3] = std::uint8_t(v);
}
static Bytes pattern(std::size_t n, std::uint8_t seed) {
    Bytes b(n);
    for (std::size_t i = 0; i < n; ++i) b[i] = std::uint8_t(seed + i * 7 + (i >> 8));
    return b;
}

// A tiny disc: "/a.bin" at 0x10000 (0x100 bytes), "/dir/b.bin" at 0x20000
// (0x80 bytes), "/keep.bin" at 0x30000 (0x40 bytes). The FST sits at 0x8000.
struct Disc {
    Bytes image;
    Bytes fst;
};

static Disc make_disc() {
    Disc d;
    d.image.assign(0x40000, 0xEE);
    std::memcpy(d.image.data(), "GTSTE8", 6);
    d.image[6] = 0;  // disc 1
    d.image[7] = 2;  // version
    poke32(d.image, 0x1C, 0xC2339F3Du);
    // FST: root, a.bin, dir, b.bin, keep.bin
    const char* names[] = {"a.bin", "dir", "b.bin", "keep.bin"};
    Bytes strings;
    std::vector<std::uint32_t> name_offsets;
    for (const char* n : names) {
        name_offsets.push_back(std::uint32_t(strings.size()));
        strings.insert(strings.end(), n, n + std::strlen(n) + 1);
    }
    Bytes fst(5 * 12, 0);
    auto entry = [&](int i, bool dir, std::uint32_t name, std::uint32_t a, std::uint32_t b) {
        poke32(fst, i * 12, (dir ? 0x01000000u : 0) | name);
        poke32(fst, i * 12 + 4, a);
        poke32(fst, i * 12 + 8, b);
    };
    entry(0, true, 0, 0, 5);
    entry(1, false, name_offsets[0], 0x10000, 0x100);
    entry(2, true, name_offsets[1], 0, 4);
    entry(3, false, name_offsets[2], 0x20000, 0x80);
    entry(4, false, name_offsets[3], 0x30000, 0x40);
    fst.insert(fst.end(), strings.begin(), strings.end());
    d.fst = fst;
    std::memcpy(d.image.data() + 0x8000, fst.data(), fst.size());
    poke32(d.image, 0x420, 0x2440 + 0x2000);  // main.dol offset (unused here)
    poke32(d.image, 0x424, 0x8000);
    poke32(d.image, 0x428, std::uint32_t(fst.size()));
    poke32(d.image, 0x42C, std::uint32_t(fst.size()));
    Bytes a = pattern(0x100, 1), b = pattern(0x80, 2), k = pattern(0x40, 3);
    std::memcpy(d.image.data() + 0x10000, a.data(), a.size());
    std::memcpy(d.image.data() + 0x20000, b.data(), b.size());
    std::memcpy(d.image.data() + 0x30000, k.data(), k.size());
    return d;
}

// Writes `data` to the card at a sector + skip and returns it as a piece.
static GcPiece on_card(Bytes& card, std::uint64_t sector, std::uint32_t skip, const Bytes& data) {
    const std::uint64_t at = sector * 512 + skip;
    if (card.size() < at + data.size()) card.resize(at + data.size(), 0);
    std::memcpy(card.data() + at, data.data(), data.size());
    GcPiece p;
    p.kind = GcPiece::Kind::Device;
    p.source = sector;
    p.skip = skip;
    p.length = data.size();
    return p;
}

// The image on the card at sector 100 (one fragment), as the loader finds it.
constexpr std::uint64_t kImageSector = 100;

// Serves reads like gc_rt.c: each run from the card, zeros or the FST.
struct Reader {
    const Bytes* card = nullptr;
    const Bytes* fst = nullptr;
    std::vector<GcRun> runs;
    std::uint8_t byte(std::uint64_t at) const {
        for (const GcRun& r : runs) {
            if (at < r.disc || at >= r.disc + r.length) continue;
            const std::uint64_t into = at - r.disc;
            if (r.kind == GcRun::Kind::Zero) return 0;
            if (r.kind == GcRun::Kind::Fst) return fst->at(r.fst_offset + into);
            return card->at(r.sector * 512 + r.skip + into);
        }
        throw 1;  // a read past every run fails in the blob
    }
    Bytes read(std::uint64_t at, std::uint64_t n) const {
        Bytes out(n);
        for (std::uint64_t i = 0; i < n; ++i) out[i] = byte(at + i);
        return out;
    }
};

static std::vector<Fragment> put_image(Bytes& card, const Bytes& image) {
    const std::uint64_t at = kImageSector * 512;
    if (card.size() < at + image.size()) card.resize(at + image.size(), 0);
    std::memcpy(card.data() + at, image.data(), image.size());
    return {Fragment{kImageSector, (image.size() + 511) / 512}};
}

// Sorted, contiguous from 0: the blob fails a read that falls in a gap.
static void expect_contiguous(const std::vector<GcRun>& runs) {
    std::uint64_t at = 0;
    for (const GcRun& r : runs) {
        EXPECT_EQ(r.disc, at);
        at = r.disc + r.length;
    }
}

static Bytes file_through(const Reader& r, const Fst& fst, const std::string& path, bool& found) {
    const std::uint32_t i = fst.find(path, false);
    found = i != Fst::npos;
    if (!found) return {};
    return r.read(fst.entries()[i].offset, fst.entries()[i].size);
}

static void test_full_mod() {
    Disc d = make_disc();
    Bytes card;
    const std::vector<Fragment> image = put_image(card, d.image);
    const Bytes new_a = pattern(0x1234, 9);   // grows past its slot
    const Bytes new_c = pattern(0x77, 5);     // a created file, odd length
    const Bytes tail = pattern(0x20, 4);

    std::vector<GcFile> files(3);
    files[0].disc_path = "/A.BIN";  // the SDK's paths are case-insensitive
    files[0].pieces.push_back(on_card(card, 10, 100, new_a));
    files[1].disc_path = "/dir/new/c.txt";
    files[1].create = true;
    files[1].pieces.push_back(on_card(card, 40, 0, new_c));
    // b.bin: its first 0x40 original bytes, then new bytes from the card.
    files[2].disc_path = "/dir/b.bin";
    GcPiece orig;
    orig.kind = GcPiece::Kind::Image;
    orig.source = 0x20000;
    orig.length = 0x40;
    files[2].pieces.push_back(orig);
    files[2].pieces.push_back(on_card(card, 50, 3, tail));

    GcOverlayPlan plan;
    std::string error;
    Bytes boot(d.image.begin(), d.image.begin() + 0x440);
    EXPECT_TRUE(plan_gc_overlay(boot, d.fst, d.image.size(), files, plan, error));
    if (!error.empty()) std::cerr << error << std::endl;
    EXPECT_EQ(plan.fst.size() % 32, 0u);

    Reader r;
    r.card = &card;
    r.fst = &plan.fst;
    EXPECT_TRUE(gc_disc_runs(image, d.image.size(), &plan, r.runs, error));
    if (!error.empty()) std::cerr << error << std::endl;
    expect_contiguous(r.runs);

    // The header the loader uses names the new FST, which the view serves.
    const std::uint32_t fst_at = be32(plan.header.data() + 4), fst_size = be32(plan.header.data() + 8);
    EXPECT_EQ(be32(plan.header.data()), be32(d.image.data() + 0x420));
    EXPECT_TRUE(fst_at >= d.image.size());
    EXPECT_EQ(fst_size, plan.fst.size());
    EXPECT_TRUE(be32(plan.header.data() + 12) >= fst_size);
    EXPECT_TRUE(r.read(fst_at, fst_size) == plan.fst);
    Fst fst;
    EXPECT_TRUE(Fst::parse(plan.fst.data(), plan.fst.size(), false, fst, error));

    // The image itself reads as it is.
    EXPECT_TRUE(r.read(0, 0x440) == Bytes(d.image.begin(), d.image.begin() + 0x440));
    bool found = false;
    EXPECT_TRUE(file_through(r, fst, "/a.bin", found) == new_a);
    EXPECT_TRUE(found);
    EXPECT_TRUE(file_through(r, fst, "/dir/new/c.txt", found) == new_c);
    EXPECT_TRUE(found);
    Bytes want_b(d.image.begin() + 0x20000, d.image.begin() + 0x20040);
    want_b.insert(want_b.end(), tail.begin(), tail.end());
    EXPECT_TRUE(file_through(r, fst, "/dir/b.bin", found) == want_b);
    Bytes keep(d.image.begin() + 0x30000, d.image.begin() + 0x30040);
    EXPECT_TRUE(file_through(r, fst, "/keep.bin", found) == keep);
    EXPECT_TRUE(found);

    // Every moved file's tail up to 32 bytes reads as zero (the DVD driver
    // rounds reads up), and so does the space between files.
    const std::uint32_t c = fst.find("/dir/new/c.txt", false);
    const Bytes rounded = r.read(fst.entries()[c].offset, (fst.entries()[c].size + 31) & ~31u);
    for (std::size_t i = new_c.size(); i < rounded.size(); ++i) EXPECT_EQ(int(rounded[i]), 0);
    const std::uint32_t a = fst.find("/a.bin", false);
    EXPECT_EQ(int(r.byte(fst.entries()[a].offset + 0x1240)), 0);
}

static void test_refusals() {
    Disc d = make_disc();
    Bytes boot(d.image.begin(), d.image.begin() + 0x440);
    GcOverlayPlan plan;
    std::string error;
    Bytes card;

    std::vector<GcFile> exists(1);
    exists[0].disc_path = "/keep.bin";
    exists[0].create = true;
    exists[0].pieces.push_back(on_card(card, 1, 0, pattern(4, 1)));
    EXPECT_FALSE(plan_gc_overlay(boot, d.fst, d.image.size(), exists, plan, error));

    std::vector<GcFile> missing(1);
    missing[0].disc_path = "/nope.bin";
    missing[0].pieces.push_back(on_card(card, 1, 0, pattern(4, 1)));
    EXPECT_FALSE(plan_gc_overlay(boot, d.fst, d.image.size(), missing, plan, error));

    std::vector<GcFile> folder(1);
    folder[0].disc_path = "/dir";
    folder[0].pieces.push_back(on_card(card, 1, 0, pattern(4, 1)));
    EXPECT_FALSE(plan_gc_overlay(boot, d.fst, d.image.size(), folder, plan, error));

    Bytes wii = boot;
    poke32(wii, 0x1C, 0);
    EXPECT_FALSE(plan_gc_overlay(wii, d.fst, d.image.size(), {}, plan, error));

    // An image piece past the image's fragments cannot be served.
    std::vector<GcFile> past(1);
    past[0].disc_path = "/a.bin";
    GcPiece far;
    far.kind = GcPiece::Kind::Image;
    far.source = d.image.size();
    far.length = 0x10;
    past[0].pieces.push_back(far);
    EXPECT_TRUE(plan_gc_overlay(boot, d.fst, d.image.size(), past, plan, error));
    std::vector<GcRun> runs;
    const std::vector<Fragment> image = put_image(card, d.image);
    EXPECT_FALSE(gc_disc_runs(image, d.image.size(), &plan, runs, error));
}

static void test_no_pack() {
    Disc d = make_disc();
    Bytes card;
    const std::vector<Fragment> image = put_image(card, d.image);
    Reader r;
    r.card = &card;
    std::string error;
    EXPECT_TRUE(gc_disc_runs(image, d.image.size(), nullptr, r.runs, error));
    expect_contiguous(r.runs);
    EXPECT_TRUE(r.read(0, d.image.size()) == d.image);
    bool past = false;
    try {
        r.byte(d.image.size());
    } catch (int) {
        past = true;
    }
    EXPECT_TRUE(past);
}

// An image read from its RVZ: the same view, the image's bytes named by
// their offset in it.
static void test_rvz() {
    Disc d = make_disc();
    Bytes card;
    std::vector<GcFile> files(1);
    files[0].disc_path = "/dir/b.bin";
    GcPiece orig;
    orig.kind = GcPiece::Kind::Image;
    orig.source = 0x20000;
    orig.length = 0x40;
    files[0].pieces.push_back(orig);
    files[0].pieces.push_back(on_card(card, 50, 3, pattern(0x20, 4)));
    GcOverlayPlan plan;
    std::string error;
    Bytes boot(d.image.begin(), d.image.begin() + 0x440);
    EXPECT_TRUE(plan_gc_overlay(boot, d.fst, d.image.size(), files, plan, error));
    std::vector<GcRun> runs;
    EXPECT_TRUE(gc_disc_runs_rvz(d.image.size(), &plan, runs, error));
    expect_contiguous(runs);
    EXPECT_TRUE(!runs.empty() && runs[0].kind == GcRun::Kind::Image && runs[0].disc == 0 &&
                runs[0].length == d.image.size() && runs[0].image_offset == 0);
    bool moved = false, from_card = false;
    for (const GcRun& r : runs) {
        if (r.kind == GcRun::Kind::Image && r.disc >= d.image.size()) {
            moved = r.image_offset == 0x20000 && r.length == 0x40;
        }
        if (r.kind == GcRun::Kind::Sd) from_card = true;
    }
    EXPECT_TRUE(moved);
    EXPECT_TRUE(from_card);
    EXPECT_TRUE(gc_disc_runs_rvz(d.image.size(), nullptr, runs, error));
    EXPECT_EQ(runs.size(), std::size_t(1));
    // A piece past the image's end cannot be served.
    files[0].pieces[0].source = d.image.size() - 0x20;
    EXPECT_TRUE(plan_gc_overlay(boot, d.fst, d.image.size(), files, plan, error));
    EXPECT_FALSE(gc_disc_runs_rvz(d.image.size(), &plan, runs, error));
}

int main() {
    test_full_mod();
    test_rvz();
    test_refusals();
    test_no_pack();
    if (g_failures == 0) std::cout << "gcoverlay: all passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
