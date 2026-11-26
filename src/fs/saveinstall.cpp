// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/saveinstall.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

#include "riftwii/checkedalloc.hpp"
#include "riftwii/emunand.hpp"
#include "riftwii/launch.hpp"

namespace riftwii {
namespace {

bool exists(const std::string& path) {
    struct stat status;
    // Any error other than absence must fail closed at a later mkdir/move,
    // rather than assume permission errors mean an available backup name.
    return stat(path.c_str(), &status) == 0 || errno != ENOENT;
}

bool mkdirs(const std::string& path) {
    for (std::size_t i = path.find('/'); i != std::string::npos; i = path.find('/', i + 1)) {
        if (i == 0 || path[i - 1] == ':') continue;
        const std::string parent = path.substr(0, i);
        if (mkdir(parent.c_str(), 0777) != 0 && errno != EEXIST) return false;
    }
    return mkdir(path.c_str(), 0777) == 0 || errno == EEXIST;
}

bool timestamp_ok(const std::string& timestamp) {
    if (timestamp.size() != 15 || timestamp[8] != '-') return false;
    for (std::size_t i = 0; i < timestamp.size(); ++i) {
        if (i != 8 && (timestamp[i] < '0' || timestamp[i] > '9')) return false;
    }
    return true;
}

// Only paths from the validated plan are removed, in reverse depth order.
// No recursive directory walker can follow an unexpected card entry.
void discard_stage(const std::string& stage, const std::vector<std::string>& written) {
    for (const std::string& file : written) {
        const std::string full = stage + "/" + file;
        std::remove(full.c_str());
        std::size_t slash = full.rfind('/');
        while (slash > stage.size() && slash != std::string::npos) {
            rmdir(full.substr(0, slash).c_str());
            slash = full.rfind('/', slash - 1);
        }
    }
    rmdir(stage.c_str());
}

}  // namespace

bool save_read_gci(const SaveListing& save, const ZipReadAt& read, ZipInflateStream* inflater,
                   std::vector<std::uint8_t>& gci, std::string& error) {
    gci.clear();
    if (!save_listing_ok(save) || save.format != SaveFormat::Gci) {
        error = "the download is not a listed GameCube save";
        return false;
    }
    if (!save.member[0]) {
        if (!try_resize(gci, save.size) || !read(0, gci.data(), gci.size())) {
            gci.clear();
            error = "the downloaded GCI could not be read into memory";
            return false;
        }
        return true;
    }
    std::vector<ZipEntry> entries;
    if (!zip_read_directory_at(read, save.size, entries, error, kSaveMaxFiles * 2)) return false;
    std::set<std::string> names, regular;
    std::size_t chosen = entries.size();
    unsigned gci_count = 0;
    std::uint64_t total = 0;
    const auto bad = [&] {
        gci.clear();
        error = "the save ZIP has unsafe paths, conflicting files or more than one GCI";
        return false;
    };
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const ZipEntry& entry = entries[i];
        if (!save_archive_path_ok(entry.name, entry.directory)) return bad();
        std::string folded = entry.name;
        if (entry.directory) folded.pop_back();
        for (char& c : folded) {
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        }
        if (!names.insert(folded).second) return bad();
        if (entry.directory) continue;
        regular.insert(folded);
        total += entry.unpacked_size;
        if (total > kSaveMaxUnpackedBytes || regular.size() > kSaveMaxFiles) return bad();
        if (folded.size() >= 4 && folded.compare(folded.size() - 4, 4, ".gci") == 0) ++gci_count;
        if (entry.name == save.member) chosen = i;
    }
    for (const std::string& name : names) {
        for (std::size_t slash = name.find('/'); slash != std::string::npos; slash = name.find('/', slash + 1)) {
            if (regular.count(name.substr(0, slash))) return bad();
        }
    }
    if (gci_count != 1 || chosen == entries.size()) return bad();
    const ZipEntry& entry = entries[chosen];
    std::string member = save.member;
    for (char& c : member) {
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    }
    if (member.size() < 4 || member.compare(member.size() - 4, 4, ".gci") != 0 ||
        entry.unpacked_size < 64 + 8192 || entry.unpacked_size > 64 + 2043u * 8192 ||
        (entry.unpacked_size - 64) % 8192 != 0) return bad();
    if (!try_resize(gci, entry.unpacked_size)) {
        error = "there is not enough memory to unpack the GameCube save";
        return false;
    }
    std::size_t written = 0;
    const bool ok = zip_extract_at(read, entry, inflater, [&](const std::uint8_t* bytes, std::size_t count) {
        if (count > gci.size() - written) return false;
        std::memcpy(gci.data() + written, bytes, count);
        written += count;
        return true;
    }, error);
    if (!ok || written != gci.size()) {
        gci.clear();
        return false;
    }
    return true;
}

bool save_apply_gci(const SaveListing& save, const std::vector<std::uint8_t>& gci,
                     std::vector<std::uint8_t>& card, bool replace, std::string& error) {
    if (!save_listing_ok(save) || save.platform != SavePlatform::GameCube || save.format != SaveFormat::Gci ||
        gci.size() < 64 + 8192 || gci.size() > 64 + 2043u * 8192 ||
        (!save.member[0] && gci.size() != save.size) || std::memcmp(gci.data(), save.game_id, 6) != 0) {
        error = "the downloaded GCI does not belong to this game or has the wrong size";
        return false;
    }
    GcCardInfo info;
    if (!gc_card_read(card, info, error)) return false;
    if (info.shift_jis != gc_card_shift_jis(save.game_id)) {
        error = "the memory card and this game's save use different text encodings";
        return false;
    }
    const std::string game(save.game_id, 4), maker(save.game_id + 4, 2);
    bool have_game = false;
    for (const GcSave& existing : info.saves) {
        if (existing.game_code == game && existing.maker == maker) have_game = true;
    }
    if (have_game && !replace) {
        error = "this game already has a save; replacement needs confirmation";
        return false;
    }
    std::vector<std::uint8_t> changed;
    if (!try_assign_range(changed, card.begin(), card.end())) {
        error = "there is not enough memory to change the card safely";
        return false;
    }
    // The GCI name is an encoded, fixed-width field, not a filesystem path.
    std::size_t length = 0;
    while (length < 32 && gci[8 + length] != 0) ++length;
    const std::string name(reinterpret_cast<const char*>(gci.data() + 8), length);
    for (const GcSave& existing : info.saves) {
        if (existing.game_code == game && existing.maker == maker && existing.file_name == name) {
            if (!gc_card_delete(changed, existing.slot, error)) return false;
        }
    }
    if (!gc_card_import(changed, gci, error)) return false;
    card.swap(changed);
    error.clear();
    return true;
}

bool save_publish(const std::string& staged, const std::string& target, const std::string& backup,
                   bool replace, const SavePublishOps& ops, std::string& error) {
    error.clear();
    if (!ops.exists || !ops.move || staged == target || backup == target || staged == backup ||
        !ops.exists(staged) || ops.exists(backup)) {
        error = "the staged save or its backup location is not available";
        return false;
    }
    const bool had_save = ops.exists(target);
    if (had_save && !replace) {
        error = "this game already has a save; replacement needs confirmation";
        return false;
    }
    if (had_save && !ops.move(target, backup)) {
        error = "the current save could not be kept as a backup; nothing was replaced";
        return false;
    }
    if (!ops.move(staged, target)) {
        if (had_save && !ops.move(backup, target)) {
            error = "the save could not be installed or restored; the old save is still at " + backup;
        } else {
            error = "the save could not be installed; the previous save is unchanged";
        }
        return false;
    }
    return true;
}

std::uint32_t save_wii_title_type(const std::string& root, const std::string& game_id, std::uint64_t title_id) {
    if (!save_game_id_ok(game_id)) return kSaveDiscTitle;
    const std::uint32_t type = static_cast<std::uint32_t>(title_id >> 32);
    const std::uint32_t id = static_cast<std::uint32_t>(title_id);
    const std::uint32_t game = static_cast<std::uint32_t>(static_cast<unsigned char>(game_id[0])) << 24 |
                               static_cast<std::uint32_t>(static_cast<unsigned char>(game_id[1])) << 16 |
                               static_cast<std::uint32_t>(static_cast<unsigned char>(game_id[2])) << 8 |
                               static_cast<std::uint32_t>(static_cast<unsigned char>(game_id[3]));
    if (id == game && (type == kSaveDiscTitle || type == kSaveDiscChannelTitle)) return type;
    const bool channel_save = exists(root + save_wii_title_path(game_id, kSaveDiscChannelTitle) + "/data");
    const bool disc_save = exists(root + save_wii_title_path(game_id, kSaveDiscTitle) + "/data");
    if (channel_save && !disc_save) return kSaveDiscChannelTitle;
    if (game_id.compare(0, 3, "RMC") == 0) return kSaveDiscChannelTitle;
    return kSaveDiscTitle;
}

bool save_install_wii_zip(const SaveListing& save, const std::string& root, const ZipReadAt& read,
                           ZipInflateStream* inflater, bool replace, const std::string& timestamp,
                           std::string& backup, std::string& error, const SaveInstallProgress& progress,
                           std::uint64_t title_id) {
    backup.clear();
    // rwnand: is the menu's temporary USB mount. All other roots pass the
    // same folder rules as the emulated NAND manager, before adding paths.
    std::string checked;
    const bool usb_mount = root.compare(0, 8, "rwnand:/") == 0;
    const std::string configured = usb_mount ? "usb:/" + root.substr(8) : root;
    if (!emunand_root(configured, checked) || !timestamp_ok(timestamp) ||
        !save_listing_ok(save) || save.format != SaveFormat::WiiZip) {
        error = "the download has no safe emulated NAND destination";
        return false;
    }
    std::vector<ZipEntry> entries;
    std::vector<std::size_t> plan;
    if (!zip_read_directory_at(read, save.size, entries, error, kSaveMaxFiles * 2) ||
        !save_plan_wii_zip(save, entries, plan, error)) return false;
    const std::string title = root + save_wii_title_path(save.game_id, save_wii_title_type(root, save.game_id, title_id));
    const std::string target = title + "/data";
    const std::string stage = title + "/data.download";
    if (exists(stage)) {
        // An earlier download stopped before it finished (power cut, card
        // pulled). Its files are kept under a unique name, as backups are,
        // so nothing is lost and this download can carry on.
        std::string aside;
        for (unsigned serial = 1; serial <= 999; ++serial) {
            aside = title + "/data.download-" + timestamp + "-" + std::to_string(serial);
            if (!exists(aside)) break;
            aside.clear();
        }
        if (aside.empty() || std::rename(stage.c_str(), aside.c_str()) != 0) {
            error = "an earlier save download is still at " + stage + " and could not be moved aside";
            return false;
        }
    }
    const bool had_save = exists(target);
    if (had_save && !replace) {
        error = "this game already has a save; replacement needs confirmation";
        return false;
    }
    std::string destination;
    for (unsigned serial = 1; serial <= 999; ++serial) {
        destination = title + "/data.before-" + timestamp + "-" + std::to_string(serial);
        if (!exists(destination)) break;
        destination.clear();
    }
    if (destination.empty() || !mkdirs(title) || mkdir(stage.c_str(), 0777) != 0) {
        error = "the save's staging folder or backup name could not be made";
        return false;
    }
    std::vector<std::string> written;
    if (!try_reserve(written, plan.size())) {
        rmdir(stage.c_str());
        error = "there is not enough memory to install the save";
        return false;
    }
    bool ok = true;
    std::uint64_t done = 0;
    for (std::size_t index : plan) {
        const ZipEntry& entry = entries[index];
        std::string relative;
        save_wii_zip_relative(save, entry.name, relative);
        written.push_back(relative);
        const std::string path = stage + "/" + relative;
        const std::size_t slash = path.rfind('/');
        if (!mkdirs(path.substr(0, slash))) {
            error = "a folder for the save could not be made";
            ok = false;
            break;
        }
        FILE* output = std::fopen(path.c_str(), "wb");
        if (!output) {
            error = "a file of the save could not be opened for writing";
            ok = false;
            break;
        }
        std::uint8_t magic[4] = {};
        std::size_t magic_bytes = 0;
        ok = zip_extract_at(read, entry, inflater, [&](const std::uint8_t* data, std::size_t size) {
            if (relative == "banner.bin" && magic_bytes < sizeof(magic)) {
                const std::size_t take = std::min(size, sizeof(magic) - magic_bytes);
                std::memcpy(magic + magic_bytes, data, take);
                magic_bytes += take;
            }
            if (std::fwrite(data, 1, size, output) != size) return false;
            done += size;
            return !progress || progress(done, save.unpacked_size);
        }, error);
        if (std::fclose(output) != 0) {
            error = "a save file could not be finished on the drive";
            ok = false;
        }
        if (ok && relative == "banner.bin" && std::memcmp(magic, "WIBN", 4) != 0) {
            error = "the save's banner.bin is not a Wii save banner";
            ok = false;
        }
        if (!ok) break;
    }
    if (ok && progress && !progress(save.unpacked_size, save.unpacked_size)) {
        error = "the save download was stopped";
        ok = false;
    }
    if (ok) {
        const SavePublishOps ops{exists, [](const std::string& from, const std::string& to) {
            return std::rename(from.c_str(), to.c_str()) == 0;
        }};
        ok = save_publish(stage, target, destination, replace, ops, error);
        if (ok && had_save) backup = destination;
    }
    if (!ok) discard_stage(stage, written);
    return ok;
}

}  // namespace riftwii
