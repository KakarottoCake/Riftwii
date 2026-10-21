// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace riftwii {

// A TGC image: a whole GameCube disc packed into one file on another disc.
// Nintendo's bonus and demo discs keep the games they offer this way (the
// Zelda Ocarina of Time / Master Quest disc D43E01, the Wind Waker demo
// disc PZLE01), and their menu loads the chosen game from its TGC with
// a loader of its own. The layout follows Dolphin's TGC reader and the
// public descriptions of the format (NOTICE.md, design provenance).
//
// The header, big-endian 32-bit words from the file's start:
//   0x00 magic 0xAE0F38A2
//   0x08 the header's size: the packed disc (boot.bin, BI2, apploader)
//        starts here
//   0x0C the packed disc header area's size
//   0x10, 0x14, 0x18 the FST's place in the file, its size and its
//        largest size (what the program reserves for it)
//   0x1C, 0x20 the program (DOL): its place in the file and its size
//   0x24 where the files' data starts in the file
//   0x34 where the FST thinks it starts: each file's offset in the packed
//        FST counts from here, not from the TGC's start
// All of it comes from a disc and is untrusted: parse_gc_tgc bounds every
// field by the TGC's own length, and that by the image.
constexpr std::uint32_t kGcTgcMagic = 0xAE0F38A2u;
constexpr std::size_t kGcTgcHeaderBytes = 0x38;
// Bigger than any GameCube program's FST (they take a few hundred KB) and
// small enough that MEM1 keeps room for the game.
constexpr std::uint32_t kGcTgcFstLimit = 0x400000;

struct GcTgc {
    // Where the TGC file is on the outer disc and how long it is (from the
    // outer disc's FST).
    std::uint32_t disc_offset = 0;
    std::uint32_t bytes = 0;
    // From its header, all in bytes from the TGC's start.
    std::uint32_t header_bytes = 0;
    std::uint32_t disc_header_bytes = 0;
    std::uint32_t fst_offset = 0;
    std::uint32_t fst_size = 0;
    std::uint32_t fst_max = 0;
    std::uint32_t dol_offset = 0;
    std::uint32_t dol_size = 0;
    std::uint32_t file_area_offset = 0;
    std::uint32_t file_area_virtual = 0;

    // The same places on the outer disc, which is what RiftWii reads.
    std::uint32_t boot_at() const { return disc_offset + header_bytes; }
    std::uint32_t bi2_at() const { return disc_offset + header_bytes + 0x440; }
    std::uint32_t fst_at() const { return disc_offset + fst_offset; }
    std::uint32_t dol_at() const { return disc_offset + dol_offset; }
    std::uint32_t dol_end() const { return disc_offset + dol_offset + dol_size; }
};

// Reads the header of the TGC at `disc_offset` on an image of `image_bytes`,
// `tgc_bytes` long. False, with `why` saying which field is wrong, when the
// magic is not there or anything in the header reaches outside the TGC
// (or the TGC outside the image).
bool parse_gc_tgc(const std::uint8_t* header, std::size_t header_size, std::uint32_t disc_offset,
                  std::uint32_t tgc_bytes, std::uint64_t image_bytes, GcTgc& out, std::string& why);

// True when every section of the DOL whose first 0x100 bytes are
// `dol_header` lies within its `dol_size` bytes: the TGC's DOL is read
// from the outer disc, and a section past its end would be another file.
bool gc_tgc_dol_fits(const std::uint8_t* dol_header, std::size_t header_size, std::uint32_t dol_size,
                     std::string& why);

// The TGC's FST, read from it, made the program's FST on the outer disc:
// each file's offset becomes its place on the outer disc (its offset from
// file_area_virtual, then from file_area_offset in the TGC, then from the
// TGC's own place). The program reads its files by these offsets, so every
// one of them must fall within the TGC; a file of no bytes is pointed at
// the TGC's start, as it is never read. False, with the FST unchanged,
// when the table is not whole or a file reaches outside the TGC.
bool gc_tgc_rebase_fst(std::vector<std::uint8_t>& fst, const GcTgc& tgc, std::string& why);

// A file of a GameCube FST, as the disc has it.
struct GcFstFile {
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
};

// The files of a GameCube FST whose names end in ".tgc" (any case), the
// first `max` of them in table order: the TGC images a bonus disc's menu
// may load. Empty when the table is not whole.
std::vector<GcFstFile> find_gc_tgc_files(const std::vector<std::uint8_t>& fst, std::size_t max);

// The file of a GameCube FST that starts at `offset`; false when none does.
bool find_gc_fst_file_at(const std::vector<std::uint8_t>& fst, std::uint32_t offset, GcFstFile& out);

}  // namespace riftwii
