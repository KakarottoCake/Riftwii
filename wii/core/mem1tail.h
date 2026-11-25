/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The last thing in RiftWii's .bss (Makefile.wii links mem1tail.o after
 * every library), so that it, not libogc's start-up data, spans
 * 0x81000000.
 *
 * On a real Wii something overwrites a few bytes just past 0x81000000
 * early in start-up, after libogc has set up its lists and before the
 * menu starts (what does it is not known yet; Dolphin never does it).
 * 2611-239 died on every console: about 3 KiB of new code moved libogc's
 * list of reset handlers (sys_reset_func_queue) from just under the line
 * to 0x81000014, and PAD_Init's SYS_RegisterResetFunc then followed a
 * garbage pointer (a DSI). In 236 the unused end of libogc's IPC heaps
 * happened to be there.
 *
 * A thread stack needs no starting contents, and this one's thread (Home's
 * drive scan, wii/menu/homescan.cpp) starts long after start-up, so
 * whatever was written there is overwritten by the stack's own use.
 * tools/check_mem1_line.py fails the build when 0x81000000 is not well
 * inside it. */
#ifndef RIFTWII_WII_MEM1TAIL_H
#define RIFTWII_WII_MEM1TAIL_H

#define RIFTWII_HOME_SCAN_STACK_BYTES (128 * 1024)

#ifdef __cplusplus
extern "C" {
#endif

extern unsigned char riftwii_home_scan_stack[RIFTWII_HOME_SCAN_STACK_BYTES];

#ifdef __cplusplus
}
#endif

#endif
