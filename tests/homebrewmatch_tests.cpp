// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Finding a homebrew app's GameTDB entry by name (riftwii/homebrewmatch.hpp):
// the keys names are compared by, the loose comparison, the matcher's
// choices and refusals, wiitdb.xml's homebrew blocks and the index file.
#include "riftwii/gametdb.hpp"
#include "riftwii/homebrewmatch.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

using riftwii::HomebrewMatcher;
using riftwii::HomebrewTitle;
using riftwii::homebrew_match_key;

// Shaped like wiitdb.xml's homebrew entries (2026).
const char* const kSnes =
    "<game name=\"SNES9xGX\">\n\t<id>9XGX</id>\n\t<type>Homebrew</type>\n\t<region/>\n"
    "\t<locale lang=\"EN\">\n\t\t<title>SNES9xGX</title>\n\t\t<synopsis>An emulator.</synopsis>\n\t</locale>\n"
    "\t<locale lang=\"ZHCN\">\n\t\t<title>SNES9x\xE8\xB6\x85\xE4\xBB\xBB</title>\n\t</locale>\n</game>";
const char* const kWiimmfi =
    "<game name=\"Wiimmfi Patcher (EN)\">\n\t<id>D01A</id>\n\t<type>Homebrew</type>\n"
    "\t<locale lang=\"EN\">\n\t\t<title>Wiimmfi Patcher</title>\n\t</locale>\n"
    "\t<locale lang=\"KO\">\n\t\t<title>\xEC\x99\x80\xEC\x9D\xB4</title>\n\t</locale>\n</game>";
const char* const kKart =
    "<game name=\"Mario Kart Wii (USA)\">\n\t<id>RMCE01</id>\n\t<type/>\n"
    "\t<locale lang=\"EN\">\n\t\t<title>Mario Kart Wii</title>\n\t</locale>\n</game>";

std::vector<HomebrewTitle> catalogue() {
    return {
        {"9XGX", {"SNES9xGX"}},
        {"D01A", {"Wiimmfi Patcher"}},
        {"DULA", {"USB Loader GX"}},
        {"DPLS", {"WiiMC+"}},
        {"DWMA", {"WiiMC"}},
        {"DHBA", {"Homebrew Browser"}},
        {"DP5A", {"Pix"}},
        {"DBXA", {"Biniax 2"}},
        {"DBYA", {"Biniax 3"}},
        {"DSMA", {"Savegame Manager GX"}},
        // A Korean title that would read as plain "WiiFlow" without its
        // own letters, listed before WiiFlow itself (as in wiitdb.xml).
        {"D2EA", {"WiiFlow Start Configurator", "WiiFlow \xEA\xB5\xAC\xEC\x84\xB1 \xEC\x8B\x9C\xEC\x9E\x91"}},
        {"DWFA", {"WiiFlow"}},
    };
}

void test_keys() {
    expect(homebrew_match_key("Snes9x GX 4.5.3") == "snes9xgx", "a version number is left out");
    expect(homebrew_match_key("SNES9xGX (EN)") == "snes9xgx", "a bracketed region is left out");
    expect(homebrew_match_key("snes9xgx") == "snes9xgx", "a folder's name gives the same key");
    expect(homebrew_match_key("usbloader_gx") == "usbloadergx", "underscores divide words");
    expect(homebrew_match_key("USB Loader GX r1250") == "usbloadergx", "an r revision is left out");
    expect(homebrew_match_key("Homebrew Browser v0.3.9e") == "homebrewbrowser", "a v version is left out");
    expect(homebrew_match_key("WiiXplorer beta") == "wiixplorer", "beta is left out");
    expect(homebrew_match_key("Biniax 2") == "biniax2", "a plain number names the app");
    expect(homebrew_match_key("Time Frack 3D") == "timefrack3d", "3D names the app");
    expect(homebrew_match_key("(untitled)") == "untitled", "all brackets keeps what is inside");
    expect(homebrew_match_key("\xEC\x99\x80\xEC\x9D\xB4").empty(), "no ASCII letters, no key");
    expect(homebrew_match_key("WiiFlow \xEA\xB5\xAC\xEC\x84\xB1") == "wiiflow##", "a Korean word is not dropped");
    expect(homebrew_match_key("Cha\xC3\xAEne Homebrew") == "cha#nehomebrew", "an accented letter counts as one");
}

void test_likeness() {
    expect(riftwii::homebrew_key_likeness("wiimc", "wiimc") == 1.0, "equal keys are alike");
    expect(riftwii::homebrew_key_likeness("", "") == 0.0, "empty keys are not");
    const double one_off = riftwii::homebrew_key_likeness("savegamemanagergx", "savegamemanagrgx");
    expect(one_off > 0.9 && one_off < 1.0, "one letter dropped of seventeen");
    expect(riftwii::homebrew_key_likeness("wiimc", "homebrewbrowser", 0.85) == 0.0, "far apart lengths are ruled out");
}

void test_matcher() {
    const HomebrewMatcher matcher(catalogue());
    expect(!matcher.empty(), "the matcher has keys");
    expect(matcher.match("Snes9x GX", "snes9xgx") == "9XGX", "meta.xml name, spaced differently");
    expect(matcher.match("USB Loader GX", "usbloader_gx") == "DULA", "meta.xml name exactly");
    expect(matcher.match("Savegame Manager GX", "x") == "DSMA", "exact beats nothing");
    expect(matcher.match("SaveGame Manger GX", "x") == "DSMA", "a letter off still matches");
    expect(matcher.match("WiiMC 1.3.8", "wiimc") == "DWMA", "the version is not part of the name");
    expect(matcher.match("Some Other App", "wiimc") == "DWMA", "the folder's name as the fallback");
    expect(matcher.match("", "homebrew_browser") == "DHBA", "no meta.xml: the folder's name");
    expect(matcher.match("WiiFlow Lite", "wiiflow") == "DWFA", "another app's Korean title is not WiiFlow");
    expect(matcher.match("Pix", "pix") == "DP5A", "a short name matches exactly");
    expect(matcher.match("Pux", "pux").empty(), "a short name never matches loosely");
    expect(matcher.match("Biniax 4", "biniax4").empty(), "two entries equally close are neither");
    expect(matcher.match("Mario Kart Wii", "mkwii").empty(), "an app GameTDB does not list");
    expect(matcher.match("Wiimfi Patchr", "folder") == "D01A", "two letters off of fourteen");
    expect(matcher.match("Homebrw Brwsr", "hb").empty(), "three letters off of fifteen is too far");
    const HomebrewMatcher nothing({});
    expect(nothing.empty() && nothing.match("WiiMC", "wiimc").empty(), "no entries, no match");
}

void test_wiitdb_blocks() {
    HomebrewTitle title;
    expect(riftwii::parse_wiitdb_homebrew(kSnes, title), "a homebrew block reads");
    expect(title.id == "9XGX" && title.names.size() == 2 && title.names[0] == "SNES9xGX",
           "its English title first, the same name once, the Chinese title kept");
    expect(riftwii::parse_wiitdb_homebrew(kWiimmfi, title), "another homebrew block reads");
    expect(title.id == "D01A" && title.names.size() == 1 && title.names[0] == "Wiimmfi Patcher",
           "the name attribute's region adds nothing new and the Korean title has no key");
    expect(!riftwii::parse_wiitdb_homebrew(kKart, title), "a game is not homebrew");
    expect(!riftwii::parse_wiitdb_homebrew("<game name=\"x\"><id>../x</id><type>Homebrew</type></game>", title),
           "an ID that is not one is refused");
    expect(!riftwii::parse_wiitdb_homebrew("<game name=\"x\"><id>9XGX</id><type>Homebrew</type>", title),
           "a broken block is refused");

    // The scanner keeps every homebrew entry whether wanted or not.
    const std::string xml = std::string("<?xml version=\"1.0\"?>\n<datafile>\n") + kKart + "\n" + kSnes + "\n" + kWiimmfi +
        "\n</datafile>\n";
    riftwii::WiitdbScanner scanner({"RMCE01"}, "EN");
    for (std::size_t at = 0; at < xml.size(); at += 7) scanner.feed(xml.data() + at, std::min<std::size_t>(7, xml.size() - at));
    expect(scanner.games().size() == 1 && scanner.games().count("RMCE01"), "the wanted game is found");
    expect(scanner.homebrew().size() == 2 && scanner.homebrew()[0].id == "9XGX" && scanner.homebrew()[1].id == "D01A",
           "both homebrew entries are kept, fed in small pieces");
}

void test_index_file() {
    const std::vector<HomebrewTitle> titles = catalogue();
    const std::string text = riftwii::serialize_homebrew_titles(titles);
    std::vector<HomebrewTitle> back;
    expect(riftwii::parse_homebrew_titles(text, back), "the index reads back");
    expect(back.size() == titles.size() && back[2].id == "DULA" && back[2].names[0] == "USB Loader GX", "every entry, in order");
    expect(!riftwii::parse_homebrew_titles("# nothing\n", back) && back.empty(), "no entries is no index");
    expect(riftwii::parse_homebrew_titles("../x\tEvil\nAB\tShort\n9XGX\tSNES9xGX\t\t\r\n", back), "bad lines are skipped");
    expect(back.size() == 1 && back[0].id == "9XGX" && back[0].names.size() == 1, "only the good line is kept");
    std::string many;
    for (std::size_t i = 0; i < riftwii::kMaxHomebrewTitles + 10; ++i) many += "AAAA\tName\n";
    riftwii::parse_homebrew_titles(many, back);
    expect(back.size() == riftwii::kMaxHomebrewTitles, "the count of entries is bounded");
    riftwii::parse_homebrew_titles("AAAA\t" + std::string(500, 'n') + "\tb\tc\td\te\tf\n", back);
    expect(back.size() == 1 && back[0].names.size() == riftwii::kMaxHomebrewNames &&
               back[0].names[0].size() == riftwii::kMaxHomebrewNameBytes,
           "names an entry and bytes a name are bounded");
}

}  // namespace

int main() {
    test_keys();
    test_likeness();
    test_matcher();
    test_wiitdb_blocks();
    test_index_file();
    return failures == 0 ? 0 : 1;
}
