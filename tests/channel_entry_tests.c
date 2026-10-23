// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Test the real DOL validator without copying code or starting PowerPC code.
#include <stdio.h>
#include <string.h>
#include "../channel/common/dolboot.c"

u8 __app_start[1];
u8 __bss_end[1];

static int failures;

static void expect_entry(DolHeader* header, u32 entry, bool expected) {
    header->entry = entry;
    const bool actual = dolboot_valid((const u8*)header, 0x200);
    if (actual != expected) {
        printf("FAILED: entry %08x was %s, expected %s.\n", entry,
               actual ? "accepted" : "refused", expected ? "accepted" : "refused");
        ++failures;
    }
}

int main(void) {
    DolHeader header = {0};
    expect_entry(&header, 0x81000000, false);

    header.offset[0] = 0x100;
    header.address[0] = 0x81000000;
    header.size[0] = 0x20;
    expect_entry(&header, 0x81000000, true);
    expect_entry(&header, 0x8100001c, true);
    expect_entry(&header, 0x80fffffc, false);
    expect_entry(&header, 0x81000020, false);
    expect_entry(&header, 0x81000100, false);

    // An executable can put its entry in any initialized section. Do not
    // impose a new text-only restriction on homebrew DOLs.
    header.offset[7] = 0x120;
    header.address[7] = 0x81000100;
    header.size[7] = 0x20;
    expect_entry(&header, 0x81000100, true);
    expect_entry(&header, 0x81000120, false);

    header.bss_address = 0x81000200;
    header.bss_size = 0x40;
    expect_entry(&header, 0x81000200, false);
    return failures ? 1 : 0;
}
