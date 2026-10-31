// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gctgc.hpp"

namespace riftwii {

namespace {

std::uint32_t read_be32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | p[3];
}

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
    count = read_be32(&fst[8]);
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
    if (read_be32(header) != kGcTgcMagic) {
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
    tgc.header_bytes = read_be32(header + 0x08);
    tgc.disc_header_bytes = read_be32(header + 0x0C);
    tgc.fst_offset = read_be32(header + 0x10);
    tgc.fst_size = read_be32(header + 0x14);
    tgc.fst_max = read_be32(header + 0x18);
    tgc.dol_offset = read_be32(header + 0x1C);
    tgc.dol_size = read_be32(header + 0x20);
    tgc.file_area_offset = read_be32(header + 0x24);
    tgc.file_area_virtual = read_be32(header + 0x34);

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
        const std::uint32_t offset = read_be32(dol_header + 4 * section);
        const std::uint32_t size = read_be32(dol_header + 0x90 + 4 * section);
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
        const std::uint32_t size = read_be32(entry + 8);
        const std::int64_t placed = std::int64_t(read_be32(entry + 4)) + shift;
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
        const std::uint32_t name_offset = read_be32(entry) & 0x00FFFFFFu;
        if (!name_ends_in(fst, names_at, name_offset, suffix)) continue;
        GcFstFile file;
        file.offset = read_be32(entry + 4);
        file.size = read_be32(entry + 8);
        found.push_back(file);
    }
    return found;
}

bool find_gc_fst_file_at(const std::vector<std::uint8_t>& fst, std::uint32_t offset, GcFstFile& out) {
    std::uint32_t count = 0;
    if (!fst_entry_count(fst, count)) return false;
    for (std::uint32_t i = 1; i < count; ++i) {
        const std::uint8_t* entry = &fst[std::size_t(i) * kFstEntryBytes];
        if (entry[0] != 0 || read_be32(entry + 4) != offset) continue;
        const std::uint32_t size = read_be32(entry + 8);
        if (size == 0) continue;
        out.offset = offset;
        out.size = size;
        return true;
    }
    return false;
}

}  // namespace riftwii
