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

}  // namespace riftwii
