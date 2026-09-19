// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/patch.hpp"

namespace riftwii {

struct MemoryRegion {
    std::uint32_t address = 0;
    std::uint32_t length = 0;
};

// The memory the patches read and write, so the same code runs against a
// host buffer in the tests and against the console's RAM (where a write
// also flushes the caches).
class MemoryAccess {
public:
    virtual ~MemoryAccess() = default;
    virtual bool read(std::uint32_t address, std::uint8_t* out, std::size_t length) = 0;
    virtual bool write(std::uint32_t address, const std::uint8_t* bytes, std::size_t length) = 0;
};

// Applies a plan's <memory> patches once the apploader has loaded the
// game, in document order. `value` must already hold the bytes (a
// `valuefile` is read by the caller). Semantics, from the public
// patch-format documentation and Dolphin's independent implementation:
//   - plain: `value` is written at offset | 0x80000000; when `original` is
//     given and the bytes there differ, nothing is written.
//   - search: the first place in `loaded` (the regions the apploader
//     filled, in load order) at an `align` stride where `original` matches
//     gets `value`.
//   - ocarina: the first occurrence of `value` in `loaded` (at 4-byte
//     steps, as code), then the next blr at or after it (4-byte steps)
//     becomes an unconditional branch to offset | 0x80000000.
// Every write must fall inside one of `writable`. A pattern that is not
// found or an `original` that differs is reported in `notes`, not an
// error; a malformed patch or a write outside `writable` is an error.
bool apply_memory_patches(const std::vector<MemoryPatch>& patches, const std::vector<MemoryRegion>& loaded,
                          const std::vector<MemoryRegion>& writable, MemoryAccess& memory,
                          std::vector<std::string>& notes, std::string& error);

// Largest `valuefile` a caller should read into `value`.
constexpr std::size_t kMaxMemoryValueBytes = 1u << 20;

}  // namespace riftwii
