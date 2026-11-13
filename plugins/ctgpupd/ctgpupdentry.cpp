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
#include "riftwii/ctgphashupdate.hpp"
#include "riftwii/ctgpjournal.hpp"
#include "riftwii/ctgppackages.hpp"
#include "riftwii/ctgppackwrite.hpp"
#include "riftwii/ctgppending.hpp"
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

// riftwii/ctgppending.hpp: false when the card would not give the size.
bool file_size(std::FILE* file, std::uint64_t& size) { return riftwii::ctgp_file_size(file, size); }

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

// The version of `component` jelly.bin's /PACKAGES.BIN names; 0 when it
// can't be read.
std::uint32_t jelly_version(const CtgpUpdRequest& request, const std::string& component) {
    riftwii::CtgpPackWriter disk;
    std::vector<riftwii::CtgpPackage> packages;
    std::string error;
    std::uint32_t version = 0;
    if (open_jelly(request, disk, error) && read_list(disk, packages, error))
        version = riftwii::ctgp_package_version(packages, component);
    disk.close();
    return version;
}

// The update's header and tables, from the first bytes of the file.
// `bad` (may be null) is set when the file was read and is not a DIA0 of the
// right size, rather than the card failing to give it.
bool read_dia(const std::string& path, std::uint64_t expected, riftwii::CtgpDia& dia, std::uint64_t& length,
              std::string& error, bool* bad = nullptr) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        error = path + " could not be opened";
        return false;
    }
    if (!file_size(file, length)) {
        std::fclose(file);
        error = path + ": the SD card would not give its size";
        return false;
    }
    constexpr std::uint64_t kUpdateMax = 64u << 20;
    const bool sized = length > 0 && length <= kUpdateMax && (expected == 0 || length == expected);
    bool ok = sized;
    std::vector<std::uint8_t> prefix;
    if (ok) {
        prefix.resize(length < riftwii::kCtgpDiaPrefixBytes ? std::size_t(length) : riftwii::kCtgpDiaPrefixBytes);
        ok = std::fread(prefix.data(), 1, prefix.size(), file) == prefix.size();
    }
    std::fclose(file);
    if (!ok) {
        error = path + " is empty, too large or unreadable";
        if (bad != nullptr && !sized) *bad = true;
        return false;
    }
    // An update for another package can never install here: refused before
    // any version is compared or file read.
    if (!riftwii::ctgp_dia_parse_prefix(prefix.data(), prefix.size(), length, dia, error) ||
        !riftwii::ctgp_dia_supported(dia, error)) {
        if (bad != nullptr) *bad = true;
        return false;
    }
    return true;
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
    std::uint64_t size = 0;
    if (!file_size(blob, size)) {
        std::fclose(blob);
        return 0;
    }
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
// `bad` (may be null) is set when the whole file was read and the signature
// does not check out, rather than the card failing to give it.
bool verify_signature(const std::string& path, std::string& error, bool* bad = nullptr) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        error = path + " could not be opened";
        return false;
    }
    std::uint64_t size = 0;
    if (!file_size(file, size)) {
        std::fclose(file);
        error = path + ": the SD card would not give its size";
        return false;
    }
    bool read_failed = false;
    const riftwii::CtgpUpdateRead read = [file, &read_failed](std::uint64_t offset, std::uint8_t* out,
                                                             std::size_t length) {
        const bool ok =
            fseeko(file, static_cast<off_t>(offset), SEEK_SET) == 0 && std::fread(out, 1, length, file) == length;
        if (!ok) read_failed = true;
        return ok;
    };
    const bool ok = riftwii::ctgp_dia_verify_signature(read, size, riftwii::ctgp_signing_key(), nullptr, error);
    std::fclose(file);
    if (!ok && !read_failed && bad != nullptr) *bad = true;
    return ok;
}

bool inspect(CtgpUpdRequest& request) {
    riftwii::CtgpDia dia;
    std::uint64_t length = 0;
    bool bad = false;
    if (!read_dia(request.update_path, 0, dia, length, request.error, &bad) ||
        !verify_signature(request.update_path, request.error, &bad)) {
        request.bad_update = bad ? 1 : 0;
        return false;
    }
    request.dia_component = dia.component;
    request.dia_version = dia.version;
    request.dia_base_version = base_version_of(dia);
    request.pack_version = dia.component == "HASH" ? jelly_version(request, "HASH") : pack_version(request, dia.component);
    // A HASH update brings whole lists: it goes onto any older set.
    if (dia.component == "HASH" && request.pack_version != 0 && request.pack_version < dia.version)
        request.dia_base_version = request.pack_version;
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
        std::uint64_t size = 0;
        if (!file_size(blob, size)) {
            std::fclose(blob);
            error = request.blob_path + ": the SD card would not give its size";
            return false;
        }
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
    bool bad = false;
    if (ok) {
        // The header and tables must read as a DIA0 of the size offered,
        // for CTGPR, signed by CTGP-R. One that can never install (`bad`) is
        // kept as update.dif.bad, as a waiting one would be.
        riftwii::CtgpDia dia;
        std::uint64_t length = 0;
        ok = read_dia(request.part_path, request.download_bytes, dia, length, request.error, &bad) &&
             verify_signature(request.part_path, request.error, &bad);
        if (ok) {
            char line[80];
            std::snprintf(line, sizeof line, "downloaded %s version %08x", dia.component.c_str(),
                          static_cast<unsigned>(dia.version));
            log_line(request, line);
        }
    }
    const std::string bad_path = request.update_path + ".bad";
    const auto remove = [](const char* path) { return g_libc->remove(path); };
    const auto rename = [](const char* from, const char* to) { return g_libc->rename(from, to); };
    switch (riftwii::ctgp_finish_download(ok, !ok && bad, request.part_path, request.update_path, bad_path, remove,
                                          rename)) {
    case riftwii::CtgpDownloadEnd::Kept:
        return true;
    case riftwii::CtgpDownloadEnd::SetAside:
        request.bad_update = 1;
        log_line(request, "the download can't be used, kept as " + bad_path + ": " + request.error);
        return false;
    case riftwii::CtgpDownloadEnd::Removed:
        if (ok) request.error = "the update could not be renamed to " + request.update_path;
        return false;
    }
    return false;
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
                  std::uint64_t length, riftwii::CtgpUpdateFailure& failure) {
    failure = riftwii::CtgpUpdateFailure::Temporary;
    std::string& error = request.error;
    // The update rebuilds from the files the pack has now: it only fits the
    // version its header names. A pack of another version would be rebuilt
    // from the wrong bytes, so it is refused.
    // Only CTGPR is installed here (a HASH update goes to install_hash; this
    // keeps the version compare below about the same package).
    if (!riftwii::ctgp_dia_is_pack_update(dia, error)) {
        failure = riftwii::CtgpUpdateFailure::BadUpdate;
        return false;
    }
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
            // Short enough for the menu's notice (140 characters) to show whole.
            error = "CTGP-R's hash list in jelly.bin is for " + version_text(label) + ", older than this update (" +
                    version_text(dia.version) + "), so its files can't be checked";
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
        riftwii::CtgpUpdateFileDone(), request.step, failure, error);
    std::fclose(update_file);
    return ok && note_version(pack, dia.version, error);
}

// A HASH update: CTGP-R's new signed hash lists, into jelly.bin. Every list
// is rebuilt and checked against CTGP-R's signature in memory first; then
// they are written where jelly.bin already lies on the card (the DOL's
// sd.write), never its directory entry, so its key stays the same; through
// an undo journal; read back and checked; and only then is the journal
// removed (riftwii/ctgphashupdate.hpp).
bool install_hash(CtgpUpdRequest& request, const riftwii::CtgpDia& dia, std::uint64_t length) {
    std::string& error = request.error;
    const riftwii::Fat32File* const jelly = request.sd.jelly;
    if (jelly == nullptr) {
        error = "jelly.bin: " + (request.sd.jelly_error.empty() ? std::string("not found") : request.sd.jelly_error);
        return false;
    }
    const std::uint32_t installed = jelly_version(request, "HASH");
    if (installed == 0) {
        error = "jelly.bin's version list could not be read";
        return false;
    }
    if (dia.version <= installed) {
        error = "jelly.bin already has hash lists " + version_text(installed) + ", not older than this update (" +
                version_text(dia.version) + ")";
        return false;
    }
    if (request.step) request.step(0, 1, "jelly.bin", 0.0, 0.0);
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
    std::vector<riftwii::CtgpHashUpdateList> lists;
    riftwii::CtgpUpdateFailure failure = riftwii::CtgpUpdateFailure::Temporary;
    const bool built = riftwii::ctgp_hash_update_build(read_update, length, dia, lists, failure, error);
    std::fclose(update_file);
    if (!built) {
        // Nothing was written: a broken update can be set aside at once.
        if (failure == riftwii::CtgpUpdateFailure::BadUpdate) request.bad_update = 1;
        return false;
    }
    for (const riftwii::CtgpHashUpdateList& list : lists)
        log_line(request, list.disk_path + ": " + std::to_string(list.bytes.size()) + " bytes, signed list for " +
                              version_text(list.label));
    if (request.step) request.step(0, 1, "jelly.bin", 0.3, 0.3);
    const auto key = riftwii::ctgp_efb_key(riftwii::ctgp_jelly_key_words(), jelly->entry.write_time,
                                           jelly->entry.write_date, jelly->entry.create_time, jelly->entry.create_date);
    riftwii::CtgpPackIo raw;
    const auto read = request.sd.read;
    const auto write = request.sd.write;
    raw.read = [jelly, read](std::uint64_t offset, std::uint32_t bytes, std::uint8_t* out) {
        return read(*jelly, offset, out, bytes);
    };
    raw.write = [jelly, write](std::uint64_t offset, const std::uint8_t* data, std::uint32_t bytes) {
        return write(*jelly, offset, data, bytes);
    };
    bool rolled_back = true;
    // libogc's SD driver writes synchronously: a raw write is on the card
    // when it returns.
    const bool ok = riftwii::ctgp_hash_update_install(raw, jelly->entry.size & ~511ull, key, riftwii::ctgp_blob_chunk_iv(),
                                                      lists, dia.version, request.jelly_journal_path,
                                                      [] { return true; }, rolled_back, error);
    if (ok) {
        if (request.step) request.step(0, 1, "jelly.bin", 1.0, 1.0);
        log_line(request, "jelly.bin now has hash lists " + version_text(dia.version) +
                              " (written in place; its entry and key unchanged)");
    } else if (rolled_back) {
        log_line(request, "jelly.bin was left as it was");
    }
    return ok;
}

bool install(CtgpUpdRequest& request) {
    std::string& error = request.error;
    riftwii::CtgpDia dia;
    std::uint64_t length = 0;
    bool bad = false;
    if (!read_dia(request.update_path, 0, dia, length, error, &bad)) {
        request.bad_update = bad ? 1 : 0;
        return false;
    }
    request.dia_component = dia.component;
    request.dia_version = dia.version;
    request.dia_files = static_cast<std::uint32_t>(dia.sections.size());
    request.update_bytes = length;
    // Checked again here, before anything is written: every byte the install
    // reads is then CTGP-R's.
    if (!verify_signature(request.update_path, error, &bad)) {
        request.bad_update = bad ? 1 : 0;
        return false;
    }
    if (dia.component == "HASH") return install_hash(request, dia, length);
    riftwii::CtgpUpdateFailure failure = riftwii::CtgpUpdateFailure::Temporary;

    std::FILE* blob = std::fopen(request.blob_path.c_str(), "r+b");
    if (blob == nullptr) {
        error = request.blob_path + " could not be opened for writing";
        return false;
    }
    std::uint64_t blob_size = 0;
    if (!file_size(blob, blob_size)) {
        std::fclose(blob);
        error = request.blob_path + ": the SD card would not give its size";
        return false;
    }
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
        ok = install_into(request, pack, dia, length, failure);
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
            // Nothing of the update is left in blob.bin: a broken update can
            // now be set aside without stranding half of it.
            if (failure == riftwii::CtgpUpdateFailure::BadUpdate) request.bad_update = 1;
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
