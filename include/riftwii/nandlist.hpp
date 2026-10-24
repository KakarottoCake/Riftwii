// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Sizes for ISFS_ReadDir lists, checked before any allocation.
//
// A damaged (emulated) NAND can report any count, so the Wii side sizes
// its list buffer through here first.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace riftwii {

// All FST entries on a Wii, so no folder lists more.
constexpr std::uint32_t kNandMaxEntries = 6143;
// One name: up to 12 characters plus its NUL.
constexpr std::size_t kNandNameMax = 12;
constexpr std::size_t kNandNameBytes = kNandNameMax + 1;

// What listing a NAND folder found. NotThere is the first ReadDir failing
// (no such folder, or it is a file); Damaged is a count or list no real
// NAND could report (the caller reports it); NoRoom is the list buffer
// having no room (the caller reports that instead).
enum class NandList { Ok, NotThere, Damaged, NoRoom };

// Buffer bytes for `count` names, 32-byte aligned for ISFS. False when the
// count cannot come from a real NAND.
bool nand_list_bytes(std::uint32_t count, std::size_t& bytes);

// Names from a list buffer sized for `asked` entries holding `got` ones.
// False, with `names` cleared, when the buffer does not hold `got` names.
bool parse_nand_list(const char* list, std::size_t list_bytes, std::uint32_t asked, std::uint32_t got,
                     std::vector<std::string>& names);

using NandReadDir = std::int32_t (*)(void* ctx, char* list, std::uint32_t* count);  // list nullptr: ask the count
using NandAlloc = void* (*)(std::size_t bytes);  // 32-byte aligned on the Wii
using NandFree = void (*)(void* block);

// The names in a NAND folder through the given calls, for host tests.
// The first call asks the count (fail -> NotThere, 0 -> Ok with `names`
// empty); the buffer is then sized, zeroed, and read (any refusal ->
// Damaged, no room -> NoRoom). `count` out is the first call's count,
// for the log line. `names` is cleared at the start; the buffer is
// released exactly once on every path that allocated it.
NandList list_nand_with(NandReadDir read_dir, void* ctx, NandAlloc alloc, NandFree release,
                        std::vector<std::string>& names, std::uint32_t& count);

}  // namespace riftwii
