// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "nandpermissions.hpp"
#include "memlimits.hpp"
#include "riftwii/nandcheck.hpp"

#include <gccore.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <iostream>

#if defined(_WIN32) || defined(__CYGWIN__)
#include <windows.h>
#elif defined(__linux__)
#include <sys/mman.h>
#endif

namespace {
constexpr u32 kCode = 0xD3400000;
constexpr u32 kWord = kCode + 0x100;
constexpr u32 kBefore = 0x428BD001;
u32 current = kBefore;
u16 protection = 1;
int ios = 58;
bool hardware = true;
bool fail_write = false;
bool fail_restore = false;
bool damage_other = false;  // the original word goes back with the other halfword damaged
bool damage_opening = false;  // the opening write lands with the other halfword damaged
// As on a console: a MEM_PROT write lands only once the register is read
// again, writing 2 reads back as 0x0000 (off), and while protection is on
// (bit 0), IOS's code reads as a fill pattern and drops writes.
bool posted_protection = false;
bool stuck_protection = false;  // turning protection off never lands
bool protection_pending = false;
u16 protection_next = 0;
unsigned writes = 0;

void* map_at(u32 address, std::size_t size) {
#if defined(_WIN32) || defined(__CYGWIN__)
    return VirtualAlloc(reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)), size,
                        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#elif defined(__linux__)
    void* result = mmap(reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)), size,
                        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    return result == MAP_FAILED ? nullptr : result;
#else
    (void)address;
    (void)size;
    return nullptr;
#endif
}
}  // namespace

int IOS_GetVersion() { return ios; }
u16 read16(u32) {
    if (protection_pending) {
        protection = protection_next;
        protection_pending = false;
    }
    return protection;
}
u32 read32(u32 address) {
    if (address == 0x0D800064) return hardware ? 0xFFFFFFFF : 0;
    if (address != kWord) return 0;
    return (protection & 1) == 0 ? current : 0x9ABCDEF0u;
}
void write16(u32, u16 value) {
    if (stuck_protection && value == 2) return;
    const u16 reads_back = value == 2 ? 0 : value;
    if (posted_protection) {
        protection_next = reads_back;
        protection_pending = true;
    } else {
        protection = reads_back;
    }
}
void write32(u32 address, u32 value) {
    if (address != kWord || (protection & 1) != 0) return;
    ++writes;
    if (value == kBefore && damage_other) current = 0xDEAD0000u | (kBefore & 0x0000FFFFu);
    else if (value != kBefore && damage_opening) current = 0xDEAD0000u | (value & 0x0000FFFFu);
    else if (value != kBefore && fail_write) current = 0xDEADBEEF;
    else if (value != kBefore || !fail_restore) current = value;
}
namespace riftwii::wii {
std::string g_last_log;  // the last line logged, to check what recovery says
void logf(const char* format, ...) {
    char line[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    g_last_log = line;
}
}

int main() {
    using namespace riftwii;
    using namespace riftwii::wii;
    if (!map_at(kCode, 0xC00000) || !map_at(mem::kRestartArea, mem::kRestartBytes)) {
        std::cout << "No memory at the Wii's addresses\n";
        return 77;
    }
    auto* code = reinterpret_cast<u8*>(static_cast<std::uintptr_t>(kCode));
    std::memcpy(code + 0x100, kNandCheck, sizeof(kNandCheck));
    auto* saved = reinterpret_cast<NandPermissionNote*>(static_cast<std::uintptr_t>(mem::kNandPermissionNote));
    *saved = NandPermissionNote{};
    unsigned failures = 0;
    const auto check = [&](bool passed) {
        if (!passed) ++failures;
    };
    check(open_nand_permissions("test"));
    check(nand_permission_note_valid(*saved));
    check(protection == 1);
    // A reset between opening a protected file and the scope's cleanup.
    RecoverNandPermissions();
    check(current == kBefore && saved->magic == 0);
    check(open_nand_permissions("test"));
    close_nand_permissions("test");
    check(current == kBefore && saved->magic == 0);
    // IOS changed the other halfword while the patch was open: the close
    // puts back only the patched halfword and keeps IOS's change.
    check(open_nand_permissions("test"));
    current = (current & 0x0000FFFFu) | 0x12340000u;
    close_nand_permissions("test");
    check(current == 0x1234D001u && saved->magic == 0);
    current = kBefore;
    // Failed cleanup remains recoverable at the next start.
    check(open_nand_permissions("test"));
    fail_restore = true;
    close_nand_permissions("test");
    check(nand_permission_note_valid(*saved));
    fail_restore = false;
    RecoverNandPermissions();
    check(current == kBefore && saved->magic == 0);
    // Another IOS or changed code must never be restored from old bytes.
    check(open_nand_permissions("test"));
    const unsigned before_reload = writes;
    ios = 55;
    RecoverNandPermissions();
    check(writes == before_reload && saved->magic == 0);
    ios = 58;
    current = kBefore;
    check(open_nand_permissions("test"));
    current = 0x11223344;
    const unsigned before_changed = writes;
    RecoverNandPermissions();
    check(writes == before_changed && saved->magic == 0);
    current = kBefore;
    fail_write = true;
    check(!open_nand_permissions("test"));
    check(current == kBefore && saved->magic == 0 && protection == 1);
    // A failed opening whose rollback leaves the other halfword damaged
    // (the patched one back): the note stays, as the whole word isn't.
    damage_other = true;
    check(!open_nand_permissions("test"));
    check(current == 0xDEADD001u && nand_permission_note_valid(*saved));
    damage_other = false;
    // The next start's recovery: the opening was never confirmed, so the
    // damage is taken as its own and the whole original word goes back.
    RecoverNandPermissions();
    LogNandPermissionRecovery();
    check(saved->magic == 0 && current == kBefore);
    check(riftwii::wii::g_last_log.find("was closed") != std::string::npos);
    current = kBefore;
    fail_write = false;
    // A failed opening that left the patched halfword opened and the other
    // one damaged, with its rollback failing too: the next start puts the
    // whole original word back, not just the patched halfword, and keeps
    // the note until that word is back.
    damage_opening = true;
    fail_restore = true;
    check(!open_nand_permissions("test"));
    check(current == 0xDEADE001u && nand_permission_note_valid(*saved) && saved->opened == 0);
    damage_opening = false;
    RecoverNandPermissions();
    check(current == 0xDEADE001u && nand_permission_note_valid(*saved));
    fail_restore = false;
    RecoverNandPermissions();
    LogNandPermissionRecovery();
    check(current == kBefore && saved->magic == 0);
    check(riftwii::wii::g_last_log.find("was closed") != std::string::npos);
    check(open_nand_permissions("test"));
    check(saved->opened == 1 && nand_permission_note_valid(*saved));
    close_nand_permissions("test");
    check(current == kBefore && saved->magic == 0);
    // A close whose store damages the other halfword isn't reported as
    // closed, and the note stays.
    check(open_nand_permissions("test"));
    damage_other = true;
    close_nand_permissions("test");
    damage_other = false;
    check(current == 0xDEADD001u && nand_permission_note_valid(*saved));
    check(riftwii::wii::g_last_log.find("could not be closed") != std::string::npos);
    saved->magic = 0;
    current = kBefore;
    // MEM_PROT writes that land late (a console, 3 Nov): the opening and
    // the close wait until protection is off before touching IOS's code.
    posted_protection = true;
    check(open_nand_permissions("test"));
    check(current == 0x428BE001u);
    close_nand_permissions("test");
    check(current == kBefore && saved->magic == 0);
    posted_protection = false;
    read16(0x0D8B420A);  // the last restore of the register lands
    check(protection == 1);
    // Protection that never turns off: IOS's code is left alone, so an open
    // patch keeps its note (and blocks a new one) until a later close can
    // reach it, and an opening is refused without a write.
    check(open_nand_permissions("test"));
    stuck_protection = true;
    close_nand_permissions("test");
    check(current == 0x428BE001u && nand_permission_note_valid(*saved) && protection == 1);
    check(riftwii::wii::g_last_log.find("did not turn off") != std::string::npos);
    check(!open_nand_permissions("test"));
    stuck_protection = false;
    RecoverNandPermissions();
    check(current == kBefore && saved->magic == 0);
    stuck_protection = true;
    const unsigned before_stuck = writes;
    check(!open_nand_permissions("test"));
    check(writes == before_stuck && saved->magic == 0 && protection == 1);
    stuck_protection = false;
    check(open_nand_permissions("test"));
    saved->word = 0x80000000;
    saved->checksum = nand_permission_checksum(*saved);
    const unsigned before_bad = writes;
    RecoverNandPermissions();
    check(writes == before_bad);
    std::cout << failures << " NAND permission failures\n";
    return failures == 0 ? 0 : 1;
}
