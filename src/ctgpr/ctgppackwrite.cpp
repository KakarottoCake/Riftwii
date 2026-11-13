// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgppackwrite.hpp"

#include <algorithm>
#include <cstring>
#include <memory>

#include "riftwii/ctgpefa.hpp"
#include "riftwii/ctgptables.hpp"
#include "riftwii/wiicrypt.hpp"

namespace riftwii {
namespace {

constexpr std::uint32_t kChunkBytes = kCtgpEfaChunkBytes;  // 32 KiB
constexpr std::uint32_t kSectorBytes = 512;
constexpr std::uint32_t kFatEndOfChain = 0x0FFFFFF8u;
constexpr std::uint32_t kFatMask = 0x0FFFFFFFu;

std::uint16_t le16(const std::uint8_t* p) {
    return std::uint16_t(p[0]) | (std::uint16_t(p[1]) << 8);
}

std::uint32_t le32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) |
           (std::uint32_t(p[3]) << 24);
}

void put_le32(std::uint8_t* p, std::uint32_t value) {
    p[0] = std::uint8_t(value);
    p[1] = std::uint8_t(value >> 8);
    p[2] = std::uint8_t(value >> 16);
    p[3] = std::uint8_t(value >> 24);
}

// ASCII case folding, the only case the packs use. A byte above 0x7F is
// compared as it is; long names are UTF-16 and only their ASCII part is
// compared here.
char fold(char c) {
    return (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A') : c;
}

bool same_name(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (fold(a[i]) != fold(b[i])) {
            return false;
        }
    }
    return true;
}

// The sectors of the encrypted file are read and written through one 32 KiB
// chunk at a time: the update only touches a small part of the disk, and
// re-encrypting every chunk it passes would rewrite the whole pack.
class PackImage {
public:
    PackImage(CtgpPackIo io, std::uint64_t size, std::array<std::uint8_t, 16> key,
              CtgpChunkIv iv)
        : io_(std::move(io)), size_(size), iv_(iv), aes_(key.data()) {}

    std::uint64_t size() const { return size_; }

    bool read(std::uint64_t offset, std::uint8_t* out, std::size_t length) {
        while (length > 0) {
            const std::uint64_t chunk = offset / kChunkBytes;
            const std::uint32_t within = std::uint32_t(offset % kChunkBytes);
            const std::size_t take = std::min<std::size_t>(length, kChunkBytes - within);
            if (!load(chunk)) {
                return false;
            }
            std::memcpy(out, chunk_.bytes.data() + within, take);
            offset += take;
            out += take;
            length -= take;
        }
        return true;
    }

    bool write(std::uint64_t offset, const std::uint8_t* data, std::size_t length) {
        while (length > 0) {
            const std::uint64_t chunk = offset / kChunkBytes;
            const std::uint32_t within = std::uint32_t(offset % kChunkBytes);
            const std::size_t take = std::min<std::size_t>(length, kChunkBytes - within);
            if (!load(chunk)) {
                return false;
            }
            std::memcpy(chunk_.bytes.data() + within, data, take);
            dirty_ = true;
            offset += take;
            data += take;
            length -= take;
        }
        return true;
    }

    bool flush() {
        if (!dirty_) {
            return true;
        }
        if (!write_chunk()) {
            return false;
        }
        dirty_ = false;
        return true;
    }

private:
    bool load(std::uint64_t chunk) {
        if (chunk_.loaded && chunk_.index == chunk) {
            return true;
        }
        if (!flush()) {
            return false;
        }
        const std::uint64_t offset = chunk * kChunkBytes;
        if (offset + kChunkBytes > size_) {
            return false;
        }
        std::vector<std::uint8_t> cipher(kChunkBytes);
        if (!io_.read(offset, kChunkBytes, cipher.data())) {
            return false;
        }
        const std::uint32_t sector = std::uint32_t(offset / kSectorBytes);
        const auto iv = ctgp_efa_chunk_iv(iv_, sector);
        aes_.cbc_decrypt(iv.data(), cipher.data(), chunk_.bytes.data(), kChunkBytes);
        chunk_.loaded = true;
        chunk_.index = chunk;
        return true;
    }

    bool write_chunk() {
        if (!chunk_.loaded) {
            return true;
        }
        std::vector<std::uint8_t> cipher(kChunkBytes);
        const std::uint64_t offset = chunk_.index * kChunkBytes;
        const std::uint32_t sector = std::uint32_t(offset / kSectorBytes);
        const auto iv = ctgp_efa_chunk_iv(iv_, sector);
        aes_.cbc_encrypt(iv.data(), chunk_.bytes.data(), cipher.data(), kChunkBytes);
        return io_.write(offset, cipher.data(), kChunkBytes);
    }

    struct Chunk {
        bool loaded = false;
        std::uint64_t index = 0;
        std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(kChunkBytes);
    };

    CtgpPackIo io_;
    std::uint64_t size_;
    CtgpChunkIv iv_;
    Aes128 aes_;
    Chunk chunk_;
    bool dirty_ = false;
};

struct Dirent {
    bool found = false;
    std::uint64_t offset = 0;   // absolute byte offset of the 32-byte entry
    std::uint32_t cluster = 0;
    std::uint32_t size = 0;
    bool directory = false;
    std::uint16_t write_time = 0;
    std::uint16_t write_date = 0;
};

}  // namespace

struct CtgpPackWriter::Impl {
    PackImage* image = nullptr;
    CtgpPackIo io;
    std::uint64_t size = 0;
    // Geometry (all sectors are 512 bytes on these disks).
    std::uint32_t bytes_per_sector = 0;
    std::uint32_t sectors_per_cluster = 0;
    std::uint32_t reserved = 0;
    std::uint32_t fats = 0;
    std::uint32_t fat_sectors = 0;
    std::uint32_t root_cluster = 0;
    std::uint32_t total_sectors = 0;
    std::uint32_t data_start = 0;
    std::uint32_t cluster_count = 0;
    std::uint32_t max_cluster = 0;  // bounded by the file's own size
    std::uint32_t fsinfo_sector = 0;

    std::vector<std::uint32_t> fat;
    std::vector<std::uint8_t> fat_dirty;  // one flag per 512-byte FAT sector
    std::uint32_t next_free_hint = 2;

    std::uint64_t cluster_bytes() const {
        return std::uint64_t(bytes_per_sector) * sectors_per_cluster;
    }
    std::uint64_t cluster_offset(std::uint32_t cluster) const {
        return (std::uint64_t(data_start) + std::uint64_t(cluster - 2) * sectors_per_cluster) * kSectorBytes;
    }
    std::uint32_t cluster_of(std::uint64_t offset) const {
        return 2 + std::uint32_t((offset / kSectorBytes - data_start) / sectors_per_cluster);
    }

    bool load_fat(std::string& error) {
        fat.assign(std::uint64_t(fat_sectors) * kSectorBytes / 4, 0);
        std::vector<std::uint8_t> block(kSectorBytes);
        for (std::uint32_t sector = 0; sector < fat_sectors; ++sector) {
            const std::uint64_t at = std::uint64_t(reserved + sector) * kSectorBytes;
            if (!image->read(at, block.data(), block.size())) {
                error = "could not read the package disk's file table";
                return false;
            }
            for (int i = 0; i < 128; ++i) {
                const std::size_t index = std::size_t(sector) * 128 + i;
                if (index < fat.size()) {
                    fat[index] = le32(block.data() + i * 4) & kFatMask;
                }
            }
        }
        fat_dirty.assign(fat_sectors, 0);
        return true;
    }

    void mark_fat_dirty(std::uint32_t cluster) {
        const std::size_t entry = cluster;
        const std::size_t per_sector = kSectorBytes / 4;
        const std::size_t sector = entry / per_sector;
        if (sector < fat_dirty.size()) {
            fat_dirty[sector] = 1;
        }
    }

    bool write_fat(std::string& error) {
        std::vector<std::uint8_t> block(kSectorBytes);
        for (std::uint32_t sector = 0; sector < fat_sectors; ++sector) {
            if (!fat_dirty[sector]) {
                continue;
            }
            std::memset(block.data(), 0, block.size());
            for (int i = 0; i < 128; ++i) {
                const std::size_t index = std::size_t(sector) * 128 + i;
                if (index < fat.size()) {
                    put_le32(block.data() + i * 4, fat[index]);
                }
            }
            for (std::uint32_t copy = 0; copy < fats; ++copy) {
                const std::uint64_t at = std::uint64_t(reserved + copy * fat_sectors + sector) * kSectorBytes;
                if (!image->write(at, block.data(), block.size())) {
                    error = "could not write the package disk's file table";
                    return false;
                }
            }
            fat_dirty[sector] = 0;
        }
        return true;
    }

    std::uint32_t next_cluster(std::uint32_t cluster) const {
        if (cluster >= fat.size()) {
            return kFatEndOfChain;
        }
        return fat[cluster];
    }

    bool walk_chain(std::uint32_t first, std::vector<std::uint32_t>& out, std::string& error) const {
        out.clear();
        std::uint32_t cluster = first;
        std::uint32_t guard = 0;
        while (cluster >= 2 && cluster < max_cluster) {
            out.push_back(cluster);
            if (out.size() > std::size_t(cluster_count)) {
                error = "the package disk has a loop in a file";
                return false;
            }
            cluster = next_cluster(cluster);
            if (cluster >= kFatEndOfChain) {
                return true;
            }
            if (++guard > cluster_count) {
                error = "the package disk has a loop in a file";
                return false;
            }
        }
        // A chain that runs off the end of the image is treated as its end:
        // the pack's own file table has been seen to claim a larger volume
        // than the file holds.
        return true;
    }

    // One directory entry by name. Case-insensitive, long names included.
    bool find_in_directory(std::uint32_t directory, const std::string& name, Dirent& out,
                           std::string& error) const {
        std::vector<std::uint32_t> chain;
        if (!walk_chain(directory, chain, error)) {
            return false;
        }
        std::string long_name;
        for (std::uint32_t cluster : chain) {
            const std::uint64_t base = cluster_offset(cluster);
            const std::uint32_t block_bytes = std::uint32_t(cluster_bytes());
            std::vector<std::uint8_t> block(block_bytes);
            if (!image->read(base, block.data(), block.size())) {
                error = "could not read a package folder";
                return false;
            }
            for (std::size_t at = 0; at + 32 <= block.size(); at += 32) {
                const std::uint8_t* entry = block.data() + at;
                if (entry[0] == 0x00) {
                    return true;  // end of this folder
                }
                if (entry[0] == 0xE5) {
                    long_name.clear();
                    continue;
                }
                const std::uint8_t attributes = entry[11];
                if (attributes == 0x0F) {
                    // A long-name piece: 13 UTF-16 units, in order 1,3,5.
                    std::uint16_t units[13];
                    const std::size_t offsets[3] = {1, 14, 28};
                    const int counts[3] = {5, 6, 2};
                    std::size_t unit = 0;
                    for (std::size_t part = 0; part < 3; ++part) {
                        for (int i = 0; i < counts[part] && unit < 13; ++i, ++unit) {
                            units[unit] = le16(entry + offsets[part] + i * 2);
                        }
                    }
                    for (int i = 12; i >= 0; --i) {
                        if (units[i] == 0xFFFF || units[i] == 0x0000) {
                            continue;
                        }
                        long_name.insert(long_name.begin(), char(units[i] & 0xFF));
                    }
                    continue;
                }
                if (attributes & 0x08) {
                    long_name.clear();  // volume label
                    continue;
                }
                std::string short_name;
                for (int i = 0; i < 8 && entry[i] != ' '; ++i) {
                    short_name.push_back(char(entry[i]));
                }
                if (entry[8] != ' ') {
                    short_name.push_back('.');
                    for (int i = 8; i < 11 && entry[i] != ' '; ++i) {
                        short_name.push_back(char(entry[i]));
                    }
                }
                const bool matches = same_name(name, long_name) || same_name(name, short_name);
                if (matches) {
                    out.found = true;
                    out.offset = base + at;
                    out.cluster = (std::uint32_t(le16(entry + 0x14)) << 16) | le16(entry + 0x1A);
                    out.size = le32(entry + 0x1C);
                    out.directory = (attributes & 0x10) != 0;
                    out.write_time = le16(entry + 0x16);
                    out.write_date = le16(entry + 0x18);
                    return true;
                }
                long_name.clear();
            }
        }
        return true;
    }

    bool find(const std::string& path, Dirent& out, std::string& error) const {
        if (path.empty() || path[0] != '/') {
            error = "a package path must be absolute";
            return false;
        }
        std::uint32_t directory = root_cluster;
        std::size_t at = 1;
        while (at <= path.size()) {
            std::size_t end = path.find('/', at);
            if (end == std::string::npos) {
                end = path.size();
            }
            const std::string part = path.substr(at, end - at);
            if (part.empty()) {
                error = "a package path has an empty part";
                return false;
            }
            Dirent found;
            if (!find_in_directory(directory, part, found, error)) {
                return false;
            }
            if (!found.found) {
                error = "the package does not hold " + path;
                return false;
            }
            if (end == path.size()) {
                out = found;
                return true;
            }
            if (!found.directory) {
                error = path + " is not a folder";
                return false;
            }
            directory = found.cluster;
            at = end + 1;
        }
        error = "the package does not hold " + path;
        return false;
    }

    std::uint32_t allocate(std::uint32_t count, std::string& error) {
        if (count == 0) {
            return 0;
        }
        std::uint32_t found = 0;
        std::uint32_t first = 0;
        std::uint32_t previous = 0;
        std::uint32_t cluster = next_free_hint;
        while (found < count) {
            if (cluster >= max_cluster) {
                cluster = 2;
            }
            if (cluster < 2) {
                cluster = 2;
            }
            bool wrapped = false;
            while (cluster < max_cluster && fat[cluster] != 0) {
                ++cluster;
                if (cluster >= max_cluster && !wrapped) {
                    cluster = 2;
                    wrapped = true;
                }
            }
            if (cluster >= max_cluster || cluster >= fat.size()) {
                error = "the package disk is full";
                return 0;
            }
            fat[cluster] = kFatEndOfChain;
            mark_fat_dirty(cluster);
            if (previous != 0) {
                fat[previous] = cluster;
                mark_fat_dirty(previous);
            } else {
                first = cluster;
            }
            previous = cluster;
            ++found;
            next_free_hint = cluster + 1;
            ++cluster;
        }
        return first;
    }

    void release(std::uint32_t first) {
        std::uint32_t cluster = first;
        std::uint32_t guard = 0;
        while (cluster >= 2 && cluster < max_cluster && guard++ <= cluster_count) {
            const std::uint32_t next = next_cluster(cluster);
            fat[cluster] = 0;
            mark_fat_dirty(cluster);
            if (next >= kFatEndOfChain || next < 2) {
                break;
            }
            cluster = next;
        }
    }

    bool read_chain(std::uint32_t first, std::uint32_t size, std::vector<std::uint8_t>& out,
                    std::string& error) const {
        out.clear();
        if (size == 0) {
            return true;
        }
        out.reserve(size);
        std::uint32_t cluster = first;
        std::uint32_t guard = 0;
        while (out.size() < size && cluster >= 2 && cluster < max_cluster) {
            const std::uint64_t base = cluster_offset(cluster);
            const std::size_t take = std::min<std::size_t>(cluster_bytes(), size - out.size());
            const std::size_t old = out.size();
            out.resize(old + take);
            if (!image->read(base, out.data() + old, take)) {
                error = "could not read a package file";
                return false;
            }
            cluster = next_cluster(cluster);
            if (++guard > cluster_count) {
                error = "the package disk has a loop in a file";
                return false;
            }
        }
        if (out.size() < size) {
            error = "a package file runs past the end of the disk";
            return false;
        }
        return true;
    }
};

CtgpPackWriter::CtgpPackWriter() : impl_(new Impl()) {}

CtgpPackWriter::~CtgpPackWriter() {
    close();
    delete impl_;
}

bool CtgpPackWriter::open(const CtgpPackIo& io, std::uint64_t size, std::string& error) {
    return open(io, size, ctgp_blob_key(), ctgp_blob_chunk_iv(), error);
}

bool CtgpPackWriter::open(const CtgpPackIo& io, std::uint64_t size, const std::array<std::uint8_t, 16>& key,
                          const CtgpChunkIv& iv, std::string& error) {
    close();
    error.clear();
    if (!io.read || !io.write || size < kSectorBytes * 4 || size % kSectorBytes != 0) {
        error = "the package disk is not a whole number of sectors";
        return false;
    }
    auto image = std::make_unique<PackImage>(io, size, key, iv);
    std::vector<std::uint8_t> boot(kSectorBytes);
    if (!image->read(0, boot.data(), boot.size())) {
        error = "could not read the package disk";
        return false;
    }
    const std::uint16_t magic = le16(boot.data() + 0x1FE);
    if (magic != 0xAA55) {
        error = "the package disk has no boot sector";
        return false;
    }
    Impl& d = *impl_;
    d.image = image.release();
    d.io = io;
    d.size = size;
    d.bytes_per_sector = le16(boot.data() + 0x0B);
    d.sectors_per_cluster = boot[0x0D];
    d.reserved = le16(boot.data() + 0x0E);
    d.fats = boot[0x10];
    d.fat_sectors = le32(boot.data() + 0x24);
    d.root_cluster = le32(boot.data() + 0x2C);
    d.total_sectors = le32(boot.data() + 0x20);
    d.fsinfo_sector = le16(boot.data() + 0x30);
    if (d.bytes_per_sector != kSectorBytes || d.sectors_per_cluster == 0 || d.fats == 0 ||
        d.fat_sectors == 0 || d.root_cluster < 2) {
        error = "the package disk is not a FAT32 volume RiftWii can write";
        close();
        return false;
    }
    d.data_start = d.reserved + d.fats * d.fat_sectors;
    if (d.total_sectors <= d.data_start) {
        error = "the package disk has no data area";
        close();
        return false;
    }
    d.cluster_count = (d.total_sectors - d.data_start) / d.sectors_per_cluster;
    // The pack's header may claim a larger volume than the file on the card
    // holds; never allocate a cluster whose bytes are not in the file.
    const std::uint64_t file_clusters = (size / kSectorBytes - d.data_start) / d.sectors_per_cluster;
    d.max_cluster = std::uint32_t(std::min<std::uint64_t>(std::uint64_t(d.cluster_count) + 2,
                                                         std::min<std::uint64_t>(file_clusters + 2, 0x0FFFFFF5ull)));
    if (!d.load_fat(error)) {
        close();
        return false;
    }
    return true;
}

void CtgpPackWriter::close() {
    if (impl_ == nullptr) {
        return;
    }
    if (impl_->image != nullptr) {
        impl_->image->flush();
        delete impl_->image;
        impl_->image = nullptr;
    }
    impl_->fat.clear();
    impl_->fat_dirty.clear();
}

bool CtgpPackWriter::replace_file(const std::string& path, const std::uint8_t* data, std::size_t size,
                                  std::string& error) {
    return replace_file(
        path, size,
        [data](std::size_t offset, std::uint8_t* out, std::size_t length) {
            std::memcpy(out, data + offset, length);
            return true;
        },
        error);
}

bool CtgpPackWriter::replace_file(const std::string& path, std::size_t size, const Reader& read,
                                  std::string& error) {
    error.clear();
    if (impl_ == nullptr || impl_->image == nullptr) {
        error = "the package disk is not open";
        return false;
    }
    if (size > kCtgpPackMaxFileBytes) {
        error = "the new file is too large for the package disk";
        return false;
    }
    Impl& d = *impl_;
    Dirent entry;
    if (!d.find(path, entry, error)) {
        return false;
    }
    if (entry.directory) {
        error = path + " is a folder";
        return false;
    }

    // The packs run close to full, so the old file's clusters are reused and
    // only a growth asks for new ones. The chain is extended first, then the
    // data is written over it and the entry is switched to the new size.
    std::vector<std::uint32_t> chain;
    if (!d.walk_chain(entry.cluster, chain, error)) {
        return false;
    }
    const std::uint64_t capacity = std::uint64_t(chain.size()) * d.cluster_bytes();
    if (size > capacity) {
        const std::uint64_t missing = size - capacity;
        const std::uint32_t extra = std::uint32_t((missing + d.cluster_bytes() - 1) / d.cluster_bytes());
        const std::uint32_t first_extra = d.allocate(extra, error);
        if (first_extra == 0 && extra != 0) {
            return false;
        }
        std::uint32_t cluster = first_extra;
        if (chain.empty()) {
            chain.push_back(first_extra);
            cluster = d.next_cluster(first_extra);
        } else {
            d.fat[chain.back()] = first_extra;
            d.mark_fat_dirty(chain.back());
        }
        if (!d.write_fat(error)) {
            return false;
        }
        while (chain.size() < std::size_t((size + d.cluster_bytes() - 1) / d.cluster_bytes())) {
            chain.push_back(cluster);
            cluster = d.next_cluster(cluster);
            if (cluster < 2 || cluster >= d.max_cluster) {
                break;
            }
        }
        if (std::uint64_t(chain.size()) * d.cluster_bytes() < size) {
            error = "the new file's cluster chain is too short";
            return false;
        }
    }

    std::vector<std::uint8_t> block(std::size_t(d.cluster_bytes()));
    std::size_t written = 0;
    std::size_t used = 0;
    while (written < size) {
        const std::size_t take = std::min<std::size_t>(block.size(), size - written);
        std::memset(block.data(), 0, block.size());
        if (take != 0 && !read(written, block.data(), take)) {
            error = "could not read the new contents of " + path;
            return false;
        }
        if (!d.image->write(d.cluster_offset(chain[used]), block.data(), block.size())) {
            error = "could not write a package file";
            return false;
        }
        written += take;
        ++used;
    }

    const std::uint32_t first = chain.empty() ? 0 : chain.front();
    std::uint8_t dirent[32];
    if (!d.image->read(entry.offset, dirent, sizeof(dirent))) {
        error = "could not read the file's folder entry";
        return false;
    }
    dirent[0x14] = std::uint8_t(first >> 16);
    dirent[0x15] = std::uint8_t(first >> 24);
    dirent[0x1A] = std::uint8_t(first);
    dirent[0x1B] = std::uint8_t(first >> 8);
    put_le32(dirent + 0x1C, std::uint32_t(size));
    if (!d.image->write(entry.offset, dirent, sizeof(dirent))) {
        error = "could not update the file's folder entry";
        return false;
    }

    // Shrinking frees the clusters the file no longer needs; the last used
    // cluster ends its chain.
    for (std::size_t i = used; i < chain.size(); ++i) {
        d.fat[chain[i]] = 0;
        d.mark_fat_dirty(chain[i]);
    }
    if (used > 0) {
        d.fat[chain[used - 1]] = kFatEndOfChain;
        d.mark_fat_dirty(chain[used - 1]);
    }
    if (!d.write_fat(error)) {
        return false;
    }
    if (!d.image->flush()) {
        error = "could not save the package disk";
        return false;
    }
    return true;
}

bool CtgpPackWriter::read_file(const std::string& path, std::vector<std::uint8_t>& out, std::string& error) {
    error.clear();
    if (impl_ == nullptr || impl_->image == nullptr) {
        error = "the package disk is not open";
        return false;
    }
    Dirent entry;
    if (!impl_->find(path, entry, error)) {
        return false;
    }
    if (entry.directory) {
        error = path + " is a folder";
        return false;
    }
    return impl_->read_chain(entry.cluster, entry.size, out, error);
}

bool CtgpPackWriter::read_file(const std::string& path, const Sink& sink, std::uint32_t* size,
                               std::string& error) {
    error.clear();
    if (impl_ == nullptr || impl_->image == nullptr) {
        error = "the package disk is not open";
        return false;
    }
    Dirent entry;
    if (!impl_->find(path, entry, error)) {
        return false;
    }
    if (entry.directory) {
        error = path + " is a folder";
        return false;
    }
    if (size != nullptr) {
        *size = entry.size;
    }
    const Impl& d = *impl_;
    std::vector<std::uint8_t> block(std::size_t(d.cluster_bytes()));
    std::uint32_t cluster = entry.cluster;
    std::uint32_t done = 0;
    std::uint32_t guard = 0;
    while (done < entry.size && cluster >= 2 && cluster < d.max_cluster) {
        const std::size_t take = std::min<std::size_t>(block.size(), entry.size - done);
        if (!d.image->read(d.cluster_offset(cluster), block.data(), take)) {
            error = "could not read a package file";
            return false;
        }
        if (!sink(block.data(), take)) {
            if (error.empty()) {
                error = "could not use " + path;
            }
            return false;
        }
        done += std::uint32_t(take);
        cluster = d.next_cluster(cluster);
        if (++guard > d.cluster_count) {
            error = "the package disk has a loop in a file";
            return false;
        }
    }
    if (done < entry.size) {
        error = "a package file runs past the end of the disk";
        return false;
    }
    return true;
}

std::uint32_t CtgpPackWriter::free_clusters() const {
    if (impl_ == nullptr) {
        return 0;
    }
    std::uint32_t free = 0;
    for (std::uint32_t cluster = 2; cluster < impl_->max_cluster; ++cluster) {
        if (cluster < impl_->fat.size() && impl_->fat[cluster] == 0) {
            ++free;
        }
    }
    return free;
}

}  // namespace riftwii
