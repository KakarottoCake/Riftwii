// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "riftwii/redirect.hpp"

namespace riftwii {

// Read-only FAT32 walker whose job is to turn "/riivolution/mod/file.bin"
// into the list of raw SD sectors that hold the file, so the resident
// runtime can fetch them straight from /dev/sdio/slot0 without a file
// system. Everything is addressed in 512-byte device blocks (what the SD
// card and the redirect table speak), whatever the file system's own
// sector size is. Every FAT index, directory sector and chain length is
// bounds-checked against the volume geometry.

// Reads `count` consecutive 512-byte blocks starting at `lba` into `out`.
using BlockReader = std::function<bool(std::uint64_t lba, std::uint32_t count, std::uint8_t* out)>;

constexpr std::uint32_t kFatBlockBytes = 512;

struct Fat32Geometry {
    std::uint64_t volume_lba = 0;      // device block where the boot sector lives
    std::uint32_t bytes_per_sector = 0;
    std::uint32_t sectors_per_cluster = 0;
    std::uint32_t reserved_sectors = 0;
    std::uint32_t fat_count = 0;
    std::uint32_t fat_sectors = 0;      // sectors per FAT
    std::uint32_t active_fat = 0;       // which copy is walked
    std::uint32_t root_cluster = 0;
    std::uint32_t total_sectors = 0;
    std::uint32_t cluster_count = 0;    // data clusters, numbered 2 .. cluster_count + 1
    std::uint32_t data_start_sector = 0;  // first sector of cluster 2, relative to the volume

    std::uint32_t blocks_per_sector() const { return bytes_per_sector / kFatBlockBytes; }
    std::uint64_t cluster_bytes() const { return std::uint64_t(bytes_per_sector) * sectors_per_cluster; }
    // Absolute device block of the first byte of a data cluster.
    std::uint64_t cluster_lba(std::uint32_t cluster) const;
};

struct Fat32Entry {
    std::string name;                 // long name when present, else the 8.3 name (UTF-8)
    std::string short_name;           // the 8.3 name as displayed ("mod.xml", "MYLONG~1.XML")
    bool is_directory = false;
    std::uint32_t size = 0;           // zero for directories
    std::uint32_t first_cluster = 0;  // zero for empty files
};

struct Fat32File {
    Fat32Entry entry;
    std::vector<Fragment> fragments;  // absolute device blocks, coalesced, whole clusters
};

struct Fat32Limits {
    std::uint32_t max_depth = 64;              // path components
    std::uint32_t max_directory_entries = 65536;
    std::uint32_t max_name = 255;              // UCS-2 units in a long name
};

class Fat32Volume {
public:
    // Mounts the volume at block 0, or the first FAT32 partition listed in
    // an MBR at block 0. Fails when neither parses.
    static bool mount(BlockReader reader, Fat32Volume& out, std::string& error,
                      const Fat32Limits& limits = Fat32Limits());
    // Mounts a volume whose boot sector is known to sit at `volume_lba`.
    static bool mount_at(BlockReader reader, std::uint64_t volume_lba, Fat32Volume& out,
                         std::string& error, const Fat32Limits& limits = Fat32Limits());

    const Fat32Geometry& geometry() const { return geo_; }

    // Resolves an absolute path ("/dir/file", case-insensitive ASCII, long
    // and short names both match) to its entry and fragment list.
    bool lookup(const std::string& path, Fat32File& out, std::string& error) const;
    // The same, telling a missing file or folder (`missing` set, false
    // returned) from a volume that cannot be read.
    bool lookup(const std::string& path, Fat32File& out, bool& missing, std::string& error) const;
    // Lists a directory's entries (no "." / "..", no volume label).
    bool list(const std::string& path, std::vector<Fat32Entry>& out, std::string& error) const;
    bool list(const std::string& path, std::vector<Fat32Entry>& out, bool& missing, std::string& error) const;
    // Follows a cluster chain into coalesced fragments. Rejects free, bad
    // and out-of-range entries and any chain longer than the volume.
    bool chain(std::uint32_t first_cluster, std::vector<Fragment>& out, std::string& error) const;
    // Reads file bytes through the fragment list (a test oracle and the
    // basis of an SD-backed ByteSource). Fails past the end of the file.
    bool read(const Fat32File& file, std::uint64_t offset, std::uint8_t* out, std::size_t length) const;
    // Drops the FAT window and the directory listings kept from earlier
    // lookups; needed once something else (libfat) has written the volume.
    void forget_cached() const;

private:
    bool next_cluster(std::uint32_t cluster, std::uint32_t& next, std::string& error) const;
    bool find_in_directory(std::uint32_t directory_cluster, const std::string& name,
                           Fat32Entry& out, bool& found, std::string& error) const;
    bool walk_directory(std::uint32_t directory_cluster,
                        const std::function<bool(const Fat32Entry&, bool& stop)>& visit,
                        std::string& error) const;
    bool resolve_directory(const std::string& path, std::uint32_t& cluster, std::string& error,
                           bool* missing = nullptr) const;
    // A directory's entries, read whole once and then served from memory:
    // a mod of two thousand files looks each one up from the root, and
    // walking the same folders sector by sector every time took minutes on
    // a Wii's SD card.
    bool directory_entries(std::uint32_t directory_cluster, const std::vector<Fat32Entry>*& out,
                           std::string& error) const;

    BlockReader reader_;
    Fat32Geometry geo_;
    Fat32Limits limits_;
    // A window of FAT blocks for chain traversal: a multi-GB image's chain
    // spans hundreds of KiB of FAT, and one USB read per 512-byte block
    // made a game list take minutes. Mutable because all public volume
    // lookups remain logically const; heap-backed so the volume stays small
    // enough for the stack.
    static constexpr std::uint32_t kFatCacheBlocks = 64;
    mutable std::uint64_t fat_cache_lba_ = 0;     // first block held
    mutable std::uint32_t fat_cache_count_ = 0;   // blocks held; zero when empty
    mutable std::vector<std::uint8_t> fat_cache_;
    // Listings by first cluster, dropped whole past kDirCacheEntries.
    static constexpr std::size_t kDirCacheEntries = 32768;
    mutable std::map<std::uint32_t, std::vector<Fat32Entry>> dir_cache_;
    mutable std::size_t dir_cache_entries_ = 0;
};

}  // namespace riftwii
