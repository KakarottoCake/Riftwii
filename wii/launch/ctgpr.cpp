// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ctgpr.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>

#include "log.hpp"
#include "online.hpp"
#include "riftwii/checkedalloc.hpp"
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

}  // namespace

bool ctgpr_native_wanted() {
    // CTGP-R's own files are on the card: its installer put them there. The
    // launch needs nothing of its own, so their presence is what turns the
    // native launch on.
    return exists(kCtgprBlobPath) && exists(kCtgprJellyPath);
}

bool prepare_ctgpr_launch(const std::string& game_id, std::shared_ptr<const CtgpLaunchPlan>& out,
                          std::string& error) {
    CtgpInputs in;
    // Mario Kart Wii only: the native launch writes CTGP-R's Mario Kart Wii
    // hooks at fixed game addresses, so it must never run on another game, even
    // one whose disc ID happens to share the region letter.
    if (game_id.compare(0, 3, "RMC") != 0) {
        error = "CTGP-R only starts the Mario Kart Wii disc (its game ID starts \"RMC\")";
        return false;
    }
    switch (game_id.size() > 3 ? game_id[3] : '?') {
    case 'E': in.region = CtgpRegion::E; break;
    case 'J': in.region = CtgpRegion::J; break;
    case 'P': in.region = CtgpRegion::P; break;
    default:
        error = "CTGP-R supports the NTSC-U, NTSC-J and PAL discs of Mario Kart Wii";
        return false;
    }
    // The launch carries CTGP-R's own non-secret constants (the package-disk
    // key and chunk IV, the row table); it reads nothing from the card but
    // CTGP-R's own files. So the player needs no RiftWii files at all.
    if (!exists(kCtgprBlobPath) || !exists(kCtgprJellyPath)) {
        error = "CTGP-R is not installed on the SD card (sd:/ctgpr/blob.bin and jelly.bin are missing): install it "
                "with its own installer first";
        return false;
    }
    std::array<std::uint8_t, 16> key = ctgp_blob_key();
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
    Fat32File blob;
    if (!resolve_sd_file(kCtgprBlobPath, blob, error)) return false;
    const std::uint64_t blob_size = blob.entry.size & ~511ull;
    BlockReader reader = ctgp_efa_block_reader(
        [blob](std::uint64_t offset, std::uint32_t length, std::uint8_t* bytes) {
            return read_sd_file(blob, offset, bytes, length);
        },
        blob_size, key, in.tables.efa_iv);
    std::fill(key.begin(), key.end(), 0);
    Fat32Volume efa;
    if (!Fat32Volume::mount(reader, efa, error)) {
        error = "CTGP-R's package disk (blob.bin) does not open: " + error;
        return false;
    }
    // `max` caps the file at what the launch can use, checked before the
    // resize so a FAT entry claiming a huge size cannot make RiftWii try to
    // allocate it and eat the heap. BOOT.BIN and BOAT.BIN get their real
    // placement limits (ctgpimage.hpp); the rest get a generous general
    // ceiling (kCtgpFetchCeiling).
    auto fetch = [&](const char* path, std::vector<std::uint8_t>& bytes, std::uint64_t max) {
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
    };

    // The course table and the cup layouts the settings step copies into the
    // game are baked into the build (ctgpapply.cpp), the same way the row table
    // is, so the launch reads neither CTGP.CRS nor the .cup files from the
    // package disk any more.

    // The version first: only the tested one is started.
    if (!fetch("/PACKAGES.BIN", in.packages_bin, kCtgpFetchCeiling)) return false;
    std::vector<CtgpPackage> versions;
    if (!ctgp_parse_packages(in.packages_bin.data(), in.packages_bin.size(), versions, error)) return false;
    if (!ctgp_check_versions(versions, error)) return false;
    logf("CTGP-R: core 0x%08x, CTGPR 0x%08x: the tested version\n",
         static_cast<unsigned>(ctgp_package_version(versions, "CORE")),
         static_cast<unsigned>(ctgp_package_version(versions, "CTGPR")));

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

    auto plan = std::make_shared<CtgpLaunchPlan>();
    if (!ctgp_assemble(in, *plan, error)) return false;
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
    std::vector<std::vector<std::uint8_t>> lists(package_lists.size());
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
        std::vector<std::uint8_t> bytes;
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

}  // namespace riftwii::wii
