// SPDX-License-Identifier: GPL-3.0-or-later
//
// Starting another program (a DOL) from this one, as the Homebrew Channel
// does: the RiftWii channel starts RiftWii, RiftWii starts the channel
// installer, and the installer goes back to RiftWii.
#pragma once

#include <gccore.h>

#ifdef __cplusplus
extern "C" {
#endif

// Reads the file whole into memory from `alloc` (32-byte aligned), which
// must not be where the program lands. NULL if it cannot.
u8* dolboot_read(const char* path, u32* size, void* (*alloc)(u32 size));

// Whether the DOL's sections fit its file and land in MEM1, clear of this
// program and of the file's own copy.
bool dolboot_valid(const u8* dol, u32 size);

// Shuts libogc down, copies the program into place and starts it, with
// `argv0` as its argv[0]. The caller has closed its files first. Does
// not return.
void dolboot_run(const u8* dol, const char* argv0) __attribute__((noreturn));

// Puts at the system call vector (0x80000C00) the handler the Wii's system
// software leaves for a program it starts: `sc` returns at once, after
// toggling HID0's bit 28 around a sync (the SDK's and older libogc's cache
// routines end with `sc` to drain the write buffer this way). libogc
// leaves its own exception stub there instead, which jumps into this
// program's handler, gone once the next program is in memory: a program
// that runs `sc` before installing handlers of its own (CTGP-R 1.03's
// launcher, which skips its libogc's startup) would hang on a black
// screen. Programs and games that install their own replace it.
void dolboot_system_call_vector(void);

#ifdef __cplusplus
}
#endif
