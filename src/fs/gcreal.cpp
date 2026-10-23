// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2004 Michael Wiedenbauer (shagkur)
// SPDX-FileCopyrightText: 2004 Dave Murphy (WinterMute)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gcreal.hpp"

#include "riftwii/gccard.hpp"
#include "riftwii/gcbackup.hpp"
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

namespace riftwii {

bool gc_real_dma_range(std::uintptr_t address, std::size_t bytes) {
    constexpr std::uintptr_t mem1_begin = 0x80000000;
    constexpr std::uintptr_t mem1_end = 0x81800000;
    return address >= mem1_begin && address < mem1_end && (address & 31u) == 0 &&
           bytes != 0 && (bytes & 31u) == 0 && bytes <= mem1_end - address;
}

bool gc_real_have_room(const std::size_t* sizes, std::size_t count, GcRealAllocate allocate) {
    if (allocate == nullptr) allocate = std::malloc;
    if (count > kGcRealMaxBlocks) return false;
    void* held[kGcRealMaxBlocks];
    std::size_t taken = 0;
    while (taken < count) {
        void* block = allocate(sizes[taken]);
        if (block == nullptr) break;
        held[taken++] = block;
    }
    for (std::size_t i = 0; i < taken; ++i) std::free(held[i]);
    return taken == count;
}

bool gc_real_plan(const std::vector<std::uint8_t>& source, const std::vector<std::uint8_t>& target,
                  const std::string& game_id, bool every_save, GcRealPlan& out, std::string& error,
                  GcRealAllocate allocate) {
    out = GcRealPlan{};
    GcCardInfo from;
    GcCardInfo to;
    if (source.size() > kGcDolphinMaxRawBytes || target.size() > kGcDolphinMaxRawBytes ||
        !gc_card_read(source, from, error) || !gc_card_read(target, to, error)) return false;
    if (from.shift_jis != to.shift_jis) {
        error = "The cards use different text encodings.";
        return false;
    }
    if (!every_save && game_id.size() != 6) {
        error = "The game needs a six-character ID.";
        return false;
    }
    // The private image is a copy of the target; export copies of single
    // saves come on top of it.
    const std::size_t plan_room[] = {target.size(), kGcRealSlack};
    if (!gc_real_have_room(plan_room, 2, allocate)) {
        out.out_of_memory = true;
        error = "There is not enough free memory to plan the copy.";
        return false;
    }
    // All mutations stay in a private image, including partial imports when
    // space runs out. The caller can show exact counts before writing anything.
    out.image = target;
    for (const GcSave& save : from.saves) {
        if (!every_save && (save.game_code != game_id.substr(0, 4) || save.maker != game_id.substr(4, 2)))
            continue;
        ++out.selected;
        if (out.count.full) continue;
        // The save's own copy (gc_card_export) is as long as its chain of
        // blocks, up to most of a card: more than the slack above, so its
        // room is asked for first.
        if (save.intact) {
            const std::size_t save_room[] = {
                kGcCardEntryBytes + std::size_t(save.block_count) * kGcCardBlockBytes, kGcRealSlack};
            if (!gc_real_have_room(save_room, 2, allocate)) {
                out = GcRealPlan{};
                out.out_of_memory = true;
                error = "There is not enough free memory to copy the save " + save.game_code + save.maker + ".";
                return false;
            }
        }
        std::vector<std::uint8_t> gci;
        if (!save.intact || !gc_card_export(source, save.slot, gci, error)) {
            ++out.count.unusable;
            continue;
        }
        gc_import_one(out.image, gci, out.count);
    }
    return true;
}

bool gc_real_commit(const std::string& path, const std::vector<std::uint8_t>& image,
                    const std::function<bool()>& source_unchanged, std::string& error,
                    GcRealAllocate allocate) {
    const std::size_t commit_room[] = {image.size(), kGcRealSlack};
    if (!gc_real_have_room(commit_room, 2, allocate)) {
        error = "There is not enough free memory to write the copy.";
        return false;
    }
    const GcRecovery recovered = gc_recover_card(path, error);
    if (recovered == GcRecovery::Failed || recovered == GcRecovery::BothKept) return false;
    const std::string staged = path + ".real-new";
    if (!gc_overwrite_file(staged, image, error)) {
        std::remove(staged.c_str());
        return false;
    }
    if (!source_unchanged()) {
        std::remove(staged.c_str());
        error = "The real memory card changed or was removed. Nothing was copied.";
        return false;
    }
    struct stat status;
    const bool existed = stat(path.c_str(), &status) == 0;
    const std::string old = path + ".old";
    if (existed && std::rename(path.c_str(), old.c_str()) != 0) {
        std::remove(staged.c_str());
        error = "The old memory card image could not be kept.";
        return false;
    }
    if (std::rename(staged.c_str(), path.c_str()) != 0) {
        if (existed) std::rename(old.c_str(), path.c_str());
        std::remove(staged.c_str());
        error = "The new memory card image could not be put in place.";
        return false;
    }
    if (existed) std::remove(old.c_str());
    return true;
}

}  // namespace riftwii
