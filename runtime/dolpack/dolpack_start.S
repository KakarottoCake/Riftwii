/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The packed DOL's entry (dolpack.h). It starts as a libogc program does,
 * so the Homebrew Channel and RiftWii's channel fill its "_arg" block with
 * the command line; then come the fields tools/pack_dol.py reads and
 * writes, then a stack and dolpack_main. */
    .section .start, "ax", @progbits
    .globl _dolpack_start
_dolpack_start:
    b 1f
    .long 0x5f617267            /* "_arg": a loader writes struct __argv after it */
    .globl dolpack_argv
dolpack_argv:
    .long 0, 0, 0, 0, 0, 0      /* argvMagic, commandLine, length, argc, argv, endARGV */
    .long 0x52575043            /* "RWPC": DOLPACK_FIELDS_MAGIC */
    .globl dolpack_table_address
dolpack_table_address:
    .long 0                     /* the table, written by the packer */
    .long _dolpack_start        /* where the stub starts */
    .long __dolpack_image_end   /* where its file bytes end */
    .long __dolpack_end         /* where its BSS and stack end */
1:
    /* Interrupts off: nothing of the loader's may run while the sections
     * are written over memory. */
    mfmsr 3
    rlwinm 3, 3, 0, 17, 15
    mtmsr 3
    isync
    lis 1, __dolpack_stack_top@h
    ori 1, 1, __dolpack_stack_top@l
    li 0, 0
    stwu 0, -16(1)
    bl dolpack_main
2:
    b 2b
    /* Never run: Dolphin tells a Wii DOL from a GameCube one by a write to
     * HID4, which only the Wii's CPU has, and the stub has none of its own.
     * Without it Dolphin started the packed DOL as a GameCube program,
     * with no IOS, and RiftWii stopped before its first log line. */
    mtspr 1011, 3
