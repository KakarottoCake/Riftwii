// SPDX-License-Identifier: GPL-3.0-or-later
// The launch extras: HTTP for the downloads, cheat files and their GCT,
// the video mode patcher, and the settings file.
#include "riftwii/cheats.hpp"
#include "riftwii/coverart.hpp"
#include "riftwii/gamelang.hpp"
#include "riftwii/http.hpp"
#include "riftwii/langfile.hpp"
#include "riftwii/launch.hpp"
#include "riftwii/playhistory.hpp"
#include "riftwii/settingsfile.hpp"
#include "riftwii/update.hpp"
#include "riftwii/wfcpatch.hpp"
#include "riftwii/videopatch.hpp"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

namespace {

std::vector<std::uint8_t> bytes(const std::string& s) { return std::vector<std::uint8_t>(s.begin(), s.end()); }

void TestHttp() {
    HttpUrl url;
    std::string error;
    EXPECT_TRUE(parse_http_url("http://codes.rc24.xyz/txt.php?txt=SB4E01", url, error));
    EXPECT_EQ(url.host, "codes.rc24.xyz");
    EXPECT_EQ(url.port, 80);
    EXPECT_EQ(url.path, "/txt.php?txt=SB4E01");
    EXPECT_TRUE(parse_http_url("http://192.168.1.2:8080", url, error));
    EXPECT_EQ(url.port, 8080);
    EXPECT_EQ(url.path, "/");
    EXPECT_TRUE(http_get_request(url).find("Host: 192.168.1.2:8080\r\n") != std::string::npos);
    EXPECT_TRUE(parse_http_url("https://api.github.com/repos/a/b/releases?per_page=1", url, error));
    EXPECT_TRUE(url.tls);
    EXPECT_EQ(url.port, 443);
    EXPECT_EQ(url.host, "api.github.com");
    EXPECT_TRUE(http_get_request(url).find("Host: api.github.com\r\n") != std::string::npos);
    EXPECT_FALSE(parse_http_url("ftp://www.gametdb.com/", url, error));

    // The update check.
    std::string tag;
    EXPECT_TRUE(release_tag_from_json("[{\"url\":\"x\",\"tag_name\" : \"v2.0.1-beta\",\"tag_name\":\"old\"}]", tag));
    EXPECT_EQ(tag, "v2.0.1-beta");
    EXPECT_FALSE(release_tag_from_json("[]", tag));
    EXPECT_FALSE(release_tag_from_json("{\"tag_name\":3}", tag));
    EXPECT_EQ(compare_versions("v1.0.9-beta", "2.0.0-beta"), -1);
    EXPECT_EQ(compare_versions("2.0.0", "2.0.0-beta"), 1);
    EXPECT_EQ(compare_versions("v2.0.0-beta", "2.0.0-beta"), 0);
    EXPECT_EQ(compare_versions("2.0.0-rc1", "2.0.0-beta"), 1);
    EXPECT_EQ(compare_versions("2.0", "2.0.0"), 0);
    EXPECT_EQ(compare_versions("2.0.10", "2.0.9"), 1);
    EXPECT_EQ(compare_versions("1.0.5 Beta", "1.0.5-beta"), 0);

    const std::string plain = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 5\r\n\r\nhello";
    EXPECT_FALSE(http_response_complete(bytes(plain.substr(0, plain.size() - 1))));
    EXPECT_TRUE(http_response_complete(bytes(plain)));
    HttpResponse r;
    EXPECT_TRUE(parse_http_response(bytes(plain), r, error));
    EXPECT_EQ(r.status, 200);
    EXPECT_EQ(std::string(r.body.begin(), r.body.end()), "hello");
    EXPECT_EQ(r.headers["content-type"], "text/plain");

    const std::string chunked =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nWiki\r\n6\r\npedia \r\nE\r\nin \r\n\r\nchunks.\r\n0\r\n\r\n";
    EXPECT_FALSE(http_response_complete(bytes(chunked.substr(0, 40))));
    EXPECT_TRUE(http_response_complete(bytes(chunked)));
    EXPECT_TRUE(parse_http_response(bytes(chunked), r, error));
    EXPECT_EQ(std::string(r.body.begin(), r.body.end()), "Wikipedia in \r\n\r\nchunks.");
    EXPECT_FALSE(parse_http_response(bytes(chunked.substr(0, 60)), r, error));

    const std::string moved = "HTTP/1.1 301 Moved\r\nLocation: http://example.org/x\r\nContent-Length: 0\r\n\r\n";
    EXPECT_TRUE(parse_http_response(bytes(moved), r, error));
    EXPECT_EQ(r.status, 301);
    EXPECT_EQ(r.headers["location"], "http://example.org/x");
    // No length: the body runs to the connection's close.
    EXPECT_TRUE(parse_http_response(bytes("HTTP/1.0 200 OK\r\n\r\nall of it"), r, error));
    EXPECT_EQ(r.body.size(), 9u);
    EXPECT_EQ(url_encode("Mario Kart/Wii"), "Mario%20Kart%2FWii");
}

void TestCheats() {
    const std::string text =
        "\xEF\xBB\xBFSB4E01\r\n"
        "Super Mario Galaxy 2\r\n"
        "\r\n"
        "infinite health [wiiztec]\r\n"
        "043CA24C 60000000\r\n"
        "\r\n"
        "infinite starbits [wiiztec]\r\n"
        "04A75CF8 000003E6\r\n"
        "C24C9050 00000002\r\n"
        "386003E6 907F004C\r\n"
        "60000000 00000000\r\n"
        "Keeps star bits at 998.\r\n"
        "\r\n"
        "Moon jump [someone]\r\n"
        "28XXXXXX YYYY0000\r\n"
        "\r\n"
        "infinite health [wiiztec]\r\n"
        "043CA24C 60000000\r\n";
    CheatFile file;
    std::string error;
    EXPECT_TRUE(parse_cheat_text(text, file, error));
    EXPECT_EQ(file.game_id, "SB4E01");
    EXPECT_EQ(file.title, "Super Mario Galaxy 2");
    EXPECT_EQ(file.cheats.size(), 4u);
    if (file.cheats.size() != 4) return;
    EXPECT_EQ(file.cheats[0].name, "infinite health [wiiztec]");
    EXPECT_EQ(file.cheats[0].words.size(), 2u);
    EXPECT_EQ(file.cheats[1].words.size(), 8u);
    EXPECT_EQ(file.cheats[1].notes.size(), 1u);
    EXPECT_TRUE(file.cheats[2].needs_values);
    EXPECT_EQ(file.cheats[3].name, "infinite health [wiiztec] (2)");

    std::size_t count = 0;
    const std::vector<std::uint8_t> gct =
        build_gct(file, {"infinite starbits [wiiztec]", "Moon jump [someone]", "missing"}, count);
    EXPECT_EQ(count, 1u);
    EXPECT_EQ(gct.size(), 8u + 32u + 8u);
    EXPECT_EQ(gct[0], 0x00);
    EXPECT_EQ(gct[1], 0xD0);
    EXPECT_EQ(gct[8], 0x04);
    EXPECT_EQ(gct[gct.size() - 8], 0xF0);

    EXPECT_FALSE(parse_cheat_text("SB4E01\nSuper Mario Galaxy 2\n\n", file, error));
    // No header: the file is only cheats.
    EXPECT_TRUE(parse_cheat_text("Cheat\n04000000 00000001\n", file, error));
    EXPECT_EQ(file.cheats.size(), 1u);
}

// A render mode table as the SDK lays it out (GXRModeObj, 60 bytes).
void put_mode(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t tv, unsigned fb, unsigned efb, unsigned xfb,
              unsigned x, unsigned y, unsigned w, unsigned h, const std::uint8_t filter[7]) {
    auto p16 = [&](std::size_t o, unsigned v) {
        b[at + o] = static_cast<std::uint8_t>(v >> 8);
        b[at + o + 1] = static_cast<std::uint8_t>(v);
    };
    for (std::size_t i = 0; i < 60; ++i) b[at + i] = 0;
    b[at + 3] = static_cast<std::uint8_t>(tv);
    p16(4, fb);
    p16(6, efb);
    p16(8, xfb);
    p16(10, x);
    p16(12, y);
    p16(14, w);
    p16(16, h);
    b[at + 23] = 1;  // xfbMode double field
    b[at + 24] = 1;
    for (int i = 0; i < 24; ++i) b[at + 26 + i] = 6;
    for (int i = 0; i < 7; ++i) b[at + 50 + i] = filter[i];
}

unsigned get16(const std::vector<std::uint8_t>& b, std::size_t at) { return (unsigned(b[at]) << 8) | b[at + 1]; }

void TestVideo() {
    const std::uint8_t deflicker[7] = {7, 7, 12, 12, 12, 7, 7};
    const std::uint8_t sharp[7] = {0, 0, 21, 22, 21, 0, 0};
    std::vector<std::uint8_t> data(1024, 0x11);
    put_mode(data, 100, 0, 640, 456, 456, 40, 12, 640, 456, deflicker);  // NTSC 480i with borders
    put_mode(data, 200, 2, 640, 480, 480, 8, 0, 704, 480, sharp);        // NTSC 480p, 704 wide
    put_mode(data, 400, 4, 640, 528, 574, 40, 0, 640, 574, deflicker);   // PAL 576i
    // Near misses: filter taps that do not sum to 64, a width past the line.
    const std::uint8_t bad[7] = {1, 1, 1, 1, 1, 1, 1};
    put_mode(data, 600, 0, 640, 480, 480, 40, 0, 640, 480, bad);
    put_mode(data, 700, 0, 640, 480, 480, 100, 0, 640, 480, deflicker);

    std::vector<std::uint8_t> copy = data;
    VideoPatchReport report;
    patch_video_modes(copy.data(), copy.size(), VideoSettings{}, report);
    EXPECT_EQ(report.modes, 3u);
    EXPECT_EQ(report.patched, 0u);
    EXPECT_TRUE(report.side_borders);
    EXPECT_TRUE(report.top_borders);
    EXPECT_TRUE(copy == data);

    VideoSettings s;
    s.width = VideoWidth::W704;
    s.deflicker = Deflicker::Off;
    copy = data;
    report = VideoPatchReport{};
    patch_video_modes(copy.data(), copy.size(), s, report);
    EXPECT_EQ(report.patched, 2u);  // the 480p table is already 704 wide and sharp
    EXPECT_EQ(get16(copy, 114), 704u);
    EXPECT_EQ(get16(copy, 110), 8u);
    EXPECT_EQ(copy[100 + 50 + 3], 22);
    EXPECT_EQ(copy[100 + 50 + 0], 0);
    EXPECT_EQ(get16(copy, 614), 640u);  // not a table: untouched

    s = VideoSettings{};
    s.remove_borders = true;
    copy = data;
    report = VideoPatchReport{};
    patch_video_modes(copy.data(), copy.size(), s, report);
    EXPECT_EQ(get16(copy, 114), 720u);
    EXPECT_EQ(get16(copy, 110), 0u);
    EXPECT_EQ(get16(copy, 116), 480u);  // full height
    EXPECT_EQ(get16(copy, 108), 480u);  // xfbHeight follows
    EXPECT_EQ(get16(copy, 112), 0u);
    EXPECT_EQ(get16(copy, 416), 574u);
    EXPECT_EQ(get16(copy, 414), 720u);

    s = VideoSettings{};
    s.width = VideoWidth::Framebuffer;
    copy = data;
    report = VideoPatchReport{};
    patch_video_modes(copy.data(), copy.size(), s, report);
    EXPECT_EQ(get16(copy, 214), 640u);
    EXPECT_EQ(get16(copy, 210), 40u);
    EXPECT_EQ(report.patched, 1u);
}

void TestVideoModes() {
    const std::uint8_t deflicker[7] = {7, 7, 12, 12, 12, 7, 7};
    const std::uint8_t sharp[7] = {0, 0, 21, 22, 21, 0, 0};
    std::vector<std::uint8_t> data(1024, 0x11);
    put_mode(data, 0, 0, 640, 480, 480, 40, 0, 640, 480, deflicker);    // TVNtsc480IntDf
    put_mode(data, 100, 0, 640, 242, 480, 40, 0, 640, 480, deflicker);  // TVNtsc480IntAa
    put_mode(data, 200, 1, 640, 240, 240, 40, 0, 640, 480, deflicker);  // TVNtsc240Ds
    put_mode(data, 300, 0, 640, 456, 456, 40, 12, 640, 456, deflicker); // custom heights
    put_mode(data, 400, 2, 640, 480, 480, 40, 0, 640, 480, sharp);      // TVNtsc480Prog
    put_mode(data, 500, 4, 640, 528, 528, 40, 23, 640, 528, deflicker); // TVPal528IntDf
    put_mode(data, 600, 4, 640, 480, 576, 40, 0, 640, 576, deflicker);  // TVPal576IntDfScale
    for (std::size_t at : {0, 100, 200, 300, 500, 600}) data[at + 24] = 0;  // not field rendered
    data[400 + 23] = 0;  // single field
    data[400 + 24] = 0;
    data[200 + 23] = 0;
    data[100 + 25] = 1;  // aa

    VideoMode m = VideoMode::Game;
    EXPECT_TRUE(parse_video_mode("pal60", m));
    EXPECT_TRUE(m == VideoMode::Pal60);
    EXPECT_TRUE(parse_video_mode("480p", m));
    EXPECT_TRUE(m == VideoMode::Progressive);
    EXPECT_FALSE(parse_video_mode("PAL", m));
    EXPECT_EQ(std::string(to_string(VideoMode::System)), "system");

    // A mode alone is nothing to do until it is resolved to a target.
    VideoSettings s;
    s.mode = VideoMode::Pal50;
    EXPECT_FALSE(s.any());

    // PAL 50 Hz: 480-line tables take the SDK's 576-line heights.
    s.target.format = kViPal;
    EXPECT_TRUE(s.any());
    std::vector<std::uint8_t> copy = data;
    VideoPatchReport report;
    patch_video_modes(copy.data(), copy.size(), s, report);
    EXPECT_EQ(report.modes, 7u);
    EXPECT_EQ(copy[3], 4);
    EXPECT_EQ(get16(copy, 6), 528u);
    EXPECT_EQ(get16(copy, 12), 23u);
    EXPECT_EQ(get16(copy, 16), 528u);
    EXPECT_EQ(get16(copy, 106), 264u);
    EXPECT_EQ(get16(copy, 108), 524u);
    EXPECT_EQ(copy[203], 5);  // PAL double strike
    EXPECT_EQ(get16(copy, 212), 11u);
    EXPECT_EQ(copy[303], 0);  // unknown heights: left as they were
    EXPECT_EQ(get16(copy, 306), 456u);
    EXPECT_EQ(copy[403], 4);  // 576p does not exist: interlaced
    EXPECT_EQ(copy[423], 1);
    EXPECT_EQ(copy[503], 4);  // already PAL
    EXPECT_EQ(report.converted, 4u);

    // PAL 60 Hz: 576-line tables go back to 480, EuRGB60.
    s.target.format = kViEurgb60;
    copy = data;
    report = VideoPatchReport{};
    patch_video_modes(copy.data(), copy.size(), s, report);
    EXPECT_EQ(copy[3], 20);
    EXPECT_EQ(copy[503], 20);
    EXPECT_EQ(get16(copy, 506), 480u);
    EXPECT_EQ(get16(copy, 512), 0u);
    EXPECT_EQ(get16(copy, 516), 480u);
    EXPECT_EQ(copy[603], 20);  // the scaled table too
    EXPECT_EQ(get16(copy, 608), 480u);
    EXPECT_EQ(copy[403], 20);  // 480p off
    EXPECT_EQ(copy[303], 20);  // same lines: just the format

    // 480p: full interlaced tables become progressive ones.
    s.target.format = kViNtsc;
    s.target.progressive = true;
    copy = data;
    report = VideoPatchReport{};
    patch_video_modes(copy.data(), copy.size(), s, report);
    EXPECT_EQ(copy[3], 2);
    EXPECT_EQ(copy[23], 0);
    EXPECT_EQ(copy[50], 0);
    EXPECT_EQ(copy[103], 2);
    EXPECT_EQ(copy[203], 1);  // double strike stays
    EXPECT_EQ(copy[503], 2);  // PAL: to 480 lines, then progressive
    EXPECT_EQ(get16(copy, 506), 480u);
    EXPECT_EQ(copy[403], 2);
}

void TestSettings() {
    LoaderSettings s;
    s.parse("# c\nlanguage = ja\nvideo_width=704\ndeflicker = bogus\nborders = remove\nonline = off\nfuture = 1\n");
    EXPECT_EQ(s.language, "ja");
    EXPECT_EQ(s.video_width, "704");
    EXPECT_EQ(s.deflicker, "game");
    EXPECT_EQ(s.borders, "remove");
    EXPECT_FALSE(s.online);
    LoaderSettings again;
    again.parse(s.serialize());
    EXPECT_EQ(again.language, "ja");
    EXPECT_EQ(again.other["future"], "1");
    EXPECT_EQ(again.gc_adapter, "auto");  // the default
    LoaderSettings adapter;
    adapter.parse("gc_adapter = on\n");
    EXPECT_EQ(adapter.gc_adapter, "on");
    adapter.parse("gc_adapter = sometimes\n");  // unknown values are ignored
    EXPECT_EQ(adapter.gc_adapter, "on");
    LoaderSettings adapterAgain;
    adapterAgain.parse(adapter.serialize());
    EXPECT_EQ(adapterAgain.gc_adapter, "on");
    EXPECT_EQ(adapterAgain.other.count("gc_adapter"), 0u);

    GameSettings g;
    VideoSettings v = effective_video(g, s);
    EXPECT_TRUE(v.width == VideoWidth::W704);
    EXPECT_TRUE(v.remove_borders);
    g.video_width = "game";
    g.borders = "keep";
    g.deflicker = "off";
    v = effective_video(g, s);
    EXPECT_TRUE(v.width == VideoWidth::Game);
    EXPECT_FALSE(v.remove_borders);
    EXPECT_TRUE(v.deflicker == Deflicker::Off);

    LaunchModel m;
    m.game.cheats = true;
    m.game.cheat_names = {"infinite health [wiiztec]", "b"};
    m.game.video_width = "720";
    m.game.deflicker = "off";
    m.game.borders = "remove";
    LaunchModel back;
    back.restore(m.save());
    EXPECT_TRUE(back.game.cheats);
    EXPECT_EQ(back.game.cheat_names.size(), 2u);
    EXPECT_EQ(back.game.video_width, "720");
    EXPECT_EQ(back.game.deflicker, "off");
    EXPECT_EQ(back.game.borders, "remove");

    // Video mode, game language and cIOS: global defaults, game choices.
    LoaderSettings global;
    global.parse("video_mode = pal60\ngame_language = de\ngame_cios = 252\n");
    EXPECT_EQ(global.video_mode, "pal60");
    global.parse("video_mode = secam\ngame_language = xx\ngame_cios = 247\n");  // ignored
    EXPECT_EQ(global.video_mode, "pal60");
    EXPECT_EQ(global.game_language, "de");
    EXPECT_EQ(global.game_cios, "252");
    LoaderSettings globalAgain;
    globalAgain.parse(global.serialize());
    EXPECT_EQ(globalAgain.game_cios, "252");
    EXPECT_EQ(globalAgain.other.size(), 0u);
    GameSettings pick;
    EXPECT_TRUE(effective_video(pick, global).mode == VideoMode::Pal60);
    EXPECT_EQ(effective_game_language(pick, global), 2);
    EXPECT_EQ(effective_game_cios(pick, global), 252);
    EXPECT_EQ(effective_game_language(pick, LoaderSettings{}), -1);
    EXPECT_EQ(effective_game_cios(pick, LoaderSettings{}), 0);
    pick.video_mode = "480p";
    pick.language = "console";
    pick.cios = "auto";
    EXPECT_TRUE(effective_video(pick, global).mode == VideoMode::Progressive);
    EXPECT_EQ(effective_game_language(pick, global), -1);
    EXPECT_EQ(effective_game_cios(pick, global), 0);
    pick.cios = "248";
    EXPECT_EQ(effective_game_cios(pick, global), 248);
    int slot = 0;
    EXPECT_FALSE(parse_cios_choice("25a", slot));
    EXPECT_FALSE(parse_cios_choice("253", slot));
    LaunchModel withPick;
    withPick.game = pick;
    LaunchModel pickBack;
    pickBack.restore(withPick.save());
    EXPECT_EQ(pickBack.game.video_mode, "480p");
    EXPECT_EQ(pickBack.game.language, "console");
    EXPECT_EQ(pickBack.game.cios, "248");

    LoaderSettings fav;
    fav.parse("favorites = SB4E01, RMCE01,,bad id,R8PE01\n");
    EXPECT_EQ(fav.favorites.size(), 3u);
    EXPECT_EQ(fav.favorites.count("RMCE01"), 1u);
    LoaderSettings favAgain;
    favAgain.parse(fav.serialize());
    EXPECT_TRUE(favAgain.favorites == fav.favorites);
    EXPECT_EQ(favAgain.other.count("favorites"), 0u);
    EXPECT_TRUE(LoaderSettings{}.serialize().find("favorites") == std::string::npos);
}

void TestWfc() {
    WfcServer server = WfcServer::Off;
    EXPECT_TRUE(parse_wfc_server("wiilink", server));
    EXPECT_TRUE(server == WfcServer::WiiLink);
    EXPECT_FALSE(parse_wfc_server("nintendo", server));
    EXPECT_EQ(wfc_domain(WfcServer::Wiimmfi, ""), "wiimmfi.de");
    EXPECT_EQ(wfc_domain(WfcServer::Custom, "wfc.example"), "wfc.example");
    EXPECT_TRUE(valid_wfc_domain("zwei.moe"));
    EXPECT_FALSE(valid_wfc_domain("far-too-long.example.org"));
    EXPECT_FALSE(valid_wfc_domain("no_dot"));
    EXPECT_FALSE(valid_wfc_domain("a b.cd"));

    const char text[] = "xx\0https://naswii.nintendowifi.net/ac\0gamespy.nintendowifi.net\0https://\0end";
    std::vector<std::uint8_t> b(text, text + sizeof(text));
    EXPECT_EQ(patch_https_to_http(b.data(), b.size()), 1u);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(b.data() + 3)), "http://naswii.nintendowifi.net/ac");
    EXPECT_EQ(b[3 + 34], 0);
    EXPECT_EQ(patch_wfc_domain(b.data(), b.size(), "wiimmfi.de"), 2u);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(b.data() + 3)), "http://naswii.wiimmfi.de/ac");
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(b.data() + 38)), "gamespy.wiimmfi.de");
    EXPECT_EQ(b[38 + 24], 0);                     // the old tail is zeroed
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(b.data() + 63)), "https://");  // empty: left alone
    EXPECT_EQ(patch_wfc_domain(b.data(), b.size(), "much-too-long.domain.example"), 0u);

    const char ua[] = "..User-Agent\0\0RVL SDK/1.0\0";
    std::vector<std::uint8_t> u(ua, ua + sizeof(ua));
    EXPECT_EQ(patch_wiimmfi_generic(u.data(), u.size()), 0);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(u.data() + 14)), "G-3-0");
    // The GT2 bug twice is refused; once without its code, reported.
    std::string two = std::string("<GT2> RECV-0x%02x <- [--------:-----] [pid=%u]") + '\0';
    two += two;
    std::vector<std::uint8_t> t(two.begin(), two.end());
    EXPECT_EQ(patch_wiimmfi_generic(t.data(), t.size()), 1);
    t.resize(t.size() / 2);
    EXPECT_EQ(patch_wiimmfi_generic(t.data(), t.size()), 2);

    LoaderSettings global;
    global.parse("wfc_server = custom\nwfc_domain = bad domain\n");
    GameSettings game;
    EXPECT_TRUE(effective_wfc_server(game, global) == WfcServer::Off);  // no valid domain
    global.parse("wfc_domain = wfc.example\n");
    EXPECT_TRUE(effective_wfc_server(game, global) == WfcServer::Custom);
    game.server = "wiimmfi";
    EXPECT_TRUE(effective_wfc_server(game, global) == WfcServer::Wiimmfi);
    LoaderSettings again;
    again.parse(global.serialize());
    EXPECT_EQ(again.wfc_domain, "wfc.example");
    EXPECT_EQ(again.wfc_server, "custom");
    LaunchModel m;
    m.game.server = "wiilink";
    LaunchModel back;
    back.restore(m.save());
    EXPECT_EQ(back.game.server, "wiilink");
}

void TestCoverArt() {
    const std::vector<std::string> us = cover_regions("RMCE01", "en");
    EXPECT_EQ(us.size(), 3u);
    EXPECT_EQ(us[0], "US");
    EXPECT_EQ(us[1], "EN");
    EXPECT_EQ(cover_regions("RMCP01", "es")[0], "ES");
    EXPECT_EQ(cover_regions("RMCP01", "ja")[0], "EN");
    EXPECT_EQ(cover_regions("RMCJ01", "en")[0], "JA");
    EXPECT_EQ(cover_regions("RMCJ01", "en").size(), 3u);  // JA once
    EXPECT_EQ(cover_url("US", "RMCE01"), "http://art.gametdb.com/wii/cover/US/RMCE01.png");

    // 2x2 averages to 1x1; 1 pixel spreads to 3x3.
    const std::uint8_t four[16] = {0, 0, 0, 255, 100, 100, 100, 255, 200, 200, 200, 255, 100, 100, 100, 255};
    const std::vector<std::uint8_t> one = scale_rgba(four, 2, 2, 1, 1);
    EXPECT_EQ(one[0], 100);
    EXPECT_EQ(one[3], 255);
    const std::uint8_t red[4] = {255, 0, 0, 255};
    const std::vector<std::uint8_t> nine = scale_rgba(red, 1, 1, 3, 3);
    EXPECT_EQ(nine[8 * 4], 255);
    EXPECT_EQ(nine[8 * 4 + 1], 0);

    // A white 160x224 cover: opaque white inside, clear at the corners.
    std::vector<std::uint8_t> white(160 * 224 * 4, 255);
    const std::vector<std::uint8_t> file = make_cover_file(white.data(), 160, 224);
    EXPECT_EQ(file.size(), kCoverFileSize);
    EXPECT_TRUE(cover_header_valid(file.data()));
    // Tile 0, pixel (0,0): the corner, fully clear. Pixel (40,56) in
    // tile (10,14), row 0, column 0: opaque white 0xFFFF.
    EXPECT_EQ(file[8] & 0x80, 0);
    const std::size_t tile = (14 * (kCoverWidth / 4) + 10) * 32;
    EXPECT_EQ(file[8 + tile], 0xFF);
    EXPECT_EQ(file[8 + tile + 1], 0xFF);
    EXPECT_TRUE(make_cover_file(white.data(), 4, 4).empty());
    std::uint8_t bad[8] = {'R', 'W', 'C', '1', 0, 40, 0, 112};
    EXPECT_FALSE(cover_header_valid(bad));

    LoaderSettings s;
    EXPECT_EQ(s.home_tiles, "covers");
    s.parse("home_tiles = names\n");
    EXPECT_EQ(s.home_tiles, "names");
    s.parse("home_tiles = huge\n");
    EXPECT_EQ(s.home_tiles, "names");
}

void TestGameLanguage() {
    int code = 0;
    EXPECT_TRUE(parse_game_language("zh-hant", code));
    EXPECT_EQ(code, 8);
    EXPECT_TRUE(parse_game_language("console", code));
    EXPECT_EQ(code, -1);
    EXPECT_FALSE(parse_game_language("EN", code));
    EXPECT_EQ(std::string(game_language_name(9)), "ko");

    // SCGetLanguage's shape: the check, then the load a few words on.
    const std::uint32_t words[] = {0x60000000, 0x7C600775, 0x40820010, 0x38000000, 0x98010008,
                                   0x48000008, 0x60000000, 0x88610008, 0x4E800020, 0x88610008};
    std::vector<std::uint8_t> code_bytes;
    for (std::uint32_t w : words) {
        for (int shift = 24; shift >= 0; shift -= 8) code_bytes.push_back(static_cast<std::uint8_t>(w >> shift));
    }
    std::vector<std::uint8_t> copy = code_bytes;
    EXPECT_EQ(patch_game_language(copy.data(), copy.size(), -1), 0u);
    EXPECT_TRUE(copy == code_bytes);
    EXPECT_EQ(patch_game_language(copy.data(), copy.size(), 3), 1u);
    EXPECT_EQ(copy[28], 0x38);  // li r3,3
    EXPECT_EQ(copy[29], 0x60);
    EXPECT_EQ(copy[31], 3);
    EXPECT_EQ(copy[39], 0x08);  // only the first load after the check
    // No check, no patch.
    copy.assign(code_bytes.begin() + 28, code_bytes.end());
    EXPECT_EQ(patch_game_language(copy.data(), copy.size(), 1), 0u);
}

void TestLang() {
    riftwii::Translations t;
    const std::string po =
        "\xEF\xBB\xBF# Spanish\r\n"
        "msgid \"\"\n"
        "msgstr \"Content-Type: text/plain; charset=UTF-8\\n\"\n"
        "\n"
        "#: rift_menu.cpp\n"
        "msgid \"Back\"\n"
        "msgstr \"Atr\xC3\xA1s\"\n"
        "\n"
        "msgid \"On, {1} picked\"\n"
        "msgstr \"S\xC3\xAD, \"\n"
        "  \"{1} elegidos\"\n"
        "\n"
        "msgid \"Say \\\"hi\\\"\"\n"
        "msgstr \"\"\n"
        "\n"
        "msgctxt \"x\"\n"
        "msgid \"Ignored\"\n"
        "msgstr \"No\"\n"
        "msgid \"Two\\nlines\"\n"
        "msgstr \"Dos\\nl\xC3\xADneas\"\n";
    EXPECT_EQ(riftwii::parse_po(po, t), 3u);
    EXPECT_EQ(t["Back"], "Atr\xC3\xA1s");
    EXPECT_EQ(t["On, {1} picked"], "S\xC3\xAD, {1} elegidos");
    EXPECT_EQ(t["Two\nlines"], "Dos\nl\xC3\xADneas");
    EXPECT_TRUE(t.count("") == 0);            // the header entry
    EXPECT_TRUE(t.count("Say \"hi\"") == 0);  // untranslated
    EXPECT_TRUE(t.count("Ignored") == 0);     // has a context

    EXPECT_EQ(riftwii::fill_placeholders("{2} of {1}", {"a", "b"}), "b of a");
    EXPECT_EQ(riftwii::fill_placeholders("{3} {} {x", {"a"}), "{3} {} {x");

    const std::vector<char32_t> cps = riftwii::decode_utf8("A\xC3\xB1\xE6\x97\xA5\xF0\x9F\x98\x80\xE9z\xC0\xAF");
    const std::vector<char32_t> want = {U'A', 0xF1, 0x65E5, 0x1F600, 0xE9, U'z', 0xC0, 0xAF};
    EXPECT_TRUE(cps == want);
}

void TestHistory() {
    PlayHistory h;
    h.parse("# comment\r\nSB4E01\t3\t1790000000\nRMCE01\t1\t1790000500\nbad line\nrmce01\t1\t5\nSMNE01\tx\t1\n");
    EXPECT_EQ(h.size(), 2u);
    EXPECT_TRUE(h.find("SB4E01") && h.find("SB4E01")->count == 3);
    EXPECT_TRUE(h.find("rmce01") == nullptr);  // not an ID
    std::vector<std::string> recent = h.recent();
    EXPECT_TRUE(recent.size() == 2 && recent[0] == "RMCE01");
    h.record("SB4E01", 1790001000);
    h.record("R2SE18", 1790000800);
    h.record("", 1790002000);  // ignored
    recent = h.recent(2);
    EXPECT_TRUE(recent.size() == 2 && recent[0] == "SB4E01" && recent[1] == "R2SE18");
    EXPECT_EQ(h.find("SB4E01")->count, 4u);
    PlayHistory back;
    back.parse(h.serialize());
    EXPECT_EQ(back.size(), 3u);
    EXPECT_EQ(back.find("R2SE18")->last, 1790000800);
}

}  // namespace

int main() {
    TestHttp();
    TestCheats();
    TestVideo();
    TestVideoModes();
    TestSettings();
    TestGameLanguage();
    TestWfc();
    TestCoverArt();
    TestLang();
    TestHistory();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "extras: all tests passed" << std::endl;
    return 0;
}
