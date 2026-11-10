// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The native CTGP-R launch's plan (docs/CTGPR.md): CTGP-R's package disk
// opened and checked, its settings and cup layout read, its code linked and
// its files given their records, all into the DOL's CtgpLaunchPlan
// (riftwii/plugin_ctgpr.hpp). The DOL places the plan in MEM1 afterwards.
#include "ctgprprepare.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <vector>

#include "riftwii/checkedalloc.hpp"
#include "riftwii/ctgpapply.hpp"
#include "riftwii/ctgpefa.hpp"
#include "riftwii/ctgphash.hpp"
#include "riftwii/ctgppackages.hpp"
#include "riftwii/ctgpstage2.hpp"
#include "riftwii/ctgptables.hpp"
#include "riftwii/sha1.hpp"
#include "rwpruntime.hpp"

namespace riftwii::plugin {
namespace {

// CTGP-R's own files, as its installer puts them on the card.
constexpr const char* kBlobPath = "sd:/ctgpr/blob.bin";
constexpr const char* kJellyPath = "sd:/ctgpr/jelly.bin";
constexpr const char* kConfigPath = "sd:/ctgpr/config.ini";
// The player's own cup layout, which CTGP-R's channel saves (layout 2).
constexpr const char* kCupLayoutPath = "sd:/ctgpr/cuplayout.cup";

// The largest file read whole through the mount (config.ini, cuplayout.cup).
constexpr long kMaxWholeFile = 16 << 20;

// A generous ceiling for package-disk files whose only hard limit is that they
// must fit in memory (PACKAGES.BIN, BEAT.BIN, the hash file, the package file
// lists). BOOT.BIN and BOAT.BIN instead get their own placement caps, which
// are far smaller (ctgpimage.hpp).
constexpr std::uint64_t kFetchCeiling = 16ull << 20;

// One line in boot.log, as the DOL's logf writes it (the format ends in its
// own line end).
void logf(const char* format, ...) __attribute__((format(printf, 1, 2)));
void logf(const char* format, ...) {
    char line[384];
    va_list args;
    va_start(args, format);
    host().format(line, sizeof(line), format, args);
    va_end(args);
    host().log_text(line);
}

bool exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

// A file of the card, read whole through the mount.
bool read_whole(const char* path, std::vector<std::uint8_t>& out, std::string& error) {
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        error = std::string(path) + " is missing";
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

// CTGP-R's package disk (blob.bin), opened for reading: the card file's
// sectors, decrypted a chunk at a time. `reader` stays valid for as long as
// `efa` is used.
bool open_package_disk(const CtgprSdAccess& sd, const CtgpTables& tables, BlockReader& reader, Fat32Volume& efa,
                       std::string& error) {
    Fat32File blob;
    if (!sd.resolve(kBlobPath, blob, error)) return false;
    std::array<std::uint8_t, 16> key = ctgp_blob_key();
    const std::uint64_t blob_size = blob.entry.size & ~511ull;
    auto read = sd.read;
    reader = ctgp_efa_block_reader(
        [blob, read](std::uint64_t offset, std::uint32_t length, std::uint8_t* bytes) {
            return read(blob, offset, bytes, length);
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
bool fetch_from_package_disk(Fat32Volume& efa, const char* path, CtgpBytes& bytes, std::uint64_t max,
                             std::string& error) {
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

// The cup layout config.ini picks (cup_course_layout_using), read from the
// player's install the way CTGP-R's channel reads it:
//   0: the channel's default, from the channel program in MAIN.DOL
//      (ctgpstage2.hpp);
//   1: alphabetical, alphabeticEN.cup (alphabeticJP.cup when the channel's
//      language is 6), copied as it is;
//   2: the player's own sd:/ctgpr/cuplayout.cup; without a usable one the
//      channel's slot stays empty (no cups), as the channel leaves it.
// A layout that is damaged or out of range stops the launch; nothing of it
// reaches the game.
bool read_cup_layout(Fat32Volume& efa, const CtgpSettings& settings, CtgpLayout& out, std::string& error,
                     CtgprFailure& why) {
    if (!settings.ctgpr_enabled) return true;  // no custom tracks: no cups are written
    const int choice = ctgp_layout_choice(settings);
    if (choice == 1) {
        const char* path =
            settings.language == 6 ? "/PACKAGES/CTGPR/alphabeticJP.cup" : "/PACKAGES/CTGPR/alphabeticEN.cup";
        CtgpBytes cup;
        if (!fetch_from_package_disk(efa, path, cup, kCtgpCupFileBytes, error)) return false;
        if (!ctgp_layout_from_cup(cup.data(), cup.size(), out, error)) {
            error = std::string(path) + ": " + error;
            return false;
        }
        logf("CTGP-R: cup layout 1 (alphabetical) from %s: %u cups\n", path, static_cast<unsigned>(out.groups));
        return true;
    }
    if (choice == 2) {
        out = CtgpLayout();
        std::vector<std::uint8_t> cup;
        std::string ignored;
        struct stat st;
        if (stat(kCupLayoutPath, &st) != 0 || !S_ISREG(st.st_mode) ||
            st.st_size != static_cast<off_t>(kCtgpCupFileBytes) || !read_whole(kCupLayoutPath, cup, ignored)) {
            logf("CTGP-R: cup layout 2 (your own): no usable %s, so no cups (as the channel)\n", kCupLayoutPath);
            return true;
        }
        std::string reason;
        if (!ctgp_layout_from_cup(cup.data(), cup.size(), out, reason)) {
            out = CtgpLayout();
            logf("CTGP-R: cup layout 2 (your own): %s is not used (%s), so no cups\n", kCupLayoutPath,
                 reason.c_str());
            return true;
        }
        logf("CTGP-R: cup layout 2 (your own) from %s: %u cups\n", kCupLayoutPath, static_cast<unsigned>(out.groups));
        return true;
    }
    // Layout 0: decode the channel program and take its first layout.
    CtgpBytes dol;
    if (!fetch_from_package_disk(efa, "/PACKAGES/CHAN/MAIN.DOL", dol, kFetchCeiling, error)) return false;
    CtgpStage2Table table;
    CtgpStage2Result result = ctgp_stage2_table(dol.data(), dol.size(), table, error);
    CtgpChannelCups cups;
    if (result == CtgpStage2Result::Ok) result = ctgp_stage2_cups(dol.data(), dol.size(), table, cups, error);
    if (result != CtgpStage2Result::Ok) {
        error = "the channel program in MAIN.DOL: " + error;
        if (result == CtgpStage2Result::Unsupported) why = CtgprFailure::Version;
        return false;
    }
    if (!ctgp_layout_from_entry(cups.entries.data(), out, error)) {
        error = "the channel program's default cup layout: " + error;
        return false;
    }
    logf("CTGP-R: cup layout 0 (default) from MAIN.DOL's channel program: table built in %u steps, %u KB "
         "unpacked, layouts at 0x%08x, %u cups\n",
         static_cast<unsigned>(table.steps), static_cast<unsigned>(cups.inflated / 1024),
         static_cast<unsigned>(cups.address), static_cast<unsigned>(out.groups));
    return true;
}

}  // namespace

bool ctgpr_prepare(CtgprLaunchRequest& request) {
    const CtgprSdAccess& sd = request.sd;
    const std::string& game_id = request.game_id;
    std::string& error = request.error;
    CtgprFailure& why = request.failure;
    CtgpLaunchPlan& plan = *request.plan;

    CtgpInputs in;
    // Mario Kart Wii only: the native launch writes CTGP-R's Mario Kart Wii
    // hooks at fixed game addresses, so it must never run on another game, even
    // one whose disc ID happens to share the region letter.
    if (game_id.compare(0, 3, "RMC") != 0) {
        error = "CTGP-R only starts the Mario Kart Wii disc (its game ID starts \"RMC\")";
        why = CtgprFailure::NotMarioKart;
        return false;
    }
    switch (game_id.size() > 3 ? game_id[3] : '?') {
    case 'E': in.region = CtgpRegion::E; break;
    case 'J': in.region = CtgpRegion::J; break;
    case 'P': in.region = CtgpRegion::P; break;
    default:
        error = "CTGP-R supports the NTSC-U, NTSC-J and PAL discs of Mario Kart Wii";
        why = CtgprFailure::Region;
        return false;
    }
    // The launch carries CTGP-R's own non-secret constants (the package-disk
    // key and chunk IV, the row table); it reads nothing from the card but
    // CTGP-R's own files. So the player needs no RiftWii files at all.
    if (!exists(kBlobPath) || !exists(kJellyPath)) {
        error = "CTGP-R is not installed on the SD card (sd:/ctgpr/blob.bin and jelly.bin are missing): install it "
                "with its own installer first";
        why = CtgprFailure::NotInstalled;
        return false;
    }
    in.tables.efa_iv = ctgp_blob_chunk_iv();
    in.tables.efb_key = ctgp_jelly_key_words();
    std::size_t row_size = 0;
    const std::uint8_t* row_data = ctgp_embedded_row_table(row_size);
    in.rows_bin.assign(row_data, row_data + row_size);
    in.channel_stub = request.channel_stub;
    {
        std::vector<std::uint8_t> ini;
        std::string ignored;
        if (exists(kConfigPath) && read_whole(kConfigPath, ini, ignored)) in.config_ini.assign(ini.begin(), ini.end());
    }

    // efa: the package disk, read raw from the card's sectors.
    BlockReader reader;
    Fat32Volume efa;
    if (!open_package_disk(sd, in.tables, reader, efa, error)) return false;
    // BOOT.BIN and BOAT.BIN get their real placement limits (ctgpimage.hpp);
    // the rest get a generous general ceiling (kFetchCeiling).
    auto fetch = [&](const char* path, CtgpBytes& bytes, std::uint64_t max) {
        return fetch_from_package_disk(efa, path, bytes, max, error);
    };

    // The version first: only the tested one is started.
    if (!fetch("/PACKAGES.BIN", in.packages_bin, kCtgpPackagesMaxBytes)) return false;
    std::vector<CtgpPackage> versions;
    if (!ctgp_parse_packages(in.packages_bin.data(), in.packages_bin.size(), versions, error)) return false;
    if (!ctgp_check_versions(versions, error)) {
        why = CtgprFailure::Version;
        return false;
    }
    if (!ctgp_has_hash_reference("CORE", ctgp_package_version(versions, "CORE"))) {
        error = "the installed core has no authenticated hash-list body reference in this build";
        why = CtgprFailure::Version;
        return false;
    }
    {
        const std::uint32_t ctgpr = ctgp_package_version(versions, "CTGPR");
        const bool tested = ctgpr == kCtgpTestedCtgprVersion1192 || ctgpr == kCtgpTestedCtgprVersion1196;
        logf("CTGP-R: core 0x%08x (the tested one), CTGPR 0x%08x (%s)\n",
             static_cast<unsigned>(ctgp_package_version(versions, "CORE")), static_cast<unsigned>(ctgpr),
             tested ? "the tested one" : "a newer track package: its signed list is checked as it loads");
    }

    // The cup layout first: its buffers are gone again before BOOT.BIN and
    // BOAT.BIN are read, so the arena's peak stays theirs.
    if (!read_cup_layout(efa, ctgp_settings_from_ini(in.config_ini, versions, in.region), in.cup_layout, error, why))
        return false;
    if (!fetch("/PACKAGES/CORE/BOOT.BIN", in.boot_bin, kCtgpBootMax) ||
        !fetch("/PACKAGES/CORE/BOAT.BIN", in.boat_bin, kCtgpBoatMax) ||
        !fetch("/PACKAGES/CORE/BEAT.BIN", in.beat_bin, kFetchCeiling))
        return false;
    in.efa_boot_sector.resize(512);
    if (!reader(0, 1, in.efa_boot_sector.data())) {
        error = "cannot read the package disk's boot sector";
        return false;
    }
    std::uint64_t volume_lba = 0;
    if (!sd.boot_sector(volume_lba, in.sda_boot_sector, error)) return false;
    in.sda_volume_lba = static_cast<std::uint32_t>(volume_lba);

    // My Stuff replacing Common.szs turns ghost saving off (the channel checks the same names).
    for (const char* name : kCtgpMyStuffCommons) {
        Fat32File f;
        std::string ignored_error;
        if (sd.resolve(std::string("sd:/ctgpr/My Stuff/") + name, f, ignored_error) && !f.entry.is_directory) {
            in.my_stuff_common = true;
            logf("CTGP-R: My Stuff holds %s: ghosts will not save\n", name);
            break;
        }
    }
    // The CTGPR tracks' digests, which the channel always loads.
    if (!fetch("/PACKAGES/CTGPR/deyazhashes.hsh", in.track_hashes, kFetchCeiling)) return false;

    if (!ctgp_assemble(in, plan, error)) return false;
    // The code files as read are in the plan now (linked); the rest of the
    // launch needs none of them, so their room goes to the package lists.
    const Sha1Digest track_digest = sha1(in.track_hashes.data(), in.track_hashes.size());
    // The core code's digests, for the CORE.HSH check below, before the copies go.
    struct CoreCodeDigest {
        const char* what;
        Sha1Digest digest;
    };
    const CoreCodeDigest core_code[] = {
        {"BOOT.BIN", sha1(in.boot_bin.data(), in.boot_bin.size())},
        {"BOAT.BIN", sha1(in.boat_bin.data(), in.boat_bin.size())},
        {"BEAT.BIN", sha1(in.beat_bin.data(), in.beat_bin.size())},
    };
    CtgpBytes().swap(in.boot_bin);
    CtgpBytes().swap(in.boat_bin);
    CtgpBytes().swap(in.beat_bin);
    CtgpBytes().swap(in.packages_bin);
    CtgpBytes().swap(in.track_hashes);
    logf("CTGP-R: offline only: online and LAN play off, ghost signer given an unusable key\n");

    // The signed lists of file digests that vouch for CTGPR's and CORE's
    // files: on the second package disk, jelly.bin, which opens with a key
    // made from its own FAT times (the in-game start-up builds it the same
    // way). Each list is checked as the channel checks it; without a list
    // that verifies, the packages' signed files cannot be used and the launch
    // stops, as the channel's does.
    Fat32File jelly;
    if (!sd.resolve(kJellyPath, jelly, error)) return false;
    const auto efb_key = ctgp_efb_key(in.tables.efb_key, jelly.entry.write_time, jelly.entry.write_date,
                                      jelly.entry.create_time, jelly.entry.create_date);
    auto read = sd.read;
    BlockReader jreader = ctgp_efa_block_reader(
        [jelly, read](std::uint64_t offset, std::uint32_t length, std::uint8_t* bytes) {
            return read(jelly, offset, bytes, length);
        },
        jelly.entry.size & ~511ull, efb_key, in.tables.efa_iv);
    Fat32Volume efb;
    if (!Fat32Volume::mount(jreader, efb, error)) {
        error = "CTGP-R's hash disk (jelly.bin) does not open: " + error;
        return false;
    }

    // The packages' files: the lists config.ini's options pick, in the
    // channel's order (ctgp_package_lists).
    const auto package_settings = ctgp_settings_from_ini(in.config_ini, versions, in.region);
    if (package_settings.no_music) logf("CTGP-R: using default music: this release has no signed music payload list\n");
    if (package_settings.font != 0)
        logf("CTGP-R: using the default font: this release has no signed font payload list\n");
    const auto package_lists = ctgp_package_lists(package_settings);
    std::vector<CtgpBytes> lists(package_lists.size());
    std::size_t list_bytes = 0;
    for (std::size_t i = 0; i < package_lists.size(); ++i) {
        const std::size_t remaining = 8 * kCtgpPackageMaxCopiedBytes - list_bytes;
        if (!fetch(package_lists[i].path.c_str(), lists[i], remaining)) return false;
        list_bytes += lists[i].size();
        logf("CTGP-R: package list %s\n", package_lists[i].path.c_str());
    }
    const CtgprSdAccess sd_copy = sd;
    CtgpSourceResolver resolver = [&efa, sd_copy](const std::string& device, const std::string& path,
                                                  CtgpSourceFile& found) {
        Fat32File f;
        std::string ignored_error;
        if (device == "efa") {
            if (!efa.lookup(path, f, ignored_error) || f.entry.is_directory) return false;
        } else if (device == "sda") {
            if (!sd_copy.resolve("sd:" + path, f, ignored_error) || f.entry.is_directory) return false;
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
        if (f.entry.size > kFetchCeiling || !try_resize(bytes, f.entry.size) ||
            !efb.read(f, 0, bytes.data(), bytes.size())) {
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
    // The channel verifies this file under its signed debug/smallfile.sml
    // name, not its on-disk name (FUN_80954b04).
    if (!ctgpr_hashes.matches_digest("/debug/smallfile.sml", track_digest)) {
        error = "deyazhashes.hsh does not match CTGPR.HSH's signed debug/smallfile.sml digest";
        return false;
    }
    // The DSA signature of each list is checked against CTGP-R's own key as it
    // loads (ctgp_load_hash_list); the per-file digests those lists carry are
    // written into the game's open-file records, where CTGP's code compares
    // each served file as it reads it.
    logf("CTGP-R: hash lists' signatures checked against CTGP-R's key (CTGPR %u files, CORE %u files)\n",
         static_cast<unsigned>(ctgpr_hashes.file_count()), static_cast<unsigned>(core_hashes.file_count()));

    // These roles were matched against the locally checked CORE release:
    // BOOT = bigfile1, BOAT = bigfile7, BEAT = bigfile6. The pinned full-body
    // digest binds that mapping to this release. Membership alone would let
    // another signed core file stand in for the required loader role.
    for (const CoreCodeDigest& c : core_code) {
        if (!ctgp_core_digest_matches(core_hashes, c.what, c.digest)) {
            error = std::string("CTGP-R's core code does not match its signed hash list: ") + c.what +
                    " does not match its named CORE.HSH digest";
            return false;
        }
    }
    logf("CTGP-R: core code matches its named CORE.HSH digests\n");

    std::vector<const CtgpHashList*> hash_lists;
    std::vector<bool> allow_unsigned;
    for (const CtgpPackageList& l : package_lists) {
        hash_lists.push_back(l.hash_list == "CTGPR" ? &ctgpr_hashes : l.hash_list == "CORE" ? &core_hashes : nullptr);
        allow_unsigned.push_back(l.allow_unsigned);
    }
    if (!ctgp_attach_files(plan, lists, hash_lists, resolver, error, allow_unsigned)) return false;
    for (const std::string& note : plan.notes) logf("%s\n", note.c_str());
    // Files a package lists that are not on the card or the package disk:
    // left out, as the channel leaves them (a required one fails above).
    constexpr std::size_t kMissingShown = 20;
    for (std::size_t i = 0; i < plan.missing.size() && i < kMissingShown; ++i)
        logf("CTGP-R: not found, left out: %s\n", plan.missing[i].c_str());
    if (plan.missing.size() > kMissingShown)
        logf("CTGP-R: and %u more files not found\n", static_cast<unsigned>(plan.missing.size() - kMissingShown));
    return true;
}

}  // namespace riftwii::plugin
