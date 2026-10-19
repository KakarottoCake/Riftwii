// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Backups of the GameCube loader's memory cards (riftwii/gcbackup.hpp):
// plain files copied with stdio, so the same code runs on the Wii (libfat)
// and in the host tests.
#include "riftwii/gcbackup.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <tuple>

#include "riftwii/gccard.hpp"

namespace riftwii {

namespace {

// A piece of a copy or compare: large enough that an SD card reads it at
// full speed, small enough for the menu's heap twice over.
constexpr std::size_t kPieceBytes = 64u << 10;
// Same-minute backups numbered past this are refused (a clock stuck at
// one minute would otherwise make names without end).
constexpr unsigned kMaxSerial = 99;

const char kExtension[] = ".raw";
const char kUndo[] = "-undo";

std::string two_digits(unsigned v) {
    char buf[4];
    std::snprintf(buf, sizeof buf, "%02u", v % 100);
    return buf;
}

// Reads exactly `count` digits at `text`; false on anything else.
bool read_digits(const char* text, unsigned count, unsigned& out) {
    out = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        out = out * 10 + static_cast<unsigned>(text[i] - '0');
    }
    return true;
}

auto order_key(const GcBackupStamp& s) {
    return std::make_tuple(s.year, s.month, s.day, s.hour, s.minute, s.serial);
}

// The file's length, or -1 when it is not there or not a file.
long long file_length(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return -1;
    return static_cast<long long>(st.st_size);
}

// Makes `folder` and every folder above it after the device ("sd:/").
void make_folders(const std::string& folder) {
    const std::size_t device = folder.find(":/");
    std::size_t at = device == std::string::npos ? 1 : device + 2;
    while (at <= folder.size()) {
        const std::size_t slash = folder.find('/', at);
        const std::size_t end = slash == std::string::npos ? folder.size() : slash;
        mkdir(folder.substr(0, end).c_str(), 0777);
        at = end + 1;
    }
}

}  // namespace

GcBackupStamp gc_backup_stamp(const std::tm& local) {
    GcBackupStamp stamp;
    stamp.year = static_cast<unsigned>(local.tm_year + 1900);
    stamp.month = static_cast<unsigned>(local.tm_mon + 1);
    stamp.day = static_cast<unsigned>(local.tm_mday);
    stamp.hour = static_cast<unsigned>(local.tm_hour);
    stamp.minute = static_cast<unsigned>(local.tm_min);
    return stamp;
}

std::string gc_backup_folder(const std::string& game_id, bool shared) {
    // The card file's own name, so a backup folder always matches its card.
    const std::string card = gc_card_path(game_id, shared);
    const std::size_t slash = card.rfind('/');
    std::string stem = card.substr(slash + 1);
    stem.resize(stem.size() - (sizeof kExtension - 1));
    return "sd:/riftwii/gc/backups/" + stem;
}

std::string gc_backup_name(const GcBackupStamp& stamp) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04u-%s-%s_%s%s", stamp.year % 10000, two_digits(stamp.month).c_str(),
                  two_digits(stamp.day).c_str(), two_digits(stamp.hour).c_str(), two_digits(stamp.minute).c_str());
    std::string name = buf;
    if (stamp.serial >= 2) name += "-" + std::to_string(stamp.serial);
    if (stamp.undo) name += kUndo;
    return name + kExtension;
}

bool gc_backup_parse(const std::string& name, GcBackupStamp& out) {
    // "YYYY-MM-DD_HHMM" (15 characters), maybe "-N", maybe "-undo", then ".raw".
    constexpr std::size_t kStampChars = 15;
    std::size_t extension = sizeof kExtension - 1;
    if (name.size() < kStampChars + extension) return false;
    if (name.compare(name.size() - extension, extension, kExtension) != 0) return false;
    const std::size_t undo_chars = sizeof kUndo - 1;
    const bool undo = name.size() >= kStampChars + undo_chars + extension &&
                      name.compare(name.size() - extension - undo_chars, undo_chars, kUndo) == 0;
    // The marker is read as part of the ending, so the serial's check
    // below sees only what is between the minute and it.
    if (undo) extension += undo_chars;
    const char* p = name.c_str();
    GcBackupStamp s;
    if (!read_digits(p, 4, s.year) || p[4] != '-' || !read_digits(p + 5, 2, s.month) || p[7] != '-' ||
        !read_digits(p + 8, 2, s.day) || p[10] != '_' || !read_digits(p + 11, 2, s.hour) ||
        !read_digits(p + 13, 2, s.minute))
        return false;
    if (s.month < 1 || s.month > 12 || s.day < 1 || s.day > 31 || s.hour > 23 || s.minute > 59) return false;
    const std::size_t rest = name.size() - extension - kStampChars;
    if (rest > 0) {
        // "-2" to "-99": one or two digits after the dash, no leading zero.
        if (rest < 2 || rest > 3 || p[kStampChars] != '-' || p[kStampChars + 1] == '0') return false;
        if (!read_digits(p + kStampChars + 1, static_cast<unsigned>(rest - 1), s.serial)) return false;
        if (s.serial < 2) return false;
    }
    s.undo = undo;
    out = s;
    return true;
}

std::vector<std::string> gc_backups_newest_first(const std::vector<std::string>& names) {
    std::vector<std::pair<GcBackupStamp, std::string>> found;
    for (const std::string& name : names) {
        GcBackupStamp stamp;
        if (gc_backup_parse(name, stamp)) found.emplace_back(stamp, name);
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        return order_key(a.first) > order_key(b.first);
    });
    std::vector<std::string> out;
    for (const auto& entry : found) out.push_back(entry.second);
    return out;
}

std::string gc_backup_new_name(const std::vector<std::string>& existing, GcBackupStamp now) {
    // The minutes and serials taken, plain or undo alike.
    std::vector<GcBackupStamp> taken;
    for (const std::string& name : existing) {
        GcBackupStamp stamp;
        if (gc_backup_parse(name, stamp)) taken.push_back(stamp);
    }
    now.serial = 0;
    for (;;) {
        const auto same = [&](const GcBackupStamp& t) {
            return t.year == now.year && t.month == now.month && t.day == now.day && t.hour == now.hour &&
                   t.minute == now.minute && t.serial == now.serial;
        };
        const std::string name = gc_backup_name(now);
        const bool free = std::none_of(taken.begin(), taken.end(), same) &&
                          std::find(existing.begin(), existing.end(), name) == existing.end();
        if (free) return name;
        now.serial = now.serial == 0 ? 2 : now.serial + 1;
        if (now.serial > kMaxSerial) return std::string();
    }
}

std::vector<std::string> gc_backups_to_prune(const std::vector<std::string>& names, std::size_t keep) {
    std::vector<std::string> sorted = gc_backups_newest_first(names);
    if (sorted.size() <= keep) return {};
    return std::vector<std::string>(sorted.begin() + static_cast<std::ptrdiff_t>(keep), sorted.end());
}

std::vector<std::string> gc_backup_list(const std::string& folder) {
    std::vector<std::string> names;
    DIR* dir = opendir(folder.c_str());
    if (!dir) return names;
    while (dirent* entry = readdir(dir)) names.push_back(entry->d_name);
    closedir(dir);
    return gc_backups_newest_first(names);
}

bool gc_files_same(const std::string& a, const std::string& b) {
    const long long length = file_length(a);
    if (length < 0 || length != file_length(b)) return false;
    std::FILE* fa = std::fopen(a.c_str(), "rb");
    std::FILE* fb = fa ? std::fopen(b.c_str(), "rb") : nullptr;
    bool same = fa && fb;
    std::vector<std::uint8_t> pa(same ? kPieceBytes : 0), pb(same ? kPieceBytes : 0);
    while (same) {
        const std::size_t got = std::fread(pa.data(), 1, pa.size(), fa);
        if (got == 0) break;
        same = std::fread(pb.data(), 1, got, fb) == got && std::memcmp(pa.data(), pb.data(), got) == 0;
    }
    if (same && std::ferror(fa)) same = false;
    if (fb) std::fclose(fb);
    if (fa) std::fclose(fa);
    return same;
}

bool gc_copy_file(const std::string& from, const std::string& to, std::string& error) {
    std::FILE* in = std::fopen(from.c_str(), "rb");
    if (!in) {
        error = "The memory card file could not be opened.";
        return false;
    }
    std::FILE* out = std::fopen(to.c_str(), "wb");
    if (!out) {
        std::fclose(in);
        error = "The copy could not be made on the SD card.";
        return false;
    }
    std::vector<std::uint8_t> piece(kPieceBytes);
    bool ok = true;
    for (;;) {
        const std::size_t got = std::fread(piece.data(), 1, piece.size(), in);
        if (got == 0) break;
        if (std::fwrite(piece.data(), 1, got, out) != got) {
            ok = false;
            error = "The SD card is full, or the copy could not be written.";
            break;
        }
    }
    if (ok && std::ferror(in)) {
        ok = false;
        error = "The memory card file could not be read to the end.";
    }
    std::fclose(in);
    if (std::fclose(out) != 0 && ok) {
        ok = false;
        error = "The copy could not be finished on the SD card.";
    }
    if (!ok) std::remove(to.c_str());
    return ok;
}

bool gc_overwrite_file(const std::string& path, const std::vector<std::uint8_t>& bytes, std::string& error) {
    // The same length: written in place, so the card keeps the clusters the
    // game's runtime reads it by (one piece stays one piece); the backup a
    // restore makes first covers a cut in the middle.
    if (file_length(path) == static_cast<long long>(bytes.size())) {
        std::FILE* f = std::fopen(path.c_str(), "r+b");
        if (!f) {
            error = "The memory card file could not be opened for writing.";
            return false;
        }
        const bool written = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
        const bool closed = std::fclose(f) == 0;
        if (!written || !closed) {
            error = "The memory card file could not be written.";
            return false;
        }
        return true;
    }
    // Another length: written whole beside it first, and put in its place
    // only then, so a full card or a cut leaves the old card as it was
    // (BUG-18). FAT's rename does not replace a file, so the old one is
    // moved aside first and comes back if the new one cannot take its place.
    const std::string temp = path + ".tmp";
    std::FILE* f = std::fopen(temp.c_str(), "wb");
    if (!f) {
        error = "The memory card file could not be opened for writing.";
        return false;
    }
    const bool written = bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    const bool closed = std::fclose(f) == 0;
    if (!written || !closed) {
        std::remove(temp.c_str());
        error = "The memory card file could not be written (is the SD card full?).";
        return false;
    }
    const std::string aside = path + ".old";
    // A write cut between the two renames below left the card only at .old:
    // it goes back first, never removed as a leftover.
    if (file_length(path) < 0 && file_length(aside) >= 0) std::rename(aside.c_str(), path.c_str());
    std::remove(aside.c_str());
    const bool had_card = std::rename(path.c_str(), aside.c_str()) == 0;
    if (std::rename(temp.c_str(), path.c_str()) != 0) {
        if (had_card) std::rename(aside.c_str(), path.c_str());
        error = "The new memory card file could not be put in place; it is at " + temp + ".";
        return false;
    }
    if (had_card) std::remove(aside.c_str());
    return true;
}

GcBackupResult gc_backup_card(const std::string& card_path, const std::string& folder, const GcBackupStamp& now,
                              const std::string& keep) {
    GcBackupResult result;
    const long long length = file_length(card_path);
    if (length <= 0) {
        // Not made yet, or made empty and never formatted: nothing to keep.
        result.what = GcBackupResult::What::NoCard;
        return result;
    }
    result.bytes = static_cast<std::uint64_t>(length);
    if (result.bytes > kGcBackupMaxBytes) {
        result.error = "The memory card file is larger than any GameCube card, so it was not copied.";
        return result;
    }
    make_folders(folder);
    std::vector<std::string> existing = gc_backup_list(folder);
    if (!existing.empty() && gc_files_same(card_path, folder + "/" + existing.front())) {
        result.what = GcBackupResult::What::Same;
        result.name = existing.front();
        return result;
    }
    const std::string name = gc_backup_new_name(existing, now);
    if (name.empty()) {
        result.error = "There are too many backups with the same date and time.";
        return result;
    }
    if (!gc_copy_file(card_path, folder + "/" + name, result.error)) return result;
    result.what = GcBackupResult::What::Made;
    result.name = name;
    // The new one is never pruned, even when a Wii's clock was set back and
    // its name sorts oldest: the others make room for it. Nor is `keep`.
    std::vector<std::string> others;
    for (const std::string& old : existing)
        if (old != keep) others.push_back(old);
    const std::size_t room = kGcBackupsKept - 1 - (others.size() < existing.size() ? 1 : 0);
    for (const std::string& old : gc_backups_to_prune(others, room)) {
        if (std::remove((folder + "/" + old).c_str()) == 0) result.pruned.push_back(old);
    }
    return result;
}

}  // namespace riftwii
