// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ES gives a title it starts hardware access only when the title's TMD asks
// for it (the access rights at TMD offset 0x1D8, "movs r2, #0xEC; lsls r2,
// r2, #1" in this sequence of ES's Thumb code), and an IOS's TMD does not.
// Writing 1 to the byte 25 bytes into the sequence makes that test pass for
// every title. This is the byte libruntimeiospatch's IosPatch_AHBPROT
// writes (USB Loader GX, WiiFlow); the code here is RiftWii's own.

#include "keepaccess.h"

#include <gccore.h>
#include <ogc/machine/processor.h>

int keepaccess_for_next_ios(void) {
    if (read32(0x0D800064) != 0xFFFFFFFF) return KEEPACCESS_NO_ACCESS;
    static const u8 kRights[] = {0x68, 0x5B, 0x22, 0xEC, 0x00, 0x52, 0x18, 0x9B,
                                 0x68, 0x1B, 0x46, 0x98, 0x07, 0xDB};
    // MEM2's protection off while IOS's code is written, then back.
    const u16 protection = read16(0x0D8B420A);
    write16(0x0D8B420A, 2);
    int patched = 0;
    for (u32 at = 0xD3400000; at + 26 <= 0xD4000000; at += 2) {
        volatile const u8* p = (volatile const u8*)at;
        int same = 1;
        for (unsigned i = 0; same && i < sizeof kRights; ++i) same = p[i] == kRights[i];
        if (!same) continue;
        // One aligned 32-bit store, not a byte: a lone byte store to MEM2
        // through its uncached mirror may not keep the bytes around it,
        // and these are IOS's own code (CTGP's launch on a Wii stopped
        // right after this, in the reload into the cIOS).
        const u32 word = (at + 25) & ~3u;
        const unsigned shift = (3 - ((at + 25) & 3)) * 8;
        volatile u32* w = (volatile u32*)word;
        *w = (*w & ~(0xFFu << shift)) | (0x01u << shift);
        ++patched;
    }
    write16(0x0D8B420A, protection);
    return patched;
}
