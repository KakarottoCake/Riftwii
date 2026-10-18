// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <gctypes.h>

// The start-up stash (include/riftwii/stashimage.hpp, docs/MEMORY.md):
// the menu's font, its translations and the GPL's text, which boot.dol
// carries as one more section just above .bss, where the heap grows.
//
// crt0 calls TakeStash (from __wrap___CheckARGV, before .bss is cleared):
// a fresh image there is copied to mem::kStashArea in MEM2 and its magic
// erased, so its MEM1 bytes can become heap. On a warm restart those bytes
// are heap leftovers and the copy in MEM2 from the first start is used.
extern "C" void riftwii_take_stash();

namespace riftwii::wii::stash {

// Checks the copy in MEM2 once (its checksum and this build's version).
// mem::Init calls it first thing in main, before anything uses MEM2.
void Check();

// The end of the copy in MEM2 when it is good, else mem::kStashArea: arena
// 2 starts above it.
u32 ReservedEnd();

// An entry ("menufont.zst", "es.po.zst", "licence.zst"); false when the
// stash is missing (riftwii.elf started directly has none) or damaged.
bool Find(const char* name, const u8*& data, u32& size);

// session.log: what crt0 found and how much MEM2 the copy holds.
void Log();

}  // namespace riftwii::wii::stash
