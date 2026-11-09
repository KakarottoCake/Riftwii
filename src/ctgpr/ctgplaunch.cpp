// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgplaunch.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <tuple>
#include <utility>

#include "riftwii/ctgpapply.hpp"
#include "riftwii/ctgpfat.hpp"
#include "riftwii/ctgpfields.hpp"
#include "riftwii/ctgppackages.hpp"
#include "riftwii/ctgpsign.hpp"

namespace riftwii {
namespace {

CtgpBytes word_bytes(std::uint32_t value, std::uint32_t size) {
    CtgpBytes out(size);
    for (std::uint32_t i = 0; i < size; ++i) out[i] = static_cast<std::uint8_t>(value >> (8 * (size - 1 - i)));
    return out;
}

constexpr std::uint32_t kBootBase = kCtgpBootBase;

}  // namespace

namespace {

// In the system area games leave to debuggers and code handlers (0x80001800-
// 0x80002FFF), which CTGP's code never references; a page the memory map
// keeps identity-mapped and writable. Not the space after BOOT.BIN's end: the
// package disk's driver uses it as buffers (a ghost save reads enough to
// overwrite a stub there, and the next disc read ran the garbage).
constexpr std::uint32_t kStartupStub = 0x80002E00;  // above the code handler and its veneers (0x80001800-)
constexpr std::uint32_t kStartupFlag = 0x80002FF0;

std::uint32_t boot_word(const CtgpLaunchPlan& p, std::uint32_t address) {
    const std::size_t at = address - kCtgpBootBase;
    return (std::uint32_t(p.boot[at]) << 24) | (std::uint32_t(p.boot[at + 1]) << 16) |
           (std::uint32_t(p.boot[at + 2]) << 8) | p.boot[at + 3];
}

// The channel mounts "sda:" and "efa:" before it starts the game, which runs
// each drive's startup function (SD card, and the package disk: it opens
// blob.bin and jelly.bin and sets up the decryption). The game's code only
// reads them. So the first read of a package file runs both startups once:
// the first hook (the disc read) goes to a small stub that does that and then
// carries on to the hook's own destination. Only a read at a virtual handle
// (a negative offset word) starts it.
void add_startup(CtgpLaunchPlan& p) {
    CtgpGameOp* hook0 = nullptr;
    for (CtgpGameOp& g : p.game_ops)
        if (g.group == CtgpGameOp::Hook && g.hook_index == 0) hook0 = &g;
    if (!hook0 || hook0->bytes.size() != 4 || p.boot.size() < kCtgpEfaDisc + 12 - kCtgpBootBase) return;
    const std::uint32_t word = (std::uint32_t(hook0->bytes[0]) << 24) | (std::uint32_t(hook0->bytes[1]) << 16) |
                               (std::uint32_t(hook0->bytes[2]) << 8) | hook0->bytes[3];
    if ((word >> 26) != 18 || (word & 2)) return;  // a relative b or bl only
    std::int32_t li = static_cast<std::int32_t>(word & 0x03FFFFFC);
    if (li & 0x02000000) li -= 0x04000000;
    const std::uint32_t dest = hook0->address + static_cast<std::uint32_t>(li);
    const std::uint32_t sda_startup = boot_word(p, kCtgpSdaDisc + 8);
    const std::uint32_t efa_startup = boot_word(p, kCtgpEfaDisc + 8);
    if (sda_startup < kCtgpBoatBase || efa_startup < kCtgpBoatBase) return;

    std::vector<std::uint32_t> w;
    w.push_back(0x2C050000);                                            // cmpwi r5,0
    w.push_back(0x40800000);                                            // bge go   (filled in below)
    w.push_back(0x3D600000u | (kStartupFlag >> 16));                    // lis r11,flag@ha
    w.push_back(0x898B0000u | (kStartupFlag & 0xFFFF));                 // lbz r12,flag@l(r11)
    w.push_back(0x2C0C0000);                                            // cmpwi r12,0
    w.push_back(0x40820000);                                            // bne go
    w.push_back(0x39800001);                                            // li r12,1
    w.push_back(0x998B0000u | (kStartupFlag & 0xFFFF));                 // stb r12,flag@l(r11)
    w.push_back(0x9421FF90);                                            // stwu r1,-0x70(r1)
    w.push_back(0x7C0802A6);                                            // mflr r0
    w.push_back(0x90010074);                                            // stw r0,0x74(r1)
    for (unsigned r = 3; r <= 10; ++r) w.push_back(0x90010000u | (r << 21) | (0x10 + (r - 3) * 4));  // stw rN,..(r1)
    for (std::uint32_t target : {sda_startup, efa_startup}) {
        w.push_back(0x3D800000u | (target >> 16));                      // lis r12,target@h
        w.push_back(0x618C0000u | (target & 0xFFFF));                   // ori r12,r12,target@l
        w.push_back(0x7D8903A6);                                        // mtctr r12
        w.push_back(0x4E800421);                                        // bctrl
    }
    for (unsigned r = 3; r <= 10; ++r) w.push_back(0x80010000u | (r << 21) | (0x10 + (r - 3) * 4));  // lwz rN,..(r1)
    w.push_back(0x80010074);                                            // lwz r0,0x74(r1)
    w.push_back(0x7C0803A6);                                            // mtlr r0
    w.push_back(0x38210070);                                            // addi r1,r1,0x70
    const std::size_t go = w.size();
    w[1] |= static_cast<std::uint32_t>((go - 1) * 4) & 0xFFFC;
    w[5] |= static_cast<std::uint32_t>((go - 5) * 4) & 0xFFFC;
    w.push_back(0x48000000u | ((dest - (kStartupStub + 4 * static_cast<std::uint32_t>(go))) & 0x03FFFFFC));  // b dest

    p.hook0_original = word;
    const std::uint32_t jump = 0x48000000u | (word & 1u) | ((kStartupStub - hook0->address) & 0x03FFFFFC);
    hook0->bytes = {static_cast<std::uint8_t>(jump >> 24), static_cast<std::uint8_t>(jump >> 16),
                    static_cast<std::uint8_t>(jump >> 8), static_cast<std::uint8_t>(jump)};

    CtgpGameOp flag;
    flag.address = kStartupFlag;
    flag.bytes = {0, 0, 0, 0};
    flag.group = CtgpGameOp::Startup;
    p.game_ops.push_back(std::move(flag));
    CtgpGameOp stub;
    stub.address = kStartupStub;
    for (std::uint32_t x : w) {
        stub.bytes.push_back(static_cast<std::uint8_t>(x >> 24));
        stub.bytes.push_back(static_cast<std::uint8_t>(x >> 16));
        stub.bytes.push_back(static_cast<std::uint8_t>(x >> 8));
        stub.bytes.push_back(static_cast<std::uint8_t>(x));
    }
    stub.group = CtgpGameOp::Startup;
    p.game_ops.push_back(std::move(stub));
}

}  // namespace

namespace {

std::string version_text(std::uint32_t v) {
    char text[16];
    std::snprintf(text, sizeof text, "0x%08X", static_cast<unsigned>(v));
    return text;
}

}  // namespace

bool ctgp_check_versions(const std::vector<CtgpPackage>& packages, std::string& error) {
    // The CORE package (CTGP-R's game-side code) governs the addresses the
    // native launch relies on -- the hook row table and the memory map. The
    // CTGPR track package governs the cup layouts, which are baked into this
    // build for one CTGP-R version (ctgpapply.cpp), so both must match the
    // tested versions. (A later step that derives the cups from the player's
    // own package disk will let the CTGPR version vary again.)
    const std::uint32_t core = ctgp_package_version(packages, "CORE");
    const std::uint32_t ctgpr = ctgp_package_version(packages, "CTGPR");
    if (core == kCtgpTestedCoreVersion && ctgpr == kCtgpTestedCtgprVersion) return true;
    error = "this CTGP-R is a different version (core " + version_text(core) + ", CTGPR " + version_text(ctgpr) +
            ") from the one this launch is made for (core " + version_text(kCtgpTestedCoreVersion) + ", CTGPR " +
            version_text(kCtgpTestedCtgprVersion) +
            "), so RiftWii does not start it. Start CTGP-R from the Homebrew Channel instead";
    return false;
}

bool ctgp_is_native_stand_in(const std::string& game_id, const std::string& app) {
    // Mario Kart Wii only: every region's disc ID starts "RMC" (RMCE, RMCP,
    // RMCJ, RMCK...). The region letter is the fourth character, so a full ID
    // is needed, not the bare prefix.
    if (game_id.size() < 4 || game_id.compare(0, 3, "RMC") != 0) return false;
    // Exactly CTGP-R's channel app, compared without regard to letter case,
    // since the card's FAT file system is case-insensitive and the pack's XML
    // may spell the path either way.
    static const char kCtgprApp[] = "sd:/apps/ctgpr/boot.dol";
    if (app.size() != sizeof kCtgprApp - 1) return false;
    for (std::size_t i = 0; i < app.size(); ++i) {
        char a = app[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (a != kCtgprApp[i]) return false;
    }
    return true;
}

bool ctgp_assemble(const CtgpInputs& in, CtgpLaunchPlan& plan, std::string& error) {
    std::vector<CtgpPackage> packages;
    if (!ctgp_parse_packages(in.packages_bin.data(), in.packages_bin.size(), packages, error)) return false;
    if (!ctgp_check_versions(packages, error)) return false;
    CtgpRowTable rows;
    if (!ctgp_parse_row_table(in.rows_bin.data(), in.rows_bin.size(), rows, error)) return false;
    CtgpImage image;
    if (!ctgp_build_image(in.boot_bin, in.boat_bin, in.beat_bin, rows, in.region, image, error)) return false;

    CtgpLaunchPlan p;
    p.boot = std::move(image.boot);
    p.boat = std::move(image.boat);
    p.image_end = kCtgpBoatBase + static_cast<std::uint32_t>(p.boat.size());
    const std::uint32_t boot_end = kBootBase + static_cast<std::uint32_t>(p.boot.size());

    // Writes into the loader's block land in BOOT; anything else is the game's.
    auto store = [&](std::uint32_t address, std::uint32_t size, std::uint32_t value) {
        if (address >= kBootBase && address + size <= boot_end) {
            const auto bytes = word_bytes(value, size);
            std::memcpy(&p.boot[address - kBootBase], bytes.data(), size);
        } else {
            CtgpGameOp op;
            op.address = address;
            op.bytes = word_bytes(value, size);
            op.group = CtgpGameOp::LowMemory;
            p.game_ops.push_back(std::move(op));
        }
    };

    // The partition objects: sda: from the card's boot sector, efa: from the
    // package disk's.
    CtgpFatVolume sda, efa;
    if (!ctgp_parse_boot_sector(in.sda_boot_sector.data(), in.sda_boot_sector.size(), in.sda_volume_lba, sda, error))
        return false;
    if (!ctgp_parse_boot_sector(in.efa_boot_sector.data(), in.efa_boot_sector.size(), 0, efa, error)) return false;
    struct Part { const CtgpFatVolume* vol; std::uint32_t base, disc, features; };
    for (const Part& part : {Part{&sda, kCtgpSdaBase, kCtgpSdaDisc, 0x103}, Part{&efa, kCtgpEfaBase, kCtgpEfaDisc, 3}}) {
        CtgpPartitionPlacement at;
        at.base = part.base;
        at.disc_interface = part.disc;
        at.features = part.features;
        std::vector<std::uint8_t> bytes;
        if (!ctgp_build_partition(*part.vol, at, bytes, error)) return false;
        if (part.base < kBootBase || part.base + bytes.size() > boot_end) {
            error = "BOOT.BIN is too small for the partition objects";
            return false;
        }
        std::memcpy(&p.boot[part.base - kBootBase], bytes.data(), bytes.size());
    }

    // CTGP keeps its live state in the exception-vector slots the game's SDK
    // leaves empty: the room rules at 0x80000A00-0A0E (0A0B = 2 restricts the
    // game to bikes), flags at 0B9C, 0DC8, 0E01-0EF0. Under the channel they
    // start clear; under RiftWii they hold libogc's exception stubs, which libogc
    // copies into every slot. Cleared here, before the settings step writes its
    // bytes there: the slots that never fire on this CPU, and the tails of the
    // trace and performance-monitor slots past the game's 0xA4-byte stub. The
    // system call slot's tail is cleared at the jump (wii/boot.cpp), after its
    // 28-byte handler goes in.
    for (const auto& [address, length] : std::initializer_list<std::pair<std::uint32_t, std::uint32_t>>{
             {0x80000A00, 0x200}, {0x80000DA8, 0x58}, {0x80000E00, 0x100}, {0x80000FA8, 0x58},
             {0x80001000, 0x300}, {0x80001500, 0x200}}) {
        CtgpGameOp op;
        op.address = address;
        op.bytes.assign(length, 0);
        op.group = CtgpGameOp::LowMemory;
        p.game_ops.push_back(std::move(op));
    }

    // Loader words: config.ini options (online-only ones forced off,
    // ctgp_field_writes), package ids, then the settings step.
    for (const CtgpWordWrite& w : ctgp_field_writes(in.config_ini)) store(w.address, 4, w.value);
    for (const CtgpWordWrite& w : ctgp_package_id_writes(packages)) store(w.address, 4, w.value);
    // The BATs the core's start-up (BOAT 0x8179810c, after it installs its
    // exception vectors) writes into DBAT7 and IBAT0: the channel stores two
    // pairs of its randomised BAT table here (chan 0x808753e4, 0x808754f8).
    // DBAT7 is the start-up mapping of low memory the core swaps out (see
    // ctgpmmu.hpp; 0 without the memory map: nothing to swap). IBAT0 stays
    // the MEM1 instruction BAT. Zeros (BOOT.BIN's own) turn IBAT0 off, and
    // the game stops at its next instruction fetch, on CTGP's "Press A" screen.
    store(0x816FFACC, 4, 0);           // DBAT7U (set with the memory map below)
    store(0x816FFAD0, 4, 0);           // DBAT7L
    store(0x816FFAD4, 4, 0x80001FFF);  // IBAT0U: 0x80000000, 256 MB, Vs Vp
    store(0x816FFAD8, 4, 0x00000002);  // IBAT0L: physical 0, cached, read/write
    const CtgpSettings settings = ctgp_settings_from_ini(in.config_ini, packages, in.region);
    for (const CtgpOp& op : ctgp_apply_settings(settings)) {
        switch (op.kind) {
        case CtgpOpKind::Store:
            store(op.address, op.size, op.value);
            break;
        case CtgpOpKind::RowStore: {
            CtgpGameOp g;
            g.address = rows.row(op.row, in.region) + op.offset;
            g.bytes = word_bytes(op.value, 4);
            g.group = CtgpGameOp::Settings;
            p.game_ops.push_back(std::move(g));
            break;
        }
        case CtgpOpKind::RowMove: {
            CtgpGameOp g;
            g.kind = CtgpGameOp::Move;
            g.group = CtgpGameOp::Settings;
            const std::uint32_t row = rows.row(op.row, in.region);
            g.address = row + op.offset;
            g.from = row + op.from;
            g.length = op.size;
            p.game_ops.push_back(std::move(g));
            break;
        }
        }
    }
    // Whether the game's Common.szs is fit for saving ghosts (chan FUN_80954cec,
    // copied to 0x816ff508; 0 makes CTGP refuse with "You cannot use this
    // Common.szs to save ghosts!"), and whether My Stuff replaces it
    // (0x816ff510). The channel then hashes My Stuff's Common files and may still
    // allow ghosts; here any of them turns ghost saving off.
    {
        const CtgpSettings opts = ctgp_settings_from_ini(in.config_ini, {}, in.region);
        const bool replaced = in.my_stuff_common && opts.my_stuff != 0 && opts.my_stuff != 3;
        store(0x816FF508, 4, replaced ? 0 : 1);
        store(0x816FF510, 4, replaced ? 1 : 0);
        // ini "ghost_saving_mode" (chan FUN_80956b38 copies r13+0x43c here).
        // The ghost-slot hook (BOAT 0x81775958) reads it.
        store(0x816FF514, 4, static_cast<std::uint32_t>(opts.ghost_saving_mode));
    }
    // A signing key that can never sign a ghost validly, in place of the
    // card's own, and no console tag (ctgpsign.hpp): the CTGP-R developers'
    // condition for this launch. Without any key, saving a ghost crashes into
    // CTGP's reboot-to-channel.
    for (const CtgpWordWrite& w : ctgp_unsigned_key_writes()) store(w.address, 4, w.value);
    // Leaving for the channel starts `channel_stub` instead (a RiftWii change:
    // the channel's own stub hangs). CTGP leaves its reason at 0x93000000.
    if (!in.channel_stub.empty()) {
        static const char kStub[] = "efa:/packages/CHAN/stub.dol";
        // The stub's path sits at a fixed offset well inside a real BOOT.BIN,
        // but a short or malformed CORE/BOOT.BIN could be smaller than that:
        // check the span is present before touching it, so the fixed-offset
        // read and write below can never run off the end of the image.
        const std::uint32_t stub_offset = kCtgpChannelStub - kBootBase;
        if (p.boot.size() < static_cast<std::size_t>(stub_offset) + kCtgpChannelStubLength) {
            error = "BOOT.BIN is too small to hold the channel stub's path";
            return false;
        }
        if (in.channel_stub.size() >= kCtgpChannelStubLength) {
            error = "the path for leaving to the channel is too long: " + in.channel_stub;
            return false;
        }
        std::uint8_t* at = &p.boot[stub_offset];
        if (std::memcmp(at, kStub, sizeof kStub) != 0) {
            error = "BOOT.BIN does not hold the channel stub's path where expected";
            return false;
        }
        std::memset(at, 0, kCtgpChannelStubLength);
        std::memcpy(at, in.channel_stub.data(), in.channel_stub.size());
    }
    // The CTGPR tracks' digests (chan FUN_80883918 reads 255 of them; the
    // settings step copies them into the loader's block in two parts).
    if (!in.track_hashes.empty()) {
        if (in.track_hashes.size() < kCtgpTrackHashCount * 20) {
            error = "deyazhashes.hsh holds fewer than 255 digests";
            return false;
        }
        std::vector<std::uint8_t> table(kCtgpTrackHashesALength + kCtgpTrackHashesBLength, 0);
        std::memcpy(table.data(), in.track_hashes.data(), kCtgpTrackHashCount * 20);
        for (const auto& [address, from, length] :
             {std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>{kCtgpTrackHashesA, 0, kCtgpTrackHashesALength},
              {kCtgpTrackHashesB, kCtgpTrackHashesALength, kCtgpTrackHashesBLength}}) {
            if (address < kBootBase || address + length > boot_end) {
                error = "BOOT.BIN is too small for the track digests";
                return false;
            }
            std::memcpy(&p.boot[address - kBootBase], table.data() + from, length);
        }
    }
    // The hooks last.
    int hook_number = 0;
    for (const CtgpHook& h : image.hooks) {
        CtgpGameOp g;
        g.address = h.site;
        g.bytes = word_bytes(h.word, 4);
        g.group = CtgpGameOp::Hook;
        g.hook_index = hook_number++;
        p.game_ops.push_back(std::move(g));
    }
    add_startup(p);
    // What the channel's entry routine (0x80874c14) does to the game just before
    // it starts it, apart from the MMU: a blr over one function, two blocks of
    // game code zeroed, two nops.
    if (rows.low.size() == 54) {
        const unsigned r = static_cast<unsigned>(in.region);
        int env_number = 0;
        auto env_write = [&](std::uint32_t address, CtgpBytes bytes) {
            CtgpGameOp g;
            g.address = address;
            g.bytes = std::move(bytes);
            g.group = CtgpGameOp::Environment;
            g.env_index = env_number++;
            p.game_ops.push_back(std::move(g));
        };
        env_write(rows.low[r], word_bytes(0x4E800020, 4));
        env_write(rows.low[6 + r], CtgpBytes(808, 0));
        env_write(rows.low[9 + r], CtgpBytes(1376, 0));
        env_write(rows.low[12 + r] + 0x6C, word_bytes(0x60000000, 4));
        env_write(rows.low[12 + r] + 0x74, word_bytes(0x60000000, 4));
    }
    // The channel's memory map.
    if (rows.low.size() == 54) {
        CtgpMmu mmu;
        if (!ctgp_build_mmu(rows.low, in.region, mmu, error)) return false;
        CtgpGameOp g;
        g.address = kCtgpHtabBase;
        g.bytes = std::move(mmu.htab);
        g.group = CtgpGameOp::Mmu;
        p.game_ops.push_back(std::move(g));
        mmu.htab.clear();
        store(0x816FFACC, 4, mmu.core_dbat7u);
        store(0x816FFAD0, 4, mmu.core_dbat7l);
        p.notes.push_back("CTGP-R: page table with " + std::to_string(mmu.entries) + " entries");
        p.mmu = true;
        p.mmu_regs = mmu;
    }
    p.notes.push_back("CTGP-R: " + std::to_string(image.hooks.size()) + " hooks, BOOT " +
                      std::to_string(p.boot.size()) + " B, BOAT " + std::to_string(p.boat.size()) + " B");
    plan = std::move(p);
    return true;
}

bool ctgp_attach_files(CtgpLaunchPlan& plan, const std::vector<CtgpBytes>& package_fsts,
                       const std::vector<const CtgpHashList*>& hash_lists, const CtgpSourceResolver& resolver,
                       std::string& error) {
    if (hash_lists.size() != package_fsts.size()) {
        error = "every package needs its hash list entry (or null)";
        return false;
    }
    const std::uint32_t table_end = kCtgpRecordBlock + kCtgpRecordCount * kCtgpRecordSize;
    const std::uint32_t last_partition = ctgp_partition_address(2) + 0x4C + 4;
    if (plan.boot.size() < table_end - kCtgpBootBase || plan.boot.size() < last_partition - kCtgpBootBase) {
        error = "the BOOT image is too small for the record table";
        return false;
    }
    CtgpRecordTable table;
    std::vector<CtgpMergedFile> all;
    std::size_t listed = 0;
    for (std::size_t i = 0; i < package_fsts.size(); ++i) {
        std::vector<CtgpPackageFile> files;
        if (!ctgp_parse_package_fst(package_fsts[i].data(), package_fsts[i].size(), files, error)) {
            error = "package " + std::to_string(i) + ": " + error;
            return false;
        }
        listed += files.size();
        const std::size_t before = all.size();
        if (!ctgp_add_package(table, files, resolver, hash_lists[i], all, error)) {
            error = "package " + std::to_string(i) + ": " + error;
            return false;
        }
        // The merged files are the list's files that were found, in list order.
        std::size_t next = before;
        for (const CtgpPackageFile& f : files) {
            if (next < all.size() && all[next].game_path == f.game_path) {
                ++next;
                continue;
            }
            plan.missing.push_back(std::to_string(i) + ((f.flags & 1) ? " * " : "   ") + f.source + " -> " + f.game_path);
        }
    }
    const auto put = [&plan](std::uint32_t address, std::uint32_t x) {
        const std::size_t at = address - kCtgpBootBase;
        plan.boot[at] = static_cast<std::uint8_t>(x >> 24);
        plan.boot[at + 1] = static_cast<std::uint8_t>(x >> 16);
        plan.boot[at + 2] = static_cast<std::uint8_t>(x >> 8);
        plan.boot[at + 3] = static_cast<std::uint8_t>(x);
    };
    std::copy(table.image().begin(), table.image().end(), plan.boot.begin() + (kCtgpRecordBlock - kCtgpBootBase));
    for (int device = 0; device < 3; ++device) {
        put(ctgp_partition_address(device) + ctgp_part::open_file_count, table.open_count(device));
        put(ctgp_partition_address(device) + ctgp_part::first_open_file, table.first_open(device));
    }
    plan.files = std::move(all);
    plan.notes.push_back("CTGP-R: " + std::to_string(package_fsts.size()) + " package lists, " + std::to_string(listed) +
                         " files listed, " + std::to_string(plan.files.size()) + " found (" +
                         std::to_string(table.used()) + " records)");
    return true;
}

bool ctgp_check_layout(const CtgpLaunchPlan& plan, const CtgpMemoryState& state, std::string& error) {
    const std::uint32_t start = kCtgpImageStart;
    const std::uint32_t end = plan.image_end;
    if (plan.boot.empty() || plan.boat.empty() || end <= start) {
        error = "the CTGP-R plan is empty";
        return false;
    }
    if (kCtgpBootBase + plan.boot.size() > kCtgpBoatBase) {
        error = "BOOT.BIN runs into BOAT.BIN";
        return false;
    }
    if (end > kCtgpMem1End) {
        error = "the CTGP-R image runs past the end of MEM1";
        return false;
    }
    if (state.loader_end > start) {
        error = "this loader's memory reaches into the CTGP-R image";
        return false;
    }
    if (state.apploader_end > start) {
        error = "the apploader image reaches into the CTGP-R image";
        return false;
    }
    if (state.fst_address != 0 && state.fst_address < end) {
        error = "the game's FST sits inside the CTGP-R image (it needs the FST to be moved)";
        return false;
    }
    if (state.bi2_address != 0 && state.bi2_address < end) {
        error = "the disc info block (BI2) sits inside the CTGP-R image";
        return false;
    }
    for (const MemoryRegion& r : state.loaded) {
        const std::uint64_t r_end = std::uint64_t(r.address) + r.length;
        if (r.length != 0 && r.address < end && r_end > start) {
            error = "a section of the game's executable lies inside the CTGP-R image";
            return false;
        }
    }
    return true;
}

bool ctgp_place(const CtgpLaunchPlan& plan, MemoryAccess& memory, std::string& error) {
    if (!memory.write(kCtgpBootBase, plan.boot.data(), plan.boot.size()) ||
        !memory.write(kCtgpBoatBase, plan.boat.data(), plan.boat.size())) {
        error = "cannot write the CTGP-R image";
        return false;
    }
    for (const CtgpGameOp& op : plan.game_ops) {
        if (op.kind == CtgpGameOp::Write) {
            if (!memory.write(op.address, op.bytes.data(), op.bytes.size())) {
                error = "cannot write a CTGP-R patch";
                return false;
            }
        } else {
            std::vector<std::uint8_t> tmp(op.length);
            if (!memory.read(op.from, tmp.data(), tmp.size()) || !memory.write(op.address, tmp.data(), tmp.size())) {
                error = "cannot move game code for a CTGP-R patch";
                return false;
            }
        }
    }
    return true;
}

}  // namespace riftwii
