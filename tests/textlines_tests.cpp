// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The line and word readers (riftwii/textlines.hpp) against the streams
// they replace: every input is read both ways and must come out the same.
#include <cfloat>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "riftwii/playhistory.hpp"
#include "riftwii/textlines.hpp"
#include "riftwii/themepack.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) EXPECT_TRUE((a) == (b))

using Lines = std::vector<std::string>;

// Texts with the edge cases the parsers meet: no last line break, empty
// lines, CRLF, a NUL inside, comments, leading and trailing white space.
static const std::vector<std::string>& Texts() {
    static const std::vector<std::string> texts = {
        "", "\n", "\n\n", "a", "a\n", "a\nb", "a\nb\n", "a\n\nb", "\na",
        "a\r\nb\r\n", "a\r\n\r\n", "\r", "# note\nkey=value\n",
        std::string("x\0y\nz", 5), ",", ",,", "a,b,", ",a", "a,,b",
        "a\tb\tc", "a\t\tc", "a\tb\t", "a\tb", "\t", "  spaced words \t\n",
        "RSBE01\t3\t1700000000", "one two\vthree\ffour\rfive",
    };
    return texts;
}

static Lines StreamLines(const std::string& text, char delimiter) {
    Lines out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line, delimiter)) out.push_back(line);
    return out;
}

static Lines ReaderLines(const std::string& text, char delimiter) {
    Lines out;
    riftwii::LineReader in(text, delimiter);
    std::string line;
    while (in.next(line)) out.push_back(line);
    return out;
}

static void TestLinesMatchGetline() {
    for (const std::string& text : Texts()) {
        for (char delimiter : {'\n', ',', '\t'}) {
            const bool same = StreamLines(text, delimiter) == ReaderLines(text, delimiter);
            if (!same) std::cerr << "lines differ for \"" << text << "\" split on " << int(delimiter) << std::endl;
            EXPECT_TRUE(same);
        }
    }
    // The plain cases, spelt out.
    EXPECT_EQ(ReaderLines("", '\n'), Lines{});
    EXPECT_EQ(ReaderLines("\n", '\n'), Lines{""});
    EXPECT_EQ(ReaderLines("a\nb", '\n'), (Lines{"a", "b"}));
    EXPECT_EQ(ReaderLines("a\n", '\n'), Lines{"a"});
    EXPECT_EQ(ReaderLines("a\r\n", '\n'), Lines{"a\r"});
}

// Two fields split on tabs and then the rest of the line, as play
// history reads one: a delimiter for each call.
static void TestMixedDelimiters() {
    for (const std::string& text : Texts()) {
        std::istringstream stream(text);
        riftwii::LineReader reader(text);
        std::string a1, b1, c1, a2, b2, c2;
        const bool from_stream = std::getline(stream, a1, '\t') && std::getline(stream, b1, '\t') &&
                                 static_cast<bool>(std::getline(stream, c1));
        const bool from_reader = reader.next(a2, '\t') && reader.next(b2, '\t') && reader.next(c2);
        EXPECT_EQ(from_stream, from_reader);
        if (from_stream && from_reader) {
            EXPECT_EQ(a1, a2);
            EXPECT_EQ(b1, b2);
            EXPECT_EQ(c1, c2);
        }
    }
}

static void TestWordsMatchStream() {
    for (const std::string& text : Texts()) {
        std::istringstream stream(text);
        riftwii::WordReader reader(text);
        Lines a, b;
        for (std::string w; stream >> w;) a.push_back(w);
        for (std::string w; reader.next(w);) b.push_back(w);
        EXPECT_EQ(a, b);
    }
    // A failed read leaves the word as it was, as >> does.
    riftwii::WordReader reader("  ");
    std::string word = "kept";
    EXPECT_TRUE(!reader.next(word));
    EXPECT_EQ(word, std::string("kept"));
    EXPECT_TRUE(!reader);
}

// One script line read as "cmd int int" and as "cmd float float int",
// the way the GUI test script reads its commands.
static void TestNumbersMatchStream() {
    const std::vector<std::string> lines = {
        "wait 10", "wait", "wait   ", "wait abc", "wait -5", "wait +7", "wait 10px",
        "wait 99999999999", "wait -99999999999", "wait 2147483647", "wait -2147483648",
        "wait - 3", "wait 0x10", "wait 007",
        "glide 10 20 30", "glide 1.5 -2.25 8", "glide .5 5. 3", "glide 1e2 2E-1 4",
        "glide 1e 2 3", "glide . 2 3", "glide -. 2 3", "glide 3 4", "glide 1.5.5 2 3",
        "glide +1 -0 2", "glide 1e+ 2 3",
    };
    for (const std::string& line : lines) {
        std::istringstream stream(line);
        riftwii::WordReader reader(line);
        std::string cmd1, cmd2;
        stream >> cmd1;
        reader.next(cmd2);
        EXPECT_EQ(cmd1, cmd2);
        if (cmd1 == "wait") {
            int v1 = 42, v2 = 42;
            stream >> v1;
            const bool ok = reader.next(v2);
            EXPECT_EQ(static_cast<bool>(stream), ok);
            if (v1 != v2) std::cerr << "int differs for \"" << line << "\": " << v1 << " vs " << v2 << std::endl;
            EXPECT_EQ(v1, v2);
            std::string rest1 = "none", rest2 = "none";
            stream >> rest1;
            reader.next(rest2);
            EXPECT_EQ(rest1, rest2);
        } else {
            float x1 = 42, y1 = 42, x2 = 42, y2 = 42;
            int n1 = 42, n2 = 42;
            stream >> x1 >> y1 >> n1;
            reader.next(x2);
            reader.next(y2);
            reader.next(n2);
            EXPECT_EQ(static_cast<bool>(stream), static_cast<bool>(reader));
            if (x1 != x2 || y1 != y2 || n1 != n2)
                std::cerr << "numbers differ for \"" << line << "\": " << x1 << " " << y1 << " " << n1 << " vs "
                          << x2 << " " << y2 << " " << n2 << std::endl;
            EXPECT_EQ(x1, x2);
            EXPECT_EQ(y1, y2);
            EXPECT_EQ(n1, n2);
        }
    }
}

// libstdc++ (the Wii's) stops a float at the first letter and caps one too
// large; macOS's libc++ does neither, so these cases are spelt out, not
// compared.
static void TestFloatStopsAtLetters() {
    riftwii::WordReader reader("12abc 3 4");
    float x = 42, y = 42;
    EXPECT_TRUE(reader.next(x));
    EXPECT_EQ(x, 12.0f);
    EXPECT_TRUE(!reader.next(y));
    EXPECT_EQ(y, 0.0f);
    // Too large: libstdc++ gives the largest float and fails (libc++
    // gives infinity).
    riftwii::WordReader huge("1e99 -1e99");
    float big = 0;
    EXPECT_TRUE(!huge.next(big));
    EXPECT_EQ(big, FLT_MAX);
    riftwii::WordReader negative("-1e99");
    EXPECT_TRUE(!negative.next(big));
    EXPECT_EQ(big, -FLT_MAX);
}

// autorun.txt's lines: a command, words and numbers, then the rest of the
// line as std::getline gives it ("option Section/Option = choice").
static void TestRestMatchesGetline() {
    const std::vector<std::string> lines = {
        "option Speed/Laps = 5 laps", "option", "option   ", "option  a =  b  ", "usb RMCE01 2 tail",
        "usb RMCE01", "usb RMCE01 x tail", "usb", "usb RMCE01 99999999999 tail", "usb\tA\t3\t\tz",
        "usb A -", "usb A 7", "usb A 7 ",
    };
    for (const std::string& line : lines) {
        // Command and rest.
        {
            std::istringstream stream(line);
            riftwii::WordReader reader(line);
            std::string c1, c2, r1 = "none", r2 = "none";
            stream >> c1;
            reader.next(c2);
            const bool s = static_cast<bool>(std::getline(stream, r1));
            const bool r = reader.rest(r2);
            EXPECT_EQ(c1, c2);
            EXPECT_EQ(s, r);
            EXPECT_EQ(r1, r2);
        }
        // Command, word, number and rest.
        {
            std::istringstream stream(line);
            riftwii::WordReader reader(line);
            std::string c1, c2, w1, w2, r1 = "none", r2 = "none";
            int n1 = 0, n2 = 0;
            stream >> c1 >> w1 >> n1;
            reader.next(c2);
            reader.next(w2);
            reader.next(n2);
            const bool s = static_cast<bool>(std::getline(stream, r1));
            const bool r = reader.rest(r2);
            EXPECT_EQ(c1, c2);
            EXPECT_EQ(w1, w2);
            EXPECT_EQ(n1, n2);
            EXPECT_EQ(s, r);
            if (r1 != r2) std::cerr << "rest differs for \"" << line << "\": \"" << r1 << "\" vs \"" << r2 << "\"" << std::endl;
            EXPECT_EQ(r1, r2);
        }
    }
}

// Play history's fields: an ID and a count each end at a tab, the time is
// the rest of the line, and the last line needs no line break.
static void TestPlayHistoryFields() {
    riftwii::PlayHistory h;
    h.parse("SB4E01\t3\t1790000000\n"
            "RMCE01\t2\t\n"            // no time
            "SMNE01\t\t5\n"            // no count
            "R2SE18\t1\t5\textra\n"    // a field too many: the time is "5\textra"
            "RSBE01\t4\n"              // two fields
            "\n\t\t\n"
            "SOUE01\t7\t9");           // the last line, no break
    EXPECT_EQ(h.size(), 2u);
    EXPECT_TRUE(h.find("SB4E01") && h.find("SB4E01")->count == 3);
    EXPECT_TRUE(h.find("SOUE01") && h.find("SOUE01")->last == 9);
}

// A theme pack's "retire" line: its words with any white space between.
static void TestThemePackRetire() {
    const std::string text = "RIFTWII THEMES 1\nretire \tOld\t  New  a.png\t b.png \nretire Gone -\nend\n";
    riftwii::ThemePack pack;
    std::string error;
    EXPECT_TRUE(riftwii::parse_theme_pack(reinterpret_cast<const std::uint8_t*>(text.data()), text.size(), pack, error));
    EXPECT_EQ(pack.retired.size(), 2u);
    if (pack.retired.size() == 2) {
        EXPECT_EQ(pack.retired[0].name, std::string("Old"));
        EXPECT_EQ(pack.retired[0].replacement, std::string("New"));
        EXPECT_EQ(pack.retired[0].files, (Lines{"a.png", "b.png"}));
        EXPECT_TRUE(pack.retired[1].replacement.empty());
    }
    // A retire line with no name is refused.
    const std::string bare = "RIFTWII THEMES 1\nretire \nend\n";
    riftwii::ThemePack none;
    EXPECT_TRUE(!riftwii::parse_theme_pack(reinterpret_cast<const std::uint8_t*>(bare.data()), bare.size(), none, error));
}

int main() {
    TestLinesMatchGetline();
    TestMixedDelimiters();
    TestWordsMatchStream();
    TestNumbersMatchStream();
    TestFloatStopsAtLetters();
    TestRestMatchesGetline();
    TestPlayHistoryFields();
    TestThemePackRetire();
    if (g_failures == 0) std::cout << "textlines: all passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
