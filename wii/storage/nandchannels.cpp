// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "nandchannels.hpp"

#include <gccore.h>
#include <malloc.h>

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>

#include "boot.hpp"
#include "ios_reload.hpp"
#include "log.hpp"
#include "movedchannels.hpp"
#include "usbcatalog.hpp"
#include "nanduse.hpp"
#include "nandpermissions.hpp"
#include "wadfolder.hpp"
#include "wadinstall.hpp"
#include "riftwii/wadfile.hpp"
#include "riftwii/checkedalloc.hpp"

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

// Open once before the listing's first protected file, so a refused open
// cannot warm the Starlet's cache with the unpatched permission check.
struct NandAccess {
    bool trace = false;
    bool refused = false;
    NandPermissionSession permissions{"Wii channels"};
    s32 open(const std::string& path) {
        static char name[64] ATTRIBUTE_ALIGN(32);
        std::snprintf(name, sizeof name, "%s", path.c_str());
        if (trace) {
            logf("Wii channels: opening %s on the Wii.\n", name);
            LogSync();
        }
        s32 fd = ISFS_Open(name, ISFS_OPEN_READ);
        if (fd == -102) refused = true;
        if (fd == -102 && permissions.IsOpen()) {
            logf("Wii channels: %s was still refused (-102) with the NAND permission check open.\n", name);
        }
        return fd;
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
    if (access.trace) logf("Wii channels: reading %u header bytes from %s.\n", static_cast<unsigned>(bytes), path.c_str());
    if (access.trace) LogSync();
    const s32 got = buffer ? ISFS_Read(fd, buffer.get(), bytes) : -1;
    ISFS_Close(fd);
    if (got <= 0) return false;
    return try_assign_range(out, buffer.get(), buffer.get() + got);
}

// A whole NAND file of at most `max_bytes`, or false with `error`.
bool read_whole(NandAccess& access, const std::string& path, u32 max_bytes, std::vector<u8>& out,
                std::string& error) {
    const s32 fd = access.open(path);
    if (fd < 0) {
        error = path + " could not be opened (" + std::to_string(fd) + ")";
        return false;
    }
    if (access.trace) logf("Wii channels: reading the file size of %s.\n", path.c_str());
    static fstats stats ATTRIBUTE_ALIGN(32);
    if (access.trace) LogSync();
    const u32 size = ISFS_GetFileStats(fd, &stats) < 0 ? 0 : stats.file_length;
    if (size == 0 || size > max_bytes) {
        ISFS_Close(fd);
        error = path + " is " + std::to_string(size) + " bytes, not a size RiftWii reads";
        return false;
    }
    // Keep only one full program in memory. A full aligned read followed by
    // vector::assign needed twice its size, with no checked allocation.
    constexpr u32 kReadChunkBytes = 64 * 1024;
    AlignedBuffer buffer = aligned(std::min(size, kReadChunkBytes));
    std::vector<u8> bytes;
    if (!buffer || !try_resize(bytes, size)) {
        ISFS_Close(fd);
        error = "out of memory for " + path;
        return false;
    }
    if (access.trace) logf("Wii channels: reading %u bytes from %s.\n", static_cast<unsigned>(size), path.c_str());
    if (access.trace) LogSync();
    u32 done = 0;
    while (done < size) {
        const u32 piece = std::min(kReadChunkBytes, size - done);
        const s32 got = ISFS_Read(fd, buffer.get(), piece);
        if (got != static_cast<s32>(piece)) {
            ISFS_Close(fd);
            error = path + " read " + std::to_string(got) + " of " + std::to_string(piece) +
                    " bytes at offset " + std::to_string(done);
            return false;
        }
        std::memcpy(bytes.data() + done, buffer.get(), piece);
        done += piece;
    }
    ISFS_Close(fd);
    out.swap(bytes);
    return true;
}

// A whole file of the SD card of at most `max_bytes`, or false with `error`.
bool read_card_file(const std::string& path, u32 max_bytes, std::vector<u8>& out, std::string& error) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        error = path + " could not be opened";
        return false;
    }
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size <= 0 || static_cast<unsigned long>(size) > max_bytes) {
        std::fclose(file);
        error = path + " is " + std::to_string(size) + " bytes, not a size RiftWii reads";
        return false;
    }
    std::vector<u8> bytes;
    if (!try_resize(bytes, static_cast<std::size_t>(size))) {
        std::fclose(file);
        error = "out of memory for " + path;
        return false;
    }
    const std::size_t got = std::fread(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
    if (got != bytes.size()) {
        error = path + " read " + std::to_string(got) + " of " + std::to_string(size) + " bytes";
        return false;
    }
    out.swap(bytes);
    return true;
}

// The first `bytes` of a file of the SD card, or false.
bool read_card_head(const std::string& path, std::size_t bytes, std::vector<u8>& out) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return false;
    out.resize(bytes);
    const std::size_t got = std::fread(out.data(), 1, bytes, file);
    std::fclose(file);
    out.resize(got);
    return got > 0;
}

// Reads the first `bytes` of a content's file into `out`; false when it cannot.
using HeadReader = std::function<bool(const std::string& path, std::size_t bytes, std::vector<u8>& out)>;

// Whether a channel GameTDB's list does not name stays off Home, and if so,
// logs it by code, banner name and reason. It reads the start of each of the
// title's own contents (judge_unlisted_channel says what it needs): the boot
// content's up to kForwarderMaxBytes, the others' a program header's worth
// (a two-stage forwarder's second stage whole).
bool leave_out_unlisted(const NandChannel& channel, const HeadReader& read, const std::string& where) {
    std::vector<ContentStart> starts;
    for (const TmdContent& content : channel.tmd.contents) {
        if (content.index == 0 || (content.type & 0x8000) != 0) continue;
        const std::uint64_t most = content.index == channel.tmd.boot_index ? kForwarderMaxBytes : kProgramHeadBytes;
        ContentStart start;
        start.index = content.index;
        start.size = content.size;
        const std::size_t bytes = static_cast<std::size_t>(std::min<std::uint64_t>(most, content.size));
        if (bytes != 0 && read(nand_content_path(channel.title_id, content.id), bytes, start.head))
            starts.push_back(std::move(start));
    }
    // A forwarder in two stages names its app in the second: that one is read
    // whole when it is small enough to be one (forwarder_second_stage).
    const int second = forwarder_second_stage(channel.tmd, starts);
    for (ContentStart& start : starts) {
        if (second < 0 || start.index != second) continue;
        const TmdContent* content = tmd_content_by_index(channel.tmd, start.index);
        std::vector<u8> whole;
        if (content != nullptr && read(nand_content_path(channel.title_id, content->id),
                                       static_cast<std::size_t>(start.size), whole) &&
            whole.size() == start.size) {
            start.head = std::move(whole);
        }
    }
    const ChannelVerdict verdict = judge_unlisted_channel(channel.code, channel.tmd, starts);
    if (!verdict.left_out) return false;
    std::string name;
    for (const std::string& banner_name : channel.names)
        if (name.empty()) name = banner_name;
    std::string unread;
    if (verdict.unread_content_id != 0) {
        char id[48];
        std::snprintf(id, sizeof id, "; content %08x could not be read",
                      static_cast<unsigned>(verdict.unread_content_id));
        unread = id;
    }
    logf("Wii channels: %s%s%s%s left out%s (rule %d of 4): %s%s\n", channel.code.c_str(),
         name.empty() ? "" : " (", name.c_str(), name.empty() ? "" : ")", where.c_str(), verdict.rule,
         verdict.reason.c_str(), unread.c_str());
    return true;
}

// classify_channel passes over some titles without a word, so a report
// could not say why such a channel is not on Home. Says it.
void log_left_out_title(std::uint64_t title_id, const std::string& where) {
    const std::string reason = channel_left_out_reason(title_id);
    if (reason.empty()) return;
    std::string code = channel_code(title_id);
    if (code.empty()) {
        char hex[16];
        std::snprintf(hex, sizeof hex, "%08x", static_cast<unsigned>(title_id & 0xFFFFFFFFu));
        code = hex;
    }
    const bool homebrew = is_homebrew_code(code);
    logf("Wii channels: %s left out%s%s: %s\n", code.c_str(), where.c_str(), homebrew ? " (rule 1 of 4)" : "",
         reason.c_str());
}

bool card_file_exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && !S_ISDIR(st.st_mode);
}

// A file a failed replacement left only as its .riftwii-old copy (both
// renames failed) gets its name back. False when it could not: the copy is
// then the only good one and must stay. Runs before anything reads or
// seeds the file, so a fresh copy never takes the original's place.
bool recover_emu_file(const std::string& destination) {
    const std::string old = destination + ".riftwii-old";
    if (card_file_exists(destination) || !card_file_exists(old)) return true;
    return std::rename(old.c_str(), destination.c_str()) == 0;
}

s32 open_result(NandAccess& access, const std::string& path) {
    const s32 fd = access.open(path);
    if (fd >= 0) ISFS_Close(fd);
    return fd;
}

bool program_present(const TmdContents& tmd, const TmdContent* program,
                     const std::function<bool(const TmdContent&)>& own_content_exists) {
    if (program == nullptr) return false;
    if ((program->type & 0x8000) == 0) return own_content_exists(*program);
    for (const TmdContent& c : tmd.contents) {
        if (c.index == 0 || (c.type & 0x8000) != 0) continue;
        if (own_content_exists(c)) return true;
    }
    return false;
}

}  // namespace

bool list_nand_channels(std::vector<NandChannel>& out, std::string& status, bool include_non_games) {
    out.clear();
    if (NandOpen() < 0) {
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
    unsigned unreadable = 0, missing = 0, unlisted = 0, on_card = 0;
    for (u32 i = 0; i < count; ++i) {
        NandChannel channel;
        channel.title_id = titles[i];
        channel.kind = classify_channel(channel.title_id);
        // Dragon Quest X's boot program is a disc channel (00010004) the
        // Wii keeps; the game it starts is on the WFS USB drive.
        if (channel.kind.kind == ChannelKind::None && static_cast<u32>(channel.title_id >> 32) == kDiscChannels) {
            std::string ignored;
            if (read_stored_tmd(channel.title_id, channel.tmd, ignored) &&
                is_wfs_game(channel.title_id, channel.tmd.title_type)) {
                channel.kind.kind = ChannelKind::Wfs;
                logf("Wii channels: %s is a WFS title (Dragon Quest X): its game is on the WFS USB drive\n",
                     channel_code(channel.title_id).c_str());
            }
        }
        channel.non_game = channel.kind.kind == ChannelKind::None && include_non_games &&
                           is_nintendo_channel(channel.title_id);
        if (channel.kind.kind == ChannelKind::None && !channel.non_game) {
            log_left_out_title(channel.title_id, "");
            continue;
        }
        channel.code = channel_code(channel.title_id);
        // Homebrew forwarders take Shop-like codes too (Nintendont's is
        // WWND), and custom injects take made-up ones GameTDB does not list.
        // A code its list lacks is judged below from the title itself.
        // A code with no Shop letter (a custom inject's) is always judged,
        // A WFS title is Nintendo's own, whatever the list says.
        const bool not_in_list = !channel.non_game && channel.kind.kind != ChannelKind::Wfs &&
                                 (channel.kind.kind == ChannelKind::Other ||
                                  (GameTitleListed(channel.code, channel.listed_name) && channel.listed_name.empty()));
        std::string why;
        if (!read_stored_tmd(channel.title_id, channel.tmd, why)) {
            logf("Wii channels: %s left out: %s\n", channel.code.c_str(), why.c_str());
            ++unreadable;
            continue;
        }
        channel.game_id = channel_game_id(channel.code, channel.tmd.maker);
        const TmdContent* banner = tmd_content_by_index(channel.tmd, 0);
        const TmdContent* program = tmd_content_by_index(channel.tmd, channel.tmd.boot_index);
        channel.playable = program_present(channel.tmd, program, [&](const TmdContent& c) {
            return open_result(access, nand_content_path(channel.title_id, c.id)) >= 0;
        });
        // The Wii Menu moved it to the SD card: the Wii kept its TMD and
        // ticket, and its contents are read from the card's content.bin.
        MovedChannel moved;
        if (!channel.playable) {
            const std::string moved_path = moved_channel_file(channel.code);
            std::string moved_why;
            if (moved_path.empty()) {
                // Not moved: its game data was deleted.
            } else if (moved.open(moved_path, channel.title_id, moved_why)) {
                channel.content_bin = moved_path;
                channel.playable = program_present(channel.tmd, program, [&](const TmdContent& c) {
                    return moved.has(c.id);
                });
            } else {
                logf("Wii channels: %s is on the SD card (%s), but it cannot be read: %s\n", channel.code.c_str(),
                     moved_path.c_str(), moved_why.c_str());
            }
        }
        const HeadReader read = [&](const std::string& path, std::size_t bytes, std::vector<u8>& head) {
            if (!channel.content_bin.empty()) {
                for (const TmdContent& c : channel.tmd.contents) {
                    if (!moved.has(c.id) || nand_content_path(channel.title_id, c.id) != path) continue;
                    std::string ignored;
                    return moved.read(c.id, bytes, head, ignored);
                }
            }
            return read_head(access, path, static_cast<u32>(bytes), head);
        };
        if (banner) {
            channel.banner_path = nand_content_path(channel.title_id, banner->id);
            std::vector<u8> head;
            if (read(channel.banner_path, kBannerHeadBytes, head))
                parse_banner_names(head.data(), head.size(), channel.names);
        }
        if (not_in_list && leave_out_unlisted(channel, read, "")) {
            ++unlisted;
            continue;
        }
        if (channel.listed_name.empty()) {
            bool banner_name = false;
            for (const std::string& n : channel.names) banner_name = banner_name || !n.empty();
            if (!banner_name)
                logf("Wii channels: %s has no name in the title list and its banner gives none; Home shows its code\n",
                     channel.code.c_str());
        }
        if (!channel.playable) {
            if (!channel.non_game) ++missing;
            const s32 result = program ? open_result(access, nand_content_path(channel.title_id, program->id)) : -1;
            logf("Wii channels: %s%s: its program is not there (boot index %u, content %08x, type 0x%04x, open %d)\n",
                 channel.code.c_str(), not_in_list ? " (not in GameTDB's list)" : "",
                 static_cast<unsigned>(channel.tmd.boot_index), program ? program->id : 0,
                 program ? static_cast<unsigned>(program->type) : 0, result);
        }
        if (channel.playable && !channel.content_bin.empty()) ++on_card;
        out.push_back(std::move(channel));
    }
    std::sort(out.begin(), out.end(), [](const NandChannel& a, const NandChannel& b) { return a.code < b.code; });
    const std::size_t non_games = static_cast<std::size_t>(std::count_if(
        out.begin(), out.end(), [](const NandChannel& channel) { return channel.non_game; }));
    status = std::to_string(out.size() - non_games) +
             " WiiWare, Virtual Console and other channel game(s) on the Wii";
    if (non_games) status += ", " + std::to_string(non_games) + " non-game channel(s)";
    if (on_card) status += ", " + std::to_string(on_card) + " of them moved to the SD card by the Wii Menu";
    if (missing) status += ", " + std::to_string(missing) + " of them with their game data removed (they cannot start)";
    if (unlisted) status += "; " + std::to_string(unlisted) + " unlisted channel(s) left out";
    if (unreadable) status += "; " + std::to_string(unreadable) + " unreadable title(s) left out";
    if (access.refused) status += "; the Wii refused to show their files, so they have no names";
    status += ".";
    logf("Wii channels: %s\n", status.c_str());
    return true;
}

namespace {

struct EmuTotals {
    unsigned unreadable = 0, unlisted = 0, twice = 0, instead = 0;
};

void scan_emu_title_type(const std::string& folder, std::uint32_t upper, std::vector<NandChannel>& out,
                         bool include_non_games, EmuTotals& totals) {
    char type[9];
    std::snprintf(type, sizeof(type), "%08x", static_cast<unsigned>(upper));
    const std::string titles = folder + "/title/" + type;
    DIR* dir = opendir(titles.c_str());
    if (dir == nullptr) return;
    while (const dirent* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name.size() != 8 || name.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) continue;
        NandChannel channel;
        channel.emu_nand = folder;
        channel.title_id = (std::uint64_t(upper) << 32) | std::strtoul(name.c_str(), nullptr, 16);
        if (wad_managed_title(folder, channel.title_id)) continue;
        channel.kind = classify_channel(channel.title_id);
        channel.non_game = channel.kind.kind == ChannelKind::None && include_non_games &&
                           is_nintendo_channel(channel.title_id);
        if (channel.kind.kind == ChannelKind::None && !channel.non_game) {
            log_left_out_title(channel.title_id, " in " + folder);
            continue;
        }
        channel.code = channel_code(channel.title_id);
        // A code with no Shop letter (a custom inject's) is always judged,
        // even before GameTDB's list has loaded.
        const bool not_in_list = !channel.non_game &&
                                 (channel.kind.kind == ChannelKind::Other ||
                                  (GameTitleListed(channel.code, channel.listed_name) && channel.listed_name.empty()));
        // The Wii's own copy, when it can start, is the one shown. One whose
        // game data was removed from the Wii (it keeps only the ticket, the
        // TMD and the save; a tester had 30 of 31 so, all whole in the
        // emulated NAND) gives way to this copy, which can.
        const auto wii_copy = std::find_if(out.begin(), out.end(),
                                           [&](const NandChannel& c) { return c.title_id == channel.title_id; });
        if (wii_copy != out.end() && wii_copy->playable) {
            ++totals.twice;
            continue;
        }
        // The stored TMD, as the Wii keeps it.
        std::vector<u8> tmd;
        std::string why;
        if (!read_card_file(titles + "/" + name + "/content/title.tmd", kMaxTmdBytes, tmd, why) ||
            !parse_tmd_contents(tmd.data(), tmd.size(), channel.tmd, why)) {
            logf("Wii channels: %s in %s left out: %s\n", channel.code.c_str(), folder.c_str(), why.c_str());
            ++totals.unreadable;
            continue;
        }
        channel.game_id = channel_game_id(channel.code, channel.tmd.maker);
        const TmdContent* banner = tmd_content_by_index(channel.tmd, 0);
        const TmdContent* program = tmd_content_by_index(channel.tmd, channel.tmd.boot_index);
        if (banner) {
            channel.banner_path = nand_content_path(channel.title_id, banner->id);
            std::vector<u8> head;
            if (read_card_head(folder + channel.banner_path, kBannerHeadBytes, head))
                parse_banner_names(head.data(), head.size(), channel.names);
        }
        if (not_in_list) {
            const HeadReader read = [&](const std::string& path, std::size_t bytes, std::vector<u8>& head) {
                return read_card_head(folder + path, bytes, head);
            };
            if (leave_out_unlisted(channel, read, " in " + folder)) {
                ++totals.unlisted;
                continue;
            }
        }
        if (channel.listed_name.empty()) {
            bool banner_name = false;
            for (const std::string& n : channel.names) banner_name = banner_name || !n.empty();
            if (!banner_name)
                logf("Wii channels: %s in %s has no name in the title list and its banner gives none; Home shows its code\n",
                     channel.code.c_str(), folder.c_str());
        }
        channel.playable = program_present(channel.tmd, program, [&](const TmdContent& c) {
            return card_file_exists(folder + nand_content_path(channel.title_id, c.id));
        });
        if (!channel.playable) {
            logf("Wii channels: %s in %s: its program is not there (boot index %u, content %08x, type 0x%04x)\n",
                 channel.code.c_str(), folder.c_str(), static_cast<unsigned>(channel.tmd.boot_index),
                 program ? program->id : 0, program ? static_cast<unsigned>(program->type) : 0);
        }
        if (wii_copy != out.end()) {
            if (!channel.playable) {
                ++totals.twice;
                continue;
            }
            *wii_copy = std::move(channel);  // nothing was added to `out` since the search
            ++totals.instead;
            continue;
        }
        out.push_back(std::move(channel));
    }
    closedir(dir);
}

}

bool list_emu_channels(const std::string& folder, std::vector<NandChannel>& out, std::string& status,
                       bool include_non_games) {
    EmuTotals totals;
    bool opened = false;
    for (const std::uint32_t upper : kChannelTitleTypes) {
        char type[9];
        std::snprintf(type, sizeof(type), "%08x", static_cast<unsigned>(upper));
        if (DIR* probe = opendir((folder + "/title/" + type).c_str())) {
            opened = true;
            closedir(probe);
        }
        scan_emu_title_type(folder, upper, out, include_non_games, totals);
    }
    if (!opened) {
        status = "No channels in " + folder + ".";
        logf("Wii channels: %s\n", status.c_str());
        return false;
    }
    std::sort(out.begin(), out.end(), [](const NandChannel& a, const NandChannel& b) { return a.code < b.code; });
    const std::size_t non_games = static_cast<std::size_t>(std::count_if(
        out.begin(), out.end(), [](const NandChannel& channel) { return channel.non_game; }));
    status = std::to_string(out.size() - non_games) +
             " WiiWare, Virtual Console and other channel game(s) in " + folder;
    if (non_games) status += ", " + std::to_string(non_games) + " non-game channel(s)";
    if (totals.twice) status += "; " + std::to_string(totals.twice) + " the Wii has itself listed once";
    if (totals.instead)
        status += "; " + std::to_string(totals.instead) +
                  " shown from here instead of the Wii's copy, whose game data was removed";
    if (totals.unlisted) status += "; " + std::to_string(totals.unlisted) + " unlisted channel(s) left out";
    if (totals.unreadable) status += "; " + std::to_string(totals.unreadable) + " unreadable title(s) left out";
    status += ".";
    logf("Wii channels: %s\n", status.c_str());
    return !out.empty();
}

bool emu_channel_program_present(const std::string& folder, std::uint64_t title_id) {
    if (folder.empty()) return false;
    char relative[80];
    std::snprintf(relative, sizeof(relative), "/title/%08x/%08x/content/title.tmd",
                  static_cast<unsigned>(title_id >> 32), static_cast<unsigned>(title_id));
    std::vector<std::uint8_t> bytes;
    TmdContents tmd;
    std::string why;
    if (!read_card_file(folder + relative, kMaxTmdBytes, bytes, why) ||
        !parse_tmd_contents(bytes.data(), bytes.size(), tmd, why) || tmd.title_id != title_id)
        return false;
    const TmdContent* program = tmd_content_by_index(tmd, tmd.boot_index);
    return program_present(tmd, program, [&](const TmdContent& content) {
        return card_file_exists(folder + nand_content_path(title_id, content.id));
    });
}

bool prepare_wad_nand_system(const std::string& folder, std::string& error) {
    // Full emulation redirects system settings too. A newly created NAND
    // needs the console's region, Remote pairings and title owners before
    // the game's SDK asks for them. Existing settings stay as they are.
    if (folder.compare(0, 4, "sd:/") != 0 || folder.size() <= 4 || folder.find("..") != std::string::npos ||
        folder.find_first_of("\\", 4) != std::string::npos || folder.find(':', 3) != std::string::npos) {
        error = "the emulated NAND is not a plain SD folder";
        return false;
    }
    const char* paths[] = {"/shared2/sys/SYSCONF", "/title/00000001/00000002/data/setting.txt",
                           "/sys/uid.sys", "/sys/cert.sys"};
    if (NandOpen() < 0) {
        error = "the Wii's system settings could not be read";
        return false;
    }
    struct NandUser {
        ~NandUser() { NandClose(); }
    } nand_user;
    NandAccess access;
    for (const char* path : paths) {
        const std::string destination = folder + path;
        // The emulated NAND's own file, if a failed replacement set it aside,
        // comes back before the Wii's copy could be seeded in its place.
        if (!recover_emu_file(destination)) {
            error = destination + " is in its .riftwii-old copy and could not be put back";
            return false;
        }
        if (card_file_exists(destination)) continue;
        std::vector<u8> bytes;
        if (!read_whole(access, path, 128 * 1024, bytes, error)) return false;
        for (std::size_t slash = destination.find('/', 4); slash != std::string::npos;
             slash = destination.find('/', slash + 1)) {
            mkdir(destination.substr(0, slash).c_str(), 0777);
        }
        const std::string part = destination + ".riftwii-part";
        FILE* file = std::fopen(part.c_str(), "wb");
        const bool written = file && std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
        const bool closed = file && std::fclose(file) == 0;
        if (!written || !closed || std::rename(part.c_str(), destination.c_str()) != 0) {
            std::remove(part.c_str());
            error = "the SD card could not save the Wii's system settings (is it full or locked?)";
            return false;
        }
        logf("WiiWare folder: missing system file %s was copied from this Wii to the emulated NAND.\n", path);
    }
    mkdir((folder + "/tmp").c_str(), 0777);
    return true;
}

namespace {

// Writes `bytes` to `destination` through a .riftwii-part file, making
// the folders on the way (below `folder`).
bool write_emu_file(const std::string& folder, const std::string& destination, const std::vector<u8>& bytes,
                    std::string& error) {
    for (std::size_t slash = destination.find('/', folder.size() + 1); slash != std::string::npos;
         slash = destination.find('/', slash + 1)) {
        mkdir(destination.substr(0, slash).c_str(), 0777);
    }
    const std::string part = destination + ".riftwii-part";
    FILE* file = std::fopen(part.c_str(), "wb");
    const bool written = file && std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    const bool closed = file && std::fclose(file) == 0;
    error = "the SD card could not save " + destination + " (is it full or locked?)";
    if (!written || !closed) {
        std::remove(part.c_str());
        return false;
    }
    // FAT's rename does not replace a file: the old one steps aside first
    // and comes back if the new one cannot take its name, so a failure
    // never loses a file that was there (uid.sys above all).
    const std::string old = destination + ".riftwii-old";
    if (!recover_emu_file(destination)) {
        std::remove(part.c_str());
        return false;
    }
    std::remove(old.c_str());  // a leftover beside a whole file
    const bool had_old = card_file_exists(destination);
    if (had_old && std::rename(destination.c_str(), old.c_str()) != 0) {
        std::remove(part.c_str());
        return false;
    }
    if (std::rename(part.c_str(), destination.c_str()) != 0) {
        if (had_old) std::rename(old.c_str(), destination.c_str());
        std::remove(part.c_str());
        return false;
    }
    if (had_old) std::remove(old.c_str());
    error.clear();
    return true;
}

}  // namespace

namespace {

// Whether the SD card holds `path` as a file of exactly `size` bytes.
bool card_file_has_size(const std::string& path, std::uint64_t size) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && !S_ISDIR(st.st_mode) && static_cast<std::uint64_t>(st.st_size) == size;
}

// One title copied from this Wii's NAND into the emulated NAND `folder`:
// its contents (a shared one through both NANDs' content.map), its ticket,
// with `owner` its entry in the ownership list and its data folder, and
// its TMD last, so a copy cut short never looks like a whole title. A
// title already there in the same version is left as it is (`copied`
// false). `shared_allowed` false refuses a title with shared contents.
bool copy_nand_title_to_emu(NandAccess& access, const std::string& folder, std::uint64_t title, bool owner,
                            bool shared_allowed, TmdContents& tmd, bool& copied, std::string& error) {
    copied = false;
    char ticket_path[48];
    std::snprintf(ticket_path, sizeof ticket_path, "/ticket/%08x/%08x.tik", static_cast<unsigned>(title >> 32),
                  static_cast<unsigned>(title));
    char title_path[40];
    std::snprintf(title_path, sizeof title_path, "/title/%08x/%08x", static_cast<unsigned>(title >> 32),
                  static_cast<unsigned>(title));
    const std::string tmd_path = std::string(title_path) + "/content/title.tmd";

    std::vector<u8> tmd_bytes;
    std::string why;
    if (!read_whole(access, tmd_path, kMaxTmdBytes, tmd_bytes, error)) return false;
    if (!parse_tmd_contents(tmd_bytes.data(), tmd_bytes.size(), tmd, why) || tmd.title_id != title) {
        error = std::string(title_path) + "'s TMD on the Wii is damaged (" + why + ")";
        return false;
    }

    // Where each content lives in the emulated NAND. A shared one keeps the
    // name the emulated NAND's list gives it, or takes a new one there; it
    // is read from the Wii under the name the Wii's list gives it.
    std::vector<std::string> destinations;
    std::vector<std::string> sources;
    std::vector<SharedContent> wii_map;
    std::vector<SharedContent> emu_map;
    bool maps_read = false;
    bool map_changed = false;
    const std::string emu_map_path = folder + "/shared1/content.map";
    for (const TmdContent& content : tmd.contents) {
        if ((content.type & 0x8000) == 0) {
            if (!try_push_back(destinations, folder + nand_content_path(title, content.id)) ||
                !try_push_back(sources, nand_content_path(title, content.id))) {
                error = "out of memory for the list of contents";
                return false;
            }
            continue;
        }
        if (!shared_allowed) {
            error = "the channel uses a shared content, which is not copied";
            return false;
        }
        if (!maps_read) {
            std::vector<u8> bytes;
            if (!read_whole(access, "/shared1/content.map", 1u << 20, bytes, error)) return false;
            wii_map = parse_content_map(bytes.data(), bytes.size());
            bytes.clear();
            // A list a failed replacement left aside is read, not started
            // again empty: rewriting it would drop other titles' entries.
            if (!recover_emu_file(emu_map_path)) {
                error = "the emulated NAND's shared-content list is in " + emu_map_path +
                        ".riftwii-old and could not be put back";
                return false;
            }
            // An empty list is a list with no entries (the installer writes
            // one), read as such: read_card_file refuses an empty file.
            if (card_file_exists(emu_map_path) && !card_file_has_size(emu_map_path, 0) &&
                !read_card_file(emu_map_path, 1u << 20, bytes, error))
                return false;
            emu_map = parse_content_map(bytes.data(), bytes.size());
            // The parser stops at the first entry that isn't one: a damaged
            // list would be written back short, and other titles would lose
            // their shared contents. Refused instead, as the NAND manager does.
            if (emu_map.size() * 28 != bytes.size()) {
                error = "the emulated NAND's shared-content list " + emu_map_path + " is damaged";
                return false;
            }
            maps_read = true;
        }
        const SharedContent* on_wii = find_shared_content(wii_map, content.sha1);
        if (on_wii == nullptr) {
            error = "a shared content of " + std::string(title_path) + " is not in the Wii's content.map";
            return false;
        }
        std::string name;
        if (const SharedContent* known = find_shared_content(emu_map, content.sha1)) {
            name = known->name;
        } else {
            name = next_shared_name(emu_map);
            if (name.size() != 8 || !try_push_back(emu_map, SharedContent{name, content.sha1})) {
                error = "the emulated NAND has too many shared contents";
                return false;
            }
            map_changed = true;
        }
        if (!try_push_back(destinations, folder + "/shared1/" + name + ".app") ||
            !try_push_back(sources, "/shared1/" + on_wii->name + ".app")) {
            error = "out of memory for the list of contents";
            return false;
        }
    }

    // The ownership list lets ES give a channel its own folder: read (and
    // the entry added in memory) first, so a title found whole below still
    // gets an entry a list restored from an older copy had lost.
    std::vector<u8> uid;
    const std::string uid_path = folder + "/sys/uid.sys";
    std::size_t uid_before = 0;
    const std::string data_path = folder + title_path + "/data";
    if (owner) {
        // A list an earlier failed replacement left aside is read, not rebuilt.
        if (!recover_emu_file(uid_path)) {
            error = "the emulated NAND's title ownership list is in " + uid_path +
                    ".riftwii-old and could not be put back";
            return false;
        }
        if (card_file_exists(uid_path) && !read_card_file(uid_path, 12 * 8192, uid, error)) return false;
        uid_before = uid.size();
        if (!add_wad_title_uid(uid, title, error)) return false;
    }
    // The channel's save folder: one already there is fine; a file in its
    // place would leave the channel without one.
    const auto make_data_folder = [&]() {
        if (!owner) return true;
        mkdir(data_path.c_str(), 0777);
        struct stat st;
        if (stat(data_path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return true;
        error = "the SD card could not make the channel's data folder " + data_path;
        return false;
    };

    // Already there, the same version as on the Wii, every content the size
    // its TMD gives: nothing more than its ownership entry and data folder.
    // An older copy is replaced.
    std::vector<u8> emu_tmd;
    std::string ignored;
    bool whole = !map_changed && read_card_file(folder + tmd_path, kMaxTmdBytes, emu_tmd, ignored) &&
                 emu_tmd == tmd_bytes && card_file_exists(folder + ticket_path);
    for (std::size_t i = 0; whole && i < tmd.contents.size(); ++i) {
        const TmdContent& content = tmd.contents[i];
        whole = card_file_has_size(destinations[i], content.size);
        // A shared content's file can be another content of the same size,
        // left by an earlier run: only its bytes say it is this one. An own
        // content's name is its ID, so its size is enough.
        if (whole && (content.type & 0x8000) != 0) {
            std::vector<u8> there;
            whole = read_card_file(destinations[i], 8u << 20, there, ignored) &&
                    sha1(there.data(), there.size()) == content.sha1;
        }
    }
    if (whole) {
        if (uid.size() != uid_before && !write_emu_file(folder, uid_path, uid, error)) return false;
        return make_data_folder();
    }

    std::vector<u8> ticket;
    if (!read_whole(access, ticket_path, 0x10000, ticket, error)) return false;
    // One content in memory at a time (an IOS is a few dozen of them).
    constexpr u32 kMaxContentBytes = 8u << 20;
    const auto read_checked = [&](std::size_t i, std::vector<u8>& bytes) {
        const TmdContent& content = tmd.contents[i];
        if (content.size > kMaxContentBytes) {
            error = "a content of " + std::string(title_path) + " is larger than RiftWii copies";
            return false;
        }
        if (!read_whole(access, sources[i], kMaxContentBytes, bytes, error)) return false;
        if (bytes.size() != content.size || sha1(bytes.data(), bytes.size()) != content.sha1) {
            error = "a content of " + std::string(title_path) + " on the Wii does not match its TMD";
            return false;
        }
        return true;
    };
    // A shared content already in the emulated NAND under its name is kept
    // only when its bytes are the ones the TMD names (a run cut short can
    // leave another content's file of the same size there).
    std::vector<char> keep;
    if (!try_resize(keep, tmd.contents.size())) {
        error = "out of memory for the list of contents";
        return false;
    }
    for (std::size_t i = 0; i < tmd.contents.size(); ++i) {
        const TmdContent& content = tmd.contents[i];
        if ((content.type & 0x8000) == 0 || !card_file_has_size(destinations[i], content.size)) continue;
        std::vector<u8> there;
        keep[i] = read_card_file(destinations[i], kMaxContentBytes, there, ignored) &&
                  sha1(there.data(), there.size()) == content.sha1;
    }
    // Every content is read and checked on the Wii before anything is
    // replaced, so a source that can't be read leaves an older copy as it
    // was, still whole.
    for (std::size_t i = 0; i < tmd.contents.size(); ++i) {
        if (keep[i]) continue;
        std::vector<u8> bytes;
        if (!read_checked(i, bytes)) return false;
    }

    // The shared-content list as it will be written, made before anything
    // on the card changes: an empty one (out of memory, or more entries
    // than it holds) written over the old would cut every title's shared
    // contents off.
    std::vector<u8> encoded_map;
    if (map_changed) {
        encoded_map = serialize_content_map(emu_map);
        if (encoded_map.size() != emu_map.size() * 28) {
            error = "the emulated NAND's list of shared contents could not be made";
            return false;
        }
    }

    // An older copy's TMD goes before its contents are replaced: an update
    // cut short then leaves no title at all (the next launch copies it
    // again), never an old TMD over new contents that looks whole.
    if (card_file_exists(folder + tmd_path) && std::remove((folder + tmd_path).c_str()) != 0) {
        error = "the SD card could not remove the older copy of " + std::string(title_path);
        return false;
    }
    for (std::size_t i = 0; i < tmd.contents.size(); ++i) {
        if (keep[i]) continue;
        std::vector<u8> bytes;
        if (!read_checked(i, bytes)) return false;
        if (!write_emu_file(folder, destinations[i], bytes, error)) return false;
    }
    if (map_changed && !write_emu_file(folder, emu_map_path, encoded_map, error)) return false;
    if (!write_emu_file(folder, folder + ticket_path, ticket, error)) return false;
    if (owner && uid.size() != uid_before && !write_emu_file(folder, uid_path, uid, error)) return false;
    if (!make_data_folder()) return false;
    if (!write_emu_file(folder, folder + tmd_path, tmd_bytes, error)) return false;
    copied = true;
    return true;
}

}  // namespace

bool copy_return_channel_to_emu_nand(const std::string& folder, std::uint64_t title, std::string& error) {
    if (folder.compare(0, 4, "sd:/") != 0 || folder.size() <= 4 || folder.find("..") != std::string::npos ||
        folder.find_first_of("\\", 4) != std::string::npos || folder.find(':', 3) != std::string::npos) {
        error = "the emulated NAND is not a plain SD folder";
        return false;
    }
    if ((title >> 32) != 0x00010001) {
        error = "only an ordinary channel can be copied";
        return false;
    }
    if (NandOpen() < 0) {
        error = "the Wii's storage could not be opened";
        return false;
    }
    struct NandUser {
        ~NandUser() { NandClose(); }
    } nand_user;
    NandAccess access;
    // RiftWii's channel is a few small contents of its own; a shared one
    // means this is not that channel.
    TmdContents channel;
    bool copied = false;
    if (!copy_nand_title_to_emu(access, folder, title, true, false, channel, copied, error)) return false;
    // Starting the channel loads the IOS its TMD names, and under the
    // emulation of the whole NAND it is read from this folder too. Without
    // it the console stopped on a black screen, the Wii Remotes cut off (a
    // tester's vWii, returning from a WiiWare game: the folder held the
    // game and the channel, never an IOS).
    if (channel.required_ios == 0) return true;
    // Titles 1 and 2 are boot2 and the Wii Menu, not IOSes: a damaged TMD
    // naming them copies no IOS.
    if (channel.required_ios < 3) {
        error = "the channel's TMD names IOS" + std::to_string(channel.required_ios) + ", which is not an IOS";
        return false;
    }
    const std::uint64_t ios = 0x0000000100000000ull | channel.required_ios;
    TmdContents ios_tmd;
    bool ios_copied = false;
    if (!copy_nand_title_to_emu(access, folder, ios, false, true, ios_tmd, ios_copied, error)) {
        error = "IOS" + std::to_string(channel.required_ios) + ", which the channel starts on, could not be copied: " +
                error;
        return false;
    }
    if (ios_copied) {
        logf("Return to %08x-%08x: IOS%u, which the channel starts on, was copied to %s too (%u contents).\n",
             static_cast<unsigned>(title >> 32), static_cast<unsigned>(title),
             static_cast<unsigned>(channel.required_ios), folder.c_str(),
             static_cast<unsigned>(ios_tmd.contents.size()));
    }
    return true;
}

bool read_nand_channel_banner(const NandChannel& channel, std::vector<std::uint8_t>& out, std::string& error) {
    // A banner with its sound is a few hundred KB; past this it is not one.
    constexpr u32 kMaxBannerBytes = 4u << 20;
    if (!channel.wad_path.empty()) {
        const TmdContent* banner = tmd_content_by_index(channel.tmd, 0);
        if (banner && !channel.wad_banner_cache.empty() &&
            read_card_file(channel.wad_banner_cache, kMaxBannerBytes, out, error) &&
            out.size() == banner->size && sha1(out.data(), out.size()) == banner->sha1) return true;
        const auto source = open_wad_source(channel.wad_path, error);
        WadMetadata metadata;
        if (!source || !read_wad_metadata(*source, metadata, error)) return false;
        std::uint8_t key[16] = {};
        const bool read = read_console_wad_banner(*source, metadata, key, out, error);
        clear_wad_key(key, sizeof key);
        return read;
    }
    if (channel.banner_path.empty()) {
        error = "it has no banner content";
        return false;
    }
    if (!channel.emu_nand.empty()) return read_card_file(channel.emu_nand + channel.banner_path, kMaxBannerBytes, out, error);
    if (!channel.content_bin.empty()) {
        const TmdContent* banner = tmd_content_by_index(channel.tmd, 0);
        MovedChannel moved;
        if (banner == nullptr || banner->size > kMaxBannerBytes) {
            error = "its banner has no size we can read";
            return false;
        }
        return moved.open(channel.content_bin, channel.title_id, error) &&
               moved.read(banner->id, banner->size, out, error);
    }
    NandAccess access;
    const s32 fd = access.open(channel.banner_path);
    if (fd < 0) {
        error = channel.banner_path + " could not be opened (" + std::to_string(fd) + ")";
        return false;
    }
    static fstats stats ATTRIBUTE_ALIGN(32);
    const u32 size = ISFS_GetFileStats(fd, &stats) < 0 ? 0 : stats.file_length;
    if (size == 0 || size > kMaxBannerBytes) {
        ISFS_Close(fd);
        error = channel.banner_path + " has no size we can read";
        return false;
    }
    AlignedBuffer buffer = aligned(size);
    const s32 got = buffer ? ISFS_Read(fd, buffer.get(), size) : -1;
    ISFS_Close(fd);
    if (got != static_cast<s32>(size)) {
        error = channel.banner_path + " read " + std::to_string(got) + " of " + std::to_string(size) + " bytes";
        return false;
    }
    if (!try_assign_range(out, buffer.get(), buffer.get() + size)) {
        error = "out of memory for " + channel.banner_path;
        return false;
    }
    return true;
}

bool read_channel_boot(std::uint64_t title_id, const std::string& emu_nand, ChannelBootFiles& out, std::string& error,
                       int ios_choice) {
    // A WiiWare game's program is a few megabytes (the shop sold games of
    // at most 40 MB in all); MEM1 holds 24 MB.
    constexpr u32 kMaxProgramBytes = 24u << 20;
    constexpr u32 kMaxCertBytes = 0x4000;
    ChannelBootFiles files;
    files.title_id = title_id;
    files.emu_nand = emu_nand;
    const bool emulated = !emu_nand.empty();
    logf("Wii channels: opening the Wii filesystem for the launch.\n");
    if (NandOpen() < 0) {
        error = "the Wii's memory could not be read";
        return false;
    }
    struct NandUser {
        ~NandUser() { NandClose(); }
    } nand_user;
    logf("Wii channels: reading the title metadata for %08x-%08x.\n",
         static_cast<unsigned>(title_id >> 32), static_cast<unsigned>(title_id));
    char title_dir[64];
    std::snprintf(title_dir, sizeof title_dir, "/title/%08x/%08x", static_cast<unsigned>(title_id >> 32),
                  static_cast<unsigned>(title_id & 0xFFFFFFFFu));
    if (emulated) {
        if (!read_card_file(emu_nand + title_dir + "/content/title.tmd", kMaxTmdBytes, files.tmd, error)) return false;
    } else {
        u32 tmd_size = 0;
        if (ES_GetStoredTMDSize(title_id, &tmd_size) < 0 || tmd_size < 0x1E4 || tmd_size > kMaxTmdBytes) {
            error = "its TMD could not be read";
            return false;
        }
        logf("Wii channels: reading %u bytes of title metadata through ES.\n", static_cast<unsigned>(tmd_size));
        AlignedBuffer tmd = aligned(tmd_size);
        if (!tmd || ES_GetStoredTMD(title_id, reinterpret_cast<signed_blob*>(tmd.get()), tmd_size) < 0) {
            error = "its TMD could not be read";
            return false;
        }
        files.tmd.assign(tmd.get(), tmd.get() + tmd_size);
    }
    TmdContents contents;
    if (!parse_tmd_contents(files.tmd.data(), files.tmd.size(), contents, error)) return false;
    if (contents.title_id != title_id) {
        error = "the TMD belongs to a different channel";
        return false;
    }
    files.required_ios = contents.required_ios;
    files.game_id = channel_game_id(channel_code(title_id), contents.maker);
    const auto add_ios = [&files](int ios) {
        if (std::find(files.ios_order.begin(), files.ios_order.end(), ios) == files.ios_order.end())
            files.ios_order.push_back(ios);
    };
    if (ios_choice != 0) add_ios(ios_choice);
    if (!running_in_dolphin()) {
        ImageGame as_image;
        as_image.id = files.game_id;
        as_image.required_ios = files.required_ios;
        // Keep image preferences within each group, but let the NAND's
        // path put SD-capable bases before base 38 (#437). The player's
        // explicit choice was added above and still keeps its place.
        const auto order = channel_cios_order(as_image, emu_nand);
        for (int slot : order) {
            std::string why;
            logf("Wii channels: checking the ticket and version for IOS%d.\n", slot);
            if (slot_has_ticket(slot) && d2x_slot_allowed(slot, why)) add_ios(slot);
        }
    }
    // A stock IOS cannot serve the copied NAND. After the d2x attempts,
    // reloading it would only repeat the launch with the content missing.
    // Real NAND channels still get this fallback. Dolphin has no d2x, so
    // there it stays for a copied NAND too: `debug_off = identify` starts
    // the game without the emulation, for testing.
    const bool stock_fallback = !emulated || running_in_dolphin();
    if (stock_fallback && files.required_ios != 0) add_ios(static_cast<int>(files.required_ios));
    if (emulated && files.ios_order.empty()) {
        error = "no installed d2x IOS is available to serve the copied NAND";
        return false;
    }

    NandAccess access;
    access.trace = true;
    // A channel the Wii Menu moved to the SD card: the Wii has no program
    // for it, and the program is read straight from the card's content.bin
    // (stage_moved_channel then copies the contents to the Wii for the
    // game's own reads).
    MovedChannel moved;
    bool from_card = false;
    if (!emulated) {
        const TmdContent* boot = tmd_content_by_index(contents, contents.boot_index);
        logf("Wii channels: checking whether the boot content is on the Wii.\n");
        const bool on_wii = program_present(contents, boot, [&](const TmdContent& c) {
            return open_result(access, nand_content_path(title_id, c.id)) >= 0;
        });
        logf("Wii channels: the boot content %s.\n", on_wii ? "is on the Wii" : "is missing; checking the SD card");
        const std::string moved_path = on_wii ? "" : moved_channel_file(channel_code(title_id));
        if (!moved_path.empty()) {
            logf("Wii channels: opening the moved channel at %s.\n", moved_path.c_str());
            if (!moved.open(moved_path, title_id, error)) {
                error = moved_path + ": " + error;
                return false;
            }
            from_card = true;
            logf("Wii channels: its program is read from %s, where the Wii Menu moved it\n", moved_path.c_str());
        }
    }
    // The first `bytes` of one of the title's own contents, or all of it.
    const auto read_content = [&](const TmdContent& c, std::size_t bytes, std::vector<u8>& out_bytes,
                                  std::string& why) {
        const std::string path = nand_content_path(title_id, c.id);
        logf("Wii channels: reading %s of content %u (%08x, %llu bytes) from %s.\n",
             bytes >= c.size ? "the whole file" : "the header", static_cast<unsigned>(c.index),
             static_cast<unsigned>(c.id), static_cast<unsigned long long>(c.size),
             emulated ? "the copied NAND" : from_card ? "the SD card" : "the Wii");
        LogSync();
        if (emulated) {
            if (bytes >= c.size) return read_card_file(emu_nand + path, static_cast<u32>(bytes), out_bytes, why);
            return read_card_head(emu_nand + path, bytes, out_bytes);
        }
        if (from_card) return moved.has(c.id) && moved.read(c.id, bytes, out_bytes, why);
        if (bytes >= c.size) return read_whole(access, path, static_cast<u32>(bytes), out_bytes, why);
        return read_head(access, path, static_cast<u32>(bytes), out_bytes);
    };
    std::vector<ContentStart> starts;
    for (const TmdContent& c : contents.contents) {
        if (c.index == 0 || (c.type & 0x8000) != 0) continue;
        ContentStart start;
        start.index = c.index;
        start.size = c.size;
        // Enough for an ELF's program headers too (parse_channel_program).
        const std::size_t head = static_cast<std::size_t>(std::min<std::uint64_t>(kProgramHeadBytes, c.size));
        std::string ignored;
        if (read_content(c, head, start.head, ignored)) starts.push_back(start);
    }
    // A header probe ranks candidates; only a complete unpack and parse
    // below can commit the choice. Reject a bad stream and try another.
    std::vector<ContentStart> candidates = starts;
    bool loaded = false;
    std::string why;
    for (int attempt = 0; attempt < 3 && !loaded; ++attempt) {
        logf("Wii channels: choosing the program from %u content header(s), attempt %d.\n",
             static_cast<unsigned>(candidates.size()), attempt + 1);
        if (!choose_channel_program(contents, candidates, files.program, why)) {
            error = why;
            return false;
        }
        const TmdContent* program = tmd_content_by_index(contents, files.program.index);
        if (!program) return false;
        if (program->size > kMaxProgramBytes) {
            error = "its program is " + std::to_string(program->size) + " bytes, more than MEM1 holds";
            return false;
        }
        if (!read_content(*program, kMaxProgramBytes, files.dol, error)) {
            if (error.empty()) error = "its program could not be read";
            return false;
        }
        const bool compressed = channel_program_compressed(files.dol.data(), files.dol.size());
        if (compressed) {
            const std::size_t packed_size = files.dol.size();
            logf("Wii channels: unpacking the program in content %u (%u bytes).\n",
                 static_cast<unsigned>(files.program.index), static_cast<unsigned>(packed_size));
            if (!unpack_channel_program(files.dol, why)) {
                logf("Wii channels: content %d is compressed and %s; not a program\n",
                     static_cast<int>(files.program.index), why.c_str());
                candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                                [&](const ContentStart& c) {
                                                    return c.index == files.program.index;
                                                }),
                                 candidates.end());
                continue;
            }
            DolHeader dol;
            std::string format, parse_why;
            if (!parse_channel_program(files.dol.data(), files.dol.size(), dol, format, parse_why)) {
                logf("Wii channels: content %d unpacks to something that is not a program (%s)\n",
                     static_cast<int>(files.program.index), parse_why.c_str());
                candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                                [&](const ContentStart& c) {
                                                    return c.index == files.program.index;
                                                }),
                                 candidates.end());
                continue;
            }
            files.program.dol = dol;
            files.program.format = format;
            logf("Wii channels: content %d is LZ77-compressed (%u bytes); unpacked for the game (%u bytes, %s)\n",
                 static_cast<int>(files.program.index), static_cast<unsigned>(packed_size),
                 static_cast<unsigned>(files.dol.size()), files.program.format.c_str());
        }
        if (files.dol.size() < files.program.dol.image_size()) {
            error = "its program is shorter than its own header says";
            return false;
        }
        loaded = true;
    }
    if (!loaded) {
        error = "none of its contents is a program RiftWii can load";
        return false;
    }
    // Prefer the installed title's own ticket. The synthetic identity is
    // a fallback when a copied NAND lacks it or ES refuses it at launch;
    // IOS may still refuse the synthetic ticket too.
    constexpr u32 kMaxTicketBytes = 0x1000;
    char ticket_path[64];
    std::snprintf(ticket_path, sizeof(ticket_path), "/ticket/%08x/%08x.tik",
                  static_cast<unsigned>(title_id >> 32), static_cast<unsigned>(title_id));
    std::string ticket_error;
    logf("Wii channels: reading the installed ticket at %s.\n", ticket_path);
    const bool ticket_read = emulated
        ? read_card_file(emu_nand + ticket_path, kMaxTicketBytes, files.ticket, ticket_error)
        : read_whole(access, ticket_path, kMaxTicketBytes, files.ticket, ticket_error);
    if (!ticket_read) {
        logf("Wii channels: the installed ticket could not be read (%s); trying a synthetic identity.\n",
             ticket_error.c_str());
        files.ticket = riftwii::make_identify_ticket({}, title_id);
        files.ticket_synthetic = true;
    } else if (files.ticket.size() > kSignedTicketBytes) {
        // A .tik can hold several tickets; ES_Identify takes exactly one
        // (libogc refuses any other size before asking IOS).
        logf("Wii channels: the ticket file holds %u bytes; the first ticket (%u bytes) is used.\n",
             static_cast<unsigned>(files.ticket.size()), static_cast<unsigned>(kSignedTicketBytes));
        files.ticket.resize(kSignedTicketBytes);
    }
    // The certificates are the same on every Wii: an emulated NAND's own
    // copy when it has one, else the Wii's.
    logf("Wii channels: reading the certificates.\n");
    std::string ignored;
    if (!emulated || !read_card_file(emu_nand + "/sys/cert.sys", kMaxCertBytes, files.certs, ignored)) {
        if (!read_whole(access, "/sys/cert.sys", kMaxCertBytes, files.certs, error)) return false;
    }
    logf("Wii channels: the program, ticket and certificates are ready.\n");
    out = std::move(files);
    return true;
}

namespace {
std::uint64_t g_channel_to_start = 0;
std::string g_channel_nand;
int g_channel_ios = 0;
bool g_channel_direct = false;
bool g_channel_wfs = false;
}  // namespace

void SetChannelToStart(const NandChannel& channel) {
    g_channel_to_start = channel.title_id;
    g_channel_nand = channel.emu_nand;
    g_channel_wfs = channel.kind.kind == ChannelKind::Wfs;
}
std::uint64_t ChannelToStart() { return g_channel_to_start; }
const std::string& ChannelToStartNand() { return g_channel_nand; }
void SetChannelToStartNand(const std::string& folder) { g_channel_nand = folder; }
void SetChannelToStartIos(int ios) { g_channel_ios = ios; }
int ChannelToStartIos() { return g_channel_ios; }
void SetChannelToStartDirect(bool direct) { g_channel_direct = direct; }
bool ChannelToStartDirect() { return g_channel_direct; }
bool ChannelToStartWfs() { return g_channel_wfs; }

std::string nand_channel_name(const NandChannel& channel, int language) {
    // GameTDB's name first, as for disc games: it is in the menu's language.
    if (!channel.listed_name.empty()) return channel.listed_name;
    if (language >= 0 && language < kBnrLanguages && !channel.names[language].empty()) return channel.names[language];
    if (!channel.names[kBnrEnglish].empty()) return channel.names[kBnrEnglish];
    for (const std::string& name : channel.names)
        if (!name.empty()) return name;
    return channel.code;
}

PriiloaderFound PriiloaderInstalled() {
    static PriiloaderFound found = PriiloaderFound::Unknown;
    static bool looked = false;
    if (looked) return found;
    looked = true;
    constexpr u64 kWiiMenuTitle = 0x0000000100000002ull;
    TmdContents wii_menu;
    std::string why;
    if (!read_stored_tmd(kWiiMenuTitle, wii_menu, why)) {
        logf("Priiloader: not known, the Wii Menu's TMD could not be read\n");
        return found;
    }
    const std::string copy = priiloader_copy_path(wii_menu);
    if (copy.empty()) {
        logf("Priiloader: not known, the Wii Menu's TMD names no program\n");
        return found;
    }
    // The cIOS scan may have released the last filesystem user already.
    // This lookup owns an acquisition even if the channel scan failed.
    if (NandOpen() < 0) {
        logf("Priiloader: not known, the Wii's filesystem could not be opened.\n");
        return found;
    }
    struct NandUser {
        ~NandUser() { NandClose(); }
    } nand_user;
    NandAccess access;
    const s32 fd = access.open(copy);
    if (fd >= 0) {
        ISFS_Close(fd);
        found = PriiloaderFound::Yes;
        logf("Priiloader: installed (%s is there)\n", copy.c_str());
    } else if (fd == -106) {  // no such file
        found = PriiloaderFound::No;
        logf("Priiloader: not installed (no %s)\n", copy.c_str());
    } else {
        logf("Priiloader: not known, %s could not be looked for (%d)\n", copy.c_str(), static_cast<int>(fd));
    }
    return found;
}

}  // namespace riftwii::wii
