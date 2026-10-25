// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "movedchannels.hpp"

#include <gccore.h>
#include <malloc.h>

#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

#include "boot.hpp"
#include "log.hpp"
#include "nanduse.hpp"
#include "otpkey.hpp"
#include "riftwii/checkedalloc.hpp"

namespace riftwii::wii {
namespace {

// Read, decrypted and written this much at a time.
constexpr std::size_t kChunkBytes = 64 * 1024;
// A content.bin is a channel's contents (the Shop's biggest were a few
// hundred MB); past this its offsets would not fit the card's file API.
constexpr long kMaxFileBytes = 0x7FFFFFFF;
// BootMii puts a Wii's keys here when it backs up its NAND.
constexpr const char* kBootMiiKeys = "sd:/keys.bin";
constexpr const char* kStagedList = "sd:/riftwii/staged_channels.txt";
// Older rewrites left a backup here. Both copies are read and merged
// before a replacement is published, so neither copy can lose a path.
constexpr const char* kStagedNew = "sd:/riftwii/staged_channels.new";
constexpr const char* kStagedOld = "sd:/riftwii/staged_channels.old";
// Only a complete, closed .new is renamed here. At start this publication
// is finished before another rewrite or append can change the list.
constexpr const char* kStagedReady = "sd:/riftwii/staged_channels.ready";
constexpr std::size_t kMaxKeysBytes = 0x400;
constexpr std::size_t kMaxStagedListBytes = 64 * 1024;
// Read older lists that grew past the append limit, without accepting an
// unbounded file from the card. A failed read leaves every copy intact.
constexpr long kMaxStagedReadBytes = 4 * 1024 * 1024;
// The FS attribute byte of each content file staging creates. ES makes the
// ones it installs (the Wii Menu moving a channel back among them) with 0,
// so a file with this one is RiftWii's copy: the cleanup removes no other,
// whatever the list on the card names.
constexpr u8 kStagedAttribute = 0x52;

using AlignedBuffer = std::unique_ptr<u8, decltype(&std::free)>;
AlignedBuffer aligned(std::size_t bytes) {
    return AlignedBuffer(static_cast<u8*>(memalign(32, (bytes + 31) & ~std::size_t(31))), &std::free);
}

bool read_at(FILE* file, std::uint64_t offset, std::uint8_t* out, std::size_t length) {
    return std::fseek(file, static_cast<long>(offset), SEEK_SET) == 0 && std::fread(out, 1, length, file) == length;
}

// The key for a content.bin written by the console `console_id`.
bool key_for(std::uint32_t console_id, BackupKey& out, std::string& error) {
    BackupKey own;
    const bool have_own = ConsoleBackupKey(own);
    if (have_own && own.console_id == console_id) {
        out = own;
        return true;
    }
    if (FILE* file = std::fopen(kBootMiiKeys, "rb")) {
        std::uint8_t bytes[kMaxKeysBytes];
        const std::size_t got = std::fread(bytes, 1, sizeof bytes, file);
        std::fclose(file);
        BackupKey other;
        if (parse_bootmii_keys(bytes, got, other) && other.console_id == console_id) {
            out = other;
            other = BackupKey{};
            return true;
        }
    }
    own = BackupKey{};
    char text[200];
    if (!have_own) {
        std::snprintf(text, sizeof text,
                      "this Wii's key could not be read, and %s is not the key of the Wii that moved it (%08x)",
                      kBootMiiKeys, static_cast<unsigned>(console_id));
    } else {
        std::snprintf(text, sizeof text,
                      "another Wii (console %08x) moved it to the card, and %s is not that Wii's key",
                      static_cast<unsigned>(console_id), kBootMiiKeys);
    }
    error = text;
    return false;
}

// ISFS wants its path in memory it can reach: a 32-byte-aligned buffer.
const char* isfs_path(const std::string& path) {
    static char name[64] ATTRIBUTE_ALIGN(32);
    std::snprintf(name, sizeof name, "%s", path.c_str());
    return name;
}

// The size of a NAND file, or -1 when it is not there.
s64 nand_file_size(const std::string& path) {
    const s32 fd = ISFS_Open(isfs_path(path), ISFS_OPEN_READ);
    if (fd < 0) return -1;
    static fstats stats ATTRIBUTE_ALIGN(32);
    const s64 size = ISFS_GetFileStats(fd, &stats) < 0 ? -1 : static_cast<s64>(stats.file_length);
    ISFS_Close(fd);
    return size;
}

// Whether the NAND file is a copy staging made (kStagedAttribute). 1 when
// it is, 0 when it is another's, -106 when it is not there, else an error.
s32 staged_by_riftwii(const std::string& path) {
    u32 owner = 0;
    u16 group = 0;
    u8 attribute = 0, owner_perm = 0, group_perm = 0, other_perm = 0;
    const s32 result = ISFS_GetAttr(isfs_path(path), &owner, &group, &attribute, &owner_perm, &group_perm, &other_perm);
    if (result < 0) return result;
    return attribute == kStagedAttribute ? 1 : 0;
}

// False when the list could not be written: the file must then not be
// made, or nothing would remove it again.
bool append_staged(const std::string& nand_path, std::string& error) {
    struct stat pending;
    if (stat(kStagedReady, &pending) == 0) {
        error = "the SD card channels' cleanup list could not be published; start RiftWii again to retry";
        return false;
    }
    FILE* list = std::fopen(kStagedList, "a+b");
    if (!list) {
        error = std::string(kStagedList) + " could not be written (is the card locked or full?)";
        return false;
    }
    const bool sought = std::fseek(list, 0, SEEK_END) == 0;
    const long size = sought ? std::ftell(list) : -1;
    const std::size_t entry_bytes = nand_path.size() + 1;
    if (size < 0 || entry_bytes > kMaxStagedListBytes ||
        static_cast<unsigned long>(size) > kMaxStagedListBytes - entry_bytes) {
        std::fclose(list);
        error = "the SD card channels' cleanup list is full or could not be sized; no new copy was made";
        return false;
    }
    // A failed earlier append may have left half a line. Do not join a
    // new path to it, since cleanup would not recognize the combined line.
    if (size > 0 && (std::fseek(list, -1, SEEK_END) != 0 || std::fgetc(list) != '\n')) {
        std::fclose(list);
        error = "the SD card channels' cleanup list has an unfinished line; start RiftWii again to retry";
        return false;
    }
    if (std::fseek(list, 0, SEEK_END) != 0) {
        std::fclose(list);
        error = "the SD card channels' cleanup list could not be written";
        return false;
    }
    const bool wrote = std::fprintf(list, "%s\n", nand_path.c_str()) > 0;
    const bool closed = std::fclose(list) == 0;
    if (!wrote || !closed) {
        error = std::string(kStagedList) + " could not be written (is the card locked or full?)";
        return false;
    }
    return true;
}

// Adds the paths in a staged list to `paths`, each once.
bool read_staged(const char* file, std::vector<std::string>& paths) {
    FILE* list = std::fopen(file, "rb");
    if (!list) return errno == ENOENT;
    const bool sought = std::fseek(list, 0, SEEK_END) == 0;
    const long size = sought ? std::ftell(list) : -1;
    if (size < 0 || size > kMaxStagedReadBytes || std::fseek(list, 0, SEEK_SET) != 0) {
        std::fclose(list);
        return false;
    }
    // A valid path is 45 bytes without its newline. Reserve enough for
    // this file before building a list whose size comes from the card.
    if (!try_reserve(paths, paths.size() + static_cast<std::size_t>(size) / 45 + 1)) {
        std::fclose(list);
        return false;
    }
    char line[128];
    while (std::fgets(line, sizeof line, list)) {
        std::string path = line;
        while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) path.pop_back();
        std::uint64_t title_id = 0;
        std::uint32_t content_id = 0;
        if (parse_staged_content_path(path, title_id, content_id) &&
            std::find(paths.begin(), paths.end(), path) == paths.end()) paths.push_back(path);
    }
    const bool read = std::ferror(list) == 0;
    const bool closed = std::fclose(list) == 0;
    return read && closed;
}

bool file_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

// Finish publishing a complete replacement. Keep the old backup until the
// replacement has its final name; .ready survives a failed rename or a cut
// in power after the main list was removed.
bool publish_staged() {
    if (!file_exists(kStagedReady)) return true;
    if (file_exists(kStagedList) && std::remove(kStagedList) != 0) return false;
    if (std::rename(kStagedReady, kStagedList) != 0) return false;
    return !file_exists(kStagedOld) || std::remove(kStagedOld) == 0;
}

// Both input lists have already been read and merged, and `paths` holds
// every entry still needed. Mark the checked replacement as ready before
// touching either input, so an interrupted rewrite can always finish.
bool write_staged(const std::vector<std::string>& paths) {
    if (!publish_staged()) return false;
    std::size_t bytes = 0;
    for (const std::string& path : paths) {
        const std::size_t entry_bytes = path.size() + 1;
        if (entry_bytes > static_cast<std::size_t>(kMaxStagedReadBytes) - bytes) return false;
        bytes += entry_bytes;
    }
    FILE* list = std::fopen(kStagedNew, "wb");
    if (!list) return false;
    bool wrote = true;
    for (const std::string& path : paths) {
        if (std::fprintf(list, "%s\n", path.c_str()) <= 0) {
            wrote = false;
            break;
        }
    }
    if (std::fclose(list) != 0) wrote = false;
    if (!wrote) {
        std::remove(kStagedNew);  // the list as it was stays
        return false;
    }
    if (std::rename(kStagedNew, kStagedReady) != 0) {
        std::remove(kStagedNew);
        return false;
    }
    if (!publish_staged()) return false;
    if (paths.empty()) std::remove(kStagedList);
    return true;
}

// The TMD the Wii keeps for the title, as ES gives it.
bool read_stored_tmd(std::uint64_t title_id, TmdContents& out, std::string& error) {
    constexpr u32 kMaxTmdBytes = 0x10000;
    u32 size = 0;
    if (ES_GetStoredTMDSize(title_id, &size) < 0 || size < 0x1E4 || size > kMaxTmdBytes) {
        error = "the Wii's TMD for it could not be read";
        return false;
    }
    AlignedBuffer tmd = aligned(size);
    if (!tmd || ES_GetStoredTMD(title_id, reinterpret_cast<signed_blob*>(tmd.get()), size) < 0) {
        error = "the Wii's TMD for it could not be read";
        return false;
    }
    return parse_tmd_contents(tmd.get(), size, out, error);
}

// Whether each of the title's own contents is on the Wii at its full
// size, so it starts without the card's copy.
bool title_contents_on_wii(std::uint64_t title_id) {
    TmdContents tmd;
    std::string ignored;
    if (!read_stored_tmd(title_id, tmd, ignored)) return false;
    for (const TmdContent& c : tmd.contents) {
        if ((c.type & 0x8000) != 0) continue;  // shared: in /shared1, never moved
        if (nand_file_size(nand_content_path(title_id, c.id)) != static_cast<s64>(c.size)) return false;
    }
    return true;
}

}  // namespace

std::string moved_channel_file(const std::string& code) {
    if (code.size() != 4) return "";
    const std::string path = "sd:" + content_bin_path(code);
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) return "";
    return path;
}

MovedChannel::~MovedChannel() {
    if (file_) std::fclose(file_);
    key_ = BackupKey{};
}

bool MovedChannel::open(const std::string& path, std::uint64_t title_id, std::string& error) {
    file_ = std::fopen(path.c_str(), "rb");
    if (!file_) {
        error = path + " could not be opened";
        return false;
    }
    path_ = path;
    std::fseek(file_, 0, SEEK_END);
    const long file_size = std::ftell(file_);
    if (file_size <= 0 || file_size >= kMaxFileBytes) {
        error = "it is not a size a content.bin has";
        return false;
    }
    std::vector<std::uint8_t> start;
    const std::size_t search = std::min<std::size_t>(kContentBinSearchBytes, static_cast<std::size_t>(file_size));
    if (!try_resize(start, search)) {
        error = "out of memory for " + path;
        return false;
    }
    ContentBinLayout layout;
    if (!read_at(file_, 0, start.data(), start.size()) ||
        !find_content_bin_layout(start.data(), start.size(), static_cast<std::uint64_t>(file_size), layout, error)) {
        if (error.empty()) error = path + " could not be read";
        return false;
    }
    std::vector<std::uint8_t> tmd_bytes;
    if (!try_resize(tmd_bytes, static_cast<std::size_t>(layout.tmd_size))) {
        error = "out of memory for " + path;
        return false;
    }
    if (!read_at(file_, layout.tmd_offset, tmd_bytes.data(), tmd_bytes.size()) ||
        !parse_tmd_contents(tmd_bytes.data(), tmd_bytes.size(), tmd_, error)) {
        if (error.empty()) error = "its TMD could not be read";
        return false;
    }
    if (tmd_.title_id != title_id) {
        char text[96];
        std::snprintf(text, sizeof text, "it holds title %08x-%08x, not this channel",
                      static_cast<unsigned>(tmd_.title_id >> 32), static_cast<unsigned>(tmd_.title_id));
        error = text;
        return false;
    }
    // The Wii's TMD is the one the game is started by: a content.bin from
    // before the channel was updated, or of another copy, is not used.
    TmdContents installed;
    if (!read_stored_tmd(title_id, installed, error) || !content_bin_matches_tmd(tmd_, installed, error)) return false;
    if (!content_bin_places(layout, tmd_, places_, error)) return false;
    return key_for(layout.console_id, key_, error);
}

const WadContentPlace* MovedChannel::place_of(std::uint32_t content_id) const {
    for (const WadContentPlace& place : places_)
        if (place.content.id == content_id) return &place;
    return nullptr;
}

bool MovedChannel::has(std::uint32_t content_id) const { return place_of(content_id) != nullptr; }

bool MovedChannel::stream(std::uint32_t content_id,
                          const std::function<bool(const std::uint8_t*, std::size_t)>& sink, std::string& error) {
    const WadContentPlace* place = place_of(content_id);
    char name[32];
    std::snprintf(name, sizeof name, "content %08x", static_cast<unsigned>(content_id));
    if (place == nullptr) {
        error = std::string(name) + " is not in " + path_;
        return false;
    }
    std::vector<std::uint8_t> chunk;
    if (!try_resize(chunk, kChunkBytes)) {
        error = "out of memory for " + path_;
        return false;
    }
    WadContentDecrypter decrypter(key_.prng, place->content);
    for (std::uint64_t done = 0; done < place->stored;) {
        const std::size_t piece = static_cast<std::size_t>(std::min<std::uint64_t>(kChunkBytes, place->stored - done));
        if (!read_at(file_, place->offset + done, chunk.data(), piece)) {
            error = path_ + " could not be read";
            return false;
        }
        const std::size_t own = decrypter.decrypt(chunk.data(), piece);
        if (own != 0 && !sink(chunk.data(), own)) {
            if (error.empty()) error = std::string(name) + " could not be written";
            return false;
        }
        done += piece;
    }
    if (!decrypter.matches()) {
        error = std::string(name) + " does not match its hash (a damaged content.bin)";
        return false;
    }
    return true;
}

bool MovedChannel::read(std::uint32_t content_id, std::uint64_t bytes, std::vector<std::uint8_t>& out,
                        std::string& error) {
    const WadContentPlace* place = place_of(content_id);
    if (place == nullptr) {
        char text[64];
        std::snprintf(text, sizeof text, "content %08x is not in the file", static_cast<unsigned>(content_id));
        error = text;
        return false;
    }
    if (bytes >= place->content.size) {
        std::vector<std::uint8_t> whole;
        if (!try_resize(whole, static_cast<std::size_t>(place->content.size))) {
            error = "out of memory for " + path_;
            return false;
        }
        std::size_t at = 0;
        const bool read = stream(content_id, [&](const std::uint8_t* data, std::size_t length) {
            std::memcpy(whole.data() + at, data, length);
            at += length;
            return true;
        }, error);
        if (!read) return false;
        out.swap(whole);
        return true;
    }
    // A head only: CBC decrypts from the start without the rest, and the
    // hash, over the whole content, is left to the full reads.
    const std::size_t stored = static_cast<std::size_t>((bytes + 15) & ~std::uint64_t(15));
    std::vector<std::uint8_t> head;
    if (!try_resize(head, stored) || !read_at(file_, place->offset, head.data(), head.size())) {
        error = path_ + " could not be read";
        return false;
    }
    WadContentDecrypter decrypter(key_.prng, place->content);
    decrypter.decrypt(head.data(), head.size());
    head.resize(static_cast<std::size_t>(bytes));
    out.swap(head);
    return true;
}

bool stage_moved_channel(std::uint64_t title_id, const std::function<void(double)>& progress, std::string& error) {
    const std::string code = channel_code(title_id);
    const std::string path = moved_channel_file(code);
    if (path.empty()) return true;
    if (NandOpen() < 0) {
        error = "the Wii's memory could not be opened";
        return false;
    }
    struct NandUser {
        ~NandUser() { NandClose(); }
    } nand_user;
    // The title's content folder belongs to ES: the running IOS refuses it
    // to an app, both what is there and writing to it, until the
    // permission check is opened. RiftWii writes there only for this.
    struct Permissions {
        bool opened = open_nand_permissions("SD card channels");
        ~Permissions() {
            if (opened) close_nand_permissions("SD card channels");
        }
    } permissions;
    MovedChannel moved;
    if (!moved.open(path, title_id, error)) {
        // A content.bin left on the card (damaged, another Wii's, or older
        // than the channel) does not stop a channel the Wii has in full.
        if (!title_contents_on_wii(title_id)) return false;
        logf("Wii channels: %s is on the Wii in full; %s is not used (%s)\n", code.c_str(), path.c_str(),
             error.c_str());
        error.clear();
        return true;
    }

    // What the NAND lacks: a staged copy from an earlier start that was not
    // removed, or the Wii Menu's own, is used as it is.
    std::vector<TmdContent> missing;
    std::uint64_t total = 0;
    for (const TmdContent& c : moved.tmd().contents) {
        if (!moved.has(c.id)) continue;
        if (nand_file_size(nand_content_path(title_id, c.id)) == static_cast<s64>(c.size)) continue;
        missing.push_back(c);
        total += c.size;
    }
    if (missing.empty()) return true;
    logf("Wii channels: %s was moved to the SD card; copying %u content(s), %llu bytes, to the Wii for this game, "
         "as the Wii Menu does\n",
         code.c_str(), static_cast<unsigned>(missing.size()), static_cast<unsigned long long>(total));

    std::vector<std::string> written;
    const auto undo = [&]() {
        for (const std::string& file : written) ISFS_Delete(isfs_path(file));
    };
    AlignedBuffer buffer = aligned(kChunkBytes);
    std::uint64_t done = 0;
    bool ok = buffer != nullptr;
    if (!ok) error = "out of memory for copying the game";
    for (const TmdContent& c : missing) {
        if (!ok) break;
        const std::string to = nand_content_path(title_id, c.id);
        // A short copy from an interrupted start is replaced; any other
        // file there (another version of the content) is not RiftWii's to
        // remove.
        if (nand_file_size(to) >= 0 && staged_by_riftwii(to) != 1) {
            error = to + " is on the Wii already at another size, and it is not a copy RiftWii made";
            ok = false;
            break;
        }
        if (!append_staged(to, error)) {
            ok = false;
            break;
        }
        ISFS_Delete(isfs_path(to));
        s32 result = ISFS_CreateFile(isfs_path(to), kStagedAttribute, ISFS_OPEN_RW, ISFS_OPEN_RW, ISFS_OPEN_READ);
        // Undone from here on, even when it cannot be opened.
        if (result >= 0) written.push_back(to);
        const s32 fd = result < 0 ? result : ISFS_Open(isfs_path(to), ISFS_OPEN_WRITE);
        if (fd < 0) {
            error = to + " could not be created on the Wii (" + std::to_string(fd) + ")";
            ok = false;
            break;
        }
        ok = moved.stream(c.id, [&](const std::uint8_t* data, std::size_t length) {
            std::memcpy(buffer.get(), data, length);
            result = ISFS_Write(fd, buffer.get(), length);
            if (result != static_cast<s32>(length)) {
                error = to + " could not be written (" + std::to_string(result) +
                        "; is the Wii's memory full?)";
                return false;
            }
            done += length;
            if (progress && total) progress(static_cast<double>(done) / static_cast<double>(total));
            return true;
        }, error);
        ISFS_Close(fd);
    }
    if (!ok) undo();
    if (ok) logf("Wii channels: %s's contents are on the Wii until RiftWii starts again\n", code.c_str());
    return ok;
}

void UnstageMovedChannels() {
    if (!publish_staged()) {
        logf("Wii channels: the SD card channels' cleanup list could not be published; cleanup is tried next time\n");
        return;
    }
    // A rewrite cut short leaves a kStagedNew, never read: it may be partial.
    std::remove(kStagedNew);
    std::vector<std::string> paths;
    if (!read_staged(kStagedList, paths) || !read_staged(kStagedOld, paths)) {
        logf("Wii channels: the SD card channels' cleanup lists could not be read in full; both are kept for next time\n");
        return;
    }
    if (paths.empty()) {
        write_staged(paths);
        return;
    }
    // The list stays for the next start when the files cannot be removed now.
    if (NandOpen() < 0) {
        logf("Wii channels: the Wii's memory could not be opened; the SD card channels' copies are removed next time\n");
        return;
    }
    const bool opened = open_nand_permissions("SD card channels");
    if (!opened) {
        NandClose();
        logf("Wii channels: the SD card channels' copies could not be reached; they are removed next time\n");
        return;
    }
    unsigned removed = 0, kept = 0;
    std::vector<std::string> left;
    // The content.bin of the title the last paths were under, opened once.
    std::uint64_t open_title = 0;
    bool tried = false;
    std::unique_ptr<MovedChannel> moved;
    for (const std::string& path : paths) {
        std::uint64_t title_id = 0;
        std::uint32_t content_id = 0;
        // Only exact paths of contents staging can copy. Other entries in
        // the card's list cannot authorize removing a NAND file.
        if (!parse_staged_content_path(path, title_id, content_id)) {
            ++kept;
            continue;
        }
        // Only RiftWii's own copies: not a file ES installed, which the list
        // on the card cannot vouch for.
        const s32 ours = staged_by_riftwii(path);
        if (ours == -106) {  // already gone
            ++removed;
            continue;
        }
        if (ours == 0) {
            ++kept;
            continue;
        }
        if (ours < 0) {
            left.push_back(path);
            continue;
        }
        if (!tried || title_id != open_title) {
            tried = true;
            open_title = title_id;
            moved.reset(new (std::nothrow) MovedChannel());
            const std::string file = moved_channel_file(channel_code(title_id));
            std::string why;
            if (moved && (file.empty() || !moved->open(file, title_id, why))) moved.reset();
        }
        // A missing or unreadable card file cannot establish which contents
        // were copied. Keep the paths so cleanup can retry with that file.
        if (!moved) {
            left.push_back(path);
            continue;
        }
        if (!moved->has(content_id)) {
            ++kept;
            continue;
        }
        const s32 result = ISFS_Delete(isfs_path(path));
        if (result >= 0 || result == -106) {
            ++removed;
        } else {
            left.push_back(path);
        }
    }
    close_nand_permissions("SD card channels");
    NandClose();
    if (!write_staged(left)) {
        logf("Wii channels: the SD card channels' cleanup list could not be rewritten; its recovery copies are kept\n");
    }
    logf("Wii channels: removed %u content file(s) copied to the Wii for SD card channels last time%s%s\n", removed,
         kept ? " (some kept: not copies RiftWii made, or not included in their channel's card file)" : "",
         left.empty() ? "" : " (some could not be removed or checked; tried again next time)");
}

}  // namespace riftwii::wii
