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
    Put32(h, 0xD8, 0x70000000);  // bss outside RAM
    EXPECT_FALSE(riftwii::parse_dol_header(h.data(), h.size(), d, error));

    h = MakeHeader();
    Put32(h, 0x64, 0x90000000);  // data in MEM2 is fine
    EXPECT_TRUE(riftwii::parse_dol_header(h.data(), h.size(), d, error));
}

int main() {
    TestParse();
    TestRejections();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "dol tests passed" << std::endl;
    return 0;
}
