// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/dol.hpp"

#include <iostream>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using Bytes = std::vector<std::uint8_t>;

static void Put32(Bytes& b, std::size_t at, std::uint32_t v) {
    b[at] = static_cast<std::uint8_t>(v >> 24);
    b[at + 1] = static_cast<std::uint8_t>(v >> 16);
    b[at + 2] = static_cast<std::uint8_t>(v >> 8);
    b[at + 3] = static_cast<std::uint8_t>(v);
}

// A header with one text and one data section, like a small game.
static Bytes MakeHeader() {
    Bytes h(riftwii::kDolHeaderBytes, 0);
    Put32(h, 0x00, 0x100);       // text0 offset
    Put32(h, 0x48, 0x80004000);  // text0 address
    Put32(h, 0x90, 0x2000);      // text0 size
    Put32(h, 0x1C, 0x2100);      // data0 offset
    Put32(h, 0x64, 0x80006000);  // data0 address
    Put32(h, 0xAC, 0x400);       // data0 size
    Put32(h, 0xD8, 0x80006400);  // bss
    Put32(h, 0xDC, 0x1000);
    Put32(h, 0xE0, 0x80004100);  // entry
    return h;
}

static void TestParse() {
    riftwii::DolHeader d;
    std::string error;
    Bytes h = MakeHeader();
    EXPECT_TRUE(riftwii::parse_dol_header(h.data(), h.size(), d, error));
    EXPECT_TRUE(d.sections[0].used());
    EXPECT_TRUE(d.sections[0].is_text);
    EXPECT_EQ(d.sections[0].address, 0x80004000u);
    EXPECT_EQ(d.sections[7].offset, 0x2100u);
    EXPECT_FALSE(d.sections[7].is_text);
    EXPECT_FALSE(d.sections[1].used());
    EXPECT_EQ(d.bss_address, 0x80006400u);
    EXPECT_EQ(d.bss_size, 0x1000u);
    EXPECT_EQ(d.entry, 0x80004100u);
    EXPECT_EQ(d.image_size(), 0x2500ull);
}

static void TestRejections() {
    riftwii::DolHeader d;
    std::string error;
    Bytes h = MakeHeader();
    EXPECT_FALSE(riftwii::parse_dol_header(h.data(), 0xFF, d, error));

    h = MakeHeader();
    Put32(h, 0x00, 0x80);  // section inside the header
    EXPECT_FALSE(riftwii::parse_dol_header(h.data(), h.size(), d, error));

    h = MakeHeader();
    Put32(h, 0x48, 0x81800000);  // text above MEM1
    EXPECT_FALSE(riftwii::parse_dol_header(h.data(), h.size(), d, error));

    h = MakeHeader();
    Put32(h, 0x48, 0x80004002);  // misaligned
    EXPECT_FALSE(riftwii::parse_dol_header(h.data(), h.size(), d, error));

    h = MakeHeader();
    Put32(h, 0x90, 0x01800000);  // text runs past MEM1
    EXPECT_FALSE(riftwii::parse_dol_header(h.data(), h.size(), d, error));

    h = MakeHeader();
    Put32(h, 0xE0, 0x80006100);  // entry in data
    EXPECT_FALSE(riftwii::parse_dol_header(h.data(), h.size(), d, error));

    h = MakeHeader();
    Put32(h, 0x64, 0x80005F00);  // data0 loads over text0's last 0x100 bytes
    EXPECT_FALSE(riftwii::parse_dol_header(h.data(), h.size(), d, error));
    Put32(h, 0x64, 0x80006000);  // right after it: fine
    EXPECT_TRUE(riftwii::parse_dol_header(h.data(), h.size(), d, error));

    h = MakeHeader();
    Put32(h, 0xD8, 0x70000000);  // bss outside RAM
    EXPECT_FALSE(riftwii::parse_dol_header(h.data(), h.size(), d, error));

    h = MakeHeader();
    Put32(h, 0x64, 0x90000000);  // data in MEM2 is fine
    EXPECT_TRUE(riftwii::parse_dol_header(h.data(), h.size(), d, error));
}

static void Put16(Bytes& b, std::size_t at, std::uint16_t v) {
    b[at] = static_cast<std::uint8_t>(v >> 8);
    b[at + 1] = static_cast<std::uint8_t>(v);
}

// WiiWare programs (riftwii::parse_channel_program): a DOL as before, an
// ELF's loaded segments, and a DOL the strict reading refuses.
static void TestChannelPrograms() {
    riftwii::DolHeader d;
    std::string format, error;
    Bytes h = MakeHeader();
    h.resize(riftwii::kProgramHeadBytes, 0);
    EXPECT_TRUE(riftwii::parse_channel_program(h.data(), h.size(), d, format, error));
    EXPECT_EQ(format, std::string("DOL"));

    // A big-endian PowerPC ELF: code at 0x80004000 (executable), data with
    // a zeroed tail at 0x80100000, a note that loads nothing.
    Bytes elf(riftwii::kProgramHeadBytes, 0);
    elf[0] = 0x7F;
    elf[1] = 'E';
    elf[2] = 'L';
    elf[3] = 'F';
    elf[4] = 1;
    elf[5] = 2;
    Put16(elf, 0x12, 20);
    Put32(elf, 0x18, 0x80004010);  // entry
    Put32(elf, 0x1C, 0x34);        // program headers
    Put16(elf, 0x2A, 0x20);
    Put16(elf, 0x2C, 3);
    const auto segment = [&](int i, std::uint32_t type, std::uint32_t offset, std::uint32_t address,
                             std::uint32_t file, std::uint32_t memory, std::uint32_t flags) {
        const std::size_t at = 0x34 + i * 0x20;
        Put32(elf, at, type);
        Put32(elf, at + 4, offset);
        Put32(elf, at + 8, address);
        Put32(elf, at + 12, address);
        Put32(elf, at + 16, file);
        Put32(elf, at + 20, memory);
        Put32(elf, at + 24, flags);
    };
    segment(0, 1, 0x100, 0x80004000, 0x3000, 0x3000, 5);    // R-X
    segment(1, 1, 0x3100, 0x00100000, 0x800, 0x2800, 6);    // RW-, a physical address, 0x2000 zeroed
    segment(2, 4, 0x3900, 0, 0x40, 0, 4);                   // a note
    EXPECT_TRUE(riftwii::parse_channel_program(elf.data(), elf.size(), d, format, error));
    EXPECT_EQ(format, std::string("ELF"));
    EXPECT_EQ(d.entry, 0x80004010u);
    EXPECT_TRUE(d.sections[0].is_text);
    EXPECT_EQ(d.sections[0].address, 0x80004000u);
    EXPECT_EQ(d.sections[0].size, 0x3000u);
    EXPECT_FALSE(d.sections[7].is_text);
    EXPECT_EQ(d.sections[7].address, 0x80100000u);  // the physical address, as the CPU runs it
    EXPECT_EQ(d.sections[7].offset, 0x3100u);
    EXPECT_EQ(d.bss_address, 0x80100800u);
    EXPECT_EQ(d.bss_size, 0x2000u);
    EXPECT_EQ(d.image_size(), 0x3900ull);
    // A little-endian (or another CPU's) ELF is refused, not read as a DOL.
    elf[5] = 1;
    EXPECT_FALSE(riftwii::parse_channel_program(elf.data(), elf.size(), d, format, error));
    elf[5] = 2;
    // Program headers past what was read: refused.
    Put32(elf, 0x1C, riftwii::kProgramHeadBytes - 0x10);
    EXPECT_FALSE(riftwii::parse_channel_program(elf.data(), elf.size(), d, format, error));

    // A DOL with its entry in a data section and a physical text address:
    // strict reading refuses it, the lenient one loads it.
    h = MakeHeader();
    Put32(h, 0x48, 0x00004000);  // text0 at a physical address
    Put32(h, 0xE0, 0x80006010);  // entry in data0
    h.resize(riftwii::kProgramHeadBytes, 0);
    EXPECT_FALSE(riftwii::parse_dol_header(h.data(), h.size(), d, error));
    EXPECT_TRUE(riftwii::parse_channel_program(h.data(), h.size(), d, format, error));
    EXPECT_TRUE(format.find("DOL read leniently") == 0);
    EXPECT_EQ(d.sections[0].address, 0x80004000u);
    // Overlapping sections are loaded in order too.
    h = MakeHeader();
    Put32(h, 0x64, 0x80005000);  // data0 over text0
    h.resize(riftwii::kProgramHeadBytes, 0);
    EXPECT_TRUE(riftwii::parse_channel_program(h.data(), h.size(), d, format, error));
    // Nothing that loads anywhere: refused.
    Bytes junk(riftwii::kProgramHeadBytes, 0x5A);
    EXPECT_FALSE(riftwii::parse_channel_program(junk.data(), junk.size(), d, format, error));
}

int main() {
    TestParse();
    TestRejections();
    TestChannelPrograms();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "dol tests passed" << std::endl;
    return 0;
}
