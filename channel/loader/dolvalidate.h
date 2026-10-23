// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdint.h>

typedef struct {
    uint32_t offset[18];
    uint32_t address[18];
    uint32_t size[18];
    uint32_t bss_address;
    uint32_t bss_size;
    uint32_t entry;
} DolHeader;

// Kept separate from the loader's PowerPC/IPC code so host tests exercise
// its actual validator. The caller supplies the loader's linked end address.
static int dol_valid(const DolHeader* header, uint32_t file_size, uint32_t loader_end) {
    const uint32_t mem1_end = 0x81700000u;
    int entry_loaded = 0;
    for (int i = 0; i < 18; ++i) {
        if (!header->size[i]) continue;
        const uint32_t section_start = header->address[i];
        const uint32_t section_end = section_start + header->size[i];
        if (header->offset[i] > file_size || header->size[i] > file_size - header->offset[i]) return 0;
        if (section_start < loader_end || section_end > mem1_end || section_end < section_start) return 0;
        if (header->entry >= section_start && header->entry < section_end) entry_loaded = 1;
    }
    const uint32_t bss_start = header->bss_address;
    const uint32_t bss_end = bss_start + header->bss_size;
    if (header->bss_size && (bss_start < loader_end || bss_end > mem1_end || bss_end < bss_start)) return 0;
    // Refuse an entry in a hole instead of jumping into stale MEM1 data.
    return entry_loaded;
}
