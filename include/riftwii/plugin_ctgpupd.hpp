// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

#include "riftwii/ctgpcheck.hpp"
#include "riftwii/ctgpupdate.hpp"
#include "riftwii/ctgpupdater.hpp"
#include "riftwii/fat32.hpp"

// The ctgpupd plugin's request (docs/PLUGINS.md, "The ctgpupd plugin"):
// RiftWii's own CTGP-R update check, download and install, loaded only when
// an update waits on the card or a check runs, and shut down afterwards.
//
// As with the ctgpr plugin (riftwii/plugin_ctgpr.hpp), this request carries
// RiftWii's own C++ types (std::string, std::function), which is sound only
// because a plugin is loaded by the one DOL whose build stamp it carries,
// and because both sides allocate from the same heap. `layout` guards the
// rest. The DOL keeps what needs its own drivers: the network connection,
// the console's MAC address, the raw SD reads jelly.bin's key needs, the
// game disc reads, the screen, and the start-up replay of the undo journal
// (riftwii/ctgpjournal.hpp), which must work even when this plugin can't be
// loaded.
namespace riftwii {

enum class CtgpUpdOp : std::uint32_t {
    // Reads the update waiting at update_path (dia_* out) and the version
    // blob.bin holds now (pack_version, 0 when it can't be read).
    Inspect = 1,
    // Reads the installed versions, then asks the server whether an update
    // waits: reply out. The connection is the DOL's (connect_next, io).
    Check = 2,
    // Saves the update the check offered (download_bytes) to part_path,
    // checks its header, and renames it to update_path.
    Download = 3,
    // Installs the update at update_path: a CTGPR update into blob_path,
    // guarded by the undo journal at journal_path; a HASH update into
    // jelly.bin (sd.jelly, written in place), guarded by jelly_journal_path.
    Install = 4,
};

// The DOL's raw reads of the SD card (wii/storage/sdfile.hpp): jelly.bin's
// key is made from its directory entry's times. The DOL finds jelly.bin
// before the call and keeps it: what the lookup allocates stays in the DOL's
// hands, so an escape in the plugin cannot strand it.
struct CtgpUpdSdAccess {
    const Fat32File* jelly = nullptr;  // null when it was not found
    std::string jelly_error;           // why, when jelly is null
    bool (*read)(const Fat32File& file, std::uint64_t offset, std::uint8_t* out, std::size_t length) = nullptr;
    // Writes whole 512-byte blocks of jelly.bin where they lie on the card,
    // never its directory entry, the FAT or FSInfo (wii/storage/sdfile.hpp's
    // write_sd_file_in_place), so its key stays the same: for a HASH update.
    bool (*write)(const Fat32File& file, std::uint64_t offset, const std::uint8_t* data, std::size_t length) = nullptr;
};

// The C library calls the plugin's runtime has no host function for: the
// DOL's own.
struct CtgpUpdLibc {
    int (*remove)(const char* path) = nullptr;
    int (*rename)(const char* from, const char* to) = nullptr;
    int (*fflush)(std::FILE* file) = nullptr;
    int (*fileno)(std::FILE* file) = nullptr;
    int (*fsync)(int fd) = nullptr;
    // The DOL's errno: the plugin's stat is the DOL's (rwp_host::path_info),
    // and the journal tells "not there" from an SD error by it.
    int* (*errno_location)() = nullptr;
};

constexpr std::uint32_t ctgpupd_request_layout();

struct CtgpUpdRequest {
    std::uint32_t size = sizeof(CtgpUpdRequest);
    std::uint32_t layout = 0;  // ctgpupd_request_layout() of the DOL
    CtgpUpdOp op = CtgpUpdOp::Inspect;

    // In, every operation.
    std::string update_path;   // sd:/ctgpr/update.dif
    std::string part_path;     // sd:/ctgpr/update.dif.part
    std::string blob_path;     // sd:/ctgpr/blob.bin
    std::string journal_path;  // sd:/ctgpr/update-journal.bin
    std::string jelly_journal_path;  // sd:/ctgpr/update-journal-jelly.bin
    std::string scratch_prefix;  // sd:/ctgpr/update-base-
    CtgpUpdSdAccess sd;
    CtgpUpdLibc libc;
    std::function<void(const std::string& line)> log;  // one line, no "\n"

    // In, Check: the console's MAC address; connect_next connects to the
    // next server (false when none is left, with the reason); io then talks
    // to it, and disconnect closes it after a failed exchange.
    std::array<std::uint8_t, 6> mac{};
    std::function<bool(std::string& error)> connect_next;
    std::function<void()> disconnect;
    // In, Check and Download.
    CtgpCheckIo io;
    // In, Download.
    std::uint32_t download_bytes = 0;
    CtgpDownloadProgress download_progress;
    // In, Install.
    CtgpDiscBase disc;
    CtgpUpdateStep step;

    // Out.
    std::string dia_component;  // Inspect, Install
    std::uint32_t dia_version = 0;       // the version the update brings
    std::uint32_t dia_base_version = 0;  // the version it rebuilds from
    std::uint32_t pack_version = 0;      // Inspect: the version blob.bin holds
    std::uint32_t dia_files = 0;
    std::uint64_t update_bytes = 0;
    CtgpCheckReply reply;  // Check
    std::string error;
    // Inspect and Install: 1 when the update file itself is at fault (it is
    // not a DIA0, its signature does not check out, or a section's commands
    // are broken) and trying it again can never work; after an Install, only
    // once nothing of it is left in blob.bin. 0 for a failure of the card,
    // memory or anything else that may pass next time.
    std::uint32_t bad_update = 0;
    // 1 when the plugin left through its escape (no memory, or what would
    // have thrown): every heap block the operation held went back, the
    // strings above among them, so the DOL must not use or destroy them.
    // After an Install, the undo journal on the card is still whole: the DOL
    // plays it back (CtgpRecoverPackJournal).
    std::uint32_t escaped = 0;
};

constexpr std::uint32_t ctgpupd_request_layout() {
    return static_cast<std::uint32_t>(sizeof(CtgpUpdRequest) + 0x100u * sizeof(CtgpCheckIo) +
                                      0x10000u * sizeof(CtgpCheckReply)) ^
           static_cast<std::uint32_t>(sizeof(std::string) << 4) ^ static_cast<std::uint32_t>(sizeof(Fat32File) << 12) ^
           static_cast<std::uint32_t>(sizeof(CtgpDiscBase) << 20) ^ static_cast<std::uint32_t>(sizeof(CtgpUpdateStep) << 26);
}

// What the plugin's launch returns when the operation worked; otherwise
// RWP_LAUNCH_FAILED (the reason in `error`) or RWP_LAUNCH_NO_MEMORY.
constexpr std::int32_t kCtgpUpdDone = 0;

// Whether `raw` (`bytes` long) is a request this side can use. False with `why`.
inline bool ctgpupd_request_check(const void* raw, std::uint32_t bytes, std::string& why) {
    const auto* request = static_cast<const CtgpUpdRequest*>(raw);
    if (request == nullptr || bytes != sizeof(CtgpUpdRequest) || request->size != sizeof(CtgpUpdRequest)) {
        why = "the request is not the size this plugin was built for";
        return false;
    }
    if (request->layout != ctgpupd_request_layout()) {
        why = "the request's types are laid out differently from this plugin's";
        return false;
    }
    const CtgpUpdLibc& c = request->libc;
    if (c.remove == nullptr || c.rename == nullptr || c.fflush == nullptr || c.fileno == nullptr ||
        c.fsync == nullptr || c.errno_location == nullptr || request->sd.read == nullptr || request->sd.write == nullptr) {
        why = "the request is missing one of the DOL's functions";
        return false;
    }
    return true;
}

}  // namespace riftwii
