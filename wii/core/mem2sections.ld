/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Added to libogc's linker script (Makefile.wii, -T with INSERT): what
 * RiftWii keeps in MEM2 instead of MEM1, next to the main thread's stack
 * (wii/core/memlimits.hpp has the layout).
 *
 * .mem2bss: the zero-filled buffers of the prebuilt Bluetooth stack (libbte, 290 KiB of
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
	mem2rodata PT_LOAD;
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

/*
 * .mem2rodata: the program's read-only data, run from mem::kReadOnlyData.
 * It cannot be loaded there: the packed DOL's stub runs before libogc maps
 * MEM2. tools/dol_stash.py moves its DOL section to load just above .bss
 * instead (the start-up stash follows it), and restart.cpp's
 * __wrap___CheckARGV copies it up first thing, before anything reads it;
 * those MEM1 bytes then become the heap's. Its first two words, a marker
 * and its size, tell a fresh start from a warm restart, which finds heap
 * leftovers there and the MEM2 copy still whole. libasnd's stays in MEM1:
 * the DSP loads its mixer from there.
 */
SECTIONS {
	__riftwii_mem1_resume2 = .;
	.mem2rodata 0x90881000 : {
		__riftwii_mem2rodata_start = .;
		LONG(0x52575244)
		LONG(__riftwii_mem2rodata_end - __riftwii_mem2rodata_start)
		KEEP( *(.rodata.__sys_versioninfo) )
		*(EXCLUDE_FILE(*libasnd.a:*) .rodata EXCLUDE_FILE(*libasnd.a:*) .rodata.* EXCLUDE_FILE(*libasnd.a:*) .gnu.linkonce.r.*)
		. = ALIGN(32);
		__riftwii_mem2rodata_end = .;
	} :mem2rodata
	. = __riftwii_mem1_resume2;
} INSERT BEFORE .rodata;

/* mem::kLibraryBss and kReadOnlyData, with their sizes: the start-up
 * stash's copy follows. */
ASSERT(__riftwii_mem2bss_start == 0x90829000 && __riftwii_mem2bss_end <= 0x90881000,
       "libbte's and libwiiuse's buffers no longer fit mem::kLibraryBss (wii/core/memlimits.hpp)");
ASSERT(__riftwii_mem2rodata_start == 0x90881000 && __riftwii_mem2rodata_end <= 0x90951000,
       "the read-only data no longer fits mem::kReadOnlyData (wii/core/memlimits.hpp)");
