// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gcprograms.hpp"

#include <algorithm>

namespace riftwii {

namespace {

std::uint32_t read_be32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | p[3];
}

std::uint16_t read_be16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] << 8 | p[1]);
}

// The ELF header's fields (the System V ABI's PowerPC supplement).
constexpr std::size_t kElfHeaderBytes = 0x34;
constexpr std::size_t kProgramHeaderBytes = 0x20;
constexpr std::uint16_t kElfExecutable = 2;      // e_type ET_EXEC
constexpr std::uint16_t kElfPowerPc = 20;        // e_machine EM_PPC
// EA's linker left e_machine 0 (EM_NONE) in three of 007: NightFire's four
// programs (its loaders and driving.elf), and 20 in Nightfire.elf.
constexpr std::uint16_t kElfNoMachine = 0;
constexpr std::uint32_t kSegmentLoad = 1;        // p_type PT_LOAD
constexpr std::uint32_t kSegmentExecutable = 1;  // p_flags PF_X
constexpr unsigned kMaxProgramHeaders = 32;

// [offset, offset + size) within [0, limit), without overflowing.
bool within(std::uint64_t offset, std::uint64_t size, std::uint64_t limit) {
    return offset <= limit && size <= limit - offset;
}

bool is_relative_branch(std::uint32_t word) {
    const unsigned opcode = word >> 26;
    return opcode == 16 || opcode == 18;  // bc, b (and their l/a forms)
}

// Whether a word may touch r12 or CTR. The blob uses both around the two
// words it replays: gc_hook_program keeps the caller's LR in CTR on the way
// in, and the way back to the third word (gc_continue_program: lis/ori r12,
// mtctr, bctr) overwrites them before that word runs. So a word that reads
// either sees the blob's value, and one that sets either loses it. Any
// word naming r12 in its rD/rS or rA field (or rB, for the X-forms of
// opcodes 19 and 31) counts, a little more than needed, and so do lmw from
// r12 or below and lswi/lswx, which load runs of registers, and every move
// to or from CTR and bcctr. Every program seen starts with `lis r1` and
// `ori r1,r1`.
bool may_touch_r12_or_ctr(std::uint32_t word) {
    const unsigned opcode = word >> 26;
    const unsigned field_d = (word >> 21) & 31, field_a = (word >> 16) & 31, field_b = (word >> 11) & 31;
    const unsigned extended = (word >> 1) & 0x3FF;
    const bool x_form = opcode == 19 || opcode == 31;
    const bool names_r12 = field_d == 12 || field_a == 12 || (x_form && field_b == 12);
    const bool lmw_through_r12 = opcode == 46 && field_d <= 12;
    const bool load_string = opcode == 31 && (extended == 597 || extended == 533);  // lswi, lswx
    const bool ctr_move = (word & 0xFC1FFFFE) == 0x7C0903A6 ||                   // mtspr 9,rS
                          (word & 0xFC1FFFFE) == 0x7C0902A6;                     // mfspr rD,9
    const bool bcctr = opcode == 19 && extended == 528;
    return names_r12 || lmw_through_r12 || load_string || ctr_move || bcctr;
}

}  // namespace

bool parse_gc_elf(const std::uint8_t* head, std::size_t head_bytes, std::uint32_t file_bytes, std::uint32_t floor,
                  std::uint32_t ceiling, GcElfProgram& out, std::string& why) {
    // Filled here and handed over whole: a refused file gives no segments.
    out = GcElfProgram();
    GcElfProgram program;
    if (head == nullptr || head_bytes < kElfHeaderBytes) {
        why = "the file is shorter than an ELF header";
        return false;
    }
    const bool elf_magic = head[0] == 0x7F && head[1] == 'E' && head[2] == 'L' && head[3] == 'F';
    if (!elf_magic) {
        why = "the file is not an ELF program";
        return false;
    }
    const bool big_endian_32 = head[4] == 1 && head[5] == 2;  // ELFCLASS32, ELFDATA2MSB
    const std::uint16_t machine = read_be16(head + 0x12);
    const bool powerpc = machine == kElfPowerPc || machine == kElfNoMachine;
    if (!big_endian_32 || read_be16(head + 0x10) != kElfExecutable || !powerpc) {
        why = "the file is not a 32-bit PowerPC executable";
        return false;
    }
    const std::uint32_t entry = read_be32(head + 0x18);
    const std::uint32_t header_table = read_be32(head + 0x1C);
    const std::uint16_t header_size = read_be16(head + 0x2A);
    const std::uint16_t header_count = read_be16(head + 0x2C);
    if (header_size != kProgramHeaderBytes || header_count == 0 || header_count > kMaxProgramHeaders) {
        why = "the program's segment table is not one RiftWii reads";
        return false;
    }
    if (!within(header_table, std::uint64_t(header_count) * kProgramHeaderBytes, head_bytes)) {
        why = "the program's segment table is not in the file's first 4 KiB";
        return false;
    }
    for (unsigned i = 0; i < header_count; ++i) {
        const std::uint8_t* header = head + header_table + i * kProgramHeaderBytes;
        GcElfSegment segment;
        segment.file_offset = read_be32(header + 0x04);
        segment.address = read_be32(header + 0x08);
        segment.file_bytes = read_be32(header + 0x10);
        segment.memory_bytes = read_be32(header + 0x14);
        segment.executable = (read_be32(header + 0x18) & kSegmentExecutable) != 0;
        if (read_be32(header) != kSegmentLoad || segment.memory_bytes == 0) continue;
        if (segment.file_bytes > segment.memory_bytes || !within(segment.file_offset, segment.file_bytes, file_bytes)) {
            why = "a segment reaches past the file";
            return false;
        }
        const bool in_range = segment.address >= floor && within(segment.address, segment.memory_bytes, ceiling);
        if (!in_range || (segment.address & 3u) != 0) {
            why = "a segment is outside the memory the game has (or not word-aligned)";
            return false;
        }
        for (const GcElfSegment& other : program.segments) {
            const bool apart = segment.address >= other.address + other.memory_bytes ||
                               other.address >= segment.address + segment.memory_bytes;
            if (!apart) {
                why = "two segments overlap";
                return false;
            }
        }
        program.segments.push_back(segment);
    }
    // The blob takes over the entry's first two words (gc_program_entry_words).
    auto holds_entry = [&](const GcElfSegment& s) {
        return s.executable && (entry & 3u) == 0 && entry >= s.address && entry - s.address <= s.file_bytes &&
               s.file_bytes - (entry - s.address) >= 8;
    };
    const bool entry_in_code = std::any_of(program.segments.begin(), program.segments.end(), holds_entry);
    if (!entry_in_code) {
        why = "the program's entry is not in its code";
        return false;
    }
    program.entry = entry;
    out = program;
    return true;
}

bool gc_elf_disc_offset(const GcElfProgram& program, std::uint32_t file_disc_offset, std::uint32_t address,
                        std::uint32_t& disc_offset) {
    for (const GcElfSegment& segment : program.segments) {
        if (address < segment.address || !within(address - segment.address, 4, segment.file_bytes)) continue;
        const std::uint64_t offset =
            std::uint64_t(file_disc_offset) + segment.file_offset + (address - segment.address);
        if (offset > 0xFFFFFFFCu) return false;
        disc_offset = static_cast<std::uint32_t>(offset);
        return true;
    }
    return false;
}

void gc_changed_words(std::uint32_t address, const std::vector<std::uint8_t>& original,
                      const std::vector<std::uint8_t>& patched,
                      std::vector<std::pair<std::uint32_t, std::uint32_t>>& out) {
    const std::size_t bytes = std::min(original.size(), patched.size());
    for (std::size_t i = 0; i + 4 <= bytes; i += 4) {
        const std::uint32_t before = read_be32(&original[i]);
        const std::uint32_t after = read_be32(&patched[i]);
        if (before != after) out.emplace_back(address + static_cast<std::uint32_t>(i), after);
    }
}

bool gc_sort_disc_patches(std::vector<GcDiscPatch>& patches, std::string& why) {
    std::sort(patches.begin(), patches.end(),
              [](const GcDiscPatch& a, const GcDiscPatch& b) { return a.offset < b.offset; });
    for (std::size_t i = 1; i < patches.size(); ++i) {
        if (patches[i].offset - patches[i - 1].offset < 4) {
            why = "two patches write the same bytes of the disc";
            return false;
        }
    }
    return true;
}

bool gc_program_entry_words(std::uint32_t entry, std::uint32_t first, std::uint32_t second, unsigned index,
                            std::uint32_t hook, std::uint32_t out[2], std::string& why) {
    if (is_relative_branch(first) || is_relative_branch(second)) {
        why = "the program starts with a branch, which cannot run from elsewhere";
        return false;
    }
    if (may_touch_r12_or_ctr(first) || may_touch_r12_or_ctr(second)) {
        why = "the program's first two words may use r12 or CTR, which the blob uses around them";
        return false;
    }
    if (index > 0x7FFF) {
        why = "too many programs";
        return false;
    }
    const std::uint32_t from = entry + 4;
    const std::int64_t distance = std::int64_t(hook) - std::int64_t(from);
    if (distance < -0x02000000 || distance >= 0x02000000 || (distance & 3) != 0) {
        why = "the blob is out of a branch's reach from the program's start";
        return false;
    }
    out[0] = 0x38000000u | index;  // li r0,index
    out[1] = 0x48000000u | (static_cast<std::uint32_t>(distance) & 0x03FFFFFCu);  // b hook
    return true;
}

std::vector<std::uint32_t> gc_resident_words(const std::vector<GcResidentRegion>& base,
                                             const std::vector<std::vector<GcResidentRegion>>& programs,
                                             const std::vector<std::uint32_t>& excluded) {
    std::vector<std::uint32_t> words;
    for (std::size_t r = 0; r < base.size(); ++r) {
        const GcResidentRegion& region = base[r];
        for (std::size_t i = 0; i + 4 <= region.bytes.size(); i += 4) {
            const std::uint32_t address = region.address + static_cast<std::uint32_t>(i);
            const std::uint32_t before = read_be32(&region.bytes[i]);
            bool changed = false;
            for (const std::vector<GcResidentRegion>& state : programs) {
                const bool same_layout = r < state.size() && state[r].address == region.address &&
                                         state[r].bytes.size() == region.bytes.size();
                if (same_layout && read_be32(&state[r].bytes[i]) != before) changed = true;
            }
            const bool skip = std::find(excluded.begin(), excluded.end(), address) != excluded.end();
            if (changed && !skip) words.push_back(address);
        }
    }
    std::sort(words.begin(), words.end());
    return words;
}

std::uint32_t gc_resident_word(const std::vector<GcResidentRegion>& state, std::uint32_t address) {
    for (const GcResidentRegion& region : state) {
        if (address < region.address || !within(address - region.address, 4, region.bytes.size())) continue;
        return read_be32(&region.bytes[address - region.address]);
    }
    return 0;
}

}  // namespace riftwii
