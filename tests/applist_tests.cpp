// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/applist.hpp"
#include "riftwii/themelist.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

namespace {

const char* const kSha = "8cf92160ae5267827e7e2f4b9a9730f001e9f6d9ce5647d202e5fa8589ca5617";

// One app as the list writes it; `replace` swaps one "key":value text for
// another, to break one rule at a time.
std::string app_json(const std::string& id = "snes9xgx", const std::string& replace_from = "",
                     const std::string& replace_to = "") {
    std::string text =
        "{\"id\":\"" + id + "\",\"name\":\"Snes9x GX\",\"author\":\"Tantric\",\"version\":\"5.0.6\","
        "\"released\":\"2026-11-18\",\"category\":\"emulators\",\"description\":\"Super Nintendo Emulator\","
        "\"license\":\"GPL-2.0\",\"page\":\"https://github.com/dborth/snes9xgx\","
        "\"icon\":\"https://riftwii.github.io/apps/icons/" + id + ".png\",\"platforms\":[\"wii\",\"vwii\"],"
        "\"controllers\":[\"wiimote\",\"classic\",\"gamecube\",\"zapper\"],"
        "\"data_folders\":[\"snes9xgx\"],"
        "\"download\":{\"url\":\"https://github.com/dborth/snes9xgx/releases/download/5.0.6/Snes9xGX-5.0.6.zip\","
        "\"format\":\"zip\",\"size\":2500000,\"sha256\":\"" + std::string(kSha) + "\",\"files\":40,"
        "\"unpacked_size\":6000000},"
        "\"install\":[{\"from\":\"apps/snes9xgx/\",\"to\":\"apps/" + id + "/\"},"
        "{\"from\":\"snes9xgx/\",\"to\":\"snes9xgx/\"}]}";
    if (!replace_from.empty()) {
        const std::size_t at = text.find(replace_from);
        if (at == std::string::npos) {
            std::cerr << "test bug: no " << replace_from << std::endl;
            g_failures++;
        } else {
            text.replace(at, replace_from.size(), replace_to);
        }
    }
    return text;
}

std::string list_json(const std::string& apps, const std::string& schema = "1") {
    return "{\"schema\":" + schema + ",\"generated\":\"2026-11-23\",\"apps\":[" + apps + "]}";
}

struct Parsed {
    std::vector<AppListing> apps;
    std::size_t count = 0, skipped = 0;
    std::string error;
    bool ok = false;
};

Parsed parse(const std::string& text, std::size_t max = 16) {
    Parsed p;
    p.apps.resize(max);
    p.ok = parse_app_list(text, p.apps.data(), max, p.count, p.skipped, p.error);
    return p;
}

// Whether one app, changed by `from` -> `to`, is left out.
bool left_out(const std::string& from, const std::string& to) {
    const Parsed p = parse(list_json(app_json("snes9xgx", from, to)));
    return p.ok && p.count == 0 && p.skipped == 1;
}

void test_reads_an_app() {
    const Parsed p = parse(list_json(app_json()));
    EXPECT_TRUE(p.ok);
    EXPECT_EQ(p.count, 1u);
    EXPECT_EQ(p.skipped, 0u);
    const AppListing& a = p.apps[0];
    EXPECT_EQ(std::string(a.id), "snes9xgx");
    EXPECT_EQ(std::string(a.name), "Snes9x GX");
    EXPECT_EQ(std::string(a.version), "5.0.6");
    EXPECT_EQ(std::string(a.released), "2026-11-18");
    EXPECT_EQ(std::string(a.category), "emulators");
    EXPECT_EQ(std::string(a.sha256), kSha);
    EXPECT_EQ(a.size, 2500000u);
    EXPECT_EQ(a.files, 40u);
    EXPECT_EQ(a.unpacked_size, 6000000u);
    EXPECT_TRUE(a.format == AppFormat::Zip);
    EXPECT_EQ(a.platforms, kAppOnWii | kAppOnVWii);
    // A controller this RiftWii does not know ("zapper") is passed over.
    EXPECT_EQ(a.controllers, kAppWiiRemote | kAppClassic | kAppGameCube);
    EXPECT_TRUE(app_needs_no_wii_remote(a.controllers));
    EXPECT_TRUE(!app_needs_no_wii_remote(kAppWiiRemote | kAppNunchuk | kAppKeyboard));
    EXPECT_TRUE(!app_needs_no_wii_remote(0));
    EXPECT_EQ(a.install_count, 2);
    EXPECT_EQ(a.data_folder_count, 1);
    EXPECT_EQ(std::string(a.data_folders[0]), "snes9xgx");
}

void test_list_level_errors() {
    Parsed p = parse(list_json(app_json(), "2"));
    EXPECT_TRUE(!p.ok);
    EXPECT_TRUE(p.error.find("newer RiftWii") != std::string::npos);
    p = parse("{\"apps\":[]}");
    EXPECT_TRUE(!p.ok);
    p = parse(list_json(app_json()).substr(0, 200));  // cut short in the middle of an app
    EXPECT_TRUE(!p.ok);
    p = parse(list_json(app_json()) + "x");
    EXPECT_TRUE(!p.ok);
    p = parse("[]");
    EXPECT_TRUE(!p.ok);
    const std::string huge(kAppListMaxBytes + 1, ' ');
    p = parse(huge);
    EXPECT_TRUE(!p.ok);
    EXPECT_TRUE(p.error.find("2 MB") != std::string::npos);
    // An empty list is a list.
    p = parse(list_json(""));
    EXPECT_TRUE(p.ok);
    EXPECT_EQ(p.count, 0u);
}

void test_unknown_fields_are_skipped() {
    const Parsed p = parse("{\"schema\":1,\"future\":{\"a\":[1,2,{\"b\":null}]},\"apps\":[" +
                           app_json("snes9xgx", "\"author\":", "\"later\":[true,false],\"author\":") + "]}");
    EXPECT_TRUE(p.ok);
    EXPECT_EQ(p.count, 1u);
}

void test_app_rules() {
    // The id is the app's folder.
    EXPECT_TRUE(left_out("\"id\":\"snes9xgx\"", "\"id\":\"../snes9xgx\""));
    EXPECT_TRUE(left_out("\"id\":\"snes9xgx\"", "\"id\":\"..\""));
    EXPECT_TRUE(left_out("\"id\":\"snes9xgx\"", "\"id\":\"a/b\""));
    EXPECT_TRUE(left_out("\"id\":\"snes9xgx\"", "\"id\":\".hidden\""));
    EXPECT_TRUE(left_out("\"id\":\"snes9xgx\"", "\"id\":\"\""));
    // Texts.
    EXPECT_TRUE(left_out("\"name\":\"Snes9x GX\"", "\"name\":\"   \""));
    EXPECT_TRUE(left_out("\"name\":\"Snes9x GX\"", "\"name\":5"));
    EXPECT_TRUE(left_out("\"version\":\"5.0.6\"", "\"version\":\"" + std::string(40, 'v') + "\""));
    EXPECT_TRUE(left_out("\"category\":\"emulators\"", "\"category\":\"Emulators\""));
    EXPECT_TRUE(left_out("\"released\":\"2026-11-18\"", "\"released\":\"18/11/2026\""));
    // Addresses.
    // Plain http is fine for a download (the SHA-256 pins it), never for a page.
    EXPECT_TRUE(!left_out("\"url\":\"https://github.com", "\"url\":\"http://github.com"));
    EXPECT_TRUE(left_out("\"url\":\"https://github.com", "\"url\":\"ftp://github.com"));
    EXPECT_TRUE(left_out("\"page\":\"https://", "\"page\":\"http://"));
    EXPECT_TRUE(left_out("\"url\":\"https://github.com", "\"url\":\"https://user@github.com"));
    EXPECT_TRUE(left_out("\"url\":\"https://github.com", "\"url\":\"https://github.com:8080"));
    EXPECT_TRUE(left_out("\"url\":\"https://github.com/dborth", "\"url\":\"https://192.168.1.2/dborth"));
    EXPECT_TRUE(left_out("releases/download/5.0.6/", "releases/download/5.0.6/../../"));
    EXPECT_TRUE(left_out("\"page\":\"https://github.com/dborth/snes9xgx\"", "\"page\":\"javascript:alert(1)\""));
    EXPECT_TRUE(left_out("\"icon\":\"https://riftwii.github.io/", "\"icon\":\"https://example.com/"));
    // The download.
    EXPECT_TRUE(left_out(kSha, "8CF92160AE5267827E7E2F4B9A9730F001E9F6D9CE5647D202E5FA8589CA5617"));
    EXPECT_TRUE(left_out("\"size\":2500000", "\"size\":0"));
    EXPECT_TRUE(left_out("\"size\":2500000", "\"size\":999999999"));
    EXPECT_TRUE(left_out("\"size\":2500000", "\"size\":-5"));
    EXPECT_TRUE(left_out("\"files\":40", "\"files\":99999"));
    EXPECT_TRUE(left_out("\"unpacked_size\":6000000", "\"unpacked_size\":999999999"));
    EXPECT_TRUE(left_out("\"format\":\"zip\"", "\"format\":\"exe\""));
    // Platforms: at least one the list knows.
    EXPECT_TRUE(left_out("\"platforms\":[\"wii\",\"vwii\"]", "\"platforms\":[]"));
    EXPECT_TRUE(left_out("\"platforms\":[\"wii\",\"vwii\"]", "\"platforms\":[\"switch\"]"));
    // Controllers: no field is "not known"; a broken list (more than five
    // names, not strings) only loses the pictures, not the app.
    EXPECT_TRUE(!left_out("\"controllers\":[\"wiimote\",", "\"controllers2\":[\"wiimote\","));
    for (const char* broken_list : {"\"controllers\":[\"a\",\"b\",\"c\",\"wiimote\",", "\"controllers\":[7,\"wiimote\","}) {
        const Parsed broken = parse(list_json(app_json("snes9xgx", "\"controllers\":[\"wiimote\",", broken_list)));
        EXPECT_TRUE(broken.ok);
        EXPECT_EQ(broken.count, 1u);
        EXPECT_EQ(broken.skipped, 0u);
        if (broken.count == 1) EXPECT_EQ(broken.apps[0].controllers, 0u);
    }
    // An app named as its own better replacement.
    EXPECT_TRUE(left_out("\"platforms\"", "\"newer\":\"snes9xgx\",\"platforms\""));
    EXPECT_TRUE(!left_out("\"platforms\"", "\"newer\":\"snes9xgx-ultimate\",\"platforms\""));
}

void test_data_folders_and_install_rules() {
    // Folders the Wii, RiftWii or other loaders own are never an app's data.
    EXPECT_TRUE(left_out("\"data_folders\":[\"snes9xgx\"]", "\"data_folders\":[\"riftwii\"]"));
    EXPECT_TRUE(left_out("\"data_folders\":[\"snes9xgx\"]", "\"data_folders\":[\"Private\"]"));
    EXPECT_TRUE(left_out("\"data_folders\":[\"snes9xgx\"]", "\"data_folders\":[\"title\"]"));
    EXPECT_TRUE(left_out("\"data_folders\":[\"snes9xgx\"]", "\"data_folders\":[\"apps\"]"));
    EXPECT_TRUE(left_out("\"data_folders\":[\"snes9xgx\"]", "\"data_folders\":[\"a\",\"b\",\"c\",\"d\",\"e\"]"));
    // A rule writes only into the app's own folder or its data folders.
    EXPECT_TRUE(left_out("\"to\":\"apps/snes9xgx/\"", "\"to\":\"apps/fceugx/\""));
    EXPECT_TRUE(left_out("\"to\":\"snes9xgx/\"", "\"to\":\"riftwii/\""));
    EXPECT_TRUE(left_out("\"to\":\"snes9xgx/\"", "\"to\":\"snes9xgx/../riftwii/\""));
    EXPECT_TRUE(left_out("\"to\":\"snes9xgx/\"", "\"to\":\"/snes9xgx/\""));
    EXPECT_TRUE(left_out("\"from\":\"snes9xgx/\"", "\"from\":\"../snes9xgx/\""));
    // A folder goes to a folder.
    EXPECT_TRUE(left_out("\"to\":\"snes9xgx/\"", "\"to\":\"snes9xgx/file\""));
    // A zip needs its rules; a DOL takes none.
    EXPECT_TRUE(left_out("\"install\":[", "\"ignored\":["));
    EXPECT_TRUE(left_out("\"format\":\"zip\"", "\"format\":\"dol\""));
    // Too many rules.
    std::string many;
    for (int i = 0; i < 7; ++i) many += std::string(i ? "," : "") + "{\"from\":\"x/\",\"to\":\"apps/snes9xgx/\"}";
    EXPECT_TRUE(left_out("\"install\":[", "\"install\":[" + many + "],\"old\":["));
    // A rule that is not an object leaves out its app, and only its app.
    const Parsed p = parse(list_json(app_json("bad", "\"install\":[", "\"install\":[null,7,\"x\",") + "," +
                                     app_json("good")));
    EXPECT_TRUE(p.ok);
    EXPECT_EQ(p.count, 1u);
    EXPECT_EQ(p.skipped, 1u);
    EXPECT_EQ(std::string(p.apps[0].id), "good");
}

void test_other_formats_are_listed() {
    // A 7z or a DOL: shown, with nothing to unpack.
    const std::string seven = app_json("nulldc4wii", "\"format\":\"zip\"", "\"format\":\"7z\"");
    std::string no_rules = seven;
    const std::size_t at = no_rules.find(",\"install\":");
    no_rules = no_rules.substr(0, at) + "}";
    Parsed p = parse(list_json(no_rules));
    EXPECT_TRUE(p.ok);
    EXPECT_EQ(p.count, 1u);
    EXPECT_TRUE(p.apps[0].format == AppFormat::SevenZip);
    std::string path;
    EXPECT_TRUE(!app_install_path(p.apps[0], "apps/nulldc4wii/boot.dol", path));
}

void test_duplicates_and_room() {
    Parsed p = parse(list_json(app_json("snes9xgx") + "," + app_json("SNES9XGX") + "," + app_json("fceugx",
        "\"from\":\"apps/snes9xgx/\"", "\"from\":\"apps/fceugx/\"")));
    EXPECT_TRUE(p.ok);
    EXPECT_EQ(p.count, 2u);
    EXPECT_EQ(p.skipped, 1u);
    EXPECT_EQ(std::string(p.apps[1].id), "fceugx");
    // More apps than room: the rest are counted as left out.
    p = parse(list_json(app_json("a1") + "," + app_json("a2") + "," + app_json("a3")), 2);
    EXPECT_TRUE(p.ok);
    EXPECT_EQ(p.count, 2u);
    EXPECT_EQ(p.skipped, 1u);
}

void test_install_paths() {
    const Parsed p = parse(list_json(app_json()));
    EXPECT_EQ(p.count, 1u);
    const AppListing& a = p.apps[0];
    std::string path;
    EXPECT_TRUE(app_install_path(a, "apps/snes9xgx/boot.dol", path));
    EXPECT_EQ(path, "apps/snes9xgx/boot.dol");
    EXPECT_TRUE(app_install_path(a, "snes9xgx/roms/readme.txt", path));
    EXPECT_EQ(path, "snes9xgx/roms/readme.txt");
    // Entries no rule covers are not installed.
    EXPECT_TRUE(!app_install_path(a, "readme.txt", path));
    EXPECT_TRUE(!app_install_path(a, "apps/other/boot.dol", path));
    // Folder entries, and anything that climbs or hides a separator.
    EXPECT_TRUE(!app_install_path(a, "apps/snes9xgx/", path));
    EXPECT_TRUE(!app_install_path(a, "apps/snes9xgx/sub/", path));
    EXPECT_TRUE(!app_install_path(a, "apps/snes9xgx/../../riftwii/boot.dol", path));
    EXPECT_TRUE(!app_install_path(a, "apps/snes9xgx/a\\..\\b", path));
    EXPECT_TRUE(!app_install_path(a, "apps/snes9xgx//boot.dol", path));
    EXPECT_TRUE(!app_install_path(a, "apps/snes9xgx/c:boot.dol", path));
    // A rule for the download's top level, and one for a single file.
    const Parsed top = parse(list_json(app_json("wiidonut", "\"from\":\"apps/snes9xgx/\"", "\"from\":\"\"")));
    EXPECT_EQ(top.count, 1u);
    EXPECT_TRUE(app_install_path(top.apps[0], "boot.dol", path));
    EXPECT_EQ(path, "apps/wiidonut/boot.dol");
    const Parsed one = parse(list_json(app_json("solo", "{\"from\":\"apps/snes9xgx/\",\"to\":\"apps/solo/\"}",
                                                "{\"from\":\"Solo.dol\",\"to\":\"apps/solo/boot.dol\"}")));
    EXPECT_EQ(one.count, 1u);
    EXPECT_TRUE(app_install_path(one.apps[0], "Solo.dol", path));
    EXPECT_EQ(path, "apps/solo/boot.dol");
    EXPECT_TRUE(!app_install_path(one.apps[0], "Solo.dol.bak", path));
    // A "*" part takes any one folder: a release's versioned top folder.
    const Parsed star = parse(list_json(app_json("gcmm", "\"from\":\"apps/snes9xgx/\"", "\"from\":\"*/wii/gcmm/\"")));
    EXPECT_EQ(star.count, 1u);
    EXPECT_TRUE(app_install_path(star.apps[0], "gcmm_1.5.2/wii/gcmm/boot.dol", path));
    EXPECT_EQ(path, "apps/gcmm/boot.dol");
    EXPECT_TRUE(app_install_path(star.apps[0], "gcmm_1.6/wii/gcmm/meta.xml", path));
    EXPECT_TRUE(!app_install_path(star.apps[0], "wii/gcmm/boot.dol", path));
    EXPECT_TRUE(!app_install_path(star.apps[0], "a/b/wii/gcmm/boot.dol", path));
    // What `*` takes is checked as a written part is.
    EXPECT_TRUE(!app_install_path(star.apps[0], "../wii/gcmm/boot.dol", path));
    EXPECT_TRUE(!app_install_path(star.apps[0], "..\\wii\\gcmm\\boot.dol", path));
    EXPECT_TRUE(!app_install_path(star.apps[0], "./wii/gcmm/boot.dol", path));
    // Only whole parts, and only in `from`.
    EXPECT_TRUE(left_out("\"from\":\"apps/snes9xgx/\"", "\"from\":\"apps/snes9x*/\""));
    EXPECT_TRUE(left_out("\"to\":\"snes9xgx/\"", "\"to\":\"snes9xgx/*/\""));
    EXPECT_TRUE(left_out("{\"from\":\"apps/snes9xgx/\",\"to\":\"apps/snes9xgx/\"}",
                         "{\"from\":\"*\",\"to\":\"apps/snes9xgx/boot.dol\"}"));
}

void test_hostile_lists() {
    // RiftWii's own folders are no app's.
    EXPECT_TRUE(!app_id_ok("riftwii"));
    EXPECT_TRUE(!app_id_ok("RiftWii"));
    EXPECT_TRUE(!app_id_ok("riftwii_channel"));
    EXPECT_TRUE(left_out("\"id\":\"snes9xgx\"", "\"id\":\"RIFTWII\""));
    // FAT drops a trailing dot or space: "boot.dol." would land on boot.dol.
    const Parsed p = parse(list_json(app_json()));
    std::string path;
    EXPECT_TRUE(!app_install_path(p.apps[0], "apps/snes9xgx/boot.dol.", path));
    EXPECT_TRUE(!app_install_path(p.apps[0], "apps/snes9xgx/boot.dol ", path));
    EXPECT_TRUE(!app_install_path(p.apps[0], "apps/snes9xgx/.../boot.dol", path));
    // Broken UTF-8 in a path or a name.
    EXPECT_TRUE(!app_install_path(p.apps[0], "apps/snes9xgx/\xC0\xAF" "boot.dol", path));
    EXPECT_TRUE(!app_install_path(p.apps[0], "apps/snes9xgx/\x80.dol", path));
    // Backslashes from Windows zip tools separate folders too.
    EXPECT_TRUE(app_install_path(p.apps[0], "apps\\snes9xgx\\boot.dol", path));
    EXPECT_EQ(path, "apps/snes9xgx/boot.dol");
    EXPECT_TRUE(app_install_path(p.apps[0], "apps/snes9xgx/r\xC3\xA9sum\xC3\xA9.txt", path));
    EXPECT_TRUE(left_out("\"name\":\"Snes9x GX\"", "\"name\":\"Snes9x \xED\xA0\x80\""));
    // One string too long to keep leaves out its app, and only its app,
    // even in a field nobody reads.
    const std::string huge(5000, 'x');
    Parsed two = parse(list_json(app_json("long", "\"author\":", "\"later\":\"" + huge + "\",\"author\":") + "," +
                                 app_json("short", "\"name\":\"Snes9x GX\"", "\"name\":\"" + huge + "\"") + "," +
                                 app_json("fine")));
    EXPECT_TRUE(two.ok);
    EXPECT_EQ(two.count, 2u);
    EXPECT_EQ(two.skipped, 1u);
    // Hosts that are addresses in disguise.
    EXPECT_TRUE(!app_download_url_ok("https://0x7f.0x0.0x0.1/a.zip"));
    EXPECT_TRUE(!app_download_url_ok("https://2130706433/a.zip"));
    EXPECT_TRUE(!app_download_url_ok("https://a..b.com/a.zip"));
    EXPECT_TRUE(!app_download_url_ok("https://-a.com/a.zip"));
    EXPECT_TRUE(!app_download_url_ok("https://a.com1/a.zip"));
    EXPECT_TRUE(app_download_url_ok("https://objects.github-cdn.example.com/a.zip"));
}

void test_checks_on_their_own() {
    EXPECT_TRUE(app_id_ok("Wii64 (glN64)"));
    EXPECT_TRUE(!app_id_ok("a:b"));
    EXPECT_TRUE(!app_id_ok("name."));
    EXPECT_TRUE(app_category_ok("games"));
    EXPECT_TRUE(!app_category_ok("game tools"));
    EXPECT_TRUE(app_data_folder_ok("fceugx"));
    EXPECT_TRUE(!app_data_folder_ok("NANDS"));
    EXPECT_TRUE(app_download_url_ok("https://hbbarchive.oscwii.org/legacy/danbo.zip"));
    EXPECT_TRUE(app_download_url_ok("http://www.toxicbreakfast.com/cgi-bin/download?file=yahtzwii.zip"));
    EXPECT_TRUE(!app_page_url_ok("http://www.toxicbreakfast.com/"));
    EXPECT_TRUE(!app_download_url_ok("https://hbbarchive.oscwii.org"));
    EXPECT_TRUE(!app_download_url_ok("https://.org/a"));
    EXPECT_TRUE(!app_download_url_ok("https://[::1]/a"));
    EXPECT_TRUE(app_icon_url_ok("https://riftwii.github.io/apps/icons/a.png"));
    EXPECT_TRUE(!app_icon_url_ok("https://riftwii.github.io.evil.com/a.png"));
}

// The theme list reads through the same JSON reader: still the same.
void test_theme_list_still_reads() {
    const std::string themes =
        "{\"schema\":1,\"themes\":[{\"folder\":\"Indigo\",\"name\":\"Indigo\",\"author\":\"RiftWii\","
        "\"license\":\"CC0-1.0\",\"size\":100,\"sha256\":\"" + std::string(kSha) + "\",\"files\":2,"
        "\"unpacked_size\":200,\"has_music\":false,\"zip\":\"https://riftwii.github.io/themes/i.zip\","
        "\"preview\":\"https://riftwii.github.io/themes/i.png\"}]}";
    std::vector<ThemeListing> out(4);
    std::size_t count = 0, skipped = 0;
    std::string error;
    EXPECT_TRUE(parse_theme_list(themes, out.data(), out.size(), count, skipped, error));
    EXPECT_EQ(count, 1u);
}

}  // namespace

int main() {
    test_reads_an_app();
    test_list_level_errors();
    test_unknown_fields_are_skipped();
    test_app_rules();
    test_data_folders_and_install_rules();
    test_other_formats_are_listed();
    test_duplicates_and_room();
    test_install_paths();
    test_hostile_lists();
    test_checks_on_their_own();
    test_theme_list_still_reads();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "applist tests passed" << std::endl;
    return 0;
}
