// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// CTGP-R HASH updates (riftwii/ctgphashupdate.hpp) and the raw, in-place
// writes into jelly.bin that leave its directory entry, and so its key,
// untouched (fat32_write_in_place). The lists are made-up ones signed with a
// throwaway test key (as in ctgphash_tests); CTGP-R's key is never used to
// sign anything.
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "fat32_image.hpp"
#include "riftwii/ctgpefa.hpp"
#include "riftwii/ctgphashupdate.hpp"
#include "riftwii/ctgpjournal.hpp"
#include "riftwii/ctgppackwrite.hpp"
#include "riftwii/fat32.hpp"
#include "riftwii/wiicrypt.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;
namespace fs = std::filesystem;

static const std::uint8_t kTestP[64] = {
    0xAC, 0x45, 0xAB, 0x22, 0x82, 0xF4, 0xBD, 0xED, 0xA5, 0x08, 0x27, 0x00,
    0x4C, 0xE7, 0xB5, 0x57, 0x2E, 0xFF, 0xB0, 0xDC, 0xF6, 0x5A, 0x78, 0xE3,
    0x81, 0x73, 0xD2, 0x2D, 0xA8, 0x83, 0x88, 0x13, 0xC5, 0xA6, 0x0B, 0xFC,
    0x16, 0xB0, 0xBD, 0x43, 0x4B, 0x1E, 0xE2, 0x39, 0xFE, 0x64, 0x14, 0xBA,
    0x6F, 0x7F, 0xE9, 0xC4, 0x71, 0x9B, 0xE4, 0xE7, 0xB2, 0x83, 0xD7, 0x44,
    0x0E, 0x18, 0x82, 0x73,
};
static const std::uint8_t kTestQ[20] = {
    0xB3, 0x82, 0x4B, 0xC1, 0xB9, 0xEF, 0x94, 0x1A, 0x8C, 0xFE, 0x7E, 0xA8,
    0xDF, 0x7A, 0x7A, 0xAB, 0xE8, 0xB2, 0x7F, 0x35,
};
static const std::uint8_t kTestG[64] = {
    0x15, 0x08, 0xCE, 0xAB, 0xA0, 0x86, 0x99, 0xFE, 0x48, 0x8E, 0x62, 0xE8,
    0x0C, 0x24, 0xFB, 0x78, 0x33, 0x8B, 0xA3, 0x2E, 0x5D, 0x18, 0xDE, 0x94,
    0xA9, 0x56, 0xC0, 0xEE, 0x0A, 0xE2, 0x49, 0x53, 0x02, 0x9A, 0x19, 0x49,
    0xC4, 0x59, 0x51, 0x7D, 0xF1, 0xA0, 0x98, 0x0D, 0xA3, 0xAA, 0xA9, 0xED,
    0x1A, 0x72, 0x01, 0x8E, 0x11, 0xC1, 0x70, 0xFC, 0x2C, 0xDC, 0x63, 0x65,
    0x1F, 0xE3, 0x8D, 0x58,
};
static const std::uint8_t kTestY[64] = {
    0x1D, 0x85, 0xC9, 0x1B, 0x7F, 0x78, 0xA3, 0x33, 0xA6, 0x2F, 0xB8, 0x3C,
    0xE5, 0x50, 0xD5, 0x71, 0x4F, 0x00, 0x7A, 0x19, 0x9C, 0xD3, 0x54, 0xF0,
    0xC0, 0xE7, 0xDD, 0x21, 0xC2, 0xE6, 0x91, 0x5D, 0x2B, 0x3F, 0xC5, 0x8D,
    0xC0, 0x1D, 0x92, 0x3E, 0x61, 0xF4, 0x89, 0x4F, 0xEB, 0xE2, 0x29, 0x5D,
    0x6A, 0x8F, 0xD2, 0x2F, 0x0E, 0xC2, 0xDF, 0xEC, 0x51, 0xCE, 0x28, 0x17,
    0xB4, 0x3F, 0x6A, 0x12,
};
static const std::uint8_t kTestList[203] = {
    0x48, 0x61, 0x68, 0x30, 0x00, 0x00, 0x00, 0xCB, 0x00, 0x00, 0x00, 0x00,
    0xE2, 0x83, 0xB8, 0x1B, 0xDD, 0x64, 0x49, 0x79, 0x3B, 0x89, 0x42, 0xCF,
    0xCC, 0x08, 0x17, 0x6C, 0xD3, 0xC5, 0xB7, 0x36, 0x54, 0x45, 0x53, 0x54,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x00, 0x00, 0x00, 0x2E,
    0x30, 0x2C, 0x02, 0x14, 0x6F, 0x76, 0x0B, 0xE6, 0x35, 0x3D, 0xC7, 0x5E,
    0xFD, 0xB3, 0x3C, 0x18, 0xAB, 0xD2, 0x93, 0x84, 0xD2, 0xF9, 0x93, 0xB6,
    0x02, 0x14, 0x6B, 0x0A, 0x30, 0x6C, 0xBB, 0x8D, 0x86, 0xCA, 0xAA, 0xCF,
    0x50, 0xEE, 0x3D, 0x64, 0x81, 0x00, 0x82, 0xB0, 0xD5, 0xEC, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04,
    0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x43, 0x00, 0x00, 0x00, 0x14,
    0x00, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x57, 0x00, 0x00, 0x00, 0x14,
    0x00, 0x41, 0x00, 0x6F, 0x6E, 0x65, 0x2E, 0x62, 0x69, 0x6E, 0x00, 0x74,
    0x77, 0x6F, 0x2E, 0x62, 0x69, 0x6E, 0x00, 0xFE, 0x05, 0xBC, 0xDC, 0xDC,
    0x49, 0x28, 0x01, 0x27, 0x81, 0xA5, 0xF1, 0xA2, 0xA7, 0x7C, 0xBB, 0x53,
    0x98, 0xE1, 0x06, 0xAD, 0x78, 0x2E, 0xCD, 0xAC, 0x77, 0x0F, 0xC6, 0xEB,
    0x9A, 0x62, 0xE4, 0x4F, 0x90, 0x87, 0x3F, 0xB9, 0x7F, 0xB2, 0x6B,
};
static const std::uint8_t kTestListOtherVersion[203] = {
    0x48, 0x61, 0x68, 0x30, 0x00, 0x00, 0x00, 0xCB, 0x00, 0x00, 0x00, 0x00,
    0xE2, 0x83, 0xB8, 0x1B, 0xDD, 0x64, 0x49, 0x79, 0x3B, 0x89, 0x42, 0xCF,
    0xCC, 0x08, 0x17, 0x6C, 0xD3, 0xC5, 0xB7, 0x36, 0x54, 0x45, 0x53, 0x54,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x05, 0x00, 0x00, 0x00, 0x2F,
    0x30, 0x2D, 0x02, 0x14, 0x47, 0xF9, 0xB9, 0x42, 0x0B, 0xBE, 0xDB, 0x42,
    0xE4, 0x19, 0x94, 0x11, 0x72, 0xBE, 0xCB, 0x47, 0x29, 0x32, 0x7A, 0xFF,
    0x02, 0x15, 0x00, 0xB0, 0x72, 0xCC, 0xEF, 0x4A, 0x2E, 0xCA, 0x00, 0xE8,
    0xFE, 0x2A, 0x70, 0x3C, 0xAB, 0x4C, 0xE0, 0x2C, 0x39, 0xC1, 0x71, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04,
    0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x43, 0x00, 0x00, 0x00, 0x14,
    0x00, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x57, 0x00, 0x00, 0x00, 0x14,
    0x00, 0x41, 0x00, 0x6F, 0x6E, 0x65, 0x2E, 0x62, 0x69, 0x6E, 0x00, 0x74,
    0x77, 0x6F, 0x2E, 0x62, 0x69, 0x6E, 0x00, 0xFE, 0x05, 0xBC, 0xDC, 0xDC,
    0x49, 0x28, 0x01, 0x27, 0x81, 0xA5, 0xF1, 0xA2, 0xA7, 0x7C, 0xBB, 0x53,
    0x98, 0xE1, 0x06, 0xAD, 0x78, 0x2E, 0xCD, 0xAC, 0x77, 0x0F, 0xC6, 0xEB,
    0x9A, 0x62, 0xE4, 0x4F, 0x90, 0x87, 0x3F, 0xB9, 0x7F, 0xB2, 0x6B,
};

namespace {

CtgpDsaKey test_key() {
    CtgpDsaKey k;
    std::memcpy(k.p, kTestP, 64);
    std::memcpy(k.q, kTestQ, 20);
    std::memcpy(k.g, kTestG, 64);
    std::memcpy(k.y, kTestY, 64);
    return k;
}

const CtgpEfbKeyWords kWords = {0x1234567u, 0x89ABCDEFu};
const CtgpChunkIv kIv = {0x11111111u, 0x22222222u, 0x33333333u};
constexpr std::uint32_t kChunk = kCtgpEfaChunkBytes;
// jelly.bin's entry on the card: created 2026-11-12 18:49:20, written the same.
constexpr std::uint16_t kTime = 38544, kDate = 23916;

void be(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) out.push_back(std::uint8_t(v >> s));
}
void name8(std::vector<std::uint8_t>& out, const char* n) {
    char b[8] = {};
    std::memcpy(b, n, std::strlen(n));
    out.insert(out.end(), b, b + 8);
}

std::vector<std::uint8_t> packages_bin(std::uint32_t hash_version) {
    std::vector<std::uint8_t> p = {'P', 'A', 'K', '0'};
    be(p, 0x40); be(p, 0); be(p, 3);
    name8(p, "RMC"); be(p, 0x01000000u); be(p, 0x09000000u);
    name8(p, "HASH"); be(p, hash_version); be(p, 0x0B000000u);
    name8(p, "RMCP"); be(p, 0x01000000u); be(p, 0x09000000u);
    return p;
}

// Damage to the inner disk, for telling a list that is not there from a disk
// that can't be read right.
enum class Damage {
    None,
    HashOutside,     // /PACKAGES/HASH's cluster lies outside the disk
    HashIsFile,      // /PACKAGES/HASH is a file, not a folder
    HashFullEnd,     // /PACKAGES/HASH fills its cluster (no end mark) and its chain ends there: clean
    HashRunsOff,     // the same, but its chain runs off the disk
    Far,             // no damage: /PACKAGES/HASH and its list in later 32 KiB chunks than the root
};

// jelly.bin's inner disk, plain: /PACKAGES.BIN and /PACKAGES/HASH/TEST.HSH,
// 4 KiB clusters; `clusters` sets how much is free.
fatimg::Bytes inner_plain(const std::vector<std::uint8_t>& list, std::uint32_t clusters,
                          Damage damage = Damage::None) {
    fatimg::Image img(512, 8, 0, clusters);
    fatimg::Bytes root = fatimg::short_entry("PACKAGESBIN", 0x20, 4, 0x40);
    const fatimg::Bytes dir = fatimg::short_entry("PACKAGES   ", 0x10, 3, 0);
    root.insert(root.end(), dir.begin(), dir.end());
    img.write_dir({2}, root);
    const bool far = damage == Damage::Far;
    const std::uint32_t hash_cluster = far ? 20u : 5u, list_cluster = far ? 30u : 6u;
    img.write_dir({3}, fatimg::short_entry("HASH       ", damage == Damage::HashIsFile ? 0x20 : 0x10,
                                           damage == Damage::HashOutside ? 0x0FFFFF00u : hash_cluster, 0));
    fatimg::Bytes hash = fatimg::short_entry("TEST    HSH", 0x20, list_cluster, std::uint32_t(list.size()));
    if (damage == Damage::HashFullEnd || damage == Damage::HashRunsOff) {
        // No end mark: every other slot a deleted entry.
        while (hash.size() < img.cluster_bytes()) {
            fatimg::Bytes gone = fatimg::short_entry("GONE    HSH", 0x20, 0, 0);
            gone[0] = 0xE5;
            hash.insert(hash.end(), gone.begin(), gone.end());
        }
    }
    img.write_dir({hash_cluster}, hash);
    img.write_data({4}, packages_bin(0x01031374u));
    img.write_data({list_cluster}, list);
    // Every other cluster in use, so the disk is as full as asked.
    std::vector<std::uint32_t> filler;
    for (std::uint32_t c = 7; c < 2 + clusters; ++c)
        if (c != hash_cluster && c != list_cluster) filler.push_back(c);
    if (!filler.empty()) img.chain(filler);
    if (damage == Damage::HashRunsOff) img.set_fat(5, 0x0FFFFF00u);
    fatimg::Bytes out = img.bytes;
    out.resize((out.size() + kChunk - 1) / kChunk * kChunk, 0);
    return out;
}

std::array<std::uint8_t, 16> jelly_key(const Fat32Entry& e) {
    return ctgp_efb_key(kWords, e.write_time, e.write_date, e.create_time, e.create_date);
}

fatimg::Bytes encrypt(const fatimg::Bytes& plain, const std::array<std::uint8_t, 16>& key) {
    fatimg::Bytes out(plain.size());
    const Aes128 aes(key.data());
    for (std::size_t at = 0; at < plain.size(); at += kChunk) {
        const auto iv = ctgp_efa_chunk_iv(kIv, std::uint32_t(at / 512));
        aes.cbc_encrypt(iv.data(), plain.data() + at, out.data() + at, kChunk);
    }
    return out;
}

// The SD card: /CTGPR/JELLY.BIN in two pieces (clusters 10.. and 300..),
// its entry carrying the times its key is made from, and a neighbour file
// in the same directory sector.
struct Card {
    fatimg::Image img{512, 8, 2048, 700};
    std::uint64_t jelly_size = 0;
};

Card make_card(const fatimg::Bytes& encrypted) {
    Card card;
    card.jelly_size = encrypted.size();
    fatimg::Image& img = card.img;
    img.write_dir({2}, fatimg::short_entry("CTGPR      ", 0x10, 3, 0));
    fatimg::Bytes jelly = fatimg::short_entry("JELLY   BIN", 0x20, 10, std::uint32_t(encrypted.size()));
    fatimg::put16(jelly.data() + 0x0E, kTime);
    fatimg::put16(jelly.data() + 0x10, kDate);
    fatimg::put16(jelly.data() + 0x12, kDate);
    fatimg::put16(jelly.data() + 0x16, kTime);
    fatimg::put16(jelly.data() + 0x18, kDate);
    const fatimg::Bytes other = fatimg::short_entry("BLOB    BIN", 0x20, 600, 4096);
    jelly.insert(jelly.end(), other.begin(), other.end());
    img.write_dir({3}, jelly);
    const std::uint32_t need = std::uint32_t(encrypted.size() / img.cluster_bytes());
    std::vector<std::uint32_t> chain;
    for (std::uint32_t i = 0; i < need; ++i) chain.push_back(i < need / 2 ? 10 + i : 300 + i);
    img.write_data(chain, encrypted);
    img.write_data({600}, fatimg::Bytes(4096, 0x5A));
    return card;
}

struct Raw {
    Card* card;
    Fat32File file;
    int writes_left = -1;  // -1: no limit; else the card dies after this many writes
    int writes = 0;
};

CtgpPackIo raw_io(Raw& raw) {
    CtgpPackIo io;
    io.read = [&raw](std::uint64_t offset, std::uint32_t length, std::uint8_t* out) {
        // Through the file's fragments, as the DOL's raw reads go.
        if (offset + length > raw.file.entry.size) return false;
        std::vector<PlacedRun> runs;
        std::string e;
        if (!place_on_fragments(raw.file.fragments, offset, length, runs, e)) return false;
        for (const PlacedRun& r : runs) {
            std::memcpy(out, raw.card->img.bytes.data() + r.source * 512 + r.skip, std::size_t(r.length));
            out += r.length;
        }
        return true;
    };
    io.write = [&raw](std::uint64_t offset, const std::uint8_t* data, std::uint32_t length) {
        const BlockWriter writer = [&raw](std::uint64_t lba, std::uint32_t count, const std::uint8_t* in) {
            // Block by block, so a power cut can land inside a write.
            if ((lba + count) * 512 > raw.card->img.bytes.size()) return false;
            for (std::uint32_t b = 0; b < count; ++b) {
                if (raw.writes_left == 0) return false;
                if (raw.writes_left > 0) --raw.writes_left;
                ++raw.writes;
                std::memcpy(raw.card->img.bytes.data() + (lba + b) * 512, in + std::size_t(b) * 512, 512);
            }
            return true;
        };
        std::string e;
        return fat32_write_in_place(raw.file, writer, offset, data, length, e);
    };
    return io;
}

bool lookup(const Card& card, Fat32File& out) {
    Fat32Volume volume;
    std::string e;
    return Fat32Volume::mount(card.img.reader(), volume, e) && volume.lookup("/ctgpr/jelly.bin", out, e);
}

// Every byte of the card outside jelly.bin's own clusters.
fatimg::Bytes outside_jelly(const Card& card, const Fat32File& jelly) {
    fatimg::Bytes out = card.img.bytes;
    for (const Fragment& f : jelly.fragments) std::memset(out.data() + f.sector * 512, 0, std::size_t(f.sector_count) * 512);
    return out;
}


std::vector<std::uint8_t> vec(const std::uint8_t* p, std::size_t n) { return std::vector<std::uint8_t>(p, p + n); }

CtgpHashUpdateList new_list() {
    CtgpHashUpdateList l;
    l.section_path = "/test.hsh";
    l.package = "TEST";
    l.disk_path = "/PACKAGES/HASH/TEST.HSH";
    l.bytes = vec(kTestListOtherVersion, sizeof(kTestListOtherVersion));
    l.label = 0x01020305u;
    return l;
}

// A HASH DIA0 with one whole-list section, rebuilt by long literals.
std::vector<std::uint8_t> hash_dia(const std::vector<std::uint8_t>& list, const char* path) {
    std::vector<std::uint8_t> stream;
    std::size_t at = 0;
    while (at < list.size()) {
        const std::size_t n = std::min<std::size_t>(list.size() - at, 0x11 + 0xFF);
        if (n >= 0x11) {
            stream.push_back(0x01);
            stream.push_back(std::uint8_t(n - 0x11));
        } else {
            stream.push_back(std::uint8_t(((n - 1) << 4) | 0x0));
        }
        stream.insert(stream.end(), list.begin() + at, list.begin() + at + n);
        at += n;
    }
    std::vector<std::uint8_t> section = {'D', 'I', 'F', '0'};
    const std::uint32_t header = 0x20, table = header + std::uint32_t(stream.size());
    const std::uint32_t total = table + std::uint32_t(std::strlen(path)) + 1;
    be(section, total); be(section, 0); be(section, table); be(section, header); be(section, 0);
    be(section, std::uint32_t(list.size())); be(section, 0);
    section.insert(section.end(), stream.begin(), stream.end());
    section.insert(section.end(), path, path + std::strlen(path) + 1);
    std::vector<std::uint8_t> d = {'D', 'I', 'A', '0'};
    const std::uint32_t first = 0x64 + 16 + 8;
    be(d, first + std::uint32_t(section.size())); be(d, 0); be(d, 0);
    name8(d, "HASH"); be(d, 0x01031375u); be(d, std::uint32_t(list.size())); be(d, 0);
    while (d.size() < 0x54) d.push_back(0);
    be(d, 1); be(d, 0x64); be(d, 1); be(d, 0x74);
    name8(d, "RMC"); be(d, 0x01000000u); be(d, 0);
    be(d, first); be(d, std::uint32_t(section.size()));
    d.insert(d.end(), section.begin(), section.end());
    return d;
}

void test_set_version() {
    std::string e;
    auto p = packages_bin(0x01031374u);
    EXPECT_TRUE(ctgp_packages_set_version(p, "HASH", 0x01031375u, e));
    EXPECT_TRUE(p[0x28] == 0x01 && p[0x29] == 0x03 && p[0x2A] == 0x13 && p[0x2B] == 0x75);
    EXPECT_TRUE(p[0x18] == 0x01 && p[0x1B] == 0x00);  // RMC untouched
    // The 0x40000000 flag goes, as the channel clears it; the type byte stays.
    p[0x2C] = std::uint8_t(p[0x2C] | 0x40);
    p[0x1C] = std::uint8_t(p[0x1C] | 0x40);
    EXPECT_TRUE(ctgp_packages_set_version(p, "HASH", 0x01031376u, e));
    EXPECT_TRUE(p[0x2C] == 0x0b && p[0x2B] == 0x76);
    EXPECT_TRUE((p[0x1C] & 0x40) != 0);  // another record's flag untouched
    EXPECT_TRUE(!ctgp_packages_set_version(p, "CTGPR", 1, e));
    p.resize(20);
    EXPECT_TRUE(!ctgp_packages_set_version(p, "HASH", 1, e));
}

void test_build() {
    const CtgpDsaKey key = test_key();
    const auto good = vec(kTestListOtherVersion, sizeof(kTestListOtherVersion));
    auto d = hash_dia(good, "/test.hsh");
    CtgpDia dia;
    std::string e;
    EXPECT_TRUE(ctgp_dia_parse(d.data(), d.size(), dia, e));
    const CtgpUpdateRead read = [&d](std::uint64_t o, std::uint8_t* out, std::size_t n) {
        if (o + n > d.size()) return false;
        std::memcpy(out, d.data() + o, n);
        return true;
    };
    std::vector<CtgpHashUpdateList> lists;
    CtgpUpdateFailure failure = CtgpUpdateFailure::None;
    EXPECT_TRUE(ctgp_hash_update_build(read, d.size(), dia, lists, failure, e, &key));
    EXPECT_TRUE(lists.size() == 1 && lists[0].bytes == good && lists[0].package == "TEST" &&
                lists[0].disk_path == "/PACKAGES/HASH/TEST.HSH" && lists[0].label == 0x01020305u);
    // CTGP-R's own key does not take a list signed with the test key.
    EXPECT_TRUE(!ctgp_hash_update_build(read, d.size(), dia, lists, failure, e));
    EXPECT_TRUE(failure == CtgpUpdateFailure::BadUpdate);
    // One changed byte of the list: refused as a bad update.
    auto bent = good;
    bent[0x70] ^= 1;
    d = hash_dia(bent, "/test.hsh");
    EXPECT_TRUE(ctgp_dia_parse(d.data(), d.size(), dia, e));
    EXPECT_TRUE(!ctgp_hash_update_build(read, d.size(), dia, lists, failure, e, &key));
    EXPECT_TRUE(failure == CtgpUpdateFailure::BadUpdate);
    // Not a hash list's name, or another package's name than the list's own.
    d = hash_dia(good, "/test.bin");
    EXPECT_TRUE(ctgp_dia_parse(d.data(), d.size(), dia, e));
    EXPECT_TRUE(!ctgp_hash_update_build(read, d.size(), dia, lists, failure, e, &key));
    d = hash_dia(good, "/ctgpr.hsh");
    EXPECT_TRUE(ctgp_dia_parse(d.data(), d.size(), dia, e));
    EXPECT_TRUE(!ctgp_hash_update_build(read, d.size(), dia, lists, failure, e, &key));
    EXPECT_TRUE(failure == CtgpUpdateFailure::BadUpdate);
}

struct Setup {
    Card card;
    Fat32File before_entry;
    fatimg::Bytes before_card;
    std::array<std::uint8_t, 16> key{};
};

Setup setup(std::uint32_t inner_clusters, Damage damage = Damage::None) {
    Setup s;
    const auto old_list = vec(kTestList, sizeof(kTestList));
    Fat32Entry e;
    e.write_time = e.create_time = kTime;
    e.write_date = e.create_date = kDate;
    s.key = jelly_key(e);
    s.card = make_card(encrypt(inner_plain(old_list, inner_clusters, damage), s.key));
    lookup(s.card, s.before_entry);
    s.before_card = s.card.img.bytes;
    return s;
}

void test_install(const fs::path& dir) {
    const CtgpDsaKey key = test_key();
    Setup s = setup(64);
    EXPECT_TRUE(s.before_entry.fragments.size() == 2);
    Raw raw{&s.card, s.before_entry};
    bool rolled_back = false;
    std::string e;
    const std::string jp = (dir / "jelly-journal.bin").string();
    const bool ok = ctgp_hash_update_install(raw_io(raw), s.card.jelly_size, s.key, kIv, {new_list()}, 0x01031375u, jp,
                                             [] { return true; }, rolled_back, e, &key);
    if (!ok) std::cerr << "install: " << e << std::endl;
    EXPECT_TRUE(ok);
    EXPECT_TRUE(!fs::exists(jp));
    // Nothing outside jelly.bin's clusters changed: its directory entry (times,
    // size, first cluster), the neighbour's, both FATs, FSInfo, the boot sector.
    {
        fatimg::Bytes before = s.before_card;
        for (const Fragment& f : s.before_entry.fragments)
            std::memset(before.data() + f.sector * 512, 0, std::size_t(f.sector_count) * 512);
        EXPECT_TRUE(before == outside_jelly(s.card, s.before_entry));
    }
    Fat32File after;
    EXPECT_TRUE(lookup(s.card, after));
    EXPECT_TRUE(after.entry.write_time == kTime && after.entry.write_date == kDate &&
                after.entry.create_time == kTime && after.entry.create_date == kDate &&
                after.entry.size == s.before_entry.entry.size &&
                after.entry.first_cluster == s.before_entry.entry.first_cluster);
    EXPECT_TRUE(after.fragments.size() == s.before_entry.fragments.size());
    EXPECT_TRUE(jelly_key(after.entry) == s.key);
    // The new list and the HASH record read back with the re-derived key.
    CtgpPackIo ro = raw_io(raw);
    ro.write = [](std::uint64_t, const std::uint8_t*, std::uint32_t) { return false; };
    CtgpPackWriter jelly;
    EXPECT_TRUE(jelly.open(ro, s.card.jelly_size, jelly_key(after.entry), kIv, e));
    EXPECT_TRUE(ctgp_hash_update_verify(jelly, {new_list()}, 0x01031375u, e, &key));
    jelly.close();
}

void test_refusals(const fs::path& dir) {
    const CtgpDsaKey key = test_key();
    const std::string jp = (dir / "jelly-journal.bin").string();
    std::string e;
    bool rolled_back = false;
    {
        // A list that needs more room than the disk has: nothing written.
        Setup s = setup(8);
        CtgpHashUpdateList big = new_list();
        big.bytes.resize(64 * 1024);
        Raw raw{&s.card, s.before_entry};
        bool grow = false;
        EXPECT_TRUE(!ctgp_hash_update_install(raw_io(raw), s.card.jelly_size, s.key, kIv, {big}, 2, jp,
                                              [] { return true; }, rolled_back, e, &key, &grow));
        EXPECT_TRUE(rolled_back && raw.writes == 0 && s.card.img.bytes == s.before_card && !fs::exists(jp));
        EXPECT_TRUE(grow);  // the channel's to install (it grows jelly.bin), not a bad update
    }
    {
        // A list jelly.bin doesn't hold: nothing written.
        Setup s = setup(64);
        CtgpHashUpdateList other = new_list();
        other.disk_path = "/PACKAGES/HASH/OTHER.HSH";
        Raw raw{&s.card, s.before_entry};
        bool grow = false;
        EXPECT_TRUE(!ctgp_hash_update_install(raw_io(raw), s.card.jelly_size, s.key, kIv, {other}, 2, jp,
                                              [] { return true; }, rolled_back, e, &key, &grow));
        EXPECT_TRUE(raw.writes == 0 && s.card.img.bytes == s.before_card && !fs::exists(jp));
        EXPECT_TRUE(grow);  // the channel creates the list
    }
    {
        // The card stops answering reads once jelly.bin is open: the list
        // can't be looked up. A temporary failure, not "the channel's to
        // install" (that is only for a list confirmed missing, or no room).
        Setup s = setup(64);
        Raw raw{&s.card, s.before_entry};
        const CtgpPackIo base = raw_io(raw);
        int reads = 0;
        int reads_left = -1;
        CtgpPackIo counted;
        counted.read = [&](std::uint64_t offset, std::uint32_t length, std::uint8_t* out) {
            ++reads;
            if (reads_left == 0) return false;
            if (reads_left > 0) --reads_left;
            return base.read(offset, length, out);
        };
        counted.write = base.write;
        {
            // How many reads opening jelly.bin takes.
            CtgpPackIo ro = counted;
            ro.write = [](std::uint64_t, const std::uint8_t*, std::uint32_t) { return false; };
            CtgpPackWriter look;
            EXPECT_TRUE(look.open(ro, s.card.jelly_size, s.key, kIv, e));
            look.close();
        }
        const int open_reads = reads;
        // Every read after the first `extra` past opening fails, on a fresh
        // card each time, until the budget is enough for the whole install:
        // a failure is never "the channel's to install", and the card ends as
        // it was. Both the lookup and the read of the old list are reached.
        bool saw_lookup = false, saw_read = false, went_in = false;
        for (int extra = 0; extra < 400 && !went_in; ++extra) {
            Setup t = setup(64, Damage::Far);
            Raw traw{&t.card, t.before_entry};
            const CtgpPackIo tbase = raw_io(traw);
            int left = open_reads + extra;
            CtgpPackIo tio;
            tio.read = [&](std::uint64_t offset, std::uint32_t length, std::uint8_t* out) {
                if (left == 0) return false;
                --left;
                return tbase.read(offset, length, out);
            };
            tio.write = tbase.write;
            bool grow = true;
            went_in = ctgp_hash_update_install(tio, t.card.jelly_size, t.key, kIv, {new_list()}, 0x01031375u, jp,
                                               [] { return true; }, rolled_back, e, &key, &grow);
            const std::string why = e;
            EXPECT_TRUE(!grow);
            if (went_in) break;
            if (fs::exists(jp)) {
                // Cut in the read-back: the replay with a card that reads
                // puts it back.
                std::size_t restored = 0;
                EXPECT_TRUE(ctgp_journal_recover(jp, tbase, t.card.jelly_size, [] { return true; }, restored, e) !=
                            CtgpJournalRecovery::Failed);
            }
            EXPECT_TRUE(t.card.img.bytes == t.before_card && !fs::exists(jp));
            if (why.find("could not be looked up") != std::string::npos) saw_lookup = true;
            if (why.find("TEST.HSH could not be read") != std::string::npos) saw_read = true;
        }
        // The last budget was enough: the same list went in on that card.
        EXPECT_TRUE(saw_lookup && saw_read && went_in);
    }
    {
        // A damaged disk is never "the channel's to install": only a list
        // looked for in a folder read to its clean end counts as missing.
        struct Case {
            Damage damage;
            const char* path;
            bool grow;
            const char* says;
        };
        const Case cases[] = {
            {Damage::HashOutside, "/PACKAGES/HASH/TEST.HSH", false, "lies outside it"},
            {Damage::HashIsFile, "/PACKAGES/HASH/TEST.HSH", false, "is not a folder"},
            {Damage::HashRunsOff, "/PACKAGES/HASH/OTHER.HSH", false, "ends early"},
            {Damage::HashFullEnd, "/PACKAGES/HASH/OTHER.HSH", true, "has no"},  // read to its end: missing
        };
        for (const Case& c : cases) {
            Setup s = setup(64, c.damage);
            Raw raw{&s.card, s.before_entry};
            CtgpHashUpdateList list = new_list();
            list.disk_path = c.path;
            bool grow = !c.grow;
            EXPECT_TRUE(!ctgp_hash_update_install(raw_io(raw), s.card.jelly_size, s.key, kIv, {list}, 2, jp,
                                                  [] { return true; }, rolled_back, e, &key, &grow));
            EXPECT_TRUE(grow == c.grow);
            EXPECT_TRUE(e.find(c.says) != std::string::npos);
            EXPECT_TRUE(raw.writes == 0 && s.card.img.bytes == s.before_card && !fs::exists(jp));
        }
    }
    {
        // The journal can't be made (a locked card): nothing written at all.
        Setup s = setup(64);
        Raw raw{&s.card, s.before_entry};
        const std::string nowhere = (dir / "no-such-folder" / "journal.bin").string();
        bool grow = true;
        EXPECT_TRUE(!ctgp_hash_update_install(raw_io(raw), s.card.jelly_size, s.key, kIv, {new_list()}, 2, nowhere,
                                              [] { return true; }, rolled_back, e, &key, &grow));
        EXPECT_TRUE(rolled_back && raw.writes == 0 && s.card.img.bytes == s.before_card && !fs::exists(nowhere));
        EXPECT_TRUE(!grow);  // the card's fault: the update may go in next time
    }
    {
        // The card refuses jelly.bin's raw writes from the first one: that
        // write was journaled first, so the card is unchanged, and the
        // journal stays until the card takes writes again; then the replay
        // leaves jelly.bin as it was. (No write happens outside the journal.)
        Setup s = setup(64);
        Raw raw{&s.card, s.before_entry};
        raw.writes_left = 0;
        bool grow = true;
        EXPECT_TRUE(!ctgp_hash_update_install(raw_io(raw), s.card.jelly_size, s.key, kIv, {new_list()}, 2, jp,
                                              [] { return true; }, rolled_back, e, &key, &grow));
        EXPECT_TRUE(!rolled_back && s.card.img.bytes == s.before_card && fs::exists(jp));
        EXPECT_TRUE(!grow);
        raw.writes_left = -1;
        std::size_t restored = 0;
        EXPECT_TRUE(ctgp_journal_recover(jp, raw_io(raw), s.card.jelly_size, [] { return true; }, restored, e) ==
                    CtgpJournalRecovery::RolledBack);
        EXPECT_TRUE(restored == 1);  // the refused write's chunk was journaled first
        EXPECT_TRUE(s.card.img.bytes == s.before_card && !fs::exists(jp));
    }
    {
        // A read-back that doesn't check out (the wrong key for verifying):
        // rolled back to the stock bytes, journal gone.
        Setup s = setup(64);
        Raw raw{&s.card, s.before_entry};
        bool grow = true;
        EXPECT_TRUE(!ctgp_hash_update_install(raw_io(raw), s.card.jelly_size, s.key, kIv, {new_list()}, 2, jp,
                                              [] { return true; }, rolled_back, e, nullptr, &grow));
        EXPECT_TRUE(rolled_back && s.card.img.bytes == s.before_card && !fs::exists(jp));
        EXPECT_TRUE(!grow);
    }
}

// The card dies after N block writes (a power cut), for every N until the
// install gets through: the next start's replay of the journal puts jelly.bin
// back byte for byte, its entry and key unchanged.
void test_cuts(const fs::path& dir) {
    const CtgpDsaKey key = test_key();
    const std::string jp = (dir / "jelly-journal.bin").string();
    int completed_at = -1;
    for (int n = 1; n < 400 && completed_at < 0; ++n) {
        Setup s = setup(64);
        Raw raw{&s.card, s.before_entry};
        raw.writes_left = n;
        bool rolled_back = false;
        std::string e;
        const bool ok = ctgp_hash_update_install(raw_io(raw), s.card.jelly_size, s.key, kIv, {new_list()}, 0x01031375u,
                                                 jp, [] { return true; }, rolled_back, e, &key);
        if (ok) {
            completed_at = n;
            break;
        }
        // Power back: the start-up replay with a working card.
        raw.writes_left = -1;
        if (fs::exists(jp)) {
            std::size_t restored = 0;
            const CtgpJournalRecovery r =
                ctgp_journal_recover(jp, raw_io(raw), s.card.jelly_size, [] { return true; }, restored, e);
            EXPECT_TRUE(r != CtgpJournalRecovery::Failed);
        }
        EXPECT_TRUE(!fs::exists(jp));
        if (s.card.img.bytes != s.before_card) std::cerr << "cut after " << n << " writes did not recover" << std::endl;
        EXPECT_TRUE(s.card.img.bytes == s.before_card);
        Fat32File after;
        EXPECT_TRUE(lookup(s.card, after) && jelly_key(after.entry) == s.key);
    }
    EXPECT_TRUE(completed_at > 1);
    std::cout << "ctgphashupdate: cut after each of " << completed_at - 1 << " block writes, all recovered" << std::endl;
}

void test_in_place_bounds() {
    Setup s = setup(64);
    std::string e;
    int writes = 0;
    const BlockWriter w = [&](std::uint64_t, std::uint32_t, const std::uint8_t*) { ++writes; return true; };
    std::vector<std::uint8_t> block(1024, 0);
    EXPECT_TRUE(!fat32_write_in_place(s.before_entry, w, 100, block.data(), 512, e));             // off a block
    EXPECT_TRUE(!fat32_write_in_place(s.before_entry, w, 0, block.data(), 100, e));               // part block
    EXPECT_TRUE(!fat32_write_in_place(s.before_entry, w, s.card.jelly_size - 512, block.data(), 1024, e));  // past the end
    EXPECT_TRUE(writes == 0);
    EXPECT_TRUE(fat32_write_in_place(s.before_entry, w, s.card.jelly_size - 512, block.data(), 512, e));
    EXPECT_TRUE(writes == 1);
}

}  // namespace

int main() {
    fs::path dir = fs::temp_directory_path() /
                   ("ctgphashupdate_test_" + std::to_string(::getpid()) + "_" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    test_set_version();
    test_build();
    test_in_place_bounds();
    test_install(dir);
    test_refusals(dir);
    test_cuts(dir);
    fs::remove_all(dir);
    if (g_failures == 0) std::cout << "ctgphashupdate: all tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
