// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "cios.hpp"

#include <gccore.h>
#include <ogc/es.h>
#include <ogc/isfs.h>
#include <ogc/machine/processor.h>

#include <cstdio>

#include "install.hpp"
#include "riftwii/nandcheck.hpp"

namespace installer {

bool NandReadAccess::Open() {
    if (attempted_) return opened_;
    attempted_ = true;
    if (read32(0x0D800064) != 0xFFFFFFFF) {
        Log("The cIOS information needs hardware access, which this start does not have.\n");
        opened_ = false;
        return false;
    }
    constexpr u32 kFrom = 0xD3400000, kTo = 0xD4000000;
    protection_ = read16(0x0D8B420A);
    protection_saved_ = true;
    write16(0x0D8B420A, 2);
    std::size_t offset = 0;
    if (!riftwii::find_nand_check(reinterpret_cast<const u8*>(kFrom), kTo - kFrom, offset)) {
        RestoreProtection();
        Log("IOS%d's NAND permission check could not be found.\n", IOS_GetVersion());
        opened_ = false;
        return false;
    }
    const auto store = riftwii::nand_check_store(kFrom + static_cast<u32>(offset));
    word_ = store.word;
    shift_ = store.shift;
    before_ = read32(word_);
    after_ = riftwii::nand_check_opened(before_, store.shift);
    // An aligned word store is necessary on the uncached MEM2 mirror;
    // a halfword store hung Wii and vWii consoles in RiftWii before.
    write32(word_, after_);
    const bool opened = read32(word_) == after_;
    // A failed store may be partial: put the original back at once, as
    // RiftWii does. Restore still checks the word holds the original.
    if (!opened) write32(word_, before_);
    RestoreProtection();
    Log("IOS%d's NAND permission check %s for the cIOS information.\n", IOS_GetVersion(),
        opened ? "was opened" : "could not be opened");
    opened_ = opened && protection_restored_;
    return opened_;
}

bool NandReadAccess::Restore() {
    if (finished_) return word_restored_ && protection_restored_;
    finished_ = true;
    if (word_ != 0) {
        write16(0x0D8B420A, 2);
        const u32 current = read32(word_);
        // Only the patched halfword is ours; IOS may have changed the other
        // halfword of the same word, which must neither block nor be undone.
        if (riftwii::nand_check_half(current, shift_) == riftwii::nand_check_half(after_, shift_)) {
            const u32 mask = static_cast<u32>(0xFFFF) << shift_;
            write32(word_, (current & ~mask) | (before_ & mask));
        }
        const u32 now = read32(word_);
        word_restored_ = riftwii::nand_check_half(now, shift_) == riftwii::nand_check_half(before_, shift_);
        if (word_restored_) {
            Log("IOS%d's NAND permission check was restored after reading the cIOS information.\n",
                IOS_GetVersion());
        } else {
            Log("IOS%d's NAND permission check could not be restored after reading the cIOS information "
                "(word 0x%08x read 0x%08x, original 0x%08x, opened 0x%08x).\n",
                IOS_GetVersion(), word_, now, before_, after_);
        }
    }
    RestoreProtection();
    return word_restored_ && protection_restored_;
}

NandReadAccess::~NandReadAccess() { Restore(); }

void NandReadAccess::RestoreProtection() {
    if (!protection_saved_) return;
    write16(0x0D8B420A, protection_);
    const bool restored = read16(0x0D8B420A) == protection_;
    // Latch a failure even if a later cleanup attempt succeeds.
    protection_restored_ = protection_restored_ && restored;
    if (!restored) Log("The NAND memory protection register could not be restored.\n");
}

riftwii::D2xInfo ReadCios(int slot, NandReadAccess& access, bool& read) {
    read = false;
    const u64 title = 0x0000000100000000ull | static_cast<u32>(slot);
    // IOS TMD views are small. A fixed, bounded buffer keeps the installer
    // independent of the app's allocator and large platform modules.
    alignas(32) static u8 view_data[0x1000];
    u32 size = 0;
    if (ES_GetTMDViewSize(title, &size) < 0 || size < sizeof(tmd_view) + sizeof(tmd_view_content) ||
        size > sizeof(view_data)) {
        Log("IOS%d was skipped because its content list could not be read.\n", slot);
        return {};
    }
    auto* view = reinterpret_cast<tmd_view*>(view_data);
    if (ES_GetTMDView(title, view, size) < 0 || view->title_id != title || view->num_contents == 0 ||
        view->num_contents > (size - sizeof(tmd_view)) / sizeof(tmd_view_content)) {
        Log("IOS%d was skipped because its content list is missing or invalid.\n", slot);
        return {};
    }
    alignas(32) char path[64];
    std::snprintf(path, sizeof(path), "/title/00000001/%08x/content/%08x.app", static_cast<unsigned>(slot),
                  static_cast<unsigned>(view->contents[0].cid));
    s32 fd = ISFS_Open(path, ISFS_OPEN_READ);
    if (fd == -102 && access.Open()) fd = ISFS_Open(path, ISFS_OPEN_READ);
    if (fd < 0) {
        Log("IOS%d was skipped because its d2x information could not be opened (%d).\n", slot, fd);
        return {};
    }
    alignas(32) u8 head[0x40];
    const s32 got = ISFS_Read(fd, head, sizeof(head));
    ISFS_Close(fd);
    if (got < 0x30 || got > static_cast<s32>(sizeof(head))) {
        Log("IOS%d was skipped because its d2x information could not be read (%d).\n", slot, got);
        return {};
    }
    read = true;
    const auto info = riftwii::parse_d2x_info(head, static_cast<std::size_t>(got));
    if (!info.d2x) Log("IOS%d was skipped because it is not a d2x cIOS.\n", slot);
    else Log("IOS%d holds %s with base %d.\n", slot, riftwii::d2x_name(info).c_str(), info.base);
    return info;
}

}  // namespace installer
