// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/symsearch.hpp"

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

// The same for names ending in `suffix`, given in lower case (".elf": the
// programs a game may load itself, riftwii/gcprograms.hpp).
std::vector<GcFstFile> find_gc_fst_files_ending(const std::vector<std::uint8_t>& fst, const std::string& suffix,
                                                std::size_t max);

// The file of a GameCube FST that starts at `offset`; false when none does.
bool find_gc_fst_file_at(const std::vector<std::uint8_t>& fst, std::uint32_t offset, GcFstFile& out);


// How a bonus disc's menu starts a game from a TGC image, found in its
// code. The Zelda Ocarina of Time / Master Quest disc (D43E01) packs both
// games in one TGC with one N64 emulator, and its menu tells the emulator
// which ROM to run and how ("zle_f.n64", "-V1" for rumble, "-P0" for
// progressive scan, "-C1" for the memory card) through an argument list,
// the emulator's argv. RiftWii starts the TGC's game again itself, so it
// needs that list; without it the emulator runs its default ROM, Master
// Quest, whichever game was picked.
//
// The menu fills the list only once Play is pressed and the TGC's header
// is read, then hands it to its start routine, start(file, list), which
// loads the program with a disc driver of its own: no read RiftWii sees
// comes after the list is filled. So the routine is what is hooked.
//
// The list is a global array of string pointers, the last entry 0. The
// menu fills it in a run that ends with its last option and the 0, then
// calls the routine with the list's address:
//     lis rB,hi ; addi rList,rB,lo         the list (a MEM1 address)
//     ...
//     addi rS,r13,d ; stwx rS,rList,rI     an option from the small data
//     li rZ,0 ; ... ; stwx rZ,rList,rJ     the end of the list
//     ...
//     lis r4,hi ; ... ; addi r4,r4,lo      the list again, as the argument
//     bl start
// with each step at most a few instructions from the one before. Both
// are 0 when the menu has no such run, or more than one place looks like
// it.
struct GcTgcMenuStart {
    std::uint32_t args = 0;   // the list's address
    std::uint32_t start = 0;  // the routine it is handed to
};
GcTgcMenuStart find_gc_tgc_menu_start(const std::vector<CodeRange>& text);

}  // namespace riftwii
