// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A WFS drive made in memory for the tests (wfs_tests, wfstitle_tests):
// just enough of a writer to lay out what the reader walks, with the
// console's encryption and hashes. Nothing here allocates the way the
// console does; the reader only follows offsets and block numbers, so
// bump allocation inside a block is enough.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "riftwii/fat32.hpp"
#include "riftwii/sha1.hpp"
#include "riftwii/wiicrypt.hpp"

namespace wfsimage {

using Bytes = std::vector<std::uint8_t>;

inline void be16(std::uint8_t* p, std::uint32_t x) {
    p[0] = static_cast<std::uint8_t>(x >> 8);
    p[1] = static_cast<std::uint8_t>(x);
}
inline void be32(std::uint8_t* p, std::uint32_t x) {
    p[0] = static_cast<std::uint8_t>(x >> 24);
    p[1] = static_cast<std::uint8_t>(x >> 16);
    p[2] = static_cast<std::uint8_t>(x >> 8);
    p[3] = static_cast<std::uint8_t>(x);
}
inline std::size_t pow2_at_least(std::size_t n) {
    std::size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}
inline std::uint8_t log2_at_least(std::size_t n) {
    std::uint8_t l = 0;
    while ((std::size_t{1} << l) < n) ++l;
    return l;
}

constexpr std::uint32_t kDirectory = 0x80000000, kQuota = 0x40000000, kAreaRegular = 0x20000000,
                        kAreaBasic = 0x10000000, kLink = 0x04000000, kUnencrypted = 0x02000000;

struct Area {
    std::uint32_t first = 0;  // physical block
    std::uint32_t iv = 0;
    std::uint8_t log2 = 13;
    std::uint32_t physical(std::uint32_t area_block) const { return first + (area_block << (log2 - 12)); }
};

// One entry of a directory, before it is laid into a tree block.
struct Entry {
    std::string name;  // as created; the tree keeps it lowercase
    std::uint32_t flags = 0;
    std::uint32_t file_size = 0;
    std::uint32_t size_on_disk = 0;
    std::uint32_t directory_block = 0;
    std::uint8_t category = 0;
    Bytes units;   // the data units, in the file's order (stored reversed)
    std::size_t unit_bytes = 0;
    Bytes inline_data;
};

inline Bytes entry_bytes(const Entry& e) {
    const std::size_t base = 0x2B + (e.name.size() + 7) / 8;
    const std::size_t count = e.unit_bytes ? e.units.size() / e.unit_bytes : 0;
    const std::size_t used = base + (e.category == 0 ? e.inline_data.size() : e.units.size());
    const std::uint8_t log2 = std::max<std::uint8_t>(6, log2_at_least(used));
    Bytes m(std::size_t{1} << log2, 0);
    be32(&m[0x00], e.flags);
    be32(&m[0x04], e.size_on_disk);
    be32(&m[0x08], 1000);
    be32(&m[0x0C], 2000);
    be32(&m[0x14], e.file_size);
    be32(&m[0x18], e.directory_block);
    m[0x28] = log2;
    m[0x29] = e.category;
    m[0x2A] = static_cast<std::uint8_t>(e.name.size());
    for (std::size_t i = 0; i < e.name.size(); ++i) {
        if (e.name[i] >= 'A' && e.name[i] <= 'Z') m[0x2B + i / 8] |= static_cast<std::uint8_t>(1 << (i % 8));
    }
    if (e.category == 0) {
        std::copy(e.inline_data.begin(), e.inline_data.end(), m.begin() + static_cast<std::ptrdiff_t>(base));
    } else {
        // Units fill the allocation from its end backwards.
        const std::size_t end = pow2_at_least(base + e.units.size());
        for (std::size_t i = 0; i < count; ++i) {
            std::copy(e.units.begin() + static_cast<std::ptrdiff_t>(i * e.unit_bytes),
                      e.units.begin() + static_cast<std::ptrdiff_t>((i + 1) * e.unit_bytes),
                      m.begin() + static_cast<std::ptrdiff_t>(end - (i + 1) * e.unit_bytes));
        }
    }
    return m;
}

// A directory tree block: a radix tree of lowercase names. `values`: for
// a leaf tree, each name's entry (placed in the block); for a parent
// tree, each split key's child block number.
class TreeBlock {
public:
    TreeBlock(std::uint8_t log2, bool leaf) : bytes_(std::size_t{1} << log2, 0), leaf_(leaf) {
        be32(&bytes_[0], 0x80000000u | (leaf ? 0x20000000u : 0u));
    }
    void add_entry(const Entry& e) {
        std::string key = e.name;
        for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        entries_[key] = entry_bytes(e);
    }
    void add_child(const std::string& key, std::uint32_t block) { children_[key] = block; }

    Bytes finish() {
        std::map<std::string, std::uint32_t> values;
        if (leaf_) {
            for (const auto& [key, m] : entries_) values[key] = alloc_copy(m);
        } else {
            values = children_;
        }
        be16(&bytes_[0x3A], static_cast<std::uint32_t>(values.size()));
        if (!values.empty()) {
            std::vector<std::pair<std::string, std::uint32_t>> items(values.begin(), values.end());
            be16(&bytes_[0x38], node(items));
        }
        return bytes_;
    }

private:
    std::uint32_t alloc(std::size_t size) {
        size = std::max<std::size_t>(8, pow2_at_least(size));
        next_ = (next_ + size - 1) / size * size;
        const std::size_t at = next_;
        next_ += size;
        if (next_ > bytes_.size()) throw "tree block full";
        return static_cast<std::uint32_t>(at);
    }
    std::uint32_t alloc_copy(const Bytes& b) {
        const std::uint32_t at = alloc(b.size());
        std::copy(b.begin(), b.end(), bytes_.begin() + at);
        return at;
    }
    // A node for names that share everything consumed so far.
    std::uint16_t node(const std::vector<std::pair<std::string, std::uint32_t>>& items) {
        std::string prefix = items.front().first;
        for (const auto& it : items) {
            std::size_t n = 0;
            while (n < prefix.size() && n < it.first.size() && prefix[n] == it.first[n]) ++n;
            prefix.resize(n);
        }
        bool has_value = false;
        std::uint32_t value = 0;
        std::map<unsigned char, std::vector<std::pair<std::string, std::uint32_t>>> groups;
        for (const auto& it : items) {
            if (it.first.size() == prefix.size()) {
                has_value = true;
                value = it.second;
            } else {
                groups[static_cast<unsigned char>(it.first[prefix.size()])].emplace_back(
                    it.first.substr(prefix.size() + 1), it.second);
            }
        }
        std::vector<std::pair<unsigned char, std::uint16_t>> kids;
        for (const auto& [c, sub] : groups) kids.emplace_back(c, node(sub));
        const std::size_t keys = kids.size() + (has_value ? 1 : 0);
        const std::size_t wide = !leaf_ && has_value ? 2 : 0;
        const std::size_t size = pow2_at_least(2 + prefix.size() + keys * 3 + wide);
        const std::uint32_t at = alloc(size);
        std::uint8_t* n = &bytes_[at];
        n[0] = static_cast<std::uint8_t>(prefix.size());
        n[1] = static_cast<std::uint8_t>(keys);
        std::memcpy(n + 2, prefix.data(), prefix.size());
        std::uint8_t* key_bytes = n + 2 + prefix.size();
        const std::size_t values_end = size - 2 - wide;
        std::size_t i = 0;
        if (has_value) {
            key_bytes[i] = 0;
            if (leaf_) be16(n + values_end, value);
            else be32(n + size - 4, value);
            ++i;
        }
        for (const auto& [c, offset] : kids) {
            key_bytes[i] = c;
            be16(n + values_end - 2 * i, offset);
            ++i;
        }
        return static_cast<std::uint16_t>(at);
    }

    Bytes bytes_;
    bool leaf_;
    std::size_t next_ = 0x40;
    std::map<std::string, Bytes> entries_;
    std::map<std::string, std::uint32_t> children_;
};

class Drive {
public:
    Drive(std::uint32_t physical_blocks, const std::uint8_t* key, std::uint32_t device_iv = 0x13572468)
        : image_(std::size_t{physical_blocks} * 4096, 0), encrypted_(key != nullptr), device_iv_(device_iv) {
        if (key) std::memcpy(key_, key, 16);
    }
    std::uint32_t sectors() const { return static_cast<std::uint32_t>(image_.size() / 512); }
    Bytes& image() { return image_; }

    std::uint32_t iv_for(const Area& area, std::uint32_t physical) const {
        return (area.iv ^ device_iv_) + (physical - area.first) * 8;
    }

    // A metadata block: its hash set, then written at `physical`.
    void put_metadata(std::uint32_t physical, Bytes block, std::uint32_t iv) {
        std::fill(block.begin() + 4, block.begin() + 24, 0xFF);
        riftwii::Sha1 sha;
        sha.update(block.data(), block.size());
        const riftwii::Sha1Digest d = sha.finish();
        std::copy(d.begin(), d.end(), block.begin() + 4);
        store(physical, block, iv, true);
    }
    void put_metadata(const Area& area, std::uint32_t area_block, const Bytes& block) {
        const std::uint32_t p = area.physical(area_block);
        put_metadata(p, block, iv_for(area, p));
    }

    // A data block holding `data` (padded to whole sectors): returns the
    // hash its owner keeps.
    riftwii::Sha1Digest put_data(const Area& area, std::uint32_t area_block, const Bytes& data, bool encrypt = true) {
        Bytes padded = data;
        padded.resize((data.size() + 511) / 512 * 512, 0);
        riftwii::Sha1 sha;
        sha.update(padded.data(), padded.size());
        const riftwii::Sha1Digest d = sha.finish();
        const std::uint32_t p = area.physical(area_block);
        store(p, padded, iv_for(area, p), encrypt);
        return d;
    }

    // Block 0: the drive header and the root area's.
    void put_header(const Area& root, std::uint32_t root_directory, std::uint32_t version = 0x01010800) {
        Bytes b(std::size_t{1} << root.log2, 0);
        be32(&b[0], 0x00C00000);
        be32(&b[0x18], device_iv_);
        be32(&b[0x1C], version);
        be16(&b[0x20], 0x16A2);
        be32(&b[0x38], sectors() >> 4);  // the root quota's 8 KiB blocks
        std::uint8_t* area = &b[0x60];
        be32(area + 0x00, root.iv);
        be32(area + 0x04, static_cast<std::uint32_t>(image_.size() >> root.log2));
        be32(area + 0x08, root_directory);
        area[0x15] = root.log2;
        area[0x16] = static_cast<std::uint8_t>(root.log2 + 3);
        area[0x17] = static_cast<std::uint8_t>(root.log2 + 6);
        area[0x18] = 1;
        put_metadata(0, b, 0);
    }

    // A quota's first block, read through its parent area.
    void put_quota_header(const Area& parent, std::uint32_t area_block, const Area& quota,
                          std::uint32_t root_directory) {
        Bytes b(std::size_t{1} << quota.log2, 0);
        be32(&b[0], 0x00400000);
        std::uint8_t* area = &b[0x18];
        be32(area + 0x00, quota.iv);
        be32(area + 0x08, root_directory);
        area[0x15] = quota.log2;
        const std::uint32_t p = parent.physical(area_block);
        put_metadata(p, b, iv_for(parent, p));
    }

    riftwii::BlockReader reader() const {
        return [this](std::uint64_t lba, std::uint32_t count, std::uint8_t* out) {
            if ((lba + count) * 512 > image_.size()) return false;
            std::memcpy(out, image_.data() + lba * 512, std::size_t{count} * 512);
            return true;
        };
    }

private:
    void store(std::uint32_t physical, Bytes bytes, std::uint32_t iv, bool encrypt) {
        if (encrypted_ && encrypt) {
            std::uint8_t aes_iv[16];
            be32(aes_iv, static_cast<std::uint32_t>(bytes.size()));
            be32(aes_iv + 4, iv);
            be32(aes_iv + 8, sectors());
            be32(aes_iv + 12, 512);
            const riftwii::Aes128 aes(key_);
            aes.cbc_encrypt(aes_iv, bytes.data(), bytes.data(), bytes.size());
        }
        std::copy(bytes.begin(), bytes.end(), image_.begin() + std::size_t{physical} * 4096);
    }

    Bytes image_;
    bool encrypted_;
    std::uint8_t key_[16] = {};
    std::uint32_t device_iv_;
};

// A file's units: a block list (categories 1 and 2) of block number and
// hash pairs, in the file's order.
inline void add_block_unit(Entry& e, std::uint32_t block, const riftwii::Sha1Digest& hash) {
    std::uint8_t b[24];
    be32(b, block);
    std::memcpy(b + 4, hash.data(), 20);
    e.units.insert(e.units.end(), b, b + 24);
    e.unit_bytes = 24;
}

inline Bytes pattern(std::size_t n, std::uint32_t seed) {
    Bytes out(n);
    std::uint32_t x = seed;
    for (auto& b : out) {
        x = x * 1103515245u + 12345u;
        b = static_cast<std::uint8_t>(x >> 16);
    }
    return out;
}

}  // namespace wfsimage
