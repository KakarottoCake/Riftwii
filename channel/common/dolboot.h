// SPDX-FileCopyrightText: 2026 RiftWii contributors
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
// must not be where the program lands. NULL if it cannot; memory taken for
// a read that came up short goes back through `release`.
u8* dolboot_read(const char* path, u32* size, void* (*alloc)(u32 size), void (*release)(void* data));

// Whether the DOL's sections fit its file and land in MEM1's free part,
// clear of the file's own copy. A DOL that lands on this program is
// started through the far copy (dolboot.c), which then needs its room at
// the top of MEM2's free part.
bool dolboot_valid(const u8* dol, u32 size);

// Shuts libogc down, copies the program into place and starts it, with
// `argv0` as its argv[0]. The caller has closed its files first. Does
// not return.
void dolboot_run(const u8* dol, const char* argv0) __attribute__((noreturn));

// Where dolboot_run_line will put the far copy for `dol` with an
// argument line of `length` bytes (in MEM2), or 0 when the DOL lands
// clear of this program and is copied by it, as before. For the log.
u32 dolboot_far_address(const u8* dol, u32 length);

// For a DOL dolboot_valid accepted: whether dolboot_run_line can hand it
// an argument line of `length` bytes. False only for a DOL that reads
// one and has no place for it: past MEM1's free part, or on this program
// without the far copy's room. Such a DOL would start without its
// arguments, so it is refused instead.
bool dolboot_line_fits(const u8* dol, u32 length);

void dolboot_run_line(const u8* dol, const char* line, u32 length) __attribute__((noreturn));

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
