// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the installer's real NAND cleanup with failed hardware stores
// and content reads. The permission pattern lives at its Wii address.
#include "cios.hpp"

#include <gccore.h>
#include <ogc/es.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

#include "riftwii/nandcheck.hpp"

#ifndef __has_feature
#define __has_feature(feature) 0
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
#include <windows.h>
#elif defined(__linux__)
#include <sys/mman.h>
#endif

namespace {

constexpr u32 kMemory = 0xD3400000;
constexpr std::size_t kMemorySize = 0xC00000;
constexpr u32 kPermissionWord = kMemory + 0x100;
constexpr u32 kOriginalWord = 0x428BD001;
constexpr u16 kOriginalProtection = 1;

bool hardware_access;
bool fail_open_store;
bool fail_word_restore;
unsigned fail_protection_restore;
unsigned protection_restores;
unsigned open_calls;
unsigned opening_stores;
s32 read_result;
u32 permission_word;
u16 protection;
std::string logs;

void reset(void* memory) {
    std::memset(memory, 0, kMemorySize);
    std::memcpy(static_cast<u8*>(memory) + 0x100, riftwii::kNandCheck, sizeof(riftwii::kNandCheck));
    hardware_access = true;
    fail_open_store = false;
    fail_word_restore = false;
    fail_protection_restore = 0;
    protection_restores = 0;
    open_calls = 0;
    opening_stores = 0;
    read_result = 0x40;
    permission_word = kOriginalWord;
    protection = kOriginalProtection;
    logs.clear();
}

}  // namespace

int IOS_GetVersion() { return 58; }

u16 read16(u32) { return protection; }

u32 read32(u32 address) {
    if (address == 0x0D800064) return hardware_access ? 0xFFFFFFFF : 0;
    return address == kPermissionWord ? permission_word : 0;
}

void write16(u32, u16 value) {
    if (value == kOriginalProtection) {
        ++protection_restores;
        if (protection_restores == fail_protection_restore) return;
    }
    protection = value;
}

void write32(u32 address, u32 value) {
    if (address != kPermissionWord) return;
    if (value == kOriginalWord && fail_word_restore) return;
    if (value != kOriginalWord) ++opening_stores;
    if (value != kOriginalWord && fail_open_store) {
        permission_word = 0xDEADBEEF;  // even a partial failed store needs cleanup
        return;
    }
    permission_word = value;
}

s32 ES_GetTMDViewSize(u64, u32* size) {
    *size = sizeof(tmd_view) + sizeof(tmd_view_content);
    return 0;
}

s32 ES_GetTMDView(u64 title, tmd_view* view, u32) {
    view->title_id = title;
    view->num_contents = 1;
    view->contents[0].cid = 1;
    return 0;
}

// IOS58 refuses the file until its permission check is opened.
s32 ISFS_Open(const char*, int) {
    ++open_calls;
    return permission_word == kOriginalWord ? -102 : 7;
}

s32 ISFS_Read(s32, void* data, u32 size) {
    std::memset(data, 0, size);
    const u8 info[] = {0x1E, 0xE7, 0xC1, 0x05, 0, 0, 0, 1, 0, 0, 0, 11, 0, 0, 0, 58};
    std::memcpy(data, info, sizeof(info));
    std::memcpy(static_cast<u8*>(data) + 0x10, "d2x", 4);
    return read_result;
}

s32 ISFS_Close(s32) { return 0; }

namespace installer {
void Log(const char* format, ...) {
    char line[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    logs += line;
}
}  // namespace installer

int main() {
#if defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer)
    std::cout << "Installer cIOS tests need addresses reserved by AddressSanitizer.\n";
    return 77;
#endif
    void* memory = nullptr;
#if defined(_WIN32) || defined(__CYGWIN__)
    memory = VirtualAlloc(reinterpret_cast<void*>(kMemory), kMemorySize,
                          MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#elif defined(__linux__)
    memory = mmap(reinterpret_cast<void*>(kMemory), kMemorySize, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (memory == MAP_FAILED) memory = nullptr;
#endif
    if (memory != reinterpret_cast<void*>(kMemory)) {
        std::cout << "Installer cIOS tests cannot map the IOS permission search range.\n";
        return 77;
    }

    unsigned failures = 0;
    const auto check = [&](const char* name, bool expected_read, bool expected_restored) {
        bool read = false;
        installer::NandReadAccess access;
        const auto info = installer::ReadCios(249, access, read);
        const bool restored = access.Restore();
        if (read != expected_read || restored != expected_restored || info.d2x != expected_read ||
            (restored && (permission_word != kOriginalWord || protection != kOriginalProtection))) {
            std::cerr << "FAILED: " << name << "\n" << logs;
            ++failures;
        }
    };

    reset(memory);
    check("successful read restores both values", true, true);
    reset(memory);
    fail_word_restore = true;
    check("permission word restore failure is reported", true, false);
    reset(memory);
    fail_protection_restore = 2;
    check("final protection restore failure is reported", true, false);
    reset(memory);
    fail_protection_restore = 1;
    check("earlier protection restore failure remains latched", false, false);
    reset(memory);
    read_result = -1;
    check("content read error still restores access", false, true);
    reset(memory);
    fail_open_store = true;
    check("failed opening store is undone", false, true);
    reset(memory);
    fail_open_store = true;
    fail_word_restore = true;
    check("failed opening store also reports cleanup failure", false, false);
    reset(memory);
    std::memset(memory, 0, kMemorySize);
    fail_protection_restore = 1;
    check("missing permission pattern still checks protection restore", false, false);
    reset(memory);
    hardware_access = false;
    check("no hardware access leaves state alone", false, true);

    // The whole search shares one opening, closed once after every slot.
    reset(memory);
    {
        installer::NandReadAccess access;
        bool all_read = true;
        for (const int slot : {249, 250, 251}) {
            bool read = false;
            all_read = installer::ReadCios(slot, access, read).d2x && read && all_read;
        }
        const bool restored = access.Restore();
        if (!all_read || !restored || opening_stores != 1 || open_calls != 4 ||
            permission_word != kOriginalWord || protection != kOriginalProtection) {
            std::cerr << "FAILED: three slots share one opening\n" << logs;
            ++failures;
        }
    }

    // A change outside the patched halfword is kept.
    reset(memory);
    {
        installer::NandReadAccess access;
        bool read = false;
        installer::ReadCios(249, access, read);
        permission_word = (permission_word & 0x0000FFFF) | 0xABCD0000;
        if (!access.Restore() || permission_word != 0xABCDD001) {
            std::cerr << "FAILED: a change outside the patched halfword is kept\n" << logs;
            ++failures;
        }
    }

    // A patched halfword someone else changed is left alone and reported.
    reset(memory);
    {
        installer::NandReadAccess access;
        bool read = false;
        installer::ReadCios(249, access, read);
        permission_word = 0x428B1234;
        if (access.Restore() || permission_word != 0x428B1234 ||
            logs.find("read 0x428b1234") == std::string::npos) {
            std::cerr << "FAILED: a patched halfword someone else changed is left alone and reported\n" << logs;
            ++failures;
        }
    }

    // Unsafe() is false while the check is deliberately open, and true as
    // soon as the check or the protection register may be left changed.
    reset(memory);
    {
        installer::NandReadAccess access;
        const bool opened = access.Open();
        const bool unsafe_open = access.Unsafe();
        if (!opened || unsafe_open || !access.Restore()) {
            std::cerr << "FAILED: an open check is not unsafe\n" << logs;
            ++failures;
        }
    }
    reset(memory);
    fail_open_store = true;
    fail_word_restore = true;
    {
        installer::NandReadAccess access;
        access.Open();
        if (!access.Unsafe() || access.Restore()) {
            std::cerr << "FAILED: a failed rollback makes the access unsafe at once\n" << logs;
            ++failures;
        }
    }
    reset(memory);
    std::memset(memory, 0, kMemorySize);
    fail_protection_restore = 1;
    {
        installer::NandReadAccess access;
        access.Open();
        if (!access.Unsafe() || access.Restore()) {
            std::cerr << "FAILED: an unrestored protection register is unsafe at once\n" << logs;
            ++failures;
        }
    }

    // The search: all three slots through one opening, put back once.
    constexpr int kSlots[] = {249, 250, 251};
    reset(memory);
    {
        riftwii::D2xInfo infos[3];
        bool any_read = false;
        if (!installer::SearchCios(kSlots, 3, infos, any_read) || !any_read || !infos[0].d2x || !infos[1].d2x ||
            !infos[2].d2x || opening_stores != 1 || permission_word != kOriginalWord ||
            protection != kOriginalProtection) {
            std::cerr << "FAILED: a normal search reads all three and restores\n" << logs;
            ++failures;
        }
    }
    // A failed rollback stops the search after slot 249: its one refused open
    // is the only one (Open() fails, so there is no retry), and 250 and 251
    // are never read. The caller must then not reload IOS.
    reset(memory);
    fail_open_store = true;
    fail_word_restore = true;
    {
        riftwii::D2xInfo infos[3];
        bool any_read = false;
        if (installer::SearchCios(kSlots, 3, infos, any_read) || open_calls != 1 || any_read || infos[1].d2x ||
            infos[2].d2x) {
            std::cerr << "FAILED: the search stops at a failed rollback\n" << logs;
            ++failures;
        }
    }

    // A failed final restore after all three slots also forbids an IOS
    // reload: SearchCios must report it even though every slot was read.
    reset(memory);
    fail_word_restore = true;
    {
        riftwii::D2xInfo infos[3];
        bool any_read = false;
        if (installer::SearchCios(kSlots, 3, infos, any_read) || !any_read || !infos[2].d2x ||
            logs.find("could not be restored after reading the cIOS information") == std::string::npos) {
            std::cerr << "FAILED: a failed final restore after every slot is reported\n" << logs;
            ++failures;
        }
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    VirtualFree(memory, 0, MEM_RELEASE);
#elif defined(__linux__)
    munmap(memory, kMemorySize);
#endif
    if (failures == 0) std::cout << "installer_cios: all passed\n";
    return failures == 0 ? 0 : 1;
}
