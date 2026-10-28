// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/emulatorart.hpp"
#include "riftwii/coverart.hpp"
#include "riftwii/emulatorfile.hpp"
#include "riftwii/settingsfile.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;
#define EXPECT(condition) do { if (!(condition)) { std::cerr << __LINE__ << ": " #condition "\n"; ++failures; } } while (0)

void names_and_urls() {
    using namespace riftwii;
    EXPECT(libretro_cover_name("A&B*/:`<>?\\|\"") == "A_B__________.png");
    EXPECT(libretro_cover_name("Power Stone (USA)") == "Power Stone (USA).png");
    EXPECT(libretro_cover_name("").empty());
    EXPECT(libretro_cover_name(std::string(252, 'a')).empty());
    EXPECT(libretro_cover_url("Sega - Dreamcast", "Power Stone (USA).png") ==
           "https://thumbnails.libretro.com/Sega%20-%20Dreamcast/Named_Boxarts/Power%20Stone%20%28USA%29.png");
    EXPECT(libretro_cover_url("../private").empty());
    EXPECT(libretro_cover_url("Dreamcast", "../x.png").empty());
    EXPECT(emulator_cover_id("emu-" + std::string(64, 'a')));
    EXPECT(!emulator_cover_id("emu-" + std::string(64, 'z')));
    for (const auto* name : {"..", ".", "../x", "x/y", "x\\y", "x:", "x\n", "x?", ".hidden", "x.", "x "}) {
        EXPECT(!emulator_art_component(name));
    }
}

void index_parsing() {
    using namespace riftwii;
    std::vector<std::string> names;
    const std::string html =
        "<html><a href=\"../\">parent</a>"
        "<a href=\"Power%20Stone%20%28USA%29.png\">Power Stone</a>"
        "<a href='A%26B.png'>A&amp;B</a><a href=\"Power%20Stone%20%28USA%29.png\">duplicate</a>"
        "<a href=\"%2e%2e%2fsecret.png\">bad</a><a href=\"https://evil/x.png\">bad</a>"
        "<a href=\"%00.png\">bad</a><a href=\"x%0a.png\">bad</a><a href=\"x.png?key=x\">bad</a>"
        "<a href=\"%5cprivate.png\">bad</a><a href=\"%GG.png\">bad</a></html>";
    EXPECT(parse_libretro_index(html, names));
    EXPECT(names.size() == 2);
    EXPECT(names[0] == "A&B.png");
    EXPECT(names[1] == "Power Stone (USA).png");
    std::string cache;
    EXPECT(serialize_emulator_index(names, cache));
    std::vector<std::string> restored;
    EXPECT(parse_emulator_index(cache, restored));
    EXPECT(restored == names);
    EXPECT(!parse_emulator_index(cache + "../evil.png\n", restored));
    EXPECT(restored.empty());
    EXPECT(!parse_emulator_index(cache.substr(0, cache.size() - 1), restored));
    EXPECT(!parse_libretro_index(std::string(kEmulatorIndexMax + 1, 'x'), names));
    EXPECT(names.empty());
    EXPECT(!parse_libretro_index("<html>error</html>", names));
    EXPECT(!parse_libretro_index("<a href=\"x.png", names));
    EXPECT(!parse_libretro_index("<a href=\"" + std::string(252, 'x') + ".png\">bad</a>", names));
    std::string crowded;
    for (std::size_t i = 0; i <= kEmulatorIndexNamesMax; ++i) crowded += "<a href=\"x.png\">x</a>";
    EXPECT(!parse_libretro_index(crowded, names));
    EXPECT(names.empty());
    EXPECT(!serialize_emulator_index({"../escape.png"}, cache));
}

void matching() {
    using namespace riftwii;
    const std::vector<std::string> names = {
        "Power Stone (Japan).png", "Power Stone (Europe).png", "Power Stone (USA).png", "Power Stone 2 (USA).png"};
    EXPECT(match_emulator_cover("power_stone", names) == "Power Stone (USA).png");
    EXPECT(match_emulator_cover("Power Stone (Europe) [rip]", names) == "Power Stone (Europe).png");
    EXPECT(match_emulator_cover("Power Stone (Japan)", names) == "Power Stone (Japan).png");
    EXPECT(match_emulator_cover("Power Stone (USA, Japan)", names) == "Power Stone (USA).png");
    EXPECT(match_emulator_cover("Power Stone 2", names) == "Power Stone 2 (USA).png");
    EXPECT(match_emulator_cover("Power Stone 3", names).empty());
    EXPECT(match_emulator_cover("Power Stone", {"Power Stone 2 (USA).png"}).empty());
    EXPECT(match_emulator_cover("Super Street Fighter II", {"Super Street Fighter III (USA).png"}).empty());
    EXPECT(match_emulator_cover("Game \xE3\x81\x82", {"Game \xE3\x81\x84 (Japan).png"}).empty());
    EXPECT(match_emulator_cover("Game \xE3\x81\x82", {"Game \xE3\x81\x82 (Japan).png"}) == "Game \xE3\x81\x82 (Japan).png");
    EXPECT(match_emulator_cover("Pix", {"Pax (USA).png"}).empty());
    EXPECT(match_emulator_cover("Sonic Adventure Delux", {"Sonic Adventure Deluxe (USA).png"}) == "Sonic Adventure Deluxe (USA).png");
    EXPECT(match_emulator_cover("Crazy Taxi", {"Crazy Taxi (Japan).png", "Crazy Taxi (Europe).png"}) == "Crazy Taxi (Europe).png");
    EXPECT(match_emulator_cover("abcdefghijklmno", {"abcdefghijklmnp.png", "abcdefghijklmnn.png"}).empty());
    EXPECT(match_emulator_cover("", names).empty());
}

void misses() {
    using namespace riftwii;
    const std::int64_t now = 1800000000;
    const auto miss = emulator_art_miss("Sega - Dreamcast", now);
    EXPECT(emulator_art_miss_fresh(miss, "Sega - Dreamcast", now));
    EXPECT(emulator_art_miss_fresh(miss, "Sega - Dreamcast", now + kEmulatorArtWeek - 1));
    EXPECT(!emulator_art_miss_fresh(miss, "Sega - Dreamcast", now + kEmulatorArtWeek));
    EXPECT(!emulator_art_miss_fresh(miss, "Other system", now));
    EXPECT(!emulator_art_miss_fresh(miss, "Sega - Dreamcast", now - 1));
    EXPECT(!emulator_art_miss_fresh(miss + "junk", "Sega - Dreamcast", now));
    EXPECT(!emulator_art_miss_fresh("", "Sega - Dreamcast", now));
    EXPECT(emulator_art_miss("x\ny", now).empty());
    EXPECT(emulator_art_miss("x", 0).empty());
}

void steamgrid() {
    using namespace riftwii;
    EXPECT(steamgrid_key_valid(std::string(32, 'a')));
    EXPECT(!steamgrid_key_valid("secret\r\nHost: evil"));
    EXPECT(!steamgrid_key_valid(std::string(129, 'a')));
    EXPECT(steamgrid_image_url("https://cdn2.steamgriddb.com/grid/abc.png"));
    for (const auto* url : {"http://cdn2.steamgriddb.com/grid/a.png", "https://evil/grid/a.png",
                           "https://cdn2.steamgriddb.com.evil/grid/a.png", "https://steamgriddb.com@evil/grid/a.png",
                           "https://cdn2.steamgriddb.com:444/grid/a.png", "https://cdn2.steamgriddb.com/grid/a.png?key=x"}) {
        EXPECT(!steamgrid_image_url(url));
    }
    std::vector<SteamGridGame> games;
    EXPECT(parse_steamgrid_search(" {\"success\":true,\"data\":[{\"id\":42,\"name\":\"Power Stone\",\"unknown\":{\"x\":[true,null,1.5]}}]} ", games));
    EXPECT(games.size() == 1 && games[0].id == 42);
    EXPECT(match_steamgrid_game("Power Stone (USA)", games) == 42);
    games.push_back({43, "Power Stone 2"});
    EXPECT(match_steamgrid_game("Power Stone", games) == 42);
    games.push_back({44, "Power Stone"});
    EXPECT(match_steamgrid_game("Power Stone", games) == 0);
    EXPECT(parse_steamgrid_search("{\"success\":true,\"data\":[{\"id\":1,\"name\":\"Caf\\u00e9 \\ud83c\\udfae\"}]}", games));
    EXPECT(games[0].name == "Caf\xC3\xA9 \xF0\x9F\x8E\xAE");
    for (const auto* json : {
        "{\"success\":false,\"data\":[]}", "{\"success\":true,\"success\":false,\"data\":[]}",
        "{\"success\":true,\"data\":[{\"id\":1,\"id\":2,\"name\":\"x\"}]}",
        "{\"success\":true,\"data\":[{\"id\":-1,\"name\":\"x\"}]}",
        "{\"success\":true,\"data\":[{\"id\":01,\"name\":\"x\"}]}",
        "{\"success\":true,\"data\":[{\"id\":1.0,\"name\":\"x\"}]}",
        "{\"success\":true,\"data\":[{\"id\":4294967296,\"name\":\"x\"}]}",
        "{\"success\":true,\"data\":[{\"id\":1,\"name\":\"x\\u0000\"}]}",
        "{\"success\":true,\"data\":[{\"id\":1,\"name\":\"\\ud800\"}]}",
        "{\"success\":true,\"data\":[]} trailing", "{\"success\":true,\"data\":[,]}",
        "{\"success\":true,\"data\":[],}", "[]", "null", "{bad}"}) {
        EXPECT(!parse_steamgrid_search(json, games));
        EXPECT(games.empty());
    }
    EXPECT(!parse_steamgrid_search(std::string(kSteamGridJsonMax + 1, ' '), games));
    EXPECT(!parse_steamgrid_search("{\"success\":true,\"data\":[],\"x\":" + std::string(18, '[') + "0" + std::string(18, ']') + "}", games));
    std::string crowded = "{\"success\":true,\"data\":[";
    for (int i = 0; i < 257; ++i) {
        if (i) crowded += ',';
        crowded += "{\"id\":1,\"name\":\"x\"}";
    }
    crowded += "]}";
    EXPECT(!parse_steamgrid_search(crowded, games));
    std::vector<std::string> urls;
    EXPECT(parse_steamgrid_grids("{\"success\":true,\"data\":[{\"url\":\"https:\\/\\/cdn2.steamgriddb.com/grid/a.png\",\"width\":600,\"height\":900,\"mime\":\"image/png\"}]}", urls));
    EXPECT(urls.size() == 1);
    EXPECT(parse_steamgrid_grids("{\"success\":true,\"data\":[{\"url\":\"https://evil/grid/a.png\",\"width\":600,\"height\":900,\"mime\":\"image/png\"}]}", urls));
    EXPECT(urls.empty());
    EXPECT(!parse_steamgrid_grids("{\"success\":true,\"data\":[{\"url\":\"x\"}]}", urls));
    EXPECT(parse_steamgrid_grids("{\"success\":true,\"data\":[]}", urls));
}

void settings_and_manifest() {
    using namespace riftwii;
    LoaderSettings settings;
    EXPECT(settings.emulator_covers && !settings.steamgrid_covers);
    settings.parse("emulator_covers = off\nsteamgrid_covers = on\n");
    EXPECT(!settings.emulator_covers && settings.steamgrid_covers);
    LoaderSettings restored;
    restored.parse(settings.serialize());
    EXPECT(!restored.emulator_covers && restored.steamgrid_covers);
    CoverDownloads downloads("Emulator covers", "cover", "the emulator cover search");
    EXPECT(!downloads.count(CoverOutcome::NotFound));
    EXPECT(downloads.take_summary().find("the emulator cover search has no cover") != std::string::npos);
    downloads.reset();
    EXPECT(!downloads.count(CoverOutcome::NotFound));
    EXPECT(downloads.take_summary().find("GameTDB") == std::string::npos);
    restored.parse("emulator_covers = bogus\nsteamgrid_covers = bogus\n");
    EXPECT(!restored.emulator_covers && restored.steamgrid_covers);
    const std::string manifest = "format = 1\nname = Example\nsystem = Dreamcast\ndol = boot.dol\ngames = sd:/roms/dc\nextensions = chd\nargs = {game}\n";
    EmulatorFile file;
    std::string error;
    std::vector<std::string> warnings;
    EXPECT(parse_emulator_file(manifest + "covers = Sega - Dreamcast\n", "sd:/apps/test", file, error, warnings));
    EXPECT(file.covers == "Sega - Dreamcast" && warnings.empty());
    EXPECT(parse_emulator_file(manifest, "sd:/apps/test", file, error, warnings));
    EXPECT(file.covers.empty());
    for (const auto& name : std::vector<std::string>{"../private", "x/y", "x\\y", "", std::string(65, 'a')}) {
        EXPECT(!parse_emulator_file(manifest + "covers = " + name + "\n", "sd:/apps/test", file, error, warnings));
    }
}
}  // namespace

int main() {
    names_and_urls();
    index_parsing();
    matching();
    misses();
    steamgrid();
    settings_and_manifest();
    if (failures) std::cerr << failures << " emulator cover check(s) failed.\n";
    return failures ? 1 : 0;
}
