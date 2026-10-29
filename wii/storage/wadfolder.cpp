// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "wadfolder.hpp"

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "log.hpp"
#include "otpkey.hpp"
#include "sharednandfind.hpp"
#include "usbcatalog.hpp"
#include "wadinstall.hpp"
#include "riftwii/checkedalloc.hpp"
#include "riftwii/wadfile.hpp"

namespace riftwii::wii {
namespace {

constexpr const char* kCache = "sd:/riftwii/wiiware-cache";
constexpr std::size_t kMaxWads = 1024;
constexpr std::size_t kMaxEntries = 8192;

std::string title_folder(const std::string& nand, std::uint64_t title) {
    char relative[48];
    std::snprintf(relative, sizeof relative, "/title/%08x/%08x", static_cast<unsigned>(title >> 32),
                  static_cast<unsigned>(title));
    return nand + relative;
}

bool file_size_is(const std::string& path, std::uint64_t size) {
    struct stat info;
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) &&
           static_cast<std::uint64_t>(info.st_size) == size;
}

bool read_file(const std::string& path, std::size_t most, std::vector<std::uint8_t>& bytes) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    const bool sought = std::fseek(file, 0, SEEK_END) == 0;
    const long size = sought ? std::ftell(file) : -1;
    const bool read = size >= 0 && static_cast<unsigned long>(size) <= most &&
                      try_resize(bytes, static_cast<std::size_t>(size)) && std::fseek(file, 0, SEEK_SET) == 0 &&
                      std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
    std::fclose(file);
    return read;
}

bool write_file(const std::string& path, const std::uint8_t* bytes, std::size_t size) {
    const std::string part = path + ".part";
    FILE* file = std::fopen(part.c_str(), "wb");
    const bool written = file && std::fwrite(bytes, 1, size, file) == size;
    const bool closed = file && std::fclose(file) == 0;
    if (!written || !closed || std::rename(part.c_str(), path.c_str()) != 0) {
        std::remove(part.c_str());
        return false;
    }
    return true;
}

std::string digest_name(const Sha1Digest& digest) {
    std::string name;
    for (std::uint8_t byte : digest) {
        char hex[3];
        std::snprintf(hex, sizeof hex, "%02x", byte);
        name += hex;
    }
    return name;
}

std::uint64_t modified_time(const std::string& path) {
    if (is_usb_path(path)) return 0;  // the raw USB view exposes size, not timestamps
    struct stat info;
    return stat(path.c_str(), &info) == 0 ? static_cast<std::uint64_t>(info.st_mtime) : 0;
}

Sha1Digest source_stamp(const WadMetadata& metadata, std::uint64_t modified) {
    Sha1 hash;
    hash.update(metadata.fingerprint.data(), metadata.fingerprint.size());
    std::uint8_t bytes[8];
    for (unsigned i = 0; i < 8; ++i) bytes[i] = static_cast<std::uint8_t>(modified >> (56 - i * 8));
    hash.update(bytes, sizeof bytes);
    return hash.finish();
}

bool safe_entry(const std::string& name) {
    return !name.empty() && name[0] != '.' && name.size() <= 255 &&
           name.find_first_of("/\\:") == std::string::npos;
}

void scan_folder(const std::string& folder, bool nested, std::vector<std::string>& files, std::size_t& visited) {
    if (files.size() >= kMaxWads || visited >= kMaxEntries) return;
    std::vector<VolumeEntry> entries;
    if (is_usb_path(folder)) {
        if (!list_usb_folder(folder, entries)) return;
    } else {
        DIR* directory = opendir(folder.c_str());
        if (!directory) return;
        while (const dirent* entry = readdir(directory)) {
            if (++visited > kMaxEntries) break;
            const std::string name = entry->d_name;
            if (!safe_entry(name)) continue;
            struct stat info;
            if (stat((folder + "/" + name).c_str(), &info) != 0) continue;
            VolumeEntry item;
            item.name = name;
            item.is_directory = S_ISDIR(info.st_mode);
            item.size = static_cast<std::uint64_t>(info.st_size);
            if (!try_push_back(entries, std::move(item))) break;
        }
        closedir(directory);
    }
    for (const VolumeEntry& entry : entries) {
        if (files.size() >= kMaxWads) break;
        if (is_usb_path(folder) && ++visited > kMaxEntries) break;
        if (!safe_entry(entry.name)) continue;
        const std::string path = folder + "/" + entry.name;
        if (entry.is_directory) {
            if (!nested) scan_folder(path, true, files, visited);
        } else if (entry.name.size() > 4 && strcasecmp(entry.name.c_str() + entry.name.size() - 4, ".wad") == 0) {
            if (!try_push_back(files, path)) break;
        }
    }
}

bool installed_contents(const NandChannel& channel, const WadMetadata& metadata) {
    const std::string& folder = channel.emu_nand;
    std::vector<std::uint8_t> bytes;
    read_file(folder + "/shared1/content.map", 1u << 20, bytes);
    const auto shared = parse_content_map(bytes.data(), bytes.size());
    for (const TmdContent& content : metadata.tmd.contents) {
        std::string path = folder + nand_content_path(metadata.tmd.title_id, content.id);
        if ((content.type & 0x8000) != 0) {
            const SharedContent* found = find_shared_content(shared, content.sha1);
            if (!found) return false;
            path = folder + "/shared1/" + found->name + ".app";
        }
        if (!file_size_is(path, content.size)) return false;
    }
    char ticket[48];
    std::snprintf(ticket, sizeof ticket, "/ticket/00010001/%08x.tik", static_cast<unsigned>(metadata.tmd.title_id));
    if (!read_file(folder + ticket, 0x10000, bytes) || bytes != metadata.ticket) return false;
    if (!read_file(folder + "/sys/uid.sys", 12 * 8192, bytes)) return false;
    const std::size_t original_size = bytes.size();
    std::string error;
    return add_wad_title_uid(bytes, metadata.tmd.title_id, error) && bytes.size() == original_size;
}

}  // namespace

bool wad_managed_title(const std::string& folder, std::uint64_t title_id) {
    struct stat info;
    return stat((title_folder(folder, title_id) + "/content/.riftwii-wad").c_str(), &info) == 0;
}

void merge_wad_channels(std::vector<NandChannel>& out, std::vector<NandChannel>& found) {
    for (NandChannel& channel : found) {
        auto existing = std::find_if(out.begin(), out.end(), [&](const NandChannel& other) {
            return other.title_id == channel.title_id;
        });
        if (existing == out.end()) {
            if (!try_push_back(out, std::move(channel))) break;
        } else if (!existing->wad_path.empty() || (existing->emu_nand.empty() && existing->playable)) {
            // An SD WAD wins over a USB duplicate. An installed real-NAND
            // copy retains its established launch/save location.
            continue;
        } else {
            if (!existing->emu_nand.empty()) channel.emu_nand = existing->emu_nand;
            *existing = std::move(channel);
        }
    }
}

unsigned list_wad_channels(bool usb, std::vector<NandChannel>& out) {
    unsigned unreadable = 0;
    std::vector<std::string> files;
    std::size_t visited = 0;
    if (usb) {
        for (const std::string& root : usb_volume_roots()) scan_folder(root + "wiiware", false, files, visited);
    } else {
        scan_folder("sd:/wiiware", false, files, visited);
    }
    if (!usb) std::sort(files.begin(), files.end());
    const SharedNandLookup nand = look_up_channel_nand();
    const std::string folder = nand.usable ? nand.folder : "sd:/nands/01";
    mkdir("sd:/riftwii", 0777);
    mkdir(kCache, 0777);
    std::vector<NandChannel> found;
    for (const std::string& path : files) {
        std::string error;
        const auto source = open_wad_source(path, error);
        WadMetadata metadata;
        if (!source || !read_wad_metadata(*source, metadata, error)) {
            logf("WiiWare folder: %s was left out because %s.\n", path.c_str(), error.c_str());
            ++unreadable;
            continue;
        }
        const ChannelClass kind = classify_channel(metadata.tmd.title_id);
        if (kind.kind == ChannelKind::None) continue;
        const std::uint64_t modified = modified_time(path);
        const std::string cache = std::string(kCache) + "/" +
            digest_name(sha1(reinterpret_cast<const std::uint8_t*>(path.data()), path.size()));
        std::vector<std::uint8_t> bytes;
        WadCacheRecord record;
        bool cached = read_file(cache + ".meta", 0x800, bytes) && decode_wad_cache(bytes.data(), bytes.size(), record) &&
                      record.fingerprint == metadata.fingerprint && record.modified == modified &&
                      record.title_id == metadata.tmd.title_id && file_size_is(cache + ".bnr", record.banner_size);
        const bool banner_changed = !cached;
        if (!cached) {
            std::uint8_t key[16];
            if (!ConsoleCommonKey(key)) {
                logf("WiiWare folder: %s was left out because this Wii's key could not be read.\n", path.c_str());
                ++unreadable;
                continue;
            }
            const bool read = read_wad_banner(*source, metadata, key, bytes, error);
            std::memset(key, 0, sizeof key);
            if (!read) {
                logf("WiiWare folder: %s was left out because %s.\n", path.c_str(), error.c_str());
                ++unreadable;
                continue;
            }
            record.fingerprint = metadata.fingerprint;
            record.modified = modified;
            record.title_id = metadata.tmd.title_id;
            record.banner_size = static_cast<std::uint32_t>(bytes.size());
            if (!try_assign_range(record.banner_head, bytes.begin(), bytes.begin() + std::min<std::size_t>(bytes.size(), 0x700))) continue;
            cached = write_file(cache + ".bnr", bytes.data(), bytes.size()) && encode_wad_cache(record, bytes) &&
                     write_file(cache + ".meta", bytes.data(), bytes.size());
            logf("WiiWare folder: %s had its banner read; the SD cache %s.\n", path.c_str(), cached ? "was saved" : "could not be saved");
        }
        NandChannel channel;
        channel.title_id = metadata.tmd.title_id;
        channel.tmd = std::move(metadata.tmd);
        channel.kind = kind;
        channel.code = channel_code(channel.title_id);
        channel.game_id = channel_game_id(channel.code, channel.tmd.maker);
        if (banner_changed) {
            // Home's shared banner store must stop using an older WAD's art.
            std::remove(("sd:/riftwii/banners/" + channel.game_id + ".bnr").c_str());
        }
        parse_banner_names(record.banner_head.data(), record.banner_head.size(), channel.names);
        channel.wad_path = path;
        channel.wad_stamp = source_stamp(metadata, modified);
        if (cached) channel.wad_banner_cache = cache + ".bnr";
        channel.emu_nand = folder;
        channel.playable = true;  // its program is in the validated WAD
        if (!try_push_back(found, std::move(channel))) break;
    }
    logf("WiiWare folder: %u WAD games found on %s.\n", static_cast<unsigned>(found.size()), usb ? "the USB partitions" : "the SD card");
    merge_wad_channels(out, found);
    return unreadable;
}

bool prepare_wad_channel(NandChannel& channel, const std::function<void(double)>& progress, std::string& error) {
    if (channel.wad_path.empty()) return true;
    const auto source = open_wad_source(channel.wad_path, error);
    WadMetadata metadata;
    if (!source || !read_wad_metadata(*source, metadata, error)) return false;
    if (metadata.tmd.title_id != channel.title_id) {
        error = "the WAD is now a different game; read the drives again";
        return false;
    }
    if (!prepare_wad_nand_system(channel.emu_nand, error)) return false;
    const Sha1Digest stamp = source_stamp(metadata, modified_time(channel.wad_path));
    const std::string title = title_folder(channel.emu_nand, channel.title_id);
    const std::string marker = title + "/content/.riftwii-wad";
    std::vector<std::uint8_t> bytes;
    const bool managed = wad_managed_title(channel.emu_nand, channel.title_id);
    Sha1Digest prepared;
    const bool has_stamp = read_file(marker, prepared.size(), bytes) && bytes.size() == prepared.size();
    if (has_stamp) std::copy(bytes.begin(), bytes.end(), prepared.begin());
    const bool same_tmd = read_file(title + "/content/title.tmd", 0x10000, bytes) && bytes == metadata.tmd_bytes;
    const bool contents = same_tmd && installed_contents(channel, metadata);
    if (!wad_needs_preparation(stamp, has_stamp ? &prepared : nullptr, contents, same_tmd)) {
        logf("WiiWare folder: %s is already ready in %s; unpacking was skipped.\n", channel.game_id.c_str(), channel.emu_nand.c_str());
        return true;
    }
    const bool new_title = !read_file(title + "/content/title.tmd", 0x10000, bytes);
    std::uint8_t key[16];
    if (!ConsoleCommonKey(key)) {
        error = "this Wii's key could not be read";
        return false;
    }
    std::string code;
    if (progress) progress(0);
    logf("WiiWare folder: getting %s ready from %s in %s.\n", channel.game_id.c_str(), channel.wad_path.c_str(), channel.emu_nand.c_str());
    const bool installed = install_wad(channel.wad_path, channel.emu_nand, key, progress, code, error,
                                       channel.title_id, managed || new_title ? &stamp : nullptr);
    std::memset(key, 0, sizeof key);
    if (!installed) return false;
    channel.wad_stamp = stamp;
    return true;
}

}  // namespace riftwii::wii
