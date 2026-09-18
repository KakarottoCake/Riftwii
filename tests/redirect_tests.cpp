// SPDX-License-Identifier: GPL-3.0-or-later
// Redirect table: the freestanding walker (rt_validate/rt_lookup) against
// hand-built tables, then the full loader-side pipeline (composed files ->
// flatten -> build_redirect_table -> rt_lookup + sources) against the
// AppliedFile views as the oracle.
#include "riftwii/apply.hpp"
#include "riftwii/redirect.hpp"
#include "riftwii/source.hpp"
#include "rtable.h"

#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using Bytes = std::vector<std::uint8_t>;

static rt_entry E(std::uint64_t vstart, std::uint64_t length, std::uint32_t kind, std::uint64_t source, std::uint64_t skip) {
    rt_entry e;
    std::memset(&e, 0, sizeof(e));
    e.vstart = vstart;
    e.length = length;
    e.kind = kind;
    e.source = source;
    e.skip = skip;
    return e;
}

static Bytes MakeTable(const std::vector<rt_entry>& entries, bool fix_crc = true) {
    rt_header h;
    std::memset(&h, 0, sizeof(h));
    h.magic = RT_MAGIC;
    h.version = RT_VERSION;
    h.entry_count = static_cast<std::uint32_t>(entries.size());
    h.sdio_fd = 7;
    h.tag = 0x1234;
    h.entries_crc = fix_crc ? rt_crc32(entries.data(), entries.size() * sizeof(rt_entry)) : 0;
    Bytes b(rt_table_bytes(h.entry_count));
    std::memcpy(b.data(), &h, sizeof(h));
    if (!entries.empty()) std::memcpy(b.data() + sizeof(h), entries.data(), entries.size() * sizeof(rt_entry));
    return b;
}

static const rt_header* T(const Bytes& b) { return reinterpret_cast<const rt_header*>(b.data()); }

static void test_crc() {
    // Standard check value for "123456789".
    EXPECT_EQ(rt_crc32("123456789", 9), std::uint32_t(0xCBF43926u));
    EXPECT_EQ(rt_crc32("", 0), std::uint32_t(0));
}

static void test_walker() {
    std::vector<rt_entry> entries = {
        E(0x100, 0x100, RT_KIND_ZERO, 0, 0),
        E(0x200, 0x100, RT_KIND_MEM, 0x1000, 0),
        E(0x400, 0x200, RT_KIND_SD, 10, 100),
        E(0x800, 0x100, RT_KIND_DISC, 0x5000, 0),
    };
    Bytes tb = MakeTable(entries);
    EXPECT_EQ(rt_validate(T(tb), tb.size()), int(RT_OK));
    EXPECT_EQ(rt_validate(T(tb), tb.size() + 100), int(RT_OK));  // extra bytes are fine

    rt_run runs[8];
    std::uint32_t n = 0;
    // Gap then zero.
    EXPECT_EQ(rt_lookup(T(tb), 0x80, 0x100, runs, 8, &n), int(RT_OK));
    EXPECT_EQ(n, std::uint32_t(2));
    EXPECT_EQ(runs[0].kind, std::uint32_t(RT_KIND_PASSTHROUGH));
    EXPECT_EQ(runs[0].vstart, std::uint64_t(0x80));
    EXPECT_EQ(runs[0].length, std::uint64_t(0x80));
    EXPECT_EQ(runs[1].kind, std::uint32_t(RT_KIND_ZERO));
    EXPECT_EQ(runs[1].vstart, std::uint64_t(0x100));
    EXPECT_EQ(runs[1].length, std::uint64_t(0x80));
    // Zero into mem with source advance.
    EXPECT_EQ(rt_lookup(T(tb), 0x1F0, 0x20, runs, 8, &n), int(RT_OK));
    EXPECT_EQ(n, std::uint32_t(2));
    EXPECT_EQ(runs[1].kind, std::uint32_t(RT_KIND_MEM));
    EXPECT_EQ(runs[1].source, std::uint64_t(0x1000));
    EXPECT_EQ(runs[1].length, std::uint64_t(0x10));
    EXPECT_EQ(rt_lookup(T(tb), 0x250, 0x10, runs, 8, &n), int(RT_OK));
    EXPECT_EQ(n, std::uint32_t(1));
    EXPECT_EQ(runs[0].source, std::uint64_t(0x1050));
    // Entirely inside a gap.
    EXPECT_EQ(rt_lookup(T(tb), 0x300, 0x100, runs, 8, &n), int(RT_OK));
    EXPECT_EQ(n, std::uint32_t(1));
    EXPECT_EQ(runs[0].kind, std::uint32_t(RT_KIND_PASSTHROUGH));
    EXPECT_EQ(runs[0].length, std::uint64_t(0x100));
    // SD sector/skip arithmetic: 100 + 0x1A0 = 516 -> sector 11, skip 4.
    EXPECT_EQ(rt_lookup(T(tb), 0x400 + 0x1A0, 0x10, runs, 8, &n), int(RT_OK));
    EXPECT_EQ(n, std::uint32_t(1));
    EXPECT_EQ(runs[0].kind, std::uint32_t(RT_KIND_SD));
    EXPECT_EQ(runs[0].source, std::uint64_t(11));
    EXPECT_EQ(runs[0].skip, std::uint32_t(4));
    // SD tail, gap, disc head.
    EXPECT_EQ(rt_lookup(T(tb), 0x5F0, 0x300, runs, 8, &n), int(RT_OK));
    EXPECT_EQ(n, std::uint32_t(3));
    EXPECT_EQ(runs[0].kind, std::uint32_t(RT_KIND_SD));
    EXPECT_EQ(runs[0].length, std::uint64_t(0x10));
    EXPECT_EQ(runs[0].source, std::uint64_t(10 + (100 + 0x1F0) / 512));
    EXPECT_EQ(runs[0].skip, std::uint32_t((100 + 0x1F0) % 512));
    EXPECT_EQ(runs[1].kind, std::uint32_t(RT_KIND_PASSTHROUGH));
    EXPECT_EQ(runs[1].vstart, std::uint64_t(0x600));
    EXPECT_EQ(runs[1].length, std::uint64_t(0x200));
    EXPECT_EQ(runs[2].kind, std::uint32_t(RT_KIND_DISC));
    EXPECT_EQ(runs[2].source, std::uint64_t(0x5000));
    EXPECT_EQ(runs[2].length, std::uint64_t(0xF0));
    // After the last entry.
    EXPECT_EQ(rt_lookup(T(tb), 0x900, 0x100, runs, 8, &n), int(RT_OK));
    EXPECT_EQ(n, std::uint32_t(1));
    EXPECT_EQ(runs[0].kind, std::uint32_t(RT_KIND_PASSTHROUGH));
    // Whole table in one request: 4 entries + 3 gaps + leading gap = 8 runs.
    EXPECT_EQ(rt_lookup(T(tb), 0, 0xA00, runs, 8, &n), int(RT_OK));
    EXPECT_EQ(n, std::uint32_t(8));
    EXPECT_EQ(rt_lookup(T(tb), 0, 0xA00, runs, 7, &n), int(RT_ERR_RUNS));
    EXPECT_EQ(n, std::uint32_t(7));
    // Degenerate requests.
    EXPECT_EQ(rt_lookup(T(tb), 0x123, 0, runs, 8, &n), int(RT_OK));
    EXPECT_EQ(n, std::uint32_t(0));
    EXPECT_EQ(rt_lookup(T(tb), UINT64_MAX - 1, 2, runs, 8, &n), int(RT_ERR_RANGE));
    EXPECT_EQ(rt_lookup(T(tb), UINT64_MAX - 2, 2, runs, 8, &n), int(RT_OK));
    EXPECT_EQ(n, std::uint32_t(1));
    // Empty table: everything passes through.
    Bytes empty = MakeTable({});
    EXPECT_EQ(rt_validate(T(empty), empty.size()), int(RT_OK));
    EXPECT_EQ(rt_lookup(T(empty), 0x1000, 0x10, runs, 8, &n), int(RT_OK));
    EXPECT_EQ(n, std::uint32_t(1));
    EXPECT_EQ(runs[0].kind, std::uint32_t(RT_KIND_PASSTHROUGH));
}

static void test_validate_rejects() {
    std::vector<rt_entry> good = {E(0x100, 0x100, RT_KIND_ZERO, 0, 0), E(0x200, 0x100, RT_KIND_MEM, 0x1000, 0)};
    Bytes tb = MakeTable(good);
    {
        Bytes b = tb; std::memset(b.data(), 0, 4);
        EXPECT_EQ(rt_validate(T(b), b.size()), int(RT_ERR_MAGIC));
    }
    {
        Bytes b = tb; b[4] = 9;
        EXPECT_EQ(rt_validate(T(b), b.size()), int(RT_ERR_VERSION));
    }
    EXPECT_EQ(rt_validate(T(tb), tb.size() - 1), int(RT_ERR_SIZE));
    EXPECT_EQ(rt_validate(T(tb), 4), int(RT_ERR_SIZE));
    EXPECT_EQ(rt_validate(nullptr, 100), int(RT_ERR_SIZE));
    {
        Bytes b = MakeTable(good, false);
        EXPECT_EQ(rt_validate(T(b), b.size()), int(RT_ERR_CRC));
    }
    {
        Bytes b = MakeTable({E(0x200, 0x100, RT_KIND_MEM, 0x1000, 0), E(0x100, 0x100, RT_KIND_ZERO, 0, 0)});
        EXPECT_EQ(rt_validate(T(b), b.size()), int(RT_ERR_ORDER));
    }
    {
        Bytes b = MakeTable({E(0x100, 0x101, RT_KIND_ZERO, 0, 0), E(0x200, 0x100, RT_KIND_MEM, 0x1000, 0)});
        EXPECT_EQ(rt_validate(T(b), b.size()), int(RT_ERR_ORDER));
    }
    {
        Bytes b = MakeTable({E(0x100, 0, RT_KIND_ZERO, 0, 0)});
        EXPECT_EQ(rt_validate(T(b), b.size()), int(RT_ERR_ENTRY));
    }
    {
        Bytes b = MakeTable({E(0x100, 0x10, 5, 0, 0)});
        EXPECT_EQ(rt_validate(T(b), b.size()), int(RT_ERR_ENTRY));
    }
    {
        Bytes b = MakeTable({E(0x100, 0x10, RT_KIND_PASSTHROUGH, 0, 0)});
        EXPECT_EQ(rt_validate(T(b), b.size()), int(RT_ERR_ENTRY));
    }
    {
        Bytes b = MakeTable({E(0x100, 0x10, RT_KIND_SD, 3, 512)});
        EXPECT_EQ(rt_validate(T(b), b.size()), int(RT_ERR_ENTRY));
    }
    {
        Bytes b = MakeTable({E(0x100, 0x10, RT_KIND_ZERO, 5, 0)});
        EXPECT_EQ(rt_validate(T(b), b.size()), int(RT_ERR_ENTRY));
    }
    {
        Bytes b = MakeTable({E(0x100, 0x10, RT_KIND_MEM, 5, 3)});
        EXPECT_EQ(rt_validate(T(b), b.size()), int(RT_ERR_ENTRY));
    }
    {
        Bytes b = MakeTable({E(UINT64_MAX - 4, 0x10, RT_KIND_ZERO, 0, 0)});
        EXPECT_EQ(rt_validate(T(b), b.size()), int(RT_ERR_ENTRY));
    }
    {
        rt_entry e = E(0x100, 0x10, RT_KIND_ZERO, 0, 0);
        e.reserved = 1;
        Bytes b = MakeTable({e});
        EXPECT_EQ(rt_validate(T(b), b.size()), int(RT_ERR_ENTRY));
    }
}

static void test_place_on_fragments() {
    std::vector<riftwii::Fragment> frags = {{20, 2}, {30, 1}};  // 1024 + 512 bytes
    std::vector<riftwii::PlacedRun> runs;
    std::string err;
    EXPECT_TRUE(riftwii::place_on_fragments(frags, 0, 1536, runs, err));
    EXPECT_EQ(runs.size(), std::size_t(2));
    if (runs.size() == 2) {
        EXPECT_EQ(runs[0].source, std::uint64_t(20));
        EXPECT_EQ(runs[0].skip, std::uint32_t(0));
        EXPECT_EQ(runs[0].length, std::uint64_t(1024));
        EXPECT_EQ(runs[1].source, std::uint64_t(30));
        EXPECT_EQ(runs[1].length, std::uint64_t(512));
    }
    EXPECT_TRUE(riftwii::place_on_fragments(frags, 1000, 100, runs, err));
    EXPECT_EQ(runs.size(), std::size_t(2));
    if (runs.size() == 2) {
        EXPECT_EQ(runs[0].source, std::uint64_t(21));
        EXPECT_EQ(runs[0].skip, std::uint32_t(488));
        EXPECT_EQ(runs[0].length, std::uint64_t(24));
        EXPECT_EQ(runs[1].source, std::uint64_t(30));
        EXPECT_EQ(runs[1].skip, std::uint32_t(0));
        EXPECT_EQ(runs[1].length, std::uint64_t(76));
    }
    EXPECT_TRUE(riftwii::place_on_fragments(frags, 1100, 10, runs, err));
    EXPECT_EQ(runs.size(), std::size_t(1));
    if (!runs.empty()) {
        EXPECT_EQ(runs[0].source, std::uint64_t(30));
        EXPECT_EQ(runs[0].skip, std::uint32_t(76));
    }
    EXPECT_FALSE(riftwii::place_on_fragments(frags, 1500, 100, runs, err));  // past the end
    EXPECT_TRUE(riftwii::place_on_fragments(frags, 0, 0, runs, err));
    EXPECT_TRUE(runs.empty());
    std::vector<riftwii::Fragment> broken = {{20, 0}};
    EXPECT_FALSE(riftwii::place_on_fragments(broken, 0, 1, runs, err));
}

// ---- Oracle scenario -------------------------------------------------------

// Provider over in-memory disc files that also remembers which ByteSource
// object it handed out for which external path, so the placer can map a
// pointer back to a placement.
class RecordingProvider final : public riftwii::ContentProvider {
public:
    std::map<std::string, Bytes> disc;
    std::map<std::string, Bytes> ext;
    std::map<const riftwii::ByteSource*, std::string> opened_externals;
    riftwii::OpenStatus open_disc(const std::string& p, std::unique_ptr<riftwii::ByteSource>& out,
                                  std::string& error) override {
        auto it = disc.find(p);
        if (it == disc.end()) { error = "no such disc file"; return riftwii::OpenStatus::NotFound; }
        out.reset(new riftwii::MemorySource(it->second));
        return riftwii::OpenStatus::Ok;
    }
    riftwii::OpenStatus open_external(const std::string& p, std::unique_ptr<riftwii::ByteSource>& out,
                                      std::string& error) override {
        auto it = ext.find(p);
        if (it == ext.end()) { error = "no such external file"; return riftwii::OpenStatus::NotFound; }
        out.reset(new riftwii::MemorySource(it->second));
        opened_externals[out.get()] = p;
        return riftwii::OpenStatus::Ok;
    }
};

static riftwii::FilePatch P(const std::string& disc, const std::string& external, std::uint64_t offset,
                            std::uint64_t length, std::uint64_t fileoffset, bool resize, bool create) {
    riftwii::FilePatch f;
    f.disc = disc;
    f.external = external;
    f.offset = offset;
    f.length = length;
    f.file_offset = fileoffset;
    f.resize = resize;
    f.create = create;
    return f;
}

struct Scenario {
    Bytes disc;                                   // the "partition data" bytes
    Bytes sd;                                     // fake SD card, 512-byte sectors
    Bytes mem_e3;                                 // external held in memory
    std::map<std::string, std::vector<riftwii::Fragment>> sd_layout;
    RecordingProvider provider;
    std::unique_ptr<riftwii::AppliedFile> a, b, c;
    std::vector<riftwii::VirtualFileLayout> layout;
    Bytes table;
};

constexpr std::uint64_t kDiscBytes = 0x4000;
constexpr std::uint64_t kAOffset = 0x1000, kASize = 0x800;
constexpr std::uint64_t kBOffset = 0x2000, kBSize = 0x400;
constexpr std::uint64_t kBVirtual = 0x10000;
constexpr std::uint64_t kCVirtual = 0x20000;

static bool BuildScenario(Scenario& s, std::string& err) {
    s.disc.resize(kDiscBytes);
    for (std::size_t i = 0; i < s.disc.size(); ++i) s.disc[i] = static_cast<std::uint8_t>(i * 7 + 3);
    Bytes e1(0x80), e2(0x600), e3(0x100);
    for (std::size_t i = 0; i < e1.size(); ++i) e1[i] = static_cast<std::uint8_t>(0xA0 + i);
    for (std::size_t i = 0; i < e2.size(); ++i) e2[i] = static_cast<std::uint8_t>((i * 3) ^ 0x5A);
    for (std::size_t i = 0; i < e3.size(); ++i) e3[i] = static_cast<std::uint8_t>(0xC0 - i);
    s.provider.disc["/A"] = Bytes(s.disc.begin() + kAOffset, s.disc.begin() + kAOffset + kASize);
    s.provider.disc["/B"] = Bytes(s.disc.begin() + kBOffset, s.disc.begin() + kBOffset + kBSize);
    s.provider.ext["/E1"] = e1;
    s.provider.ext["/E2"] = e2;
    s.provider.ext["/E3"] = e3;
    // SD: E1 contiguous at sector 10; E2 split across sectors 20-21 and 30.
    s.sd.assign(64 * 512, 0xEE);
    std::memcpy(s.sd.data() + 10 * 512, e1.data(), e1.size());
    std::memcpy(s.sd.data() + 20 * 512, e2.data(), 1024);
    std::memcpy(s.sd.data() + 30 * 512, e2.data() + 1024, 512);
    s.sd_layout["/E1"] = {{10, 1}};
    s.sd_layout["/E2"] = {{20, 2}, {30, 1}};
    s.mem_e3 = e3;

    // A: same-size partial replacement, stays in place.
    if (!riftwii::build_replacement(P("/A", "/E1", 0x100, 0x80, 0, false, false), s.provider, s.a, err)) return false;
    // B: grows (0x400 -> 0x900) so it is relocated; two composed patches.
    std::vector<riftwii::FilePatch> bp;
    bp.push_back(P("/B", "/E2", 0x300, 0x600, 0x100, true, false));   // [0x300,0x800) E2[0x100..0x600), [0x800,0x900) zero
    bp.push_back(P("/B", "/E1", 0x10, 0x20, 0, false, false));        // [0x10,0x30) E1[0..0x20)
    if (!riftwii::apply_patches(bp, s.provider, s.b, err)) return false;
    // C: created from a memory-held external, zero-padded to 0x180.
    if (!riftwii::build_replacement(P("/C", "/E3", 0, 0x180, 0, true, true), s.provider, s.c, err)) return false;

    s.layout = {
        {s.a.get(), kAOffset, kAOffset},
        {s.b.get(), kBVirtual, kBOffset},
        {s.c.get(), kCVirtual, 0},
    };
    riftwii::ExternalPlacer place = [&s](const riftwii::ByteSource* src, std::uint64_t off, std::uint64_t len,
                                         std::vector<riftwii::PlacedRun>& out, std::string& e) {
        auto it = s.provider.opened_externals.find(src);
        if (it == s.provider.opened_externals.end()) { e = "unknown external source"; return false; }
        if (it->second == "/E3") {
            riftwii::PlacedRun r;
            r.kind = RT_KIND_MEM;
            r.length = len;
            r.source = reinterpret_cast<std::uintptr_t>(s.mem_e3.data()) + off;
            out = {r};
            return true;
        }
        return riftwii::place_on_fragments(s.sd_layout.at(it->second), off, len, out, e);
    };
    return riftwii::build_redirect_table(s.layout, place, 7, 0xABCD, s.table, err);
}

// What the resident runtime will do with the runs.
static bool VirtualRead(const Scenario& s, std::uint64_t offset, std::uint64_t length, Bytes& out) {
    out.assign(static_cast<std::size_t>(length), 0xFF);
    rt_run runs[32];
    std::uint32_t n = 0;
    if (rt_lookup(T(s.table), offset, length, runs, 32, &n) != RT_OK) return false;
    for (std::uint32_t i = 0; i < n; ++i) {
        const rt_run& r = runs[i];
        std::uint8_t* dst = out.data() + (r.vstart - offset);
        switch (r.kind) {
        case RT_KIND_PASSTHROUGH:
            if (r.vstart + r.length > s.disc.size()) return false;
            std::memcpy(dst, s.disc.data() + r.vstart, static_cast<std::size_t>(r.length));
            break;
        case RT_KIND_DISC:
            if (r.source + r.length > s.disc.size()) return false;
            std::memcpy(dst, s.disc.data() + r.source, static_cast<std::size_t>(r.length));
            break;
        case RT_KIND_ZERO:
            std::memset(dst, 0, static_cast<std::size_t>(r.length));
            break;
        case RT_KIND_MEM:
            std::memcpy(dst, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(r.source)), static_cast<std::size_t>(r.length));
            break;
        case RT_KIND_SD: {
            const std::uint64_t at = r.source * 512 + r.skip;
            if (at + r.length > s.sd.size()) return false;
            std::memcpy(dst, s.sd.data() + at, static_cast<std::size_t>(r.length));
            break;
        }
        default:
            return false;
        }
    }
    return true;
}

// The truth: composed views inside their virtual ranges, raw disc elsewhere.
static bool OracleRead(const Scenario& s, std::uint64_t offset, std::uint64_t length, Bytes& out) {
    out.assign(static_cast<std::size_t>(length), 0);
    for (std::uint64_t i = 0; i < length; ++i) {
        const std::uint64_t v = offset + i;
        std::uint8_t byte = 0;
        if (v >= kAOffset && v < kAOffset + s.a->size()) {
            if (!s.a->read(v - kAOffset, &byte, 1)) return false;
        } else if (v >= kBVirtual && v < kBVirtual + s.b->size()) {
            if (!s.b->read(v - kBVirtual, &byte, 1)) return false;
        } else if (v >= kCVirtual && v < kCVirtual + s.c->size()) {
            if (!s.c->read(v - kCVirtual, &byte, 1)) return false;
        } else if (v < s.disc.size()) {
            byte = s.disc[static_cast<std::size_t>(v)];
        } else {
            return false;
        }
        out[static_cast<std::size_t>(i)] = byte;
    }
    return true;
}

static void Compare(const Scenario& s, std::uint64_t offset, std::uint64_t length, const char* what) {
    Bytes got, want;
    const bool ok_v = VirtualRead(s, offset, length, got);
    const bool ok_o = OracleRead(s, offset, length, want);
    if (!ok_v || !ok_o || got != want) {
        std::cerr << "FAILED: mismatch for " << what << " at 0x" << std::hex << offset << " len 0x" << length
                  << std::dec << " (virtual " << ok_v << ", oracle " << ok_o << ")" << std::endl;
        g_failures++;
    }
}

static void test_flatten_shape() {
    Scenario s;
    std::string err;
    EXPECT_TRUE(BuildScenario(s, err));
    if (!s.b) { std::cerr << err << std::endl; return; }
    std::vector<riftwii::FlatExtent> flat = s.b->flatten();
    // [0,0x10) orig, [0x10,0x30) E1, [0x30,0x300) orig, [0x300,0x800) E2@0x100, [0x800,0x900) zero
    EXPECT_EQ(flat.size(), std::size_t(5));
    if (flat.size() == 5) {
        EXPECT_TRUE(flat[0].kind == riftwii::FlatExtent::Kind::Original);
        EXPECT_EQ(flat[0].dest, std::uint64_t(0));
        EXPECT_EQ(flat[0].length, std::uint64_t(0x10));
        EXPECT_EQ(flat[0].source_offset, std::uint64_t(0));
        EXPECT_TRUE(flat[1].kind == riftwii::FlatExtent::Kind::External);
        EXPECT_EQ(flat[1].dest, std::uint64_t(0x10));
        EXPECT_EQ(flat[1].length, std::uint64_t(0x20));
        EXPECT_TRUE(flat[2].kind == riftwii::FlatExtent::Kind::Original);
        EXPECT_EQ(flat[2].dest, std::uint64_t(0x30));
        EXPECT_EQ(flat[2].length, std::uint64_t(0x2D0));
        EXPECT_EQ(flat[2].source_offset, std::uint64_t(0x30));
        EXPECT_TRUE(flat[3].kind == riftwii::FlatExtent::Kind::External);
        EXPECT_EQ(flat[3].dest, std::uint64_t(0x300));
        EXPECT_EQ(flat[3].length, std::uint64_t(0x500));
        EXPECT_EQ(flat[3].source_offset, std::uint64_t(0x100));
        EXPECT_TRUE(flat[4].kind == riftwii::FlatExtent::Kind::Zero);
        EXPECT_EQ(flat[4].dest, std::uint64_t(0x800));
        EXPECT_EQ(flat[4].length, std::uint64_t(0x100));
    }
    // A: [0,0x100) orig, [0x100,0x180) E1, [0x180,0x800) orig.
    flat = s.a->flatten();
    EXPECT_EQ(flat.size(), std::size_t(3));
    // C: [0,0x100) E3, [0x100,0x180) zero.
    flat = s.c->flatten();
    EXPECT_EQ(flat.size(), std::size_t(2));
    if (flat.size() == 2) {
        EXPECT_TRUE(flat[0].kind == riftwii::FlatExtent::Kind::External);
        EXPECT_TRUE(flat[1].kind == riftwii::FlatExtent::Kind::Zero);
    }
    // Table shape: validated, A contributes one SD entry (in place), B six
    // (DISC, SD, DISC, SD, SD, ZERO), C two (MEM, ZERO) = 9 entries.
    EXPECT_EQ(rt_validate(T(s.table), s.table.size()), int(RT_OK));
    EXPECT_EQ(T(s.table)->entry_count, std::uint32_t(9));
    EXPECT_EQ(T(s.table)->sdio_fd, std::uint32_t(7));
    EXPECT_EQ(T(s.table)->tag, std::uint64_t(0xABCD));
}

static void test_oracle() {
    Scenario s;
    std::string err;
    EXPECT_TRUE(BuildScenario(s, err));
    if (s.table.empty()) { std::cerr << err << std::endl; return; }
    // Targeted straddles.
    Compare(s, 0x0FF0, 0x30, "into A");
    Compare(s, 0x10F0, 0x20, "A original->E1");
    Compare(s, 0x1170, 0x20, "A E1->original");
    Compare(s, 0x17F0, 0x20, "out of A");
    Compare(s, kAOffset, kASize, "whole A");
    Compare(s, kBVirtual, 0x900, "whole B");
    Compare(s, kBVirtual + 0x8, 0x30, "B disc->sd->disc");
    Compare(s, kBVirtual + 0x2F0, 0x20, "B disc->E2");
    Compare(s, kBVirtual + 0x300 + 0x3F0, 0x20, "B E2 fragment boundary");
    Compare(s, kBVirtual + 0x7F0, 0x20, "B sd->zero");
    Compare(s, kBVirtual + 0x8F0, 0x10, "B tail");
    Compare(s, kCVirtual, 0x180, "whole C");
    Compare(s, kCVirtual + 0xF0, 0x20, "C mem->zero");
    Compare(s, 0, 0x4000, "whole raw disc");
    Compare(s, kBOffset, kBSize, "old B location still raw disc");
    // Randomised reads inside the three valid windows.
    std::uint32_t seed = 12345;
    auto next = [&seed]() { seed = seed * 1103515245u + 12345u; return seed >> 8; };
    for (int i = 0; i < 3000; ++i) {
        const std::uint64_t len = 1 + next() % 0x300;
        std::uint64_t off = 0;
        switch (i % 3) {
        case 0: off = next() % (kDiscBytes - len); break;
        case 1: off = kBVirtual + next() % (0x900 - std::min<std::uint64_t>(len, 0x8FF)); break;
        default: off = kCVirtual + next() % (0x180 - std::min<std::uint64_t>(len, 0x17F)); break;
        }
        std::uint64_t window_end = (i % 3 == 0) ? kDiscBytes : (i % 3 == 1 ? kBVirtual + 0x900 : kCVirtual + 0x180);
        std::uint64_t l = std::min(len, window_end - off);
        Compare(s, off, l, "random");
    }
}

static void test_builder_rejects_overlap() {
    Scenario s;
    std::string err;
    EXPECT_TRUE(BuildScenario(s, err));
    if (!s.a) return;
    std::vector<riftwii::VirtualFileLayout> bad = {{s.a.get(), 0x1000, 0x1000}, {s.c.get(), 0x1100, 0}};
    riftwii::ExternalPlacer place = [](const riftwii::ByteSource*, std::uint64_t off, std::uint64_t len,
                                       std::vector<riftwii::PlacedRun>& out, std::string&) {
        riftwii::PlacedRun r;
        r.kind = RT_KIND_MEM;
        r.length = len;
        r.source = 0x80000000u + off;
        out = {r};
        return true;
    };
    Bytes table;
    EXPECT_FALSE(riftwii::build_redirect_table(bad, place, 0, 0, table, err));
    EXPECT_TRUE(err.find("overlap") != std::string::npos);
    // A placer that under-delivers is caught.
    riftwii::ExternalPlacer shortp = [](const riftwii::ByteSource*, std::uint64_t, std::uint64_t len,
                                        std::vector<riftwii::PlacedRun>& out, std::string&) {
        riftwii::PlacedRun r;
        r.kind = RT_KIND_MEM;
        r.length = len - 1;
        r.source = 0x80000000u;
        out = {r};
        return true;
    };
    std::vector<riftwii::VirtualFileLayout> one = {{s.a.get(), 0x1000, 0x1000}};
    EXPECT_FALSE(riftwii::build_redirect_table(one, shortp, 0, 0, table, err));
    // A file with nothing but in-place original bytes yields an empty table.
    RecordingProvider p;
    p.disc["/X"] = Bytes(16, 1);
    p.ext["/Y"] = Bytes(16, 1);
    std::unique_ptr<riftwii::AppliedFile> x;
    EXPECT_TRUE(riftwii::build_replacement(P("/X", "/Y", 0, 0, 0, true, false), p, x, err));
    std::vector<riftwii::VirtualFileLayout> same = {{x.get(), 0x100, 0x100}};
    EXPECT_TRUE(riftwii::build_redirect_table(same, place, 0, 0, table, err));
    EXPECT_EQ(T(table)->entry_count, std::uint32_t(1));  // the external replaces everything
}

int main() {
    test_crc();
    test_walker();
    test_validate_rejects();
    test_place_on_fragments();
    test_flatten_shape();
    test_oracle();
    test_builder_rejects_overlap();
    if (g_failures == 0) {
        std::cout << "ALL REDIRECT TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
