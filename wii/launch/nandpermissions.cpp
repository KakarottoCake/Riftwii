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

// MEM_PROT keeps IOS's code from the PowerPC: while it is on, reads there
// give a fill pattern (0x9abcdef0, 0x12345678) and writes are dropped.
// Turning it off counts only once the memory controller holds the new
// value, so the register is read back until it does before IOS's code is
// touched. On a console (3 Nov) the close read the word on the next
// instruction, got the fill pattern and left IOS58's check open; the
// opening only worked because its search ran first. If it never does,
// IOS's code is left alone (protection back as it was) rather than read
// as the fill pattern, which would look like a closed or missing check.
constexpr u32 kMemProt = 0x0D8B420A;

bool UnprotectIosCode(u16& was) {
    was = read16(kMemProt);
    write16(kMemProt, 2);
    for (int tries = 0; tries < 1000; ++tries) {
        if (read16(kMemProt) == 2) return true;
    }
    write16(kMemProt, was);
    return false;
}

// Only the patched halfword is ours, as in the channel installer (#379):
// IOS may change the other halfword of the same word, which must neither
// block the close nor be undone. The word is still read and written
// whole: smaller uncached stores have hung consoles. An opening that never
// read back whole (failed, or cut off before its check) may have damaged
// the other halfword itself, so it is undone as the opening's rollback
// does: the whole original word. That, and a patched halfword that no
// longer holds the opening (IOS reloaded), count as restored only if the
// whole original word is back: a damaged other halfword is never reported
// as closed. `found` is what the word held when the close looked, for the
// log. With protection stuck on, nothing is read and the note stays for a
// later try.
bool Restore(u32& found) {
    const NandPermissionNote saved = *note();
    u16 protection = 0;
    found = 0;
    if (!UnprotectIosCode(protection)) return false;
    found = read32(saved.word);
    u32 closed = 0;
    const bool ours = nand_check_closed(found, saved.before, saved.shift, closed);
    if (ours) write32(saved.word, saved.opened ? closed : saved.before);
    const u32 now = read32(saved.word);
    const bool restored = ours && saved.opened
                              ? nand_check_half(now, saved.shift) == nand_check_half(saved.before, saved.shift)
                              : now == saved.before;
    write16(kMemProt, protection);
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
    u16 protection = 0;
    if (!UnprotectIosCode(protection)) {
        logf("%s: IOS%d's memory protection did not turn off; IOS is left as it is.\n", who, IOS_GetVersion());
        return false;
    }
    std::size_t at = 0;
    if (!find_nand_check(reinterpret_cast<const u8*>(kFrom), kTo - kFrom, at)) {
        write16(kMemProt, protection);
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
    // A failed own write may be partial, the other halfword included:
    // rollback writes the whole original word, and the note goes only once
    // that whole word is back (unlike Restore, where the other halfword is
    // IOS's to change while the check is open). It is read back before
    // protection returns, which would hide it behind the fill pattern.
    bool rolled_back = false;
    if (read_back != after) {
        write32(store.word, before);
        rolled_back = read32(store.word) == before;
    }
    write16(kMemProt, protection);
    if (read_back != after) {
        if (rolled_back) {
            note()->magic = 0;
            FlushNote();
        }
        logf("%s: IOS%d's NAND permission write at 0x%08x failed (read 0x%08x).\n",
             who, IOS_GetVersion(), store.word, read_back);
        return false;
    }
    note()->opened = 1;
    note()->checksum = nand_permission_checksum(*note());
    FlushNote();
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
