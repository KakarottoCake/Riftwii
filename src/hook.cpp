// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/hook.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "riftwii/fst.hpp"
#include "rt_hook.h"
#include "rtable.h"

namespace riftwii {
namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}

bool slot_fits(std::uint32_t offset, std::uint32_t bytes, std::uint32_t size) {
    return (offset & 3) == 0 && offset < size && bytes <= size - offset;
}

constexpr std::uint32_t kMem2ArenaFloor = 0x90800000;  // never reserve below 8 MiB into MEM2
constexpr std::uint32_t kMem2End = 0x94000000;
constexpr std::uint32_t kReserveGranule = 0x10000;

std::string hex32(std::uint32_t v) {
    char buf[11];
    std::snprintf(buf, sizeof(buf), "0x%08x", static_cast<unsigned>(v));
    return buf;
}

}  // namespace

bool parse_resident_blob(const std::uint8_t* bytes, std::size_t length, ResidentBlob& out, std::string& error) {
    if (length < sizeof(rt_blob_header)) {
        error = "resident blob is shorter than its header";
        return false;
    }
    ResidentBlob b;
    const std::uint32_t magic = be32(bytes);
    b.version = be32(bytes + 4);
    b.size = be32(bytes + 8);
    b.context_offset = be32(bytes + 12);
    b.hook_ioctl_async_offset = be32(bytes + 16);
    b.replay_ioctl_async_offset = be32(bytes + 20);
    b.continue_ioctl_async_offset = be32(bytes + 24);
    b.complete_di_offset = be32(bytes + 28);
    if (magic != RT_BLOB_MAGIC) {
        error = "resident blob magic mismatch";
        return false;
    }
    if (b.version != RT_BLOB_VERSION) {
        error = "resident blob version " + std::to_string(b.version) + " is not supported";
        return false;
    }
    if (b.size != length || (b.size & 31) != 0) {
        error = "resident blob size field does not match the data";
        return false;
    }
    if (!slot_fits(b.context_offset, kResidentContextBytes, b.size) || (b.context_offset & 31) != 0 ||
        !slot_fits(b.hook_ioctl_async_offset, 4, b.size) || !slot_fits(b.replay_ioctl_async_offset, 16, b.size) ||
        !slot_fits(b.continue_ioctl_async_offset, 16, b.size) || !slot_fits(b.complete_di_offset, 4, b.size) ||
        b.continue_ioctl_async_offset != b.replay_ioctl_async_offset + 16) {
        error = "resident blob offsets are inconsistent";
        return false;
    }
    if (be32(bytes + b.context_offset) != RT_CONTEXT_MAGIC) {
        error = "resident blob context magic mismatch";
        return false;
    }
    out = b;
    error.clear();
    return true;
}

std::array<std::uint32_t, 4> encode_absolute_jump(unsigned reg, std::uint32_t target) {
    const std::uint32_t r = reg & 31u;
    return {
        0x3C000000u | (r << 21) | (target >> 16),           // lis r, target@h
        0x60000000u | (r << 21) | (r << 16) | (target & 0xFFFF),  // ori r, r, target@l
        0x7C0903A6u | (r << 21),                            // mtctr r
        0x4E800420u,                                        // bctr
    };
}

bool displaceable(std::uint32_t instruction, unsigned scratch_reg, std::string& why) {
    const std::uint32_t opcode = instruction >> 26;
    if (opcode == 16 || opcode == 18) {
        why = "it is a branch";
        return false;
    }
    if (opcode == 17) {
        why = "it is a system call";
        return false;
    }
    if (opcode == 19) {
        const std::uint32_t xo = (instruction >> 1) & 0x3FF;
        if (xo == 16 || xo == 528 || xo == 50) {  // bclr, bcctr, rfi
            why = "it is a branch or return";
            return false;
        }
    }
    if (opcode == 0) {
        why = "it is not a valid instruction";
        return false;
    }
    const std::uint32_t rd = (instruction >> 21) & 31;
    const std::uint32_t ra = (instruction >> 16) & 31;
    const std::uint32_t rb = (instruction >> 11) & 31;
    // Register fields are only meaningful for register-form opcodes, but
    // refusing on any match is a safe over-approximation.
    if (opcode != 24 && (rd == scratch_reg || ra == scratch_reg || rb == scratch_reg)) {
        why = "it uses the scratch register";
        return false;
    }
    why.clear();
    return true;
}

bool plan_resident_placement(std::uint32_t arena_end, std::uint32_t blob_size, std::uint32_t extra_bytes,
                             ResidentPlacement& out, std::string& error) {
    if (arena_end <= kMem2ArenaFloor || arena_end > kMem2End || (arena_end & 31) != 0) {
        error = "MEM2 arena end " + hex32(arena_end) + " is not plausible";
        return false;
    }
    const std::uint64_t needed = static_cast<std::uint64_t>(blob_size) + extra_bytes;
    const std::uint64_t reserved = (needed + kReserveGranule - 1) / kReserveGranule * kReserveGranule;
    if (reserved == 0 || reserved > arena_end - kMem2ArenaFloor) {
        error = "resident reservation does not fit above the MEM2 floor";
        return false;
    }
    // Keep the boundary on a 64 KiB line when the arena end already is.
    std::uint32_t base = static_cast<std::uint32_t>(arena_end - reserved);
    base &= ~static_cast<std::uint32_t>(kReserveGranule - 1);
    if (base < kMem2ArenaFloor) {
        error = "resident reservation does not fit above the MEM2 floor";
        return false;
    }
    out.base = base;
    out.reserved_bytes = arena_end - base;
    out.new_arena_end = base;
    error.clear();
    return true;
}

bool build_mem_payload(const std::vector<MemReplacement>& replacements, std::uint32_t payload_address,
                       std::uint64_t tag, std::vector<std::uint8_t>& payload, std::string& error) {
    if (replacements.empty()) {
        error = "no replacements to lay out";
        return false;
    }
    std::vector<std::size_t> order(replacements.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return replacements[a].virtual_offset < replacements[b].virtual_offset;
    });
    const auto align32 = [](std::size_t n) { return (n + 31) & ~static_cast<std::size_t>(31); };
    const std::size_t table_bytes = align32(rt_table_bytes(static_cast<std::uint32_t>(replacements.size())));
    std::size_t total = table_bytes;
    for (const MemReplacement& r : replacements) {
        if (r.bytes.empty()) {
            error = "a replacement is empty";
            return false;
        }
        total += align32(r.bytes.size());
    }
    if (total > 0x40000000u) {
        error = "replacement payload is too large";
        return false;
    }
    payload.assign(total, 0);
    rt_header header{};
    header.magic = RT_MAGIC;
    header.version = RT_VERSION;
    header.entry_count = static_cast<std::uint32_t>(replacements.size());
    header.sdio_fd = 0xFFFFFFFFu;
    header.tag = tag;
    std::vector<rt_entry> entries(replacements.size());
    std::size_t data_offset = table_bytes;
    std::uint64_t previous_end = 0;
    for (std::size_t k = 0; k < order.size(); ++k) {
        const MemReplacement& r = replacements[order[k]];
        if (k > 0 && r.virtual_offset < previous_end) {
            error = "replacements overlap in the virtual partition";
            return false;
        }
        previous_end = r.virtual_offset + r.bytes.size();
        rt_entry& e = entries[k];
        e.vstart = r.virtual_offset;
        e.length = r.bytes.size();
        e.source = static_cast<std::uint64_t>(payload_address) + data_offset;
        e.skip = 0;
        e.kind = RT_KIND_MEM;
        e.reserved = 0;
        std::memcpy(payload.data() + data_offset, r.bytes.data(), r.bytes.size());
        data_offset += align32(r.bytes.size());
    }
    header.entries_crc = rt_crc32(entries.data(), entries.size() * sizeof(rt_entry));
    std::memcpy(payload.data(), &header, sizeof(header));
    std::memcpy(payload.data() + sizeof(header), entries.data(), entries.size() * sizeof(rt_entry));
    const int status = rt_validate(reinterpret_cast<const rt_header*>(payload.data()), table_bytes);
    if (status != RT_OK) {
        error = "built table failed validation: " + std::to_string(status);
        return false;
    }
    error.clear();
    return true;
}

bool plan_virtual_window(Fst& fst, const std::vector<VirtualFile>& files, std::vector<MemReplacement>& replacements,
                         std::uint64_t& window_end, std::string& error) {
    constexpr std::uint64_t kWindowEnd = 0x400000000ull;  // word 0x100000000: past the 32-bit word space
    std::uint64_t next = kVirtualWindowStart;
    for (const VirtualFile& f : files) {
        const std::uint32_t index = fst.find(f.disc_path, false);
        if (index == Fst::npos) {
            error = "no such disc file '" + f.disc_path + "'";
            return false;
        }
        if (fst.entries()[index].is_directory) {
            error = "'" + f.disc_path + "' is a directory";
            return false;
        }
        const std::uint64_t size = f.bytes.size();
        const std::uint64_t padded = (size + 31) & ~std::uint64_t(31);
        if (padded > 0xFFFFFFFFull) {  // the FST size field is 32 bits
            error = "'" + f.disc_path + "' is too large for an FST entry";
            return false;
        }
        if (padded == 0 || next + padded > kWindowEnd) {
            error = "virtual window is full";
            return false;
        }
        if (!fst.set_file_extent(index, next, static_cast<std::uint32_t>(size), error)) return false;
        MemReplacement r;
        r.virtual_offset = next;
        r.bytes = f.bytes;
        r.bytes.resize(static_cast<std::size_t>(padded), 0);
        replacements.push_back(std::move(r));
        next += padded;
    }
    window_end = next;
    error.clear();
    return true;
}

}  // namespace riftwii
