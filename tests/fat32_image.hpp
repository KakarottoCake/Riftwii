// SPDX-License-Identifier: GPL-3.0-or-later
// A FAT32 image built in memory for the tests: boot sector, FATs, and
// helpers to author directory entries (short, long-named) and chains.
// Shared by the host resolver's tests (fat32_tests) and the runtime
// engine's (rtfat_tests).
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace fatimg {

using Bytes = std::vector<std::uint8_t>;
using BlockReader = std::function<bool(std::uint64_t lba, std::uint32_t count, std::uint8_t* out)>;

inline void put16(std::uint8_t* p, std::uint32_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }
inline void put32(std::uint8_t* p, std::uint32_t v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }

inline std::uint8_t checksum11(const char* n) {
    std::uint8_t sum = 0;
    for (int i = 0; i < 11; ++i) sum = static_cast<std::uint8_t>(((sum & 1) ? 0x80 : 0) + (sum >> 1) + static_cast<std::uint8_t>(n[i]));
    return sum;
}

inline Bytes short_entry(const char* name11, std::uint8_t attr, std::uint32_t cluster, std::uint32_t size, std::uint8_t nt_flags = 0) {
    Bytes e(32, 0);
    std::memcpy(e.data(), name11, 11);
    e[11] = attr;
    e[12] = nt_flags;
    put16(e.data() + 0x14, cluster >> 16);
    put16(e.data() + 0x1A, cluster & 0xFFFF);
    put32(e.data() + 0x1C, size);
    return e;
}

// Long name entries (last part first) followed by the short entry.
inline Bytes lfn_entries(const std::vector<std::uint16_t>& name, const char* short11, std::uint8_t attr, std::uint32_t cluster,
                  std::uint32_t size, bool wrong_checksum = false) {
    const std::uint8_t sum = static_cast<std::uint8_t>(checksum11(short11) ^ (wrong_checksum ? 0xFF : 0));
    const std::size_t parts = (name.size() + 12) / 13;
    Bytes out;
    for (std::size_t p = parts; p >= 1; --p) {
        Bytes e(32, 0);
        e[0] = static_cast<std::uint8_t>(p | (p == parts ? 0x40 : 0));
        e[11] = 0x0F;
        e[13] = sum;
        std::uint16_t chars[13];
        for (int i = 0; i < 13; ++i) {
            const std::size_t idx = (p - 1) * 13 + i;
            chars[i] = idx < name.size() ? name[idx] : (idx == name.size() ? 0x0000 : 0xFFFF);
        }
        for (int i = 0; i < 5; ++i) put16(e.data() + 1 + i * 2, chars[i]);
        for (int i = 0; i < 6; ++i) put16(e.data() + 14 + i * 2, chars[5 + i]);
        for (int i = 0; i < 2; ++i) put16(e.data() + 28 + i * 2, chars[11 + i]);
        out.insert(out.end(), e.begin(), e.end());
    }
    Bytes s = short_entry(short11, attr, cluster, size);
    out.insert(out.end(), s.begin(), s.end());
    return out;
}

inline std::vector<std::uint16_t> ucs(const std::u16string& s) { return std::vector<std::uint16_t>(s.begin(), s.end()); }

struct Image {
    std::uint32_t bps, spc, reserved = 4, fats = 2, clusters = 256, fat_sectors, total_sectors;
    std::uint64_t volume_lba;
    Bytes bytes;

    Image(std::uint32_t bytes_per_sector, std::uint32_t sectors_per_cluster, std::uint64_t at_lba,
          std::uint32_t cluster_count = 256)
        : bps(bytes_per_sector), spc(sectors_per_cluster), clusters(cluster_count), volume_lba(at_lba) {
        fat_sectors = ((clusters + 2) * 4 + bps - 1) / bps;
        total_sectors = reserved + fats * fat_sectors + clusters * spc;
        bytes.assign(static_cast<std::size_t>(volume_lba * 512 + std::uint64_t(total_sectors) * bps), 0);
        std::uint8_t* b = boot();
        b[0] = 0xEB; b[1] = 0x58; b[2] = 0x90;
        std::memcpy(b + 3, "MSWIN4.1", 8);
        put16(b + 0x0B, bps);
        b[0x0D] = static_cast<std::uint8_t>(spc);
        put16(b + 0x0E, reserved);
        b[0x10] = static_cast<std::uint8_t>(fats);
        b[0x15] = 0xF8;
        put32(b + 0x20, total_sectors);
        put32(b + 0x24, fat_sectors);
        put32(b + 0x2C, 2);
        put16(b + 0x30, 1);
        put16(b + 0x32, 6);
        std::memcpy(b + 0x52, "FAT32   ", 8);
        b[0x1FE] = 0x55; b[0x1FF] = 0xAA;
        // FAT[0] media, FAT[1] EOC, FAT[2] root EOC.
        set_fat(0, 0x0FFFFFF8); set_fat(1, 0x0FFFFFFF); set_fat(2, 0x0FFFFFFF);
        if (volume_lba != 0) {
            // MBR: one FAT32 LBA partition. The boot sector signature also
            // lives here so the mounter looks at the partition table.
            std::uint8_t* m = bytes.data();
            m[0x1FE] = 0x55; m[0x1FF] = 0xAA;
            std::uint8_t* pe = m + 0x1BE;
            pe[4] = 0x0C;
            put32(pe + 8, static_cast<std::uint32_t>(volume_lba));
            put32(pe + 12, total_sectors * (bps / 512));
        }
    }
    std::uint8_t* boot() { return bytes.data() + volume_lba * 512; }
    std::uint64_t data_start_sector() const { return reserved + std::uint64_t(fats) * fat_sectors; }
    std::uint64_t cluster_lba(std::uint32_t c) const { return volume_lba + (data_start_sector() + std::uint64_t(c - 2) * spc) * (bps / 512); }
    std::uint8_t* cluster(std::uint32_t c) { return bytes.data() + cluster_lba(c) * 512; }
    std::uint32_t cluster_bytes() const { return bps * spc; }
    void set_fat(std::uint32_t c, std::uint32_t v) {
        for (std::uint32_t f = 0; f < fats; ++f) {
            put32(boot() + (std::uint64_t(reserved) + std::uint64_t(f) * fat_sectors) * bps + std::uint64_t(c) * 4, v);
        }
    }
    void chain(const std::vector<std::uint32_t>& cs) {
        for (std::size_t i = 0; i < cs.size(); ++i) set_fat(cs[i], i + 1 < cs.size() ? cs[i + 1] : 0x0FFFFFFF);
    }
    void write_data(const std::vector<std::uint32_t>& cs, const Bytes& data) {
        chain(cs);
        std::size_t pos = 0;
        for (std::uint32_t c : cs) {
            const std::size_t n = std::min<std::size_t>(cluster_bytes(), data.size() - pos);
            std::memcpy(cluster(c), data.data() + pos, n);
            pos += n;
            if (pos >= data.size()) break;
        }
    }
    bool write_dir(const std::vector<std::uint32_t>& cs, const Bytes& entries) {
        chain(cs);
        std::size_t pos = 0;
        for (std::uint32_t c : cs) {
            std::memset(cluster(c), 0, cluster_bytes());
            const std::size_t n = std::min<std::size_t>(cluster_bytes(), entries.size() - pos);
            std::memcpy(cluster(c), entries.data() + pos, n);
            pos += n;
        }
        return pos >= entries.size();  // false: the directory does not fit its clusters
    }
    BlockReader reader() const {
        return [this](std::uint64_t lba, std::uint32_t count, std::uint8_t* out) {
            const std::uint64_t end = (lba + count) * 512;
            if (end > bytes.size()) return false;
            std::memcpy(out, bytes.data() + lba * 512, count * 512);
            return true;
        };
    }
};


}  // namespace fatimg
