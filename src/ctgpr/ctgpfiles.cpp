// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgpfiles.hpp"

#include <algorithm>
#include <cstring>

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

void put32(std::vector<std::uint8_t>& v, std::size_t at, std::uint32_t x) {
    v[at] = static_cast<std::uint8_t>(x >> 24);
    v[at + 1] = static_cast<std::uint8_t>(x >> 16);
    v[at + 2] = static_cast<std::uint8_t>(x >> 8);
    v[at + 3] = static_cast<std::uint8_t>(x);
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
    : image_(static_cast<std::size_t>(kCtgpRecordCount) * kCtgpRecordSize, 0), next_free_(kCtgpPoolCount, 0) {}

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
    const auto known = entries_.find(key);
    if (known != entries_.end()) {
        slot = known->second;
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
    Fst fst;
    // File entries hold a string position and flags, not a disc extent: read
    // them as plain numbers.
    if (!Fst::parse(data, size, false, fst, error)) return false;
    std::vector<CtgpPackageFile> files;
    for (std::uint32_t i = 1; i < fst.count(); ++i) {
        const FstEntry& e = fst.entries()[i];
        if (e.is_directory) continue;
        if (e.offset >= size) {
            error = "package list entry " + std::to_string(i) + ": the source path is outside the file";
            return false;
        }
        const char* s = reinterpret_cast<const char*>(data) + e.offset;
        const std::size_t room = size - static_cast<std::size_t>(e.offset);
        const std::size_t len = ::strnlen(s, room);
        if (len == room || len == 0) {
            error = "package list entry " + std::to_string(i) + ": the source path is not terminated";
            return false;
        }
        CtgpPackageFile f;
        if (!fst.path_of(i, f.game_path)) {
            error = "package list entry " + std::to_string(i) + ": no path";
            return false;
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
                      std::vector<CtgpMergedFile>& out, std::string& error) {
    std::vector<CtgpMergedFile> merged;
    for (const CtgpPackageFile& f : files) {
        const bool required = (f.flags & 1) != 0;
        const auto colon = f.source.find(':');
        const std::string device = colon == std::string::npos ? std::string() : f.source.substr(0, colon);
        const std::string path = colon == std::string::npos ? f.source : f.source.substr(colon + 1);
        CtgpSourceFile src;
        CtgpRecordTable::Slot slot;
        const int dev = ctgp_device_index(device);
        bool ok = dev >= 0 && resolver(device, path, src);
        if (ok) {
            CtgpOpenFile open;
            open.size = src.size;
            open.first_cluster = src.first_cluster;
            open.hashed = (f.flags & 2) != 0;
            // A file that already has a record is shared without a second look,
            // as in the channel; a new signed file needs its digest from the list.
            if (open.hashed && !table.has(f.source) && !(hashes && hashes->find(f.game_path, open.digest))) {
                error = "the package lists " + f.game_path + " as signed, but its hash list does not have it";
                return false;
            }
            ok = table.add(f.source, dev, open, slot);
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

bool ctgp_merge_files(Fst& fst, const std::vector<CtgpMergedFile>& files, std::string& error) {
    std::vector<FstNewFile> fresh;
    for (const CtgpMergedFile& f : files) {
        // The handle goes into the table's offset field as it is; the table
        // keeps offsets shifted right by two.
        const std::uint64_t offset = static_cast<std::uint64_t>(f.handle) << 2;
        std::uint32_t index = fst.find(f.game_path, false);
        if (index == Fst::npos) index = fst.find(f.game_path, true);
        if (index != Fst::npos) {
            if (fst.entries()[index].is_directory) {
                error = f.game_path + " is a directory in the game";
                return false;
            }
            if (!fst.set_file_extent(index, offset, f.size, error)) return false;
        } else {
            // A later package may name the same new file again: the last one wins.
            bool again = false;
            for (FstNewFile& n : fresh) {
                if (n.path == f.game_path) {
                    n.offset = offset;
                    n.size = f.size;
                    again = true;
                }
            }
            if (!again) fresh.push_back({f.game_path, offset, f.size});
        }
    }
    if (fresh.empty()) return true;
    std::vector<std::uint32_t> indices;
    return fst.create_files(fresh, indices, error);
}

}  // namespace riftwii
