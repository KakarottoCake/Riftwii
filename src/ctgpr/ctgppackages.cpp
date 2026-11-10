// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgppackages.hpp"

namespace riftwii {
namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

}  // namespace

bool ctgp_parse_packages(const std::uint8_t* data, std::size_t size,
                         std::vector<CtgpPackage>& out, std::string& error) {
    if (size < 16 || be32(data) != 0x50414B30u) {
        error = "not a PAK0 package list";
        return false;
    }
    const std::uint32_t total = be32(data + 4);
    const std::uint32_t reserved = be32(data + 8);
    const std::uint32_t count = be32(data + 12);
    if (reserved != 0) {
        error = "package list has an unknown format";
        return false;
    }
    if (count > kCtgpMaxPackages) {
        error = "package list has too many records";
        return false;
    }
    if (total != (std::uint64_t(count) + 1) * 16 || total > size) {
        error = "package list size does not match its record count";
        return false;
    }
    std::vector<CtgpPackage> list;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint8_t* r = data + 16 + std::size_t(i) * 16;
        CtgpPackage p;
        for (int k = 0; k < 8 && r[k] != 0; ++k) p.name.push_back(static_cast<char>(r[k]));
        p.version = be32(r + 8);
        p.flags = be32(r + 12);
        list.push_back(p);
    }
    out = std::move(list);
    return true;
}

std::uint32_t ctgp_package_version(const std::vector<CtgpPackage>& packages, const std::string& name) {
    for (const auto& p : packages)
        if (p.name == name) return p.version;
    return 0;
}

std::vector<CtgpWordWrite> ctgp_package_id_writes(const std::vector<CtgpPackage>& packages) {
    return {
        {0x816ff9d8, ctgp_package_version(packages, "CORE")},
        {0x816ff9dc, ctgp_package_version(packages, "CTGPR")},
        {0x816ff9e0, ctgp_package_version(packages, "CHAN")},
        {0x816ff9e4, ctgp_package_version(packages, "MYSTUFF")},
        {0x816ff9e8, 0},
    };
}

}  // namespace riftwii
