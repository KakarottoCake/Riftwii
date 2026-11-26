/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Added to libogc's linker script (Makefile.wii, -T with INSERT): the
 * zero-filled buffers of the prebuilt Bluetooth stack (libbte, 290 KiB of
 * packet pools) and of the Wii Remote library (libwiiuse) go to MEM2, to
 * mem::kLibraryBss (wii/core/memlimits.hpp), instead of MEM1. IOS reads
 * and writes MEM2 as it does MEM1, and both libraries flush the cache
 * around what they hand it.
 *
 * Their segment is not a loadable one (PT_NULL), so elf2dol leaves it out
 * of the DOL (it reads only PT_LOAD segments, and would otherwise take this
 * one for the program's own .bss) and no loader clears it: restart.cpp's
 * __wrap___CheckARGV zeroes it at every start, before anything runs. The location counter is put
 * back afterwards, so .sbss, .bss and __Arena1Lo stay where they were.
 */
PHDRS {
	mem2bss PT_NULL;
}

SECTIONS {
	__riftwii_mem1_resume = .;
	.mem2bss 0x90829000 (NOLOAD) : {
		*libbte.a:*(.bss .bss.* COMMON)
		*libwiiuse.a:*(.bss .bss.* COMMON)
		. = ALIGN(32);
	} :mem2bss
	__riftwii_mem2bss_start = ADDR(.mem2bss);
	__riftwii_mem2bss_end = ADDR(.mem2bss) + SIZEOF(.mem2bss);
	. = __riftwii_mem1_resume;
} INSERT BEFORE .sbss;

/* mem::kLibraryBss and kLibraryBssBytes: the start-up stash's copy follows. */
ASSERT(__riftwii_mem2bss_start == 0x90829000 && __riftwii_mem2bss_end <= 0x90881000,
       "libbte's and libwiiuse's buffers no longer fit mem::kLibraryBss (wii/core/memlimits.hpp)");
