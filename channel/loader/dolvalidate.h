// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The loader's check of the RiftWii DOL, in its own file so a host test
// (tests/channel_loader_entry_tests.c) runs this very code. The includer
// defines u32, MEM1_HI and LOADER_END (the loader's linked end): the
// loader passes (u32)__loader_end, so its build is byte for byte what it
// was with the function written inline.
#pragma once

typedef struct {
    u32 offset[18];
    u32 address[18];
    u32 size[18];
    u32 bss_address;
    u32 bss_size;
    u32 entry;
} DolHeader;

// Every section in MEM1 above this loader, and inside the file.
static int dol_valid(const DolHeader* h, u32 size) {
    const u32 self_hi = LOADER_END;
    int entry_loaded = 0;
    for (int i = 0; i < 18; ++i) {
        if (!h->size[i]) continue;
        const u32 lo = h->address[i], hi = lo + h->size[i];
        if (h->offset[i] > size || h->size[i] > size - h->offset[i]) return 0;
        if (lo < self_hi || hi > MEM1_HI || hi < lo) return 0;
        if (h->entry >= lo && h->entry < hi) entry_loaded = 1;
    }
    const u32 lo = h->bss_address, hi = lo + h->bss_size;
    if (h->bss_size && (lo < self_hi || hi > MEM1_HI || hi < lo)) return 0;
    // Refuse an entry in a hole instead of jumping into stale MEM1 data.
    return entry_loaded;
}
