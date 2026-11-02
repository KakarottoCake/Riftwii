# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The last step of starting a DOL that lands where the running program
# is (dolboot.c, "the far copy"): WiiSX and Wii64 fill MEM1 up to about
# 0x80bc0000, through RiftWii's own sections at 0x80a00000. dolboot.c
# copies these instructions, and the plan they follow, to memory that
# neither program uses (the top of MEM2's free part), and jumps there.
# From then on nothing of the old program is needed, so the copies may
# overwrite it.
#
# Position independent: only relative branches, and every address comes
# from the plan. No stack of the old program is used.
#
# In:  r3 = the plan: a count, then that many (to, from, bytes) triples;
#           `from` 0 clears `bytes` at `to` instead of copying
#      r4 = the entry point
#      r5 = the top of a stack of its own, for the program's first
#           instructions (libogc's start sets its own at once)
# Interrupts are off. Does not return.

	.section .rodata.dolfar, "a"
	.balign	32
	.globl	dolfar_code
dolfar_code:
	mr	1, 5
	li	0, 0
	stwu	0, -16(1)		# an empty back chain
	lwz	6, 0(3)			# copies left
	addi	3, 3, 4

next:
	cmpwi	6, 0
	beq	start
	lwz	7, 0(3)			# to
	lwz	8, 4(3)			# from, or 0
	lwz	9, 8(3)			# bytes
	addi	3, 3, 12
	addi	6, 6, -1
	cmpwi	9, 0
	beq	next
	mtctr	9
	addi	10, 7, -1
	cmpwi	8, 0
	beq	clear
	addi	8, 8, -1
copy:
	lbzu	11, 1(8)
	stbu	11, 1(10)
	bdnz	copy
	b	flush
clear:
	li	11, 0
clear_byte:
	stbu	11, 1(10)
	bdnz	clear_byte

	# Every cache line of [to, to + bytes) out of the data cache, then
	# out of the instruction cache: the copy may be code, and the old
	# program's code was cached at the same addresses.
flush:
	add	12, 7, 9		# the end
	rlwinm	10, 7, 0, 0, 26		# the first line
flush_line:
	dcbf	0, 10
	addi	10, 10, 32
	cmplw	10, 12
	blt	flush_line
	sync
	rlwinm	10, 7, 0, 0, 26
invalidate_line:
	icbi	0, 10
	addi	10, 10, 32
	cmplw	10, 12
	blt	invalidate_line
	sync
	isync
	b	next

start:
	mtctr	4
	bctr

	.globl	dolfar_code_end
dolfar_code_end:

	# dolboot.c leaves the routine this much room before its plan
	# (FAR_CODE_MAX).
	.if	dolfar_code_end - dolfar_code > 0x100
	.error	"dolfar.S no longer fits before its plan (FAR_CODE_MAX in dolboot.c)"
	.endif
