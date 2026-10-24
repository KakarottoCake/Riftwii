// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2017 koolkdev (the WFS layout, learned from wfslib)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/wfs.hpp"

#include <algorithm>
#include <cstring>

#include "riftwii/checkedalloc.hpp"
#include "riftwii/sha1.hpp"
#include "riftwii/wiicrypt.hpp"

namespace riftwii {
namespace {

constexpr std::uint32_t kSectorBytes = 512;
constexpr std::uint8_t kPhysicalLog2 = 12;           // the drive is addressed in 4 KiB blocks
constexpr std::uint32_t kSectorsPerPhysical = 8;     // 4 KiB in 512-byte sectors
constexpr std::uint8_t kLargeShift = 3;              // a large block is 8 area blocks
constexpr std::uint8_t kClusterShift = 6;            // a cluster is 8 large blocks
constexpr std::size_t kCacheBlocks = 24;
constexpr int kMaxTreeDepth = 16;                    // a directory's blocks: a few levels at most
constexpr int kMaxNodeDepth = 256;                   // a name's nodes: one per character at most
constexpr std::size_t kMaxNodes = 8192;              // nodes in one block (8 KiB, 2 bytes at least each)

// Metadata block header: flags, then the block's SHA-1.
constexpr std::size_t kHashOffset = 4;
constexpr std::size_t kHashBytes = 20;
constexpr std::size_t kBlockHeaderBytes = 0x18;
constexpr std::uint32_t kLeafTree = 0x20000000;

// The drive header, after block 0's own header.
constexpr std::size_t kDeviceIv = 0x18;
constexpr std::size_t kDeviceVersion = 0x1C;
constexpr std::size_t kDeviceType = 0x20;
constexpr std::size_t kDeviceRootQuotaBlocks = 0x0C + 0x14;  // the root quota's entry: its count of 8 KiB blocks
constexpr std::size_t kDeviceHeaderBytes = 0x48;

// An area's header: in block 0 after the drive header, in a quota's own
// first block after the block header.
constexpr std::size_t kAreaIv = 0x00;
constexpr std::size_t kAreaBlocksCount = 0x04;
constexpr std::size_t kAreaRootDirectory = 0x08;
constexpr std::size_t kAreaBlockLog2 = 0x15;
constexpr std::size_t kAreaHeaderBytes = 0x60;

// A directory tree block: the block header, a 32-byte sub-allocator, then
// the tree's root node offset and its count of names.
constexpr std::size_t kTreeRoot = 0x38;
constexpr std::size_t kTreeCount = 0x3A;
constexpr std::size_t kTreeFirstNode = 0x3C;

// An entry (wfslib's EntryMetadata).
constexpr std::size_t kEntryFlags = 0x00;
constexpr std::size_t kEntrySizeOnDisk = 0x04;
constexpr std::size_t kEntryMtime = 0x0C;
constexpr std::size_t kEntryFileSize = 0x14;
constexpr std::size_t kEntryDirectoryBlock = 0x18;
constexpr std::size_t kEntryLog2 = 0x28;
constexpr std::size_t kEntryCategory = 0x29;
constexpr std::size_t kEntryNameLength = 0x2A;
constexpr std::size_t kEntryCaseBitmap = 0x2B;
constexpr std::uint32_t kUnencrypted = 0x02000000;
constexpr std::uint32_t kLink = 0x04000000;
constexpr std::uint32_t kAreaSizeBasic = 0x10000000;
constexpr std::uint32_t kAreaSizeRegular = 0x20000000;
constexpr std::uint32_t kQuota = 0x40000000;
constexpr std::uint32_t kDirectory = 0x80000000;

// A file's data units, listed at the end of its entry.
constexpr std::size_t kBlockRefBytes = 4 + kHashBytes;        // block number, hash
constexpr std::size_t kClusterRefBytes = 4 + 8 * kHashBytes;  // first block, a hash per large block
constexpr std::uint32_t kMaxClustersPerMetadataBlock = 48;

enum Category : std::uint8_t { Inline = 0, Blocks = 1, LargeBlocks = 2, Clusters = 3, ClusterBlocks = 4 };

std::uint16_t be16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}
std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}
void put_be32(std::uint8_t* p, std::uint32_t x) {
    p[0] = static_cast<std::uint8_t>(x >> 24);
    p[1] = static_cast<std::uint8_t>(x >> 16);
    p[2] = static_cast<std::uint8_t>(x >> 8);
    p[3] = static_cast<std::uint8_t>(x);
}

std::size_t round_up_pow2(std::size_t size) {
    std::size_t p = 1;
    while (p < size) p <<= 1;
    return p;
}
std::uint32_t div_ceil(std::uint32_t n, std::uint32_t d) {
    return static_cast<std::uint32_t>((std::uint64_t{n} + d - 1) / d);
}

// The SHA-1 a metadata block carries of itself: over the whole block, its
// own 20 bytes counted as 0xFF.
bool metadata_hash_ok(const std::vector<std::uint8_t>& block) {
    if (block.size() < kBlockHeaderBytes) return false;
    static const std::uint8_t kPlaceholder[kHashBytes] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                          0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    Sha1 sha;
    sha.update(block.data(), kHashOffset);
    sha.update(kPlaceholder, kHashBytes);
    sha.update(block.data() + kHashOffset + kHashBytes, block.size() - kHashOffset - kHashBytes);
    const Sha1Digest digest = sha.finish();
    return std::memcmp(digest.data(), block.data() + kHashOffset, kHashBytes) == 0;
}

// Unsigned byte order, as the console compares names.
bool name_less_or_equal(const std::string& a, const std::string& b) {
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        const auto x = static_cast<unsigned char>(a[i]);
        const auto y = static_cast<unsigned char>(b[i]);
        if (x != y) return x < y;
    }
    return a.size() <= b.size();
}

std::string block_text(std::uint32_t physical) {
    return "block " + std::to_string(physical);
}

}  // namespace

std::string wfs_lower(const std::string& name) {
    std::string out = name;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

void WfsVolume::decrypt_block0(std::vector<std::uint8_t>& block, std::uint32_t sectors) const {
    if (!encrypted_) return;
    std::uint8_t aes_iv[16];
    put_be32(aes_iv, static_cast<std::uint32_t>(block.size()));
    put_be32(aes_iv + 4, 0);
    put_be32(aes_iv + 8, sectors);
    put_be32(aes_iv + 12, kSectorBytes);
    const Aes128 aes(key_);
    aes.cbc_decrypt(aes_iv, block.data(), block.data(), block.size());
}

bool WfsVolume::read_block(std::uint32_t physical, std::uint8_t log2, std::uint32_t data_bytes, std::uint32_t iv,
                           bool encrypted, std::vector<std::uint8_t>& out, std::string& error) const {
    const std::uint32_t capacity = std::uint32_t{1} << log2;
    if (data_bytes == 0 || data_bytes > capacity) {
        error = block_text(physical) + " was asked for " + std::to_string(data_bytes) + " bytes";
        return false;
    }
    // Sectors are read and decrypted whole; the data's own size is what
    // the caller keeps.
    const std::uint32_t aligned = div_ceil(data_bytes, kSectorBytes) * kSectorBytes;
    const std::uint64_t first_sector = std::uint64_t{physical} * kSectorsPerPhysical;
    const std::uint64_t sector_count = aligned / kSectorBytes;
    if (first_sector > sectors_ || sector_count > sectors_ - first_sector) {
        error = block_text(physical) + " lies past the end of the drive";
        return false;
    }
    if (!try_resize(out, aligned)) {
        error = "no memory for " + block_text(physical);
        return false;
    }
    if (!reader_(first_lba_ + first_sector, static_cast<std::uint32_t>(sector_count), out.data())) {
        error = block_text(physical) + " could not be read from the drive";
        return false;
    }
    if (encrypted_ && encrypted) {
        std::uint8_t aes_iv[16];
        put_be32(aes_iv, aligned);
        put_be32(aes_iv + 4, iv);
        put_be32(aes_iv + 8, sectors_);
        put_be32(aes_iv + 12, kSectorBytes);
        const Aes128 aes(key_);
        aes.cbc_decrypt(aes_iv, out.data(), out.data(), aligned);
    }
    return true;
}

std::uint32_t WfsVolume::block_iv(const Area& area, std::uint32_t physical) const {
    const std::uint32_t sectors_into_area = (physical - area.first) * kSectorsPerPhysical;
    return (area.iv ^ device_iv_) + sectors_into_area;
}

bool WfsVolume::metadata_block(const Area& area, std::uint32_t area_block, std::uint8_t log2,
                               const std::vector<std::uint8_t>*& out, std::string& error) const {
    // An area block is 1 or 2 physical blocks; the IV and the drive's
    // address both count from the area's first block.
    const std::uint8_t area_shift = static_cast<std::uint8_t>(area.log2 - kPhysicalLog2);
    const std::uint64_t physical = area.first + (std::uint64_t{area_block} << area_shift);
    if (physical > 0xFFFFFFFFull) {
        error = "area block " + std::to_string(area_block) + " lies past the end of the drive";
        return false;
    }
    const auto physical32 = static_cast<std::uint32_t>(physical);
    for (auto it = cache_.begin(); it != cache_.end(); ++it) {
        if (it->physical == physical32 && it->log2 == log2) {
            cache_.splice(cache_.begin(), cache_, it);
            out = &cache_.front().bytes;
            return true;
        }
    }
    Cached fresh;
    fresh.physical = physical32;
    fresh.log2 = log2;
    if (!read_block(physical32, log2, std::uint32_t{1} << log2, block_iv(area, physical32), true, fresh.bytes, error))
        return false;
    if (!metadata_hash_ok(fresh.bytes)) {
        error = block_text(physical32) + " (metadata) fails its hash: the drive is damaged";
        return false;
    }
    cache_.push_front(std::move(fresh));
    if (cache_.size() > kCacheBlocks) cache_.pop_back();
    out = &cache_.front().bytes;
    return true;
}

bool WfsVolume::data_block(const Area& area, std::uint32_t area_block, std::uint8_t log2, std::uint32_t data_bytes,
                           const std::uint8_t* hash, bool encrypted, std::vector<std::uint8_t>& out,
                           std::string& error) const {
    const std::uint8_t area_shift = static_cast<std::uint8_t>(area.log2 - kPhysicalLog2);
    const std::uint64_t physical = area.first + (std::uint64_t{area_block} << area_shift);
    if (physical > 0xFFFFFFFFull) {
        error = "data block " + std::to_string(area_block) + " lies past the end of the drive";
        return false;
    }
    const auto physical32 = static_cast<std::uint32_t>(physical);
    if (!read_block(physical32, log2, data_bytes, block_iv(area, physical32), encrypted, out, error)) return false;
    // The hash covers the sectors read, the padding after the data too.
    Sha1 sha;
    sha.update(out.data(), out.size());
    const Sha1Digest digest = sha.finish();
    if (std::memcmp(digest.data(), hash, kHashBytes) != 0) {
        error = block_text(physical32) + " (file data) fails its hash: the drive is damaged";
        return false;
    }
    out.resize(data_bytes);
    return true;
}

bool WfsVolume::open(BlockReader reader, std::uint64_t first_lba, std::uint64_t sectors, const std::uint8_t* key,
                     std::string& error) {
    reader_ = std::move(reader);
    first_lba_ = first_lba;
    // The IV carries the sector count in 32 bits: 2 TiB of 512-byte sectors.
    if (sectors > 0xFFFFFFFFull) {
        error = "the drive is larger than WFS can address (2 TiB)";
        return false;
    }
    encrypted_ = key != nullptr;
    if (key) std::memcpy(key_, key, sizeof key_);
    cache_.clear();

    // Block 0 as stored: 8 KiB, or 4 KiB on a drive that small.
    std::vector<std::uint8_t> raw(std::size_t{2} << kPhysicalLog2);
    if (sectors != 0 && sectors < kSectorsPerPhysical) {
        error = "the drive is smaller than one WFS block";
        return false;
    }
    const bool whole = sectors == 0 || sectors >= 2 * kSectorsPerPhysical;
    if (!whole || !reader_(first_lba_, 2 * kSectorsPerPhysical, raw.data())) {
        raw.resize(std::size_t{1} << kPhysicalLog2);
        if (!reader_(first_lba_, kSectorsPerPhysical, raw.data())) {
            error = "block 0 could not be read from the drive";
            return false;
        }
    }

    // The sector count is part of every block's IV. Without it (the menu
    // cannot always ask the drive), it comes from the header itself: a
    // wrong IV garbles only the first 16 bytes CBC decrypts, and the
    // header past them records how many 8 KiB blocks WFS formatted, the
    // sector count rounded down. Block 0's hash then says which count
    // within that rounding is the drive's.
    std::vector<std::uint32_t> candidates;
    if (sectors != 0) candidates.push_back(static_cast<std::uint32_t>(sectors));
    if (sectors == 0 || encrypted_) {
        std::vector<std::uint8_t> peek(raw);
        decrypt_block0(peek, 0);
        const std::uint64_t quota_blocks = be32(peek.data() + kBlockHeaderBytes + kDeviceRootQuotaBlocks);
        const std::uint8_t* area = peek.data() + kBlockHeaderBytes + kDeviceHeaderBytes;
        const std::uint8_t area_log2 = area[kAreaBlockLog2];
        const std::uint64_t area_blocks = be32(area + kAreaBlocksCount);
        std::vector<std::uint64_t> bases{quota_blocks << (kPhysicalLog2 + 1 - 9)};
        if (area_log2 >= 9 && area_log2 <= 16) bases.push_back(area_blocks << (area_log2 - 9));
        for (std::uint64_t base : bases) {
            for (std::uint64_t extra = 0; extra < 16; ++extra) {
                const std::uint64_t count = base + extra;
                if (count < raw.size() / kSectorBytes || count > 0xFFFFFFFFull) continue;
                if (std::find(candidates.begin(), candidates.end(), count) == candidates.end())
                    candidates.push_back(static_cast<std::uint32_t>(count));
            }
        }
    }

    // Block 0 is one physical block, or two when the root area uses 8 KiB
    // blocks: whichever size hashes right (with IV 0, as it is written).
    std::vector<std::uint8_t> block0;
    bool found = false;
    for (std::uint32_t count : candidates) {
        sectors_ = count;
        for (std::uint8_t log2 : {kPhysicalLog2, static_cast<std::uint8_t>(kPhysicalLog2 + 1)}) {
            if ((std::size_t{1} << log2) > raw.size()) continue;
            block0.assign(raw.begin(), raw.begin() + (std::ptrdiff_t{1} << log2));
            decrypt_block0(block0, sectors_);
            if (metadata_hash_ok(block0)) {
                found = true;
                break;
            }
        }
        // Unencrypted, the count changes nothing: the first is as good.
        if (found || !encrypted_) break;
    }
    if (!found) {
        error = encrypted_ ? "no WFS volume that this console's key opens (block 0 does not decrypt to a header)"
                           : "no unencrypted WFS volume here (block 0 has no header)";
        return false;
    }
    device_iv_ = be32(block0.data() + kDeviceIv);
    version_ = be32(block0.data() + kDeviceVersion);
    device_type_ = be16(block0.data() + kDeviceType);
    const std::uint8_t* area = block0.data() + kBlockHeaderBytes + kDeviceHeaderBytes;
    if (kBlockHeaderBytes + kDeviceHeaderBytes + kAreaHeaderBytes > block0.size()) {
        error = "block 0 is too small for the root area's header";
        return false;
    }
    root_.first = 0;
    root_.iv = be32(area + kAreaIv);
    root_.log2 = area[kAreaBlockLog2];
    root_.root_directory = be32(area + kAreaRootDirectory);
    if (root_.log2 < kPhysicalLog2 || root_.log2 > kPhysicalLog2 + 1) {
        error = "the root area's block size (2^" + std::to_string(root_.log2) + ") is not 4 or 8 KiB";
        return false;
    }
    return true;
}

bool WfsVolume::tree_pairs(const std::vector<std::uint8_t>& block, bool parent_tree,
                           std::vector<std::pair<std::string, std::uint32_t>>& out, std::string& error) const {
    out.clear();
    if (block.size() < kTreeFirstNode) {
        error = "a directory block is too small for its tree";
        return false;
    }
    const std::uint16_t count = be16(block.data() + kTreeCount);
    if (count == 0) return true;

    // Each node: its prefix length and key count, the prefix, one key byte
    // per child (a leading 0 key marks a value ending here), then from the
    // node's end backwards the children's offsets (and a parent tree's
    // 32-bit value). The node is sized to the next power of two.
    struct Pending {
        std::uint16_t offset;
        std::string key;
        int depth;
    };
    std::vector<Pending> stack{{be16(block.data() + kTreeRoot), std::string(), 0}};
    std::size_t visited = 0;
    while (!stack.empty()) {
        Pending node = std::move(stack.back());
        stack.pop_back();
        if (++visited > kMaxNodes || node.depth > kMaxNodeDepth) {
            error = "a directory tree loops or is too deep";
            return false;
        }
        const std::size_t o = node.offset;
        if (o < kTreeFirstNode || o + 2 > block.size()) {
            error = "a directory tree node lies outside its block";
            return false;
        }
        const std::uint8_t prefix_length = block[o];
        const std::uint8_t keys = block[o + 1];
        if (o + 2 + prefix_length + keys > block.size()) {
            error = "a directory tree node runs past its block";
            return false;
        }
        const std::uint8_t* key_bytes = block.data() + o + 2 + prefix_length;
        const bool has_value = keys > 0 && key_bytes[0] == 0;
        const std::size_t wide_value = parent_tree && has_value ? 2 : 0;
        const std::size_t node_size = round_up_pow2(2 + prefix_length + std::size_t{keys} * 3 + wide_value);
        if (o + node_size > block.size()) {
            error = "a directory tree node runs past its block";
            return false;
        }
        const std::size_t values_end = o + node_size - 2 - wide_value;
        std::string key = node.key;
        key.append(reinterpret_cast<const char*>(block.data() + o + 2), prefix_length);
        if (has_value) {
            const std::uint32_t value = parent_tree ? be32(block.data() + o + node_size - 4)
                                                    : be16(block.data() + values_end);
            out.emplace_back(key, value);
        }
        // Children last to first on the stack, so they come off in order.
        for (int i = keys - 1; i >= (has_value ? 1 : 0); --i) {
            const std::size_t at = values_end - 2 * static_cast<std::size_t>(i);
            std::string child_key = key;
            child_key.push_back(static_cast<char>(key_bytes[i]));
            stack.push_back(Pending{be16(block.data() + at), std::move(child_key), node.depth + 1});
        }
    }
    if (out.size() != count) {
        error = "a directory tree holds " + std::to_string(out.size()) + " names where its header says " +
                std::to_string(count);
        return false;
    }
    return true;
}

bool WfsVolume::quota_area(const Area& parent, std::uint32_t area_block, std::uint8_t log2, Area& out,
                           std::string& error) const {
    // A quota's first block is read as its parent's (its IV is the
    // parent's); everything inside it then counts from that block.
    const std::vector<std::uint8_t>* header = nullptr;
    if (!metadata_block(parent, area_block, log2, header, error)) return false;
    if (header->size() < kBlockHeaderBytes + kAreaHeaderBytes) {
        error = "a quota's header block is too small";
        return false;
    }
    const std::uint8_t* area = header->data() + kBlockHeaderBytes;
    const std::uint8_t area_shift = static_cast<std::uint8_t>(parent.log2 - kPhysicalLog2);
    out.first = parent.first + (area_block << area_shift);
    out.iv = be32(area + kAreaIv);
    out.log2 = area[kAreaBlockLog2];
    out.root_directory = be32(area + kAreaRootDirectory);
    if (out.log2 < kPhysicalLog2 || out.log2 > kPhysicalLog2 + 1) {
        error = "a quota's block size (2^" + std::to_string(out.log2) + ") is not 4 or 8 KiB";
        return false;
    }
    return true;
}

bool WfsVolume::make_entry(const Area& area, const std::string& key, const std::uint8_t* metadata, std::size_t room,
                           WfsEntry& out, std::string& error) const {
    if (room < kEntryCaseBitmap) {
        error = "an entry of \"" + key + "\" runs past its block";
        return false;
    }
    const std::uint8_t log2 = metadata[kEntryLog2];
    const std::size_t allocated = std::size_t{1} << std::min<std::uint8_t>(log2, 15);
    const std::uint8_t name_length = metadata[kEntryNameLength];
    if (log2 < 6 || log2 > 10 || allocated > room || name_length != key.size()) {
        error = "the entry of \"" + key + "\" is damaged";
        return false;
    }
    out = WfsEntry();
    const std::uint32_t flags = be32(metadata + kEntryFlags);
    // Which letters were upper case: one bit per character, low bit first.
    out.name = key;
    for (std::size_t i = 0; i < key.size(); ++i) {
        const bool upper = (metadata[kEntryCaseBitmap + i / 8] >> (i % 8)) & 1;
        const char c = key[i];
        if (upper && c >= 'a' && c <= 'z') out.name[i] = static_cast<char>(c - 'a' + 'A');
    }
    out.directory = (flags & kDirectory) != 0;
    out.link = (flags & kLink) != 0;
    out.mtime = be32(metadata + kEntryMtime);
    out.metadata.assign(metadata, metadata + allocated);
    out.area_block = area.first;
    out.area_iv = area.iv;
    out.area_log2 = area.log2;
    if (!out.directory) {
        out.size = be32(metadata + kEntryFileSize);
        return true;
    }
    const std::uint32_t tree = be32(metadata + kEntryDirectoryBlock);
    if (out.link) return true;
    if (flags & kQuota) {
        // A quota is an area of its own, of 4 KiB blocks unless marked
        // "regular" (8 KiB).
        const bool regular = !(flags & kAreaSizeBasic) && (flags & kAreaSizeRegular);
        const auto quota_log2 = static_cast<std::uint8_t>(regular ? kPhysicalLog2 + 1 : kPhysicalLog2);
        Area quota;
        if (!quota_area(area, tree, quota_log2, quota, error)) return false;
        out.area_block = quota.first;
        out.area_iv = quota.iv;
        out.area_log2 = quota.log2;
        out.tree_block = quota.root_directory;
    } else {
        out.tree_block = tree;
    }
    return true;
}

WfsFind WfsVolume::lookup(const Area& area, std::uint32_t tree_block, const std::string& lower_name, WfsEntry& out,
                          std::string& error) const {
    std::uint32_t block_number = tree_block;
    for (int depth = 0; depth < kMaxTreeDepth; ++depth) {
        const std::vector<std::uint8_t>* block = nullptr;
        if (!metadata_block(area, block_number, area.log2, block, error)) return WfsFind::Failed;
        const bool leaf = (be32(block->data()) & kLeafTree) != 0;
        std::vector<std::pair<std::string, std::uint32_t>> pairs;
        if (!tree_pairs(*block, !leaf, pairs, error)) return WfsFind::Failed;
        if (leaf) {
            for (const auto& [key, offset] : pairs) {
                if (key != lower_name) continue;
                if (offset >= block->size()) {
                    error = "the entry of \"" + key + "\" lies outside its block";
                    return WfsFind::Failed;
                }
                // Copied first: make_entry may read more blocks, and the
                // cache may drop this one meanwhile.
                const std::vector<std::uint8_t> copy(*block);
                if (!make_entry(area, key, copy.data() + offset, copy.size() - offset, out, error))
                    return WfsFind::Failed;
                return WfsFind::Found;
            }
            return WfsFind::Missing;
        }
        // A parent tree's keys split the names between its children: the
        // child for a name is the one with the greatest key not after it.
        bool chosen = false;
        std::string best;
        std::uint32_t next = 0;
        for (const auto& [key, child] : pairs) {
            if (!name_less_or_equal(key, lower_name)) continue;
            if (!chosen || name_less_or_equal(best, key)) {
                best = key;
                next = child;
                chosen = true;
            }
        }
        if (!chosen) return WfsFind::Missing;
        block_number = next;
    }
    error = "a directory's tree of blocks is too deep";
    return WfsFind::Failed;
}

WfsFind WfsVolume::find(const std::string& path, WfsEntry& out, std::string& error) const {
    out = WfsEntry();
    out.directory = true;
    out.area_block = root_.first;
    out.area_iv = root_.iv;
    out.area_log2 = root_.log2;
    out.tree_block = root_.root_directory;
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        const std::string part = path.substr(start, end - start);
        start = end + 1;
        if (part.empty() || part == ".") continue;
        if (!out.directory || out.link) return WfsFind::Missing;
        const Area area{out.area_block, out.area_iv, out.area_log2, out.tree_block};
        WfsEntry next;
        const WfsFind found = lookup(area, out.tree_block, wfs_lower(part), next, error);
        if (found != WfsFind::Found) return found;
        out = std::move(next);
    }
    return WfsFind::Found;
}

bool WfsVolume::walk(const Area& area, std::uint32_t tree_block, int depth, std::vector<WfsEntry>& out,
                     std::size_t limit, std::string& error) const {
    if (depth >= kMaxTreeDepth) {
        error = "a directory's tree of blocks is too deep";
        return false;
    }
    const std::vector<std::uint8_t>* cached = nullptr;
    if (!metadata_block(area, tree_block, area.log2, cached, error)) return false;
    const std::vector<std::uint8_t> block(*cached);  // the walk below reads other blocks
    const bool leaf = (be32(block.data()) & kLeafTree) != 0;
    std::vector<std::pair<std::string, std::uint32_t>> pairs;
    if (!tree_pairs(block, !leaf, pairs, error)) return false;
    for (const auto& [key, value] : pairs) {
        if (leaf) {
            if (out.size() >= limit) {
                error = "the directory has more than " + std::to_string(limit) + " entries";
                return false;
            }
            if (value >= block.size()) {
                error = "the entry of \"" + key + "\" lies outside its block";
                return false;
            }
            WfsEntry entry;
            if (!make_entry(area, key, block.data() + value, block.size() - value, entry, error)) return false;
            out.push_back(std::move(entry));
        } else if (!walk(area, value, depth + 1, out, limit, error)) {
            return false;
        }
    }
    return true;
}

bool WfsVolume::list(const WfsEntry& directory, std::vector<WfsEntry>& out, std::string& error,
                     std::size_t limit) const {
    out.clear();
    if (!directory.directory || directory.link) {
        error = "\"" + directory.name + "\" is not a directory";
        return false;
    }
    const Area area{directory.area_block, directory.area_iv, directory.area_log2, directory.tree_block};
    return walk(area, directory.tree_block, 0, out, limit, error);
}

bool WfsVolume::file_block(const WfsEntry& file, std::uint32_t index, std::uint32_t& area_block, std::uint8_t& log2,
                           const std::uint8_t*& hash, std::vector<std::uint8_t>& holder, std::string& error) const {
    const std::vector<std::uint8_t>& m = file.metadata;
    const std::uint8_t category = m[kEntryCategory];
    const std::uint32_t size_on_disk = be32(m.data() + kEntrySizeOnDisk);
    const std::size_t base = kEntryCaseBitmap + (std::size_t{m[kEntryNameLength]} + 7) / 8;
    const std::uint8_t large_log2 = static_cast<std::uint8_t>(file.area_log2 + kLargeShift);
    const std::uint8_t cluster_log2 = static_cast<std::uint8_t>(file.area_log2 + kClusterShift);
    // The units' list ends where the entry's power-of-two allocation does
    // and runs backwards: the file's first unit is the last one stored.
    const auto unit_at = [&](std::uint32_t count, std::size_t unit_bytes, std::uint32_t i) -> const std::uint8_t* {
        const std::size_t end = round_up_pow2(base + std::size_t{count} * unit_bytes);
        if (i >= count || end > m.size()) return nullptr;
        return m.data() + end - (std::size_t{i} + 1) * unit_bytes;
    };
    const std::uint8_t* unit = nullptr;
    switch (category) {
    case Blocks:
    case LargeBlocks: {
        log2 = category == Blocks ? file.area_log2 : large_log2;
        unit = unit_at(div_ceil(size_on_disk, std::uint32_t{1} << log2), kBlockRefBytes, index);
        if (!unit) break;
        area_block = be32(unit);
        hash = unit + 4;
        return true;
    }
    case Clusters: {
        log2 = large_log2;
        const std::uint32_t per_cluster = std::uint32_t{1} << (kClusterShift - kLargeShift);
        unit = unit_at(div_ceil(size_on_disk, std::uint32_t{1} << cluster_log2), kClusterRefBytes,
                       index / per_cluster);
        if (!unit) break;
        const std::uint32_t within = index % per_cluster;
        area_block = be32(unit) + (within << kLargeShift);
        hash = unit + 4 + within * kHashBytes;
        return true;
    }
    case ClusterBlocks: {
        // The entry lists metadata blocks, each a forward list of clusters.
        log2 = large_log2;
        const std::uint32_t per_cluster = std::uint32_t{1} << (kClusterShift - kLargeShift);
        const std::uint32_t per_block =
            std::min<std::uint32_t>(static_cast<std::uint32_t>(((std::size_t{1} << file.area_log2) - kBlockHeaderBytes) /
                                                               kClusterRefBytes),
                                    kMaxClustersPerMetadataBlock);
        const std::uint32_t clusters = div_ceil(size_on_disk, std::uint32_t{1} << cluster_log2);
        unit = unit_at(div_ceil(clusters, per_block), 4, index / per_cluster / per_block);
        if (!unit) break;
        const Area area{file.area_block, file.area_iv, file.area_log2, 0};
        const std::vector<std::uint8_t>* list_block = nullptr;
        if (!metadata_block(area, be32(unit), file.area_log2, list_block, error)) return false;
        holder = *list_block;
        const std::uint32_t within_block = index % (per_cluster * per_block);
        const std::size_t at = kBlockHeaderBytes + std::size_t{within_block / per_cluster} * kClusterRefBytes;
        if (at + kClusterRefBytes > holder.size()) break;
        const std::uint32_t within = within_block % per_cluster;
        area_block = be32(holder.data() + at) + (within << kLargeShift);
        hash = holder.data() + at + 4 + within * kHashBytes;
        return true;
    }
    default:
        error = "\"" + file.name + "\" has an unknown data layout (" + std::to_string(category) + ")";
        return false;
    }
    error = "the block list of \"" + file.name + "\" is damaged";
    return false;
}

bool WfsVolume::read(const WfsEntry& file, std::uint32_t offset, std::uint8_t* out, std::size_t length,
                     std::string& error) const {
    if (file.directory || file.metadata.size() <= kEntryCaseBitmap) {
        error = "\"" + file.name + "\" is not a file";
        return false;
    }
    if (offset > file.size || length > file.size - offset) {
        error = "a read past the end of \"" + file.name + "\"";
        return false;
    }
    if (length == 0) return true;
    const std::vector<std::uint8_t>& m = file.metadata;
    const bool encrypted = (be32(m.data() + kEntryFlags) & kUnencrypted) == 0;
    if (m[kEntryCategory] == Inline) {
        // Inline data follows the name's case bitmap.
        const std::size_t base = kEntryCaseBitmap + (std::size_t{m[kEntryNameLength]} + 7) / 8;
        if (base + file.size > m.size()) {
            error = "the inline data of \"" + file.name + "\" is damaged";
            return false;
        }
        std::memcpy(out, m.data() + base + offset, length);
        return true;
    }
    const Area area{file.area_block, file.area_iv, file.area_log2, 0};
    std::vector<std::uint8_t> holder;
    std::vector<std::uint8_t> data;
    std::uint64_t at = offset;
    while (length > 0) {
        std::uint32_t area_block = 0;
        std::uint8_t log2 = 0;
        const std::uint8_t* hash = nullptr;
        // Every layout's data blocks are of one size: the index of the
        // block a byte is in comes from that size alone.
        const std::uint8_t data_log2 = m[kEntryCategory] == Blocks ? file.area_log2
                                                              : static_cast<std::uint8_t>(file.area_log2 + kLargeShift);
        const auto index = static_cast<std::uint32_t>(at >> data_log2);
        if (!file_block(file, index, area_block, log2, hash, holder, error)) return false;
        const std::uint64_t block_start = std::uint64_t{index} << log2;
        const auto data_bytes = static_cast<std::uint32_t>(std::min<std::uint64_t>(std::uint64_t{1} << log2,
                                                                                    file.size - block_start));
        if (!data_block(area, area_block, log2, data_bytes, hash, encrypted, data, error)) return false;
        const auto skip = static_cast<std::size_t>(at - block_start);
        const std::size_t take = std::min<std::size_t>(length, data.size() - skip);
        std::memcpy(out, data.data() + skip, take);
        out += take;
        at += take;
        length -= take;
    }
    return true;
}

bool WfsVolume::read_all(const std::string& path, std::vector<std::uint8_t>& out, std::uint32_t max_bytes,
                         std::string& error) const {
    WfsEntry entry;
    const WfsFind found = find(path, entry, error);
    if (found == WfsFind::Missing) error = path + " is not on the drive";
    if (found != WfsFind::Found) return false;
    if (entry.directory) {
        error = path + " is a directory";
        return false;
    }
    if (entry.size > max_bytes) {
        error = path + " is larger (" + std::to_string(entry.size) + " bytes) than RiftWii reads (" +
                std::to_string(max_bytes) + ")";
        return false;
    }
    if (!try_resize(out, entry.size)) {
        error = "no memory for " + path;
        return false;
    }
    return read(entry, 0, out.data(), out.size(), error);
}

}  // namespace riftwii
