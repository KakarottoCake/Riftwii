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
#include "usbcatalog.hpp"
#include "nanduse.hpp"

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

// A whole NAND file of at most `max_bytes`, or false with `error`.
bool read_whole(NandAccess& access, const std::string& path, u32 max_bytes, std::vector<u8>& out,
                std::string& error) {
    const s32 fd = access.open(path);
    if (fd < 0) {
        error = path + " could not be opened (" + std::to_string(fd) + ")";
        return false;
    }
    static fstats stats ATTRIBUTE_ALIGN(32);
    const u32 size = ISFS_GetFileStats(fd, &stats) < 0 ? 0 : stats.file_length;
    if (size == 0 || size > max_bytes) {
        ISFS_Close(fd);
        error = path + " is " + std::to_string(size) + " bytes, not a size RiftWii reads";
        return false;
    }
    AlignedBuffer buffer = aligned(size);
    const s32 got = buffer ? ISFS_Read(fd, buffer.get(), size) : -1;
    ISFS_Close(fd);
    if (got != static_cast<s32>(size)) {
        error = path + " read " + std::to_string(got) + " of " + std::to_string(size) + " bytes";
        return false;
    }
    out.assign(buffer.get(), buffer.get() + size);
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
    std::vector<u8> bytes(static_cast<std::size_t>(size));
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
// content's up to kForwarderMaxBytes, the others' a program header's worth.
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
    const ChannelVerdict verdict = judge_unlisted_channel(channel.code, channel.tmd, starts);
    if (!verdict.left_out) return false;
    std::string name;
    for (const std::string& banner_name : channel.names)
        if (name.empty()) name = banner_name;
    logf("Wii channels: %s%s%s%s left out%s: %s\n", channel.code.c_str(), name.empty() ? "" : " (",
         name.c_str(), name.empty() ? "" : ")", where.c_str(), verdict.reason.c_str());
    return true;
}

bool card_file_exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && !S_ISDIR(st.st_mode);
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
    unsigned unreadable = 0, missing = 0, unlisted = 0;
    for (u32 i = 0; i < count; ++i) {
        NandChannel channel;
        channel.title_id = titles[i];
        channel.kind = classify_channel(channel.title_id);
        if (channel.kind.kind == ChannelKind::None) continue;
        channel.code = channel_code(channel.title_id);
        // Homebrew forwarders take Shop-like codes too (Nintendont's is
        // WWND), and custom injects take made-up ones GameTDB does not list.
        // A code its list lacks is judged below from the title itself.
        const bool not_in_list = GameTitleListed(channel.code, channel.listed_name) && channel.listed_name.empty();
        std::string why;
        if (!read_stored_tmd(channel.title_id, channel.tmd, why)) {
            logf("Wii channels: %s left out: %s\n", channel.code.c_str(), why.c_str());
            ++unreadable;
            continue;
        }
        channel.game_id = channel_game_id(channel.code, channel.tmd.maker);
        const TmdContent* banner = tmd_content_by_index(channel.tmd, 0);
        const TmdContent* program = tmd_content_by_index(channel.tmd, channel.tmd.boot_index);
        if (banner) {
            channel.banner_path = nand_content_path(channel.title_id, banner->id);
            std::vector<u8> head;
            if (read_head(access, channel.banner_path, kBannerHeadBytes, head))
                parse_banner_names(head.data(), head.size(), channel.names);
        }
        if (not_in_list) {
            const HeadReader read = [&](const std::string& path, std::size_t bytes, std::vector<u8>& head) {
                return read_head(access, path, static_cast<u32>(bytes), head);
            };
            if (leave_out_unlisted(channel, read, "")) {
                ++unlisted;
                continue;
            }
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
    if (unlisted) status += "; " + std::to_string(unlisted) + " unlisted channel(s) left out";
    if (unreadable) status += "; " + std::to_string(unreadable) + " unreadable title(s) left out";
    if (access.refused) status += "; the Wii refused to show their files, so they have no names";
    status += ".";
    logf("Wii channels: %s\n", status.c_str());
    return true;
}

bool list_emu_channels(const std::string& folder, std::vector<NandChannel>& out, std::string& status) {
    const std::string titles = folder + "/title/00010001";
    DIR* dir = opendir(titles.c_str());
    if (dir == nullptr) {
        status = "No channels in " + folder + ".";
        logf("Wii channels: %s\n", status.c_str());
        return false;
    }
    std::size_t found = 0;
    unsigned unreadable = 0, unlisted = 0, twice = 0, instead = 0;
    while (const dirent* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name.size() != 8 || name.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) continue;
        NandChannel channel;
        channel.emu_nand = folder;
        channel.title_id = (std::uint64_t(kDownloadedChannels) << 32) | std::strtoul(name.c_str(), nullptr, 16);
        channel.kind = classify_channel(channel.title_id);
        if (channel.kind.kind == ChannelKind::None) continue;
        channel.code = channel_code(channel.title_id);
        const bool not_in_list = GameTitleListed(channel.code, channel.listed_name) && channel.listed_name.empty();
        // The Wii's own copy, when it can start, is the one shown. One whose
        // game data was removed from the Wii (it keeps only the ticket, the
        // TMD and the save; a tester had 30 of 31 so, all whole in the
        // emulated NAND) gives way to this copy, which can.
        const auto wii_copy = std::find_if(out.begin(), out.end(),
                                           [&](const NandChannel& c) { return c.title_id == channel.title_id; });
        if (wii_copy != out.end() && wii_copy->playable) {
            ++twice;
            continue;
        }
        // The stored TMD, as the Wii keeps it.
        std::vector<u8> tmd;
        std::string why;
        if (!read_card_file(titles + "/" + name + "/content/title.tmd", kMaxTmdBytes, tmd, why) ||
            !parse_tmd_contents(tmd.data(), tmd.size(), channel.tmd, why)) {
            logf("Wii channels: %s in %s left out: %s\n", channel.code.c_str(), folder.c_str(), why.c_str());
            ++unreadable;
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
                ++unlisted;
                continue;
            }
        }
        channel.playable = program && !(program->type & 0x8000) &&
                           card_file_exists(folder + nand_content_path(channel.title_id, program->id));
        if (wii_copy != out.end()) {
            if (!channel.playable) {
                ++twice;  // neither copy can start: the Wii's stays, with its note
                continue;
            }
            *wii_copy = std::move(channel);  // nothing was added to `out` since the search
            ++instead;
            ++found;
            continue;
        }
        out.push_back(std::move(channel));
        ++found;
    }
    closedir(dir);
    std::sort(out.begin(), out.end(), [](const NandChannel& a, const NandChannel& b) { return a.code < b.code; });
    status = std::to_string(found) + " WiiWare and Virtual Console game(s) in " + folder;
    if (twice) status += "; " + std::to_string(twice) + " the Wii has itself listed once";
    if (instead)
        status += "; " + std::to_string(instead) + " shown from here instead of the Wii's copy, whose game data was removed";
    if (unlisted) status += "; " + std::to_string(unlisted) + " unlisted channel(s) left out";
    if (unreadable) status += "; " + std::to_string(unreadable) + " unreadable title(s) left out";
    status += ".";
    logf("Wii channels: %s\n", status.c_str());
    return found != 0;
}

bool read_nand_channel_banner(const NandChannel& channel, std::vector<std::uint8_t>& out, std::string& error) {
    // A banner with its sound is a few hundred KB; past this it is not one.
    constexpr u32 kMaxBannerBytes = 4u << 20;
    if (channel.banner_path.empty()) {
        error = "it has no banner content";
        return false;
    }
    if (!channel.emu_nand.empty()) return read_card_file(channel.emu_nand + channel.banner_path, kMaxBannerBytes, out, error);
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
    out.assign(buffer.get(), buffer.get() + size);
    return true;
}

bool read_channel_boot(std::uint64_t title_id, const std::string& emu_nand, ChannelBootFiles& out, std::string& error) {
    // A WiiWare game's program is a few megabytes (the shop sold games of
    // at most 40 MB in all); MEM1 holds 24 MB.
    constexpr u32 kMaxProgramBytes = 24u << 20;
    constexpr u32 kMaxTicketBytes = 0x1000;  // 0x2A4, one ticket
    constexpr u32 kMaxCertBytes = 0x4000;    // /sys/cert.sys is 0xA00
    ChannelBootFiles files;
    files.title_id = title_id;
    files.emu_nand = emu_nand;
    const bool emulated = !emu_nand.empty();
    if (NandOpen() < 0) {
        error = "the Wii's memory could not be read";
        return false;
    }
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
        AlignedBuffer tmd = aligned(tmd_size);
        if (!tmd || ES_GetStoredTMD(title_id, reinterpret_cast<signed_blob*>(tmd.get()), tmd_size) < 0) {
            error = "its TMD could not be read";
            return false;
        }
        files.tmd.assign(tmd.get(), tmd.get() + tmd_size);
    }
    TmdContents contents;
    if (!parse_tmd_contents(files.tmd.data(), files.tmd.size(), contents, error)) return false;
    files.required_ios = contents.required_ios;
    files.game_id = channel_game_id(channel_code(title_id), contents.maker);
    if (!running_in_dolphin()) {
        ImageGame as_image;
        as_image.id = files.game_id;
        as_image.required_ios = files.required_ios;
        for (int slot : image_cios_order(as_image, 0)) {
            std::string why;
            if (slot_has_ticket(slot) && d2x_slot_allowed(slot, why)) files.ios_order.push_back(slot);
        }
    }
    if (files.required_ios != 0) files.ios_order.push_back(static_cast<int>(files.required_ios));

    NandAccess access;
    std::vector<ContentStart> starts;
    for (const TmdContent& c : contents.contents) {
        if (c.index == 0 || (c.type & 0x8000) != 0) continue;
        ContentStart start;
        start.index = c.index;
        start.size = c.size;
        const std::string path = nand_content_path(title_id, c.id);
        // Enough for an ELF's program headers too (parse_channel_program).
        const std::size_t head = static_cast<std::size_t>(std::min<std::uint64_t>(kProgramHeadBytes, c.size));
        const bool read = emulated ? read_card_head(emu_nand + path, head, start.head)
                                   : read_head(access, path, head, start.head);
        if (read) starts.push_back(start);
    }
    if (!choose_channel_program(contents, starts, files.program, error)) return false;
    const TmdContent* program = tmd_content_by_index(contents, files.program.index);
    if (!program) return false;
    const std::string program_path = nand_content_path(title_id, program->id);
    const bool program_read = emulated ? read_card_file(emu_nand + program_path, kMaxProgramBytes, files.dol, error)
                                       : read_whole(access, program_path, kMaxProgramBytes, files.dol, error);
    if (!program_read) return false;
    if (files.dol.size() < files.program.dol.image_size()) {
        error = "its program is shorter than its own header says";
        return false;
    }
    char ticket_path[64];
    std::snprintf(ticket_path, sizeof ticket_path, "/ticket/%08x/%08x.tik", static_cast<unsigned>(title_id >> 32),
                  static_cast<unsigned>(title_id & 0xFFFFFFFFu));
    const bool ticket_read = emulated ? read_card_file(emu_nand + ticket_path, kMaxTicketBytes, files.ticket, error)
                                      : read_whole(access, ticket_path, kMaxTicketBytes, files.ticket, error);
    if (!ticket_read) return false;
    // The certificates are the same on every Wii: an emulated NAND's own
    // copy when it has one, else the Wii's.
    std::string ignored;
    if (!emulated || !read_card_file(emu_nand + "/sys/cert.sys", kMaxCertBytes, files.certs, ignored)) {
        if (!read_whole(access, "/sys/cert.sys", kMaxCertBytes, files.certs, error)) return false;
    }
    out = std::move(files);
    return true;
}

namespace {
std::uint64_t g_channel_to_start = 0;
std::string g_channel_nand;
}  // namespace

void SetChannelToStart(const NandChannel& channel) {
    g_channel_to_start = channel.title_id;
    g_channel_nand = channel.emu_nand;
}
std::uint64_t ChannelToStart() { return g_channel_to_start; }
const std::string& ChannelToStartNand() { return g_channel_nand; }

std::string nand_channel_name(const NandChannel& channel, int language) {
    // GameTDB's name first, as for disc games: it is in the menu's language.
    if (!channel.listed_name.empty()) return channel.listed_name;
    if (language >= 0 && language < kBnrLanguages && !channel.names[language].empty()) return channel.names[language];
    if (!channel.names[kBnrEnglish].empty()) return channel.names[kBnrEnglish];
    for (const std::string& name : channel.names)
        if (!name.empty()) return name;
    return channel.code;
}

}  // namespace riftwii::wii
