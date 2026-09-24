/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The RVZ runtime blob (rt.ld plus Zstandard): its constant tables are
 * part of the blob, and the loader relocates the references to them
 * (tools/rtreloc.py). .data/.bss must stay empty. */
OUTPUT_FORMAT("elf32-powerpc")
OUTPUT_ARCH(powerpc)
ENTRY(_rt_blob_start)

SECTIONS
{
    . = RT_BASE;
    .text : {
        KEEP(*(.text.rt_entry))
        *(.text .text.*)
        . = ALIGN(8);
        *(.rodata .rodata.* .sdata2 .sbss2)
        . = ALIGN(32);
        _rt_blob_end = .;
    }
    .data : { *(.data .data.* .sdata .sdata.*) }
    .bss : { *(.bss .bss.* .sbss .sbss.* COMMON) }
    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note.*) *(.gnu.attributes) }
}
