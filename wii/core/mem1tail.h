/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The last thing in RiftWii's .bss (Makefile.wii links mem1tail.o after
 * every library), so that it, not libogc's start-up data, spans
 * 0x81000000.
 *
 * libogc's SYS_Init, which crt0 runs before main, boots the DSP by
 * copying 128 bytes of DSP code to 0x81000000 (the address the DSP
 * starts from), and copies the old bytes back afterwards. On a real Wii
 * that code was still there when libogc next used the memory (Dolphin
 * never shows it). 2611-239 died on every console: about 3 KiB of new
 * code moved libogc's list of reset handlers (sys_reset_func_queue) from
 * just under the line to 0x81000014, and SYS_RegisterResetFunc then
 * followed 0x029F0037, the DSP code's sixth word, as a pointer (a DSI).
 * In 236 the unused end of libogc's IPC heaps happened to be there.
 *
 * A thread stack needs no starting contents, and this one's thread (Home's
 * drive scan, wii/menu/homescan.cpp) starts long after SYS_Init, so
 * whatever is left there is overwritten by the stack's own use.
 * tools/check_mem1_line.py fails the build when 0x81000000 is not well
 * inside it. */
#ifndef RIFTWII_WII_MEM1TAIL_H
#define RIFTWII_WII_MEM1TAIL_H

/* 256 KiB, though the scan itself needs far less: the stack is sized so it
 * still spans the line. Moving the six in-game pad builds out of the DOL
 * into the pads plugin shrank RiftWii, and with it everything before this
 * stack, so at 128 KiB its end fell about 92 KiB short of 0x81000000.
 * Pinning the stack to the line in the linker script, so it stops
 * depending on the DOL's size, is planned separately. */
#define RIFTWII_HOME_SCAN_STACK_BYTES (256 * 1024)

#ifdef __cplusplus
extern "C" {
#endif

extern unsigned char riftwii_home_scan_stack[RIFTWII_HOME_SCAN_STACK_BYTES];

#ifdef __cplusplus
}
#endif

#endif
