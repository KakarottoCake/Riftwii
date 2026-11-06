// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "nandpermissions.hpp"

#include <gccore.h>
#include <ogc/cache.h>
#include <ogc/machine/processor.h>
#include <ogc/mutex.h>

#include <cstdio>

#include "log.hpp"
#include "memlimits.hpp"
#include "riftwii/nandcheck.hpp"

namespace riftwii::wii {
namespace {

NandPermissionNote* note() {
    return reinterpret_cast<NandPermissionNote*>(mem::kNandPermissionNote);
}

mutex_t g_lock = LWP_MUTEX_NULL;

struct Locked {
    Locked() : acquired(g_lock != LWP_MUTEX_NULL && LWP_MutexLock(g_lock) == 0) {}
    ~Locked() {
        if (acquired) LWP_MutexUnlock(g_lock);
    }
    Locked(const Locked&) = delete;
    Locked& operator=(const Locked&) = delete;

    bool acquired;
};

// Format while state is locked, then write to the SD log after unlocking.
// An operation produces at most a search line and one result line.
struct PermissionLog {
    ~PermissionLog() {
        for (unsigned line = 0; line < count; ++line) logf("%s", lines[line]);
    }

    template <typename... Args>
    void Add(const char* format, Args... args) {
        if (count == 2) return;
        std::snprintf(lines[count], sizeof(lines[count]), format, args...);
        ++count;
    }

    char lines[2][512]{};
    unsigned count = 0;
};

unsigned g_users = 0;
int g_open_ios = 0;
int g_open_revision = 0;
// A nested user may outlive the caller that supplied the opening label.
char g_open_who[128] = {};

struct CheckAddress {
    int ios = 0;
    int revision = 0;
    u32 match = 0;
    u32 before = 0;
};
CheckAddress g_check;
const char* g_recovery = nullptr;
u32 g_recovery_word = 0;

void FlushNote() { DCFlushRange(note(), sizeof(NandPermissionNote)); }

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
// only once that word is back. So does a word still exactly as RiftWii's
// last failed store left it (the note's `left`), whatever that store did
// to the patched halfword. A patched halfword holding anything else
// (IOS reloaded) is left alone and the note dropped. A confirmed close
// that doesn't take (its store damaged either halfword, or didn't land)
// leaves a note for exactly the word it meant to write, the other
// halfword as IOS had it then: that note counts as unconfirmed, so a later
// close or start puts that whole word back, and neither undoes IOS's own
// change nor mistakes the damage for one. `found` is what the word held
// when the close looked, for the log.
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
        ours = patched == 0xE001 || patched == nand_check_half(saved.before, saved.shift) || found == saved.left;
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
    } else {
        if (saved.opened) {
            note()->before = target;
            note()->after = nand_check_opened(target, saved.shift);
            note()->opened = 0;
        }
        note()->left = now;
        note()->checksum = nand_permission_checksum(*note());
    }
    FlushNote();
    return restored ? CloseResult::closed : CloseResult::left;
}

}  // namespace

void InitNandPermissionLock() {
    // main calls this after memory setup and before starting any readers.
    if (g_lock == LWP_MUTEX_NULL) LWP_MutexInit(&g_lock, false);
}

void RecoverNandPermissions() {
    // Startup only, before the mutex or any reader threads exist.
    g_users = 0;
    g_check = {};
    g_open_ios = IOS_GetVersion();
    g_open_revision = IOS_GetRevision();
    std::snprintf(g_open_who, sizeof(g_open_who), "%s", "Start-up recovery");
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

namespace {

bool OpenPermissions(const char* who, PermissionLog& messages) {
    const int ios = IOS_GetVersion();
    const int revision = IOS_GetRevision();
    if (g_users && g_open_ios == ios && g_open_revision == revision) {
        ++g_users;
        return true;
    }
    // Reloads through RiftWii clear this explicitly, including same-IOS
    // reloads. Also discard a cache whose IOS identity changed elsewhere.
    const bool ios_changed = g_open_ios != ios || g_open_revision != revision;
    if (g_users || (ios_changed && nand_permission_note_valid(*note()))) {
        // A known revision change is a reload too. Do not let the old
        // live note authorize a restore into the new revision's code.
        note()->magic = 0;
        FlushNote();
    }
    g_users = 0;
    if (g_check.ios != ios || g_check.revision != revision) g_check = {};
    if (read32(0x0D800064) != 0xFFFFFFFF) {
        messages.Add("%s: no hardware access; IOS%d is left as it is.\n", who, IOS_GetVersion());
        return false;
    }
    // A failed restore retains the note, and must not be overwritten by a
    // second patch. Retry it while the same IOS is still running. A retry
    // that drops the note (the word is no longer the patch's: IOS reloaded)
    // leaves nothing in the way, so the opening goes on.
    u32 found = 0;
    if (nand_permission_note_valid(*note()) && note()->ios == static_cast<u32>(IOS_GetVersion())) {
        const CloseResult earlier = Restore(found);
        if (earlier != CloseResult::closed && nand_permission_note_valid(*note())) {
            messages.Add("%s: the earlier NAND permission patch could not be closed%s; no new patch was made.\n", who,
                         earlier == CloseResult::unreachable ? " (memory protection did not turn off)" : "");
            return false;
        }
    }
    constexpr u32 kFrom = 0xD3400000, kTo = 0xD4000000;
    u16 protection = 0;
    if (!UnprotectIosCode(protection)) {
        messages.Add("%s: IOS%d's memory protection did not turn off; IOS is left as it is.\n", who, IOS_GetVersion());
        return false;
    }
    if (g_check.match) {
        const NandCheckStore cached = nand_check_store(g_check.match);
        if (read32(cached.word) != g_check.before) g_check = {};
    }
    if (!g_check.match) {
        messages.Add("%s: checking IOS%d's NAND permission code before opening it.\n", who, ios);
        std::size_t at = 0;
        if (!find_nand_check(reinterpret_cast<const u8*>(kFrom), kTo - kFrom, at)) {
            write16(kMemProt, protection);
            messages.Add("%s: IOS%d's NAND permission check was not found.\n", who, ios);
            return false;
        }
        const u32 match = kFrom + static_cast<u32>(at);
        const NandCheckStore store = nand_check_store(match);
        const u32 before = read32(store.word);
        // The search and store are separate reads. Never patch a branch
        // that stopped being the original check in between them.
        if (nand_check_half(before, store.shift) != 0xD001) {
            write16(kMemProt, protection);
            messages.Add("%s: IOS%d's NAND permission code changed before it could be opened.\n", who, ios);
            return false;
        }
        g_check = {ios, revision, match, before};
    }
    const u32 match = g_check.match;
    const NandCheckStore store = nand_check_store(match);
    const u32 before = read32(store.word);
    if (before != g_check.before) {
        g_check = {};
        write16(kMemProt, protection);
        messages.Add("%s: IOS%d's NAND permission code changed before it could be opened.\n", who, ios);
        return false;
    }
    const u32 after = nand_check_opened(before, store.shift);
    NandPermissionNote saved;
    saved.magic = 0x52574E50;
    saved.ios = IOS_GetVersion();
    saved.word = store.word;
    saved.before = before;
    saved.after = after;
    saved.shift = store.shift;
    saved.left = before;
    saved.checksum = nand_permission_checksum(saved);
    g_open_ios = ios;
    g_open_revision = revision;
    std::snprintf(g_open_who, sizeof(g_open_who), "%s", who);
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
    u32 rolled = read_back;
    if (read_back != after) {
        write32(store.word, before);
        rolled = read32(store.word);
    }
    write16(kMemProt, protection);
    if (read_back != after) {
        if (rolled == before) {
            note()->magic = 0;
        } else {
            note()->left = rolled;
            note()->checksum = nand_permission_checksum(*note());
        }
        FlushNote();
        messages.Add("%s: IOS%d's NAND permission write at 0x%08x failed (read 0x%08x).\n",
                     who, IOS_GetVersion(), store.word, read_back);
        return false;
    }
    note()->opened = 1;
    note()->left = after;
    note()->checksum = nand_permission_checksum(*note());
    FlushNote();
    g_users = 1;
    messages.Add("%s: IOS%d's NAND permission check opened at 0x%08x.\n", who, IOS_GetVersion(), match);
    return true;
}

void ClosePermissions(const char* who, PermissionLog& messages, bool all = false) {
    if (all) {
        const bool had_users = g_users != 0;
        g_users = 0;
        // A failed close or opening rollback can leave cleanup with no users.
        if (!had_users && !nand_permission_note_valid(*note())) return;
    } else {
        if (g_users == 0) return;
        if (--g_users != 0) return;
    }
    // Attribute the one close to the batch that made the opening, even
    // if its last nested user is the one to release it.
    who = g_open_who;
    if (!nand_permission_note_valid(*note())) {
        messages.Add("%s: the NAND permission patch's saved state is damaged; IOS was left as it is.\n", who);
        return;
    }
    const NandPermissionNote saved = *note();
    const bool ios_changed = saved.ios != static_cast<u32>(IOS_GetVersion()) || g_open_revision != IOS_GetRevision();
    if (ios_changed || read32(0x0D800064) != 0xFFFFFFFF) {
        g_check = {};
        if (ios_changed) {
            note()->magic = 0;
            FlushNote();
        }
        messages.Add("%s: the NAND permission check cannot be restored under this IOS or without hardware access.\n", who);
        return;
    }
    u32 found = 0;
    switch (Restore(found)) {
    case CloseResult::closed:
        messages.Add("%s: IOS%d's NAND permission check closed (word 0x%08x, expected 0x%08x).\n",
                     who, IOS_GetVersion(), saved.word, saved.after);
        break;
    case CloseResult::left:
        messages.Add("%s: IOS%d's NAND permission check changed or could not be closed (word 0x%08x read 0x%08x, "
                     "original 0x%08x, opened 0x%08x).\n",
                     who, IOS_GetVersion(), saved.word, found, saved.before, saved.after);
        break;
    case CloseResult::unreachable:
        messages.Add("%s: IOS%d's memory protection did not turn off, so its NAND permission check (word 0x%08x) was "
                     "not read or closed; the next opening or start tries the close again.\n",
                     who, IOS_GetVersion(), saved.word);
        break;
    }
}

}  // namespace

bool open_nand_permissions(const char* who) {
    PermissionLog messages;
    Locked lock;
    if (!lock.acquired) return false;
    return OpenPermissions(who, messages);
}

void close_nand_permissions(const char* who) {
    PermissionLog messages;
    Locked lock;
    if (!lock.acquired) return;
    ClosePermissions(who, messages);
}

void CloseAllNandPermissions(const char* who) {
    PermissionLog messages;
    Locked lock;
    if (!lock.acquired) return;
    ClosePermissions(who, messages, true);
    g_check = {};
}

}  // namespace riftwii::wii
