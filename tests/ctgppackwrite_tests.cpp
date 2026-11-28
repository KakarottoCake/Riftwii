// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Writing files back into an encrypted pack (riftwii/ctgppackwrite.hpp), on a
// small synthetic FAT32 volume with a test key: a file is replaced and read
// back, and a file whose cluster chain is damaged is refused before anything
// is written.
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/ctgpefa.hpp"
#include "riftwii/ctgppackwrite.hpp"
#include "riftwii/wiicrypt.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

namespace {

constexpr std::uint32_t kSector = 512;
constexpr std::uint32_t kChunk = kCtgpEfaChunkBytes;
constexpr std::uint32_t kTotalSectors = 4096;  // 2 MiB
constexpr std::uint32_t kReserved = 32;
constexpr std::uint32_t kFats = 2;
constexpr std::uint32_t kFatSectors = 8;
constexpr std::uint32_t kSectorsPerCluster = 8;  // 4 KiB clusters
constexpr std::uint32_t kDataStart = kReserved + kFats * kFatSectors;
constexpr std::uint32_t kTestCluster = kSectorsPerCluster * kSector;
constexpr std::uint32_t kEnd = 0x0FFFFFFFu;

const std::array<std::uint8_t, 16> kKey = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
const CtgpChunkIv kIv = {0x11111111u, 0x22222222u, 0x33333333u};

void put16(std::uint8_t* p, std::uint32_t v) { p[0] = std::uint8_t(v); p[1] = std::uint8_t(v >> 8); }
void put32(std::uint8_t* p, std::uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }

std::uint64_t cluster_at(std::uint32_t cluster) {
    return (std::uint64_t(kDataStart) + std::uint64_t(cluster - 2) * kSectorsPerCluster) * kSector;
}

// A plain volume: root folder in cluster 2 holding A.BIN, one cluster long
// at cluster 3. `link3` is what the FAT says follows cluster 3.
std::vector<std::uint8_t> make_plain(std::uint32_t link3) {
    std::vector<std::uint8_t> v(std::size_t(kTotalSectors) * kSector, 0);
    std::uint8_t* boot = v.data();
    boot[0] = 0xEB; boot[1] = 0x58; boot[2] = 0x90;
    put16(boot + 0x0B, kSector);
    boot[0x0D] = std::uint8_t(kSectorsPerCluster);
    put16(boot + 0x0E, kReserved);
    boot[0x10] = std::uint8_t(kFats);
    put32(boot + 0x20, kTotalSectors);
    put32(boot + 0x24, kFatSectors);
    put32(boot + 0x2C, 2);
    put16(boot + 0x30, 1);
    put16(boot + 0x1FE, 0xAA55);
    for (std::uint32_t copy = 0; copy < kFats; ++copy) {
        std::uint8_t* fat = v.data() + (std::size_t(kReserved) + copy * kFatSectors) * kSector;
        put32(fat + 0, 0x0FFFFFF8u);
        put32(fat + 4, kEnd);
        put32(fat + 8, kEnd);       // root folder
        put32(fat + 12, link3);     // A.BIN
    }
    std::uint8_t* root = v.data() + cluster_at(2);
    std::memcpy(root, "A       BIN", 11);
    root[11] = 0x20;
    put16(root + 0x1A, 3);
    put32(root + 0x1C, kTestCluster);
    std::memset(v.data() + cluster_at(3), 0xA5, kTestCluster);
    return v;
}

// The volume as the pack holds it: every 32 KiB chunk encrypted on its own.
std::vector<std::uint8_t> encrypt(const std::vector<std::uint8_t>& plain) {
    std::vector<std::uint8_t> out(plain.size());
    const Aes128 aes(kKey.data());
    for (std::size_t at = 0; at < plain.size(); at += kChunk) {
        const auto iv = ctgp_efa_chunk_iv(kIv, std::uint32_t(at / kSector));
        aes.cbc_encrypt(iv.data(), plain.data() + at, out.data() + at, kChunk);
    }
    return out;
}

std::vector<std::uint8_t> decrypt(const std::vector<std::uint8_t>& image) {
    std::vector<std::uint8_t> out(image.size());
    const Aes128 aes(kKey.data());
    for (std::size_t at = 0; at < image.size(); at += kChunk) {
        const auto iv = ctgp_efa_chunk_iv(kIv, std::uint32_t(at / kSector));
        aes.cbc_decrypt(iv.data(), image.data() + at, out.data() + at, kChunk);
    }
    return out;
}

// A.BIN's directory entry claims `size` bytes.
void set_size(std::vector<std::uint8_t>& plain, std::uint32_t size) {
    put32(plain.data() + cluster_at(2) + 0x1C, size);
}

// FAT entry `cluster` in both copies.
void set_fat(std::vector<std::uint8_t>& plain, std::uint32_t cluster, std::uint32_t value) {
    for (std::uint32_t copy = 0; copy < kFats; ++copy) {
        put32(plain.data() + (std::size_t(kReserved) + copy * kFatSectors) * kSector + cluster * 4, value);
    }
}

std::uint32_t get32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

CtgpPackIo io_on(std::vector<std::uint8_t>& image, int& writes) {
    CtgpPackIo io;
    io.read = [&image](std::uint64_t o, std::uint32_t n, std::uint8_t* out) {
        if (o + n > image.size()) return false;
        std::memcpy(out, image.data() + o, n);
        return true;
    };
    io.write = [&image, &writes](std::uint64_t o, const std::uint8_t* in, std::uint32_t n) {
        if (o + n > image.size()) return false;
        std::memcpy(image.data() + o, in, n);
        ++writes;
        return true;
    };
    return io;
}

std::vector<std::uint8_t> pattern(std::size_t n) {
    std::vector<std::uint8_t> d(n);
    for (std::size_t i = 0; i < n; ++i) d[i] = std::uint8_t((i * 13 + (i >> 9)) & 0xFF);
    return d;
}

}  // namespace

int main() {
    std::string err;

    {   // a healthy file grows from one cluster to three and reads back
        auto image = encrypt(make_plain(kEnd));
        int writes = 0;
        CtgpPackWriter w;
        EXPECT_TRUE(w.open(io_on(image, writes), image.size(), kKey, kIv, err));
        const auto data = pattern(3 * kTestCluster - 100);
        EXPECT_TRUE(w.replace_file("/A.BIN", data.data(), data.size(), err));
        std::vector<std::uint8_t> back;
        EXPECT_TRUE(w.read_file("/a.bin", back, err));
        EXPECT_TRUE(back == data);
        w.close();
        CtgpPackWriter again;
        EXPECT_TRUE(again.open(io_on(image, writes), image.size(), kKey, kIv, err));
        EXPECT_TRUE(again.read_file("/A.BIN", back, err));
        EXPECT_TRUE(back == data);
    }

    // A file whose cluster is marked free (0), reserved (1) or bad: growing
    // it could hand the same cluster out again and store two pieces of the
    // file in one place. It is refused, and nothing is written.
    for (const std::uint32_t link : {0u, 1u, 0x0FFFFFF7u}) {
        auto image = encrypt(make_plain(link));
        const auto before = image;
        int writes = 0;
        CtgpPackWriter w;
        EXPECT_TRUE(w.open(io_on(image, writes), image.size(), kKey, kIv, err));
        const auto data = pattern(2 * kTestCluster);
        EXPECT_TRUE(!w.replace_file("/A.BIN", data.data(), data.size(), err));
        EXPECT_TRUE(!err.empty());
        w.close();
        EXPECT_TRUE(writes == 0);
        EXPECT_TRUE(image == before);
    }

    // A two-cluster file whose first cluster links to itself: both reads
    // would reach the size with the same cluster twice. Refused before any
    // byte goes to the sink, and the replace writes nothing.
    // A one-cluster chain under a two-cluster size is refused the same way.
    for (const std::uint32_t link : {3u, kEnd}) {
        auto plain = make_plain(link);
        set_size(plain, 2 * kTestCluster);
        auto image = encrypt(plain);
        const auto before = image;
        int writes = 0;
        CtgpPackWriter w;
        EXPECT_TRUE(w.open(io_on(image, writes), image.size(), kKey, kIv, err));
        std::vector<std::uint8_t> back;
        EXPECT_TRUE(!w.read_file("/A.BIN", back, err));
        EXPECT_TRUE(!err.empty());
        bool sunk = false;
        EXPECT_TRUE(!w.read_file("/A.BIN", [&sunk](const std::uint8_t*, std::size_t) { sunk = true; return true; },
                                 nullptr, err));
        EXPECT_TRUE(!sunk);
        const auto data = pattern(kTestCluster);
        EXPECT_TRUE(!w.replace_file("/A.BIN", data.data(), data.size(), err));
        w.close();
        EXPECT_TRUE(writes == 0);
        EXPECT_TRUE(image == before);
    }

    // Loops further along the chain: a rho (3 -> 4 -> 5 -> 4) and a 2-cycle
    // (3 -> 4 -> 3). The replace walks the whole chain and catches either;
    // a size that reaches into the loop makes both reads refuse too.
    for (const bool rho : {true, false}) {
        auto plain = make_plain(4);
        if (rho) {
            set_fat(plain, 4, 5);
            set_fat(plain, 5, 4);
        } else {
            set_fat(plain, 4, 3);
        }
        auto image = encrypt(plain);
        const auto before = image;
        int writes = 0;
        CtgpPackWriter w;
        EXPECT_TRUE(w.open(io_on(image, writes), image.size(), kKey, kIv, err));
        const auto data = pattern(kTestCluster);
        EXPECT_TRUE(!w.replace_file("/A.BIN", data.data(), data.size(), err));
        EXPECT_TRUE(err.find("loop") != std::string::npos);
        w.close();
        EXPECT_TRUE(writes == 0);
        EXPECT_TRUE(image == before);

        set_size(plain, 5 * kTestCluster);
        image = encrypt(plain);
        CtgpPackWriter r;
        EXPECT_TRUE(r.open(io_on(image, writes), image.size(), kKey, kIv, err));
        std::vector<std::uint8_t> back;
        EXPECT_TRUE(!r.read_file("/A.BIN", back, err));
        EXPECT_TRUE(err.find("loop") != std::string::npos);
        bool sunk = false;
        EXPECT_TRUE(!r.read_file("/A.BIN", [&sunk](const std::uint8_t*, std::size_t) { sunk = true; return true; },
                                 nullptr, err));
        EXPECT_TRUE(!sunk);
    }

    // Another chain that runs into A.BIN's cluster (6 -> 3): the cluster is
    // shared, so nothing is written over or freed.
    {
        auto plain = make_plain(kEnd);
        set_fat(plain, 6, 3);
        auto image = encrypt(plain);
        const auto before = image;
        int writes = 0;
        CtgpPackWriter w;
        EXPECT_TRUE(w.open(io_on(image, writes), image.size(), kKey, kIv, err));
        const auto data = pattern(2 * kTestCluster);
        EXPECT_TRUE(!w.replace_file("/A.BIN", data.data(), data.size(), err));
        EXPECT_TRUE(err.find("shared") != std::string::npos);
        w.close();
        EXPECT_TRUE(writes == 0);
        EXPECT_TRUE(image == before);
    }

    // A growth the disk can't hold leaves no cluster marked used: with one
    // free cluster, growing A.BIN by two fails, and a later replace that
    // fits (which writes the FAT) still leaves that cluster free.
    {
        auto plain = make_plain(kEnd);
        const std::uint32_t last = 2 + (kTotalSectors - kDataStart) / kSectorsPerCluster - 1;
        for (std::uint32_t c = 5; c <= last; ++c) set_fat(plain, c, kEnd);
        auto image = encrypt(plain);
        int writes = 0;
        CtgpPackWriter w;
        EXPECT_TRUE(w.open(io_on(image, writes), image.size(), kKey, kIv, err));
        EXPECT_TRUE(w.free_clusters() == 1);
        const auto big = pattern(3 * kTestCluster);
        EXPECT_TRUE(!w.replace_file("/A.BIN", big.data(), big.size(), err));
        EXPECT_TRUE(w.free_clusters() == 1);
        const auto small = pattern(kTestCluster);
        EXPECT_TRUE(w.replace_file("/A.BIN", small.data(), small.size(), err));
        w.close();
        CtgpPackWriter again;
        EXPECT_TRUE(again.open(io_on(image, writes), image.size(), kKey, kIv, err));
        EXPECT_TRUE(again.free_clusters() == 1);
    }

    // FAT32's top four bits of an entry are the volume's: a write of the
    // file table keeps them, in the entries it changes and the ones it
    // doesn't, in both copies.
    {
        auto plain = make_plain(kEnd);
        set_fat(plain, 2, 0xF0000000u | kEnd);  // the root folder, untouched
        set_fat(plain, 3, 0xA0000000u | kEnd);  // A.BIN, which grows
        auto image = encrypt(plain);
        int writes = 0;
        CtgpPackWriter w;
        EXPECT_TRUE(w.open(io_on(image, writes), image.size(), kKey, kIv, err));
        const auto data = pattern(2 * kTestCluster);
        EXPECT_TRUE(w.replace_file("/A.BIN", data.data(), data.size(), err));
        w.close();
        const auto after = decrypt(image);
        for (std::uint32_t copy = 0; copy < kFats; ++copy) {
            const std::uint8_t* fat = after.data() + (std::size_t(kReserved) + copy * kFatSectors) * kSector;
            EXPECT_TRUE(get32(fat + 8) == (0xF0000000u | kEnd));
            EXPECT_TRUE((get32(fat + 12) & 0xF0000000u) == 0xA0000000u);
            EXPECT_TRUE((get32(fat + 12) & 0x0FFFFFFFu) >= 2 && (get32(fat + 12) & 0x0FFFFFFFu) < 0x0FFFFFF8u);
        }
        std::vector<std::uint8_t> back;
        EXPECT_TRUE(w.open(io_on(image, writes), image.size(), kKey, kIv, err));
        EXPECT_TRUE(w.read_file("/A.BIN", back, err));
        EXPECT_TRUE(back == data);
    }

    if (g_failures == 0) std::cout << "ctgppackwrite: all tests passed" << std::endl;
    return g_failures ? 1 : 0;
}
