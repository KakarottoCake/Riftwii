# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# Run from the repository root, inside devkitPro's MSYS2 shell.
include $(DEVKITPPC)/wii_rules

OUT := build_wii/argv-emulator
SOURCE := tools/dolphin/argv-emulator/argv_emulator.c
CFLAGS := -Os -Wall -Wextra -Werror $(MACHDEP) -I$(LIBOGC_INC)

.PHONY: all
all: $(OUT)/boot.dol

$(OUT):
	@mkdir -p "$@"

$(OUT)/argv_emulator.o: $(SOURCE) | $(OUT)
	$(CC) $(CFLAGS) -c "$<" -o "$@"

$(OUT)/boot.elf: $(OUT)/argv_emulator.o
	$(CC) $(MACHDEP) "$<" -L$(LIBOGC_LIB) -lfat -logc -lm -o "$@"

$(OUT)/boot.dol: $(OUT)/boot.elf
	elf2dol "$<" "$@"
