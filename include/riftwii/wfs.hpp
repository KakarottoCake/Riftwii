// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2017 koolkdev (the WFS layout, learned from wfslib)
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <vector>

#include "riftwii/fat32.hpp"

namespace riftwii {

// A drive formatted as WFS: the Wii U's file system, which IOS59 brought
// back to the Wii for Dragon Quest X. The game's installer formats a USB
// drive with it and keeps the game there; IOS59's /dev/usb/wfssrv and
// /dev/wfsi serve it to the game. RiftWii only reads it, to find what is
// installed (riftwii/wfstitle.hpp). The layout is the one wfslib
// (https://github.com/koolkdev/wfslib) documents; this is RiftWii's own
// reader of it, read-only and bounds-checked like the FAT32 and NTFS ones.
//
// The drive is read in 4 KiB "physical" blocks. Each metadata block starts
// with a 4-byte flags word and a SHA-1 of the block (hashed with those 20
// bytes as 0xFF); a data block's SHA-1 is kept by whoever points at it. With
// a key, every block is AES-128-CBC encrypted: the IV is four big-endian
// words, the bytes read, a per-block value, the drive's sector count and
// its sector size. The per-block value is 0 for block 0 and, for every
// other block, (area IV xor drive IV) plus the block's distance from its
// area's first block, in sectors.
//
// Block 0 holds the drive header and the root "area". An area is a run of
// blocks with its own block size (4 or 8 KiB) and IV. A directory is a
// radix tree of names (lowercase, with a bitmap of which letters were
// upper case) in one metadata block, or a tree of such blocks. A directory
// can be a "quota", an area of its own. A file's data is inline in its
// entry, or in blocks of one, eight or 64 area blocks that its entry (or
// separate metadata blocks, for big files) lists.

struct WfsEntry {
    std::string name;  // as created, upper case restored
    bool directory = false;
    bool link = false;          // a symbolic link: listed, never followed
    std::uint32_t size = 0;     // a file's size in bytes
    std::uint32_t mtime = 0;

    // Where it is, for list() and read(): its area and, for a directory,
    // the area block of its tree; for a file, its whole entry, which
    // lists its data.
    std::uint32_t area_block = 0;  // the area's first physical block
    std::uint32_t area_iv = 0;
    std::uint8_t area_log2 = 0;    // the area's block size
    std::uint32_t tree_block = 0;  // a directory's tree, in area blocks
    std::vector<std::uint8_t> metadata;
};

enum class WfsFind { Found, Missing, Failed };

class WfsVolume {
public:
    // Opens the WFS at `first_lba` (512-byte sectors), `sectors` long (the
    // drive's capacity for a drive WFS formatted whole: it is part of every
    // block's IV; 0 when not known, and it is worked out from block 0).
    // `key`: the console's 16-byte WFS key, or null for a volume with no
    // encryption (wfs-tools' "plain"). False with `error` saying why: no
    // WFS there, or a key that is not this drive's.
    bool open(BlockReader reader, std::uint64_t first_lba, std::uint64_t sectors, const std::uint8_t* key,
              std::string& error);

    std::uint32_t version() const { return version_; }
    std::uint16_t device_type() const { return device_type_; }
    std::uint8_t root_block_log2() const { return root_.log2; }
    std::uint32_t sectors() const { return sectors_; }

    // The entry at an absolute path ("/title/GD/S4MJ/content/default.dol",
    // any case). Missing when a part of it does not exist; Failed (with
    // `error`) when the drive could not be read or is damaged.
    WfsFind find(const std::string& path, WfsEntry& out, std::string& error) const;

    // A directory's entries, in the tree's (name) order. Capped at
    // `limit`: a damaged tree cannot make the menu list forever.
    bool list(const WfsEntry& directory, std::vector<WfsEntry>& out, std::string& error,
              std::size_t limit = 100000) const;

    // `length` bytes of a file from `offset`; false past its end, or when a
    // block cannot be read or fails its hash.
    bool read(const WfsEntry& file, std::uint32_t offset, std::uint8_t* out, std::size_t length,
              std::string& error) const;

    // A whole file of at most `max_bytes` (its memory asked for through
    // checkedalloc first).
    bool read_all(const std::string& path, std::vector<std::uint8_t>& out, std::uint32_t max_bytes,
                  std::string& error) const;

    struct Area {
        std::uint32_t first = 0;  // physical block of its header
        std::uint32_t iv = 0;
        std::uint8_t log2 = 0;    // its block size
        std::uint32_t root_directory = 0;
    };

private:
    void decrypt_block0(std::vector<std::uint8_t>& block, std::uint32_t sectors) const;
    bool read_block(std::uint32_t physical, std::uint8_t log2, std::uint32_t data_bytes, std::uint32_t iv,
                    bool encrypted, std::vector<std::uint8_t>& out, std::string& error) const;
    bool metadata_block(const Area& area, std::uint32_t area_block, std::uint8_t log2,
                        const std::vector<std::uint8_t>*& out, std::string& error) const;
    bool data_block(const Area& area, std::uint32_t area_block, std::uint8_t log2, std::uint32_t data_bytes,
                    const std::uint8_t* hash, bool encrypted, std::vector<std::uint8_t>& out,
                    std::string& error) const;
    std::uint32_t block_iv(const Area& area, std::uint32_t physical) const;
    bool tree_pairs(const std::vector<std::uint8_t>& block, bool parent_tree,
                    std::vector<std::pair<std::string, std::uint32_t>>& out, std::string& error) const;
    WfsFind lookup(const Area& area, std::uint32_t tree_block, const std::string& lower_name, WfsEntry& out,
                   std::string& error) const;
    bool walk(const Area& area, std::uint32_t tree_block, int depth, std::vector<WfsEntry>& out,
              std::size_t limit, std::string& error) const;
    bool make_entry(const Area& area, const std::string& key, const std::uint8_t* metadata, std::size_t room,
                    WfsEntry& out, std::string& error) const;
    bool quota_area(const Area& parent, std::uint32_t area_block, std::uint8_t log2, Area& out,
                    std::string& error) const;
    bool file_block(const WfsEntry& file, std::uint32_t index, std::uint32_t& area_block, std::uint8_t& log2,
                    const std::uint8_t*& hash, std::vector<std::uint8_t>& holder, std::string& error) const;

    BlockReader reader_;
    std::uint64_t first_lba_ = 0;
    std::uint32_t sectors_ = 0;
    bool encrypted_ = false;
    std::uint8_t key_[16] = {};
    std::uint32_t device_iv_ = 0;
    std::uint32_t version_ = 0;
    std::uint16_t device_type_ = 0;
    Area root_;

    // The last metadata blocks read, decrypted: a path walk and a listing
    // read the same few blocks again and again.
    struct Cached {
        std::uint32_t physical = 0;
        std::uint8_t log2 = 0;
        std::vector<std::uint8_t> bytes;
    };
    mutable std::list<Cached> cache_;
};

// Lowercase, as WFS keeps names (ASCII only, as the console does).
std::string wfs_lower(const std::string& name);

}  // namespace riftwii
