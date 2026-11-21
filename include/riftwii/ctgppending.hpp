// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What the menu does with a CTGP-R update already waiting on the card
// (sd:/ctgpr/update.dif) before it asks the server for one, and how a file is
// moved out of the way. Kept apart from the menu so the host tests can check
// it.

#include <sys/types.h>

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

namespace riftwii {

enum class CtgpPendingStep {
    Install,   // offer it
    Stop,      // it could not be looked at now (the card, memory): leave it, try next time
    Retire,    // its version is already installed: move it to update.dif.installed
    SetAside,  // it can never be installed here: move it to update.dif.bad
};

// `inspected`: the plugin read the update; `bad_update`: when it could not,
// the file itself is at fault (CtgpUpdRequest::bad_update). The versions are
// the update's, the one it rebuilds from and blob.bin's (0: unknown).
inline CtgpPendingStep ctgp_pending_step(bool inspected, bool bad_update, std::uint32_t pack_version,
                                         std::uint32_t version, std::uint32_t base_version) {
    if (!inspected) return bad_update ? CtgpPendingStep::SetAside : CtgpPendingStep::Stop;
    if (pack_version != 0 && pack_version == version) return CtgpPendingStep::Retire;
    if (pack_version != 0 && pack_version != base_version) return CtgpPendingStep::SetAside;
    return CtgpPendingStep::Install;
}

// Moves `from` to `to`, an older `to` going first (FAT does not rename onto a
// file that is there). True only when the rename worked: a card that is
// locked, or a file that can't be moved, leaves `from` where it was, and the
// caller must not say it was moved.
inline bool ctgp_move_aside(const std::string& from, const std::string& to,
                            const std::function<int(const char*)>& remove,
                            const std::function<int(const char*, const char*)>& rename) {
    remove(to.c_str());
    return rename(from.c_str(), to.c_str()) == 0;
}

// Whether an update waits at `path`. `size_of` gives a file's length (0 on
// success, as stat). An empty file is not an update: CTGP-R's own channel
// leaves a 0-byte update.dif when it starts, so one is removed quietly (no
// notice, nothing set aside) and the server is asked as if none were there.
inline bool ctgp_update_waiting(const std::string& path,
                                const std::function<int(const char*, std::uint64_t*)>& size_of,
                                const std::function<int(const char*)>& remove) {
    std::uint64_t size = 0;
    if (size_of(path.c_str(), &size) != 0) return false;
    if (size != 0) return true;
    remove(path.c_str());
    return false;
}

// What an update file is, from what the ctgpupd plugin read of it.
// `read`: the card gave what was asked (a read that failed says nothing about
// the file); `dia0`: its header and tables read as a DIA0 of the size
// expected; `signed_ok`: CTGP-R's signature checks out over all of it;
// `ours`: its package is one RiftWii installs (ctgp_dia_supported).
// A signed update for another package (GHOST, on 2611-208) is genuine: it is
// not "bad", CTGP-R's channel installs it, and the file is removed so the
// channel downloads it fresh.
enum class CtgpUpdateKind { Usable, ChannelInstalls, Bad, Unreadable };
inline CtgpUpdateKind ctgp_update_kind(bool read, bool dia0, bool signed_ok, bool ours) {
    if (!read) return CtgpUpdateKind::Unreadable;
    if (!dia0 || !signed_ok) return CtgpUpdateKind::Bad;
    return ours ? CtgpUpdateKind::Usable : CtgpUpdateKind::ChannelInstalls;
}

// How a download ends. A checked one replaces `update`; one the check
// found can never be installed (CtgpUpdRequest::bad_update) is kept as
// `bad_path`, so the menu can say it was set aside, as it does for one found
// waiting; anything else (the card, the connection) is removed.
enum class CtgpDownloadEnd { Kept, SetAside, Removed };
inline CtgpDownloadEnd ctgp_finish_download(bool checked, bool bad, const std::string& part,
                                            const std::string& update, const std::string& bad_path,
                                            const std::function<int(const char*)>& remove,
                                            const std::function<int(const char*, const char*)>& rename) {
    if (checked) {
        if (ctgp_move_aside(part, update, remove, rename)) return CtgpDownloadEnd::Kept;
    } else if (bad) {
        if (ctgp_move_aside(part, bad_path, remove, rename)) return CtgpDownloadEnd::SetAside;
    }
    remove(part.c_str());
    return CtgpDownloadEnd::Removed;
}

// The length of an open file, left at its start. False when the card would
// not say (a failed seek, or ftello's -1): that is the card's fault, never
// the file's, so a caller must not take it as a file that is empty or too
// large.
inline bool ctgp_file_size(std::FILE* file, std::uint64_t& size) {
    size = 0;
    if (fseeko(file, 0, SEEK_END) != 0) return false;
    const off_t end = ftello(file);
    if (end < 0 || fseeko(file, 0, SEEK_SET) != 0) return false;
    size = static_cast<std::uint64_t>(end);
    return true;
}

}  // namespace riftwii
