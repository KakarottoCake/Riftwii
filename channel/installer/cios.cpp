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
namespace {

// IOS58 refuses other titles' files. Follow RiftWii's own cIOS search:
// open its NAND permission check only after -102, with AHBPROT access,
// then restore the word before returning (and before any IOS reload).
class NandReadAccess {
public:
    bool Open() {
        if (read32(0x0D800064) != 0xFFFFFFFF) {
            Log("The cIOS information needs hardware access, which this start does not have.\n");
            return false;
        }
        constexpr u32 kFrom = 0xD3400000, kTo = 0xD4000000;
        const u16 protection = read16(0x0D8B420A);
        write16(0x0D8B420A, 2);
        std::size_t offset = 0;
        if (!riftwii::find_nand_check(reinterpret_cast<const u8*>(kFrom), kTo - kFrom, offset)) {
            write16(0x0D8B420A, protection);
            Log("IOS%d's NAND permission check could not be found.\n", IOS_GetVersion());
            return false;
        }
        const auto store = riftwii::nand_check_store(kFrom + static_cast<u32>(offset));
        volatile u32* word = reinterpret_cast<volatile u32*>(store.word);
        const u32 before = *word;
        const u32 after = riftwii::nand_check_opened(before, store.shift);
        // An aligned word store is necessary on the uncached MEM2 mirror;
        // a halfword store hung Wii and vWii consoles in RiftWii before.
        *word = after;
        const bool opened = *word == after;
        if (opened) {
            word_ = word;
            before_ = before;
        } else {
            *word = before;
        }
        write16(0x0D8B420A, protection);
        Log("IOS%d's NAND permission check %s for the cIOS information.\n", IOS_GetVersion(),
            opened ? "was opened" : "could not be opened");
        return opened;
    }

    ~NandReadAccess() {
        if (word_ == nullptr) return;
        const u16 protection = read16(0x0D8B420A);
        write16(0x0D8B420A, 2);
        *word_ = before_;
        const bool restored = *word_ == before_;
        write16(0x0D8B420A, protection);
        Log("IOS%d's NAND permission check %s after reading the cIOS information.\n", IOS_GetVersion(),
            restored ? "was restored" : "could not be restored");
    }

private:
    volatile u32* word_ = nullptr;
    u32 before_ = 0;
};

}  // namespace

riftwii::D2xInfo ReadCios(int slot, bool& read) {
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
    NandReadAccess access;
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
