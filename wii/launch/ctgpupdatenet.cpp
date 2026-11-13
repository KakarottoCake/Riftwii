// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ctgpupdatenet.hpp"

#include <dirent.h>
#include <network.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <new>
#include <vector>

#include "ctgpr.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "plugins.hpp"
#include "riftwii/ctgpjournal.hpp"
#include "riftwii/ctgppackwrite.hpp"
#include "riftwii/plugin_ctgpupd.hpp"
#include "riftwii/pluginfile.hpp"
#include "riftwii/update.hpp"
#include "sdfile.hpp"

namespace riftwii::wii {

namespace {

constexpr const char* kPluginName = "ctgpupd";

// The update servers the channel talks to, in the order it tries them.
constexpr const char* kServers[] = {"2.ctgp.billy.wales", "ctgpr.chadsoft.co.uk"};
constexpr int kConnectTimeoutMs = 8000;

CtgpPackIo blob_io(std::FILE* blob) {
    CtgpPackIo io;
    io.read = [blob](std::uint64_t offset, std::uint32_t bytes, std::uint8_t* out) {
        return fseeko(blob, static_cast<off_t>(offset), SEEK_SET) == 0 && std::fread(out, 1, bytes, blob) == bytes;
    };
    io.write = [blob](std::uint64_t offset, const std::uint8_t* data, std::uint32_t bytes) {
        return fseeko(blob, static_cast<off_t>(offset), SEEK_SET) == 0 &&
               std::fwrite(data, 1, bytes, blob) == bytes;
    };
    return io;
}

bool exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

// Scratch files an install that was cut off (a crash, a power cut, a restart
// for memory) left behind. Nothing else is named like this.
void remove_scratch_files() {
    DIR* dir = opendir("sd:/ctgpr");
    if (dir == nullptr) return;
    std::vector<std::string> names;
    // A damaged folder can repeat its entries forever: the scan stops after
    // more than CTGP-R's folder ever holds.
    unsigned seen = 0;
    while (const dirent* entry = readdir(dir)) {
        if (++seen > 4096) break;
        const std::string name = entry->d_name;
        if (name.size() > 16 && name.compare(0, 12, "update-base-") == 0 &&
            name.compare(name.size() - 4, 4, ".tmp") == 0)
            names.push_back(name);
    }
    closedir(dir);
    for (const std::string& name : names) {
        const std::string path = "sd:/ctgpr/" + name;
        if (std::remove(path.c_str()) == 0) logf("CTGP-R update: removed a leftover scratch file, %s\n", name.c_str());
    }
}

// The DOL's C library calls, as the plugin takes them (CtgpUpdLibc).
int libc_remove(const char* path) { return std::remove(path); }
int libc_rename(const char* from, const char* to) { return std::rename(from, to); }
int libc_fflush(std::FILE* file) { return std::fflush(file); }
int libc_fileno(std::FILE* file) { return fileno(file); }
int libc_fsync(int fd) { return fsync(fd); }

bool resolve_for_plugin(const std::string& sd_path, Fat32File& out, std::string& error) {
    return resolve_sd_file(sd_path, out, error);
}

}  // namespace

CtgpUpdater::~CtgpUpdater() {
    if (connected_) conn_.close();
    // The plugin's arena goes back to the heap (plugins.hpp).
    plugin::ShutdownAll("the CTGP-R update is done");
}

bool CtgpUpdater::run(CtgpUpdRequest& request, std::string& error) {
    request.layout = ctgpupd_request_layout();
    request.update_path = kCtgpUpdatePath;
    request.part_path = kCtgpUpdatePartPath;
    request.blob_path = kCtgprBlobPath;
    request.jelly_path = kCtgprJellyPath;
    request.journal_path = kCtgpJournalPath;
    request.scratch_prefix = "sd:/ctgpr/update-base-";
    request.sd.resolve = resolve_for_plugin;
    request.sd.read = read_sd_file;
    request.libc.remove = libc_remove;
    request.libc.rename = libc_rename;
    request.libc.fflush = libc_fflush;
    request.libc.fileno = libc_fileno;
    request.libc.fsync = libc_fsync;
    request.log = [](const std::string& line) { logf("CTGP-R update: %s\n", line.c_str()); };

    std::string message;
    const std::int32_t result = plugin::Launch(kPluginName, &request, sizeof(request), message);
    if (request.escaped != 0) {
        // The strings the plugin filled went back with its heap blocks
        // (riftwii/plugin_ctgpupd.hpp): new empty ones in their place, which
        // allocate nothing, and nothing of the old ones is read.
        new (&request.error) std::string();
        new (&request.dia_component) std::string();
        error = result == RWP_LAUNCH_NO_MEMORY ? "RiftWii ran out of memory" : "the ctgpupd plugin stopped";
        logf("CTGP-R update: the ctgpupd plugin stopped: %s (its own log line above says where)\n", error.c_str());
        return false;
    }
    if (result == kCtgpUpdDone) return true;
    if (result < 0) {
        // The plugin is missing, from another RiftWii, damaged or did not
        // start: its own sentence says which, and what to do.
        logf("CTGP-R update: needs RiftWii's ctgpupd plugin in %s (%s), and it could not be used: %s\n",
             plugin::Directory().c_str(), plugin_file_name(kPluginName, RIFTWII_VERSION).c_str(), message.c_str());
        error = message;
        return false;
    }
    error = result == RWP_LAUNCH_NO_MEMORY ? "RiftWii ran out of memory"
            : request.error.empty()        ? "the ctgpupd plugin gave no reason"
                                           : request.error;
    return false;
}

bool CtgpUpdater::inspect(CtgpUpdateInfo& out, std::string& error) {
    CtgpUpdRequest request;
    request.op = CtgpUpdOp::Inspect;
    if (!run(request, error)) return false;
    out.component = request.dia_component;
    out.version = request.dia_version;
    out.files = request.dia_files;
    out.bytes = request.update_bytes;
    return true;
}

bool CtgpUpdater::check(CtgpCheckReply& reply, std::string& error) {
    if (!NetStart(error)) return false;
    CtgpUpdRequest request;
    request.op = CtgpUpdOp::Check;
    const s32 mac_rc = net_get_mac_address(request.mac.data());
    if (mac_rc < 0) {
        error = "the console's MAC address could not be read (" + std::to_string(mac_rc) + ")";
        return false;
    }
    next_server_ = 0;
    const char* host = nullptr;
    request.connect_next = [this, &host](std::string& why) {
        while (next_server_ < sizeof kServers / sizeof kServers[0]) {
            host = kServers[next_server_++];
            NetServer server;
            if (!ResolveServer(host, kCtgpCheckPort, server, why)) {
                logf("CTGP-R update: %s\n", why.c_str());
                continue;
            }
            if (!conn_.connect(server, kConnectTimeoutMs, why)) {
                logf("CTGP-R update: %s: %s\n", host, why.c_str());
                continue;
            }
            conn_.set_receive_timeout(15000);
            connected_ = true;
            return true;
        }
        return false;
    };
    request.disconnect = [this]() {
        conn_.close();
        connected_ = false;
    };
    request.io.send = [this](const std::uint8_t* d, std::size_t n) { return conn_.send(d, n); };
    request.io.receive_some = [this](std::uint8_t* out, std::size_t max, std::size_t& got) {
        return conn_.receive_some(out, max, got);
    };
    if (!run(request, error)) {
        if (connected_) {
            conn_.close();
            connected_ = false;
        }
        return false;
    }
    reply = request.reply;
    logf("CTGP-R update: %s answered: status %u, %s\n", host != nullptr ? host : "the server",
         unsigned(reply.status),
         reply.update_waiting ? ("an update of " + std::to_string(reply.update_bytes) + " bytes").c_str()
                              : "no update");
    return true;
}

bool CtgpUpdater::download(std::uint32_t size, const CtgpDownloadProgress& progress, std::string& error) {
    if (!connected_) {
        error = "the update check did not finish";
        return false;
    }
    CtgpUpdRequest request;
    request.op = CtgpUpdOp::Download;
    request.download_bytes = size;
    request.download_progress = progress;
    request.io.send = [this](const std::uint8_t* d, std::size_t n) { return conn_.send(d, n); };
    request.io.receive_some = [this](std::uint8_t* out, std::size_t max, std::size_t& got) {
        return conn_.receive_some(out, max, got);
    };
    const bool ok = run(request, error);
    conn_.close();
    connected_ = false;
    if (!ok) std::remove(kCtgpUpdatePartPath);
    return ok;
}

bool CtgpUpdater::install(const CtgpDiscBase& disc, const CtgpUpdateStep& step, std::string& error) {
    CtgpUpdRequest request;
    request.op = CtgpUpdOp::Install;
    request.disc = disc;
    request.step = step;
    const bool ok = run(request, error);
    if (!ok && request.escaped != 0) {
        // The plugin stopped halfway: its journal on the card is whole (each
        // chunk is saved before it is changed), so it is played back now.
        std::string journal_error;
        if (!CtgpRecoverPackJournal(journal_error))
            logf("CTGP-R update: the pack could not be put back now (%s); the next start tries again\n",
                 journal_error.c_str());
    }
    return ok;
}

bool CtgpRecoverPackJournal(std::string& error) {
    remove_scratch_files();
    if (!exists(kCtgpJournalPath)) return true;
    std::FILE* blob = std::fopen(kCtgprBlobPath, "r+b");
    if (blob == nullptr) {
        error = "a CTGP-R update was interrupted, and sd:/ctgpr/blob.bin could not be opened to undo it";
        logf("CTGP-R update: %s\n", error.c_str());
        return false;
    }
    fseeko(blob, 0, SEEK_END);
    const std::uint64_t size = std::uint64_t(ftello(blob));
    std::size_t restored = 0;
    const CtgpJournalRecovery result = ctgp_journal_recover(
        kCtgpJournalPath, blob_io(blob), size,
        [blob]() { return std::fflush(blob) == 0 && fsync(fileno(blob)) == 0; }, restored, error);
    std::fclose(blob);
    switch (result) {
    case CtgpJournalRecovery::None:
        return true;
    case CtgpJournalRecovery::RolledBack:
        logf("CTGP-R update: an interrupted update was undone (%zu chunks of blob.bin put back)\n", restored);
        return true;
    case CtgpJournalRecovery::Failed:
        break;
    }
    logf("CTGP-R update: an interrupted update could not be undone: %s\n", error.c_str());
    return false;
}

bool CtgpPackReady(std::string& message) {
    std::string error;
    if (CtgpRecoverPackJournal(error)) return true;
    message = tr("An earlier CTGP-R update was interrupted and could not be undone: {1}. Install CTGP-R again with its own installer.", {error});
    return false;
}

}  // namespace riftwii::wii
