// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/wfstitle.hpp"

#include <cstdio>

namespace riftwii {
namespace {

constexpr std::size_t kTmdTitleType = 0x194;
constexpr std::size_t kTmdFixed = 0x1E4;
constexpr std::uint32_t kMaxTmdBytes = 0x1E4 + 512 * 36;  // 512 contents at most
constexpr std::size_t kMaxTitles = 64;

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

bool printable(const std::string& s) {
    for (char c : s) {
        if (c < 0x21 || c > 0x7E) return false;
    }
    return true;
}

// A meta file's title ID: 16 hex digits, then ".tmd".
bool tmd_name(const std::string& name, std::uint64_t& title_id) {
    if (name.size() != 20 || wfs_lower(name.substr(16)) != ".tmd") return false;
    title_id = 0;
    for (std::size_t i = 0; i < 16; ++i) {
        const char c = name[i];
        int digit = -1;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        if (digit < 0) return false;
        title_id = (title_id << 4) | static_cast<std::uint64_t>(digit);
    }
    return true;
}

std::uint32_t code_value(const std::string& code) {
    std::uint32_t v = 0;
    for (char c : code) v = (v << 8) | static_cast<unsigned char>(c);
    return v;
}

// One title's folder: its TMD (the one whose ID ends in its code), the
// others as add-ons, and its program.
bool read_title(const WfsVolume& volume, WfsTitle& title, std::string& why) {
    std::string error;
    WfsEntry meta;
    const WfsFind found = volume.find(title.path + "/meta", meta, error);
    if (found == WfsFind::Missing) {
        why = "no meta folder";
        return false;
    }
    std::vector<WfsEntry> metas;
    if (found != WfsFind::Found || !volume.list(meta, metas, error, 256)) {
        why = error;
        return false;
    }
    for (const WfsEntry& m : metas) {
        std::uint64_t id = 0;
        if (m.directory || !tmd_name(m.name, id)) continue;
        if (static_cast<std::uint32_t>(id) != code_value(title.code)) {
            title.add_ons.push_back(id);
            continue;
        }
        std::vector<std::uint8_t> tmd;
        if (!volume.read_all(title.path + "/meta/" + m.name, tmd, kMaxTmdBytes, error)) {
            why = "its TMD: " + error;
            return false;
        }
        Tmd parsed;
        if (!parse_tmd(tmd.data(), tmd.size(), parsed, error)) {
            why = "its TMD: " + error;
            return false;
        }
        if (parsed.title_id != id) {
            why = "its TMD is named " + wfs_title_text(id) + " but is for " + wfs_title_text(parsed.title_id);
            return false;
        }
        title.title_id = parsed.title_id;
        title.version = parsed.title_version;
        title.ios = parsed.required_ios();
        title.wfs_type = tmd_is_wfs_title(tmd.data(), tmd.size());
        title.tmd = std::move(tmd);
    }
    if (title.title_id == 0) {
        why = "no TMD for " + title.code + " in its meta folder";
        return false;
    }
    WfsEntry program;
    if (volume.find(title.path + "/content/default.dol", program, error) == WfsFind::Found && !program.directory) {
        title.program = true;
        title.program_bytes = program.size;
    }
    return true;
}

}  // namespace

bool is_wfs_install_disc(const std::vector<PartitionEntry>& table) {
    for (const PartitionEntry& p : table) {
        if (p.type == kWfsInstallerPartition) return true;
    }
    return false;
}

bool tmd_is_wfs_title(const std::uint8_t* tmd, std::size_t size) {
    return tmd != nullptr && size >= kTmdFixed && (be32(tmd + kTmdTitleType) & kTitleTypeWfs) != 0;
}

std::string wfs_title_text(std::uint64_t title_id) {
    char text[24];
    const auto low = static_cast<std::uint32_t>(title_id);
    std::string code;
    for (int shift = 24; shift >= 0; shift -= 8) {
        const char c = static_cast<char>((low >> shift) & 0xFF);
        code += (c >= 0x20 && c <= 0x7E) ? c : '.';
    }
    std::snprintf(text, sizeof text, "%08x-", static_cast<unsigned>(title_id >> 32));
    return text + code;
}

bool find_wfs_titles(const WfsVolume& volume, std::vector<WfsTitle>& out, std::vector<std::string>& notes,
                     std::string& error) {
    out.clear();
    WfsEntry title_root;
    const WfsFind found = volume.find("/title", title_root, error);
    if (found == WfsFind::Missing) {
        error = "the drive has no /title folder (nothing is installed on it)";
        return false;
    }
    std::vector<WfsEntry> groups;
    if (found != WfsFind::Found || !volume.list(title_root, groups, error, 256)) return false;
    for (const WfsEntry& group : groups) {
        if (!group.directory || group.link || group.name.size() != 2 || !printable(group.name)) {
            notes.push_back("/title/" + group.name + " is not a title group (two characters)");
            continue;
        }
        std::vector<WfsEntry> codes;
        std::string why;
        if (!volume.list(group, codes, why, 256)) {
            notes.push_back("/title/" + group.name + ": " + why);
            continue;
        }
        for (const WfsEntry& code : codes) {
            const std::string path = "/title/" + group.name + "/" + code.name;
            if (!code.directory || code.link || code.name.size() != 4 || !printable(code.name)) {
                notes.push_back(path + " is not a title (four characters)");
                continue;
            }
            if (out.size() >= kMaxTitles) {
                notes.push_back("more than " + std::to_string(kMaxTitles) + " titles; the rest are left out");
                return true;
            }
            WfsTitle title;
            title.path = path;
            title.group = group.name;
            title.code = code.name;
            if (!read_title(volume, title, why)) {
                notes.push_back(path + ": " + why);
                continue;
            }
            out.push_back(std::move(title));
        }
    }
    return true;
}

int wfs_title_for_disc(const std::string& disc_id, const std::vector<WfsTitle>& titles) {
    if (disc_id.size() < 4) return -1;
    const std::string code = disc_id.substr(0, 4);
    const std::string group = disc_id.size() >= 6 ? disc_id.substr(4, 2) : std::string();
    for (std::size_t i = 0; i < titles.size(); ++i) {
        if (titles[i].program && titles[i].code == code) return static_cast<int>(i);
    }
    int same_group = -1;
    for (std::size_t i = 0; i < titles.size(); ++i) {
        if (!titles[i].program || group.empty() || titles[i].group != group) continue;
        if (same_group >= 0) return -1;  // two of the maker's games: not this function's guess to make
        same_group = static_cast<int>(i);
    }
    return same_group;
}

}  // namespace riftwii
