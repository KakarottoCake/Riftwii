// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/dol.hpp"

namespace riftwii {
namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}

bool in_ram(std::uint32_t start, std::uint64_t length) {
    const std::uint64_t end = static_cast<std::uint64_t>(start) + length;
    return (start >= 0x80000000u && end <= 0x81800000ull) || (start >= 0x90000000u && end <= 0x94000000ull);
}

}  // namespace

std::uint64_t DolHeader::image_size() const {
    std::uint64_t end = kDolHeaderBytes;
    for (const DolSection& s : sections) {
        if (!s.used()) continue;
        const std::uint64_t section_end = static_cast<std::uint64_t>(s.offset) + s.size;
        if (section_end > end) end = section_end;
    }
    return end;
}

bool parse_dol_header(const std::uint8_t* bytes, std::size_t length, DolHeader& out, std::string& error) {
    if (length < kDolHeaderBytes) {
        error = "DOL header is truncated";
        return false;
    }
    DolHeader h;
    for (std::size_t i = 0; i < kDolSections; ++i) {
        DolSection& s = h.sections[i];
        s.offset = be32(bytes + i * 4);
        s.address = be32(bytes + 0x48 + i * 4);
        s.size = be32(bytes + 0x90 + i * 4);
        s.is_text = i < kDolTextSections;
        if (!s.used()) continue;
        if (s.offset < kDolHeaderBytes) {
            error = "DOL section " + std::to_string(i) + " overlaps the header";
            return false;
        }
        if ((s.address & 3) != 0 || !in_ram(s.address, s.size)) {
            error = "DOL section " + std::to_string(i) + " does not load inside RAM";
            return false;
        }
    }
    // No two sections load over each other: the loader would keep only
    // whichever it copied last.
    for (std::size_t i = 0; i < kDolSections; ++i) {
        const DolSection& a = h.sections[i];
        if (!a.used()) continue;
        for (std::size_t j = i + 1; j < kDolSections; ++j) {
            const DolSection& b = h.sections[j];
            if (!b.used()) continue;
            if (std::uint64_t(a.address) < std::uint64_t(b.address) + b.size &&
                std::uint64_t(b.address) < std::uint64_t(a.address) + a.size) {
                error = "DOL sections " + std::to_string(i) + " and " + std::to_string(j) + " load over each other";
                return false;
            }
        }
    }
    h.bss_address = be32(bytes + 0xD8);
    h.bss_size = be32(bytes + 0xDC);
    h.entry = be32(bytes + 0xE0);
    if (h.bss_size != 0 && !in_ram(h.bss_address, h.bss_size)) {
        error = "DOL BSS does not lie inside RAM";
        return false;
    }
    bool entry_in_text = false;
    for (std::size_t i = 0; i < kDolTextSections; ++i) {
        const DolSection& s = h.sections[i];
        if (s.used() && h.entry >= s.address && h.entry < s.address + s.size) entry_in_text = true;
    }
    if (!entry_in_text) {
        error = "DOL entry point is not inside a text section";
        return false;
    }
    out = h;
    error.clear();
    return true;
}

namespace {

std::uint16_t be16(const std::uint8_t* p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }

// A load address as the cached one the Wii's CPU runs from: physical
// (0x00000000, 0x10000000) and uncached (0xC0000000, 0xD0000000)
// addresses of MEM1 and MEM2 are the same memory.
std::uint32_t cached_address(std::uint32_t a) {
    if (a < 0x01800000u || (a >= 0x10000000u && a < 0x14000000u)) return a | 0x80000000u;
    if ((a >= 0xC0000000u && a < 0xC1800000u) || (a >= 0xD0000000u && a < 0xD4000000u)) return a - 0x40000000u;
    return a;
}

bool entry_in_a_section(const DolHeader& h, bool text_only) {
    for (const DolSection& s : h.sections) {
        if (!s.used() || (text_only && !s.is_text)) continue;
        if (h.entry >= s.address && h.entry - s.address < s.size) return true;
    }
    return false;
}

// A 32-bit big-endian PowerPC ELF's loaded segments as sections.
bool parse_elf(const std::uint8_t* b, std::size_t length, DolHeader& out, std::string& error) {
    if (length < 0x34 || b[0] != 0x7F || b[1] != 'E' || b[2] != 'L' || b[3] != 'F') {
        error = "not an ELF";
        return false;
    }
    if (b[4] != 1 || b[5] != 2 || be16(b + 0x12) != 20) {
        error = "an ELF, but not a 32-bit big-endian PowerPC one";
        return false;
    }
    DolHeader h;
    h.entry = cached_address(be32(b + 0x18));
    const std::uint32_t phoff = be32(b + 0x1C);
    const std::uint32_t phentsize = be16(b + 0x2A);
    const std::uint32_t phnum = be16(b + 0x2C);
    if (phentsize < 0x20 || phnum == 0 || phnum > 64 ||
        static_cast<std::uint64_t>(phoff) + static_cast<std::uint64_t>(phentsize) * phnum > length) {
        error = "an ELF whose program headers are not where RiftWii reads";
        return false;
    }
    std::size_t text = 0, data = 0;
    for (std::uint32_t i = 0; i < phnum; ++i) {
        const std::uint8_t* ph = b + phoff + i * phentsize;
        if (be32(ph) != 1) continue;  // PT_LOAD only
        const std::uint32_t offset = be32(ph + 4);
        const std::uint32_t address = cached_address(be32(ph + 8));
        const std::uint32_t file_size = be32(ph + 16);
        const std::uint32_t memory_size = be32(ph + 20);
        const bool executable = (be32(ph + 24) & 1u) != 0;
        if (memory_size > file_size && h.bss_size == 0) {
            // The segment's tail past its bytes is zeros: the program's
            // own start-up clears it, as a DOL's BSS.
            h.bss_address = address + file_size;
            h.bss_size = memory_size - file_size;
        }
        if (file_size == 0) continue;
        if (!in_ram(address, file_size)) {
            error = "an ELF segment does not load inside RAM";
            return false;
        }
        DolSection* slot = nullptr;
        if (executable && text < kDolTextSections) slot = &h.sections[text++];
        else if (data < kDolDataSections) slot = &h.sections[kDolTextSections + data++];
        else if (text < kDolTextSections) slot = &h.sections[text++];
        if (!slot) {
            error = "an ELF with more segments than RiftWii loads";
            return false;
        }
        slot->offset = offset;
        slot->address = address;
        slot->size = file_size;
        slot->is_text = slot < &h.sections[kDolTextSections];
    }
    if (text + data == 0) {
        error = "an ELF with nothing to load";
        return false;
    }
    if (!entry_in_a_section(h, false)) {
        error = "an ELF whose entry point is in none of its segments";
        return false;
    }
    out = h;
    return true;
}

// A DOL header read leniently: what parse_dol_header refuses that a
// loader copying sections one by one still runs.
bool parse_lenient_dol(const std::uint8_t* b, std::size_t length, DolHeader& out, std::string& error) {
    if (length < kDolHeaderBytes) {
        error = "DOL header is truncated";
        return false;
    }
    DolHeader h;
    bool any = false;
    for (std::size_t i = 0; i < kDolSections; ++i) {
        DolSection& s = h.sections[i];
        s.offset = be32(b + i * 4);
        s.address = cached_address(be32(b + 0x48 + i * 4));
        s.size = be32(b + 0x90 + i * 4);
        s.is_text = i < kDolTextSections;
        if (!s.used()) continue;
        if ((s.address & 3) != 0 || !in_ram(s.address, s.size)) {
            error = "DOL section " + std::to_string(i) + " does not load inside RAM, even read leniently";
            return false;
        }
        any = true;
    }
    if (!any) {
        error = "a DOL header with no sections";
        return false;
    }
    h.bss_address = cached_address(be32(b + 0xD8));
    h.bss_size = be32(b + 0xDC);
    if (h.bss_size != 0 && !in_ram(h.bss_address, h.bss_size)) h.bss_address = h.bss_size = 0;
    h.entry = cached_address(be32(b + 0xE0));
    if (!entry_in_a_section(h, false)) {
        error = "DOL entry point is in none of its sections, even read leniently";
        return false;
    }
    out = h;
    return true;
}

}  // namespace

bool parse_channel_program(const std::uint8_t* bytes, std::size_t length, DolHeader& out, std::string& format,
                           std::string& error) {
    std::string strict_why, elf_why, lenient_why;
    if (parse_dol_header(bytes, length, out, strict_why)) {
        format = "DOL";
        error.clear();
        return true;
    }
    if (parse_elf(bytes, length, out, elf_why)) {
        format = "ELF";
        error.clear();
        return true;
    }
    // An ELF that is not a usable one is not a DOL either.
    if (length >= 4 && bytes[0] == 0x7F && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F') {
        error = elf_why;
        return false;
    }
    if (parse_lenient_dol(bytes, length, out, lenient_why)) {
        format = "DOL read leniently (" + strict_why + ")";
        error.clear();
        return true;
    }
    error = strict_why;
    return false;
}

}  // namespace riftwii
