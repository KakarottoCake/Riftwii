// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "nandpermissions.hpp"

#include <gccore.h>
#include <ogc/cache.h>
#include <ogc/machine/processor.h>

#include "log.hpp"
#include "memlimits.hpp"
#include "riftwii/nandcheck.hpp"

namespace riftwii::wii {
namespace {

NandPermissionNote* note() {
    return reinterpret_cast<NandPermissionNote*>(mem::kNandPermissionNote);
}

bool g_open = false;
const char* g_recovery = nullptr;
u32 g_recovery_word = 0;

void FlushNote() { DCFlushRange(note(), 32); }

// Only the patched halfword is ours, as in the channel installer (#379):
// IOS may change the other halfword of the same word, which must neither
// block the close nor be undone. The word is still read and written
// whole: smaller uncached stores have hung consoles. `found` is what the
// word held when the close looked, for the log.
bool Restore(u32& found) {
    const NandPermissionNote saved = *note();
    const u32 protection = read16(0x0D8B420A);
    write16(0x0D8B420A, 2);
    found = read32(saved.word);
    u32 closed = 0;
    const bool ours = nand_check_closed(found, saved.before, saved.shift, closed);
    if (ours) write32(saved.word, closed);
    const bool restored = nand_check_half(read32(saved.word), saved.shift) == nand_check_half(saved.before, saved.shift);
    write16(0x0D8B420A, protection);
    if (restored || !ours) {
        note()->magic = 0;
        FlushNote();
    }
    return restored;
}

}  // namespace

void RecoverNandPermissions() {
    g_open = false;
    // No IOS requests, allocation or logging before the SD card is mounted.
    // Random game memory must never become a pointer to write through.
    if (!nand_permission_note_valid(*note())) return;
    g_recovery_word = note()->word;
    if (note()->ios != static_cast<u32>(IOS_GetVersion())) {
        note()->magic = 0;
        FlushNote();
        g_recovery = "the saved IOS patch belongs to another IOS and was discarded";
    } else if (read32(0x0D800064) != 0xFFFFFFFF) {
        g_recovery = "the saved IOS patch could not be checked without hardware access";
    } else {
        u32 found = 0;
        g_recovery = Restore(found) ? "the NAND permission check left by an interrupted run was closed"
                                    : "the saved NAND permission patch no longer matches, or could not be closed";
    }
}

void LogNandPermissionRecovery() {
    if (!g_recovery) return;
    logf("Start-up recovery: %s (word 0x%08x).\n", g_recovery, g_recovery_word);
    g_recovery = nullptr;
}

bool open_nand_permissions(const char* who) {
    if (g_open) return true;
    if (read32(0x0D800064) != 0xFFFFFFFF) {
        logf("%s: no hardware access; IOS%d is left as it is.\n", who, IOS_GetVersion());
        return false;
    }
    // A failed restore retains the note, and must not be overwritten by a
    // second patch. Retry it while the same IOS is still running.
    u32 found = 0;
    if (nand_permission_note_valid(*note()) && note()->ios == static_cast<u32>(IOS_GetVersion()) && !Restore(found)) {
        logf("%s: the earlier NAND permission patch could not be closed; no new patch was made.\n", who);
        return false;
    }
    constexpr u32 kFrom = 0xD3400000, kTo = 0xD4000000;
    logf("%s: checking IOS%d's NAND permission code before opening it.\n", who, IOS_GetVersion());
    const u16 protection = read16(0x0D8B420A);
    write16(0x0D8B420A, 2);
    std::size_t at = 0;
    if (!find_nand_check(reinterpret_cast<const u8*>(kFrom), kTo - kFrom, at)) {
        write16(0x0D8B420A, protection);
        logf("%s: IOS%d's NAND permission check was not found.\n", who, IOS_GetVersion());
        return false;
    }
    const u32 match = kFrom + static_cast<u32>(at);
    const NandCheckStore store = nand_check_store(match);
    const u32 before = read32(store.word);
    const u32 after = nand_check_opened(before, store.shift);
    NandPermissionNote saved;
    saved.magic = 0x52574E50;
    saved.ios = IOS_GetVersion();
    saved.word = store.word;
    saved.before = before;
    saved.after = after;
    saved.shift = store.shift;
    saved.checksum = nand_permission_checksum(saved);
    *note() = saved;
    FlushNote();
    // One aligned 32-bit store: smaller uncached stores have hung consoles.
    write32(store.word, after);
    const u32 read_back = read32(store.word);
    if (read_back != after) write32(store.word, before);
    write16(0x0D8B420A, protection);
    if (read_back != after) {
        // A failed own write may be partial; rollback is unconditional here,
        // while recovery at another start requires the patched halfword to
        // hold the opening. Only that halfword is compared, as in Restore.
        if (nand_check_half(read32(store.word), store.shift) == nand_check_half(before, store.shift)) {
            note()->magic = 0;
            FlushNote();
        }
        logf("%s: IOS%d's NAND permission write at 0x%08x failed (read 0x%08x).\n",
             who, IOS_GetVersion(), store.word, read_back);
        return false;
    }
    g_open = true;
    logf("%s: IOS%d's NAND permission check opened at 0x%08x.\n", who, IOS_GetVersion(), match);
    return true;
}

void close_nand_permissions(const char* who) {
    if (!g_open) return;
    g_open = false;
    if (!nand_permission_note_valid(*note())) {
        logf("%s: the NAND permission patch's saved state is damaged; IOS was left as it is.\n", who);
        return;
    }
    const NandPermissionNote saved = *note();
    if (saved.ios != static_cast<u32>(IOS_GetVersion()) || read32(0x0D800064) != 0xFFFFFFFF) {
        logf("%s: the NAND permission check cannot be restored under this IOS or without hardware access.\n", who);
        return;
    }
    u32 found = 0;
    if (Restore(found)) {
        logf("%s: IOS%d's NAND permission check closed (word 0x%08x, expected 0x%08x).\n", who, IOS_GetVersion(),
             saved.word, saved.after);
    } else {
        logf("%s: IOS%d's NAND permission check changed or could not be closed (word 0x%08x read 0x%08x, "
             "original 0x%08x, opened 0x%08x).\n",
             who, IOS_GetVersion(), saved.word, found, saved.before, saved.after);
    }
}

}  // namespace riftwii::wii
