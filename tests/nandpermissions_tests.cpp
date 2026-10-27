// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "nandpermissions.hpp"
#include "memlimits.hpp"
#include "riftwii/nandcheck.hpp"

#include <gccore.h>
#include <cstdarg>
#include <cstring>
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
u16 read16(u32) { return protection; }
u32 read32(u32 address) {
    if (address == 0x0D800064) return hardware ? 0xFFFFFFFF : 0;
    return address == kWord ? current : 0;
}
void write16(u32, u16 value) { protection = value; }
void write32(u32 address, u32 value) {
    if (address != kWord) return;
    ++writes;
    if (value != kBefore && fail_write) current = 0xDEADBEEF;
    else if (value != kBefore || !fail_restore) current = value;
}
namespace riftwii::wii {
void logf(const char*, ...) {}
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
    fail_write = false;
    check(open_nand_permissions("test"));
    saved->word = 0x80000000;
    saved->checksum = nand_permission_checksum(*saved);
    const unsigned before_bad = writes;
    RecoverNandPermissions();
    check(writes == before_bad);
    std::cout << failures << " NAND permission failures\n";
    return failures == 0 ? 0 : 1;
}
