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

constexpr std::uint32_t kMem1Low = 0x80004000;         // below: the SDK's globals and vectors
constexpr std::uint32_t kMem1End = 0x81800000;
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
    for (std::uint32_t i = 0; i < RT_IPC_ENTRIES; ++i) {
        b.hook_offsets[i] = be32(bytes + 16 + i * 4);
        b.replay_offsets[i] = be32(bytes + 16 + RT_IPC_ENTRIES * 4 + i * 4);
        b.continue_offsets[i] = be32(bytes + 16 + RT_IPC_ENTRIES * 8 + i * 4);
    }
    b.complete_di_offset = be32(bytes + 16 + RT_IPC_ENTRIES * 12);
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
        !slot_fits(b.complete_di_offset, 4, b.size)) {
        error = "resident blob offsets are inconsistent";
        return false;
    }
    for (std::uint32_t i = 0; i < RT_IPC_ENTRIES; ++i) {
        if (!slot_fits(b.hook_offsets[i], 4, b.size) || !slot_fits(b.replay_offsets[i], 16, b.size) ||
            !slot_fits(b.continue_offsets[i], 16, b.size) || (b.replay_offsets[i] & 15) != 0 ||
            (b.continue_offsets[i] & 15) != 0 || b.continue_offsets[i] != b.replay_offsets[i] + 16 ||
            b.replay_offsets[i] < sizeof(rt_blob_header) ||
            (b.replay_offsets[i] < b.context_offset + kResidentContextBytes &&
             b.context_offset < b.replay_offsets[i] + 32)) {
            error = "resident blob IPC table offsets are inconsistent";
            return false;
        }
        for (std::uint32_t j = 0; j < i; ++j) {
            if (b.replay_offsets[i] < b.replay_offsets[j] + 32 && b.replay_offsets[j] < b.replay_offsets[i] + 32) {
                error = "resident blob IPC replay slots overlap";
                return false;
            }
        }
    }
    if (be32(bytes + b.context_offset) != RT_CONTEXT_MAGIC) {
        error = "resident blob context magic mismatch";
        return false;
    }
    b.hook_ioctl_async_offset = b.hook_offsets[RT_IPC_ASYNC_IOCTL];
    b.replay_ioctl_async_offset = b.replay_offsets[RT_IPC_ASYNC_IOCTL];
    b.continue_ioctl_async_offset = b.continue_offsets[RT_IPC_ASYNC_IOCTL];
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
    if (opcode == 31) {
        // Special registers: the trampoline restores LR before the replay,
        // but CTR is clobbered by the jump that follows it and XER by the
        // C handler. Only LR moves are safe.
        const std::uint32_t xo = (instruction >> 1) & 0x3FF;
        const std::uint32_t spr = ((instruction >> 16) & 31) | (((instruction >> 11) & 31) << 5);
        if ((xo == 339 || xo == 467) && spr != 8) {
            why = "it reads or writes a special register other than LR";
            return false;
        }
    }
    if (instruction == 0x60000000u) {  // nop (ori r0,r0,0) names no register
        why.clear();
        return true;
    }
    const std::uint32_t rd = (instruction >> 21) & 31;
    const std::uint32_t ra = (instruction >> 16) & 31;
    const std::uint32_t rb = (instruction >> 11) & 31;
    // rD/rS and rA are registers in every integer form; the rB field is one
    // only in register forms (opcode 31, rlwnm, paired-single 4), an
    // immediate elsewhere.
    const bool rb_is_register = opcode == 31 || opcode == 23 || opcode == 4;
    if (rd == scratch_reg || ra == scratch_reg || (rb_is_register && rb == scratch_reg)) {
        why = "it uses the scratch register";
        return false;
    }
    why.clear();
    return true;
}

bool plan_resident_placement(std::uint32_t arena1_hi, std::uint32_t mem1_floor, std::uint32_t arena2_end,
                             std::uint32_t blob_size, std::uint32_t extra_bytes, ResidentPlacement& out,
                             std::string& error) {
    ResidentPlacement p;
    // Code: right below the MEM1 arena top, on a 32-byte line (the blob's
    // context and DMA buffers are laid out for one).
    if (arena1_hi <= kMem1Low || arena1_hi > kMem1End || (arena1_hi & 31) != 0) {
        error = "MEM1 arena top " + hex32(arena1_hi) + " is not plausible";
        return false;
    }
    if (blob_size == 0 || (blob_size & 31) != 0 || blob_size > arena1_hi - kMem1Low) {
        error = "resident blob size is not a positive multiple of 32 that fits";
        return false;
    }
    p.code_base = arena1_hi - blob_size;
    if (p.code_base < mem1_floor) {
        error = "no room for the resident code between " + hex32(mem1_floor) + " and the MEM1 arena top " +
                hex32(arena1_hi);
        return false;
    }
    p.code_bytes = blob_size;
    p.new_arena1_hi = p.code_base;

    // Data: the top of the MEM2 arena in 64 KiB granules, only when needed.
    p.new_arena2_end = arena2_end;
    if (extra_bytes != 0) {
        if (arena2_end <= kMem2ArenaFloor || arena2_end > kMem2End || (arena2_end & 31) != 0) {
            error = "MEM2 arena end " + hex32(arena2_end) + " is not plausible";
            return false;
        }
        const std::uint64_t reserved =
            (static_cast<std::uint64_t>(extra_bytes) + kReserveGranule - 1) / kReserveGranule * kReserveGranule;
        if (reserved > arena2_end - kMem2ArenaFloor) {
            error = "resident data does not fit above the MEM2 floor";
            return false;
        }
        // Keep the boundary on a 64 KiB line when the arena end already is.
        std::uint32_t base = static_cast<std::uint32_t>(arena2_end - reserved);
        base &= ~static_cast<std::uint32_t>(kReserveGranule - 1);
        if (base < kMem2ArenaFloor) {
            error = "resident data does not fit above the MEM2 floor";
            return false;
        }
        p.data_base = base;
        p.data_bytes = arena2_end - base;
        p.new_arena2_end = base;
    }
    out = p;
    error.clear();
    return true;
}

bool PayloadPieces::needs_sd() const {
    if (!sd.empty()) return true;
    for (const rt_entry& e : entries) {
        if (e.kind == RT_KIND_SD) return true;
    }
    return false;
}

bool build_payload(const PayloadPieces& pieces_in, std::uint32_t payload_address, std::uint64_t tag,
                   std::uint32_t sdio_fd, std::vector<std::uint8_t>& payload, std::string& error) {
    const std::vector<MemReplacement>& mem = pieces_in.mem;
    const std::vector<SdReplacement>& sd = pieces_in.sd;
    const std::vector<DiscReplacement>& disc = pieces_in.disc;
    if (pieces_in.empty()) {
        error = "no replacements to lay out";
        return false;
    }
    const auto align32 = [](std::size_t n) { return (n + 31) & ~static_cast<std::size_t>(31); };

    // Every entry first; MEM data is laid out in table order below.
    struct Piece {
        rt_entry entry;
        const std::vector<std::uint8_t>* bytes = nullptr;
    };
    std::vector<Piece> pieces;
    std::size_t data_bytes = 0;
    for (const MemReplacement& r : mem) {
        if (r.bytes.empty()) {
            error = "a replacement is empty";
            return false;
        }
        Piece p;
        p.entry.vstart = r.virtual_offset;
        p.entry.length = r.bytes.size();
        p.entry.source = 0;
        p.entry.skip = 0;
        p.entry.kind = RT_KIND_MEM;
        p.entry.reserved = 0;
        p.bytes = &r.bytes;
        pieces.push_back(p);
        data_bytes += align32(r.bytes.size());
    }
    for (const SdReplacement& r : sd) {
        std::uint64_t at = r.virtual_offset;
        if (r.runs.empty()) {
            error = "an SD replacement has no runs";
            return false;
        }
        for (const PlacedRun& run : r.runs) {
            if (run.kind != RT_KIND_SD || run.length == 0 || run.skip >= RT_SECTOR_BYTES) {
                error = "an SD replacement run is malformed";
                return false;
            }
            Piece p;
            p.entry.vstart = at;
            p.entry.length = run.length;
            p.entry.source = run.source;
            p.entry.skip = run.skip;
            p.entry.kind = RT_KIND_SD;
            p.entry.reserved = 0;
            pieces.push_back(p);
            at += run.length;
        }
    }
    for (const DiscReplacement& r : disc) {
        if (r.length == 0) {
            error = "a disc replacement is empty";
            return false;
        }
        Piece p;
        p.entry.vstart = r.virtual_offset;
        p.entry.length = r.length;
        p.entry.source = r.disc_offset;
        p.entry.skip = 0;
        p.entry.kind = RT_KIND_DISC;
        p.entry.reserved = 0;
        pieces.push_back(p);
    }
    for (const rt_entry& e : pieces_in.entries) {
        if (e.kind != RT_KIND_SD && e.kind != RT_KIND_DISC && e.kind != RT_KIND_ZERO) {
            error = "a ready-made table entry is not SD, DISC or ZERO";
            return false;
        }
        if (e.length == 0) {
            error = "a ready-made table entry is empty";
            return false;
        }
        Piece p;
        p.entry = e;
        p.entry.reserved = 0;
        pieces.push_back(p);
    }
    std::sort(pieces.begin(), pieces.end(),
              [](const Piece& a, const Piece& b) { return a.entry.vstart < b.entry.vstart; });
    const std::size_t table_bytes = align32(rt_table_bytes(static_cast<std::uint32_t>(pieces.size())));
    if (table_bytes + data_bytes > 0x40000000u) {
        error = "replacement payload is too large";
        return false;
    }
    payload.assign(table_bytes + data_bytes, 0);
    rt_header header{};
    header.magic = RT_MAGIC;
    header.version = RT_VERSION;
    header.entry_count = static_cast<std::uint32_t>(pieces.size());
    header.sdio_fd = sdio_fd;
    header.tag = tag;
    std::vector<rt_entry> entries(pieces.size());
    std::uint64_t previous_end = 0;
    std::size_t data_offset = table_bytes;
    for (std::size_t k = 0; k < pieces.size(); ++k) {
        const Piece& p = pieces[k];
        if (k > 0 && p.entry.vstart < previous_end) {
            error = "replacements overlap in the virtual partition";
            return false;
        }
        previous_end = p.entry.vstart + p.entry.length;
        entries[k] = p.entry;
        if (p.bytes) {
            entries[k].source = static_cast<std::uint64_t>(payload_address) + data_offset;
            std::memcpy(payload.data() + data_offset, p.bytes->data(), p.bytes->size());
            data_offset += align32(p.bytes->size());
        }
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

bool build_mem_payload(const std::vector<MemReplacement>& replacements, std::uint32_t payload_address,
                       std::uint64_t tag, std::vector<std::uint8_t>& payload, std::string& error) {
    PayloadPieces pieces;
    pieces.mem = replacements;
    return build_payload(pieces, payload_address, tag, 0xFFFFFFFFu, payload, error);
}

std::uint64_t VirtualFile::size() const {
    if (!bytes.empty()) return bytes.size();
    std::uint64_t total = 0;
    for (const PlacedRun& run : sd_runs) total += run.length;
    return total;
}

bool plan_virtual_window(Fst& fst, const std::vector<VirtualFile>& files, std::vector<MemReplacement>& mem,
                         std::vector<SdReplacement>& sd, std::vector<DiscReplacement>& disc,
                         std::uint64_t& window_cursor, std::string& error) {
    constexpr std::uint64_t kWindowEnd = 0x400000000ull;  // word 0x100000000: past the 32-bit word space
    std::uint64_t next = window_cursor;
    if (next < kVirtualWindowStart || (next & 31) != 0) {
        error = "virtual window cursor is not in the window";
        return false;
    }
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
        const FstEntry original_entry = fst.entries()[index];
        const std::uint64_t size = f.original ? original_entry.size : f.size();
        const std::uint64_t padded = (size + 31) & ~std::uint64_t(31);
        if (padded > 0xFFFFFFFFull) {  // the FST size field is 32 bits
            error = "'" + f.disc_path + "' is too large for an FST entry";
            return false;
        }
        if (padded == 0) {
            error = "'" + f.disc_path + "' has no content";
            return false;
        }
        if (next + padded > kWindowEnd) {
            error = "virtual window is full";
            return false;
        }
        if (!fst.set_file_extent(index, next, static_cast<std::uint32_t>(size), error)) return false;
        if (f.original) {
            DiscReplacement r;
            r.virtual_offset = next;
            r.disc_offset = original_entry.offset;
            r.length = size;
            disc.push_back(r);
        } else if (!f.bytes.empty()) {
            MemReplacement r;
            r.virtual_offset = next;
            r.bytes = f.bytes;
            r.bytes.resize(static_cast<std::size_t>(padded), 0);
            mem.push_back(std::move(r));
        } else {
            SdReplacement r;
            r.virtual_offset = next;
            r.runs = f.sd_runs;
            sd.push_back(std::move(r));
        }
        next += padded;
    }
    window_cursor = next;
    error.clear();
    return true;
}

}  // namespace riftwii
