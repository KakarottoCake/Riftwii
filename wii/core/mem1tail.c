/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Why this is alone in its file, and linked last: mem1tail.h. It must
 * reference nothing, as every library has been searched before it. */
#include "mem1tail.h"

/* The scan reads FAT and NTFS volumes, parses pack XMLs (pugixml) and
 * looks for network packs: the menu thread's own depth, so a stack as
 * big. */
__attribute__((aligned(32))) unsigned char riftwii_home_scan_stack[RIFTWII_HOME_SCAN_STACK_BYTES];
