/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Every plugin's link (Makefile.plugin): code, constants, then .bss, from
 * RWP_BASE. The build links each plugin at two bases and
 * tools/make_plugin.py checks that the relocation list turns one into the
 * other; it also refuses any allocated section not named here, and a
 * non-empty .data (docs/PLUGINS.md, "Format"). */
OUTPUT_FORMAT("elf32-powerpc")
OUTPUT_ARCH(powerpc)
ENTRY(rwp_init)

SECTIONS
{
    . = RWP_BASE;
    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.* .sdata2 .sdata2.* .sbss2 .sbss2.*) }
    .data : { *(.data .data.* .sdata .sdata.*) }
    . = ALIGN(32);
    .bss : { *(.bss .bss.* .sbss .sbss.* COMMON) }
    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note.*) *(.gnu.attributes) }
}
