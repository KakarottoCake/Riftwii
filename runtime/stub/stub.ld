/* SPDX-FileCopyrightText: 2026 RiftWii contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The return stub (include/riftwii/returnstub.h): its header and code at
 * 0x80001800, then its IPC buffers and a stack, all under RTSTUB_LIMIT.
 * The loader copies the image, not the buffers or the stack.
 */
OUTPUT_FORMAT("elf32-powerpc")
OUTPUT_ARCH(powerpc:common)
ENTRY(_rtstub_start)

SECTIONS {
	. = 0x80001800;
	.text : { KEEP(*(.start)) *(.text .text.*) }
	.rodata : { *(.rodata .rodata.*) }
	.data : { *(.data .data.*) *(.sdata .sdata.*) . = ALIGN(32); }
	__rtstub_image_end = .;
	.bss (NOLOAD) : { *(.sbss .sbss.*) *(.bss .bss.*) *(COMMON) . = ALIGN(32); }
	. += 0x200;
	. = ALIGN(32);
	__rtstub_stack_top = .;
	__rtstub_end = .;

	/DISCARD/ : { *(.comment) *(.eh_frame) *(.gnu.attributes) }
}

ASSERT(__rtstub_end <= 0x80002400, "the return stub and its stack must end under RTSTUB_LIMIT")
