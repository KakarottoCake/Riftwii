// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/redirect.hpp"  // PlacedRun
#include "rtable.h"               // rt_entry

// Installing the resident runtime: the blob format (runtime/resident/
// rt_hook.h), the PowerPC instruction patching it needs and the placement
// in MEM1 (code) and MEM2 (data). Pure functions so the host tests cover
// them; wii/resident.cpp performs the actual copies and cache flushes.
namespace riftwii {

constexpr std::size_t kResidentIpcEntries = 14;

// The blob header, validated (see rt_blob_header).
struct ResidentBlob {
    std::uint32_t version = 0;
    std::uint32_t size = 0;
    std::uint32_t context_offset = 0;
    std::array<std::uint32_t, kResidentIpcEntries> hook_offsets{};
    std::array<std::uint32_t, kResidentIpcEntries> replay_offsets{};
    std::array<std::uint32_t, kResidentIpcEntries> continue_offsets{};
    std::uint32_t complete_di_offset = 0;
    std::uint32_t complete_fs_offset = 0;
    // 4A compatibility aliases for the legacy DI-only loader.  They are
    // derived from the async-Ioctl table entry, never serialized.
    std::uint32_t hook_ioctl_async_offset = 0;
    std::uint32_t replay_ioctl_async_offset = 0;
    std::uint32_t continue_ioctl_async_offset = 0;
};
static_assert(4 * (4 + 3 * kResidentIpcEntries + 2) == 192, "resident blob ABI v4 header size");
constexpr std::size_t kResidentContextBytes = 2048;  // sizeof(struct rt_context)
bool parse_resident_blob(const std::uint8_t* bytes, std::size_t length, ResidentBlob& out, std::string& error);

// A same-size replacement served from memory (E3): the bytes the game must
// see at [virtual_offset, virtual_offset + bytes.size()) of the partition.
struct MemReplacement {
    std::uint64_t virtual_offset = 0;
    std::vector<std::uint8_t> bytes;
};

// Bytes the game must see at [virtual_offset, virtual_offset + total run
// length) that live on the SD card (E4): the runs place_on_fragments
// produced for that range, in file order. The runtime fetches them
// through the SD fd the loader hands it.
struct SdReplacement {
    std::uint64_t virtual_offset = 0;
    std::vector<PlacedRun> runs;
};

// Bytes the game must see at [virtual_offset, virtual_offset + length)
// that are the disc's own bytes at [disc_offset, disc_offset + length)
// (E7): the untouched part of a relocated or partially patched file. The
// runtime fetches them with its own DVDLowRead.
struct DiscReplacement {
    std::uint64_t virtual_offset = 0;
    std::uint64_t disc_offset = 0;
    std::uint64_t length = 0;
};

// Everything one redirect table is made of: replacements the loader
// authored directly, plus ready-made entries (from build_redirect_table,
// the XML pipeline's compiler) whose sources need no memory.
struct PayloadPieces {
    std::vector<MemReplacement> mem;
    std::vector<SdReplacement> sd;
    std::vector<DiscReplacement> disc;
    std::vector<rt_entry> entries;  // SD, DISC or ZERO kinds only (MEM needs bytes)
    bool empty() const { return mem.empty() && sd.empty() && disc.empty() && entries.empty(); }
    bool needs_sd() const;
};

// Lays out the payload the loader puts after the blob: the redirect table
// (at payload offset 0) followed by the MEM replacement data, each 32-byte
// aligned, with MEM sources computed for `payload_address`; SD
// replacements become one SD entry per run. The size does not depend on
// the address, so callers may size the reservation with a placeholder
// address first. Fails on empty or overlapping pieces.
bool build_payload(const PayloadPieces& pieces, std::uint32_t payload_address, std::uint64_t tag,
                   std::uint32_t sdio_fd, std::vector<std::uint8_t>& payload, std::string& error);
// MEM only, no SD fd.
bool build_mem_payload(const std::vector<MemReplacement>& replacements, std::uint32_t payload_address,
                       std::uint64_t tag, std::vector<std::uint8_t>& payload, std::string& error);

// A file whose content the game must see with a new size (grown, shrunk
// or created). It is relocated to the virtual window: partition offsets
// above any physical disc (word offsets from 0x80000000, i.e. 8 GiB), which
// the runtime serves entirely from the redirect table.
struct VirtualFile {
    std::string disc_path;             // absolute FST path of an existing file
    std::vector<std::uint8_t> bytes;   // new content in memory, any size; or
    std::vector<PlacedRun> sd_runs;    // new content on the card (its size is the runs' total); or
    bool original = false;             // the file's own disc bytes, relocated as they are (E7)
    std::uint64_t size() const;        // 0 for `original`: the planner takes the FST entry's size
};
constexpr std::uint64_t kVirtualWindowStart = 0x200000000ull;  // byte offset; word 0x80000000

// Assigns each virtual file a 32-byte aligned slot in the window from
// `window_cursor` (in/out: the next free byte), rewrites the FST entries
// (offset and size) and appends the corresponding MEM, SD or DISC
// replacement. MEM content is padded with zeros to a 32-byte multiple;
// SD and DISC content is not, the runtime zero-fills the window's gaps,
// so a read rounded up by the DVD driver is served either way. Fails on
// unknown paths, directories, empty content, or a window that would
// leave the 32-bit word space.
bool plan_virtual_window(class Fst& fst, const std::vector<VirtualFile>& files,
                         std::vector<MemReplacement>& mem, std::vector<SdReplacement>& sd,
                         std::vector<DiscReplacement>& disc, std::uint64_t& window_cursor, std::string& error);

// lis/ori/mtctr/bctr through `reg` (0-31): an absolute jump in four words.
std::array<std::uint32_t, 4> encode_absolute_jump(unsigned reg, std::uint32_t target);

// The hook written over a hooked function's first instruction: one
// relative `b` to its trampoline (MEM1 is 24 MiB, inside b's +-32 MiB
// reach). Only that instruction is displaced, so code that enters the
// function at +4 after running the first instruction itself (Pulsar's
// IOS_Open "OpenFix") runs the untouched original. False when `to` is out
// of reach or either address is not word aligned.
bool encode_branch(std::uint32_t from, std::uint32_t to, std::uint32_t& out);
constexpr std::size_t kHookStubBytes = 4;

// Whether an instruction may be moved from the start of a hooked function
// into the replay slot: no branches, system calls or returns, and nothing
// that touches the register the continuation jump clobbers.
bool displaceable(std::uint32_t instruction, unsigned scratch_reg, std::string& why);

// Where the runtime goes. The code (the blob, with its context) sits at
// the top of the MEM1 arena, just below what the apploader left there
// (BI2, FST): MEM1 is the one region every SDK keeps an instruction BAT
// for, a 2009 SDK drops the MEM2 one before its first IPC call (section
// 23). The data (redirect table, MEM bytes, bounce buffers) takes the top
// of the MEM2 arena, rounded to 64 KiB, and only when there is any.
// `arena1_hi` is the apploader's value at 0x80000034, `arena2_end` the
// value IOS put at 0x80003128; `mem1_floor` is the lowest address the
// code may take (above the loader and the apploader image).
struct ResidentPlacement {
    std::uint32_t code_base = 0;        // blob copied here (MEM1)
    std::uint32_t code_bytes = 0;       // from code_base to the old arena hi
    std::uint32_t new_arena1_hi = 0;    // what 0x80000034 becomes (== code_base)
    std::uint32_t data_base = 0;        // payload and buffers (MEM2), 0 when none
    std::uint32_t data_bytes = 0;       // from data_base to the old arena end
    std::uint32_t new_arena2_end = 0;   // what 0x80003128 becomes (unchanged when data_bytes == 0)
};
bool plan_resident_placement(std::uint32_t arena1_hi, std::uint32_t mem1_floor, std::uint32_t arena2_end,
                             std::uint32_t blob_size, std::uint32_t extra_bytes, ResidentPlacement& out,
                             std::string& error);

}  // namespace riftwii
