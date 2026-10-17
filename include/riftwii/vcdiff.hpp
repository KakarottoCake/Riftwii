// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace riftwii {

// Patch-only mods (xdelta and other VCDIFF patches, RFC 3284): the
// patched image made from the original and the patch, on the Wii, for
// players who have the original and the patch but not the patched image.
//
// The patch is read window by window (xdelta3 writes 8 MB ones); each is
// checked against its Adler-32 when the patch carries one (xdelta3 does).
// Patches with secondary compression or their own code table are refused.

// Reads `length` bytes at `offset`; false on an error.
using VcdiffReadFn = std::function<bool(std::uint64_t offset, void* out, std::size_t length)>;
// Takes the next `length` bytes of the patched image; false to stop.
using VcdiffWriteFn = std::function<bool(const std::uint8_t* data, std::size_t length)>;
// After each window: patch bytes read so far and the patch's size; false to stop.
using VcdiffProgressFn = std::function<bool(std::uint64_t done, std::uint64_t total)>;

struct VcdiffLimits {
    std::uint64_t max_window = 64u << 20;    // a window's output and each of its sections
    std::uint64_t max_copy_window = 1ull << 32;  // how far into the original one window may copy from
    // A copy window up to `whole_copy_window` is read at once; a larger one
    // (xdelta3 makes them up to its source buffer, 64 MB and more) is read
    // as needed, through `cache_blocks` blocks of `cache_block` bytes.
    std::uint64_t whole_copy_window = 16u << 20;
    std::uint32_t cache_block = 64u << 10;
    std::uint32_t cache_blocks = 256;
};

struct VcdiffStats {
    std::uint64_t source_reads = 0;   // reads of the original
    std::uint64_t source_bytes = 0;
};

// Writes the whole patched image through `write`. `read_target` reads back
// what was written (for windows that copy from earlier output, which
// xdelta3 does not write by default); it may be empty.
bool apply_vcdiff(const VcdiffReadFn& patch, std::uint64_t patch_size, const VcdiffReadFn& source,
                  std::uint64_t source_size, const VcdiffWriteFn& write, const VcdiffReadFn& read_target,
                  const VcdiffProgressFn& progress, std::string& error, const VcdiffLimits& limits = {},
                  VcdiffStats* stats = nullptr);

// The first `length` bytes of the patched image (from its first window
// only, so `length` must fit in it): enough to read the disc header of
// what the patch makes, and to tell, by the window's checksum, whether
// `source` is the original it was made from (`checked` false when the
// patch carries no checksum).
bool vcdiff_head(const VcdiffReadFn& patch, std::uint64_t patch_size, const VcdiffReadFn& source,
                 std::uint64_t source_size, std::size_t length, std::vector<std::uint8_t>& out, bool& checked,
                 std::string& error, const VcdiffLimits& limits = {});

// The checksum xdelta3 gives each window (Adler-32 as zlib computes it).
std::uint32_t vcdiff_adler32(const std::uint8_t* data, std::size_t length, std::uint32_t adler = 1);

}  // namespace riftwii
