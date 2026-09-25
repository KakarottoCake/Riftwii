// SPDX-License-Identifier: GPL-3.0-or-later
// RVZ reading and the guards on what RiftWii will play. The images in
// tests/fixtures/rvz are made by tools/rvz/make_test_disc.py from a disc
// built from scratch; manifest.txt holds the SHA-1 of every 0x8000 bytes a
// reader must return.
#include "riftwii/hook.hpp"
#include "riftwii/redirect.hpp"
#include "riftwii/rvz.hpp"
#include "riftwii/sha1.hpp"
#include "riftwii/source.hpp"
#include "rt_hook.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32) || defined(__CYGWIN__)
#include <windows.h>
#elif defined(__linux__)
#include <sys/mman.h>
#endif

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

namespace {

std::string g_dir;

std::vector<std::uint8_t> load(const std::string& name) {
    std::ifstream f(g_dir + "/rvz/" + name, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::shared_ptr<const ByteSource> source(std::vector<std::uint8_t> bytes) {
    return std::make_shared<MemorySource>(std::move(bytes));
}

std::string hex(const Sha1Digest& d) {
    std::string s;
    char b[3];
    for (std::uint8_t c : d) {
        std::snprintf(b, sizeof b, "%02x", c);
        s += b;
    }
    return s;
}

struct Manifest {
    std::uint64_t disc_size = 0;
    std::uint64_t part_disc = 0;
    std::uint64_t part_size = 0;
    std::map<std::uint64_t, std::string> raw, part;
};

Manifest read_manifest() {
    Manifest m;
    std::ifstream f(g_dir + "/rvz/manifest.txt");
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream in(line);
        std::string key;
        in >> key;
        if (key == "disc_size") {
            in >> std::hex >> m.disc_size;
        } else if (key == "partition_data") {
            in >> std::hex >> m.part_disc >> m.part_size;
        } else if (key == "raw" || key == "part") {
            std::uint64_t off;
            std::string digest;
            in >> std::hex >> off >> digest;
            (key == "raw" ? m.raw : m.part)[off] = digest;
        }
    }
    return m;
}

void TestSha1() {
    const std::string abc = "abc";
    EXPECT_EQ(hex(sha1(reinterpret_cast<const std::uint8_t*>(abc.data()), abc.size())),
              std::string("a9993e364706816aba3e25717850c26c9cd0d89d"));
    EXPECT_EQ(hex(sha1(nullptr, 0)), std::string("da39a3ee5e6b4b0d3255bfef95601890afd80709"));
    const std::string two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    EXPECT_EQ(hex(sha1(reinterpret_cast<const std::uint8_t*>(two.data()), two.size())),
              std::string("84983e441c3bd26ebaae4aa1f95129e5e54670f1"));
    Sha1 split;  // the same, fed in odd pieces across block edges
    for (std::size_t i = 0; i < two.size(); i += 7) {
        split.update(reinterpret_cast<const std::uint8_t*>(two.data()) + i, std::min<std::size_t>(7, two.size() - i));
    }
    EXPECT_EQ(hex(split.finish()), std::string("84983e441c3bd26ebaae4aa1f95129e5e54670f1"));
}

void put32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    for (int s = 24; s >= 0; s -= 8) v.push_back(static_cast<std::uint8_t>(x >> s));
}

// Any window of a packed stream decodes to the same bytes as the whole.
void TestUnpackWindows() {
    std::vector<std::uint8_t> packed;
    put32(packed, 5);
    for (int i = 0; i < 5; ++i) packed.push_back(static_cast<std::uint8_t>(0xA0 + i));
    put32(packed, 0x80000000u | 5000);
    for (int i = 0; i < 68; ++i) packed.push_back(static_cast<std::uint8_t>(i * 37 + 1));
    put32(packed, 3);
    packed.insert(packed.end(), {1, 2, 3});
    const std::uint32_t total = 5008;
    rtrvz_junk junk;
    for (std::uint64_t base : {std::uint64_t(0), std::uint64_t(0x7FF0), std::uint64_t(0x123456)}) {
        std::vector<std::uint8_t> whole(total);
        EXPECT_EQ(rtrvz_unpack(packed.data(), static_cast<std::uint32_t>(packed.size()), base, 0, whole.data(), total, &junk),
                  RTRVZ_OK);
        EXPECT_EQ(int(whole[4]), 0xA4);
        EXPECT_EQ(int(whole[5007]), 3);
        for (std::uint32_t skip : {0u, 1u, 4u, 5u, 6u, 2083u, 2084u, 2090u, 5004u}) {
            for (std::uint32_t len : {1u, 3u, 100u, 3000u}) {
                if (skip + len > total) continue;
                std::vector<std::uint8_t> part(len);
                EXPECT_EQ(rtrvz_unpack(packed.data(), static_cast<std::uint32_t>(packed.size()), base, skip, part.data(),
                                       len, &junk),
                          RTRVZ_OK);
                EXPECT_TRUE(std::memcmp(part.data(), whole.data() + skip, len) == 0);
            }
        }
    }
    std::vector<std::uint8_t> out(total + 1);
    EXPECT_EQ(rtrvz_unpack(packed.data(), static_cast<std::uint32_t>(packed.size()), 0, 0, out.data(), total + 1, &junk),
              RTRVZ_ERR_SHORT);
    EXPECT_EQ(rtrvz_unpack(packed.data(), 40, 0, 0, out.data(), total, &junk), RTRVZ_ERR_TRUNCATED);
    EXPECT_EQ(rtrvz_unpack(packed.data(), 7, 0, 0, out.data(), 5, &junk), RTRVZ_ERR_TRUNCATED);

    // Exception lists: one with two entries, one empty, then padding.
    std::vector<std::uint8_t> lists = {0, 2};
    lists.resize(2 + 44, 0x11);
    lists.insert(lists.end(), {0, 0});
    std::uint32_t at = 0;
    EXPECT_EQ(rtrvz_exception_bytes(lists.data(), static_cast<std::uint32_t>(lists.size()), 2, 0, &at), RTRVZ_OK);
    EXPECT_EQ(at, 48u);
    EXPECT_EQ(rtrvz_exception_bytes(lists.data(), 30, 1, 0, &at), RTRVZ_ERR_TRUNCATED);
    // Stored uncompressed, an empty list (2 bytes) is padded to 4.
    const std::uint8_t empty[4] = {0, 0, 0, 0};
    EXPECT_EQ(rtrvz_exception_bytes(empty, 4, 1, 1, &at), RTRVZ_OK);
    EXPECT_EQ(at, 4u);
    EXPECT_EQ(rtrvz_exception_bytes(empty, 3, 1, 1, &at), RTRVZ_ERR_TRUNCATED);
    EXPECT_EQ(rtrvz_exception_bytes(empty, 3, 1, 0, &at), RTRVZ_OK);
    EXPECT_EQ(at, 2u);
}

void CheckImage(const std::string& name, const Manifest& m) {
    auto bytes = load(name);
    EXPECT_FALSE(bytes.empty());
    std::unique_ptr<RvzImage> image;
    std::string error;
    if (!RvzImage::open(source(bytes), image, error)) {
        std::cerr << name << ": " << error << std::endl;
        EXPECT_TRUE(false);
        return;
    }
    EXPECT_EQ(image->head().iso_size, m.disc_size);
    EXPECT_EQ(image->head().partitions.size(), std::size_t(1));
    EXPECT_EQ(image->partition_data_disc_offset(0), m.part_disc);
    EXPECT_EQ(image->partition_data_size(0), m.part_size);
    std::vector<std::uint8_t> block(0x8000);
    int bad = 0;
    for (const auto& [off, digest] : m.raw) {
        if (!image->read_raw(off, block.data(), block.size())) {
            std::cerr << name << ": raw " << off << ": " << image->last_error() << std::endl;
            ++bad;
        } else if (hex(sha1(block.data(), block.size())) != digest) {
            std::cerr << name << ": raw block at " << std::hex << off << std::dec << " differs" << std::endl;
            ++bad;
        }
    }
    std::vector<std::uint8_t> whole(m.part_size);
    if (!image->read_partition(0, 0, whole.data(), whole.size())) {
        std::cerr << name << ": " << image->last_error() << std::endl;
        ++bad;
    }
    for (const auto& [off, digest] : m.part) {
        const std::size_t n = std::min<std::uint64_t>(0x8000, m.part_size - off);
        if (hex(sha1(whole.data() + off, n)) != digest) {
            std::cerr << name << ": partition block at " << std::hex << off << std::dec << " differs" << std::endl;
            ++bad;
        }
    }
    EXPECT_EQ(bad, 0);
    // Small reads in any order agree with the whole, across chunk and
    // sector edges.
    const std::uint64_t offsets[] = {0, 0x7BFF, 0x7C00, 0x1EFFD, 0x1F000, 0x3E001, 0x1F0000, 0x1FFFFF,
                                     0x3A1230, 0x3E7FFF, m.part_size - 3};
    for (int round = 0; round < 2; ++round) {
        for (std::uint64_t off : offsets) {
            for (std::size_t len : {std::size_t(1), std::size_t(3), std::size_t(0x7C01), std::size_t(0x40005)}) {
                if (off + len > m.part_size) continue;
                std::vector<std::uint8_t> got(len);
                EXPECT_TRUE(image->read_partition(0, off, got.data(), len));
                EXPECT_TRUE(std::memcmp(got.data(), whole.data() + off, len) == 0);
            }
        }
    }
    // Unaligned raw reads, including the disc header's first 0x80 bytes.
    std::uint8_t id[6];
    EXPECT_TRUE(image->read_raw(0, id, 6));
    EXPECT_TRUE(std::memcmp(id, "RVZT01", 6) == 0);
    std::uint8_t span[0x100];
    EXPECT_TRUE(image->read_raw(0x7F, span, sizeof span));
    // Encrypted partition data and bytes past the disc are not there.
    EXPECT_FALSE(image->read_raw(m.part_disc + 0x10, span, 4));
    EXPECT_TRUE(image->last_error().find("encrypted") != std::string::npos);
    EXPECT_FALSE(image->read_raw(m.disc_size, span, 4));
    EXPECT_FALSE(image->read_raw(m.part_disc - 2, span, 4));
    EXPECT_FALSE(image->read_partition(0, m.part_size - 2, span, 4));
    EXPECT_FALSE(image->read_partition(1, 0, span, 4));
}

void TestImages() {
    const Manifest m = read_manifest();
    EXPECT_EQ(m.disc_size, 0x6F0000u);
    EXPECT_EQ(m.part.size(), std::size_t(0x5D0000 / 0x8000));
    for (const char* name : {"zstd-128k-l5.rvz", "zstd-32k-l19.rvz", "zstd-256k-l22.rvz", "zstd-2m-l3.rvz", "none-128k.rvz"}) {
        CheckImage(name, m);
    }
    std::unique_ptr<RvzImage> image;
    std::string error;
    EXPECT_FALSE(RvzImage::open(source(load("lzma-128k.rvz")), image, error));
    EXPECT_TRUE(error.find("LZMA") != std::string::npos);
    EXPECT_FALSE(RvzImage::open(source(load("lzma2-2m.wia")), image, error));
}

// The stub d2x boots from, and the sources the loader reads through.
void TestStub() {
    std::unique_ptr<RvzImage> image;
    std::string error;
    EXPECT_TRUE(RvzImage::open(source(load("zstd-128k-l5.rvz")), image, error));
    if (!image) return;
    EXPECT_EQ(image->partition_at(0x70000), std::size_t(0));
    EXPECT_EQ(image->partition_at(0x50000), SIZE_MAX);
    RvzStub stub;
    EXPECT_TRUE(build_rvz_stub(*image, stub, error));
    // The system area and the partition header are adjacent here: one range.
    EXPECT_EQ(stub.ranges.size(), std::size_t(1));
    EXPECT_EQ(stub.bytes.size(), std::size_t(0x70000));
    if (!stub.ranges.empty()) {
        EXPECT_EQ(stub.ranges[0].disc_offset, 0u);
        EXPECT_EQ(stub.ranges[0].length, 0x70000u);
    }
    const Manifest m = read_manifest();
    int bad = 0;
    for (std::uint64_t off = 0; off < stub.bytes.size(); off += 0x8000) {
        if (hex(sha1(stub.bytes.data() + off, 0x8000)) != m.raw.at(off)) ++bad;
    }
    EXPECT_EQ(bad, 0);
    EXPECT_TRUE(std::memcmp(stub.bytes.data(), "RVZT01", 6) == 0);
    const RvzPartitionSource data(*image, 0);
    EXPECT_EQ(data.size(), m.part_size);
    std::uint8_t id[6];
    EXPECT_TRUE(data.read(0, id, 6));
    EXPECT_TRUE(std::memcmp(id, "RVZT01", 6) == 0);
    const RvzRawSource raw(*image);
    EXPECT_EQ(raw.size(), m.disc_size);
}

RvzVerdict verdict_of(const std::string& name, RvzHead* head_out = nullptr) {
    auto bytes = load(name);
    RvzHead head;
    std::string error;
    EXPECT_TRUE(read_rvz_head(MemorySource(bytes), head, error));
    if (head_out != nullptr) *head_out = head;
    return check_rvz(head, bytes.size());
}

bool mentions(const RvzVerdict& v, const std::string& text) {
    for (const auto& r : v.reasons) {
        if (r.find(text) != std::string::npos) return true;
    }
    return false;
}

void TestVerdicts() {
    RvzHead base;
    RvzVerdict v = verdict_of("zstd-128k-l5.rvz", &base);
    EXPECT_TRUE(v.support == RvzSupport::Supported);
    EXPECT_TRUE(v.reasons.empty());
    EXPECT_EQ(describe_rvz(base), std::string("RVZ 1.00, Zstandard level 5, 128 KiB chunks"));
    EXPECT_EQ(base.version_compatible, 0x00030000u);
    EXPECT_TRUE(verdict_of("zstd-32k-l19.rvz").support == RvzSupport::Supported);
    EXPECT_TRUE(verdict_of("none-128k.rvz").support == RvzSupport::Supported);

    v = verdict_of("zstd-256k-l22.rvz");
    EXPECT_TRUE(v.support == RvzSupport::AtOwnRisk);
    EXPECT_EQ(v.reasons.size(), std::size_t(1));
    EXPECT_TRUE(mentions(v, "256 KiB chunks are larger"));

    v = verdict_of("zstd-2m-l3.rvz");
    EXPECT_TRUE(v.support == RvzSupport::Unsupported);
    EXPECT_TRUE(mentions(v, "2 MiB chunks are too large"));

    v = verdict_of("lzma-128k.rvz");
    EXPECT_TRUE(v.support == RvzSupport::Unsupported);
    EXPECT_TRUE(mentions(v, "compressed with LZMA,"));

    // The WIA fixture keeps its headers only, so it is also incomplete.
    v = verdict_of("lzma2-2m.wia");
    EXPECT_TRUE(v.support == RvzSupport::Unsupported);
    EXPECT_TRUE(mentions(v, "WIA file"));
    EXPECT_TRUE(mentions(v, "incomplete"));

    const std::uint64_t size = base.file_size;
    RvzHead h = base;
    h.version_compatible = 0x02000000;
    v = check_rvz(h, size);
    EXPECT_TRUE(v.support == RvzSupport::Unsupported);
    EXPECT_TRUE(mentions(v, "RVZ 2.00, newer"));
    h = base;
    h.version = 0x01010000;
    v = check_rvz(h, size);
    EXPECT_TRUE(v.support == RvzSupport::AtOwnRisk);
    EXPECT_TRUE(mentions(v, "newer Dolphin (RVZ 1.01)"));
    h = base;
    h.version = 0x00020000;
    EXPECT_TRUE(check_rvz(h, size).support == RvzSupport::Unsupported);
    h = base;
    h.disc_type = 1;
    EXPECT_TRUE(mentions(check_rvz(h, size), "GameCube"));
    h = base;
    h.partitions.clear();
    EXPECT_TRUE(check_rvz(h, size).support == RvzSupport::Unsupported);
    h = base;
    h.method = 2;
    EXPECT_TRUE(mentions(check_rvz(h, size), "bzip2"));
    h = base;
    h.method = 1;
    EXPECT_TRUE(mentions(check_rvz(h, size), "method 1,"));
    h = base;
    h.chunk_size = 0x30000;
    EXPECT_TRUE(mentions(check_rvz(h, size), "not one RVZ allows"));
    h = base;
    h.chunk_size = 0x4000;
    EXPECT_TRUE(check_rvz(h, size).support == RvzSupport::Unsupported);
    h = base;
    h.level = -5;
    EXPECT_TRUE(check_rvz(h, size).support == RvzSupport::Supported);
    h.level = 22;
    EXPECT_TRUE(check_rvz(h, size).support == RvzSupport::Supported);
    h.level = 23;
    v = check_rvz(h, size);
    EXPECT_TRUE(v.support == RvzSupport::AtOwnRisk);
    EXPECT_TRUE(mentions(v, "level 23 is above the highest"));
    // A refusal lists only why it is refused.
    h.disc_type = 1;
    v = check_rvz(h, size);
    EXPECT_EQ(v.reasons.size(), std::size_t(1));
    EXPECT_TRUE(mentions(v, "GameCube"));
    EXPECT_TRUE(mentions(check_rvz(base, size - 1), "incomplete"));
    EXPECT_TRUE(mentions(check_rvz(base, size + 1), "larger than its header"));
}

void TestDamage() {
    const auto good = load("zstd-128k-l5.rvz");
    RvzHead head;
    std::string error;
    for (std::size_t at : {std::size_t(0x20), std::size_t(0x60), std::size_t(0x124 + 3)}) {
        auto bytes = good;
        bytes[at] ^= 1;
        EXPECT_FALSE(read_rvz_head(MemorySource(bytes), head, error));
        EXPECT_TRUE(error.find("damaged") != std::string::npos);
    }
    auto bytes = good;
    bytes[1] = 'X';
    EXPECT_FALSE(read_rvz_head(MemorySource(bytes), head, error));
    EXPECT_EQ(error, std::string("not an RVZ file"));
    EXPECT_FALSE(read_rvz_head(MemorySource(std::vector<std::uint8_t>(good.begin(), good.begin() + 0x100)), head, error));

    // Cut short: the headers still read, groups past the end fail cleanly.
    std::unique_ptr<RvzImage> image;
    if (!RvzImage::open(source(std::vector<std::uint8_t>(good.begin(), good.begin() + good.size() / 2)), image, error)) {
        EXPECT_TRUE(false);
        return;
    }
    std::vector<std::uint8_t> out(0x10000);
    bool any_failed = false;
    for (std::uint64_t off = 0; off + out.size() <= image->partition_data_size(0); off += out.size()) {
        if (!image->read_partition(0, off, out.data(), out.size())) {
            any_failed = true;
            EXPECT_TRUE(image->last_error().find("past the end") != std::string::npos);
            break;
        }
    }
    EXPECT_TRUE(any_failed);
}

// ---- The in-game runtime serving an RVZ game (rt_hook.c, RT_RVZ) --------

// The runtime addresses memory with 32-bit fields, so on a 64-bit host its
// context, state, buffers and the game's buffer must sit below 4 GiB.
std::uint8_t* LowBuffer(std::size_t bytes) {
#if defined(_WIN32) || defined(__CYGWIN__)
    for (std::uintptr_t hint = 0x10000000; hint < 0x70000000; hint += 0x01000000) {
        void* p = VirtualAlloc(reinterpret_cast<void*>(hint), bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (p) return static_cast<std::uint8_t*>(p);
    }
    return nullptr;
#elif defined(__linux__)
    void* p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    return p == MAP_FAILED ? nullptr : static_cast<std::uint8_t*>(p);
#else
    (void)bytes;
    return nullptr;
#endif
}

std::uint32_t Low(const void* p) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)); }

constexpr std::uint32_t kCompleteEntry = 0x935D0100;
constexpr std::uint32_t kGameCallback = 0x80005000;

// The card: 512-byte sectors, the RVZ and its groups file scattered on it.
// With the RVZ on USB, the RVZ is on the drive instead (d2x's /dev/usb2,
// fd 11) and only the groups file is on the card.
std::vector<std::uint8_t> g_card;
std::vector<std::uint8_t> g_usb;
constexpr std::uint32_t kUsbFd = 11;
bool g_d2x = false;
unsigned g_usb_reads = 0;
unsigned g_issued = 0;
unsigned g_status_trips = 0;
unsigned g_largest_request = 0;
rt_pending* g_record = nullptr;

void DeviceCopy(const std::vector<std::uint8_t>& device, std::uint32_t sector, std::uint32_t count, std::uint32_t dst) {
    EXPECT_EQ(dst % 32, 0u);
    EXPECT_TRUE(count >= 1 && count <= RT_RVZ_REQUEST_SECTORS);
    g_largest_request = std::max(g_largest_request, count);
    if ((std::uint64_t(sector) + count) * 512 > device.size()) {
        EXPECT_TRUE(false);
        return;
    }
    std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(dst)), device.data() + std::size_t(sector) * 512,
                std::size_t(count) * 512);
}
void CardCopy(std::uint32_t sector, std::uint32_t count, std::uint32_t dst) { DeviceCopy(g_card, sector, count, dst); }

std::int32_t RvzIoctlv(std::uint32_t fd, std::uint32_t ioctl, std::uint32_t in_count, std::uint32_t out_count,
                       rt_ioctlv* vec, std::uint32_t callback, rt_pending* record) {
    EXPECT_EQ(callback, kCompleteEntry);
    g_record = record;
    ++g_issued;
    if (fd == kUsbFd) {  // d2x's /dev/usb2: the card's d2x request, another number
        EXPECT_EQ(ioctl, RT_UMS_READ_SECTORS);
        EXPECT_EQ(in_count, 2u);
        EXPECT_EQ(out_count, 1u);
        const auto* sector = reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(vec[0].data));
        const auto* count = reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(vec[1].data));
        EXPECT_EQ(vec[2].len, *count * 512u);
        DeviceCopy(g_usb, *sector, *count, vec[2].data);
        ++g_usb_reads;
        return 0;
    }
    EXPECT_EQ(fd, 9u);
    if (ioctl == RT_SDHC_ISINSERTED) {  // d2x's null round trip
        EXPECT_TRUE(g_d2x);
        EXPECT_EQ(in_count + out_count, 0u);
        return 0;
    }
    EXPECT_EQ(in_count, 2u);
    EXPECT_EQ(out_count, 1u);
    if (ioctl == RT_SDHC_READ) {
        EXPECT_TRUE(g_d2x);
        const auto* sector = reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(vec[0].data));
        const auto* count = reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(vec[1].data));
        EXPECT_EQ(vec[2].len, *count * 512u);
        CardCopy(*sector, *count, vec[2].data);
        return 0;
    }
    EXPECT_FALSE(g_d2x);
    EXPECT_EQ(ioctl, 7u);
    const auto* rq = reinterpret_cast<const rt_sdio_request*>(static_cast<std::uintptr_t>(vec[0].data));
    EXPECT_EQ(rq->cmd, 0x12u);
    EXPECT_EQ(vec[1].data, rq->dma_addr);
    CardCopy(rq->arg, rq->blk_cnt, rq->dma_addr);  // SDHC: a sector number
    return 0;
}

std::int32_t RvzIoctl(std::uint32_t fd, std::uint32_t ioctl, std::uint32_t*, std::uint32_t, std::uint32_t,
                      std::uint32_t out_len, std::uint32_t callback, rt_pending* record) {
    EXPECT_FALSE(g_d2x);  // /dev/sdio/slot0's null round trip
    EXPECT_EQ(fd, 9u);
    EXPECT_EQ(ioctl, RT_SDIO_GETSTATUS);
    EXPECT_EQ(out_len, 4u);
    EXPECT_EQ(callback, kCompleteEntry);
    g_record = record;
    ++g_issued;
    ++g_status_trips;
    return 0;
}

// Places `bytes` on the card in pieces of `piece` sectors, each piece
// `gap` sectors after the last, from `sector` on.
std::vector<rt_rvz_extent> Place(std::vector<std::uint8_t>& device, const std::vector<std::uint8_t>& bytes,
                                 std::uint32_t sector, std::uint32_t piece, std::uint32_t gap) {
    std::vector<rt_rvz_extent> out;
    const std::uint32_t sectors = static_cast<std::uint32_t>((bytes.size() + 511) / 512);
    for (std::uint32_t at = 0; at < sectors; at += piece) {
        const std::uint32_t n = std::min(piece, sectors - at);
        if ((std::size_t(sector) + n) * 512 > device.size()) device.resize((std::size_t(sector) + n) * 512);
        std::memcpy(device.data() + std::size_t(sector) * 512, bytes.data() + std::size_t(at) * 512,
                    std::min<std::size_t>(std::size_t(n) * 512, bytes.size() - std::size_t(at) * 512));
        out.push_back(rt_rvz_extent{at, sector, n});
        sector += n + gap;
    }
    return out;
}

// One DVDLowRead through the hook, its requests answered in order until
// the game's callback. Returns the result the game sees.
std::int32_t RuntimeRead(rt_context& ctx, std::uint64_t offset, std::uint32_t length, std::uint8_t* out) {
    std::uint32_t di_cmd[8] = {0x71000000, length, static_cast<std::uint32_t>(offset >> 2), 0, 0, 0, 0, 0};
    std::uintptr_t args[8] = {3, 0x71, reinterpret_cast<std::uintptr_t>(di_cmd), 0x20,
                              reinterpret_cast<std::uintptr_t>(out), length, kGameCallback, 0x80006000};
    std::uint32_t result = 0xFFFF;
    g_issued = 0;
    g_record = nullptr;
    EXPECT_EQ(rt_on_ioctl_async(&ctx, args, &result), 1);  // answered: nothing reaches the drive
    EXPECT_EQ(result, 0u);
    EXPECT_EQ(g_issued, 1u);
    rt_pending* rec = g_record;
    if (rec == nullptr) return -100;
    for (int step = 0; step < 100000; ++step) {
        const unsigned before = g_issued;
        std::int32_t res = 0;
        std::uintptr_t cb = 0xFFFF;
        std::uintptr_t ud = 0;
        rt_on_di_complete(&ctx, &res, rec, &cb, &ud);
        if (cb != 0) {
            EXPECT_EQ(cb, kGameCallback);
            EXPECT_EQ(ud, 0x80006000u);
            EXPECT_EQ(rec->in_use, 0u);
            return res;
        }
        EXPECT_EQ(g_issued, before + 1);  // each step issues exactly the next request
        if (g_issued != before + 1) return -101;
    }
    return -102;
}

// `mod`: a mod's table on top of the RVZ (memory, a file on the card, a
// file on the USB drive, a relocated disc range), as modplan builds one.
void CheckRuntime(const char* name, bool d2x, std::uint32_t piece, bool usb = false, bool mod = false) {
    std::unique_ptr<RvzImage> image;
    std::string error;
    const std::vector<std::uint8_t> file = load(name);
    EXPECT_TRUE(RvzImage::open(source(file), image, error));
    if (!image) return;
    RvzRuntimeTable table;
    EXPECT_TRUE(image->runtime_table(0, table, error));
    EXPECT_EQ(std::uint64_t(table.data_kib) * 1024, image->partition_data_size(0));
    EXPECT_EQ(table.entries.size(), std::size_t(table.group_count) * 8);

    g_card.assign(512 * 64, 0);
    g_usb.assign(512 * 2048, 0);
    g_d2x = d2x;
    g_largest_request = 0;
    g_usb_reads = 0;
    // The file in pieces: groups straddle them, and a request never
    // crosses one (nor passes 64 sectors).
    const std::vector<rt_rvz_extent> extents = Place(usb ? g_usb : g_card, file, usb ? 4096 : 64, piece, 5);
    const std::vector<rt_rvz_extent> table_extents =
        Place(g_card, table.entries, static_cast<std::uint32_t>(g_card.size() / 512) + 3, 1, 2);
    EXPECT_TRUE(extents.size() <= RT_RVZ_MAX_EXTENTS);
    EXPECT_TRUE(table_extents.size() <= RT_RVZ_TABLE_EXTENTS);

    // As resident.cpp lays it out: context, state, two buffers, the
    // decoder's workspace (the host library's is larger), the game's buffer.
    const RvzBufferSizes buffers = rvz_buffer_sizes(table);
    EXPECT_EQ(buffers.decoder, std::string(name).compare(0, 4, "zstd") == 0);
    EXPECT_EQ(buffers.stored != 0, buffers.decoder);
    EXPECT_TRUE(buffers.group >= table.max_raw && buffers.stored >= table.max_compressed);
    EXPECT_EQ(table.max_stored, std::max(table.max_raw, table.max_compressed));
    const std::uint32_t dctx_bytes = buffers.decoder ? 0x40000 : 0;
    const std::size_t low_bytes = 0x10000 + sizeof(rt_rvz_state) + std::size_t(buffers.stored) + buffers.group +
                                  dctx_bytes + 0x70000;
    std::uint8_t* low = LowBuffer(low_bytes);
    EXPECT_TRUE(low != nullptr);
    if (low == nullptr) return;
    std::memset(low, 0, low_bytes);
    rt_context& ctx = *reinterpret_cast<rt_context*>(low);
    ctx.magic = RT_CONTEXT_MAGIC;
    ctx.complete_entry = kCompleteEntry;
    ctx.sdio_fd = 9;
    ctx.sdio_sdhc = d2x ? RT_SD_D2X : 1;
    ctx.ioctlv_async = 0x8019445C;
    ctx.di_read_entry = 0x80194000;
    rt_rvz_state& st = *reinterpret_cast<rt_rvz_state*>(low + 0x10000);
    std::uint8_t* stored = low + 0x10000 + ((sizeof(rt_rvz_state) + 31) & ~std::size_t(31));
    std::uint8_t* group = stored + buffers.stored;
    std::uint8_t* dctx = group + buffers.group;
    std::uint8_t* out = dctx + dctx_bytes;
    st.magic = RT_RVZ_MAGIC;
    st.data_kib = table.data_kib;
    st.group_kib = table.group_kib;
    st.split_kib = table.split_kib;
    st.first_groups = table.first_groups;
    st.group_count = table.group_count;
    st.lists = table.lists;
    st.stored_bytes = buffers.stored;
    st.group_bytes = buffers.group;
    st.stored_buffer = Low(stored);
    st.group_buffer = Low(group);
    st.dctx_buffer = Low(dctx);
    st.dctx_bytes = dctx_bytes;
    st.extent_count = static_cast<std::uint32_t>(extents.size());
    st.table_extent_count = static_cast<std::uint32_t>(table_extents.size());
    st.on_usb = usb ? 1 : 0;
    ctx.usb_fd = usb ? kUsbFd : 0xFFFFFFFFu;
    st.cached_group = RT_RVZ_NO_GROUP;
    st.entry_sector = RT_RVZ_NO_GROUP;
    std::copy(extents.begin(), extents.end(), st.extents);
    std::copy(table_extents.begin(), table_extents.end(), st.table_extents);
    ctx.rvz_state = Low(&st);
    rt_host_ioctlv_async = &RvzIoctlv;
    rt_host_ioctl_async = &RvzIoctl;

    const std::uint64_t size = image->partition_data_size(0);
    // The mod: what each range must read as instead of the RVZ's bytes.
    struct Overlay {
        std::uint64_t at;
        std::vector<std::uint8_t> bytes;
    };
    std::vector<Overlay> overlays;
    if (mod) {
        PayloadPieces pieces;
        MemReplacement m;
        m.virtual_offset = 0x10000;
        for (int i = 0; i < 0x300; ++i) m.bytes.push_back(static_cast<std::uint8_t>(i * 7 + 1));
        pieces.mem.push_back(m);
        // A file on the card: 2000 bytes from byte 100 of sector 8 (below
        // the RVZ's pieces, which start at sector 64).
        for (std::size_t k = 8 * 512; k < 16 * 512; ++k) g_card[k] = static_cast<std::uint8_t>(k * 13 + 5);
        SdReplacement sd;
        sd.virtual_offset = 0x20000;
        EXPECT_TRUE(place_on_fragments({{8, 8}}, 100, 2000, sd.runs, error));
        pieces.sd.push_back(sd);
        // A pack's file on the USB drive: 1500 bytes of sector 10 on.
        for (std::size_t k = 10 * 512; k < 14 * 512; ++k) g_usb[k] = static_cast<std::uint8_t>(k * 29 + 3);
        SdReplacement on_usb;
        on_usb.virtual_offset = 0x30000;
        EXPECT_TRUE(place_on_fragments({{10, 4}}, 0, 1500, on_usb.runs, error, RT_KIND_USB));
        pieces.sd.push_back(on_usb);
        // A relocated file: the game's bytes from elsewhere in the partition.
        DiscReplacement d;
        d.virtual_offset = 0x40000;
        d.disc_offset = 0x123450;
        d.length = 0x1000;
        pieces.disc.push_back(d);
        EXPECT_TRUE(pieces.needs_usb());
        const std::uint32_t table_address = Low(out) + 0x48000;
        std::vector<std::uint8_t> payload;
        EXPECT_TRUE(build_payload(pieces, table_address, 0, 9, payload, error));
        EXPECT_TRUE(payload.size() < 0x18000);
        std::memcpy(out + 0x48000, payload.data(), payload.size());
        ctx.table = table_address;
        ctx.usb_fd = kUsbFd;
        // The records' bounce buffers, which the table's card and drive runs use.
        for (std::uint32_t i = 0; i < RT_MAX_PENDING; ++i) ctx.pending[i].bounce = Low(out + 0x60000) + i * RT_BOUNCE_BYTES;  // one /dev/usb2 handle for packs and an RVZ alike
        overlays.push_back({0x10000, m.bytes});
        overlays.push_back({0x20000, std::vector<std::uint8_t>(g_card.begin() + 8 * 512 + 100,
                                                               g_card.begin() + 8 * 512 + 2100)});
        overlays.push_back({0x30000, std::vector<std::uint8_t>(g_usb.begin() + 10 * 512, g_usb.begin() + 10 * 512 + 1500)});
        std::vector<std::uint8_t> moved(0x1000);
        EXPECT_TRUE(image->read_partition(0, 0x123450, moved.data(), moved.size()));
        overlays.push_back({0x40000, moved});
    }
    std::vector<std::uint8_t> want(0x40000);
    std::uint64_t seed = 0x9E3779B97F4A7C15ull;
    auto next = [&seed]() {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        return seed;
    };
    auto check = [&](std::uint64_t offset, std::uint32_t length) {
        std::memset(out, 0xEE, length + 64);
        const std::int32_t res = RuntimeRead(ctx, offset, length, out);
        EXPECT_EQ(res, RT_DI_SUCCESS);
        EXPECT_TRUE(image->read_partition(0, offset, want.data(), length));
        for (const Overlay& o : overlays) {
            const std::uint64_t from = std::max(offset, o.at);
            const std::uint64_t to = std::min(offset + length, o.at + o.bytes.size());
            if (from < to) std::memcpy(want.data() + (from - offset), o.bytes.data() + (from - o.at), to - from);
        }
        const bool same = std::memcmp(out, want.data(), length) == 0;
        if (!same) std::cerr << name << ": read at 0x" << std::hex << offset << " + 0x" << length << std::dec
                             << " differs (last error " << st.last_error << ")" << std::endl;
        EXPECT_TRUE(same);
        EXPECT_EQ(out[length], 0xEE);
    };
    // The start (the boot and FST segment), the split, the end, then
    // random reads of every size a game makes, small ones in a row.
    check(0, 0x440);
    if (mod) {
        // Every edge of every replacement, and one read over all of them.
        check(0x10000 - 0x20, 0x340);
        check(0x1FF00, 0x900);
        check(0x2FFE0, 0x800);
        check(0x3F000, 0x3000);
        check(0x8000, 0x38000);
    }
    check(std::uint64_t(table.split_kib) * 1024 - 0x100, 0x200);
    check(size - 0x8000, 0x8000);
    for (int i = 0; i < 60; ++i) {
        const std::uint32_t length = static_cast<std::uint32_t>(next() % 0x3FFE0 + 0x20) & ~31u;
        const std::uint64_t offset = (next() % (size - length)) & ~std::uint64_t(3);
        check(offset, length);
        if (i % 10 == 0) check(offset + length, 0x20);  // the next read, from the cached group
    }
    EXPECT_EQ(st.failures, 0u);
    EXPECT_TRUE(st.groups_loaded > 0);
    EXPECT_EQ(g_usb_reads != 0, usb || mod);  // the groups come from the drive holding the RVZ
    EXPECT_TRUE(g_largest_request >= 1 && g_largest_request <= std::min(piece, RT_RVZ_REQUEST_SECTORS));

    // Past the partition's data: the drive's refusal, as a layer check expects.
    EXPECT_EQ(RuntimeRead(ctx, size - 0x20, 0x40, out), RT_DI_ERROR);
    EXPECT_EQ(st.past_end, 1u);
    EXPECT_EQ(RuntimeRead(ctx, size + 0x8000, 0x20, out), RT_DI_ERROR);

    // A second read while one is being served: refused, the first unharmed.
    {
        std::uint32_t cmd1[8] = {0x71000000, 0x800, 0, 0, 0, 0, 0, 0};
        std::uintptr_t args1[8] = {3, 0x71, reinterpret_cast<std::uintptr_t>(cmd1), 0x20,
                                   reinterpret_cast<std::uintptr_t>(out), 0x800, kGameCallback, 0x80006000};
        std::uint32_t r1 = 0xFFFF;
        EXPECT_EQ(rt_on_ioctl_async(&ctx, args1, &r1), 1);
        rt_pending* first = g_record;
        std::uint32_t cmd2[8] = {0x71000000, 0x20, 0x100, 0, 0, 0, 0, 0};
        std::uintptr_t args2[8] = {3, 0x71, reinterpret_cast<std::uintptr_t>(cmd2), 0x20,
                                   reinterpret_cast<std::uintptr_t>(out + 0x1000), 0x20, kGameCallback, 0x80006000};
        std::uint32_t r2 = 0;
        EXPECT_EQ(rt_on_ioctl_async(&ctx, args2, &r2), 1);
        EXPECT_EQ(r2, 0xFFFFFFFFu);
        EXPECT_EQ(st.busy_refusals, 1u);
        std::uintptr_t cb = 0;
        std::uintptr_t ud = 0;
        for (int step = 0; step < 1000 && cb == 0; ++step) {
            std::int32_t res = 0;
            rt_on_di_complete(&ctx, &res, first, &cb, &ud);
            if (cb != 0) EXPECT_EQ(res, RT_DI_SUCCESS);
        }
        EXPECT_EQ(cb, kGameCallback);
        EXPECT_TRUE(image->read_partition(0, 0, want.data(), 0x800));
        EXPECT_EQ(std::memcmp(out, want.data(), 0x800), 0);
        EXPECT_EQ(st.busy, 0u);
    }

    // An SD failure: the read fails, and the next one still works.
    {
        st.cached_group = RT_RVZ_NO_GROUP;
        std::uint32_t cmd[8] = {0x71000000, 0x20, static_cast<std::uint32_t>((size / 2) >> 2), 0, 0, 0, 0, 0};
        std::uintptr_t args[8] = {3, 0x71, reinterpret_cast<std::uintptr_t>(cmd), 0x20,
                                  reinterpret_cast<std::uintptr_t>(out), 0x20, kGameCallback, 0x80006000};
        std::uint32_t r = 0;
        EXPECT_EQ(rt_on_ioctl_async(&ctx, args, &r), 1);
        rt_pending* rec = g_record;
        std::int32_t res = 0;
        std::uintptr_t cb = 0;
        std::uintptr_t ud = 0;
        rt_on_di_complete(&ctx, &res, rec, &cb, &ud);  // the null round trip: the first request goes out
        EXPECT_EQ(cb, 0u);
        res = -4;                                     // which the card refuses
        rt_on_di_complete(&ctx, &res, rec, &cb, &ud);
        EXPECT_EQ(cb, kGameCallback);
        EXPECT_EQ(res, RT_DI_ERROR);
        check(size / 2, 0x20);
    }

    // On /dev/sdio/slot0 a read of the card waits while a savegame
    // command wants the card (null round trips), then goes out.
    if (!d2x && !usb && !mod) {
        std::uint8_t* fs_bytes = LowBuffer(sizeof(rt_fs_state));
        EXPECT_TRUE(fs_bytes != nullptr);
        if (fs_bytes != nullptr) {
            std::memset(fs_bytes, 0, sizeof(rt_fs_state));
            rt_fs_state& fs = *reinterpret_cast<rt_fs_state*>(fs_bytes);
            fs.card_rca = 7;
            fs.card_wanted = 1;
            ctx.fs_state = Low(&fs);
            ctx.flags |= RT_FLAG_FS;
            st.cached_group = RT_RVZ_NO_GROUP;
            st.entry_sector = RT_RVZ_NO_GROUP;
            const std::uint64_t at = (size / 3) & ~std::uint64_t(3);
            std::uint32_t cmd[8] = {0x71000000, 0x40, static_cast<std::uint32_t>(at >> 2), 0, 0, 0, 0, 0};
            std::uintptr_t args[8] = {3, 0x71, reinterpret_cast<std::uintptr_t>(cmd), 0x20,
                                      reinterpret_cast<std::uintptr_t>(out), 0x40, kGameCallback, 0x80006000};
            std::uint32_t r = 0;
            std::memset(out, 0xEE, 0x40);
            EXPECT_EQ(rt_on_ioctl_async(&ctx, args, &r), 1);
            rt_pending* rec = g_record;
            std::int32_t res = 0;
            std::uintptr_t cb = 0;
            std::uintptr_t ud = 0;
            const unsigned trips0 = g_status_trips;
            for (int i = 0; i < 3; ++i) {
                res = 0;
                rt_on_di_complete(&ctx, &res, rec, &cb, &ud);  // the start, then waits: another wait each time
                EXPECT_EQ(cb, 0u);
                EXPECT_EQ(rec->phase, RT_PHASE_RVZ_WAIT);
            }
            EXPECT_EQ(g_status_trips, trips0 + 3);
            fs.card_wanted = 0;  // the savegame command is over
            for (int step = 0; step < 1000 && cb == 0; ++step) {
                res = 0;
                rt_on_di_complete(&ctx, &res, rec, &cb, &ud);
                EXPECT_TRUE(cb != 0 || rec->phase != RT_PHASE_RVZ_WAIT);
            }
            EXPECT_EQ(cb, kGameCallback);
            EXPECT_EQ(res, RT_DI_SUCCESS);
            EXPECT_TRUE(image->read_partition(0, at, want.data(), 0x40));
            EXPECT_EQ(std::memcmp(out, want.data(), 0x40), 0);
            ctx.flags &= ~RT_FLAG_FS;
            ctx.fs_state = 0;
        }
    }

    rt_host_ioctlv_async = nullptr;
    rt_host_ioctl_async = nullptr;
#if defined(_WIN32) || defined(__CYGWIN__)
    VirtualFree(low, 0, MEM_RELEASE);
#elif defined(__linux__)
    munmap(low, low_bytes);
#endif
}

void TestRuntime() {
    for (const char* name : {"zstd-128k-l5.rvz", "zstd-32k-l19.rvz", "zstd-256k-l22.rvz", "zstd-2m-l3.rvz", "none-128k.rvz"}) {
        CheckRuntime(name, true, 37);
        CheckRuntime(name, false, 1000);
        CheckRuntime(name, true, 53, true);  // on USB, the card through d2x (the stub's device)
    }
    // A mod on top, the RVZ on the card (both SD modes) and on the drive.
    CheckRuntime("zstd-128k-l5.rvz", true, 37, false, true);
    CheckRuntime("zstd-128k-l5.rvz", false, 1000, false, true);
    CheckRuntime("zstd-32k-l19.rvz", true, 53, true, true);
    CheckRuntime("none-128k.rvz", false, 37, false, true);
    // The KiB geometry of a dual-layer game's partition passes 4 GiB of
    // data, which the table and the runtime's arithmetic must carry.
    EXPECT_EQ(0x1D0000000ull / 1024 / (0x7C00 / 1024), 0x1D0000000ull / 0x7C00);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: rvz_tests <tests/fixtures>" << std::endl;
        return 2;
    }
    g_dir = argv[1];
    TestSha1();
    TestUnpackWindows();
    TestImages();
    TestStub();
    TestVerdicts();
    TestDamage();
    TestRuntime();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "rvz: all tests passed" << std::endl;
    return 0;
}
