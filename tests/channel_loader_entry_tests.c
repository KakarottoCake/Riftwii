// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdio.h>
#include "../channel/loader/dolvalidate.h"

static int failures;

static void expect_entry(DolHeader* header, uint32_t entry, int expected) {
    header->entry = entry;
    const int actual = dol_valid(header, 0x200, 0x80004000);
    if (actual != expected) {
        printf("FAILED: loader entry %08x was %s, expected %s.\n", entry,
               actual ? "accepted" : "refused", expected ? "accepted" : "refused");
        ++failures;
    }
}

int main(void) {
    DolHeader header = {0};
    expect_entry(&header, 0x81000000, 0);

    header.offset[0] = 0x100;
    header.address[0] = 0x81000000;
    header.size[0] = 0x20;
    expect_entry(&header, 0x81000000, 1);
    expect_entry(&header, 0x8100001c, 1);
    expect_entry(&header, 0x8100001f, 1);
    expect_entry(&header, 0x80fffffc, 0);
    expect_entry(&header, 0x81000020, 0);
    expect_entry(&header, 0x81000100, 0);

    // Like the common validator, the loader accepts any initialized
    // section, including section 7 (the first data section).
    header.offset[7] = 0x120;
    header.address[7] = 0x81000100;
    header.size[7] = 0x20;
    expect_entry(&header, 0x81000100, 1);
    expect_entry(&header, 0x8100011f, 1);
    expect_entry(&header, 0x81000120, 0);
    expect_entry(&header, 0x81000080, 0);

    header.bss_address = 0x81000200;
    header.bss_size = 0x40;
    expect_entry(&header, 0x81000200, 0);
    return failures ? 1 : 0;
}
