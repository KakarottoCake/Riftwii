// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/redirect.hpp"

namespace riftwii {

// GameCube mods without touching the image. A pack's changed and created
// files move to a window past the end of the image, and so does a
// rewritten FST; the game's view of the disc is the image up to its end,
// then the window. RiftWii's GameCube loader (plugins/gamecube/gcnative.cpp)
// serves every disc read from that view, as a list of runs of SD sectors,
// zeros and the FST it keeps in memory.

// One piece of a file's new content; a file is its pieces in order.
struct GcPiece {
    enum class Kind { Image, Device, Zero };
    Kind kind = Kind::Zero;
    std::uint64_t length = 0;
    std::uint64_t source = 0;  // Image: byte offset in the image; Device: 512-byte sector
    std::uint32_t skip = 0;    // Device: bytes into sector `source` (under 512)
};

struct GcFile {
    std::string disc_path;     // absolute FST path ("/files/x.bin" style, no "files" prefix: "/x.bin")
    bool create = false;       // the path is new; fails if it exists, as an existing path fails without it
    std::vector<GcPiece> pieces;
};

struct GcOverlayPlan {
    std::vector<std::uint8_t> fst;       // the new FST, padded to 32 bytes
    std::vector<std::uint8_t> header;    // disc header bytes 0x420-0x440, pointing at the new FST
    std::uint64_t fst_offset = 0;        // where the new FST sits in the window
    struct Entry {
        std::uint64_t vstart = 0;
        std::uint64_t length = 0;
        GcPiece piece;
    };
    std::vector<Entry> entries;          // file content in the window, sorted
};

// A GameCube game's disc offsets are 32-bit.
constexpr std::uint64_t kGcDiscLimit = 0x100000000ull;

// `boot` is the first 0x440 bytes of the image (boot.bin), `fst` its FST
// as stored at boot.bin's 0x424, `image_size` the image file's size.
bool plan_gc_overlay(const std::vector<std::uint8_t>& boot, const std::vector<std::uint8_t>& fst,
                     std::uint64_t image_size, const std::vector<GcFile>& files, GcOverlayPlan& out,
                     std::string& error);

// One stretch of the game's view of the disc, as the loader serves it.
struct GcRun {
    // Fst: the plan's FST, which the loader keeps in memory. Image: bytes
    // of an image the blob reads by its own offsets (an RVZ).
    enum class Kind { Sd, Zero, Fst, Image };
    Kind kind = Kind::Zero;
    std::uint64_t disc = 0;
    std::uint64_t length = 0;
    std::uint64_t sector = 0;  // Sd: the first 512-byte sector
    std::uint32_t skip = 0;    // Sd: bytes into it
    std::uint64_t fst_offset = 0;  // Fst: bytes into the plan's FST
    std::uint64_t image_offset = 0;  // Image: where its bytes start in the image
};

// The whole view, sorted and without gaps: the image (its file's SD
// fragments, `image_size` bytes), then, with a plan, its window, the gaps
// between the window's files as zeros.
bool gc_disc_runs(const std::vector<Fragment>& image, std::uint64_t image_size, const GcOverlayPlan* plan,
                  std::vector<GcRun>& out, std::string& error);

// The same for an image read from its RVZ (docs/RVZ.md): the image's
// bytes, in place and in the window, are Image runs that name their offset
// in the image, which the blob's RVZ build serves from the RVZ.
bool gc_disc_runs_rvz(std::uint64_t image_size, const GcOverlayPlan* plan, std::vector<GcRun>& out,
                      std::string& error);

}  // namespace riftwii
