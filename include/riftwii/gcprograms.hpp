// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// GameCube games that load more programs from the disc with code of their
// own (the plugin's "Programs the game loads itself"). EA's James Bond
// games start a small loader, which reads the game itself from an ELF file
// on the disc and jumps to it; that program later loads another loader,
// which loads the driving missions' ELF, and back. Each has its own copy
// of the SDK, which RiftWii has to patch as it does the disc's own
// program, and RiftWii's part of MEM1 has to point at the running one's
// functions. The planning here is platform-free and host-tested; the
// plugin runs the patches and the blob applies them (runtime/gc's
// gc_programs.h).

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "riftwii/symsearch.hpp"

namespace riftwii {

// A program's loadable segment: `file_bytes` from `file_offset` in the ELF
// file go to `address`, and the rest up to `memory_bytes` is zeroed.
struct GcElfSegment {
    std::uint32_t address = 0;
    std::uint32_t file_offset = 0;
    std::uint32_t file_bytes = 0;
    std::uint32_t memory_bytes = 0;
    bool executable = false;
};

struct GcElfProgram {
    std::uint32_t entry = 0;
    std::vector<GcElfSegment> segments;  // the PT_LOAD ones with memory, in the file's order
};

// How much of an ELF file's start parse_gc_elf needs: its header and the
// program headers, which every program seen so far has right after it.
constexpr std::size_t kGcElfHeadBytes = 0x1000;

// A 32-bit big-endian PowerPC executable whose loadable segments all lie
// within [floor, ceiling) and within the file's `file_bytes`, its entry in
// an executable one. `head` is the file's start (up to kGcElfHeadBytes).
bool parse_gc_elf(const std::uint8_t* head, std::size_t head_bytes, std::uint32_t file_bytes, std::uint32_t floor,
                  std::uint32_t ceiling, GcElfProgram& out, std::string& why);

// The disc offset the word at `address` is loaded from, the file starting
// at `file_disc_offset`: false when no segment loads it from the file (a
// zeroed tail, or outside the program).
bool gc_elf_disc_offset(const GcElfProgram& program, std::uint32_t file_disc_offset, std::uint32_t address,
                        std::uint32_t& disc_offset);

// The words in which `patched` differs from `original` (the same section,
// at `address`), as (address, new word). The sections' sizes are equal.
void gc_changed_words(std::uint32_t address, const std::vector<std::uint8_t>& original,
                      const std::vector<std::uint8_t>& patched,
                      std::vector<std::pair<std::uint32_t, std::uint32_t>>& out);

// A word the blob writes over what a read brings from the disc.
struct GcDiscPatch {
    std::uint32_t offset = 0;
    std::uint32_t word = 0;
};

// Sorted by offset for the blob's binary search. False when two patches
// overlap (the same bytes would be written twice).
bool gc_sort_disc_patches(std::vector<GcDiscPatch>& patches, std::string& why);

// A program's first two words become "li r0,index" and "b hook" (from
// entry + 4): the blob takes the program's start there, then runs the two
// from a slot of its own. False when either cannot run from elsewhere (a
// relative branch) or may use r12 or CTR, which the blob uses on the way in
// and on the way back to the third word.
bool gc_program_entry_words(std::uint32_t entry, std::uint32_t first, std::uint32_t second, unsigned index,
                            std::uint32_t hook, std::uint32_t out[2], std::string& why);

// Whether any relative branch in the code (b, bc, with or without link)
// goes to `target`. A program whose code branches to its entry + 4 is not
// taken over: that word becomes the jump to the blob, and only a start at
// the entry has set r0 to the program's index first.
bool gc_code_branches_to(const std::vector<CodeRange>& text, std::uint32_t target);

// RiftWii's part of MEM1 as a set of regions, read whole: before any
// program's patches (`base`) and after each program's. The words a program
// switch has to set are those any program changed from the base, less the
// `excluded` addresses (words the blob changes as it runs). Addresses in
// ascending order.
struct GcResidentRegion {
    std::uint32_t address = 0;
    std::vector<std::uint8_t> bytes;
};
std::vector<std::uint32_t> gc_resident_words(const std::vector<GcResidentRegion>& base,
                                             const std::vector<std::vector<GcResidentRegion>>& programs,
                                             const std::vector<std::uint32_t>& excluded);

// The word at `address` in a state read with the same regions; 0 when no
// region has it.
std::uint32_t gc_resident_word(const std::vector<GcResidentRegion>& state, std::uint32_t address);

}  // namespace riftwii
