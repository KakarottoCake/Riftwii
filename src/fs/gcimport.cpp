// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gcimport.hpp"

#include "riftwii/gccard.hpp"

#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <sys/stat.h>

namespace riftwii {
namespace {

bool is_directory(const std::string& path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

std::string lower(const std::string& text) {
    std::string out = text;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool ends_with_nocase(const std::string& text, const std::string& suffix) {
    const std::string low = lower(text);
    return low.size() >= suffix.size() && low.compare(low.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// The plain names in `folder`, sorted; none when it is not there. At most
// kGcDolphinMaxEntries are looked at, so one huge folder cannot stall the menu.
std::vector<std::string> list_names(const std::string& folder, bool& truncated) {
    std::vector<std::string> names;
    DIR* dir = opendir(folder.c_str());
    if (!dir) return names;
    std::size_t seen = 0;
    while (dirent* entry = readdir(dir)) {
        if (++seen > kGcDolphinMaxEntries) {
            truncated = true;
            break;
        }
        const std::string name = entry->d_name;
        if (gc_import_name_ok(name)) names.push_back(name);
    }
    closedir(dir);
    std::sort(names.begin(), names.end());
    return names;
}

void add_unique(std::vector<std::string>& list, const std::string& path, std::size_t limit, bool& truncated) {
    if (std::find(list.begin(), list.end(), path) != list.end()) return;
    if (list.size() >= limit) {
        truncated = true;
        return;
    }
    list.push_back(path);
}

// Whether a raw card's name is for one of `regions`, or for no region at
// all: "MemoryCardA.USA.raw" is the USA's, "backup.raw" is anybody's.
bool raw_name_fits(const std::string& name, const std::vector<GcDolphinRegion>& regions) {
    const std::string low = lower(name);
    bool tagged = false;
    for (GcDolphinRegion region : {GcDolphinRegion::Usa, GcDolphinRegion::Eur, GcDolphinRegion::Jap}) {
        const std::string tag = "." + lower(gc_dolphin_region_name(region)) + ".";
        if (low.find(tag) == std::string::npos) continue;
        tagged = true;
        if (std::find(regions.begin(), regions.end(), region) != regions.end()) return true;
    }
    return !tagged;
}

// The .gci's header checks: the same ones gc_card_import makes, so a bad
// file can be told from a duplicate and from a full card. `blocks` gets
// the save's block count (entry offset 0x38).
bool gci_header(const std::vector<std::uint8_t>& gci, std::uint32_t& blocks) {
    if (gci.size() < kGcCardEntryBytes || gci[0] == 0xFF) return false;
    blocks = static_cast<std::uint32_t>(gci[0x38] << 8 | gci[0x39]);
    return blocks != 0 && gci.size() == kGcCardEntryBytes + std::size_t(blocks) * kGcCardBlockBytes;
}

// A directory entry's text field with its trailing NULs cut, as the
// library reads one, so the same save compares equal.
std::string field(const std::uint8_t* at, std::size_t bytes) {
    std::size_t length = 0;
    while (length < bytes && at[length] != 0) ++length;
    return std::string(reinterpret_cast<const char*>(at), length);
}

}  // namespace

const char* gc_dolphin_region_name(GcDolphinRegion region) {
    switch (region) {
        case GcDolphinRegion::Usa: return "USA";
        case GcDolphinRegion::Eur: return "EUR";
        case GcDolphinRegion::Jap: return "JAP";
    }
    return "USA";
}

std::vector<GcDolphinRegion> gc_dolphin_regions(const std::string& game_id, bool shared) {
    if (shared) {
        if (gc_card_shift_jis(game_id)) return {GcDolphinRegion::Jap};
        return {GcDolphinRegion::Usa, GcDolphinRegion::Eur};
    }
    const char letter = game_id.size() >= 4 ? game_id[3] : '\0';
    if (letter == 'E') return {GcDolphinRegion::Usa};
    if (letter == 'J' || letter == 'K') return {GcDolphinRegion::Jap};
    return {GcDolphinRegion::Eur};
}

bool gc_import_name_ok(const std::string& name) {
    if (name.empty() || name.size() > 255 || name[0] == '.') return false;
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F || c == '/' || c == '\\' || c == ':') return false;
    }
    return true;
}

bool gc_dolphin_folder_exists(const std::string& folder) { return is_directory(folder); }

GcDolphinFiles gc_dolphin_scan(const std::string& folder, const std::vector<GcDolphinRegion>& regions) {
    GcDolphinFiles found;
    found.folder_exists = is_directory(folder);
    if (!found.folder_exists) return found;

    // The folder itself, and the GC folder inside it when it was copied whole.
    std::vector<std::string> bases{folder};
    if (is_directory(folder + "/GC")) bases.push_back(folder + "/GC");

    for (const std::string& base : bases) {
        for (GcDolphinRegion region : regions) {
            const std::string region_folder = base + "/" + gc_dolphin_region_name(region);
            for (const char* slot : {"Card A", "Card B"}) {
                const std::string cards = region_folder + "/" + slot;
                for (const std::string& name : list_names(cards, found.truncated)) {
                    if (ends_with_nocase(name, ".gci"))
                        add_unique(found.gci, cards + "/" + name, kGcDolphinMaxGci, found.truncated);
                }
            }
        }
    }
    // Raw cards sit beside the region folders; one in a region's own folder is taken too.
    std::vector<std::string> raw_folders = bases;
    for (const std::string& base : bases) {
        for (GcDolphinRegion region : regions) raw_folders.push_back(base + "/" + gc_dolphin_region_name(region));
    }
    for (const std::string& where : raw_folders) {
        for (const std::string& name : list_names(where, found.truncated)) {
            if (ends_with_nocase(name, ".raw") && raw_name_fits(name, regions))
                add_unique(found.raw, where + "/" + name, kGcDolphinMaxRaw, found.truncated);
        }
    }
    std::sort(found.gci.begin(), found.gci.end());
    std::sort(found.raw.begin(), found.raw.end());
    return found;
}

void gc_import_one(std::vector<std::uint8_t>& card, const std::vector<std::uint8_t>& gci, GcImportCount& count) {
    if (count.full) return;
    std::uint32_t blocks = 0;
    // The menu bounds what it reads, but the library does not rely on it.
    if (gci.size() > kGcDolphinMaxGciBytes || !gci_header(gci, blocks)) {
        ++count.unusable;
        return;
    }
    GcCardInfo info;
    std::string error;
    if (!gc_card_read(card, info, error)) {
        ++count.unusable;
        return;
    }
    const std::string code = field(gci.data(), 4);
    const std::string maker = field(gci.data() + 4, 2);
    const std::string name = field(gci.data() + 8, 32);
    for (const GcSave& save : info.saves) {
        if (save.game_code == code && save.maker == maker && save.file_name == name) {
            ++count.skipped;
            return;
        }
    }
    if (info.free_entries == 0 || blocks > info.free_blocks) {
        count.full = true;
        return;
    }
    if (!gc_card_import(card, gci, error)) {
        ++count.unusable;
        return;
    }
    ++count.added;
}

void gc_import_card(std::vector<std::uint8_t>& card, const std::vector<std::uint8_t>& raw, GcImportCount& count) {
    GcCardInfo info;
    std::string error;
    if (raw.size() > kGcDolphinMaxRawBytes || !gc_card_read(raw, info, error)) {
        ++count.unusable;
        return;
    }
    for (const GcSave& save : info.saves) {
        if (count.full) return;
        std::vector<std::uint8_t> gci;
        if (!save.intact || !gc_card_export(raw, save.slot, gci, error)) {
            ++count.unusable;
            continue;
        }
        gc_import_one(card, gci, count);
    }
}

bool gc_posix_lister(const std::string& folder, std::vector<GcFolderEntry>& out) {
    out.clear();
    DIR* dir = opendir(folder.c_str());
    if (!dir) return false;
    // One more than the scan looks at, so it can tell there were too many.
    while (dirent* entry = readdir(dir)) {
        if (out.size() > kGcDolphinMaxEntries) break;
        GcFolderEntry found;
        found.name = entry->d_name;
        // The size and the kind of a name that is never joined are not asked for.
        if (gc_import_name_ok(found.name)) {
            struct stat info;
            if (stat((folder + "/" + found.name).c_str(), &info) == 0) {
                found.is_directory = S_ISDIR(info.st_mode);
                found.bytes = found.is_directory ? 0 : static_cast<std::uint64_t>(info.st_size);
            }
        }
        out.push_back(found);
    }
    closedir(dir);
    return true;
}

bool gc_nintendont_name_ok(const std::string& name) {
    if (!gc_import_name_ok(name)) return false;
    const std::string low = lower(name);
    for (const char* fixed : {"ninmem.raw", "ninmemj.raw", "ninmemb.raw", "ninmemjb.raw"}) {
        if (low == fixed) return true;
    }
    // <ID4>.raw (8 characters) or <ID4>_B.raw (10): the ID is four letters or digits.
    const bool slot_b = low.size() == 10 && low.compare(4, 6, "_b.raw") == 0;
    const bool slot_a = low.size() == 8 && low.compare(4, 4, ".raw") == 0;
    if (!slot_a && !slot_b) return false;
    for (std::size_t i = 0; i < 4; ++i) {
        if (!std::isalnum(static_cast<unsigned char>(low[i]))) return false;
    }
    return true;
}

GcNintendontFiles gc_nintendont_scan(const std::vector<std::string>& folders, const GcFolderLister& list) {
    GcNintendontFiles found;
    for (const std::string& folder : folders) {
        std::vector<GcFolderEntry> entries;
        if (!list(folder, entries)) continue;
        std::sort(entries.begin(), entries.end(),
                  [](const GcFolderEntry& a, const GcFolderEntry& b) { return a.name < b.name; });
        std::size_t seen = 0;
        for (const GcFolderEntry& entry : entries) {
            if (++seen > kGcDolphinMaxEntries) {
                found.truncated = true;
                break;
            }
            if (entry.is_directory || !gc_nintendont_name_ok(entry.name)) continue;
            // A card is whole 8 KB blocks, from 0.5 MB to 16 MB; anything
            // else with a card's name is not read.
            if (entry.bytes < kGcNintendontMinBytes || entry.bytes > kGcDolphinMaxRawBytes ||
                entry.bytes % kGcCardBlockBytes != 0) {
                ++found.wrong_size;
                continue;
            }
            if (found.cards.size() >= kGcNintendontMaxCards) {
                found.truncated = true;
                return found;
            }
            GcNintendontCard card;
            card.name = entry.name;
            card.path = folder + "/" + entry.name;
            card.bytes = entry.bytes;
            found.cards.push_back(card);
        }
    }
    return found;
}

bool gc_nintendont_list(std::size_t card_index, const std::uint8_t* card, std::size_t card_bytes,
                        std::vector<GcNintendontSave>& out, std::string& error) {
    GcCardInfo info;
    if (card_bytes > kGcDolphinMaxRawBytes || !gc_card_read(card, card_bytes, info, error)) {
        if (card_bytes > kGcDolphinMaxRawBytes) error = "The file is larger than any memory card.";
        return false;
    }
    for (const GcSave& save : info.saves) {
        GcNintendontSave listed;
        listed.card = card_index;
        listed.slot = save.slot;
        listed.id = save.game_code + save.maker;
        listed.file_name = save.file_name;
        listed.comment_title = save.comment_title;
        listed.blocks = save.block_count;
        listed.intact = save.intact;
        listed.shift_jis = info.shift_jis;
        listed.entry = save.entry;
        out.push_back(listed);
    }
    return true;
}

std::string gc_nintendont_route(const GcNintendontSave& save, bool shared) { return gc_card_path(save.id, shared); }

void gc_nintendont_blank_card(std::vector<std::uint8_t>& card, const std::uint8_t* source, std::size_t source_bytes,
                              bool shift_jis, std::uint64_t format_time) {
    GcCardFormat format;
    format.shift_jis = shift_jis;
    format.format_time = format_time;
    constexpr std::size_t kHeaderBytes = 0x200;
    if (source_bytes >= kHeaderBytes) {
        const std::vector<std::uint8_t> header(source, source + kHeaderBytes);
        gc_card_flash_id(header, format.flash_id);
    }
    gc_card_format(card, format);
}

void gc_nintendont_import(std::vector<std::uint8_t>& target, const std::uint8_t* source, std::size_t source_bytes,
                          const GcNintendontSave& save, GcImportCount& count) {
    if (count.full) return;
    GcCardInfo from;
    GcCardInfo to;
    std::string error;
    if (source_bytes > kGcDolphinMaxRawBytes || !gc_card_read(source, source_bytes, from, error) ||
        !gc_card_read(target, to, error) || from.shift_jis != to.shift_jis) {
        ++count.unusable;
        return;
    }
    // The save must still be in its slot as it was listed: a Nintendont
    // card that changed since (Nintendont ran in between) is not guessed at.
    const GcSave* listed = nullptr;
    for (const GcSave& candidate : from.saves) {
        if (candidate.slot == save.slot) listed = &candidate;
    }
    if (!listed || !listed->intact || listed->entry != save.entry) {
        ++count.unusable;
        return;
    }
    std::vector<std::uint8_t> gci;
    if (!gc_card_export(source, source_bytes, save.slot, gci, error)) {
        ++count.unusable;
        return;
    }
    gc_import_one(target, gci, count);
}

}  // namespace riftwii
