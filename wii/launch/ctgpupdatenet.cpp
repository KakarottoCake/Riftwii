// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ctgpupdatenet.hpp"

#include <dirent.h>
#include <network.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <new>
#include <vector>

#include "ctgpr.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "memlimits.hpp"
#include "plugins.hpp"
#include "riftwii/ctgpfetch.hpp"
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

enum class Presence { Absent, Present, Unknown };

// Unknown: the card gave an error other than "not there", so a journal may be
// there; the start-up undo then refuses rather than skip it.
Presence presence(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) return Presence::Present;
    return errno == ENOENT || errno == ENOTDIR ? Presence::Absent : Presence::Unknown;
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

// Test builds (test_limits.txt "jelly_cut_after = N", mem::TestJellyCutAfter):
// the card takes N blocks of in-place jelly.bin writes during an install and
// then no more, the last write cut part way, as a power cut would leave it.
// The plugin's own undo fails too, so the journal stays for the start-up
// replay. Off (0) in other builds.
std::uint32_t g_jelly_blocks_left = 0;
bool g_jelly_cut_armed = false;

bool jelly_write(const Fat32File& file, std::uint64_t offset, const std::uint8_t* data, std::size_t length) {
    if (!g_jelly_cut_armed) return write_sd_file_in_place(file, offset, data, length);
    const std::size_t blocks = length / 512;
    if (g_jelly_blocks_left >= blocks) {
        g_jelly_blocks_left -= std::uint32_t(blocks);
        return write_sd_file_in_place(file, offset, data, length);
    }
    static bool told = false;
    if (g_jelly_blocks_left > 0) write_sd_file_in_place(file, offset, data, std::size_t(g_jelly_blocks_left) * 512);
    if (!told) logf("CTGP-R update: TEST: the card stops taking jelly.bin writes here (jelly_cut_after)\n");
    told = true;
    g_jelly_blocks_left = 0;
    return false;
}

// The DOL's C library calls, as the plugin takes them (CtgpUpdLibc).
int libc_remove(const char* path) { return std::remove(path); }
int libc_rename(const char* from, const char* to) { return std::rename(from, to); }
int libc_fflush(std::FILE* file) { return std::fflush(file); }
int libc_fileno(std::FILE* file) { return fileno(file); }
int libc_fsync(int fd) { return fsync(fd); }
int* libc_errno() { return &errno; }


}  // namespace

CtgpUpdater::~CtgpUpdater() {
    if (connected_) conn_.close();
    // The plugin's arena goes back to the heap (plugins.hpp).
    plugin::ShutdownAll("the CTGP-R update is done");
}

bool CtgpUpdater::run(CtgpUpdRequest& request, std::string& error) {
    bad_update_ = false;
    channel_installs_ = false;
    channel_form_ = false;
    request.layout = ctgpupd_request_layout();
    request.update_path = kCtgpUpdatePath;
    request.part_path = kCtgpUpdatePartPath;
    request.blob_path = kCtgprBlobPath;
    request.journal_path = kCtgpJournalPath;
    request.jelly_journal_path = kCtgpJellyJournalPath;
    request.scratch_prefix = "sd:/ctgpr/update-base-";
    // jelly.bin (Check and Install read it) is found here and kept in this
    // frame until the plugin returns (riftwii/plugin_ctgpupd.hpp).
    Fat32File jelly;
    if (request.op == CtgpUpdOp::Check || request.op == CtgpUpdOp::Install || request.op == CtgpUpdOp::Inspect) {
        std::string why;
        if (resolve_sd_file(kCtgprJellyPath, jelly, why)) {
            request.sd.jelly = &jelly;
        } else {
            request.sd.jelly_error = why;
        }
    }
    request.sd.read = read_sd_file;
    request.sd.write = jelly_write;
    g_jelly_cut_armed = request.op == CtgpUpdOp::Install && mem::TestJellyCutAfter() != 0;
    g_jelly_blocks_left = mem::TestJellyCutAfter();
    if (g_jelly_cut_armed) logf("CTGP-R update: TEST: jelly.bin writes stop after %u blocks\n", unsigned(g_jelly_blocks_left));
    request.libc.remove = libc_remove;
    request.libc.rename = libc_rename;
    request.libc.fflush = libc_fflush;
    request.libc.fileno = libc_fileno;
    request.libc.fsync = libc_fsync;
    request.libc.errno_location = libc_errno;
    request.log = [](const std::string& line) { logf("CTGP-R update: %s\n", line.c_str()); };

    std::string message;
    const std::int32_t result = plugin::Launch(kPluginName, &request, sizeof(request), message);
    g_jelly_cut_armed = false;
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
    bad_update_ = result != RWP_LAUNCH_NO_MEMORY && request.bad_update != 0;
    channel_installs_ = result != RWP_LAUNCH_NO_MEMORY && request.escaped == 0 && request.channel_installs != 0;
    channel_form_ = channel_installs_ && request.channel_installs == 2;
    return false;
}

bool CtgpUpdater::inspect(CtgpUpdateInfo& out, std::string& error) {
    CtgpUpdRequest request;
    request.op = CtgpUpdOp::Inspect;
    if (!run(request, error)) return false;
    out.component = request.dia_component;
    out.version = request.dia_version;
    out.base_version = request.dia_base_version;
    out.pack_version = request.pack_version;
    out.files = request.dia_files;
    out.bytes = request.update_bytes;
    return true;
}

bool CtgpUpdater::check(CtgpCheckReply& reply, std::string& error, bool keep_open) {
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
    // Only download() keeps it: the player may take a while over the prompt.
    if (!keep_open) close_connection();
    logf("CTGP-R update: %s answered: status %u, %s\n", host != nullptr ? host : "the server",
         unsigned(reply.status),
         reply.update_waiting ? ("an update of " + std::to_string(reply.update_bytes) + " bytes").c_str()
                              : "no update");
    return true;
}

void CtgpUpdater::close_connection() {
    if (connected_) conn_.close();
    connected_ = false;
}

// riftwii/ctgpfetch.hpp: a fresh check that must offer the same update, then
// the download at once; once more if the server sends nothing.
bool CtgpUpdater::download(std::uint32_t size, const CtgpDownloadProgress& progress, std::string& error) {
    offer_changed_ = false;
    CtgpFetchSteps steps;
    steps.check = [this](CtgpCheckReply& reply, std::string& why) { return check(reply, why, true); };
    steps.download = [&](bool& closed_at_start, std::string& why) {
        const bool ok = download_now(size, progress, why);
        closed_at_start = !ok && !bad_update_ && ctgp_download_closed_at_start(why);
        return ok;
    };
    steps.close = [this] { close_connection(); };
    steps.log = [](const std::string& line) { logf("CTGP-R update: %s\n", line.c_str()); };
    const CtgpFetch result = ctgp_fetch_offer(size, steps, error);
    offer_changed_ = result == CtgpFetch::OfferChanged;
    return result == CtgpFetch::Done;
}

bool CtgpUpdater::download_now(std::uint32_t size, const CtgpDownloadProgress& progress, std::string& error) {
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

namespace {

bool recover_blob_journal(std::string& error);

// The start-up undo of a HASH update cut off inside jelly.bin. The journal
// holds jelly.bin's bytes as they lie on the card, so the replay needs no
// key: it writes them back in place (never the entry, the FAT or FSInfo).
// The key is made from the entry's times, so the entry is read again after
// the replay and must be exactly what it was: then the key is unchanged.
bool recover_jelly_journal(std::string& error) {
    switch (presence(kCtgpJellyJournalPath)) {
    case Presence::Absent:
        return true;
    case Presence::Unknown:
        error = "the SD card could not say whether sd:/ctgpr/update-journal-jelly.bin is there";
        logf("CTGP-R update: %s\n", error.c_str());
        return false;
    case Presence::Present:
        break;
    }
    forget_sd_layout();
    Fat32File jelly;
    std::string why;
    if (!resolve_sd_file(kCtgprJellyPath, jelly, why)) {
        error = "a CTGP-R update was interrupted, and sd:/ctgpr/jelly.bin could not be found to undo it (" + why + ")";
        logf("CTGP-R update: %s\n", error.c_str());
        return false;
    }
    CtgpPackIo io;
    io.read = [&jelly](std::uint64_t offset, std::uint32_t bytes, std::uint8_t* out) {
        return read_sd_file(jelly, offset, out, bytes);
    };
    io.write = [&jelly](std::uint64_t offset, const std::uint8_t* data, std::uint32_t bytes) {
        return write_sd_file_in_place(jelly, offset, data, bytes);
    };
    std::size_t restored = 0;
    const CtgpJournalRecovery result = ctgp_journal_recover(
        kCtgpJellyJournalPath, io, jelly.entry.size & ~std::uint64_t(511), [] { return true; }, restored, error);
    if (result == CtgpJournalRecovery::Failed) {
        logf("CTGP-R update: an interrupted jelly.bin update could not be undone: %s\n", error.c_str());
        return false;
    }
    forget_sd_layout();
    Fat32File after;
    if (!resolve_sd_file(kCtgprJellyPath, after, why) || after.entry.write_time != jelly.entry.write_time ||
        after.entry.write_date != jelly.entry.write_date || after.entry.create_time != jelly.entry.create_time ||
        after.entry.create_date != jelly.entry.create_date || after.entry.size != jelly.entry.size ||
        after.entry.first_cluster != jelly.entry.first_cluster) {
        error = "jelly.bin's directory entry is not what it was before the undo";
        logf("CTGP-R update: %s\n", error.c_str());
        return false;
    }
    if (result == CtgpJournalRecovery::RolledBack)
        logf("CTGP-R update: an interrupted hash-list update was undone (%zu chunks of jelly.bin put back; its "
             "entry and key unchanged)\n",
             restored);
    return true;
}

}  // namespace

bool CtgpRecoverPackJournal(std::string& error) {
    remove_scratch_files();
    // Both are tried, whatever the other gives: one that can't be undone
    // now must not leave the other half written.
    std::string jelly_error;
    const bool jelly_ok = recover_jelly_journal(jelly_error);
    const bool blob_ok = recover_blob_journal(error);
    if (!jelly_ok) {
        error = jelly_error;
        return false;
    }
    return blob_ok;
}

namespace {

bool recover_blob_journal(std::string& error) {
    switch (presence(kCtgpJournalPath)) {
    case Presence::Absent:
        return true;
    case Presence::Unknown:
        error = "the SD card could not say whether sd:/ctgpr/update-journal.bin is there";
        logf("CTGP-R update: %s\n", error.c_str());
        return false;
    case Presence::Present:
        break;
    }
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

}  // namespace

bool CtgpPackReady(std::string& message) {
    std::string error;
    if (CtgpRecoverPackJournal(error)) return true;
    message = tr("An earlier CTGP-R update was interrupted and could not be undone: {1}. Install CTGP-R again with its own installer.", {error});
    return false;
}

}  // namespace riftwii::wii
