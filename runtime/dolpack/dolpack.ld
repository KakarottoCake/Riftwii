/* SPDX-FileCopyrightText: 2026 RiftWii contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The packed DOL's stub (dolpack.h): one text section at DOLPACK_BASE,
 * then its BSS (the decoder's workspace) and a stack, all under
 * RiftWii's own sections at 0x80a00000.
 */
OUTPUT_FORMAT("elf32-powerpc")
OUTPUT_ARCH(powerpc:common)
ENTRY(_dolpack_start)

SECTIONS {
	. = 0x80900000;
	.text : { KEEP(*(.start)) *(.text .text.*) }
	.rodata : { *(.rodata .rodata.*) }
	.data : { *(.data .data.*) *(.sdata .sdata.*) . = ALIGN(32); }  /* DOL sections: 32-byte sizes */
	__dolpack_image_end = .;
	__dolpack_bss_start = .;
	.bss (NOLOAD) : { *(.sbss .sbss.*) *(.bss .bss.*) *(COMMON) . = ALIGN(32); }
	__dolpack_bss_end = .;
	. += 0x4000;
	. = ALIGN(32);
	__dolpack_stack_top = .;
	__dolpack_end = .;

	/DISCARD/ : { *(.comment) *(.eh_frame) *(.gnu.attributes) }
}
