/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* RiftWii catches no C++ exceptions. A throw from a prebuilt library must
 * still reach its existing terminate handler: libgcc first needs the
 * records for its own unwind initialization before it can report that
 * the caller has no frame. Keep those records and crtbegin/crtend's
 * registration marker and terminator; discard the other libraries' tables.
 * The Wii and in-game crash reporters walk the PowerPC stack chain instead.
 * See docs/MEMORY.md for the checked devkitPPC paths and measurements. */
SECTIONS
{
    /DISCARD/ :
    {
        *(EXCLUDE_FILE (*crtbegin.o *crtend.o *unwind*.o) .eh_frame .eh_frame.*)
        *(.eh_frame_hdr .gcc_except_table .gcc_except_table.*)
    }
}
