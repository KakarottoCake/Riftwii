// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gctgc.hpp"
#include "riftwii/byteorder.hpp"

namespace riftwii {

namespace {

void write_be32(std::uint8_t* p, std::uint32_t value) {
    p[0] = static_cast<std::uint8_t>(value >> 24);
    p[1] = static_cast<std::uint8_t>(value >> 16);
    p[2] = static_cast<std::uint8_t>(value >> 8);
    p[3] = static_cast<std::uint8_t>(value);
}

// [offset, offset + size) within [0, limit), without overflowing.
bool within(std::uint64_t offset, std::uint64_t size, std::uint64_t limit) {
    return offset <= limit && size <= limit - offset;
}

constexpr std::size_t kFstEntryBytes = 12;

// The entries of a GameCube FST: the root's third word is their count, and
// the names' table follows them. Bounded by the table's own size.
bool fst_entry_count(const std::vector<std::uint8_t>& fst, std::uint32_t& count) {
    if (fst.size() < kFstEntryBytes) return false;
    const bool root_is_directory = fst[0] == 1;
    count = be32(&fst[8]);
    return root_is_directory && count >= 1 && count <= fst.size() / kFstEntryBytes;
}

// Whether the name at `name_offset` in the names' table ends in `suffix`
// (lower case, compared in any case).
bool name_ends_in(const std::vector<std::uint8_t>& fst, std::size_t names_at, std::uint32_t name_offset,
                  const std::string& suffix) {
    const std::size_t start = names_at + name_offset;
    if (name_offset >= fst.size() || start >= fst.size()) return false;
    std::size_t end = start;
    while (end < fst.size() && fst[end] != 0) ++end;
    if (end == fst.size() || end - start < suffix.size()) return false;  // not ended inside the table, or too short
    auto lower = [](std::uint8_t c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); };
    const std::size_t suffix_at = end - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        if (lower(fst[suffix_at + i]) != suffix[i]) return false;
    }
    return true;
}

}  // namespace

bool parse_gc_tgc(const std::uint8_t* header, std::size_t header_size, std::uint32_t disc_offset,
                  std::uint32_t tgc_bytes, std::uint64_t image_bytes, GcTgc& out, std::string& why) {
    if (header == nullptr || header_size < kGcTgcHeaderBytes) {
        why = "the TGC's header is short";
        return false;
    }
    if (be32(header) != kGcTgcMagic) {
        why = "the file is not a TGC image (no TGC magic)";
        return false;
    }
    // A GameCube disc's offsets are 32-bit: the TGC, and so every file the
    // rebased FST names, ends below 4 GB.
    const std::uint64_t disc_limit = image_bytes < 0xFFFFFFFFull ? image_bytes : 0xFFFFFFFFull;
    if (!within(disc_offset, tgc_bytes, disc_limit)) {
        why = "the TGC reaches past the image's end";
        return false;
    }
    GcTgc tgc;
    tgc.disc_offset = disc_offset;
    tgc.bytes = tgc_bytes;
    tgc.header_bytes = be32(header + 0x08);
    tgc.disc_header_bytes = be32(header + 0x0C);
    tgc.fst_offset = be32(header + 0x10);
    tgc.fst_size = be32(header + 0x14);
    tgc.fst_max = be32(header + 0x18);
    tgc.dol_offset = be32(header + 0x1C);
    tgc.dol_size = be32(header + 0x20);
    tgc.file_area_offset = be32(header + 0x24);
    tgc.file_area_virtual = be32(header + 0x34);

    // The packed disc's header: boot.bin (0x440 bytes) and BI2 (0x2000),
    // which RiftWii reads as it reads them from a disc of its own.
    constexpr std::uint32_t kBootAndBi2 = 0x440 + 0x2000;
    if (tgc.header_bytes < kGcTgcHeaderBytes || tgc.header_bytes % 4 != 0 ||
        tgc.disc_header_bytes < kBootAndBi2 || !within(tgc.header_bytes, tgc.disc_header_bytes, tgc_bytes)) {
        why = "the TGC's packed disc header is not within it";
        return false;
    }
    // A table smaller than its root entry is not one.
    if (tgc.fst_size < kFstEntryBytes || tgc.fst_size > kGcTgcFstLimit || !within(tgc.fst_offset, tgc.fst_size, tgc_bytes)) {
        why = "the TGC's FST is not within it";
        return false;
    }
    if (tgc.fst_max < tgc.fst_size) tgc.fst_max = tgc.fst_size;  // as the loader does for a disc's own boot.bin
    if (tgc.fst_max > kGcTgcFstLimit) {
        why = "the TGC's FST asks for more memory than a game has";
        return false;
    }
    // A DOL's header alone is 0x100 bytes.
    if (tgc.dol_size < 0x100 || !within(tgc.dol_offset, tgc.dol_size, tgc_bytes)) {
        why = "the TGC's program is not within it";
        return false;
    }
    if (tgc.file_area_offset > tgc_bytes) {
        why = "the TGC's file area starts past its end";
        return false;
    }
    out = tgc;
    return true;
}

bool gc_tgc_dol_fits(const std::uint8_t* dol_header, std::size_t header_size, std::uint32_t dol_size,
                     std::string& why) {
    if (dol_header == nullptr || header_size < 0x100) {
        why = "the TGC's program header is short";
        return false;
    }
    // 7 text and 11 data sections: offsets at 0x00, sizes at 0x90.
    for (int section = 0; section < 18; ++section) {
        const std::uint32_t offset = be32(dol_header + 4 * section);
        const std::uint32_t size = be32(dol_header + 0x90 + 4 * section);
        if (size == 0) continue;
        if (!within(offset, size, dol_size)) {
            why = "a section of the TGC's program reaches past the program's end";
            return false;
        }
    }
    return true;
}

bool gc_tgc_rebase_fst(std::vector<std::uint8_t>& fst, const GcTgc& tgc, std::string& why) {
    std::uint32_t count = 0;
    if (!fst_entry_count(fst, count)) {
        why = "the TGC's FST is not whole";
        return false;
    }
    // The shift from where the FST counts its files to where they are on
    // the outer disc. Signed: the virtual start may lie past the real one.
    const std::int64_t shift = std::int64_t(tgc.disc_offset) + std::int64_t(tgc.file_area_offset) -
                               std::int64_t(tgc.file_area_virtual);
    const std::uint64_t tgc_end = std::uint64_t(tgc.disc_offset) + tgc.bytes;
    std::vector<std::uint8_t> rebased = fst;
    for (std::uint32_t i = 1; i < count; ++i) {
        std::uint8_t* entry = &rebased[std::size_t(i) * kFstEntryBytes];
        const std::uint8_t kind = entry[0];
        if (kind == 1) continue;  // a directory: its words are entry indices
        if (kind != 0) {
            why = "the TGC's FST has an entry that is neither a file nor a directory";
            return false;
        }
        const std::uint32_t size = be32(entry + 8);
        const std::int64_t placed = std::int64_t(be32(entry + 4)) + shift;
        if (size == 0) {
            write_be32(entry + 4, tgc.disc_offset);
            continue;
        }
        if (placed < std::int64_t(tgc.disc_offset) || !within(std::uint64_t(placed), size, tgc_end)) {
            why = "a file of the TGC's FST (entry " + std::to_string(i) + ") lies outside the TGC";
            return false;
        }
        write_be32(entry + 4, static_cast<std::uint32_t>(placed));
    }
    fst.swap(rebased);
    return true;
}

std::vector<GcFstFile> find_gc_tgc_files(const std::vector<std::uint8_t>& fst, std::size_t max) {
    return find_gc_fst_files_ending(fst, ".tgc", max);
}

std::vector<GcFstFile> find_gc_fst_files_ending(const std::vector<std::uint8_t>& fst, const std::string& suffix,
                                                std::size_t max) {
    std::vector<GcFstFile> found;
    std::uint32_t count = 0;
    if (!fst_entry_count(fst, count)) return found;
    const std::size_t names_at = std::size_t(count) * kFstEntryBytes;
    for (std::uint32_t i = 1; i < count && found.size() < max; ++i) {
        const std::uint8_t* entry = &fst[std::size_t(i) * kFstEntryBytes];
        if (entry[0] != 0) continue;
        const std::uint32_t name_offset = be32(entry) & 0x00FFFFFFu;
        if (!name_ends_in(fst, names_at, name_offset, suffix)) continue;
        GcFstFile file;
        file.offset = be32(entry + 4);
        file.size = be32(entry + 8);
        found.push_back(file);
    }
    return found;
}

bool find_gc_fst_file_at(const std::vector<std::uint8_t>& fst, std::uint32_t offset, GcFstFile& out) {
    std::uint32_t count = 0;
    if (!fst_entry_count(fst, count)) return false;
    for (std::uint32_t i = 1; i < count; ++i) {
        const std::uint8_t* entry = &fst[std::size_t(i) * kFstEntryBytes];
        if (entry[0] != 0 || be32(entry + 4) != offset) continue;
        const std::uint32_t size = be32(entry + 8);
        if (size == 0) continue;
        out.offset = offset;
        out.size = size;
        return true;
    }
    return false;
}


namespace {

// PowerPC instruction fields (big-endian words).
unsigned opcode(std::uint32_t insn) { return insn >> 26; }
unsigned reg_d(std::uint32_t insn) { return (insn >> 21) & 31u; }
unsigned reg_a(std::uint32_t insn) { return (insn >> 16) & 31u; }
std::int32_t simm(std::uint32_t insn) { return static_cast<std::int16_t>(insn & 0xFFFFu); }

bool is_stwx(std::uint32_t insn) { return opcode(insn) == 31 && ((insn >> 1) & 0x3FFu) == 151; }
bool is_li_zero(std::uint32_t insn, unsigned reg) { return insn == (0x38000000u | (reg << 21)); }

// How far apart the steps of the list's run may be, in instructions.
constexpr std::size_t kNearSteps = 3;
constexpr std::size_t kNearList = 12;

// The list's address when the words before `end` (a stwx storing the
// list's 0) are the run find_gc_tgc_menu_args describes; 0 when not.
std::uint32_t menu_args_ending_at(const std::uint8_t* bytes, std::size_t end) {
    const auto word = [&](std::size_t i) { return be32(bytes + i * 4); };
    const std::uint32_t last = word(end);
    const unsigned zero_reg = reg_d(last);
    const unsigned list_reg = reg_a(last);
    // li rZ,0 just before.
    std::size_t li = end;
    for (std::size_t back = 1; back <= kNearSteps && back <= end && li == end; ++back)
        if (is_li_zero(word(end - back), zero_reg)) li = end - back;
    if (li == end) return 0;
    // An option stored into the same list just before that, from r13's data.
    std::size_t option = li;
    for (std::size_t back = 1; back <= kNearSteps && back <= li && option == li; ++back) {
        const std::uint32_t store = word(li - back);
        if (!is_stwx(store) || reg_a(store) != list_reg) continue;
        const std::size_t at = li - back;
        for (std::size_t from = 1; from <= kNearSteps && from <= at; ++from) {
            const std::uint32_t load = word(at - from);
            if (opcode(load) == 14 && reg_a(load) == 13 && reg_d(load) == reg_d(store)) {
                option = at;
                break;
            }
        }
    }
    if (option == li) return 0;
    // The list's address: addi rList,rB,lo after lis rB,hi.
    for (std::size_t back = 1; back <= kNearList && back <= option; ++back) {
        const std::uint32_t add = word(option - back);
        if (opcode(add) != 14 || reg_d(add) != list_reg || reg_a(add) == 0) continue;
        const std::size_t at = option - back;
        for (std::size_t from = 1; from <= kNearSteps && from <= at; ++from) {
            const std::uint32_t high = word(at - from);
            if (opcode(high) == 15 && reg_a(high) == 0 && reg_d(high) == reg_a(add)) {
                const std::uint32_t address = ((high & 0xFFFFu) << 16) + static_cast<std::uint32_t>(simm(add));
                // A word in MEM1, as the menu's globals are.
                const bool in_mem1 = address >= 0x80000000u && address < 0x81800000u && address % 4 == 0;
                return in_mem1 ? address : 0;
            }
        }
        return 0;  // rList set otherwise: not this run
    }
    return 0;
}

// The routine the list is handed to, in the instructions after the run's
// end (`end`): the first `bl` whose r4 was just loaded with the list's
// address (other calls come between: the Zelda disc's menu stops its
// music and the disc first); 0 when none comes before the function
// returns, or soon.
constexpr std::size_t kNearCall = 64;
constexpr std::uint32_t kBlr = 0x4E800020u;

std::uint32_t menu_start_after(const CodeRange& range, std::size_t end, std::uint32_t list) {
    const std::size_t count = range.size / 4;
    const auto word = [&](std::size_t i) { return be32(range.bytes + i * 4); };
    for (std::size_t i = end + 1; i < count && i <= end + kNearCall; ++i) {
        const std::uint32_t call = word(i);
        if (call == kBlr) return 0;
        if (opcode(call) != 18 || (call & 3u) != 1) continue;  // bl
        // addi r4,r4,lo with lis r4,hi before it, the list's address.
        for (std::size_t back = 1; back <= kNearSteps && back <= i; ++back) {
            const std::uint32_t add = word(i - back);
            if (opcode(add) != 14 || reg_d(add) != 4 || reg_a(add) != 4) continue;
            for (std::size_t from = 1; from <= kNearSteps && from <= i - back; ++from) {
                const std::uint32_t high = word(i - back - from);
                if (opcode(high) != 15 || reg_d(high) != 4 || reg_a(high) != 0) continue;
                if (((high & 0xFFFFu) << 16) + static_cast<std::uint32_t>(simm(add)) != list) return 0;
                std::int32_t offset = static_cast<std::int32_t>(call & 0x03FFFFFCu);
                if (offset & 0x02000000) offset -= 0x04000000;  // sign of the 26-bit field
                return range.address + static_cast<std::uint32_t>(i * 4) + static_cast<std::uint32_t>(offset);
            }
        }
    }
    return 0;
}

}  // namespace

GcTgcMenuStart find_gc_tgc_menu_start(const std::vector<CodeRange>& text) {
    GcTgcMenuStart found;
    for (const CodeRange& range : text) {
        const std::size_t count = range.size / 4;
        for (std::size_t i = 0; i < count; ++i) {
            if (!is_stwx(be32(range.bytes + i * 4))) continue;
            GcTgcMenuStart here;
            here.args = menu_args_ending_at(range.bytes, i);
            if (here.args == 0) continue;
            here.start = menu_start_after(range, i, here.args);
            if (here.start == 0) continue;
            // Two different places: not sure which is the menu's.
            if (found.args != 0 && (found.args != here.args || found.start != here.start)) return GcTgcMenuStart();
            found = here;
        }
    }
    return found;
}

}  // namespace riftwii
