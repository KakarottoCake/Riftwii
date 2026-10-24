// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/cardvolume.hpp"

#include <cstdio>
#include <cstring>

namespace riftwii {
namespace {

// The MBR and FAT boot sector layouts, as Microsoft's FAT specification
// and every partitioning tool lay them out.
constexpr std::size_t kSignatureOffset = 510;
constexpr std::size_t kPartitionTableOffset = 446;
constexpr std::size_t kPartitionEntryBytes = 16;
constexpr int kPartitionEntries = 4;
constexpr std::uint8_t kGptProtectiveType = 0xEE;
constexpr std::size_t kOemNameOffset = 3;          // "EXFAT   ", "NTFS    "
constexpr std::size_t kBytesPerSectorOffset = 11;
constexpr std::size_t kSectorsPerClusterOffset = 13;
constexpr std::size_t kFat16TypeOffset = 54;       // "FAT12   ", "FAT16   ", "FAT     "
constexpr std::size_t kFat32TypeOffset = 82;       // "FAT32   "

std::uint32_t le32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

std::uint16_t le16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

bool has_signature(const std::uint8_t* sector) {
    return sector[kSignatureOffset] == 0x55 && sector[kSignatureOffset + 1] == 0xAA;
}

bool text_at(const std::uint8_t* sector, std::size_t offset, const char* text) {
    return std::memcmp(sector + offset, text, std::strlen(text)) == 0;
}

// The file system a boot sector names, or "" when it names none.
std::string file_system_name(const std::uint8_t* boot) {
    if (text_at(boot, kOemNameOffset, "EXFAT   ")) return "exFAT";
    if (text_at(boot, kOemNameOffset, "NTFS    ")) return "NTFS";
    if (text_at(boot, kFat32TypeOffset, "FAT32")) return "FAT32";
    if (text_at(boot, kFat16TypeOffset, "FAT16")) return "FAT16";
    if (text_at(boot, kFat16TypeOffset, "FAT12")) return "FAT12";
    if (text_at(boot, kFat16TypeOffset, "FAT")) return "FAT";
    return "";
}

bool is_fat(const std::string& name) {
    return name.compare(0, 3, "FAT") == 0;
}

std::string where(const CardVolumeStart& start) {
    char text[96];
    switch (start.scheme) {
    case CardVolumeStart::Scheme::Mbr:
        std::snprintf(text, sizeof(text), "in MBR partition %d (type 0x%02X) from sector %u", start.partition,
                      static_cast<unsigned>(start.type), static_cast<unsigned>(start.first_sector));
        return text;
    case CardVolumeStart::Scheme::NoTable:
        return "with no partition table (the volume starts at sector 0)";
    case CardVolumeStart::Scheme::Gpt:
        std::snprintf(text, sizeof(text), "a GPT partition table (protective MBR entry %d from sector %u)",
                      start.partition, static_cast<unsigned>(start.first_sector));
        return text;
    case CardVolumeStart::Scheme::Unreadable:
        break;
    }
    return "no partition the card's first sector names";
}

}  // namespace

CardVolumeStart find_card_volume(const std::uint8_t* sector0) {
    CardVolumeStart start;
    if (!has_signature(sector0)) {
        start.problem = "sector 0 has no boot signature, 55 AA";
        return start;
    }
    // A card formatted without a partition table: sector 0 is the FAT
    // volume's boot sector itself. FatFs, under libfat and in d2x, looks
    // for the same type names first.
    if (!file_system_name(sector0).empty()) {
        start.scheme = CardVolumeStart::Scheme::NoTable;
        return start;
    }
    for (int entry = 0; entry < kPartitionEntries; ++entry) {
        const std::uint8_t* row = sector0 + kPartitionTableOffset + entry * kPartitionEntryBytes;
        const std::uint8_t type = row[4];
        if (type == 0) continue;
        start.partition = entry + 1;
        start.type = type;
        start.first_sector = le32(row + 8);
        start.scheme = type == kGptProtectiveType ? CardVolumeStart::Scheme::Gpt : CardVolumeStart::Scheme::Mbr;
        return start;
    }
    start.problem = "the partition table in sector 0 has no used entry";
    return start;
}

std::string describe_card_volume(const CardVolumeStart& start, const std::uint8_t* boot_sector) {
    if (start.scheme == CardVolumeStart::Scheme::Unreadable) return "no file system that RiftWii could find (" + start.problem + ")";
    if (start.scheme == CardVolumeStart::Scheme::Gpt) return where(start);
    if (boot_sector == nullptr) return "a volume " + where(start) + ", whose boot sector could not be read";
    const std::string name = file_system_name(boot_sector);
    if (name.empty()) {
        const char* signature = has_signature(boot_sector) ? "" : " and no boot signature";
        return "no FAT, exFAT or NTFS boot sector" + std::string(signature) + " " + where(start);
    }
    if (!is_fat(name)) return name + " " + where(start);
    char geometry[80];
    std::snprintf(geometry, sizeof(geometry), ", %u-byte sectors, %u sectors per cluster, ",
                  static_cast<unsigned>(le16(boot_sector + kBytesPerSectorOffset)),
                  static_cast<unsigned>(boot_sector[kSectorsPerClusterOffset]));
    return name + geometry + where(start);
}

}  // namespace riftwii
