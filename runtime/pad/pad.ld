/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The GameCube adapter blob: header and trampolines first, then the C
 * code. Position independent: linked at two RT_BASE
 * values and compared; .rodata/.data/.bss must stay empty. */
OUTPUT_FORMAT("elf32-powerpc")
OUTPUT_ARCH(powerpc)
ENTRY(_pad_blob_start)

SECTIONS
{
    . = RT_BASE;
    .text : {
        KEEP(*(.text.pad_entry))
        *(.text .text.*)
        . = ALIGN(32);
        _pad_blob_end = .;
    }
    .rodata : { *(.rodata .rodata.* .sdata2 .sbss2) }
    .data : { *(.data .data.* .sdata .sdata.*) }
    .bss : { *(.bss .bss.* .sbss .sbss.* COMMON) }
    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note.*) *(.gnu.attributes) }
}
