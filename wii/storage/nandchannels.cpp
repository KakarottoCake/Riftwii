// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "nandchannels.hpp"

#include <gccore.h>
#include <malloc.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "boot.hpp"
#include "log.hpp"

namespace riftwii::wii {
namespace {

// A Wii holds at most a few hundred titles; a list longer than this is not
// one ES gave in earnest.
constexpr u32 kMaxTitles = 1024;
// A stored TMD with a few hundred contents is under 16 KB; Virtual Console
// and WiiWare ones have a handful.
constexpr u32 kMaxTmdBytes = 0x10000;
// IMET (at 0x40 or 0x80) and its names are in the banner's first 0x700 bytes.
constexpr u32 kBannerHeadBytes = 0x700;

using AlignedBuffer = std::unique_ptr<u8, decltype(&std::free)>;
AlignedBuffer aligned(std::size_t bytes) {
    return AlignedBuffer(static_cast<u8*>(memalign(32, (bytes + 31) & ~std::size_t(31))), &std::free);
}

// The NAND permission check, opened once for the first file the running
// IOS refuses (-102: the Homebrew Channel's IOS refuses other titles'
// contents to an app), and closed by the listing's end.
struct NandAccess {
    bool opened = false;
    bool refused = false;
    s32 open(const std::string& path) {
        static char name[64] ATTRIBUTE_ALIGN(32);
        std::snprintf(name, sizeof name, "%s", path.c_str());
        s32 fd = ISFS_Open(name, ISFS_OPEN_READ);
        if (fd == -102 && !opened && !refused) {
            if (open_nand_permissions("Wii channels")) {
                opened = true;
                fd = ISFS_Open(name, ISFS_OPEN_READ);
            } else {
                refused = true;
            }
        }
        return fd;
    }
    ~NandAccess() {
        if (opened) close_nand_permissions("Wii channels");
    }
};

bool read_stored_tmd(u64 title, TmdContents& out, std::string& why) {
    u32 size = 0;
    if (ES_GetStoredTMDSize(title, &size) < 0 || size < 0x1E4 || size > kMaxTmdBytes) {
        why = "its TMD could not be read";
        return false;
    }
    AlignedBuffer tmd = aligned(size);
    if (!tmd || ES_GetStoredTMD(title, reinterpret_cast<signed_blob*>(tmd.get()), size) < 0) {
        why = "its TMD could not be read";
        return false;
    }
    return parse_tmd_contents(tmd.get(), size, out, why);
}

// The first bytes of a NAND file, or false when it cannot be opened.
bool read_head(NandAccess& access, const std::string& path, u32 bytes, std::vector<u8>& out) {
    const s32 fd = access.open(path);
    if (fd < 0) return false;
    AlignedBuffer buffer = aligned(bytes);
    const s32 got = buffer ? ISFS_Read(fd, buffer.get(), bytes) : -1;
    ISFS_Close(fd);
    if (got <= 0) return false;
    out.assign(buffer.get(), buffer.get() + got);
    return true;
}

bool exists(NandAccess& access, const std::string& path) {
    const s32 fd = access.open(path);
    if (fd < 0) return false;
    ISFS_Close(fd);
    return true;
}

}  // namespace

bool list_nand_channels(std::vector<NandChannel>& out, std::string& status) {
    out.clear();
    if (ISFS_Initialize() < 0) {
        status = "The Wii's memory could not be read (ISFS).";
        return false;
    }
    u32 count = 0;
    if (ES_GetNumTitles(&count) < 0 || count == 0 || count > kMaxTitles) {
        status = "The Wii did not list its titles.";
        return false;
    }
    AlignedBuffer ids = aligned(count * sizeof(u64));
    if (!ids || ES_GetTitles(reinterpret_cast<u64*>(ids.get()), count) < 0) {
        status = "The Wii did not list its titles.";
        return false;
    }
    const u64* titles = reinterpret_cast<const u64*>(ids.get());
    NandAccess access;
    unsigned unreadable = 0, missing = 0;
    for (u32 i = 0; i < count; ++i) {
        NandChannel channel;
        channel.title_id = titles[i];
        channel.kind = classify_channel(channel.title_id);
        if (channel.kind.kind == ChannelKind::None) continue;
        channel.code = channel_code(channel.title_id);
        std::string why;
        if (!read_stored_tmd(channel.title_id, channel.tmd, why)) {
            logf("Wii channels: %s left out: %s\n", channel.code.c_str(), why.c_str());
            ++unreadable;
            continue;
        }
        const TmdContent* banner = tmd_content_by_index(channel.tmd, 0);
        const TmdContent* program = tmd_content_by_index(channel.tmd, channel.tmd.boot_index);
        if (banner) {
            channel.banner_path = nand_content_path(channel.title_id, banner->id);
            std::vector<u8> head;
            if (read_head(access, channel.banner_path, kBannerHeadBytes, head))
                parse_banner_names(head.data(), head.size(), channel.names);
        }
        // A shared program content would live in /shared1; Shop titles'
        // programs are their own.
        channel.playable = program && !(program->type & 0x8000) &&
                           exists(access, nand_content_path(channel.title_id, program->id));
        if (!channel.playable) ++missing;
        out.push_back(std::move(channel));
    }
    std::sort(out.begin(), out.end(), [](const NandChannel& a, const NandChannel& b) { return a.code < b.code; });
    status = std::to_string(out.size()) + " WiiWare and Virtual Console game(s) on the Wii";
    if (missing) status += ", " + std::to_string(missing) + " of them with their game data removed (they cannot start)";
    if (unreadable) status += "; " + std::to_string(unreadable) + " unreadable title(s) left out";
    if (access.refused) status += "; the Wii refused to show their files, so they have no names";
    status += ".";
    logf("Wii channels: %s\n", status.c_str());
    return true;
}

std::string nand_channel_name(const NandChannel& channel, int language) {
    if (language >= 0 && language < kBnrLanguages && !channel.names[language].empty()) return channel.names[language];
    if (!channel.names[kBnrEnglish].empty()) return channel.names[kBnrEnglish];
    for (const std::string& name : channel.names)
        if (!name.empty()) return name;
    return channel.code;
}

}  // namespace riftwii::wii
