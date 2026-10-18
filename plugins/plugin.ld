/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Every plugin's link (Makefile.plugin): code, constants, then .bss, from
 * RWP_BASE. The build links each plugin at two bases and
 * tools/make_plugin.py checks that the relocation list turns one into the
 * other; it also refuses any allocated section not named here, and a
 * non-empty .data (docs/PLUGINS.md, "Format").
 *
 * Static constructors and destructors, and exception and unwind tables,
 * are kept (KEEP, so --gc-sections cannot drop them) in two sections of
 * their own that make_plugin.py refuses with a message naming them.
 * Discarded instead, the plugin would link and then run with a global
 * never constructed, or terminate at its first throw. */
OUTPUT_FORMAT("elf32-powerpc")
OUTPUT_ARCH(powerpc)
ENTRY(rwp_init)

SECTIONS
{
    . = RWP_BASE;
    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.* .sdata2 .sdata2.* .sbss2 .sbss2.*) }
    .data : { *(.data .data.* .sdata .sdata.*) }
    .rwp_constructors : {
        KEEP(*(.ctors .ctors.* .dtors .dtors.* .init_array .init_array.* .fini_array .fini_array.*))
    }
    .rwp_exceptions : {
        KEEP(*(EXCLUDE_FILE(*libgcc.a:*) .eh_frame EXCLUDE_FILE(*libgcc.a:*) .eh_frame.*))
        KEEP(*(.gcc_except_table .gcc_except_table.*))
    }
    . = ALIGN(32);
    .bss : { *(.bss .bss.* .sbss .sbss.* COMMON) }
    /* libgcc's own unwind entries describe only its helpers (the -Os
     * register save and restore, crtresxgpr.o): without an unwinder they
     * are dead bytes, and every plugin links them. */
    /DISCARD/ : { *libgcc.a:*(.eh_frame .eh_frame.*) *(.comment) *(.note.*) *(.gnu.attributes) }
}
