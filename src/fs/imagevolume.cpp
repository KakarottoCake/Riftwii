// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/imagevolume.hpp"

#include <algorithm>
#include <cstring>

#include "riftwii/ntfs.hpp"

namespace riftwii {
namespace {

constexpr std::size_t kMaxGptEntries = 128;

std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint64_t le64(const std::uint8_t* p) { return le32(p) | (static_cast<std::uint64_t>(le32(p + 4)) << 32); }

VolumeEntry from_fat(const Fat32Entry& e) {
    VolumeEntry v;
    v.name = e.name;
    v.is_directory = e.is_directory;
    v.size = e.size;
    return v;
}

class Fat32ImageVolume final : public ImageVolume {
public:
    explicit Fat32ImageVolume(Fat32Volume volume) : volume_(std::move(volume)) {}
    const char* kind() const override { return "FAT32"; }
    bool lookup(const std::string& path, VolumeFile& out, std::string& error) const override {
        Fat32File f;
        if (!volume_.lookup(path, f, error)) return false;
        out.entry = from_fat(f.entry);
        out.fragments = std::move(f.fragments);
        return true;
    }
    bool list(const std::string& path, std::vector<VolumeEntry>& out, std::string& error) const override {
        std::vector<Fat32Entry> entries;
        if (!volume_.list(path, entries, error)) return false;
        out.clear();
        out.reserve(entries.size());
        for (const Fat32Entry& e : entries) out.push_back(from_fat(e));
        return true;
    }
    bool read(const VolumeFile& file, std::uint64_t offset, std::uint8_t* out, std::size_t length) const override {
        if (file.entry.size > 0xFFFFFFFFull) return false;
        Fat32File f;
        f.entry.size = static_cast<std::uint32_t>(file.entry.size);
        f.fragments = file.fragments;
        return volume_.read(f, offset, out, length);
    }

private:
    Fat32Volume volume_;
};

// Mounts whichever file system has its boot sector at `lba`.
bool mount_candidate(const BlockReader& reader, std::uint64_t lba, std::unique_ptr<ImageVolume>& out,
                     std::string& error) {
    std::uint8_t block[512];
    if (!reader(lba, 1, block)) {
        error = "cannot read block " + std::to_string(lba);
        return false;
    }
    if (NtfsVolume::is_boot_sector(block)) {
        auto v = std::make_unique<NtfsVolume>();
        if (!NtfsVolume::mount_at(reader, lba, *v, error)) {
            error = "NTFS at block " + std::to_string(lba) + ": " + error;
            return false;
        }
        out = std::move(v);
        return true;
    }
    Fat32Volume fat;
    if (!Fat32Volume::mount_at(reader, lba, fat, error)) {
        error = "block " + std::to_string(lba) + ": " + error;
        return false;
    }
    out = std::make_unique<Fat32ImageVolume>(std::move(fat));
    return true;
}

// The logical partitions of an MBR extended partition, in chain order.
void logical_partitions(const BlockReader& reader, std::uint64_t extended, std::vector<DrivePartition>& out) {
    std::uint64_t ebr = extended;
    for (int n = 0; n < 32; ++n) {
        std::uint8_t b[512];
        if (!reader(ebr, 1, b) || b[510] != 0x55 || b[511] != 0xAA) return;
        const std::uint8_t* first = b + 0x1BE;
        const std::uint8_t* next = first + 16;
        if (first[4] != 0 && le32(first + 8) != 0) out.push_back({ebr + le32(first + 8), le32(first + 12)});
        if ((next[4] != 0x05 && next[4] != 0x0F) || le32(next + 8) == 0) return;
        ebr = extended + le32(next + 8);  // relative to the extended partition
    }
}

int ascii_lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

bool same_name(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (ascii_lower(static_cast<unsigned char>(a[i])) != ascii_lower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

// How well a volume's top folder matches `wanted`: the first name counts
// most. Leading slashes and anything past the first folder are ignored.
std::size_t score_volume(const ImageVolume& volume, const std::vector<std::string>& wanted) {
    std::vector<VolumeEntry> top;
    std::string e;
    if (!volume.list("/", top, e)) return 0;
    std::size_t score = 0;
    for (std::size_t i = 0; i < wanted.size(); ++i) {
        std::string name = wanted[i];
        while (!name.empty() && name[0] == '/') name.erase(0, 1);
        name = name.substr(0, name.find('/'));
        if (name.empty()) continue;
        for (const VolumeEntry& entry : top) {
            if (entry.is_directory && same_name(entry.name, name)) {
                score += wanted.size() - i;
                break;
            }
        }
    }
    return score;
}

// The partitions a GPT lists, in table order.
void gpt_partitions(const BlockReader& reader, std::vector<DrivePartition>& out) {
    std::uint8_t header[512];
    if (!reader(1, 1, header) || std::memcmp(header, "EFI PART", 8) != 0) return;
    const std::uint64_t table = le64(header + 0x48);
    const std::uint32_t count = le32(header + 0x50);
    const std::uint32_t size = le32(header + 0x54);
    if (size < 128 || size > 512 || (512 % size) != 0 || table < 2) return;
    const std::size_t n = std::min<std::size_t>(count, kMaxGptEntries);
    const std::size_t per_block = 512 / size;
    std::uint8_t block[512];
    for (std::size_t i = 0; i < n; ++i) {
        if (i % per_block == 0 && !reader(table + i / per_block, 1, block)) return;
        const std::uint8_t* e = block + (i % per_block) * size;
        bool used = false;
        for (int k = 0; k < 16; ++k) used = used || e[k] != 0;  // type GUID zero: unused
        const std::uint64_t first = le64(e + 0x20);
        const std::uint64_t last = le64(e + 0x28);
        // An entry ending before it starts is damaged: listed with no size
        // it read as "size unknown", and a WBFS header's sector count went
        // unchecked against it (MIN-05).
        if (used && first != 0 && last >= first) out.push_back({first, last - first + 1});
    }
}

}  // namespace

std::vector<DrivePartition> drive_partitions(const BlockReader& reader, const std::uint8_t* mbr) {
    std::vector<DrivePartition> out;
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) return out;
    bool protective = false;
    for (int i = 0; i < 4; ++i) {
        const std::uint8_t* pe = mbr + 0x1BE + i * 16;
        const std::uint8_t type = pe[4];
        const std::uint32_t lba = le32(pe + 8);
        if (type == 0xEE) protective = true;
        if ((type == 0x05 || type == 0x0F) && lba != 0) logical_partitions(reader, lba, out);
        if (type == 0 || type == 0xEE || type == 0x05 || type == 0x0F || lba == 0) continue;
        out.push_back({lba, le32(pe + 12)});
    }
    if (protective) gpt_partitions(reader, out);
    return out;
}

const std::vector<std::string>& default_wanted_folders() {
    static const std::vector<std::string> names = {"wbfs", "games", "riivolution"};
    return names;
}

DriveBlock0 classify_drive_block0(const std::uint8_t* block0) {
    if (block0[510] == 0x55 && block0[511] == 0xAA) return DriveBlock0::Readable;
    if (std::memcmp(block0, "WBFS", 4) == 0) return DriveBlock0::Wbfs;
    // Encrypted bytes use most of the 256 values (about 220 in 512 random
    // bytes) and never repeat one for long. Any format with a structure in
    // block 0 (a boot sector, a header, padding) repeats zeros or a filler
    // byte, and uses far fewer values.
    bool seen[256] = {};
    unsigned distinct = 0;
    unsigned run = 1;
    unsigned longest_run = 1;
    bool all_zero = block0[0] == 0;
    for (int i = 0; i < 512; ++i) {
        const std::uint8_t b = block0[i];
        if (!seen[b]) {
            seen[b] = true;
            ++distinct;
        }
        if (b != 0) all_zero = false;
        if (i > 0) {
            run = b == block0[i - 1] ? run + 1 : 1;
            longest_run = std::max(longest_run, run);
        }
    }
    if (all_zero) return DriveBlock0::Empty;
    if (distinct >= 160 && longest_run <= 6) return DriveBlock0::WiiU;
    return DriveBlock0::Unknown;
}

bool mount_image_volume(BlockReader reader, std::unique_ptr<ImageVolume>& out, std::string& error) {
    return mount_image_volume(std::move(reader), out, error, default_wanted_folders());
}

// What a partition that is neither FAT32 nor NTFS holds, for the notes:
// a WBFS partition (USB Loader GX's own format) is worth naming, as games
// on it are not read while a FAT32 or NTFS partition is in use.
std::string other_partition_kind(const BlockReader& reader, std::uint64_t lba) {
    std::uint8_t block[512];
    if (!reader(lba, 1, block)) return "unreadable";
    const bool wbfs = std::memcmp(block, "WBFS", 4) == 0;
    return wbfs ? "WBFS" : "not FAT32 or NTFS";
}

bool mount_image_volume(BlockReader reader, std::unique_ptr<ImageVolume>& out, std::string& error,
                        const std::vector<std::string>& wanted, std::vector<std::string>* notes) {
    out.reset();
    if (!reader) {
        error = "no block reader";
        return false;
    }
    std::uint8_t mbr[512];
    if (!reader(0, 1, mbr)) {
        error = "cannot read block 0";
        return false;
    }
    std::string tried;
    std::string e;
    if (mount_candidate(reader, 0, out, e)) {
        error.clear();
        return true;
    }
    tried = e;
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        error = "no FAT32 or NTFS volume (" + tried + ")";
        return false;
    }
    // Every partition that mounts is scored; one volume is kept at a time.
    std::size_t best_score = 0;
    std::uint64_t best_lba = 0;
    for (const DrivePartition& p : drive_partitions(reader, mbr)) {
        const std::string where = "partition at block " + std::to_string(p.lba);
        std::unique_ptr<ImageVolume> candidate;
        if (!mount_candidate(reader, p.lba, candidate, e)) {
            tried += "; " + e;
            if (notes) notes->push_back(where + ": " + other_partition_kind(reader, p.lba));
            continue;
        }
        const std::size_t score = score_volume(*candidate, wanted);
        if (notes) {
            notes->push_back(where + ": " + candidate->kind() + ", " + std::to_string(score) +
                             " point(s) for its top folders");
        }
        if (!out || score > best_score) {
            out = std::move(candidate);
            best_score = score;
            best_lba = p.lba;
        }
    }
    if (out && notes) notes->push_back("using the partition at block " + std::to_string(best_lba));
    if (out) {
        error.clear();
        return true;
    }
    error = "no FAT32 or NTFS partition found (" + tried + ")";
    return false;
}

}  // namespace riftwii
