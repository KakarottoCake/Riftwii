// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "netsock.hpp"
#include "riftwii/ctgpcheck.hpp"
#include "riftwii/ctgpupdate.hpp"
#include "riftwii/ctgpupdater.hpp"

// RiftWii's own CTGP-R update check, download and install. The work is done
// by the ctgpupd plugin (riftwii/plugin_ctgpupd.hpp), loaded on the first
// call and shut down when the CtgpUpdater goes; this side gives it the
// network connection, the console's MAC address, the raw SD reads and the
// game disc. The undo journal's start-up replay stays here, so an install
// that was cut off is undone even when the plugin can't be loaded.
namespace riftwii {
struct CtgpUpdRequest;
}

namespace riftwii::wii {

inline constexpr const char* kCtgpUpdatePath = "sd:/ctgpr/update.dif";
inline constexpr const char* kCtgpUpdatePartPath = "sd:/ctgpr/update.dif.part";
inline constexpr const char* kCtgpJournalPath = "sd:/ctgpr/update-journal.bin";
// The undo journal of a HASH update's writes into jelly.bin.
inline constexpr const char* kCtgpJellyJournalPath = "sd:/ctgpr/update-journal-jelly.bin";

// The update waiting on the card.
struct CtgpUpdateInfo {
    std::string component;
    std::uint32_t version = 0;       // the version it brings
    std::uint32_t base_version = 0;  // the version it rebuilds from
    std::uint32_t pack_version = 0;  // the version blob.bin holds (0: unknown)
    std::uint32_t files = 0;
    std::uint64_t bytes = 0;
};

class CtgpUpdater {
public:
    CtgpUpdater() = default;
    ~CtgpUpdater();
    CtgpUpdater(const CtgpUpdater&) = delete;
    CtgpUpdater& operator=(const CtgpUpdater&) = delete;

    // Reads the header of the update at kCtgpUpdatePath.
    bool inspect(CtgpUpdateInfo& out, std::string& error);
    // After a failed inspect() or install(): true when the update file itself
    // can never be installed (not a CTGP-R update, a bad signature, broken
    // commands) and, for install(), the pack was put back. False when the
    // card, memory or the plugin was at fault: the file may be fine.
    bool bad_update() const { return bad_update_; }
    // After a failed install() of a HASH update: true when it is genuine but
    // needs jelly.bin made larger, which only CTGP-R's channel does. Nothing
    // was written.
    bool channel_installs() const { return channel_installs_; }
    // After a failed download(): true when the server, asked again, no
    // longer offers the update the player agreed to (none, or another size).
    bool offer_changed() const { return offer_changed_; }
    // Brings the network up and asks the update server whether an update
    // waits for the installed packages. Only the answer is used: download()
    // asks again on a connection of its own.
    // (`keep_open`: download()'s own check, which downloads on it at once.)
    bool check(CtgpCheckReply& reply, std::string& error, bool keep_open = false);
    // Saves the update the check offered (`size` bytes) to kCtgpUpdatePath
    // (by way of kCtgpUpdatePartPath): asks the server again and downloads at
    // once, so no connection waits on the player; fails (offer_changed())
    // when the server now offers none, or one of another size. A download the server
    // closes before its first byte is tried once more. `progress` may stop it.
    bool download(std::uint32_t size, const CtgpDownloadProgress& progress, std::string& error);
    // Installs the update at kCtgpUpdatePath into blob.bin. `disc` reads the
    // game's own files; `step` is told how far each file has got.
    bool install(const CtgpDiscBase& disc, const CtgpUpdateStep& step, std::string& error);

private:
    bool run(CtgpUpdRequest& request, std::string& error);
    bool download_now(std::uint32_t size, const CtgpDownloadProgress& progress, std::string& error);
    void close_connection();

    SocketTransport conn_;
    bool connected_ = false;
    std::size_t next_server_ = 0;
    bool bad_update_ = false;
    bool channel_installs_ = false;
    bool offer_changed_ = false;
};

// Puts the pack back as it was before an update that did not finish (power
// lost, a crash). Call before anything reads or changes blob.bin. Returns
// false only when a journal is there and could not be played back.
bool CtgpRecoverPackJournal(std::string& error);

// For every CTGP-R launch: true when blob.bin is whole (no undo journal left,
// or the one left was played back now). False with the player's sentence
// when an interrupted update could not be undone: CTGP-R must not start on a
// half-written pack.
bool CtgpPackReady(std::string& message);

}  // namespace riftwii::wii
