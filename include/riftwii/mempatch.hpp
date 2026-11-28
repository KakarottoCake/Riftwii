// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/patch.hpp"
#include "riftwii/symsearch.hpp"

namespace riftwii {

struct MemoryRegion {
    std::uint32_t address = 0;
    std::uint32_t length = 0;
};

// Returns `regions` with every byte in `exclusions` removed. Both inputs may
// be unsorted and may contain empty, duplicate, touching or overlapping
// ranges. Arithmetic is widened before ranges are clipped, so a region at
// the top of the 32-bit address space cannot wrap into low memory.
std::vector<MemoryRegion> subtract_memory_regions(const std::vector<MemoryRegion>& regions,
                                                   const std::vector<MemoryRegion>& exclusions);

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
//   - plain: `value` is written at offset | 0x80000000 (an uncached
//     0xC…/0xD… offset at its cached 0x8…/0x9… alias); when `original` is
//     given and the bytes there differ, nothing is written.
//   - search: the first place in `loaded` (the regions the apploader
//     filled, in load order) at an `align` stride where `original` matches
//     gets `value`, which may be longer or shorter than `original`.
//   - ocarina: the first occurrence of `value` in `loaded` (at 4-byte
//     steps, as code), then the next blr at or after it (4-byte steps)
//     becomes an unconditional branch to offset | 0x80000000.
// Every write must fall inside one of `writable`. A pattern that is not
// found or an `original` that differs is reported in `notes`, not an
// error; a malformed patch or a write outside `writable` is an error.
bool apply_memory_patches(const std::vector<MemoryPatch>& patches, const std::vector<MemoryRegion>& loaded,
                          const std::vector<MemoryRegion>& writable, MemoryAccess& memory,
                          std::vector<std::string>& notes, std::string& error);

// Where a plain or ocarina patch's offset points in RAM: offset | 0x80000000,
// an uncached 0xC…/0xD… offset at its cached 0x8…/0x9… alias (cached and
// uncached views of the same RAM are one place).
std::uint32_t memory_patch_target(const MemoryPatch& p);

// What the plain patches will leave in memory, for a search that runs
// before they are applied (RiftWii's PADRead search). In document order,
// as apply_memory_patches goes: a later write wins over an earlier one,
// and a patch whose `original` differs from `memory` (the game as loaded,
// with the earlier writes over it) is skipped. Writes that touch become
// one range. Search and ocarina patches have no address until applied
// and aren't run, so a guard they would change isn't seen here: what is
// found must be checked again once the patches are in. Nor is a patch
// apply_memory_patches would refuse (it is reported when the patches
// really go in). A range of one whole-word write points into `patches`,
// which must outlive this; touching writes, and a write that changes part
// of a word (the rest of the word read from `memory`), become a copy.
class PatchedCode final {
public:
    // The most plain patches previewed (see build).
    static constexpr std::size_t kMaxPreviewedPatches = 4096;

    // False (with `error`) when there's no memory for the lists or the
    // joined copies, or more than kMaxPreviewedPatches plain patches; the
    // ranges are then empty.
    bool build(const std::vector<MemoryPatch>& patches, MemoryAccess& memory, std::string& error);
    // Word-aligned, in address order, not overlapping.
    const std::vector<CodeRange>& ranges() const { return ranges_; }

private:
    struct Write {
        std::uint32_t address;
        const std::uint8_t* bytes;
        std::size_t size;
    };
    bool read(MemoryAccess& memory, std::uint32_t address, std::vector<std::uint8_t>& out) const;
    bool join(MemoryAccess& memory);

    std::vector<Write> writes_;                     // document order
    std::vector<std::vector<std::uint8_t>> joined_;  // touching writes, one copy each
    std::vector<CodeRange> ranges_;
};

// The game memory all of a launch's searches may read together. One search
// reads the loaded game once, a few MiB; but nothing limited how many
// searches a pack makes (macros repeat them), and each read it all again
// (CAV-02, BUG-07). Real packs read well under this. The pads' preview of
// a pack's searches (padhook.cpp) keeps to it too.
constexpr std::uint64_t kMaxSearchBytes = std::uint64_t(1) << 30;

// Largest `valuefile` a caller should read into `value`.
constexpr std::size_t kMaxMemoryValueBytes = 1u << 20;

// All the memory-patch bytes (values and originals) one launch may carry.
// One XML holds at most 1 MiB of them, but each selection of a patch copies
// them again, and macros can select the same patch thousands of times
// (MIN-09): gigabytes were asked for before the bad_alloc ended the launch.
// The plan counts the values written in the XML; the launch counts them
// again with each valuefile it reads (FU-04).
constexpr std::size_t kMaxPlanMemoryBytes = std::size_t(16) << 20;

}  // namespace riftwii
