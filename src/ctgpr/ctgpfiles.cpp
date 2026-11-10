// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgpfiles.hpp"

#include <algorithm>
#include <cstring>

#include "riftwii/checkedalloc.hpp"

namespace riftwii {

const CtgpRecordPool kCtgpRecordPools[kCtgpPoolCount] = {
    {0x81628CB8, 1024, 0x00020000, 0x82000000, 0x00008000},
    {0x81661180, 1024, 0x00040000, 0x8C000000, 0x00010000},
    {0x816228B8, 128, 0x00100000, 0x80000000, 0x00040000},
    {0x8165AD80, 128, 0x00200000, 0x88000000, 0x00080000},
    {0x816969C0, 256, 0x00400000, 0xB0000000, 0x00100000},
    {0x816A4AC0, 256, 0x00800000, 0xE0000000, 0x00200000},
    {0x816937C0, 64, 0x01000000, 0xA0000000, 0x00400000},
    {0x816A31C0, 32, 0x04000000, 0xC0000000, 0x01000000},
    {0x81693180, 8, 0x08000000, 0x90000000, 0x02000000},
    {0x8165ACB8, 1, 0x10000000, 0x84000000, 0x04000000},
};

namespace {

template <typename Bytes>
void put32(Bytes& v, std::size_t at, std::uint32_t x) {
    v[at] = static_cast<std::uint8_t>(x >> 24);
    v[at + 1] = static_cast<std::uint8_t>(x >> 16);
    v[at + 2] = static_cast<std::uint8_t>(x >> 8);
    v[at + 3] = static_cast<std::uint8_t>(x);
}

// FAT names are case-insensitive. Refuse alternate separators and parent
// components before applying the one unsigned-file exception.
bool my_stuff_source(const std::string& source) {
    std::string folded = source;
    for (char& c : folded) {
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    }
    const std::string prefix = "sda:/ctgpr/my stuff/";
    if (folded.compare(0, prefix.size(), prefix) != 0 || folded.size() == prefix.size() ||
        folded.find('\\') != std::string::npos || folded.find(':', 4) != std::string::npos)
        return false;
    std::size_t start = 4;
    while (start < folded.size()) {
        const std::size_t end = folded.find('/', start);
        if (folded.substr(start, end == std::string::npos ? end : end - start) == "..") return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

}  // namespace

int ctgp_device_index(const std::string& device) {
    if (device == "sda") return 0;
    if (device == "efa") return 1;
    if (device == "efb") return 2;
    return -1;
}

std::vector<std::uint8_t> ctgp_build_record(const CtgpOpenFile& file, std::uint32_t partition,
                                            std::uint32_t prev, std::uint32_t next) {
    std::vector<std::uint8_t> r(kCtgpRecordSize, 0);
    put32(r, 0x00, file.size);
    put32(r, 0x04, file.first_cluster);   // start cluster
    put32(r, 0x0C, file.first_cluster);   // read position: the start of the chain
    put32(r, 0x18, file.first_cluster);   // append position
    put32(r, 0x3C, partition);
    put32(r, 0x40, prev);
    put32(r, 0x44, next);
    r[0x48] = 1;                          // read
    r[0x4B] = 1;                          // in use
    if (file.hashed) {
        r[0x4E] = 1;
        std::copy(file.digest.begin(), file.digest.end(), r.begin() + 0xB4);
        // The SHA-1 state the channel starts such a record with: 0x60 bytes
        // zeroed, then the five standard initial words, and a zero count.
        put32(r, 0x50, 0x67452301u);
        put32(r, 0x54, 0xEFCDAB89u);
        put32(r, 0x58, 0x98BADCFEu);
        put32(r, 0x5C, 0x10325476u);
        put32(r, 0x60, 0xC3D2E1F0u);
    }
    return r;
}

CtgpRecordTable::CtgpRecordTable()
    : next_free_(kCtgpPoolCount, 0) {
    // A failed checked request leaves an empty image for the caller to refuse.
    try_assign(image_, static_cast<std::size_t>(kCtgpRecordCount) * kCtgpRecordSize, std::uint8_t(0));
}

std::uint32_t CtgpRecordTable::open_count(int device) const {
    return device >= 0 && device < 3 ? count_[device] : 0;
}
std::uint32_t CtgpRecordTable::first_open(int device) const {
    return device >= 0 && device < 3 ? head_[device] : 0;
}

bool CtgpRecordTable::has(const std::string& key) const {
    return entries_.find(key) != entries_.end();
}

bool CtgpRecordTable::add(const std::string& key, int device, const CtgpOpenFile& file, Slot& slot) {
    if (image_.empty()) return false;
    const auto known = entries_.find(key);
    if (known != entries_.end()) {
        slot = known->second;
        const std::size_t record = slot.address - kCtgpRecordBlock;
        // Shared sources must carry the same authenticated bytes, even when
        // different package paths refer to them.
        if (image_[record + 0x4E] != static_cast<unsigned>(file.hashed) ||
            (file.hashed && std::memcmp(image_.data() + record + 0xB4, file.digest.data(), 20) != 0))
            return false;
        return true;
    }
    if (device < 0 || device > 2) return false;
    std::size_t pool = 0;
    while (pool < kCtgpPoolCount - 1 && file.size >= kCtgpRecordPools[pool].size_below) ++pool;
    if (file.size >= kCtgpRecordPools[kCtgpPoolCount - 1].size_below) return false;
    for (; pool < kCtgpPoolCount; ++pool) {
        if (next_free_[pool] < kCtgpRecordPools[pool].count) break;
    }
    if (pool == kCtgpPoolCount) return false;
    const CtgpRecordPool& p = kCtgpRecordPools[pool];
    const std::uint32_t index = next_free_[pool]++;
    slot.address = p.start + index * kCtgpRecordSize;
    slot.handle = p.handle_base + index * p.handle_stride;
    slot.size = file.size;

    // The new file becomes the head of its partition's list of open files.
    const std::uint32_t old_head = head_[device];
    const std::vector<std::uint8_t> r = ctgp_build_record(file, ctgp_partition_address(device), 0, old_head);
    std::copy(r.begin(), r.end(), image_.begin() + (slot.address - kCtgpRecordBlock));
    if (old_head != 0) put32(image_, old_head - kCtgpRecordBlock + 0x40, slot.address);
    head_[device] = slot.address;
    ++count_[device];
    ++used_;
    entries_.emplace(key, slot);
    return true;
}

bool ctgp_parse_package_fst(const std::uint8_t* data, std::size_t size, std::vector<CtgpPackageFile>& out,
                            std::string& error) {
    // Reserve room for FST's small names too, before it starts copying them.
    // Fst::parse checks each name's 255-byte cap and the entry array allocation.
    if (data == nullptr || size < 12) {
        error = "the package list is too short";
        return false;
    }
    const std::size_t entries = (std::size_t(data[8]) << 24) | (std::size_t(data[9]) << 16) |
                                (std::size_t(data[10]) << 8) | data[11];
    if (entries > kCtgpPackageMaxEntries || entries * 12 > size) {
        error = "the package list exceeds its entry limit";
        return false;
    }
    if (!have_room(entries * (sizeof(FstEntry) + 256))) {
        error = "no room for the package tree";
        return false;
    }
    Fst fst;
    // File entries hold a string position and flags, not a disc extent: read
    // them as plain numbers.
    FstLimits limits;
    limits.max_entries = kCtgpPackageMaxEntries;
    limits.max_depth = 32;
    if (!Fst::parse(data, size, false, fst, error, limits)) return false;
    std::vector<CtgpPackageFile> files;
    if (!try_reserve(files, std::min<std::size_t>(fst.count(), kCtgpPackageMaxFiles))) {
        error = "no room for the package list";
        return false;
    }
    std::size_t copied = 0;
    for (std::uint32_t i = 1; i < fst.count(); ++i) {
        const FstEntry& e = fst.entries()[i];
        if (e.is_directory) continue;
        if (e.offset >= size) {
            error = "package list entry " + std::to_string(i) + ": the source path is outside the file";
            return false;
        }
        const char* s = reinterpret_cast<const char*>(data) + e.offset;
        const std::size_t room = std::min(size - static_cast<std::size_t>(e.offset), kCtgpPackageMaxPath + 1);
        const std::size_t len = ::strnlen(s, room);
        if (len == room || len == 0) {
            error = "package list entry " + std::to_string(i) + ": the source path is empty, too long or not terminated";
            return false;
        }
        std::size_t path_length = 0;
        for (std::uint32_t at = i; at != 0; at = fst.entries()[at].parent)
            path_length += fst.entries()[at].name.size() + 1;
        const std::size_t bytes = path_length + len + 2;
        if (path_length > kCtgpPackageMaxPath || files.size() >= kCtgpPackageMaxFiles ||
            bytes > kCtgpPackageMaxCopiedBytes - copied) {
            error = "the package list exceeds its file or copied-path limit";
            return false;
        }
        copied += bytes;
        CtgpPackageFile f;
        if (!try_reserve(f.game_path, path_length) || !try_reserve(f.source, len)) {
            error = "no room for the package paths";
            return false;
        }
        // Fill backwards, avoiding unchecked intermediate path copies.
        f.game_path.resize(path_length);
        std::size_t end = path_length;
        for (std::uint32_t at = i; at != 0; at = fst.entries()[at].parent) {
            const std::string& name = fst.entries()[at].name;
            end -= name.size();
            f.game_path.replace(end, name.size(), name);
            f.game_path[--end] = '/';
        }
        f.source.assign(s, len);
        f.flags = e.size;
        files.push_back(std::move(f));
    }
    out = std::move(files);
    return true;
}

bool ctgp_add_package(CtgpRecordTable& table, const std::vector<CtgpPackageFile>& files,
                      const CtgpSourceResolver& resolver, const CtgpHashList* hashes,
                      std::vector<CtgpMergedFile>& out, std::string& error, bool allow_unsigned) {
    if (files.size() > kCtgpPackageMaxFiles) {
        error = "the package list exceeds its file limit";
        return false;
    }
    std::size_t copied = 0;
    for (const CtgpPackageFile& f : files) {
        const std::size_t bytes = f.game_path.size() + f.source.size() + 2;
        if (f.game_path.size() > kCtgpPackageMaxPath || f.source.size() > kCtgpPackageMaxPath ||
            bytes > kCtgpPackageMaxCopiedBytes - copied) {
            error = "the package list exceeds its copied-path limit";
            return false;
        }
        copied += bytes;
    }
    std::vector<CtgpMergedFile> merged;
    // Copies into the name set, record map and merged paths are bounded too.
    if (!have_room(copied * 4 + files.size() * 256) || !try_reserve(merged, files.size()) ||
        !try_reserve(out, out.size() + files.size())) {
        error = "no room to attach the package list";
        return false;
    }
    // A package may name a game path more than once (CTGP-R 1.03's own My
    // Stuff list repeats /Scene/UI/Award_E.szs). Each entry passes its own
    // digest check below, and ctgp_merge_files lets the last one win, as the
    // build that raced on a real Wii did. No entry can hide a signed one: only
    // My Stuff's files under sda:/ctgpr/my stuff/ are ever unhashed.
    for (const CtgpPackageFile& f : files) {
        const bool required = (f.flags & 1) != 0;
        const auto colon = f.source.find(':');
        std::string device = colon == std::string::npos ? std::string() : f.source.substr(0, colon);
        for (char& c : device) {
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        }
        const std::string path = colon == std::string::npos ? f.source : f.source.substr(colon + 1);
        CtgpSourceFile src;
        CtgpRecordTable::Slot slot;
        const int dev = ctgp_device_index(device);
        bool ok = dev >= 0 && resolver(device, path, src);
        if (ok) {
            CtgpOpenFile open;
            open.size = src.size;
            open.first_cluster = src.first_cluster;
            // Only MYSTUFF's files inside the channel's SD directory may
            // opt out. A modified list cannot extend that exception elsewhere.
            open.hashed = !(allow_unsigned && my_stuff_source(f.source) && (f.flags & 2) == 0);
            if (open.hashed && !(hashes && hashes->find(f.game_path, open.digest))) {
                error = "the package file " + f.game_path + " has no signed digest";
                return false;
            }
            const bool shared = table.has(f.source);
            ok = table.add(f.source, dev, open, slot);
            if (!ok && shared) {
                error = "the package reuses " + f.source + " with a different signed digest";
                return false;
            }
        }
        if (!ok) {
            if (required) {
                error = "the package needs " + f.source + " (" + (dev < 0 ? "unknown device" : "missing or no room") + ")";
                return false;
            }
            continue;
        }
        merged.push_back({f.game_path, slot.handle, slot.size});
    }
    out.insert(out.end(), merged.begin(), merged.end());
    return true;
}

}  // namespace riftwii
