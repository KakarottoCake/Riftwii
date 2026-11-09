// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "riftwii/ctgphash.hpp"
#include "riftwii/fst.hpp"

// How CTGP-R's code finds its files (docs/CTGPR.md): every file of
// every package gets a 200-byte open-file record (a libfat FILE_STRUCT) in a
// table in the game's memory, and the game's file system table points the
// file at a "virtual handle" in place of a disc offset. The handle has its
// top bit set, so the hook on the disc read sees a negative offset and sends
// the read to the record. The numbers follow the channel's FUN_809561d4.
namespace riftwii {

constexpr std::uint32_t kCtgpRecordSize = 200;
constexpr std::uint32_t kCtgpRecordBlock = 0x816228B8;  // address of the table in the game
constexpr std::uint32_t kCtgpRecordCount = 2921;        // 0x8EA08 bytes

// The table is cut into ten pools by file size. A file goes into the first
// pool that suits its size and, when that one is full, into the next ones
// (in pool order). The handle is `handle_base + index * handle_stride`: the
// ten ranges tile 0x80000000-0xFFFFFFFF without gaps.
struct CtgpRecordPool {
    std::uint32_t start;         // address of the pool's first record
    std::uint32_t count;
    std::uint32_t size_below;    // a file smaller than this starts here
    std::uint32_t handle_base;
    std::uint32_t handle_stride;
};
constexpr std::size_t kCtgpPoolCount = 10;
extern const CtgpRecordPool kCtgpRecordPools[kCtgpPoolCount];

// The libfat partitions the records belong to: "sda" (the SD card), "efa"
// (the package disk), "efb". -1 for another name.
int ctgp_device_index(const std::string& device);
constexpr std::uint32_t ctgp_partition_address(int device) {
    return 0x816B5740u + static_cast<std::uint32_t>(device) * 0x18260u;
}

struct CtgpOpenFile {
    std::uint32_t size = 0;
    std::uint32_t first_cluster = 0;  // 0 for an empty file
    bool hashed = false;              // flag 2 in the package list: SHA-1 state kept in the record
    std::array<std::uint8_t, 20> digest{};  // hashed: the digest the whole file must have (record offset 0xB4)
};

// One record, big endian, as the channel's open leaves it for a file opened
// read only: size, start cluster, the read and append positions at the start
// of the chain, the owning partition and the links to the other open files of
// that partition (`prev` is the newer one, `next` the older). The positions
// of the file's directory entry are left at zero (nothing is ever written).
std::vector<std::uint8_t> ctgp_build_record(const CtgpOpenFile& file, std::uint32_t partition,
                                            std::uint32_t prev, std::uint32_t next);

class CtgpRecordTable {
public:
    struct Slot {
        std::uint32_t address = 0;  // the record's address in the game
        std::uint32_t handle = 0;   // what goes in the file system table's offset field
        std::uint32_t size = 0;
    };

    CtgpRecordTable();

    // The record for `key` (the file as the package spells it, compared byte
    // for byte), added when it is new. False when every suitable pool is full.
    bool add(const std::string& key, int device, const CtgpOpenFile& file, Slot& slot);
    // True when `key` already has a record.
    bool has(const std::string& key) const;

    // The whole table (kCtgpRecordCount * 200 bytes), unused records zero.
    const std::vector<std::uint8_t>& image() const { return image_; }
    std::uint32_t used() const { return used_; }
    // The partition's open-file list: how many and the newest one's address
    // (partition + 0x48 and + 0x4C).
    std::uint32_t open_count(int device) const;
    std::uint32_t first_open(int device) const;

private:
    std::vector<std::uint8_t> image_;
    std::vector<std::uint32_t> next_free_;  // per pool
    // Keyed by the package's own spelling; a package lists up to ~2,900 files,
    // so a linear search per add made each package quadratic.
    std::unordered_map<std::string, Slot> entries_;
    std::uint32_t head_[3] = {0, 0, 0};
    std::uint32_t count_[3] = {0, 0, 0};
    std::uint32_t used_ = 0;
};

// One file of a package: where it goes in the game's file system, which file
// on which device holds it, and the list's flags (1 = the package fails
// without it, 2 = the file has to be on the signed list).
struct CtgpPackageFile {
    std::string game_path;  // absolute, "/Scene/UI/HS/...": the package's own tree
    std::string source;     // "efa:/packages/ctgpr/Scene/UI/HS/HealthSafety.szs"
    std::uint32_t flags = 0;
};

// Reads a package's FST.BIN: a file system table whose file entries carry, in
// place of the offset, the position of the source path (a C string inside the
// file) and, in place of the size, the flags.
bool ctgp_parse_package_fst(const std::uint8_t* data, std::size_t size, std::vector<CtgpPackageFile>& out,
                            std::string& error);

// What looking a source file up found.
struct CtgpSourceFile {
    std::uint32_t size = 0;
    std::uint32_t first_cluster = 0;
};
// `device` is "sda", "efa" or "efb", `path` what follows the colon. False when
// the file does not exist.
using CtgpSourceResolver = std::function<bool(const std::string& device, const std::string& path, CtgpSourceFile& out)>;

struct CtgpMergedFile {
    std::string game_path;
    std::uint32_t handle = 0;
    std::uint32_t size = 0;
};

// The package's first pass in the channel: every file is looked up and given
// a record. A file that is missing (or finds no room) is left out, unless its
// flags say the package cannot do without it, which fails the package.
// A file with flag 2 has to be on `hashes`, the package's verified list of
// digests (ctgp_load_hash_list), which gives its record the digest the game's
// code will hold the file to; a signed file the list does not name, or no list
// at all, fails the package, as in the channel. `out` gets the files that have
// a record, in list order.
bool ctgp_add_package(CtgpRecordTable& table, const std::vector<CtgpPackageFile>& files,
                      const CtgpSourceResolver& resolver, const CtgpHashList* hashes,
                      std::vector<CtgpMergedFile>& out, std::string& error);

// Puts the files into the game's file system table: a file the game already
// has is pointed at the handle, a new one is created (with its directories).
// Later calls override earlier ones, as the packages do.
bool ctgp_merge_files(Fst& fst, const std::vector<CtgpMergedFile>& files, std::string& error);

}  // namespace riftwii
