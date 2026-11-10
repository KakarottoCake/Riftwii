// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The native CTGP-R launch's DOL side (docs/CTGPR.md, "Where the code
// lives"): what RiftWii itself does with a plan the ctgpr plugin built. It
// tells the CTGP-R pack apart (the menu's lockout), puts the packages' files
// into the game's file system table, checks the plan against the loaded game
// and writes it into MEM1. Everything that reads, decrypts, checks and builds
// CTGP-R is in the plugin (Makefile.plugin's ctgpr_SRC), not in the DOL.
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/ctgpfiles.hpp"
#include "riftwii/ctgplaunch.hpp"

namespace riftwii {

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

bool ctgp_merge_files(Fst& fst, const std::vector<CtgpMergedFile>& files, std::string& error) {
    std::vector<FstNewFile> fresh;
    for (const CtgpMergedFile& f : files) {
        // The handle goes into the table's offset field as it is; the table
        // keeps offsets shifted right by two.
        const std::uint64_t offset = static_cast<std::uint64_t>(f.handle) << 2;
        std::uint32_t index = fst.find(f.game_path, false);
        if (index == Fst::npos) index = fst.find(f.game_path, true);
        if (index != Fst::npos) {
            if (fst.entries()[index].is_directory) {
                error = f.game_path + " is a directory in the game";
                return false;
            }
            if (!fst.set_file_extent(index, offset, f.size, error)) return false;
        } else {
            // A later package may name the same new file again: the last one wins.
            bool again = false;
            for (FstNewFile& n : fresh) {
                if (n.path == f.game_path) {
                    n.offset = offset;
                    n.size = f.size;
                    again = true;
                }
            }
            if (!again) fresh.push_back({f.game_path, offset, f.size});
        }
    }
    if (fresh.empty()) return true;
    std::vector<std::uint32_t> indices;
    return fst.create_files(fresh, indices, error);
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
