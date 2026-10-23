// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdint.h>
#include <stdio.h>

// What the loader defines before including its check.
typedef uint32_t u32;
#define MEM1_HI 0x81700000u
#define LOADER_END 0x80004000u
#include "../channel/loader/dolvalidate.h"

static int failures;

static void expect_entry(DolHeader* header, uint32_t entry, int expected) {
    header->entry = entry;
    const int actual = dol_valid(header, 0x200);
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

    // The entry stays in section 7 below, so only a range decides.
    // BSS: from the loader's end up to MEM1_HI, and not wrapping.
    expect_entry(&header, 0x81000100, 1);
    header.bss_address = LOADER_END;
    expect_entry(&header, 0x81000100, 1);
    header.bss_address = MEM1_HI - 0x40;
    expect_entry(&header, 0x81000100, 1);
    header.bss_address = LOADER_END - 0x20;
    expect_entry(&header, 0x81000100, 0);
    header.bss_address = MEM1_HI - 0x20;
    expect_entry(&header, 0x81000100, 0);
    header.bss_address = 0xffffffe0;
    expect_entry(&header, 0x81000100, 0);
    header.bss_address = 0x81000200;

    // Another section: the same address range, and its bytes in the file.
    header.offset[1] = 0x1c0;
    header.address[1] = 0x81000300;
    header.size[1] = 0x20;
    expect_entry(&header, 0x81000100, 1);
    header.address[1] = MEM1_HI - 0x20;
    expect_entry(&header, 0x81000100, 1);
    header.size[1] = 0x21;
    expect_entry(&header, 0x81000100, 0);
    header.address[1] = 0xffffffe0;
    header.size[1] = 0x40;
    expect_entry(&header, 0x81000100, 0);
    header.address[1] = 0x81000300;
    header.size[1] = 0x40;
    expect_entry(&header, 0x81000100, 1);
    header.size[1] = 0x41;
    expect_entry(&header, 0x81000100, 0);
    header.offset[1] = 0x201;
    header.size[1] = 0x20;
    expect_entry(&header, 0x81000100, 0);
    header.size[1] = 0;

    // A section may start right at the loader's end, not a byte under it.
    for (int under = 0; under <= 1; ++under) {
        DolHeader low = {0};
        low.offset[0] = 0x100;
        low.address[0] = LOADER_END - (u32)under;
        low.size[0] = 0x20;
        low.entry = low.address[0];
        if (dol_valid(&low, 0x200) != !under) {
            printf("FAILED: a section at %08x was %s.\n", (unsigned)low.address[0],
                   under ? "accepted" : "refused");
            ++failures;
        }
    }
    return failures ? 1 : 0;
}
