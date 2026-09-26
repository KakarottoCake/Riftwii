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

#ifdef __cplusplus
}
#endif
