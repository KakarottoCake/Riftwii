// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The ctgpupd plugin (docs/PLUGINS.md, "The ctgpupd plugin"): RiftWii's own
// CTGP-R update check, download and install, loaded into the plugin arena in
// MEM2 only when an update waits on the card or a check runs; the DOL shuts
// it down when it is done. Its table's launch takes the DOL's CtgpUpdRequest
// (riftwii/plugin_ctgpupd.hpp), runs the one operation the request names,
// and returns kCtgpUpdDone, or RWP_LAUNCH_FAILED or RWP_LAUNCH_NO_MEMORY with
// the reason in the request.
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

#include "riftwii/ctgpcheck.hpp"
#include "riftwii/ctgpefa.hpp"
#include "riftwii/ctgpjournal.hpp"
#include "riftwii/ctgppackages.hpp"
#include "riftwii/ctgppackwrite.hpp"
#include "riftwii/ctgptables.hpp"
#include "riftwii/ctgpupdate.hpp"
#include "riftwii/ctgpupdater.hpp"
#include "riftwii/plugin_ctgpupd.hpp"
#include "rwpruntime.hpp"

namespace {

using riftwii::CtgpUpdRequest;

// Its own version, for logs; the build stamp is what has to match.
constexpr std::uint32_t kCtgpUpdVersion = 1;

// The table of an operation's heap blocks (rwptrack.hpp), from the heap:
// 16K slots (64 KB), room for 12K blocks held at once. An install holds a
// few dozen.
constexpr std::uint32_t kTrackSlots = 16 * 1024;

// The DOL's C library calls (CtgpUpdLibc), for the length of an operation.
const riftwii::CtgpUpdLibc* g_libc = nullptr;

void log_line(const CtgpUpdRequest& request, const std::string& line) {
    if (request.log) request.log(line);
}

riftwii::CtgpPackIo file_io(std::FILE* file, bool writable) {
    riftwii::CtgpPackIo io;
    io.read = [file](std::uint64_t offset, std::uint32_t bytes, std::uint8_t* out) {
        return fseeko(file, static_cast<off_t>(offset), SEEK_SET) == 0 && std::fread(out, 1, bytes, file) == bytes;
    };
    if (writable) {
        io.write = [file](std::uint64_t offset, const std::uint8_t* data, std::uint32_t bytes) {
            return fseeko(file, static_cast<off_t>(offset), SEEK_SET) == 0 &&
                   std::fwrite(data, 1, bytes, file) == bytes;
        };
    } else {
        io.write = [](std::uint64_t, const std::uint8_t*, std::uint32_t) { return false; };
    }
    return io;
}

std::uint64_t file_size(std::FILE* file) {
    fseeko(file, 0, SEEK_END);
    const std::uint64_t size = static_cast<std::uint64_t>(ftello(file));
    fseeko(file, 0, SEEK_SET);
    return size;
}

bool read_list(riftwii::CtgpPackWriter& disk, std::vector<riftwii::CtgpPackage>& out, std::string& error) {
    std::vector<std::uint8_t> bytes;
    return disk.read_file("/PACKAGES.BIN", bytes, error) &&
           riftwii::ctgp_parse_packages(bytes.data(), bytes.size(), out, error);
}

// jelly.bin, read only. Its key is made from its FAT times, so it is found
// through the card's own directory entry (the DOL's raw reads).
bool open_jelly(const CtgpUpdRequest& request, riftwii::CtgpPackWriter& disk, std::string& error) {
    // Found and kept by the DOL (riftwii/plugin_ctgpupd.hpp): only borrowed
    // here, never copied, so nothing the DOL allocated is in the plugin's hands.
    const riftwii::Fat32File* const jelly = request.sd.jelly;
    if (jelly == nullptr) {
        error = request.sd.jelly_error.empty() ? std::string("jelly.bin was not found") : request.sd.jelly_error;
        return false;
    }
    const auto key = riftwii::ctgp_efb_key(riftwii::ctgp_jelly_key_words(), jelly->entry.write_time,
                                           jelly->entry.write_date, jelly->entry.create_time, jelly->entry.create_date);
    riftwii::CtgpPackIo io;
    const auto read = request.sd.read;
    io.read = [jelly, read](std::uint64_t offset, std::uint32_t bytes, std::uint8_t* out) {
        return read(*jelly, offset, out, bytes);
    };
    io.write = [](std::uint64_t, const std::uint8_t*, std::uint32_t) { return false; };
    return disk.open(io, jelly->entry.size & ~511ull, key, riftwii::ctgp_blob_chunk_iv(), error);
}

bool read_hash_list(const CtgpUpdRequest& request, std::vector<std::uint8_t>& out, std::string& error) {
    riftwii::CtgpPackWriter disk;
    const bool ok = open_jelly(request, disk, error) && disk.read_file("/PACKAGES/HASH/CTGPR.HSH", out, error);
    disk.close();
    if (!ok) error = "jelly.bin: " + error;
    return ok;
}

// The update's header and tables, from the first bytes of the file.
bool read_dia(const std::string& path, std::uint64_t expected, riftwii::CtgpDia& dia, std::uint64_t& length,
              std::string& error) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        error = path + " could not be opened";
        return false;
    }
    length = file_size(file);
    constexpr std::uint64_t kUpdateMax = 64u << 20;
    bool ok = length > 0 && length <= kUpdateMax && (expected == 0 || length == expected);
    std::vector<std::uint8_t> prefix;
    if (ok) {
        prefix.resize(length < riftwii::kCtgpDiaPrefixBytes ? std::size_t(length) : riftwii::kCtgpDiaPrefixBytes);
        ok = std::fread(prefix.data(), 1, prefix.size(), file) == prefix.size();
    }
    std::fclose(file);
    if (!ok) {
        error = path + " is empty, too large or unreadable";
        return false;
    }
    return riftwii::ctgp_dia_parse_prefix(prefix.data(), prefix.size(), length, dia, error);
}

// A package version as CTGP-R shows it: 0x01031193 is "1.03.1193".
std::string version_text(std::uint32_t version) {
    char text[16];
    std::snprintf(text, sizeof text, "%x.%02x.%04x", unsigned(version >> 24), unsigned((version >> 16) & 0xFF),
                  unsigned(version & 0xFFFF));
    return text;
}

// The version the update rebuilds from: its base record for its own
// component (an update with none rebuilds from itself).
std::uint32_t base_version_of(const riftwii::CtgpDia& dia) {
    std::uint32_t base_version = dia.version;
    for (const riftwii::CtgpDiaBase& base : dia.bases) {
        if (base.component == dia.component) base_version = base.version;
    }
    return base_version;
}

// The version of `component` blob.bin holds now; 0 when it can't be read.
std::uint32_t pack_version(const CtgpUpdRequest& request, const std::string& component) {
    std::FILE* blob = std::fopen(request.blob_path.c_str(), "rb");
    if (blob == nullptr) return 0;
    const std::uint64_t size = file_size(blob);
    riftwii::CtgpPackWriter disk;
    std::vector<riftwii::CtgpPackage> packages;
    std::string error;
    std::uint32_t version = 0;
    if (disk.open(file_io(blob, false), size & ~511ull, error) && read_list(disk, packages, error))
        version = riftwii::ctgp_package_version(packages, component);
    disk.close();
    std::fclose(blob);
    return version;
}

// The whole file at `path` against CTGP-R's signature (ctgpupdate.hpp): an
// update changed after CTGP-R signed it, its tables included, is refused.
bool verify_signature(const std::string& path, std::string& error) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        error = path + " could not be opened";
        return false;
    }
    const std::uint64_t size = file_size(file);
    const riftwii::CtgpUpdateRead read = [file](std::uint64_t offset, std::uint8_t* out, std::size_t length) {
        return fseeko(file, static_cast<off_t>(offset), SEEK_SET) == 0 && std::fread(out, 1, length, file) == length;
    };
    const bool ok = riftwii::ctgp_dia_verify_signature(read, size, riftwii::ctgp_signing_key(), nullptr, error);
    std::fclose(file);
    return ok;
}

bool inspect(CtgpUpdRequest& request) {
    riftwii::CtgpDia dia;
    std::uint64_t length = 0;
    if (!read_dia(request.update_path, 0, dia, length, request.error)) return false;
    if (!verify_signature(request.update_path, request.error)) return false;
    request.dia_component = dia.component;
    request.dia_version = dia.version;
    request.dia_base_version = base_version_of(dia);
    request.pack_version = pack_version(request, dia.component);
    request.dia_files = static_cast<std::uint32_t>(dia.sections.size());
    request.update_bytes = length;
    return true;
}

bool installed_packages(const CtgpUpdRequest& request, std::vector<riftwii::CtgpPackage>& out, std::string& error) {
    std::vector<riftwii::CtgpPackage> package_disk, hash_disk;
    {
        std::FILE* blob = std::fopen(request.blob_path.c_str(), "rb");
        if (blob == nullptr) {
            error = request.blob_path + " could not be opened";
            return false;
        }
        const std::uint64_t size = file_size(blob);
        riftwii::CtgpPackWriter disk;
        const bool ok = disk.open(file_io(blob, false), size & ~511ull, error) && read_list(disk, package_disk, error);
        disk.close();
        std::fclose(blob);
        if (!ok) {
            error = "blob.bin: " + error;
            return false;
        }
    }
    {
        riftwii::CtgpPackWriter disk;
        const bool ok = open_jelly(request, disk, error) && read_list(disk, hash_disk, error);
        disk.close();
        if (!ok) {
            error = "jelly.bin: " + error;
            return false;
        }
    }
    out = riftwii::ctgp_check_records(package_disk, hash_disk);
    return true;
}

bool check(CtgpUpdRequest& request) {
    std::vector<riftwii::CtgpPackage> packages;
    if (!installed_packages(request, packages, request.error)) {
        request.error = "the installed versions could not be read: " + request.error;
        return false;
    }
    std::string versions;
    for (const riftwii::CtgpPackage& p : packages) {
        char line[40];
        std::snprintf(line, sizeof line, " %s %08x", p.name.c_str(), static_cast<unsigned>(p.version));
        versions += line;
    }
    log_line(request, "checking for an update; installed:" + versions);
    std::vector<std::uint8_t> message;
    if (!riftwii::ctgp_check_request(request.mac, packages, message, request.error)) return false;
    std::string last_error;
    for (;;) {
        std::string why;  // the DOL's to fill (see open_jelly)
        if (!request.connect_next(why)) {
            if (!why.empty()) last_error = why;
            break;
        }
        if (riftwii::ctgp_check_exchange(request.io, message, request.reply, last_error)) return true;
        log_line(request, last_error);
        if (request.disconnect) request.disconnect();
    }
    request.error = last_error.empty() ? "no update server could be reached" : last_error;
    return false;
}

bool download(CtgpUpdRequest& request) {
    const char* const part_path = request.part_path.c_str();
    g_libc->remove(part_path);
    std::FILE* part = std::fopen(part_path, "wb");
    if (part == nullptr) {
        request.error = request.part_path + " could not be created";
        return false;
    }
    // One 32 KiB piece in memory; the rest goes straight to the card.
    bool ok = riftwii::ctgp_download(
        request.io, request.download_bytes,
        [part](const std::uint8_t* d, std::size_t n) { return std::fwrite(d, 1, n, part) == n; },
        request.download_progress, request.error);
    if (std::fclose(part) != 0 && ok) {
        ok = false;
        request.error = "the update could not be saved to the card";
    }
    if (ok) {
        // The header and tables must read as a DIA0 of the size offered.
        riftwii::CtgpDia dia;
        std::uint64_t length = 0;
        ok = read_dia(request.part_path, request.download_bytes, dia, length, request.error) &&
             verify_signature(request.part_path, request.error);
        if (ok) {
            char line[80];
            std::snprintf(line, sizeof line, "downloaded %s version %08x", dia.component.c_str(),
                          static_cast<unsigned>(dia.version));
            log_line(request, line);
        }
    }
    if (ok) {
        g_libc->remove(request.update_path.c_str());
        if (g_libc->rename(part_path, request.update_path.c_str()) != 0) {
            ok = false;
            request.error = "the update could not be renamed to " + request.update_path;
        }
    }
    if (!ok) g_libc->remove(part_path);
    return ok;
}

// The pack's /PACKAGES.BIN names CTGPR's version: the next check must see
// the new one, or it would offer the same update again. A failure fails the
// install, which the journal then rolls back.
bool note_version(riftwii::CtgpPackWriter& pack, std::uint32_t version, std::string& error) {
    std::vector<std::uint8_t> list;
    if (!pack.read_file("/PACKAGES.BIN", list, error) || list.size() < 16) {
        error = "the pack's version list could not be read: " + error;
        return false;
    }
    const std::uint32_t count = (std::uint32_t(list[12]) << 24) | (std::uint32_t(list[13]) << 16) |
                                (std::uint32_t(list[14]) << 8) | list[15];
    for (std::uint32_t i = 0; i < count && 16 + i * 16 + 16 <= list.size(); ++i) {
        std::uint8_t* record = list.data() + 16 + i * 16;
        if (std::memcmp(record, "CTGPR", 5) != 0) continue;
        record[8] = std::uint8_t(version >> 24);
        record[9] = std::uint8_t(version >> 16);
        record[10] = std::uint8_t(version >> 8);
        record[11] = std::uint8_t(version);
    }
    if (!pack.replace_file("/PACKAGES.BIN", list.data(), list.size(), error)) {
        error = "the pack's version list was not updated: " + error;
        return false;
    }
    return true;
}

// The DOL's disc reader, borrowed for the operation rather than copied (a
// copy's state would be the DOL's allocations, which an escape could not
// give back). The sink it is handed runs under the DOL's frames, which hold
// the USB lock and buffers of their own, so it runs guarded: an escape inside
// it returns false to the DOL, which then cleans up and returns, and the
// escape goes on from here.
struct GuardedSink {
    const riftwii::CtgpDiscSink* sink;
    const std::uint8_t* data;
    std::size_t length;
};

bool call_sink(void* context) {
    const auto* call = static_cast<const GuardedSink*>(context);
    return (*call->sink)(call->data, call->length);
}

riftwii::CtgpDiscBase borrow_disc(const CtgpUpdRequest& request) {
    if (!request.disc) return riftwii::CtgpDiscBase();
    const riftwii::CtgpDiscBase* const disc = &request.disc;
    return [disc](const std::string& path, const riftwii::CtgpDiscSink& sink, std::uint32_t* size, std::string& why) {
        const riftwii::CtgpDiscSink guarded = [&sink](const std::uint8_t* data, std::size_t length) {
            GuardedSink call{&sink, data, length};
            return riftwii::plugin::run_guarded(call_sink, &call);
        };
        const bool ok = (*disc)(path, guarded, size, why);
        riftwii::plugin::raise_pending();
        return ok;
    };
}

// The install against an open, journaled pack.
bool install_into(CtgpUpdRequest& request, riftwii::CtgpPackWriter& pack, const riftwii::CtgpDia& dia,
                  std::uint64_t length) {
    std::string& error = request.error;
    // The update rebuilds from the files the pack has now: it only fits the
    // version its header names. A pack of another version would be rebuilt
    // from the wrong bytes, so it is refused.
    std::vector<riftwii::CtgpPackage> packages;
    std::uint32_t installed = 0;
    if (read_list(pack, packages, error)) installed = riftwii::ctgp_package_version(packages, "CTGPR");
    const std::uint32_t base_version = base_version_of(dia);
    if (installed != base_version) {
        error = "this pack is CTGP-R " + version_text(installed) + ", but the update rebuilds from " +
                version_text(base_version);
        return false;
    }
    // Every rebuilt file is checked against jelly.bin's CTGPR.HSH before it
    // is written. CTGP-R's channel keeps that list at the newest version it
    // knows, so a file that changed again after this update's version does
    // not match and the update is refused: safe, and the next update brings
    // the pack up to the list. Without the list nothing could be checked, so
    // the install does not start.
    std::vector<std::uint8_t> hash_list;
    if (!read_hash_list(request, hash_list, error)) {
        error = "no hash list to check the files against: " + error;
        return false;
    }
    // A list older than the update can't vouch for its files: say so plainly
    // rather than have every file fail its check. (The list's own checks come
    // later, in ctgp_hash_list_digest; here only its label is read.)
    if (hash_list.size() >= 0x2C) {
        const std::uint32_t label = (std::uint32_t(hash_list[0x28]) << 24) | (std::uint32_t(hash_list[0x29]) << 16) |
                                    (std::uint32_t(hash_list[0x2A]) << 8) | hash_list[0x2B];
        if (label < dia.version) {
            error = "CTGP-R's hash list in jelly.bin is for " + version_text(label) + ", older than this update (" +
                    version_text(dia.version) + "), so its files can't be checked and nothing was installed";
            return false;
        }
    }
    riftwii::CtgpPackUpdateTarget target(pack, borrow_disc(request), std::move(hash_list), request.scratch_prefix);
    log_line(request, "installing");
    std::FILE* update_file = std::fopen(request.update_path.c_str(), "rb");
    if (update_file == nullptr) {
        error = "the update file could not be reopened";
        return false;
    }
    const riftwii::CtgpUpdateRead read_update = [update_file](std::uint64_t offset, std::uint8_t* out,
                                                              std::size_t bytes) {
        return fseeko(update_file, static_cast<off_t>(offset), SEEK_SET) == 0 &&
               std::fread(out, 1, bytes, update_file) == bytes;
    };
    const bool ok = riftwii::ctgp_update_apply(
        read_update, length, target, [&](const std::string& line) { log_line(request, line); },
        riftwii::CtgpUpdateFileDone(), request.step, error);
    std::fclose(update_file);
    return ok && note_version(pack, dia.version, error);
}

bool install(CtgpUpdRequest& request) {
    std::string& error = request.error;
    riftwii::CtgpDia dia;
    std::uint64_t length = 0;
    if (!read_dia(request.update_path, 0, dia, length, error)) return false;
    request.dia_component = dia.component;
    request.dia_version = dia.version;
    request.dia_files = static_cast<std::uint32_t>(dia.sections.size());
    request.update_bytes = length;
    // Checked again here, before anything is written: every byte the install
    // reads is then CTGP-R's.
    if (!verify_signature(request.update_path, error)) return false;

    std::FILE* blob = std::fopen(request.blob_path.c_str(), "r+b");
    if (blob == nullptr) {
        error = request.blob_path + " could not be opened for writing";
        return false;
    }
    const std::uint64_t blob_size = file_size(blob);
    const auto sync_blob = [blob]() { return g_libc->fflush(blob) == 0 && g_libc->fsync(g_libc->fileno(blob)) == 0; };
    // Every 32 KiB of blob.bin the update changes is copied to the card
    // first, so a power cut or crash halfway can be undone at the next start
    // (the DOL's CtgpRecoverPackJournal).
    riftwii::CtgpPackJournal journal;
    bool ok = false;
    const bool journaled = journal.begin(request.journal_path, file_io(blob, true), blob_size, error);
    riftwii::CtgpPackWriter pack;
    if (!journaled) {
        error = "the undo journal could not be started: " + error;
    } else if (pack.open(journal.io(), blob_size, error)) {
        ok = install_into(request, pack, dia, length);
    }
    // The writer keeps the last chunk it changed: write it out before the
    // journal is closed either way.
    pack.close();
    if (journaled) {
        std::string journal_error;
        if (ok && !journal.commit(sync_blob, journal_error)) {
            // blob.bin may not be on the card as written, or the journal is
            // still there and would undo the update at the next start: the
            // update is not installed, and is rolled back now.
            ok = false;
            error = "the update could not be finished: " + journal_error;
        }
        if (ok) {
            // Installed: blob.bin is synced and the journal is gone.
        } else if (journal.rollback(sync_blob, journal_error)) {
            log_line(request, "the pack was put back as it was (" + std::to_string(journal.chunks()) + " chunks)");
        } else {
            log_line(request, "the pack could not be put back now (" + journal_error + "); the next start tries again");
        }
    }
    std::fclose(blob);
    return ok;
}

// The operation inside escape()'s window (rwpruntime.hpp). Kept out of line:
// __builtin_setjmp's caller and this must be different functions.
__attribute__((noinline)) int32_t run_operation(CtgpUpdRequest& request) {
    bool ok = false;
    switch (request.op) {
    case riftwii::CtgpUpdOp::Inspect: ok = inspect(request); break;
    case riftwii::CtgpUpdOp::Check: ok = request.connect_next && check(request); break;
    case riftwii::CtgpUpdOp::Download: ok = download(request); break;
    case riftwii::CtgpUpdOp::Install: ok = install(request); break;
    default: request.error = "no such operation"; break;
    }
    return ok ? riftwii::kCtgpUpdDone : RWP_LAUNCH_FAILED;
}

int32_t ctgpupd_launch(const void* raw, uint32_t bytes) {
    std::string why;
    if (!riftwii::ctgpupd_request_check(raw, bytes, why)) {
        riftwii::plugin::host().log("refused the request: %s", why.c_str());
        return RWP_LAUNCH_FAILED;
    }
    auto& request = *static_cast<CtgpUpdRequest*>(const_cast<void*>(raw));
    g_libc = &request.libc;
    // An escape unwinds nothing, so the operation's heap blocks are tracked
    // and given back after one. The table is taken before tracking starts.
    void** const track = static_cast<void**>(riftwii::plugin::host().mem1_alloc(kTrackSlots * sizeof(void*)));
    if (track == nullptr) {
        g_libc = nullptr;
        riftwii::plugin::host().log("no room to track the update's heap blocks: it does not start");
        return RWP_LAUNCH_NO_MEMORY;
    }
    if (__builtin_setjmp(riftwii::plugin::escape_buffer()) != 0) {
        // Back from escape(): the files the operation had open are closed
        // first, so what blob.bin's and the journal's streams still held is
        // written before the DOL plays the journal back. Then every heap
        // block the operation still held goes back, the request's strings
        // among those it filled.
        const std::uint32_t streams = riftwii::plugin::close_open_streams();
        const std::uint32_t freed = riftwii::plugin::free_tracked_blocks();
        riftwii::plugin::host().mem1_free(track);
        g_libc = nullptr;
        request.escaped = 1;
        const riftwii::plugin::Escape taken = riftwii::plugin::escape_taken();
        riftwii::plugin::host().log("the update stopped: %s; %u files closed, %u heap blocks given back",
                                    taken.why ? taken.why : "no reason given", static_cast<unsigned>(streams),
                                    static_cast<unsigned>(freed));
        return taken.result;
    }
    riftwii::plugin::track_blocks(track, kTrackSlots);
    riftwii::plugin::arm_escape();
    const int32_t result = run_operation(request);
    riftwii::plugin::disarm_escape();
    riftwii::plugin::untrack_blocks();
    riftwii::plugin::host().mem1_free(track);
    g_libc = nullptr;
    return result;
}

}  // namespace

extern "C" {

int32_t rwp_init(const rwp_host* host);
void rwp_shutdown(void);

// extern: a C++ const at namespace scope would otherwise be local to this
// file, and make_plugin.py looks the table up by this name.
extern const rwp_plugin rwp_exports = {
    sizeof(rwp_plugin), RWP_API_VERSION, kCtgpUpdVersion, 0, nullptr, rwp_shutdown, ctgpupd_launch,
};

int32_t rwp_init(const rwp_host* host) {
    if (!riftwii::plugin::start_runtime(host)) return 1;
    host->log("version %u ready, its code at 0x%08x, for RiftWii %s (build %s)", kCtgpUpdVersion,
              static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&rwp_init)), host->version(),
              host->build_stamp());
    return 0;
}

void rwp_shutdown(void) {}

// The C library calls the runtime has no host function for, on the DOL's own
// (CtgpUpdLibc). Only called inside an operation.
int remove(const char* path) { return g_libc != nullptr ? g_libc->remove(path) : -1; }
int rename(const char* from, const char* to) { return g_libc != nullptr ? g_libc->rename(from, to) : -1; }
int fflush(FILE* file) { return g_libc != nullptr ? g_libc->fflush(file) : -1; }
// errno is the DOL's, which its stat (behind the runtime's) sets.
int* __errno(void) {
    static int own = 0;
    return g_libc != nullptr ? g_libc->errno_location() : &own;
}
int fileno(FILE* file) { return g_libc != nullptr ? g_libc->fileno(file) : -1; }
int fsync(int fd) { return g_libc != nullptr ? g_libc->fsync(fd) : -1; }

}  // extern "C"
