# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The last step of a start file (wii/launch/boot.cpp, game_start_file): parts of
# the game's executable that land on this loader's own memory (Madden NFL
# 08's maddens.dol reaches 0x80ACA620; the loader starts at 0x80A00000)
# cannot be written while the loader runs. boot.cpp puts this stub, a
# table and those bytes in the game's arena (unused until the game
# starts) and jumps here instead of to the game. It writes each piece in
# place (a zero source: zeros), flushes it for the CPU, then jumps to the
# game's entry. No stack, no return: what it overwrites may be the code
# that called it.
#
# r3: the table: u32 count, u32 entry, then count x {u32 dst, u32 src, u32 len}

    .section .rodata.startfile, "a"
    .balign 4
    .globl riftwii_startfile_stub
    .globl riftwii_startfile_stub_end
riftwii_startfile_stub:
    mfmsr   0
    rlwinm  0, 0, 0, 17, 15     # external interrupts off (EE, bit 16)
    mtmsr   0
    isync
    lwz     4, 0(3)             # pieces left
    lwz     5, 4(3)             # the game's entry
    addi    3, 3, 8
piece:
    cmpwi   4, 0
    beq     go
    lwz     6, 0(3)             # dst
    lwz     7, 4(3)             # src (0: zeros)
    lwz     8, 8(3)             # len
    addi    3, 3, 12
    mr      9, 6
    mr.     10, 8
    beq     next
byte:
    li      11, 0
    cmpwi   7, 0
    beq     put
    lbz     11, 0(7)
    addi    7, 7, 1
put:
    stb     11, 0(9)
    addi    9, 9, 1
    addic.  10, 10, -1
    bne     byte
    rlwinm  9, 6, 0, 0, 26      # dst rounded down to 32 bytes
    add     10, 6, 8            # its end
flush:
    dcbst   0, 9
    sync
    icbi    0, 9
    addi    9, 9, 32
    cmplw   9, 10
    blt     flush
next:
    addi    4, 4, -1
    b       piece
go:
    sync
    isync
    mtctr   5
    bctr
riftwii_startfile_stub_end:
