// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace riftwii {

// Backups of the native GameCube loader's memory cards (riftwii/gccard.hpp).
// Before a game starts, the card file it will use is copied to
// sd:/riftwii/gc/backups/<card>/, named by the date and time
// ("2026-10-17_1432.raw"), and only the newest kGcBackupsKept are kept.
// <card> is the card file's own name: the game's ID for its own card, and
// "shared" or "shared-jp" for a shared one, so a shared card's backups are
// in one place whichever game started it, and restoring one puts back
// every game's saves on that card together.

constexpr std::size_t kGcBackupsKept = 5;
// The largest card the library reads (128 Mbit): anything larger is not
// copied, so one odd file cannot fill the SD card or stall a start.
constexpr std::uint64_t kGcBackupMaxBytes = 16u << 20;

// When a backup was made, as its name says. `serial` tells apart backups
// made in the same minute: 0 for the first, 2, 3... for the next. `undo`:
// the copy a restore made of the card just before it put an older one
// back ("-undo" at the name's end), shown as "before you restored".
struct GcBackupStamp {
    unsigned year = 0, month = 0, day = 0;  // month 1-12, day 1-31
    unsigned hour = 0, minute = 0;          // 0-23, 0-59
    unsigned serial = 0;
    bool undo = false;
};

// A clock reading (localtime's) as a backup's stamp.
GcBackupStamp gc_backup_stamp(const std::tm& local);

// The folder the backups of a game's card go in (gc_card_path's choice of
// card): sd:/riftwii/gc/backups/<ID> or .../backups/shared(-jp).
std::string gc_backup_folder(const std::string& game_id, bool shared);

// "2026-10-17_1432.raw", "2026-10-17_1432-2.raw" with serial 2, and
// "2026-10-17_1432-undo.raw" or "2026-10-17_1432-2-undo.raw" for an undo copy.
std::string gc_backup_name(const GcBackupStamp& stamp);

// A backup's name read back. False for anything else in the folder (a PC's
// "._" files, a copy renamed by hand), which is then never listed or deleted.
bool gc_backup_parse(const std::string& name, GcBackupStamp& out);

// The backups among `names`, newest first; other names are left out.
std::vector<std::string> gc_backups_newest_first(const std::vector<std::string>& names);

// The name for a backup made at `now` that none of `existing` has: the
// minute's own, or the next free serial when that minute already has one
// (an undo copy and a plain one never share a minute and serial, so their
// order stays plain). `now.undo` marks the new name as an undo copy.
std::string gc_backup_new_name(const std::vector<std::string>& existing, GcBackupStamp now);

// The backups beyond the newest `keep`, oldest last: the ones to delete.
std::vector<std::string> gc_backups_to_prune(const std::vector<std::string>& names,
                                             std::size_t keep = kGcBackupsKept);

// The backup file names in `folder`, newest first (empty when it is missing).
std::vector<std::string> gc_backup_list(const std::string& folder);

// Whether two files hold the same bytes, read a piece at a time. False
// when either cannot be read.
bool gc_files_same(const std::string& a, const std::string& b);

// Copies `from` to `to` a piece at a time, `to` made anew. A copy cut short
// is removed, so a backup is either whole or not there.
bool gc_copy_file(const std::string& from, const std::string& to, std::string& error);

// Writes `bytes` over the file at `path`. A file of the same length is
// written in place, keeping its clusters on the SD card (the loader maps
// the card file's sectors, and a new file could come out in more pieces
// than it takes); any other is made anew.
bool gc_overwrite_file(const std::string& path, const std::vector<std::uint8_t>& bytes, std::string& error);

struct GcBackupResult {
    enum class What { Made, Same, NoCard, Failed };
    What what = What::Failed;
    std::string name;                 // Made: the new backup; Same: the newest one, which matches
    std::vector<std::string> pruned;  // backups deleted to keep the newest kGcBackupsKept
    std::uint64_t bytes = 0;          // the card's length
    std::string error;                // Failed: why, as a sentence
};

// Backs up the card at `card_path` into `folder` (made when missing) as
// made at `now`: skipped when the card is not there or matches the newest
// backup byte for byte, then the oldest beyond kGcBackupsKept deleted. An
// undo copy (`now.undo`, a restore's) counts among the five like any other.
GcBackupResult gc_backup_card(const std::string& card_path, const std::string& folder, const GcBackupStamp& now);

}  // namespace riftwii
