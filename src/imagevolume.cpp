// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/imagevolume.hpp"

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

// The partitions a GPT lists, in table order.
void gpt_partitions(const BlockReader& reader, std::vector<std::uint64_t>& out) {
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
        if (used && first != 0) out.push_back(first);
    }
}

}  // namespace

bool mount_image_volume(BlockReader reader, std::unique_ptr<ImageVolume>& out, std::string& error) {
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
    std::vector<std::uint64_t> starts;
    bool protective = false;
    for (int i = 0; i < 4; ++i) {
        const std::uint8_t* pe = mbr + 0x1BE + i * 16;
        const std::uint8_t type = pe[4];
        const std::uint32_t lba = le32(pe + 8);
        if (type == 0xEE) protective = true;
        if (type == 0 || type == 0xEE || type == 0x05 || type == 0x0F || lba == 0) continue;
        starts.push_back(lba);
    }
    if (protective) gpt_partitions(reader, starts);
    for (std::uint64_t lba : starts) {
        if (mount_candidate(reader, lba, out, e)) {
            error.clear();
            return true;
        }
        tried += "; " + e;
    }
    error = "no FAT32 or NTFS partition found (" + tried + ")";
    return false;
}

}  // namespace riftwii
