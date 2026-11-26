/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Why these are in a file of their own, linked last: mem1tail.h. It must
 * reference nothing, as every library has been searched before it. */
#include "mem1tail.h"

/* The Wii Menu music thread's stack, which must lie after the scan stack
 * (mem1tail.h says why). GCC lays this file's buffers out in the reverse
 * of their order here, so it comes first; tools/check_mem1_line.py fails
 * the build if the music stack ever ends up before the scan stack. */
__attribute__((aligned(32))) unsigned char riftwii_music_stack[RIFTWII_MUSIC_STACK_BYTES];

/* The scan reads FAT and NTFS volumes, parses pack XMLs (pugixml) and
 * looks for network packs: the menu thread's own depth, so a stack as
 * big. */
__attribute__((aligned(32))) unsigned char riftwii_home_scan_stack[RIFTWII_HOME_SCAN_STACK_BYTES];
