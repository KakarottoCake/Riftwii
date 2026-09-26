# SPDX-License-Identifier: GPL-3.0-or-later
#
# The RiftWii channel's first code. Launched from the Wii Menu, a
# channel's program starts at 0x3400 with address translation off
# (every retail channel's DOL has its entry there). This sets the CPU
# up as the Homebrew Channel leaves it for a program it starts, then
# jumps to the forwarder's own entry (libogc's) with translation on,
# the one way homebrew is known to start on a Wii and a vWii alike.
#
# tools/make_channel.py (channel_entry) puts this at 0x80003400 as the
# DOL's first text section and fills in `entry`. It runs from physical
# 0x3400, so it uses no absolute addresses but those.

#define HID0	1008
#define HID4	1011

	.text
	.globl	_start
_start:
	b	setup
	.long	0x52465457		# "RFTW": channel_entry checks this is the stub
entry:
	.long	0			# the forwarder's entry, from channel_entry

setup:
	# Caches off and invalidated, then on (the branch caches too)
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

	# To the forwarder's entry, translation on
	lwz	3, 0x3408(0)		# entry, at physical 0x3400 + 8
	mtsrr0	3
	mfmsr	3
	ori	3, 3, 0x30
	mtsrr1	3
	rfi
