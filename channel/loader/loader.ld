/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The channel's boot program (channel/loader): the start at 0x80003400,
 * then the loader at 0x80004000, its data and a stack, all in the first
 * 64 KiB. The forwarder it starts is linked above (Makefile.channel).
 */
OUTPUT_FORMAT("elf32-powerpc")
OUTPUT_ARCH(powerpc:common)
ENTRY(vwii_entry)

PHDRS {
	start	PT_LOAD FLAGS(5);
	loader	PT_LOAD FLAGS(7);
}

SECTIONS {
	. = 0x80003400;
	.start : { KEEP(*(.start)) . = ALIGN(32); } :start

	. = 0x80004000;
	.text : { *(.text .text.*) } :loader
	.rodata : { *(.rodata .rodata.*) }
	.data : { *(.data .data.*) *(.sdata .sdata.*) . = ALIGN(32); }  /* DOL sections: 32-byte sizes */
	. = ALIGN(32);
	__bss_start = .;
	.bss : { *(.sbss .sbss.*) *(.bss .bss.*) *(COMMON) } :NONE
	. = ALIGN(32);
	__bss_end = .;
	. += 0x4000;
	__stack_top = .;
	__loader_end = .;

	/DISCARD/ : { *(.comment) *(.eh_frame) *(.gnu.attributes) }
}
