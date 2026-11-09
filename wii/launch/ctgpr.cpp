// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ctgpr.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "i18n.hpp"
#include "log.hpp"
#include "memlimits.hpp"
#include "online.hpp"
#include "riftwii/checkedalloc.hpp"
#include "riftwii/ctgparena.hpp"
#include "riftwii/ctgpapply.hpp"
#include "riftwii/ctgpefa.hpp"
#include "riftwii/ctgphash.hpp"
#include "riftwii/ctgppackages.hpp"
#include "riftwii/ctgptables.hpp"
#include "sdfile.hpp"

namespace riftwii::wii {
namespace {

// The largest file read whole here (BOOT.BIN is about 1.2 MB).
constexpr long kMaxWholeFile = 16 << 20;

// A generous ceiling for package-disk files whose only hard limit is that they
// must fit in memory (PACKAGES.BIN, BEAT.BIN, the hash file, the package file
// lists). BOOT.BIN and BOAT.BIN instead get their own placement caps, which
// are far smaller (ctgpimage.hpp).
constexpr std::uint64_t kCtgpFetchCeiling = 16ull << 20;

bool exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

// A file of the card, read whole. `what` names it for the player.
bool read_whole(const char* path, const char* what, std::vector<std::uint8_t>& out, std::string& error) {
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        error = std::string(what) + " is missing: put " + path + " on the SD card (docs/CTGPR.md says how)";
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size < 0 || size > kMaxWholeFile || !try_resize(out, static_cast<std::size_t>(size))) {
        std::fclose(f);
        error = std::string(path) + " is too large to read";
        return false;
    }
    const bool ok = size == 0 || std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    if (!ok) error = std::string("cannot read ") + path;
    return ok;
}

// The launch's big buffers (BOOT.BIN and BOAT.BIN as read and as linked, the
// page table, the package and hash lists) live in one MEM2 block, so they do
// not fill the MEM1 heap (CtgpBytes draws from it, riftwii/ctgparena.hpp).
// Peak use is about 3.5 MB (both copies of BOOT.BIN and BOAT.BIN, 1.6 MB each,
// before the as-read copies are freed); the block is 6 MB, and a buffer that
// finds no room falls back to the heap. It is released when the plan is
// destroyed, or at once when the launch fails.
constexpr std::size_t kLaunchArenaBytes = 6u << 20;

CtgpArena g_launch_arena;
void* g_launch_arena_block = nullptr;

void reserve_launch_arena() {
    if (g_launch_arena_block != nullptr) return;
    std::size_t bytes = kLaunchArenaBytes;
    void* block = mem::TryMem2Alloc(bytes);
    if (block == nullptr) {
        logf("CTGP-R: no MEM2 block for the launch's buffers; they use the heap\n");
        return;
    }
    g_launch_arena.attach(block, bytes);
    g_launch_arena_block = block;
    g_ctgp_arena = &g_launch_arena;
    logf("CTGP-R: launch buffers in MEM2: %u KiB block at %p\n", static_cast<unsigned>(bytes / 1024), block);
}

// Gives the block back once nothing is left in it.
void release_launch_arena() {
    if (g_launch_arena_block == nullptr || g_launch_arena.live_blocks() != 0) return;
    logf("CTGP-R: launch buffers: most in use at once %u KiB of MEM2\n",
         static_cast<unsigned>(g_launch_arena.peak() / 1024));
    g_ctgp_arena = nullptr;
    std::free(g_launch_arena.detach());
    g_launch_arena_block = nullptr;
}

// CTGP-R's package disk (blob.bin), opened for reading: the card file's
// sectors, decrypted a chunk at a time. `reader` stays valid for as long as
// `efa` is used.
bool open_package_disk(const CtgpTables& tables, BlockReader& reader, Fat32Volume& efa, std::string& error) {
    Fat32File blob;
    if (!resolve_sd_file(kCtgprBlobPath, blob, error)) return false;
    std::array<std::uint8_t, 16> key = ctgp_blob_key();
    const std::uint64_t blob_size = blob.entry.size & ~511ull;
    reader = ctgp_efa_block_reader(
        [blob](std::uint64_t offset, std::uint32_t length, std::uint8_t* bytes) {
            return read_sd_file(blob, offset, bytes, length);
        },
        blob_size, key, tables.efa_iv);
    std::fill(key.begin(), key.end(), 0);
    if (!Fat32Volume::mount(reader, efa, error)) {
        error = "CTGP-R's package disk (blob.bin) does not open: " + error;
        return false;
    }
    return true;
}

// A file of the package disk, read whole. `max` caps its size before the
// resize, so a FAT entry claiming a huge size cannot make RiftWii try to
// allocate it and eat the heap.
bool fetch_from_package_disk(Fat32Volume& efa, const char* path, CtgpBytes& bytes,
                             std::uint64_t max, std::string& error) {
    Fat32File f;
    if (!efa.lookup(path, f, error)) {
        error = std::string(path) + " on CTGP-R's package disk: " + error;
        return false;
    }
    if (f.entry.size > max) {
        error = std::string(path) + " on CTGP-R's package disk is larger than it can be";
        return false;
    }
    if (!try_resize(bytes, f.entry.size)) {
        error = std::string(path) + " on CTGP-R's package disk does not fit in memory";
        return false;
    }
    if (!efa.read(f, 0, bytes.data(), bytes.size())) {
        error = std::string("cannot read ") + path + " from CTGP-R's package disk";
        return false;
    }
    return true;
}

// Why a launch stopped, for the words the player sees. The technical detail
// (names, numbers) goes to boot.log instead.
enum class Failure { Other, NotMarioKart, Region, NotInstalled, Version };

std::string failure_text(Failure why) {
    switch (why) {
    case Failure::NotMarioKart: return tr("CTGP-R only starts with the Mario Kart Wii game.");
    case Failure::Region:
        return tr("CTGP-R works with the US, Japanese and European versions of Mario Kart Wii only.");
    case Failure::NotInstalled:
        return tr("CTGP-R is not installed on the SD card. Install it with its own installer first.");
    case Failure::Version:
        return tr("This version of CTGP-R can't be started from RiftWii yet. Start CTGP-R from the Homebrew Channel instead.");
    case Failure::Other: break;
    }
    return tr("CTGP-R could not be started. Its files on the SD card may be damaged or incomplete: install CTGP-R again. The reason is in sd:/riftwii/boot.log.");
}

// What the installed CTGP-R is to the native launch, worked out once and kept:
// the menu asks on every redraw, and the answer reads the card.
bool g_status_known = false;
CtgprNativeStatus g_status = CtgprNativeStatus::NotInstalled;

CtgprNativeStatus work_out_status() {
    if (!exists(kCtgprBlobPath) || !exists(kCtgprJellyPath)) return CtgprNativeStatus::NotInstalled;
    CtgpTables tables;
    tables.efa_iv = ctgp_blob_chunk_iv();
    BlockReader reader;
    Fat32Volume efa;
    std::string error;
    CtgpBytes packages_bin;
    std::vector<CtgpPackage> versions;
    if (!open_package_disk(tables, reader, efa, error) ||
        !fetch_from_package_disk(efa, "/PACKAGES.BIN", packages_bin, kCtgpFetchCeiling, error) ||
        !ctgp_parse_packages(packages_bin.data(), packages_bin.size(), versions, error) ||
        !ctgp_check_versions(versions, error)) {
        logf("CTGP-R: the installed copy cannot be started natively: %s\n", error.c_str());
        return CtgprNativeStatus::Unsupported;
    }
    return CtgprNativeStatus::Supported;
}

}  // namespace

CtgprNativeStatus ctgpr_native_status() {
    if (!g_status_known) {
        g_status = work_out_status();
        g_status_known = true;
    }
    return g_status;
}

void ctgpr_native_status_forget() { g_status_known = false; }

bool ctgpr_native_wanted() {
    // CTGP-R's own files are on the card: its installer put them there. The
    // launch needs nothing of its own, so their presence is what turns the
    // native launch on.
    return exists(kCtgprBlobPath) && exists(kCtgprJellyPath);
}

namespace {

// The launch itself. `error` is technical (for boot.log); `why` says which
// words the player gets, and stays Other unless a step names it.
bool prepare_impl(const std::string& game_id, std::shared_ptr<const CtgpLaunchPlan>& out, std::string& error,
                  Failure& why) {
    CtgpInputs in;
    // Mario Kart Wii only: the native launch writes CTGP-R's Mario Kart Wii
    // hooks at fixed game addresses, so it must never run on another game, even
    // one whose disc ID happens to share the region letter.
    if (game_id.compare(0, 3, "RMC") != 0) {
        error = "CTGP-R only starts the Mario Kart Wii disc (its game ID starts \"RMC\")";
        why = Failure::NotMarioKart;
        return false;
    }
    switch (game_id.size() > 3 ? game_id[3] : '?') {
    case 'E': in.region = CtgpRegion::E; break;
    case 'J': in.region = CtgpRegion::J; break;
    case 'P': in.region = CtgpRegion::P; break;
    default:
        error = "CTGP-R supports the NTSC-U, NTSC-J and PAL discs of Mario Kart Wii";
        why = Failure::Region;
        return false;
    }
    // The launch carries CTGP-R's own non-secret constants (the package-disk
    // key and chunk IV, the row table); it reads nothing from the card but
    // CTGP-R's own files. So the player needs no RiftWii files at all.
    if (!exists(kCtgprBlobPath) || !exists(kCtgprJellyPath)) {
        error = "CTGP-R is not installed on the SD card (sd:/ctgpr/blob.bin and jelly.bin are missing): install it "
                "with its own installer first";
        why = Failure::NotInstalled;
        return false;
    }
    in.tables.efa_iv = ctgp_blob_chunk_iv();
    in.tables.efb_key = ctgp_jelly_key_words();
    std::size_t row_size = 0;
    const std::uint8_t* row_data = ctgp_embedded_row_table(row_size);
    in.rows_bin.assign(row_data, row_data + row_size);
    std::vector<std::uint8_t> ini;
    std::string ignored;
    FILE* config = std::fopen(kCtgprConfigPath, "rb");
    if (config) {
        std::fclose(config);
        if (read_whole(kCtgprConfigPath, "config.ini", ini, ignored)) in.config_ini.assign(ini.begin(), ini.end());
    }

    // efa: the package disk, read raw from the card's sectors.
    BlockReader reader;
    Fat32Volume efa;
    if (!open_package_disk(in.tables, reader, efa, error)) return false;
    // BOOT.BIN and BOAT.BIN get their real placement limits (ctgpimage.hpp);
    // the rest get a generous general ceiling (kCtgpFetchCeiling).
    auto fetch = [&](const char* path, CtgpBytes& bytes, std::uint64_t max) {
        return fetch_from_package_disk(efa, path, bytes, max, error);
    };

    // The course table and the cup layouts the settings step copies into the
    // game are baked into the build (ctgpapply.cpp), the same way the row table
    // is, so the launch reads neither CTGP.CRS nor the .cup files from the
    // package disk any more.

    // The version first: only the tested one is started.
    if (!fetch("/PACKAGES.BIN", in.packages_bin, kCtgpFetchCeiling)) return false;
    std::vector<CtgpPackage> versions;
    if (!ctgp_parse_packages(in.packages_bin.data(), in.packages_bin.size(), versions, error)) return false;
    if (!ctgp_check_versions(versions, error)) {
        why = Failure::Version;
        return false;
    }
    logf("CTGP-R: core 0x%08x, CTGPR 0x%08x: the tested version\n",
         static_cast<unsigned>(ctgp_package_version(versions, "CORE")),
         static_cast<unsigned>(ctgp_package_version(versions, "CTGPR")));

    // The code files and everything built from them go to MEM2 (CtgpBytes).
    reserve_launch_arena();
    if (!fetch("/PACKAGES/CORE/BOOT.BIN", in.boot_bin, kCtgpBootMax) ||
        !fetch("/PACKAGES/CORE/BOAT.BIN", in.boat_bin, kCtgpBoatMax) ||
        !fetch("/PACKAGES/CORE/BEAT.BIN", in.beat_bin, kCtgpFetchCeiling))
        return false;
    in.efa_boot_sector.resize(512);
    if (!reader(0, 1, in.efa_boot_sector.data())) {
        error = "cannot read the package disk's boot sector";
        return false;
    }
    std::uint64_t volume_lba = 0;
    if (!read_sd_boot_sector(volume_lba, in.sda_boot_sector, error)) return false;
    in.sda_volume_lba = static_cast<std::uint32_t>(volume_lba);

    // My Stuff replacing Common.szs turns ghost saving off (the channel checks the same names).
    for (const char* name : kCtgpMyStuffCommons) {
        Fat32File f;
        std::string ignored_error;
        if (resolve_sd_file(std::string("sd:/ctgpr/My Stuff/") + name, f, ignored_error) && !f.entry.is_directory) {
            in.my_stuff_common = true;
            logf("CTGP-R: My Stuff holds %s: ghosts will not save\n", name);
            break;
        }
    }
    // The CTGPR tracks' digests, which the channel always loads.
    if (!fetch("/PACKAGES/CTGPR/deyazhashes.hsh", in.track_hashes, kCtgpFetchCeiling)) return false;

    // Leaving for the channel (its menu buttons, a crash) restarts RiftWii.
    {
        const std::string self = SelfDolPath();
        if (self.rfind("sd:/", 0) == 0 && self.size() + 1 < kCtgpChannelStubLength)
            in.channel_stub = "sda:/" + self.substr(4);
        else if (self.empty())
            logf("CTGP-R: leaving for the CTGP-R Channel will not come back to RiftWii (it was not started from the "
                 "SD card, so there is no path on the card to return to)\n");
        else
            logf("CTGP-R: leaving for the CTGP-R Channel will not come back to RiftWii (its path '%s' does not fit)\n",
                 self.c_str());
    }

    std::shared_ptr<CtgpLaunchPlan> plan(new CtgpLaunchPlan, [](CtgpLaunchPlan* done) {
        delete done;
        release_launch_arena();
    });
    if (!ctgp_assemble(in, *plan, error)) return false;
    // The code files as read are in the plan now (linked); the rest of the
    // launch needs none of them, so their room goes to the package lists.
    CtgpBytes().swap(in.boot_bin);
    CtgpBytes().swap(in.boat_bin);
    CtgpBytes().swap(in.beat_bin);
    CtgpBytes().swap(in.packages_bin);
    CtgpBytes().swap(in.track_hashes);
    logf("CTGP-R: offline only: online and LAN play off, ghosts saved unsigned\n");

    // The signed lists of file digests that vouch for CTGPR's and CORE's
    // files: on the second package disk, jelly.bin, which opens with a key
    // made from its own FAT times (the in-game start-up builds it the same
    // way). Each list is checked as the channel checks it; without a list
    // that verifies, the packages' signed files cannot be used and the launch
    // stops, as the channel's does.
    Fat32File jelly;
    if (!resolve_sd_file(kCtgprJellyPath, jelly, error)) return false;
    const auto efb_key = ctgp_efb_key(in.tables.efb_key, jelly.entry.write_time, jelly.entry.write_date,
                                      jelly.entry.create_time, jelly.entry.create_date);
    BlockReader jreader = ctgp_efa_block_reader(
        [jelly](std::uint64_t offset, std::uint32_t length, std::uint8_t* bytes) {
            return read_sd_file(jelly, offset, bytes, length);
        },
        jelly.entry.size & ~511ull, efb_key, in.tables.efa_iv);
    Fat32Volume efb;
    if (!Fat32Volume::mount(jreader, efb, error)) {
        error = "CTGP-R's hash disk (jelly.bin) does not open: " + error;
        return false;
    }

    // The packages' files: the lists config.ini's options pick, in the
    // channel's order (ctgp_package_lists).
    const auto package_lists = ctgp_package_lists(ctgp_settings_from_ini(in.config_ini, versions, in.region));
    std::vector<CtgpBytes> lists(package_lists.size());
    for (std::size_t i = 0; i < package_lists.size(); ++i) {
        if (!fetch(package_lists[i].path.c_str(), lists[i], kCtgpFetchCeiling)) return false;
        logf("CTGP-R: package list %s\n", package_lists[i].path.c_str());
    }
    CtgpSourceResolver resolver = [&efa](const std::string& device, const std::string& path, CtgpSourceFile& found) {
        Fat32File f;
        std::string ignored_error;
        if (device == "efa") {
            if (!efa.lookup(path, f, ignored_error) || f.entry.is_directory) return false;
        } else if (device == "sda") {
            if (!resolve_sd_file("sd:" + path, f, ignored_error) || f.entry.is_directory) return false;
        } else {
            return false;
        }
        found.size = f.entry.size;
        found.first_cluster = f.entry.first_cluster;
        return true;
    };

    CtgpHashList ctgpr_hashes, core_hashes;
    auto load_hashes = [&](const char* name, CtgpHashList& list) {
        Fat32File f;
        const std::string path = std::string("/PACKAGES/HASH/") + name + ".HSH";
        if (!efb.lookup(path, f, error)) {
            error = std::string("the hash list for ") + name + ": " + error;
            return false;
        }
        CtgpBytes bytes;
        if (!try_resize(bytes, f.entry.size) || !efb.read(f, 0, bytes.data(), bytes.size())) {
            error = std::string("cannot read the hash list for ") + name;
            return false;
        }
        if (!ctgp_load_hash_list(bytes, name, ctgp_package_version(versions, name), list, error)) {
            error = std::string("the hash list for ") + name + " is not accepted: " + error;
            return false;
        }
        return true;
    };
    if (!load_hashes("CTGPR", ctgpr_hashes) || !load_hashes("CORE", core_hashes)) return false;
    // The DSA signature of each list is checked against CTGP-R's own key as it
    // loads (ctgp_load_hash_list); the per-file digests those lists carry are
    // written into the game's open-file records, where CTGP's code compares
    // each served file as it reads it.
    logf("CTGP-R: hash lists' signatures checked against CTGP-R's key (CTGPR %u files, CORE %u files)\n",
         static_cast<unsigned>(ctgpr_hashes.file_count()), static_cast<unsigned>(core_hashes.file_count()));
    std::vector<const CtgpHashList*> hash_lists;
    for (const CtgpPackageList& l : package_lists)
        hash_lists.push_back(l.hash_list == "CTGPR" ? &ctgpr_hashes : l.hash_list == "CORE" ? &core_hashes : nullptr);
    if (!ctgp_attach_files(*plan, lists, hash_lists, resolver, error)) return false;
    for (const std::string& note : plan->notes) logf("%s\n", note.c_str());
    // Files a package lists that are not on the card or the package disk:
    // left out, as the channel leaves them (a required one fails above).
    constexpr std::size_t kMissingShown = 20;
    for (std::size_t i = 0; i < plan->missing.size() && i < kMissingShown; ++i)
        logf("CTGP-R: not found, left out: %s\n", plan->missing[i].c_str());
    if (plan->missing.size() > kMissingShown)
        logf("CTGP-R: and %u more files not found\n", static_cast<unsigned>(plan->missing.size() - kMissingShown));
    out = std::move(plan);
    return true;
}

}  // namespace

bool prepare_ctgpr_launch(const std::string& game_id, std::shared_ptr<const CtgpLaunchPlan>& out,
                          std::string& error) {
    Failure why = Failure::Other;
    std::string technical;
    if (prepare_impl(game_id, out, technical, why)) return true;
    // Everything the failed launch held is freed by now (its plan's own
    // release ran while the inputs were still alive).
    release_launch_arena();
    logf("CTGP-R: the native launch stops: %s\n", technical.c_str());
    error = failure_text(why);
    return false;
}

}  // namespace riftwii::wii
