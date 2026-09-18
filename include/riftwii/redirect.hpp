// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "riftwii/apply.hpp"
#include "rtable.h"

namespace riftwii {

// Where one composed file sits in the game's view of the partition data
// (virtual_offset: the FST entry the game reads) and where its untouched
// bytes really are on the disc (original_offset). They differ when a file
// was relocated to the virtual window because it grew or was created.
struct VirtualFileLayout {
    const AppliedFile* file = nullptr;
    std::uint64_t virtual_offset = 0;
    std::uint64_t original_offset = 0;
};

// One placed piece of an external file: in memory or on the SD card.
struct PlacedRun {
    std::uint32_t kind = RT_KIND_MEM;  // RT_KIND_MEM or RT_KIND_SD
    std::uint64_t length = 0;
    std::uint64_t source = 0;          // MEM: address; SD: sector
    std::uint32_t skip = 0;            // SD: bytes into the first sector
};

// A contiguous run of SD sectors holding consecutive bytes of one file; a
// file is the concatenation of its fragments in order (the last one may be
// partly used).
struct Fragment {
    std::uint64_t sector = 0;
    std::uint64_t sector_count = 0;
};

// Maps [file_offset, file_offset + length) of a file laid out over
// `fragments` to SD runs. Fails when the range runs past the fragments.
bool place_on_fragments(const std::vector<Fragment>& fragments, std::uint64_t file_offset,
                        std::uint64_t length, std::vector<PlacedRun>& out, std::string& error);

// Maps [source_offset, source_offset + length) of an external ByteSource to
// where the runtime can fetch it. The loader implements this with the
// FAT32 fragment resolver; tests use in-memory placements.
using ExternalPlacer = std::function<bool(const ByteSource* external, std::uint64_t source_offset,
                                          std::uint64_t length, std::vector<PlacedRun>& out,
                                          std::string& error)>;

// Compiles the flattened files into one validated redirect table (header +
// sorted entries, native byte order). Untouched original bytes that stay
// at their disc position become gaps; relocated ones become DISC entries.
// Fails if two files overlap in virtual space or a placement fails.
bool build_redirect_table(const std::vector<VirtualFileLayout>& files, const ExternalPlacer& place,
                          std::uint32_t sdio_fd, std::uint64_t tag,
                          std::vector<std::uint8_t>& out, std::string& error);

}  // namespace riftwii
