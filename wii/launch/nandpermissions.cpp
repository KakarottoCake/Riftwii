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

// MEM_PROT keeps IOS's code from the PowerPC: while it is on (bit 0 set,
// 0x0001 on a vWii), reads there give a fill pattern (0x9abcdef0,
// 0x12345678) and writes are dropped. Writing 2 turns it off, and the
// register then reads back with bit 0 clear (0x0000). The change counts
// only once the register reads back that way, so it is read before IOS's
// code is touched. On a console (3 Nov) the close read the word on the
// next instruction, got the fill pattern and left IOS58's check open (the
// opening only worked because its search ran first); with one read of the
// register in between, IOS's code read right at once. If protection never
// reads as off, IOS's code is left alone (protection back as it was)
// rather than read as the fill pattern, which would look like a closed or
// missing check.
constexpr u32 kMemProt = 0x0D8B420A;

bool UnprotectIosCode(u16& was) {
    was = read16(kMemProt);
    write16(kMemProt, 2);
    for (int tries = 0; tries < 1000; ++tries) {
        if ((read16(kMemProt) & 1) == 0) return true;
    }
    write16(kMemProt, was);
    return false;
}

enum class CloseResult {
    closed,       // the check is back as it was
    left,         // the word isn't ours to close, or the close didn't take
    unreachable,  // protection didn't turn off: nothing was read, the note stays
};

// After a confirmed opening, only the patched halfword is ours, as in the
// channel installer (#379): IOS may change the other halfword of the same
// word, which must neither block the close nor be undone, and the close
// counts only if it left that other halfword as it found it. The word is
// still read and written whole: smaller uncached stores have hung
// consoles. An opening that never read back whole (failed, or cut off
// before its check) may have damaged either halfword itself, so while the
// patched halfword is the opening's or the original's, it is undone as
// the opening's rollback does, with the whole original word, and counts
// only once that word is back. A patched halfword holding anything else
// (IOS reloaded) is left alone and the note dropped. `found` is what the
// word held when the close looked, for the log.
CloseResult Restore(u32& found) {
    const NandPermissionNote saved = *note();
    u16 protection = 0;
    found = 0;
    if (!UnprotectIosCode(protection)) return CloseResult::unreachable;
    found = read32(saved.word);
    const unsigned other = saved.shift ^ 16;
    const u16 patched = nand_check_half(found, saved.shift);
    u32 target = saved.before;
    bool ours = false;
    if (saved.opened) {
        ours = nand_check_closed(found, saved.before, saved.shift, target);
    } else {
        ours = patched == 0xE001 || patched == nand_check_half(saved.before, saved.shift);
    }
    if (ours) write32(saved.word, target);
    const u32 now = read32(saved.word);
    const bool restored = ours && saved.opened
                              ? nand_check_half(now, saved.shift) == nand_check_half(saved.before, saved.shift) &&
                                    nand_check_half(now, other) == nand_check_half(found, other)
                              : now == saved.before;
    write16(kMemProt, protection);
    if (restored || !ours) {
        note()->magic = 0;
        FlushNote();
    }
    return restored ? CloseResult::closed : CloseResult::left;
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
        switch (Restore(found)) {
        case CloseResult::closed:
            g_recovery = "the NAND permission check left by an interrupted run was closed";
            break;
        case CloseResult::left:
            g_recovery = "the saved NAND permission patch no longer matches, or could not be closed";
            break;
        case CloseResult::unreachable:
            g_recovery = "the saved NAND permission patch could not be checked: memory protection did not turn off";
            break;
        }
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
    if (nand_permission_note_valid(*note()) && note()->ios == static_cast<u32>(IOS_GetVersion())) {
        const CloseResult earlier = Restore(found);
        if (earlier != CloseResult::closed) {
            logf("%s: the earlier NAND permission patch could not be closed%s; no new patch was made.\n", who,
                 earlier == CloseResult::unreachable ? " (memory protection did not turn off)" : "");
            return false;
        }
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
    switch (Restore(found)) {
    case CloseResult::closed:
        logf("%s: IOS%d's NAND permission check closed (word 0x%08x, expected 0x%08x).\n", who, IOS_GetVersion(),
             saved.word, saved.after);
        break;
    case CloseResult::left:
        logf("%s: IOS%d's NAND permission check changed or could not be closed (word 0x%08x read 0x%08x, "
             "original 0x%08x, opened 0x%08x).\n",
             who, IOS_GetVersion(), saved.word, found, saved.before, saved.after);
        break;
    case CloseResult::unreachable:
        logf("%s: IOS%d's memory protection did not turn off, so its NAND permission check (word 0x%08x) is "
             "still open; the next opening or start tries the close again.\n",
             who, IOS_GetVersion(), saved.word);
        break;
    }
}

}  // namespace riftwii::wii
