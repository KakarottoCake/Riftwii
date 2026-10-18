// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

// The start-up stash: data the menu needs only now and then (its font,
// its translations, the GPL's text) packed by tools/dol_stash.py into an
// extra DOL section that the loader puts just above RiftWii's .bss, where
// the heap later grows. crt0 copies it to MEM2 before anything else runs,
// so those bytes of MEM1 become heap (docs/MEMORY.md).
//
// The image, all numbers big-endian:
//   0   "RWSTASH1"
//   8   u32 total bytes of the image (header, directory and data)
//   12  u32 FNV-1a checksum of bytes [16, total)
//   16  u32 number of entries
//   20  char[44] RiftWii's version, NUL-padded: another build's image is refused
//   64  entries, 32 bytes each: char[24] name (NUL-padded), u32 offset, u32 size
//   then each entry's data, 32-byte aligned, at its offset from the image's start
//
// Everything here is pure, allocation-free and touches no global state:
// crt0 calls it before .bss is cleared.
namespace riftwii {

constexpr std::size_t kStashHeaderBytes = 64;
constexpr std::size_t kStashEntryBytes = 32;
constexpr std::size_t kStashNameBytes = 24;
constexpr std::size_t kStashVersionBytes = 44;
// Far more than the menu's font and translations; a size past it is damage.
constexpr std::uint32_t kStashMaxBytes = 2u << 20;
constexpr std::uint32_t kStashMaxEntries = 64;

std::uint32_t stash_checksum(const std::uint8_t* data, std::size_t size);

// The image's total size when `image` (with `available` readable bytes)
// starts with a whole, undamaged stash of this `version`; 0 otherwise.
std::uint32_t stash_check(const std::uint8_t* image, std::size_t available, const char* version);

// The checks of stash_check without the checksum (the magic, the sizes, the
// version and every entry's bounds): for an image already checked once.
std::uint32_t stash_check_shape(const std::uint8_t* image, std::size_t available, const char* version);

// Finds `name` in an image that passed stash_check (or stash_check_shape).
bool stash_find(const std::uint8_t* image, const char* name, const std::uint8_t*& data, std::uint32_t& size);

}  // namespace riftwii
