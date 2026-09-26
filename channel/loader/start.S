# SPDX-License-Identifier: GPL-3.0-or-later
#
# The RiftWii channel's boot program starts here. A Wii and a vWii start
# a channel's boot program differently:
#
# - A Wii starts it at physical 0x3400 with address translation off,
#   whatever the DOL's entry field says: `_start` below.
# - A vWii loads it with a boot program of the Wii U's (BC-NAND), which
#   jumps to the DOL's entry field with translation on: `vwii_entry`.
#
# Both then set the CPU up as the Homebrew Channel does for a program
# (caches, BATs, segments, HID4) and go on to loader.c with translation
# on. The layout follows FIX94's vWii NAND loader and OpenDolBoot: a
# section at 0x80003400, and the loader itself small and at 0x80004000.

#define HID0	1008
#define HID4	1011

	.section .start, "ax", @progbits
	.globl	_start
_start:				# 0x80003400, real mode (a Wii)
	b	setup
	.long	0x52465457		# "RFTW": tools/make_channel.py checks for it

	.globl	vwii_entry
vwii_entry:			# 0x80003408, translation on (a vWii): the DOL's entry
	li	3, 0
	mtsrr1	3			# into setup with interrupts and translation off
	lis	3, setup@h
	ori	3, 3, setup@l
	clrlwi	3, 3, 2			# its physical address
	mtsrr0	3
	rfi

setup:
	# Caches off and invalidated, then on
	lis	3, 0x0011
	ori	3, 3, 0x0c64
	mtspr	HID0, 3
	isync
	li	4, 0x2000		# MSR: floating point on, translation off
	mtmsr	4
	isync
	ori	3, 3, 0xc000
	mtspr	HID0, 3
	isync

	# No BATs, then every segment invalid
	li	0, 0
	mtspr	528, 0
	mtspr	530, 0
	mtspr	532, 0
	mtspr	534, 0
	mtspr	536, 0
	mtspr	538, 0
	mtspr	540, 0
	mtspr	542, 0
	mtspr	560, 0
	mtspr	562, 0
	mtspr	564, 0
	mtspr	566, 0
	mtspr	568, 0
	mtspr	570, 0
	mtspr	572, 0
	mtspr	574, 0
	isync
	lis	0, 0x8000
	mtsr	0, 0
	mtsr	1, 0
	mtsr	2, 0
	mtsr	3, 0
	mtsr	4, 0
	mtsr	5, 0
	mtsr	6, 0
	mtsr	7, 0
	mtsr	8, 0
	mtsr	9, 0
	mtsr	10, 0
	mtsr	11, 0
	mtsr	12, 0
	mtsr	13, 0
	mtsr	14, 0
	mtsr	15, 0
	isync

	# BAT0: 0x80000000 = MEM1 (256 MB block), cached, code and data
	li	3, 0x0002
	lis	4, 0x8000
	ori	4, 4, 0x1fff
	mtspr	529, 3			# IBAT0L
	mtspr	528, 4			# IBAT0U
	mtspr	537, 3			# DBAT0L
	mtspr	536, 4			# DBAT0U
	isync
	# BAT4: 0x90000000 = MEM2, cached, code and data
	addis	3, 3, 0x1000
	addis	4, 4, 0x1000
	mtspr	561, 3			# IBAT4L
	mtspr	560, 4			# IBAT4U
	mtspr	569, 3			# DBAT4L
	mtspr	568, 4			# DBAT4U
	isync
	# DBAT1: 0xc0000000 = MEM1 and the registers, uncached
	li	3, 0x002a
	lis	4, 0xc000
	ori	4, 4, 0x1fff
	mtspr	539, 3			# DBAT1L
	mtspr	538, 4			# DBAT1U
	isync
	# DBAT5: 0xd0000000 = MEM2, uncached
	addis	3, 3, 0x1000
	addis	4, 4, 0x1000
	mtspr	571, 3			# DBAT5L
	mtspr	570, 4			# DBAT5U
	isync
	# BATs 4 to 7 on
	lis	3, 0x8200
	mtspr	HID4, 3
	isync

	# Nothing was booted from a disc: no BI2
	li	0, 0
	stw	0, 0xf4(0)

	# On to `crt`, translation on
	lis	3, crt@h
	ori	3, 3, crt@l
	mtsrr0	3
	li	3, 0x2030		# floating point, translation on
	mtsrr1	3
	rfi

crt:
	lis	1, __stack_top@h
	ori	1, 1, __stack_top@l
	li	0, 0
	stwu	0, -64(1)
	lis	3, __bss_start@h
	ori	3, 3, __bss_start@l
	lis	4, __bss_end@h
	ori	4, 4, __bss_end@l
1:	cmplw	3, 4
	bge	2f
	stw	0, 0(3)
	addi	3, 3, 4
	b	1b
2:	bl	loader_main
3:	b	3b
