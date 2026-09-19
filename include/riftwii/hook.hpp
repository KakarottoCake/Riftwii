// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Installing the resident runtime: the blob format (runtime/resident/
// rt_hook.h), the PowerPC instruction patching it needs and the MEM2
// placement. Pure functions so the host tests cover them; wii/resident.cpp
// performs the actual copies and cache flushes.
namespace riftwii {

// The blob header, validated (see rt_blob_header).
struct ResidentBlob {
    std::uint32_t version = 0;
    std::uint32_t size = 0;
    std::uint32_t context_offset = 0;
    std::uint32_t hook_ioctl_async_offset = 0;
    std::uint32_t replay_ioctl_async_offset = 0;
    std::uint32_t continue_ioctl_async_offset = 0;
    std::uint32_t complete_di_offset = 0;
};
constexpr std::size_t kResidentContextBytes = 224;  // sizeof(struct rt_context)
bool parse_resident_blob(const std::uint8_t* bytes, std::size_t length, ResidentBlob& out, std::string& error);

// A same-size replacement served from memory (E3): the bytes the game must
// see at [virtual_offset, virtual_offset + bytes.size()) of the partition.
struct MemReplacement {
    std::uint64_t virtual_offset = 0;
    std::vector<std::uint8_t> bytes;
};

// Lays out the payload the loader puts after the blob: the redirect table
// (at payload offset 0) followed by the replacement data, each 32-byte
// aligned, with MEM sources computed for `payload_address`. The size does
// not depend on the address, so callers may size the reservation with a
// placeholder address first. Fails on empty or overlapping replacements.
bool build_mem_payload(const std::vector<MemReplacement>& replacements, std::uint32_t payload_address,
                       std::uint64_t tag, std::vector<std::uint8_t>& payload, std::string& error);

// A file whose content the game must see with a new size (grown, shrunk
// or created). It is relocated to the virtual window: partition offsets
// above any physical disc (word offsets from 0x80000000, i.e. 8 GiB), which
// the runtime serves entirely from the redirect table.
struct VirtualFile {
    std::string disc_path;             // absolute FST path of an existing file
    std::vector<std::uint8_t> bytes;   // new content, any size
};
constexpr std::uint64_t kVirtualWindowStart = 0x200000000ull;  // byte offset; word 0x80000000

// Assigns each virtual file a 32-byte aligned slot in the window, rewrites
// the FST entries (offset and size) and appends the corresponding MEM
// replacements, padded with zeros to a 32-byte multiple so a read rounded
// up by the DVD driver never leaves the table. Fails on unknown paths,
// directories, or a window that would leave the 32-bit word space.
bool plan_virtual_window(class Fst& fst, const std::vector<VirtualFile>& files,
                         std::vector<MemReplacement>& replacements, std::uint64_t& window_end,
                         std::string& error);

// lis/ori/mtctr/bctr through `reg` (0-31): an absolute jump in four words.
std::array<std::uint32_t, 4> encode_absolute_jump(unsigned reg, std::uint32_t target);
constexpr std::size_t kHookStubBytes = 16;

// Whether an instruction may be moved from the start of a hooked function
// into the replay slot: no branches, system calls or returns, and nothing
// that touches the register the continuation jump clobbers.
bool displaceable(std::uint32_t instruction, unsigned scratch_reg, std::string& why);

// Where the runtime goes: the top of the MEM2 arena, rounded to 64 KiB, so
// the game's allocator never sees it. `arena_end` is the value IOS put at
// 0x80003128; `extra_bytes` is space after the blob for tables and buffers.
struct ResidentPlacement {
    std::uint32_t base = 0;           // blob copied here
    std::uint32_t reserved_bytes = 0; // from base to the old arena end
    std::uint32_t new_arena_end = 0;  // what 0x80003128 becomes (== base)
};
bool plan_resident_placement(std::uint32_t arena_end, std::uint32_t blob_size, std::uint32_t extra_bytes,
                             ResidentPlacement& out, std::string& error);

}  // namespace riftwii
