/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Resident runtime blob: header + trampoline first, then the C code. The
 * blob must be position independent, so the build links it twice at
 * different RT_BASE values and compares; .rodata/.data/.bss must stay
 * empty (rt_hook.c keeps all state in the context). */
OUTPUT_FORMAT("elf32-powerpc")
OUTPUT_ARCH(powerpc)
ENTRY(_rt_blob_start)

SECTIONS
{
    . = RT_BASE;
    .text : {
        KEEP(*(.text.rt_entry))
        *(.text .text.*)
        . = ALIGN(32);
        _rt_blob_end = .;
    }
    .rodata : { *(.rodata .rodata.* .sdata2 .sbss2) }
    .data : { *(.data .data.* .sdata .sdata.*) }
    .bss : { *(.bss .bss.* .sbss .sbss.* COMMON) }
    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note.*) *(.gnu.attributes) }
}
