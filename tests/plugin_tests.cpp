// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The plugin file (include/riftwii/pluginfile.hpp): the header and every
// refusal, the SHA-256, the relocations, the names and folders, crash
// attribution and the API tables' version rules; the in-app update's
// order (plugins before the DOL); and a file written by
// tools/make_plugin.py read back by the C++ reader.
#include "riftwii/plugin_api.h"
#include "riftwii/pluginfile.hpp"
#include "riftwii/sha256.hpp"
#include "riftwii/update.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using Bytes = std::vector<std::uint8_t>;
using riftwii::PluginHeader;
using riftwii::PluginRefusal;

static std::ostream& operator<<(std::ostream& out, PluginRefusal refusal) {
    return out << riftwii::plugin_refusal_name(refusal);
}

static void Put32(Bytes& b, std::size_t at, std::uint32_t v) {
    b[at] = static_cast<std::uint8_t>(v >> 24);
    b[at + 1] = static_cast<std::uint8_t>(v >> 16);
    b[at + 2] = static_cast<std::uint8_t>(v >> 8);
    b[at + 3] = static_cast<std::uint8_t>(v);
}

static std::uint32_t Get32(const Bytes& b, std::size_t at) {
    return (std::uint32_t(b[at]) << 24) | (std::uint32_t(b[at + 1]) << 16) | (std::uint32_t(b[at + 2]) << 8) | b[at + 3];
}

static std::uint32_t Get16(const Bytes& b, std::size_t at) { return (std::uint32_t(b[at]) << 8) | b[at + 1]; }

static void PutText(Bytes& b, std::size_t at, const std::string& text) {
    for (std::size_t i = 0; i < text.size(); ++i) b[at + i] = static_cast<std::uint8_t>(text[i]);
}

static std::string Hex(const riftwii::Sha256Digest& digest) {
    static const char kDigits[] = "0123456789abcdef";
    std::string out;
    for (std::uint8_t b : digest) {
        out += kDigits[b >> 4];
        out += kDigits[b & 15];
    }
    return out;
}

struct Reloc {
    std::uint32_t offset, type, target;
};

struct Spec {
    std::string name = "selftest";
    std::string stamp = "2610-460-g1a2b3c4d";
    std::uint32_t api = RWP_API_VERSION;
    std::uint32_t image_bytes = 256;
    std::uint32_t bss_bytes = 64;
    std::uint32_t state_bytes = 0;
    std::uint32_t init = 0x10;
    std::uint32_t shutdown = 0x20;
    std::uint32_t table = 0x40;
    std::vector<Reloc> relocs;
};

// Writes the SHA-256 the way tools/make_plugin.py does: of the whole file
// with the hash field zeroed.
static void Seal(Bytes& file) {
    for (std::size_t i = 0; i < 32; ++i) file[riftwii::kPluginHashOffset + i] = 0;
    const riftwii::Sha256Digest digest = riftwii::sha256(file.data(), file.size());
    for (std::size_t i = 0; i < 32; ++i) file[riftwii::kPluginHashOffset + i] = digest[i];
}

// A plugin file as tools/make_plugin.py lays it out, built by hand.
static Bytes MakePlugin(const Spec& spec) {
    Bytes file(riftwii::kPluginHeaderBytes, 0);
    PutText(file, 0, "RWPL");
    file[4] = riftwii::kPluginHeaderVersion;
    PutText(file, 8, spec.name);
    Put32(file, 24, 7);
    Put32(file, 28, spec.api);
    PutText(file, 32, spec.stamp);
    Put32(file, 64, spec.init);
    Put32(file, 68, spec.shutdown);
    Put32(file, 72, spec.table);
    Put32(file, 76, spec.image_bytes);
    Put32(file, 80, spec.bss_bytes);
    Put32(file, 84, static_cast<std::uint32_t>(spec.relocs.size()));
    Put32(file, 88, spec.state_bytes);
    Put32(file, 92, 0);
    for (std::uint32_t i = 0; i < spec.image_bytes; ++i) file.push_back(static_cast<std::uint8_t>(i * 13 + 1));
    for (const Reloc& r : spec.relocs) {
        const std::size_t at = file.size();
        file.resize(at + 12);
        Put32(file, at, r.offset);
        Put32(file, at + 4, r.type);
        Put32(file, at + 8, r.target);
    }
    Seal(file);
    return file;
}

static PluginRefusal Parse(const Bytes& file, PluginHeader& header, std::string& why,
                           const std::string& expected = "selftest", std::uint32_t budget = RWP_ARENA_BYTES) {
    return riftwii::plugin_parse_header(file.data(), file.size(), file.size(), expected, budget, header, why);
}

static PluginRefusal Parse(const Bytes& file) {
    PluginHeader header;
    std::string why;
    return Parse(file, header, why);
}

static void TestSha256() {
    // FIPS 180-4's examples (NIST's SHA256.pdf).
    const std::string abc = "abc";
    EXPECT_EQ(Hex(riftwii::sha256(reinterpret_cast<const std::uint8_t*>(abc.data()), abc.size())),
              std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    EXPECT_EQ(Hex(riftwii::sha256(nullptr, 0)),
              std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    const std::string two_blocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    EXPECT_EQ(Hex(riftwii::sha256(reinterpret_cast<const std::uint8_t*>(two_blocks.data()), two_blocks.size())),
              std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    // A million 'a's, fed in uneven pieces: the same as one call.
    const Bytes million(1000000, 'a');
    riftwii::Sha256 pieces;
    std::size_t at = 0;
    for (std::size_t step = 1; at < million.size(); step = step * 7 % 997 + 1) {
        const std::size_t take = std::min(step, million.size() - at);
        pieces.update(million.data() + at, take);
        at += take;
    }
    const std::string expected = "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";
    EXPECT_EQ(Hex(pieces.finish()), expected);
    EXPECT_EQ(Hex(riftwii::sha256(million.data(), million.size())), expected);
}

static void TestGoodHeader() {
    Spec spec;
    spec.relocs = {{0x80, riftwii::kRelocAddr32, 0x40}};
    const Bytes file = MakePlugin(spec);
    PluginHeader header;
    std::string why;
    EXPECT_EQ(Parse(file, header, why), PluginRefusal::None);
    EXPECT_EQ(header.name, std::string("selftest"));
    EXPECT_EQ(header.plugin_version, 7u);
    EXPECT_EQ(header.api_version, static_cast<std::uint32_t>(RWP_API_VERSION));
    EXPECT_EQ(header.stamp, std::string("2610-460-g1a2b3c4d"));
    EXPECT_EQ(header.init_offset, 0x10u);
    EXPECT_EQ(header.shutdown_offset, 0x20u);
    EXPECT_EQ(header.table_offset, 0x40u);
    EXPECT_EQ(header.image_bytes, 256u);
    EXPECT_EQ(header.bss_bytes, 64u);
    EXPECT_EQ(header.reloc_count, 1u);
    EXPECT_EQ(header.memory_bytes(), 256u + 64u);
    EXPECT_EQ(header.file_bytes(), static_cast<std::uint64_t>(file.size()));
    EXPECT_TRUE(riftwii::plugin_hash_matches(file.data(), file.data() + riftwii::kPluginHeaderBytes,
                                             file.size() - riftwii::kPluginHeaderBytes));
    // A shutdown offset of 0 means none.
    spec.shutdown = 0;
    EXPECT_EQ(Parse(MakePlugin(spec)), PluginRefusal::None);
}

// Every header check, each on its own: always Damaged, with a reason.
static void TestDamagedHeaders() {
    const Bytes good = MakePlugin(Spec{});
    PluginHeader header;
    std::string why;

    // Shorter than a header, or no bytes at all.
    EXPECT_EQ(riftwii::plugin_parse_header(good.data(), 100, 100, "selftest", RWP_ARENA_BYTES, header, why),
              PluginRefusal::Damaged);
    EXPECT_EQ(riftwii::plugin_parse_header(nullptr, 0, 0, "selftest", RWP_ARENA_BYTES, header, why),
              PluginRefusal::Damaged);
    // Magic.
    Bytes bad = good;
    bad[0] = 'X';
    EXPECT_EQ(Parse(bad, header, why), PluginRefusal::Damaged);
    EXPECT_TRUE(why.find("RWPL") != std::string::npos);
    // Header version, and the bytes after it.
    bad = good;
    bad[4] = 2;
    EXPECT_EQ(Parse(bad, header, why), PluginRefusal::Damaged);
    bad = good;
    bad[6] = 1;
    EXPECT_EQ(Parse(bad), PluginRefusal::Damaged);
    // The name: another plugin's, a character outside A-Za-z0-9_-, or not
    // NUL-padded.
    EXPECT_EQ(Parse(good, header, why, "gcnative"), PluginRefusal::Damaged);
    EXPECT_TRUE(why.find("gcnative") != std::string::npos);
    bad = good;
    bad[10] = '/';
    EXPECT_EQ(Parse(bad), PluginRefusal::Damaged);
    bad = good;
    bad[22] = 'x';  // after the NUL that ends "selftest"
    EXPECT_EQ(Parse(bad), PluginRefusal::Damaged);
    // The stamp: empty, or not text.
    Spec spec;
    spec.stamp = "";
    EXPECT_EQ(Parse(MakePlugin(spec)), PluginRefusal::Damaged);
    bad = good;
    bad[33] = 0x01;
    EXPECT_EQ(Parse(bad), PluginRefusal::Damaged);
    // Image size: zero, or not whole 32-byte lines.
    spec = Spec{};
    spec.image_bytes = 0;
    EXPECT_EQ(Parse(MakePlugin(spec)), PluginRefusal::Damaged);
    spec.image_bytes = 250;
    EXPECT_EQ(Parse(MakePlugin(spec)), PluginRefusal::Damaged);
    // More relocations than any plugin has (a damaged count), refused
    // before the count is used for a size.
    bad = good;
    Put32(bad, 84, riftwii::kPluginMaxRelocs + 1);
    EXPECT_EQ(Parse(bad), PluginRefusal::Damaged);
    // The file cut short or added to.
    bad = good;
    bad.pop_back();
    EXPECT_EQ(Parse(bad, header, why), PluginRefusal::Damaged);
    EXPECT_TRUE(why.find("cut short") != std::string::npos);
    bad = good;
    bad.push_back(0);
    EXPECT_EQ(Parse(bad), PluginRefusal::Damaged);
    // Image, .bss and state past the arena: the budget, and a size that
    // would wrap 32 bits.
    spec = Spec{};
    spec.state_bytes = RWP_ARENA_BYTES;
    EXPECT_EQ(Parse(MakePlugin(spec), header, why), PluginRefusal::Damaged);
    EXPECT_TRUE(why.find("arena") != std::string::npos);
    spec = Spec{};
    spec.bss_bytes = 0xFFFFFFF0u;
    EXPECT_EQ(Parse(MakePlugin(spec)), PluginRefusal::Damaged);
    spec = Spec{};
    EXPECT_EQ(Parse(MakePlugin(spec), header, why, "selftest", 256), PluginRefusal::Damaged);
    EXPECT_EQ(Parse(MakePlugin(spec), header, why, "selftest", 320), PluginRefusal::None);
    // Entry offsets: outside the image, or not on a word.
    spec = Spec{};
    spec.init = 256;
    EXPECT_EQ(Parse(MakePlugin(spec)), PluginRefusal::Damaged);
    spec.init = 0x12;
    EXPECT_EQ(Parse(MakePlugin(spec)), PluginRefusal::Damaged);
    spec = Spec{};
    spec.shutdown = 0x1000;
    EXPECT_EQ(Parse(MakePlugin(spec)), PluginRefusal::Damaged);
    // An init past the image's end, or so far past it that offset + 4
    // would wrap 32 bits.
    spec = Spec{};
    spec.init = 256 - 4;
    EXPECT_EQ(Parse(MakePlugin(spec)), PluginRefusal::None);
    spec.init = 0xFFFFFFFCu;
    EXPECT_EQ(Parse(MakePlugin(spec), header, why), PluginRefusal::Damaged);
    EXPECT_TRUE(why.find("init") != std::string::npos);
    // Offset 0 is the reserved word (plugins/plugin.ld), never an entry.
    spec.init = 0;
    EXPECT_EQ(Parse(MakePlugin(spec), header, why), PluginRefusal::Damaged);
    EXPECT_TRUE(why.find("offset 0") != std::string::npos);
    // The table: all of its kPluginTableBytes inside the image. Its last
    // possible place is fine; anything later would let the loader read
    // probe or shutdown past the image, so the first four words alone
    // being inside is not enough.
    spec = Spec{};
    spec.table = 256 - riftwii::kPluginTableBytes;
    EXPECT_EQ(Parse(MakePlugin(spec)), PluginRefusal::None);
    spec.table = 256 - 16;
    EXPECT_EQ(Parse(MakePlugin(spec), header, why), PluginRefusal::Damaged);
    EXPECT_TRUE(why.find("table") != std::string::npos);
    spec.table = 256 - 8;
    EXPECT_EQ(Parse(MakePlugin(spec)), PluginRefusal::Damaged);
    spec.table = 0xFFFFFFF0u;
    EXPECT_EQ(Parse(MakePlugin(spec)), PluginRefusal::Damaged);
}

// The relocated rwp_plugin table, checked before init runs: the pointers
// the loader will call stay inside the image.
static void TestTable() {
    PluginHeader header;
    std::string why;
    EXPECT_EQ(Parse(MakePlugin(Spec{}), header, why), PluginRefusal::None);  // shutdown 0x20, table 0x40
    const std::uint32_t base = 0x933C0000;
    const auto image_with = [&](std::uint32_t size, std::uint32_t api, std::uint32_t probe, std::uint32_t shutdown,
                                std::uint32_t launch = 0) {
        Bytes image(header.image_bytes, 0);
        Put32(image, header.table_offset, size);
        Put32(image, header.table_offset + 4, api);
        Put32(image, header.table_offset + 8, 7);
        Put32(image, header.table_offset + 12, 0);
        Put32(image, header.table_offset + 16, probe);
        Put32(image, header.table_offset + 20, shutdown);
        Put32(image, header.table_offset + 24, launch);
        return image;
    };
    const auto check = [&](const Bytes& image) {
        return riftwii::plugin_check_table(image.data(), header, base, RWP_API_VERSION, why);
    };
    const std::uint32_t shutdown = base + 0x20;
    const std::uint32_t whole = riftwii::kPluginTableBytes;
    EXPECT_EQ(whole, 28u);
    EXPECT_EQ(check(image_with(whole, RWP_API_VERSION, base + 0x30, shutdown)), PluginRefusal::None);
    // No probe is allowed; a longer table (a later plugin) too.
    EXPECT_EQ(check(image_with(whole, RWP_API_VERSION, 0, shutdown)), PluginRefusal::None);
    EXPECT_EQ(check(image_with(40, RWP_API_VERSION, base + 0x30, shutdown)), PluginRefusal::None);
    // Its own size and API: API 1's 24-byte table, without launch, is too short.
    EXPECT_EQ(check(image_with(16, RWP_API_VERSION, base + 0x30, shutdown)), PluginRefusal::Damaged);
    EXPECT_EQ(check(image_with(24, RWP_API_VERSION, base + 0x30, shutdown)), PluginRefusal::Damaged);
    EXPECT_EQ(check(image_with(whole, RWP_API_VERSION + 1, base + 0x30, shutdown)), PluginRefusal::Damaged);
    // A probe at the image's end, in the .bss, before it, elsewhere in
    // memory, or not on a word.
    EXPECT_EQ(check(image_with(whole, RWP_API_VERSION, base + header.image_bytes - 4, shutdown)), PluginRefusal::None);
    EXPECT_EQ(check(image_with(whole, RWP_API_VERSION, base + header.image_bytes, shutdown)), PluginRefusal::Damaged);
    EXPECT_TRUE(why.find("probe") != std::string::npos);
    EXPECT_EQ(check(image_with(whole, RWP_API_VERSION, base - 4, shutdown)), PluginRefusal::Damaged);
    EXPECT_EQ(check(image_with(whole, RWP_API_VERSION, 0x80003000u, shutdown)), PluginRefusal::Damaged);
    EXPECT_EQ(check(image_with(whole, RWP_API_VERSION, base + 0x32, shutdown)), PluginRefusal::Damaged);
    // The launch (API 2) under the same rule as the probe: code inside the
    // image or none.
    EXPECT_EQ(check(image_with(whole, RWP_API_VERSION, 0, shutdown, base + 0x34)), PluginRefusal::None);
    EXPECT_EQ(check(image_with(whole, RWP_API_VERSION, 0, shutdown, base + header.image_bytes - 4)), PluginRefusal::None);
    EXPECT_EQ(check(image_with(whole, RWP_API_VERSION, 0, shutdown, base + header.image_bytes)), PluginRefusal::Damaged);
    EXPECT_TRUE(why.find("launch") != std::string::npos);
    EXPECT_EQ(check(image_with(whole, RWP_API_VERSION, 0, shutdown, base - 4)), PluginRefusal::Damaged);
    EXPECT_EQ(check(image_with(whole, RWP_API_VERSION, 0, shutdown, 0x80A00000u)), PluginRefusal::Damaged);
    EXPECT_EQ(check(image_with(whole, RWP_API_VERSION, 0, shutdown, base + 0x36)), PluginRefusal::Damaged);
    // Nothing the loader calls may be the image's first word, which is
    // reserved: the gamecube plugin of stage (b) once had its shutdown
    // linked there, the header said "none" (offset 0) and the table the
    // base, so every load was refused. Probe and launch at the base are
    // refused the same way.
    EXPECT_EQ(check(image_with(whole, RWP_API_VERSION, base, shutdown)), PluginRefusal::Damaged);
    EXPECT_TRUE(why.find("probe") != std::string::npos);
    EXPECT_EQ(check(image_with(whole, RWP_API_VERSION, 0, shutdown, base)), PluginRefusal::Damaged);
    EXPECT_TRUE(why.find("launch") != std::string::npos);
    {
        Spec none;
        none.shutdown = 0;
        PluginHeader no_shutdown;
        EXPECT_EQ(Parse(MakePlugin(none), no_shutdown, why), PluginRefusal::None);
        Bytes image(no_shutdown.image_bytes, 0);
        Put32(image, no_shutdown.table_offset, whole);
        Put32(image, no_shutdown.table_offset + 4, RWP_API_VERSION);
        Put32(image, no_shutdown.table_offset + 20, base);  // the shutdown at offset 0
        EXPECT_EQ(riftwii::plugin_check_table(image.data(), no_shutdown, base, RWP_API_VERSION, why),
                  PluginRefusal::Damaged);
        EXPECT_TRUE(why.find("offset 0") != std::string::npos);
    }
    // A shutdown other than the header's, outside the image, or missing
    // while the header has one.
    EXPECT_EQ(check(image_with(whole, RWP_API_VERSION, base + 0x30, base + 0x24)), PluginRefusal::Damaged);
    EXPECT_TRUE(why.find("shutdown") != std::string::npos);
    EXPECT_EQ(check(image_with(whole, RWP_API_VERSION, base + 0x30, 0x80001800u)), PluginRefusal::Damaged);
    EXPECT_EQ(check(image_with(whole, RWP_API_VERSION, base + 0x30, 0)), PluginRefusal::Damaged);
    // No shutdown in either.
    Spec spec;
    spec.shutdown = 0;
    EXPECT_EQ(Parse(MakePlugin(spec), header, why), PluginRefusal::None);
    EXPECT_EQ(check(image_with(whole, RWP_API_VERSION, base + 0x30, 0)), PluginRefusal::None);
    EXPECT_EQ(check(image_with(whole, RWP_API_VERSION, base + 0x30, base + 0x20)), PluginRefusal::Damaged);
}

static void TestHash() {
    Spec spec;
    spec.relocs = {{0x80, riftwii::kRelocAddr32, 0x40}};
    const Bytes good = MakePlugin(spec);
    const auto matches = [](const Bytes& file) {
        return riftwii::plugin_hash_matches(file.data(), file.data() + riftwii::kPluginHeaderBytes,
                                            file.size() - riftwii::kPluginHeaderBytes);
    };
    EXPECT_TRUE(matches(good));
    // One bit anywhere: the image, a relocation, a header field, the hash.
    for (std::size_t at : {std::size_t(200), good.size() - 1, std::size_t(24), std::size_t(100)}) {
        Bytes bad = good;
        bad[at] ^= 0x01;
        EXPECT_FALSE(matches(bad));
    }
    // A damaged image still passes the header checks: only the hash finds it.
    Bytes damaged = good;
    damaged[riftwii::kPluginHeaderBytes + 10] ^= 0xFF;
    EXPECT_EQ(Parse(damaged), PluginRefusal::None);
    EXPECT_FALSE(matches(damaged));
}

static void TestVersions() {
    PluginHeader header;
    std::string why;
    EXPECT_EQ(Parse(MakePlugin(Spec{}), header, why), PluginRefusal::None);
    EXPECT_EQ(riftwii::plugin_check_versions(header, RWP_API_VERSION, "2610-460-g1a2b3c4d", why), PluginRefusal::None);
    // Another API: named with both numbers, and checked before the stamp.
    EXPECT_EQ(riftwii::plugin_check_versions(header, RWP_API_VERSION + 1, "2610-461-g00000000", why), PluginRefusal::Api);
    EXPECT_TRUE(why.find(std::to_string(RWP_API_VERSION)) != std::string::npos);
    EXPECT_TRUE(why.find(std::to_string(RWP_API_VERSION + 1)) != std::string::npos);
    // The same API, another build: both stamps in the reason.
    EXPECT_EQ(riftwii::plugin_check_versions(header, RWP_API_VERSION, "2610-461-g00000000", why), PluginRefusal::Stamp);
    EXPECT_TRUE(why.find("2610-460-g1a2b3c4d") != std::string::npos);
    EXPECT_TRUE(why.find("2610-461-g00000000") != std::string::npos);
    // A stamp that only starts the same is another stamp.
    EXPECT_EQ(riftwii::plugin_check_versions(header, RWP_API_VERSION, "2610-460-g1a2b3c4", why), PluginRefusal::Stamp);
}

static void TestRelocation() {
    Spec spec;
    spec.image_bytes = 64;
    spec.bss_bytes = 32;
    spec.init = 0x20;  // offset 0 is the reserved word
    spec.shutdown = 0;
    spec.table = 0;
    // At base 0x933A7FC0 the target 0x50 is 0x933A8010: a low half of
    // 0x8010, which an addi takes as negative, so HA adds one to the high
    // half; the target 0x10 (0x933A7FD0) needs no such carry.
    spec.relocs = {
        {0x00, riftwii::kRelocAddr32, 0x10},    // a pointer to the image
        {0x04, riftwii::kRelocAddr32, 0x50},    // to the .bss
        {0x08, riftwii::kRelocAddr16Hi, 0x50},
        {0x0A, riftwii::kRelocAddr16Lo, 0x50},
        {0x0C, riftwii::kRelocAddr16Ha, 0x50},
        {0x0E, riftwii::kRelocAddr16Ha, 0x10},
        {0x10, riftwii::kRelocAddr32, 0x60},    // the end of the .bss itself
    };
    const Bytes file = MakePlugin(spec);
    PluginHeader header;
    std::string why;
    EXPECT_EQ(Parse(file, header, why), PluginRefusal::None);
    Bytes image(file.begin() + riftwii::kPluginHeaderBytes, file.begin() + riftwii::kPluginHeaderBytes + 64);
    const std::uint8_t* relocs = file.data() + riftwii::kPluginHeaderBytes + 64;
    const std::uint32_t base = 0x933A7FC0;
    EXPECT_TRUE(riftwii::plugin_relocate(image.data(), header, relocs, base, why));
    EXPECT_EQ(Get32(image, 0x00), base + 0x10);
    EXPECT_EQ(Get32(image, 0x04), base + 0x50);
    EXPECT_EQ(Get16(image, 0x08), 0x933Au);
    EXPECT_EQ(Get16(image, 0x0A), 0x8010u);
    EXPECT_EQ(Get16(image, 0x0C), 0x933Bu);
    EXPECT_EQ(Get16(image, 0x0E), 0x933Au);
    EXPECT_EQ(Get32(image, 0x10), base + 0x60);
    // The other bytes stay as they were.
    EXPECT_EQ(image[0x14], file[riftwii::kPluginHeaderBytes + 0x14]);

    // A bad record anywhere: refused, and the image left as it was.
    const auto refused = [&](Reloc bad, const char* says) {
        Spec s = spec;
        s.relocs.push_back(bad);
        const Bytes f = MakePlugin(s);
        PluginHeader h;
        std::string reason;
        EXPECT_EQ(Parse(f, h, reason), PluginRefusal::None);
        Bytes img(f.begin() + riftwii::kPluginHeaderBytes, f.begin() + riftwii::kPluginHeaderBytes + 64);
        const Bytes before = img;
        EXPECT_FALSE(riftwii::plugin_relocate(img.data(), h, f.data() + riftwii::kPluginHeaderBytes + 64, base, reason));
        EXPECT_TRUE(img == before);
        EXPECT_TRUE(reason.find(says) != std::string::npos);
    };
    refused({0x3E, riftwii::kRelocAddr32, 0}, "outside the image");   // a word running past the end
    refused({0x40, riftwii::kRelocAddr16Lo, 0}, "outside the image");
    refused({0x00, riftwii::kRelocAddr32, 0x61}, "past the image");    // past the .bss
    refused({0x00, 10, 0}, "cannot apply");                            // a branch: relative, never listed
    refused({0x00, 0, 0}, "cannot apply");
}

static void TestNames() {
    EXPECT_TRUE(riftwii::plugin_name_valid("selftest"));
    EXPECT_TRUE(riftwii::plugin_name_valid("gc_native-2"));
    EXPECT_TRUE(riftwii::plugin_name_valid("abcdefghijklmnop"));  // 16
    EXPECT_FALSE(riftwii::plugin_name_valid("abcdefghijklmnopq"));
    EXPECT_FALSE(riftwii::plugin_name_valid(""));
    EXPECT_FALSE(riftwii::plugin_name_valid("a.b"));
    EXPECT_FALSE(riftwii::plugin_name_valid("../x"));

    EXPECT_EQ(riftwii::plugin_file_name("selftest", "2610-460"), std::string("selftest.2610-460.rwp"));
    std::string name;
    std::string version;
    EXPECT_TRUE(riftwii::plugin_file_parse("selftest.2610-460.rwp", name, version));
    EXPECT_EQ(name, std::string("selftest"));
    EXPECT_EQ(version, std::string("2610-460"));
    EXPECT_FALSE(riftwii::plugin_file_parse("selftest.rwp", name, version));
    EXPECT_FALSE(riftwii::plugin_file_parse("selftest.2610.460.rwp", name, version));
    EXPECT_FALSE(riftwii::plugin_file_parse("selftest.2610-460.dol", name, version));
    EXPECT_FALSE(riftwii::plugin_file_parse("../x.2610.rwp", name, version));
    EXPECT_FALSE(riftwii::plugin_file_parse(".2610.rwp", name, version));

    // Another version's file (left from before an update) and this one's.
    name.clear();
    EXPECT_TRUE(riftwii::plugin_file_of_other_version("selftest.2610-459.rwp", "2610-460", name));
    EXPECT_EQ(name, std::string("selftest"));
    EXPECT_FALSE(riftwii::plugin_file_of_other_version("selftest.2610-460.rwp", "2610-460", name));
    EXPECT_FALSE(riftwii::plugin_file_of_other_version("notes.txt", "2610-460", name));
}

static void TestDirectory() {
    EXPECT_EQ(riftwii::plugin_directory_for("sd:/apps/riftwii/boot.dol"), std::string("sd:/apps/riftwii/plugins/"));
    EXPECT_EQ(riftwii::plugin_directory_for("usb:/apps/riftwii/boot.dol"), std::string("usb:/apps/riftwii/plugins/"));
    EXPECT_EQ(riftwii::plugin_directory_for("sd:/apps/riftwii-test/boot.dol"),
              std::string("sd:/apps/riftwii-test/plugins/"));
    // An older loader's path without the drive is the SD card's.
    EXPECT_EQ(riftwii::plugin_directory_for("/apps/riftwii/boot.dol"), std::string("sd:/apps/riftwii/plugins/"));
    // Nothing given (Dolphin, wiiload) or no usable path: the default.
    EXPECT_EQ(riftwii::plugin_directory_for(""), std::string("sd:/apps/riftwii/plugins/"));
    EXPECT_EQ(riftwii::plugin_directory_for("boot.dol"), std::string("sd:/apps/riftwii/plugins/"));
    EXPECT_EQ(riftwii::plugin_directory_for("sd:/boot.dol"), std::string("sd:/apps/riftwii/plugins/"));
    EXPECT_EQ(riftwii::plugin_directory_for("SD:/apps/x/boot.dol"), std::string("sd:/apps/riftwii/plugins/"));
}

static void TestDescribeAddress() {
    const riftwii::PluginRange ranges[] = {
        {"selftest", 0x933A0000, 0x800},
        {"selftest scratch", 0x933A0800, 0x1000},
        {"empty", 0x93000000, 0},
    };
    char out[64] = "untouched";
    EXPECT_TRUE(riftwii::plugin_describe_address(0x933A0000, ranges, 3, out, sizeof(out)));
    EXPECT_EQ(std::string(out), std::string("plugin selftest +0x0"));
    EXPECT_TRUE(riftwii::plugin_describe_address(0x933A07FC, ranges, 3, out, sizeof(out)));
    EXPECT_EQ(std::string(out), std::string("plugin selftest +0x7fc"));
    EXPECT_TRUE(riftwii::plugin_describe_address(0x933A0800, ranges, 3, out, sizeof(out)));
    EXPECT_EQ(std::string(out), std::string("plugin selftest scratch +0x0"));
    std::strcpy(out, "untouched");
    EXPECT_FALSE(riftwii::plugin_describe_address(0x933A1800, ranges, 3, out, sizeof(out)));
    EXPECT_FALSE(riftwii::plugin_describe_address(0x9339FFFC, ranges, 3, out, sizeof(out)));
    EXPECT_FALSE(riftwii::plugin_describe_address(0x93000000, ranges, 3, out, sizeof(out)));
    EXPECT_FALSE(riftwii::plugin_describe_address(0x80A00000, ranges, 0, out, sizeof(out)));
    EXPECT_EQ(std::string(out), std::string("untouched"));
}

// The tables' version rules (include/riftwii/plugin_api.h): each starts
// with its size and the API version, later functions only at the end, and
// RWP_HOST_HAS tells a plugin whether a shorter table has a function.
static void TestTables() {
    EXPECT_EQ(RWP_API_VERSION, 2);
    EXPECT_EQ(offsetof(rwp_host, size), 0u);
    EXPECT_EQ(offsetof(rwp_host, api_version), 4u);
    EXPECT_EQ(offsetof(rwp_plugin, size), 0u);
    EXPECT_EQ(offsetof(rwp_plugin, api_version), 4u);
    EXPECT_EQ(offsetof(rwp_plugin, version), 8u);
    EXPECT_EQ(offsetof(rwp_plugin, flags), 12u);
    // API 1's order: the groups as docs/PLUGINS.md lists them.
    EXPECT_TRUE(offsetof(rwp_host, log) < offsetof(rwp_host, arena_alloc));
    EXPECT_TRUE(offsetof(rwp_host, arena_alloc) < offsetof(rwp_host, mem1_alloc));
    EXPECT_TRUE(offsetof(rwp_host, sync_code) < offsetof(rwp_host, file_read));
    EXPECT_TRUE(offsetof(rwp_host, file_size) < offsetof(rwp_host, version));
    EXPECT_TRUE(offsetof(rwp_host, build_stamp) < offsetof(rwp_host, crash_register));
    // API 2's groups follow API 1's, in docs/PLUGINS.md's order ("Stage
    // (b)"): the C runtime, log, files, the SD card raw, system, Zstandard.
    EXPECT_TRUE(offsetof(rwp_host, crash_register) < offsetof(rwp_host, format));
    EXPECT_TRUE(offsetof(rwp_host, fill_bytes) < offsetof(rwp_host, log_text));
    EXPECT_TRUE(offsetof(rwp_host, log_close) < offsetof(rwp_host, stream_open));
    EXPECT_TRUE(offsetof(rwp_host, make_directory) < offsetof(rwp_host, sd_file_fragments));
    EXPECT_TRUE(offsetof(rwp_host, sd_hand_over) < offsetof(rwp_host, ios_open));
    EXPECT_TRUE(offsetof(rwp_host, shut_down_for_game) < offsetof(rwp_host, zstd_decompress));
    EXPECT_TRUE(offsetof(rwp_host, zstd_error_name) < offsetof(rwp_host, usb_read));
    EXPECT_EQ(offsetof(rwp_host, usb_read) + sizeof(void*), offsetof(rwp_host, set_sram_flash_id_a));
    EXPECT_EQ(offsetof(rwp_host, set_sram_flash_id_a) + sizeof(void*), sizeof(rwp_host));
    // Every entry is one pointer, none skipped: on the Wii (4-byte
    // pointers) the table is 2 words and 38 functions.
    EXPECT_EQ(sizeof(rwp_host), 8u + 38u * sizeof(void*));
    EXPECT_TRUE(offsetof(rwp_plugin, probe) < offsetof(rwp_plugin, shutdown));
    EXPECT_TRUE(offsetof(rwp_plugin, shutdown) < offsetof(rwp_plugin, launch));
    EXPECT_EQ(offsetof(rwp_plugin, launch) + sizeof(void*), sizeof(rwp_plugin));
    // The out-structures the Wii lays out the same everywhere: fixed
    // fields, no pointers.
    EXPECT_EQ(sizeof(rwp_path_info), 16u);
    EXPECT_EQ(sizeof(rwp_fragment), 16u);
    EXPECT_EQ(sizeof(rwp_sd_card), 12u);
    EXPECT_EQ(sizeof(rwp_sram), 20u);
    // A whole table has every function; one cut before the last has not.
    rwp_host host{};
    host.size = sizeof(rwp_host);
    EXPECT_TRUE(RWP_HOST_HAS(&host, set_sram_flash_id_a));
    // Before the SRAM setter: usb_read still has its original offset.
    host.size = static_cast<std::uint32_t>(offsetof(rwp_host, set_sram_flash_id_a));
    EXPECT_FALSE(RWP_HOST_HAS(&host, set_sram_flash_id_a));
    EXPECT_TRUE(RWP_HOST_HAS(&host, usb_read));
    // An API 2 table from before usb_read: still a whole API 2 table.
    host.size = static_cast<std::uint32_t>(offsetof(rwp_host, usb_read));
    EXPECT_FALSE(RWP_HOST_HAS(&host, usb_read));
    EXPECT_TRUE(RWP_HOST_HAS(&host, zstd_error_name));
    host.size = static_cast<std::uint32_t>(offsetof(rwp_host, zstd_error_name));
    EXPECT_FALSE(RWP_HOST_HAS(&host, zstd_error_name));
    EXPECT_TRUE(RWP_HOST_HAS(&host, zstd_is_error));
    // An API 1 loader's table ends at crash_register: none of API 2's.
    host.size = static_cast<std::uint32_t>(offsetof(rwp_host, crash_register) + sizeof(void*));
    EXPECT_TRUE(RWP_HOST_HAS(&host, crash_register));
    EXPECT_FALSE(RWP_HOST_HAS(&host, format));
    // The arena holds the largest plugin the header accepts.
    EXPECT_EQ(RWP_ARENA_BYTES, 256u * 1024u);
}

// tests/fixtures/plugin_selftest.rwp is the selftest plugin as
// tools/make_plugin.py built it (Makefile.plugin), re-stamped with
// `make_plugin.py variant --stamp fixture-2610-1-g00000000`: the Python
// writer and the C++ reader agree on the layout, the hash and the
// relocations.
static void TestToolFixture(const std::string& fixtures) {
    std::ifstream in(fixtures + "/plugin_selftest.rwp", std::ios::binary);
    const Bytes file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_TRUE(file.size() > riftwii::kPluginHeaderBytes);
    if (file.size() <= riftwii::kPluginHeaderBytes) return;
    PluginHeader header;
    std::string why;
    EXPECT_EQ(Parse(file, header, why), PluginRefusal::None);
    EXPECT_EQ(header.name, std::string("selftest"));
    EXPECT_EQ(header.stamp, std::string("fixture-2610-1-g00000000"));
    EXPECT_TRUE(riftwii::plugin_hash_matches(file.data(), file.data() + riftwii::kPluginHeaderBytes,
                                             file.size() - riftwii::kPluginHeaderBytes));
    EXPECT_EQ(riftwii::plugin_check_versions(header, header.api_version, "fixture-2610-1-g00000000", why),
              PluginRefusal::None);
    EXPECT_EQ(riftwii::plugin_check_versions(header, header.api_version, "2610-1-g00000000", why), PluginRefusal::Stamp);
    EXPECT_TRUE(header.reloc_count > 0);
    // Relocated at a MEM2 address, its rwp_plugin table points into itself:
    // probe and shutdown are relocated words, shutdown the header's entry;
    // it starts nothing, so its launch is NULL.
    Bytes memory(header.memory_bytes(), 0);
    std::copy(file.begin() + riftwii::kPluginHeaderBytes,
              file.begin() + riftwii::kPluginHeaderBytes + header.image_bytes, memory.begin());
    const std::uint32_t base = 0x933C0000;
    EXPECT_TRUE(riftwii::plugin_relocate(memory.data(), header,
                                         file.data() + riftwii::kPluginHeaderBytes + header.image_bytes, base, why));
    const std::size_t table = header.table_offset;
    EXPECT_EQ(Get32(memory, table + 4), header.api_version);
    EXPECT_EQ(Get32(memory, table + 8), header.plugin_version);
    const std::uint32_t probe = Get32(memory, table + 16);
    const std::uint32_t shutdown = Get32(memory, table + 20);
    EXPECT_TRUE(probe >= base && probe < base + header.image_bytes);
    EXPECT_EQ(shutdown, base + header.shutdown_offset);
    EXPECT_EQ(Get32(memory, table + 24), 0u);
    EXPECT_EQ(riftwii::plugin_check_table(memory.data(), header, base, header.api_version, why), PluginRefusal::None);
}

// The in-app update (riftwii/update.hpp's plan_update_writes): the
// release's plugins first, next to the running DOL, then the DOL last; the
// rest of the apps part waits for the new DOL's first start.
static void TestUpdateOrder() {
    using riftwii::ThemePack;
    using riftwii::ThemePackFile;
    using riftwii::UpdateWrite;
    ThemePack apps;
    apps.files = {
        ThemePackFile{"riftwii_channel/boot.dol", 0, 100},
        ThemePackFile{"riftwii/plugins/gamecube.2610-500.rwp", 100, 4000},
        ThemePackFile{"riftwii/icon.png", 4100, 50},
        ThemePackFile{"riftwii/plugins/selftest.2610-500.rwp", 4150, 300},
    };
    const std::string folder = "sd:/apps/riftwii/plugins/";
    const std::string dol = "sd:/apps/riftwii/boot.dol";
    std::vector<UpdateWrite> writes;
    std::string why;
    EXPECT_TRUE(riftwii::plan_update_writes(apps, "v2610-500", folder, dol, writes, why));
    EXPECT_EQ(writes.size(), 3u);
    if (writes.size() == 3) {
        EXPECT_EQ(writes[0].path, folder + "gamecube.2610-500.rwp");
        EXPECT_EQ(writes[0].offset, 100u);
        EXPECT_EQ(writes[0].size, 4000u);
        EXPECT_FALSE(writes[0].dol);
        EXPECT_EQ(writes[1].path, folder + "selftest.2610-500.rwp");
        // The DOL, last, whatever order the pack had.
        EXPECT_EQ(writes[2].path, dol);
        EXPECT_TRUE(writes[2].dol);
    }
    // The tag without its 'v' is the same release.
    EXPECT_TRUE(riftwii::plan_update_writes(apps, "2610-500", folder, dol, writes, why));
    EXPECT_EQ(writes.size(), 3u);
    // A pack with no plugins: the DOL alone.
    ThemePack plain;
    plain.files = {ThemePackFile{"riftwii/icon.png", 0, 50}};
    EXPECT_TRUE(riftwii::plan_update_writes(plain, "v2610-500", folder, dol, writes, why));
    EXPECT_EQ(writes.size(), 1u);
    EXPECT_TRUE(!writes.empty() && writes.back().dol);
    // A plugin of another version is refused, and nothing is planned.
    ThemePack mixed = apps;
    mixed.files.push_back(ThemePackFile{"riftwii/plugins/gamecube.2610-499.rwp", 4450, 4000});
    EXPECT_FALSE(riftwii::plan_update_writes(mixed, "v2610-500", folder, dol, writes, why));
    EXPECT_TRUE(writes.empty());
    EXPECT_TRUE(why.find("2610-499") != std::string::npos);
    // A name the loader never reads, or one in a folder below, too.
    ThemePack odd;
    odd.files = {ThemePackFile{"riftwii/plugins/old/gamecube.2610-500.rwp", 0, 10}};
    EXPECT_FALSE(riftwii::plan_update_writes(odd, "v2610-500", folder, dol, writes, why));
    odd.files = {ThemePackFile{"riftwii/plugins/gamecube.rwp", 0, 10}};
    EXPECT_FALSE(riftwii::plan_update_writes(odd, "v2610-500", folder, dol, writes, why));
    // No folder (or one without its slash), no DOL, no version.
    EXPECT_FALSE(riftwii::plan_update_writes(apps, "v2610-500", "sd:/apps/riftwii/plugins", dol, writes, why));
    EXPECT_FALSE(riftwii::plan_update_writes(apps, "v2610-500", folder, "", writes, why));
    EXPECT_FALSE(riftwii::plan_update_writes(apps, "v", folder, dol, writes, why));
}

int main(int argc, char** argv) {
    TestSha256();
    TestGoodHeader();
    TestDamagedHeaders();
    TestTable();
    TestHash();
    TestVersions();
    TestRelocation();
    TestNames();
    TestDirectory();
    TestDescribeAddress();
    TestTables();
    TestUpdateOrder();
    if (argc > 1) {
        TestToolFixture(argv[1]);
    } else {
        std::cerr << "FAILED: no fixtures folder given" << std::endl;
        g_failures++;
    }
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "plugin tests passed" << std::endl;
    return 0;
}
