// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/themelist.hpp"
#include "riftwii/zipread.hpp"
#include "zipbuild.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;
using ziptest::make_zip;
using ziptest::stored_block_inflate;
using ziptest::TestFile;

namespace {

const char* const kSha = "8cf92160ae5267827e7e2f4b9a9730f001e9f6d9ce5647d202e5fa8589ca5617";

std::string theme_json(const std::string& folder, const std::string& extra = "", const std::string& zip = "",
                       const std::string& size = "274776") {
    return "{\"folder\":\"" + folder + "\",\"name\":\"Indigo\",\"author\":\"RiftWii contributors\",\"license\":\"CC0-1.0\","
           "\"size\":" + size + ",\"sha256\":\"" + kSha + "\",\"files\":27,\"unpacked_size\":276143,\"has_music\":false,"
           "\"zip\":\"" + (zip.empty() ? "https://riftwii.github.io/themes/themes/" + folder + ".zip" : zip) +
           "\",\"preview\":\"https://riftwii.github.io/themes/previews/" + folder + ".png\"" + extra + "}";
}

std::string list_json(const std::string& themes, const std::string& schema = "1") {
    return "{\"schema\":" + schema + ",\"commit\":\"abc\",\"themes\":[" + themes + "]}";
}

struct Parsed {
    std::vector<ThemeListing> themes;
    std::size_t count = 0, skipped = 0;
    std::string error;
    bool ok = false;
};

Parsed parse(const std::string& text, std::size_t max = 500) {
    Parsed p;
    p.themes.resize(max);
    p.ok = parse_theme_list(text, p.themes.data(), max, p.count, p.skipped, p.error);
    return p;
}

bool read(const std::vector<std::uint8_t>& zip, std::vector<ZipEntry>& entries, std::string& error) {
    return zip_read_directory(zip.data(), zip.size(), entries, error);
}

ZipThemeLimits limits(std::size_t files = 10, std::uint64_t unpacked = 1000) {
    ZipThemeLimits l;
    l.files = files;
    l.unpacked_size = unpacked;
    return l;
}

}  // namespace

int main() {
    // ---- themes.json
    {
        const Parsed p = parse(list_json(theme_json("Indigo")));
        EXPECT_TRUE(p.ok);
        EXPECT_EQ(p.count, 1u);
        EXPECT_EQ(p.skipped, 0u);
        EXPECT_EQ(std::string(p.themes[0].folder), "Indigo");
        EXPECT_EQ(std::string(p.themes[0].name), "Indigo");
        EXPECT_EQ(std::string(p.themes[0].license), "CC0-1.0");
        EXPECT_EQ(p.themes[0].size, 274776u);
        EXPECT_EQ(p.themes[0].files, 27u);
        EXPECT_TRUE(!p.themes[0].has_music);
        EXPECT_EQ(std::string(p.themes[0].zip), "https://riftwii.github.io/themes/themes/Indigo.zip");
    }
    // Unknown keys, nesting, whitespace and escapes are fine.
    {
        const Parsed p = parse("{ \"schema\" : 1 , \"x\": {\"a\":[1,2,{\"b\":null}]}, \"themes\": [ " +
                               theme_json("Indigo", ",\"later\":[true,false,\"s\"]") + " ] }\n");
        EXPECT_TRUE(p.ok);
        EXPECT_EQ(p.count, 1u);
        std::string named = theme_json("Indigo");
        const std::size_t at = named.find("\"Indigo\",\"author\"");
        named.replace(at, 8, "\"In\\u0064igo\\t!\"");
        const Parsed q = parse(list_json(named));
        EXPECT_TRUE(q.ok);
        EXPECT_EQ(std::string(q.themes[0].name), "Indigo !");
    }
    EXPECT_TRUE(parse(list_json("")).ok);
    EXPECT_EQ(parse(list_json("")).count, 0u);
    // Not a list, wrong schema, no schema, broken text.
    EXPECT_TRUE(!parse("[]").ok);
    EXPECT_TRUE(!parse("").ok);
    EXPECT_TRUE(!parse(list_json(theme_json("Indigo"), "2")).ok);
    EXPECT_TRUE(!parse("{\"themes\":[]}").ok);
    EXPECT_TRUE(!parse("{\"schema\":1}").ok);
    EXPECT_TRUE(!parse(list_json(theme_json("Indigo")).substr(0, 60)).ok);
    EXPECT_TRUE(!parse(list_json(theme_json("Indigo")) + " junk").ok);
    EXPECT_TRUE(!parse("{\"schema\":1,\"themes\":[{\"folder\":\"a\\\"}]}").ok);
    // Folder names that are not one plain name leave the theme out.
    for (const char* folder : {"..", ".hidden", "a/b", "a\\\\b", "", " lead", "a..b", "a\\u0000b", "A:B", "Théme"}) {
        const Parsed p = parse(list_json(theme_json(folder)));
        EXPECT_TRUE(p.ok);
        EXPECT_EQ(p.count, 0u);
        EXPECT_EQ(p.skipped, 1u);
    }
    EXPECT_TRUE(theme_folder_ok("Indigo"));
    EXPECT_TRUE(theme_folder_ok("My Theme_2-b"));
    EXPECT_TRUE(!theme_folder_ok("a/b"));
    EXPECT_TRUE(!theme_folder_ok("a\\b"));
    EXPECT_TRUE(!theme_folder_ok(".."));
    EXPECT_TRUE(!theme_folder_ok(".x"));
    EXPECT_TRUE(!theme_folder_ok(std::string("a\nb")));
    EXPECT_TRUE(!theme_folder_ok(std::string(48, 'a')));
    EXPECT_TRUE(theme_folder_ok(std::string(47, 'a')));
    // Addresses stay on riftwii.github.io.
    EXPECT_TRUE(theme_url_ok("https://riftwii.github.io/themes/themes/Indigo.zip"));
    EXPECT_TRUE(!theme_url_ok("http://riftwii.github.io/themes/a.zip"));
    EXPECT_TRUE(!theme_url_ok("https://riftwii.github.io.evil.example/a.zip"));
    EXPECT_TRUE(!theme_url_ok("https://evil.example/riftwii.github.io/a.zip"));
    EXPECT_TRUE(!theme_url_ok("https://riftwii.github.io@evil.example/a.zip"));
    EXPECT_TRUE(!theme_url_ok("https://riftwii.github.io:8080/a.zip"));
    EXPECT_TRUE(!theme_url_ok("https://riftwii.github.io/a.zip?x=1"));
    EXPECT_TRUE(!theme_url_ok("https://riftwii.github.io/a b.zip"));
    EXPECT_TRUE(!theme_url_ok("https://riftwii.github.io/../a.zip"));
    EXPECT_TRUE(!theme_url_ok("https://riftwii.github.io/"));
    EXPECT_TRUE(!theme_url_ok("https://riftwii.github.io/" + std::string(200, 'a')));
    EXPECT_EQ(parse(list_json(theme_json("Indigo", "", "https://example.com/Indigo.zip"))).count, 0u);
    // Sizes: zero, over the zip cap, not a plain number.
    EXPECT_EQ(parse(list_json(theme_json("Indigo", "", "", "0"))).count, 0u);
    EXPECT_EQ(parse(list_json(theme_json("Indigo", "", "", "16777217"))).count, 0u);
    EXPECT_EQ(parse(list_json(theme_json("Indigo", "", "", "16777216"))).count, 1u);
    EXPECT_EQ(parse(list_json(theme_json("Indigo", "", "", "99999999999999999999"))).count, 0u);
    EXPECT_EQ(parse(list_json(theme_json("Indigo", "", "", "-5"))).count, 0u);
    EXPECT_EQ(parse(list_json(theme_json("Indigo", "", "", "1.5"))).count, 0u);
    EXPECT_EQ(parse(list_json(theme_json("Indigo", "", "", "\"5\""))).count, 0u);
    // A bad digest, a long string, an empty name.
    {
        std::string json = theme_json("Indigo");
        json.replace(json.find(kSha), 64, std::string(64, 'G'));
        EXPECT_EQ(parse(list_json(json)).count, 0u);
        std::string upper = theme_json("Indigo");
        upper.replace(upper.find(kSha), 1, "8", 1);
        upper[upper.find("8cf9")] = 'C';
        EXPECT_EQ(parse(list_json(upper)).count, 0u);  // upper case is not what the list says
        std::string longname = theme_json("Indigo");
        longname.replace(longname.find("\"Indigo\",\"author\""), 8, "\"" + std::string(80, 'n') + "\"");
        EXPECT_EQ(parse(list_json(longname)).count, 0u);
        std::string huge = theme_json("Indigo");
        huge.replace(huge.find("\"Indigo\",\"author\""), 8, "\"" + std::string(5000, 'n') + "\"");
        EXPECT_TRUE(!parse(list_json(huge)).ok);
        std::string empty = theme_json("Indigo");
        empty.replace(empty.find("\"Indigo\",\"author\""), 8, "\"  \"");
        EXPECT_EQ(parse(list_json(empty)).count, 0u);
    }
    // Bad entries are left out and good ones kept; a folder twice keeps the first.
    {
        const Parsed p = parse(list_json(theme_json("A") + "," + theme_json("..") + "," + theme_json("B") + "," + theme_json("A")));
        EXPECT_TRUE(p.ok);
        EXPECT_EQ(p.count, 2u);
        EXPECT_EQ(p.skipped, 2u);
        EXPECT_EQ(std::string(p.themes[1].folder), "B");
    }
    // Folders that differ only in case are one folder on FAT: the first stands.
    {
        const Parsed p = parse(list_json(theme_json("Indigo") + "," + theme_json("indigo") + "," + theme_json("Indigo2")));
        EXPECT_TRUE(p.ok);
        EXPECT_EQ(p.count, 2u);
        EXPECT_EQ(p.skipped, 1u);
        EXPECT_EQ(std::string(p.themes[1].folder), "Indigo2");
    }
    // The count cap: 500 themes at most, however many the list holds.
    {
        std::string many;
        for (int i = 0; i < 520; ++i) many += (i ? "," : "") + theme_json("T" + std::to_string(i));
        const std::string text = list_json(many);
        const Parsed capped = parse(text, 1000);
        EXPECT_TRUE(text.size() > 100000 && text.size() < kThemeListMaxBytes);
        EXPECT_TRUE(capped.ok);
        EXPECT_EQ(capped.count, kThemeListMaxThemes);
        EXPECT_EQ(capped.skipped, 20u);
        const Parsed small = parse(text, 10);
        EXPECT_EQ(small.count, 10u);
    }
    // The size cap: 256 KB.
    {
        std::string padded = list_json(theme_json("Indigo"));
        padded.insert(padded.size() - 1, std::string(kThemeListMaxBytes, ' '));
        const Parsed p = parse(padded);
        EXPECT_TRUE(!p.ok);
        EXPECT_TRUE(p.error.find("256 KB") != std::string::npos);
    }
    // Deep nesting is refused, not recursed into.
    EXPECT_TRUE(!parse("{\"schema\":1,\"themes\":[],\"x\":" + std::string(500, '[') + std::string(500, ']') + "}").ok);

    // ---- what the browser installed
    {
        std::string records;
        records = theme_record_text(records, "Indigo", kSha);
        records = theme_record_text(records, "Other", std::string(64, 'a'));
        std::string sha;
        EXPECT_TRUE(theme_record_find(records, "Indigo", sha));
        EXPECT_EQ(sha, std::string(kSha));
        EXPECT_TRUE(!theme_record_find(records, "Nope", sha));
        records = theme_record_text(records, "Indigo", std::string(64, 'b'));
        EXPECT_TRUE(theme_record_find(records, "Indigo", sha));
        EXPECT_EQ(sha, std::string(64, 'b'));
        records = theme_record_without(records, "Indigo");
        EXPECT_TRUE(!theme_record_find(records, "Indigo", sha));
        EXPECT_TRUE(theme_record_find(records, "Other", sha));
        EXPECT_TRUE(!theme_record_find("../x " + std::string(64, 'a') + "\nbad line\n", "../x", sha));
    }

    // ---- what the card has of a listed theme
    EXPECT_TRUE(theme_install_state(false, false, "", "a") == ThemeInstall::No);
    EXPECT_TRUE(theme_install_state(false, true, "a", "a") == ThemeInstall::No);
    EXPECT_TRUE(theme_install_state(true, false, "", "a") == ThemeInstall::Other);
    EXPECT_TRUE(theme_install_state(true, true, "a", "a") == ThemeInstall::Current);
    EXPECT_TRUE(theme_install_state(true, true, "a", "b") == ThemeInstall::Update);

    // ---- zip entry paths
    {
        std::string file;
        EXPECT_TRUE(zip_theme_path_ok("Indigo/theme.ini", "Indigo", file));
        EXPECT_EQ(file, "theme.ini");
        EXPECT_TRUE(zip_theme_path_ok("Indigo/bar wide.png", "Indigo", file));
        EXPECT_TRUE(zip_theme_path_ok("Indigo/LICENSE", "Indigo", file));
        for (const char* name : {"", "theme.ini", "/Indigo/theme.ini", "/etc/passwd", "Indigo/../theme.ini", "../Indigo/theme.ini",
                                 "Indigo/..", "Indigo/.", "Indigo//theme.ini", "Indigo/sub/theme.ini", "Other/theme.ini",
                                 "Indigo2/theme.ini", "Indigo/.hidden", "Indigo\\theme.ini", "C:/Indigo/theme.ini",
                                 "C:\\theme.ini", "Indigo/a:b", "Indigo/", "indigo/theme.ini"}) {
            EXPECT_TRUE(!zip_theme_path_ok(name, "Indigo", file));
        }
        EXPECT_TRUE(!zip_theme_path_ok(std::string("Indigo/a\nb"), "Indigo", file));
        EXPECT_TRUE(!zip_theme_path_ok(std::string("Indigo/a\0b", 10), "Indigo", file));
        EXPECT_TRUE(!zip_theme_path_ok("Indigo/" + std::string(65, 'a'), "Indigo", file));
    }

    // ---- zip directory and plan
    {
        const auto zip = make_zip({{"Indigo/", "", 0}, {"Indigo/theme.ini", "[theme]\nname=I\n", 8}, {"Indigo/bar.png", "PNGDATA", 0}});
        std::vector<ZipEntry> entries;
        std::string error;
        EXPECT_TRUE(read(zip, entries, error));
        EXPECT_EQ(entries.size(), 3u);
        EXPECT_TRUE(entries[0].directory);
        std::vector<ZipThemeFile> plan;
        EXPECT_TRUE(zip_plan_theme(entries, "Indigo", limits(), plan, error));
        EXPECT_EQ(plan.size(), 2u);
        EXPECT_EQ(plan[0].file, "theme.ini");
        std::string text;
        const ZipSink collect = [&](const std::uint8_t* d, std::size_t n) { text.append(reinterpret_cast<const char*>(d), n); return true; };
        EXPECT_TRUE(zip_extract(zip.data(), zip.size(), entries[plan[0].entry], stored_block_inflate, collect, error));
        EXPECT_EQ(text, "[theme]\nname=I\n");
        text.clear();
        EXPECT_TRUE(zip_extract(zip.data(), zip.size(), entries[plan[1].entry], nullptr, collect, error));
        EXPECT_EQ(text, "PNGDATA");
        // The folder named, but for another theme.
        EXPECT_TRUE(!zip_plan_theme(entries, "Other", limits(), plan, error));
        // Counts and sizes against the list and the hard caps.
        EXPECT_TRUE(!zip_plan_theme(entries, "Indigo", limits(1), plan, error));
        EXPECT_TRUE(zip_plan_theme(entries, "Indigo", limits(2), plan, error));
        EXPECT_TRUE(!zip_plan_theme(entries, "Indigo", limits(10, 21), plan, error));
        EXPECT_TRUE(zip_plan_theme(entries, "Indigo", limits(10, 22), plan, error));
        ZipThemeLimits hard = limits(10, 1000);
        hard.max_files = 1;
        EXPECT_TRUE(!zip_plan_theme(entries, "Indigo", hard, plan, error));
        hard = limits(10, 1u << 30);
        hard.max_unpacked_size = 10;
        EXPECT_TRUE(!zip_plan_theme(entries, "Indigo", hard, plan, error));
        hard = limits(10, 1000);
        hard.max_file_size = 8;
        EXPECT_TRUE(!zip_plan_theme(entries, "Indigo", hard, plan, error));
    }
    // Bad entries.
    {
        std::vector<ZipEntry> entries;
        std::vector<ZipThemeFile> plan;
        std::string error;
        EXPECT_TRUE(read(make_zip({{"Indigo/theme.ini", "a", 0}, {"Indigo/../escape.txt", "x", 0}}), entries, error));
        EXPECT_TRUE(!zip_plan_theme(entries, "Indigo", limits(), plan, error));
        EXPECT_TRUE(read(make_zip({{"Indigo/theme.ini", "a", 0}, {"/abs.txt", "x", 0}}), entries, error));
        EXPECT_TRUE(!zip_plan_theme(entries, "Indigo", limits(), plan, error));
        EXPECT_TRUE(read(make_zip({{"Indigo/theme.ini", "a", 0}, {"Indigo/sub/", "", 0}}), entries, error));
        EXPECT_TRUE(!zip_plan_theme(entries, "Indigo", limits(), plan, error));
        EXPECT_TRUE(read(make_zip({{"Indigo/theme.ini", "a", 0}, {"Indigo/THEME.INI", "b", 0}}), entries, error));
        EXPECT_TRUE(!zip_plan_theme(entries, "Indigo", limits(), plan, error));
        EXPECT_TRUE(error.find("twice") != std::string::npos);
        EXPECT_TRUE(read(make_zip({{"Indigo/bar.png", "a", 0}}), entries, error));
        EXPECT_TRUE(!zip_plan_theme(entries, "Indigo", limits(), plan, error));
        EXPECT_TRUE(error.find("theme.ini") != std::string::npos);
    }
    // Damaged archives.
    {
        std::vector<ZipEntry> entries;
        std::string error;
        const auto good = make_zip({{"Indigo/theme.ini", "hello", 0}});
        EXPECT_TRUE(!zip_read_directory(good.data(), 10, entries, error));
        EXPECT_TRUE(!zip_read_directory(good.data(), 0, entries, error));
        // Cut short: no end record.
        EXPECT_TRUE(!zip_read_directory(good.data(), good.size() - 5, entries, error));
        // A directory offset past the file.
        auto bad = good;
        bad[bad.size() - 6] = 0xFF;
        bad[bad.size() - 5] = 0xFF;
        EXPECT_TRUE(!zip_read_directory(bad.data(), bad.size(), entries, error));
        // A packed size past the file.
        bad = good;
        const std::size_t central = bad.size() - 22 - (46 + 16);
        bad[central + 23] = 0x7F;
        EXPECT_TRUE(!zip_read_directory(bad.data(), bad.size(), entries, error));
        // An encrypted entry.
        bad = good;
        bad[central + 8] = 1;
        EXPECT_TRUE(!zip_read_directory(bad.data(), bad.size(), entries, error));
        // Too many entries for the cap.
        EXPECT_TRUE(!zip_read_directory(good.data(), good.size(), entries, error, 0));
        // An unsupported method.
        bad = good;
        bad[central + 10] = 12;
        EXPECT_TRUE(!zip_read_directory(bad.data(), bad.size(), entries, error));
        // zip64 markers.
        bad = good;
        for (int i = 0; i < 4; ++i) bad[central + 20 + i] = 0xFF;
        EXPECT_TRUE(!zip_read_directory(bad.data(), bad.size(), entries, error));
        // A local header that is not one.
        bad = good;
        bad[0] = 'X';
        EXPECT_TRUE(!zip_read_directory(bad.data(), bad.size(), entries, error));
        // Garbage of the right length.
        const std::vector<std::uint8_t> junk(500, 0x41);
        EXPECT_TRUE(!zip_read_directory(junk.data(), junk.size(), entries, error));
    }
    // Wrong CRC, wrong size, a failing sink and a failing inflater.
    {
        std::vector<ZipEntry> entries;
        std::string error;
        auto zip = make_zip({{"Indigo/theme.ini", "hello", 0, true}});
        EXPECT_TRUE(read(zip, entries, error));
        const ZipSink drop = [](const std::uint8_t*, std::size_t) { return true; };
        EXPECT_TRUE(!zip_extract(zip.data(), zip.size(), entries[0], nullptr, drop, error));
        EXPECT_TRUE(error.find("damaged") != std::string::npos);
        zip = make_zip({{"Indigo/theme.ini", "hello", 8}});
        EXPECT_TRUE(read(zip, entries, error));
        EXPECT_TRUE(zip_extract(zip.data(), zip.size(), entries[0], stored_block_inflate, drop, error));
        EXPECT_TRUE(!zip_extract(zip.data(), zip.size(), entries[0], nullptr, drop, error));
        const ZipSink full = [](const std::uint8_t*, std::size_t) { return false; };
        EXPECT_TRUE(!zip_extract(zip.data(), zip.size(), entries[0], stored_block_inflate, full, error));
        EXPECT_TRUE(error.find("written") != std::string::npos);
        // An entry that says it is smaller than what the inflater makes.
        ZipEntry lying = entries[0];
        lying.unpacked_size = 3;
        EXPECT_TRUE(!zip_extract(zip.data(), zip.size(), lying, stored_block_inflate, drop, error));
    }

    if (g_failures) return 1;
    std::cout << "themebrowse tests passed" << std::endl;
    return 0;
}
