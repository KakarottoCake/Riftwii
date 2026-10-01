# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-FileCopyrightText: USB Loader GX contributors <https://github.com/wiidev/usbloadergx>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Cheats in a game that starts another executable (a second DOL, as
# collections and some menus do). The game's DOL loader ends with
# "sync; isync; mtctr r31; bctr"; install_cheats (wii/boot.cpp) turns that
# bctr into a branch here. The new DOL is in memory by then and the game
# has flushed the caches for it. This stub, copied to kDolSwitchStub
# (0x80001000, between the exception vectors the OS uses), hooks the code
# handler into it the way install_cheats hooked the first one and points
# the new DOL's own loader back here, then jumps to the new entry point
# (still in ctr) with every register as the game left it.
#
# The idea is USB Loader GX's (its multidol hook, patches/patchcode.c);
# this code is RiftWii's own.
#
# Layout (offsets from the start; install_cheats fills the words):
#   0x00  b code
#   0x04  the code handler's entry
#   0x08  the hook's four instructions (riftwii/codehook.hpp)
#   0x18  first word looked at
#   0x1C  where the search stops (16 bytes before the end of MEM1)
# A hook is the first blr within 0x1000 bytes after its four instructions,
# unless a branch to the handler comes first (the first DOL's hook, still
# in memory). Every match is hooked: what is left of the old DOL can take
# a hook as harmlessly as it took the first.

    .section .rodata.dolswitch, "a"
    .balign 4
    .globl riftwii_dolswitch_stub
    .globl riftwii_dolswitch_stub_end
riftwii_dolswitch_stub:
    b       code
    .long   0               # 0x04 handler entry
    .long   0, 0, 0, 0      # 0x08 hook pattern
    .long   0               # 0x18 search start
    .long   0               # 0x1C search end
code:
    stwu    1, -0x90(1)
    stw     0, 0x08(1)
    mfcr    0
    stw     0, 0x0C(1)
    stmw    3, 0x10(1)      # r3-r31: 116 bytes, to 0x84

    lis     12, 0x8000
    ori     12, 12, 0x1000  # this stub
    lwz     11, 0x04(12)    # handler entry
    lwz     4, 0x08(12)
    lwz     5, 0x0C(12)
    lwz     6, 0x10(12)
    lwz     7, 0x14(12)
    lwz     3, 0x18(12)
    lwz     10, 0x1C(12)
    lis     9, 0x4E80
    ori     9, 9, 0x0020    # blr
    lis     27, 0x7C00
    ori     27, 27, 0x04AC  # sync
    lis     26, 0x4C00
    ori     26, 26, 0x012C  # isync
    lis     25, 0x7FE9
    ori     25, 25, 0x03A6  # mtctr r31
    lis     24, 0x4E80
    ori     24, 24, 0x0420  # bctr

look:
    cmplw   3, 10
    bge     done
    lwz     8, 0(3)
    cmplw   8, 4
    bne     dol_jump
    lwz     8, 4(3)
    cmplw   8, 5
    bne     dol_jump
    lwz     8, 8(3)
    cmplw   8, 6
    bne     dol_jump
    lwz     8, 12(3)
    cmplw   8, 7
    bne     dol_jump
    addi    30, 3, 16
find_blr:
    subf    29, 3, 30
    cmplwi  29, 0x1000
    bge     dol_jump        # no blr close enough
    subf    28, 30, 11      # b from r30 to the handler
    rlwinm  28, 28, 0, 6, 29
    oris    28, 28, 0x4800
    lwz     29, 0(30)
    cmplw   29, 28
    beq     dol_jump        # already calls the handler
    cmplw   29, 9
    beq     hook
    addi    30, 30, 4
    b       find_blr
hook:
    stw     28, 0(30)
    dcbst   0, 30
    sync
    icbi    0, 30

dol_jump:
    lwz     8, 0(3)
    cmplw   8, 27
    bne     next
    lwz     8, 4(3)
    cmplw   8, 26
    bne     next
    lwz     8, 8(3)
    cmplw   8, 25
    bne     next
    lwz     8, 12(3)
    cmplw   8, 24
    bne     next
    addi    30, 3, 12       # its bctr: a branch here instead
    subf    28, 30, 12
    rlwinm  28, 28, 0, 6, 29
    oris    28, 28, 0x4800
    stw     28, 0(30)
    dcbst   0, 30
    sync
    icbi    0, 30

next:
    addi    3, 3, 4
    b       look

done:
    sync
    isync
    lmw     3, 0x10(1)
    lwz     0, 0x0C(1)
    mtcr    0
    lwz     0, 0x08(1)
    addi    1, 1, 0x90
    bctr
    .balign 4
riftwii_dolswitch_stub_end:
