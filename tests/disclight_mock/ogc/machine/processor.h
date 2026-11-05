// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <gccore.h>

u32 LockInterrupts();
void RestoreInterrupts(u32 level);
void mask32(u32 address, u32 clear, u32 set);
#define _CPU_ISR_Disable(level) do { (level) = LockInterrupts(); } while (0)
#define _CPU_ISR_Restore(level) RestoreInterrupts(level)
