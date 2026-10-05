// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/vsdparts.hpp"

#include <cstdio>

namespace riftwii {

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
