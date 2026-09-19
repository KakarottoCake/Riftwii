// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// The DOL executable header (0x100 bytes, big-endian): seven text and
// eleven data sections as (file offset, load address, size) triples, then
// the BSS address and size and the entry point. Layout per
// https://wiibrew.org/wiki/DOL . A section with size 0 is unused.
namespace riftwii {

constexpr std::size_t kDolHeaderBytes = 0x100;
constexpr std::size_t kDolTextSections = 7;
constexpr std::size_t kDolDataSections = 11;
constexpr std::size_t kDolSections = kDolTextSections + kDolDataSections;

struct DolSection {
    std::uint32_t offset = 0;   // in the file
    std::uint32_t address = 0;  // where the apploader/loader puts it
    std::uint32_t size = 0;
    bool used() const { return size != 0; }
    bool is_text = false;
};

struct DolHeader {
    DolSection sections[kDolSections];
    std::uint32_t bss_address = 0;
    std::uint32_t bss_size = 0;
    std::uint32_t entry = 0;
    // Bytes from the start of the file to the end of the last section.
    std::uint64_t image_size() const;
};

// Parses and validates a DOL header: every used section must lie inside
// RAM (MEM1 or MEM2), file offsets must be at or past the header, and the
// entry point must fall inside a text section.
bool parse_dol_header(const std::uint8_t* bytes, std::size_t length, DolHeader& out, std::string& error);

}  // namespace riftwii
