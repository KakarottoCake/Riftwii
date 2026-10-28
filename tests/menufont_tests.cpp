// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The menu font in two parts (wii/menu/fullfont.hpp): the core in the DOL
// (wii/font/core.ttf) and the whole font next to it (wii/font/rounded.ttf,
// shipped as apps/riftwii/font.ttf). Checked here on the committed files:
// every character of the translations is in the core, the core and the
// full font together draw every character of the full font, and both are
// rounded to 8 font units (tools/make_menu_font.py).
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using Bytes = std::vector<std::uint8_t>;

static Bytes ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static std::uint32_t Get16(const Bytes& b, std::size_t at) {
    if (at + 2 > b.size()) return 0;
    return static_cast<std::uint32_t>(b[at] << 8 | b[at + 1]);
}

static std::uint32_t Get32(const Bytes& b, std::size_t at) {
    if (at + 4 > b.size()) return 0;
    return static_cast<std::uint32_t>(b[at]) << 24 | static_cast<std::uint32_t>(b[at + 1]) << 16 |
           static_cast<std::uint32_t>(b[at + 2]) << 8 | b[at + 3];
}

// A table's offset in the font, 0 when it has none.
static std::size_t TableOffset(const Bytes& font, const char* tag) {
    const std::uint32_t tables = Get16(font, 4);
    for (std::uint32_t i = 0; i < tables; ++i) {
        const std::size_t record = 12 + 16 * i;
        if (record + 16 > font.size()) break;
        if (std::string(reinterpret_cast<const char*>(&font[record]), 4) == tag) return Get32(font, record + 8);
    }
    return 0;
}

// The code points the font's Unicode cmap (format 4 or 12) maps to a glyph.
static std::set<std::uint32_t> ReadCmap(const Bytes& font, std::map<std::uint32_t, std::uint32_t>* glyphMap = nullptr) {
    std::set<std::uint32_t> out;
    const std::size_t cmap = TableOffset(font, "cmap");
    if (cmap == 0) return out;
    const std::uint32_t subtables = Get16(font, cmap + 2);
    for (std::uint32_t i = 0; i < subtables; ++i) {
        const std::uint32_t platform = Get16(font, cmap + 4 + 8 * i);
        const std::uint32_t encoding = Get16(font, cmap + 6 + 8 * i);
        const bool unicode = platform == 0 || (platform == 3 && (encoding == 1 || encoding == 10));
        if (!unicode) continue;
        const std::size_t table = cmap + Get32(font, cmap + 8 + 8 * i);
        const std::uint32_t format = Get16(font, table);
        if (format == 4) {
            const std::uint32_t segments = Get16(font, table + 6) / 2;
            const std::size_t ends = table + 14;
            const std::size_t starts = ends + 2 * segments + 2;
            const std::size_t deltas = starts + 2 * segments;
            const std::size_t rangeOffsets = deltas + 2 * segments;
            for (std::uint32_t s = 0; s < segments; ++s) {
                const std::uint32_t end = Get16(font, ends + 2 * s);
                const std::uint32_t start = Get16(font, starts + 2 * s);
                const std::uint32_t delta = Get16(font, deltas + 2 * s);
                const std::uint32_t rangeOffset = Get16(font, rangeOffsets + 2 * s);
                for (std::uint32_t cp = start; cp <= end && cp != 0xFFFF; ++cp) {
                    std::uint32_t glyph;
                    if (rangeOffset == 0) {
                        glyph = (cp + delta) & 0xFFFF;
                    } else {
                        glyph = Get16(font, rangeOffsets + 2 * s + rangeOffset + 2 * (cp - start));
                        if (glyph != 0) glyph = (glyph + delta) & 0xFFFF;
                    }
                    if (glyph != 0) {
                        out.insert(cp);
                        if (glyphMap) (*glyphMap)[cp] = glyph;
                    }
                }
            }
        } else if (format == 12) {
            const std::uint32_t groups = Get32(font, table + 12);
            for (std::uint32_t g = 0; g < groups; ++g) {
                const std::size_t group = table + 16 + 12 * g;
                const std::uint32_t start = Get32(font, group);
                const std::uint32_t end = Get32(font, group + 4);
                const std::uint32_t first = Get32(font, group + 8);
                for (std::uint32_t cp = start; cp <= end && cp < 0x110000; ++cp) {
                    const std::uint32_t glyph = first + (cp - start);
                    if (glyph != 0) {
                        out.insert(cp);
                        if (glyphMap) (*glyphMap)[cp] = glyph;
                    }
                }
            }
        }
    }
    return out;
}

// How many simple glyphs have a point off the 8-unit grid (each point's x
// and y, decoded from the glyf table's flags and deltas).
static int GlyphsOffGrid(const Bytes& font, int& simpleGlyphs) {
    simpleGlyphs = 0;
    const std::size_t head = TableOffset(font, "head");
    const std::size_t maxp = TableOffset(font, "maxp");
    const std::size_t loca = TableOffset(font, "loca");
    const std::size_t glyf = TableOffset(font, "glyf");
    if (head == 0 || maxp == 0 || loca == 0 || glyf == 0) return -1;
    const bool longOffsets = Get16(font, head + 50) == 1;
    const std::uint32_t glyphs = Get16(font, maxp + 4);
    int offGrid = 0;
    for (std::uint32_t g = 0; g < glyphs; ++g) {
        const std::size_t start = longOffsets ? Get32(font, loca + 4 * g) : 2 * Get16(font, loca + 2 * g);
        const std::size_t end = longOffsets ? Get32(font, loca + 4 * g + 4) : 2 * Get16(font, loca + 2 * g + 2);
        if (end <= start) continue;  // no outline (a space)
        const std::size_t at = glyf + start;
        const auto contours = static_cast<std::int16_t>(Get16(font, at));
        if (contours <= 0) continue;  // a composite glyph: not rounded
        ++simpleGlyphs;
        const std::uint32_t points = Get16(font, at + 10 + 2 * (contours - 1)) + 1;
        std::size_t p = at + 10 + 2 * contours;
        p += 2 + Get16(font, p);  // the instructions
        std::vector<std::uint8_t> flags;
        while (flags.size() < points && p < font.size()) {
            const std::uint8_t flag = font[p++];
            flags.push_back(flag);
            if (flag & 8) {
                const std::uint8_t repeats = p < font.size() ? font[p++] : 0;
                for (int r = 0; r < repeats; ++r) flags.push_back(flag);
            }
        }
        // x: bit 1 short, bit 4 positive or same; y: bits 2 and 5.
        const auto coordinates = [&](std::uint8_t shortBit, std::uint8_t sameBit) {
            std::vector<int> values;
            int value = 0;
            for (std::uint32_t i = 0; i < points; ++i) {
                const std::uint8_t flag = flags[i];
                if (flag & shortBit) {
                    const int delta = p < font.size() ? font[p++] : 0;
                    value += (flag & sameBit) ? delta : -delta;
                } else if (!(flag & sameBit)) {
                    value += static_cast<std::int16_t>(Get16(font, p));
                    p += 2;
                }
                values.push_back(value);
            }
            return values;
        };
        if (flags.size() < points) return -1;
        const std::vector<int> xs = coordinates(2, 16);
        const std::vector<int> ys = coordinates(4, 32);
        bool glyphOff = false;
        for (std::uint32_t i = 0; i < points; ++i) {
            if (xs[i] % 8 != 0 || ys[i] % 8 != 0) glyphOff = true;
        }
        offGrid += glyphOff ? 1 : 0;
    }
    return offGrid;
}

// Every character of a UTF-8 text.
static std::set<std::uint32_t> Utf8Characters(const Bytes& text) {
    std::set<std::uint32_t> out;
    for (std::size_t i = 0; i < text.size();) {
        const std::uint8_t lead = text[i];
        std::size_t length = lead < 0x80 ? 1 : (lead >> 5) == 6 ? 2 : (lead >> 4) == 14 ? 3 : (lead >> 3) == 30 ? 4 : 1;
        std::uint32_t cp = length == 1 ? lead : length == 2 ? (lead & 0x1F) : length == 3 ? (lead & 0x0F) : (lead & 0x07);
        if (i + length > text.size()) break;
        for (std::size_t k = 1; k < length; ++k) cp = cp << 6 | (text[i + k] & 0x3F);
        out.insert(cp);
        i += length;
    }
    return out;
}

static bool InCoreRanges(std::uint32_t cp) {
    return cp < 0x3000 || (cp >= 0x3000 && cp <= 0x30FF) || (cp >= 0xFF00 && cp <= 0xFFEF);
}

static void TestSplit(const std::string& repo) {
    const Bytes core = ReadFile(repo + "/wii/font/core.ttf");
    const Bytes full = ReadFile(repo + "/wii/font/rounded.ttf");
    EXPECT_TRUE(!core.empty());
    EXPECT_TRUE(!full.empty());
    const std::set<std::uint32_t> coreChars = ReadCmap(core);
    const std::set<std::uint32_t> fullChars = ReadCmap(full);
    // Nothing is dropped: the full font file keeps all 4,012 characters
    // rounded.ttf was cut with, so with the core every one of them draws,
    // and the core holds only the full font's characters (its glyphs).
    EXPECT_EQ(fullChars.size(), 4012u);
    std::size_t notInFull = 0;
    for (std::uint32_t cp : coreChars) notInFull += fullChars.count(cp) == 0;
    EXPECT_EQ(notInFull, 0u);
    // The core's blocks are whole: Latin, symbols, kana, full-width forms.
    std::size_t blockMissing = 0;
    for (std::uint32_t cp : fullChars) blockMissing += InCoreRanges(cp) && !coreChars.count(cp);
    EXPECT_EQ(blockMissing, 0u);
    // Every character of every translation (and the English msgids) that
    // the font has is in the core, so the menu's own text never needs the
    // SD card. Hangul is checked separately against korean.ttf.
    std::size_t translationMissing = 0;
    for (const auto& entry : std::filesystem::directory_iterator(repo + "/wii/lang")) {
        if (entry.path().extension() != ".po") continue;
        for (std::uint32_t cp : Utf8Characters(ReadFile(entry.path().string()))) {
            if (fullChars.count(cp) && !coreChars.count(cp)) {
                std::cerr << entry.path().filename().string() << ": U+" << std::hex << cp << std::dec
                          << " is not in wii/font/core.ttf (tools/make_menu_font.py core)" << std::endl;
                ++translationMissing;
            }
        }
    }
    EXPECT_EQ(translationMissing, 0u);
    // Japanese text is checked against the core alone, without asking the
    // full font: a character in neither would draw blank on the Wii.
    // (Only the msgstr lines, which are what the menu shows.)
    std::size_t japaneseMissing = 0;
    {
        const Bytes ja = ReadFile(repo + "/wii/lang/ja.po");
        std::istringstream lines(std::string(ja.begin(), ja.end()));
        std::string line;
        while (std::getline(lines, line)) {
            if (line.rfind("msgstr \"", 0) != 0) continue;
            const Bytes shown(line.begin() + 8, line.end());
            for (std::uint32_t cp : Utf8Characters(shown)) {
                if (cp == '"' || cp == '\r' || coreChars.count(cp)) continue;
                std::cerr << "ja.po: U+" << std::hex << cp << std::dec
                          << " is in neither menu font" << std::endl;
                ++japaneseMissing;
            }
        }
    }
    EXPECT_EQ(japaneseMissing, 0u);
    // The core is much smaller than the whole (about a third of the characters).
    EXPECT_TRUE(coreChars.size() < fullChars.size() / 2);
}

static void TestKorean(const std::string& repo) {
    const Bytes font = ReadFile(repo + "/wii/font/korean.ttf");
    EXPECT_TRUE(!font.empty());
    EXPECT_TRUE(font.size() < 512u * 1024u);
    const auto characters = ReadCmap(font);
    const Bytes translation = ReadFile(repo + "/wii/lang/ko.po");
    std::size_t missing = 0;
    for (std::uint32_t cp : Utf8Characters(translation)) {
        if (cp < 0x20 || cp == 0x7F || characters.count(cp)) continue;
        std::cerr << "ko.po: U+" << std::hex << cp << std::dec << " is absent from korean.ttf\n";
        ++missing;
    }
    EXPECT_EQ(missing, 0u);
    // Conjoining and compatibility Jamo, including the extended blocks.
    for (const auto& range : std::vector<std::pair<unsigned, unsigned>>{
             {0x1100, 0x11FF}, {0x3131, 0x318E}, {0xA960, 0xA97C},
             {0xD7B0, 0xD7C6}, {0xD7CB, 0xD7FB}}) {
        for (unsigned cp = range.first; cp <= range.second; ++cp) {
            EXPECT_TRUE(characters.count(cp) != 0);
        }
    }
    // GameTDB KO currently uses these syllables beyond KS X 1001.
    EXPECT_TRUE(characters.count(0xD019) != 0);
    EXPECT_TRUE(characters.count(0xD6D5) != 0);
    EXPECT_EQ(TableOffset(font, "fvar"), 0u);
    EXPECT_EQ(TableOffset(font, "fpgm"), 0u);
    EXPECT_EQ(TableOffset(font, "GPOS"), 0u);
    EXPECT_EQ(TableOffset(font, "GSUB"), 0u);
    int simpleGlyphs = 0;
    EXPECT_EQ(GlyphsOffGrid(font, simpleGlyphs), 0);
    EXPECT_TRUE(simpleGlyphs > 2350);
}

static void TestRounded(const std::string& repo) {
    for (const char* name : {"/wii/font/core.ttf", "/wii/font/rounded.ttf"}) {
        int simple = 0;
        const int offGrid = GlyphsOffGrid(ReadFile(repo + name), simple);
        EXPECT_EQ(offGrid, 0);
        EXPECT_TRUE(simple > 1000);
    }
}

// The keyboard's narrowest action keys (gui_searchkeys.cpp) leave four
// pixels each side. Read the committed font's own advances, rounding each
// glyph up so the check leaves room for FreeType's pixel rounding.
static int TextAdvance(const Bytes& font, const std::map<std::uint32_t, std::uint32_t>& glyphs,
                       const std::string& text, int pixels) {
    const std::uint32_t units = Get16(font, TableOffset(font, "head") + 18);
    const std::uint32_t metrics = Get16(font, TableOffset(font, "hhea") + 34);
    const std::size_t hmtx = TableOffset(font, "hmtx");
    if (units == 0 || metrics == 0 || hmtx == 0) return -1;
    int width = 0;
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i++]);
        const int length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        std::uint32_t cp = length == 1 ? lead : lead & ((1u << (7 - length)) - 1);
        for (int part = 1; part < length && i < text.size(); ++part)
            cp = (cp << 6) | (static_cast<unsigned char>(text[i++]) & 0x3F);
        const auto found = glyphs.find(cp);
        if (found == glyphs.end()) return -1;
        const std::uint32_t metric = std::min(found->second, metrics - 1);
        const std::uint32_t advance = Get16(font, hmtx + 4 * metric);
        width += static_cast<int>((advance * pixels + units - 1) / units);
    }
    return width;
}

static void TestKeyboardLabels(const std::string& repo) {
    for (const char* language : {"en", "es", "ja", "pt", "it", "ko", "fr"}) {
        const bool korean = std::string(language) == "ko";
        const Bytes font = ReadFile(repo + (korean ? "/wii/font/korean.ttf" : "/wii/font/core.ttf"));
        std::map<std::uint32_t, std::uint32_t> glyphs;
        ReadCmap(font, &glyphs);
        std::map<std::string, std::string> translated;
        if (std::string(language) != "en") {
            const Bytes po = ReadFile(repo + "/wii/lang/" + language + ".po");
            std::istringstream input(std::string(po.begin(), po.end()));
            std::string line, key;
            while (std::getline(input, line)) {
                if (line.rfind("msgid ", 0) == 0) {
                    std::istringstream value(line.substr(6));
                    value >> std::quoted(key);
                } else if (line.rfind("msgstr ", 0) == 0) {
                    std::istringstream value(line.substr(7));
                    value >> std::quoted(translated[key]);
                }
            }
        }
        for (const char* key : {"Del", "Space", "Shift", "Clear", "Cancel", "Search", "OK"}) {
            const int room = std::string(key) == "Del" ? 40 : 92;
            const auto found = translated.find(key);
            const std::string caption = found == translated.end() || found->second.empty() ? key : found->second;
            int width = -1;
            for (int size = 18; size >= 12; --size) {
                width = TextAdvance(font, glyphs, caption, size);
                if (width >= 0 && width <= room) break;
            }
            if (width < 0 || width > room)
                std::cerr << language << ": keyboard label " << key << " needs " << width << " pixels, room " << room << '\n';
            EXPECT_TRUE(width >= 0 && width <= room);
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: menufont_tests <repository folder>" << std::endl;
        return 1;
    }
    const std::string repo = argv[1];
    TestKeyboardLabels(repo);
    TestSplit(repo);
    TestRounded(repo);
    TestKorean(repo);
    if (g_failures == 0) std::cout << "menufont: all tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
