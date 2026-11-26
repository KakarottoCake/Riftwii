/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Added to libogc's linker script (Makefile.wii, -T with INSERT): what
 * RiftWii keeps in MEM2 instead of MEM1, next to the main thread's stack
 * (wii/core/memlimits.hpp has the layout).
 *
 * .mem2bss: the zero-filled buffers of the prebuilt Bluetooth stack (libbte, 290 KiB of
 * packet pools), of the Wii Remote library (libwiiuse) and RiftWii's own
 * go to MEM2, after the read-only data (wii/core/memlimits.hpp), instead of
 * MEM1. IOS reads and writes MEM2 as it does MEM1 (the disc, SD and USB
 * bounce buffers too), and the code flushes the cache around what it hands
 * it. The other prebuilt libraries' stay in MEM1: libogc's and libasnd's
 * include buffers the DSP and the EXI bus read, and the GX FIFO stays as
 * the graphics processor reads it.
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
	mem2text PT_LOAD FLAGS(5);
}

SECTIONS {
	__riftwii_mem1_resume = .;
	.mem2bss __riftwii_mem2rodata_end (NOLOAD) : {
		*libbte.a:*(.bss .bss.* COMMON)
		*libwiiuse.a:*(.bss .bss.* COMMON)
		/* libogc's that only the CPU or IOS touch: the console's settings,
		 * the NAND boot record, the USB mouse thread's stack, a network
		 * buffer and the graphics registers' shadow copy. */
		*libogc.a:conf.o(.bss .bss.* COMMON)
		*libogc.a:wiilaunch.o(.bss .bss.* COMMON)
		*libogc.a:usbmouse.o(.bss .bss.* COMMON)
		*libogc.a:network_wii.o(.bss .bss.* COMMON)
		*libogc.a:gx.o(.bss .bss.* COMMON)
		/* The kernel's thread blocks and lock tables, the memory card's and
		 * the EXI bus's bookkeeping (not their DMA buffers), IPC's heap
		 * list; the C library's files and exit list. */
		*libogc.a:thread.o(.bss .bss.* COMMON)
		*libogc.a:lwpc_*.o(.bss .bss.* COMMON)
		*libogc.a:card.o(.bss .bss.* COMMON)
		*libogc.a:exi.o(.bss .bss.* COMMON)
		*libogc.a:ipc.o(.bss .bss.* COMMON)
		*libc.a:*(.bss .bss.* COMMON)
		*libsysbase.a:*(.bss .bss.* COMMON)
		*libstdc++.a:*(.bss .bss.* COMMON)
		/* RiftWii's own buffers but libwiigui's GX FIFO (video.o), the
		 * exception stack (mainstack.o) and what start-up runs before it
		 * is zeroed. */
		*(EXCLUDE_FILE(*.a:* video.o mainstack.o restart.o *crtbegin.o *crtend.o) .bss EXCLUDE_FILE(*.a:* video.o mainstack.o restart.o *crtbegin.o *crtend.o) .bss.* EXCLUDE_FILE(*.a:* video.o mainstack.o restart.o *crtbegin.o *crtend.o) COMMON)
		. = ALIGN(32);
	} :mem2bss
	__riftwii_mem2bss_start = ADDR(.mem2bss);
	__riftwii_mem2bss_end = ADDR(.mem2bss) + SIZEOF(.mem2bss);
	/* mem::ProgramAreaEnd(): the start-up stash's copy goes here. */
	__riftwii_mem2_end = ALIGN(__riftwii_mem2bss_end, 4096);
	. = __riftwii_mem1_resume;
} INSERT BEFORE .sbss;

/*
 * .mem2rodata: the program's read-only data, run from MEM2 right after the code.
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
	.mem2rodata __riftwii_mem2text_end : {
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

/*
 * .mem2text: the program's code, run from mem::kProgramArea, loaded and
 * copied up like .mem2rodata (tools/dol_stash.py puts it first above .bss,
 * the read-only data after it), with the same marker and size in front.
 * libogc maps MEM2 for instructions as for data (IBAT4), and its exception
 * entry turns translation back on before it calls a handler, so libogc,
 * the C library and libgcc run from MEM2 like the rest. What stays in MEM1:
 * - restart.o, whose __wrap___CheckARGV makes the copy, and libogc's
 *   cache.o, which it calls before the copy is there;
 * - everything crt0's .init calls (early_init.o, exception.o, interrupt.o,
 *   thread.o, system.o, memset, crtbegin's and crtend's code): ld can only
 *   put a long-branch stub inline in .init, where the call returns into
 *   the stub and runs the function again, forever;
 * - libogc's assembly (*_asm.o): the exception and interrupt entries and
 *   the thread switch, which run with translation off for a moment.
 * Calls between the two halves are beyond a branch's reach, and ld puts
 * long-branch stubs in between. restart.o is named alone: *restart.o would
 * keep osrestart.o too.
 */
SECTIONS {
	__riftwii_mem1_resume3 = .;
	.mem2text 0x90829000 : {
		__riftwii_mem2text_start = .;
		LONG(0x52575458)
		LONG(__riftwii_mem2text_end - __riftwii_mem2text_start)
		/* EXCLUDE_FILE holds for the one pattern after it, hence three. */
		*(EXCLUDE_FILE(*libogc.a:early_init.o *libogc.a:cache.o *libogc.a:*_asm.o *libogc.a:exception.o *libogc.a:interrupt.o *libogc.a:thread.o *libogc.a:system.o *libc.a:libc_a-memset.o *crtbegin.o *crtend.o restart.o) .text
		  EXCLUDE_FILE(*libogc.a:early_init.o *libogc.a:cache.o *libogc.a:*_asm.o *libogc.a:exception.o *libogc.a:interrupt.o *libogc.a:thread.o *libogc.a:system.o *libc.a:libc_a-memset.o *crtbegin.o *crtend.o restart.o) .text.*
		  EXCLUDE_FILE(*libogc.a:early_init.o *libogc.a:cache.o *libogc.a:*_asm.o *libogc.a:exception.o *libogc.a:interrupt.o *libogc.a:thread.o *libogc.a:system.o *libc.a:libc_a-memset.o *crtbegin.o *crtend.o restart.o) .gnu.linkonce.t.*)
		. = ALIGN(32);
		__riftwii_mem2text_end = .;
	} :mem2text
	. = __riftwii_mem1_resume3;
} INSERT BEFORE .text;

/* The three back to back from mem::kProgramArea, code first (in this
 * order, each section's address only refers back to one laid out before
 * it), with 2 MiB left for the start-up stash below 0x91000000, the end of
 * what a GameCube game uses before its jump. */
ASSERT(__riftwii_mem2text_start == 0x90829000,
       "the code no longer starts at mem::kProgramArea (wii/core/memlimits.hpp)");
ASSERT(__riftwii_mem2_end <= 0x90E00000,
       "the code, read-only data and buffers bound for MEM2 no longer leave room for the stash")
