// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/vsdparts.hpp"

#include <cstdio>

namespace riftwii {

namespace {

std::uint32_t le16(const std::uint8_t* p) { return p[0] | (p[1] << 8); }
std::uint32_t le32(const std::uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

}  // namespace

bool sd_card_span(const std::uint8_t (&s)[512], std::uint32_t& sectors, std::string& why) {
    sectors = 0;
    if (s[510] != 0x55 || s[511] != 0xAA) {
        why = "the card's first sector has no boot signature";
        return false;
    }
    // A FAT file system from sector 0 (no partition table): its own size.
    const std::uint32_t per_cluster = s[13];
    const bool bpb = (s[0] == 0xEB || s[0] == 0xE9) && le16(s + 11) == 512 && per_cluster != 0 &&
                     (per_cluster & (per_cluster - 1)) == 0 && le16(s + 14) != 0 && (s[16] == 1 || s[16] == 2);
    if (bpb) {
        sectors = le16(s + 19) != 0 ? le16(s + 19) : le32(s + 32);
        if (sectors == 0) {
            why = "the card's FAT boot sector gives no size";
            return false;
        }
        return true;
    }
    // A partition table: to the end of its last partition.
    std::uint64_t end = 0;
    for (unsigned i = 0; i < 4; ++i) {
        const std::uint8_t* e = s + 446 + i * 16;
        const std::uint32_t lba = le32(e + 8), count = le32(e + 12);
        if (e[4] == 0 || count == 0) continue;
        if (static_cast<std::uint64_t>(lba) + count > end) end = static_cast<std::uint64_t>(lba) + count;
    }
    if (end == 0) {
        why = "the card's partition table is empty";
        return false;
    }
    sectors = end > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(end);
    return true;
}

std::string vsd_part_name(const std::string& image, unsigned part) {
    char suffix[16];
    std::snprintf(suffix, sizeof suffix, ".%03u", part);
    return image + suffix;
}

std::string vsd_split_image(const std::string& file) {
    if (file.size() <= 4 || file.compare(file.size() - 4, 4, ".001") != 0) return "";
    return file.substr(0, file.size() - 4);
}

bool join_vsd_parts(const std::vector<VsdPart>& parts, std::vector<Fragment>& out, std::uint64_t& bytes,
                    std::string& why) {
    out.clear();
    bytes = 0;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const VsdPart& part = parts[i];
        const std::string which = parts.size() > 1 ? "part " + std::to_string(i + 1) : "the image";
        if (part.size % 512 != 0) {
            why = which + "'s size (" + std::to_string(part.size) + " bytes) is not whole 512-byte sectors";
            return false;
        }
        std::uint64_t need = part.size / 512;
        for (const Fragment& f : part.fragments) {
            if (need == 0) break;
            Fragment piece{f.sector, f.sector_count < need ? f.sector_count : need};
            need -= piece.sector_count;
            if (piece.sector_count == 0) continue;
            if (!out.empty() && out.back().sector + out.back().sector_count == piece.sector)
                out.back().sector_count += piece.sector_count;
            else
                out.push_back(piece);
        }
        if (need != 0) {
            why = which + "'s pieces on the drive are shorter than the file";
            return false;
        }
        bytes += part.size;
    }
    return true;
}

}  // namespace riftwii
