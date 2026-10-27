// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The player's own menu music (riftwii/musiclist.hpp): which files are
// songs, what settings.txt accepts, playlists as PC players write them,
// a folder's songs and the shuffle.
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "riftwii/musiclist.hpp"
#include "riftwii/settingsfile.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) EXPECT_TRUE((a) == (b))

using Songs = std::vector<std::string>;
using riftwii::MusicFormat;

static void TestFormats() {
    EXPECT_TRUE(riftwii::music_format("song.ogg") == MusicFormat::Ogg);
    EXPECT_TRUE(riftwii::music_format("SONG.OGG") == MusicFormat::Ogg);
    EXPECT_TRUE(riftwii::music_format("usb:/a/b.Mp3") == MusicFormat::Mp3);
    EXPECT_TRUE(riftwii::music_format("song.wav") == MusicFormat::None);
    EXPECT_TRUE(riftwii::music_format("ogg") == MusicFormat::None);
    EXPECT_TRUE(riftwii::music_format("") == MusicFormat::None);
    EXPECT_TRUE(riftwii::music_is_playlist("car.m3u"));
    EXPECT_TRUE(riftwii::music_is_playlist("car.M3U8"));
    EXPECT_TRUE(!riftwii::music_is_playlist("car.mp3"));
}

static void TestChoices() {
    EXPECT_TRUE(riftwii::music_choice_valid(""));
    EXPECT_TRUE(riftwii::music_choice_valid("sd:/music/song.ogg"));
    EXPECT_TRUE(riftwii::music_choice_valid("usb:/song.mp3"));
    EXPECT_TRUE(riftwii::music_choice_valid("usb:/lists/car.m3u"));
    EXPECT_TRUE(riftwii::music_choice_valid("sd:/music/"));
    EXPECT_TRUE(riftwii::music_choice_valid("usb:/"));
    EXPECT_TRUE(!riftwii::music_choice_valid("sd:/music"));          // a folder ends in '/'
    EXPECT_TRUE(!riftwii::music_choice_valid("sd:/notes.txt"));
    EXPECT_TRUE(!riftwii::music_choice_valid("nand:/song.ogg"));
    EXPECT_TRUE(!riftwii::music_choice_valid("song.ogg"));
    EXPECT_TRUE(!riftwii::music_choice_valid("sd:/music/../x.ogg"));
    EXPECT_TRUE(riftwii::music_choice_valid("sd:/music/..x.ogg"));   // a name, not a climb
    EXPECT_TRUE(riftwii::music_choice_is_folder("sd:/music/"));
    EXPECT_TRUE(!riftwii::music_choice_is_folder("sd:/music/a.ogg"));
    EXPECT_EQ(riftwii::music_choice_name("sd:/music/a.ogg"), "a.ogg");
    EXPECT_EQ(riftwii::music_choice_name("sd:/music/"), "music");
    EXPECT_EQ(riftwii::music_choice_name("usb:/"), "usb:");
}

static void TestPlaylists() {
    // A Windows player's list: CRLF, a header, a drive letter, back slashes.
    const std::string windows =
        "#EXTM3U\r\n#EXTINF:123,Artist - One\r\nE:\\Music\\One.mp3\r\n\r\n  Two.ogg  \r\nsub\\Three.MP3\r\n"
        "cover.jpg\r\n\\Top.ogg\r\n";
    EXPECT_EQ(riftwii::music_playlist_songs(windows, "usb:/lists/car.m3u"),
              (Songs{"usb:/Music/One.mp3", "usb:/lists/Two.ogg", "usb:/lists/sub/Three.MP3", "usb:/Top.ogg"}));
    // Unix ends, a byte order mark, a lone '\r', a climb that is dropped.
    const std::string unix_text = "\xEF\xBB\xBF/music/a.ogg\nb.mp3\rc/../../d.mp3\n";
    EXPECT_EQ(riftwii::music_playlist_songs(unix_text, "sd:/l.m3u8"), (Songs{"sd:/music/a.ogg", "sd:/b.mp3"}));
    // At most so many.
    EXPECT_EQ(riftwii::music_playlist_songs("a.ogg\nb.ogg\nc.ogg\n", "sd:/l.m3u", 2), (Songs{"sd:/a.ogg", "sd:/b.ogg"}));
    EXPECT_TRUE(riftwii::music_playlist_songs("", "sd:/l.m3u").empty());
}

static void TestFolders() {
    // A pretend card: folder -> its entries.
    std::map<std::string, std::vector<riftwii::MusicFolderEntry>> card;
    card["sd:/music"] = {{"b.ogg", false}, {"a.mp3", false}, {"._a.mp3", false}, {"notes.txt", false},
                         {"Album", true}, {".hidden", true}};
    card["sd:/music/Album"] = {{"x.OGG", false}, {"Deeper", true}};
    card["sd:/music/Album/Deeper"] = {{"y.mp3", false}};
    card["sd:/music/.hidden"] = {{"z.mp3", false}};
    const riftwii::MusicFolderLister lister = [&](const std::string& folder, std::vector<riftwii::MusicFolderEntry>& out) {
        auto it = card.find(folder);
        if (it == card.end()) return false;
        out = it->second;
        return true;
    };
    EXPECT_EQ(riftwii::music_folder_songs("sd:/music/", lister),
              (Songs{"sd:/music/Album/Deeper/y.mp3", "sd:/music/Album/x.OGG", "sd:/music/a.mp3", "sd:/music/b.ogg"}));
    // One folder down only.
    EXPECT_EQ(riftwii::music_folder_songs("sd:/music/", lister, 1),
              (Songs{"sd:/music/Album/x.OGG", "sd:/music/a.mp3", "sd:/music/b.ogg"}));
    // That folder alone.
    EXPECT_EQ(riftwii::music_folder_songs("sd:/music/", lister, 0), (Songs{"sd:/music/a.mp3", "sd:/music/b.ogg"}));
    EXPECT_EQ(riftwii::music_folder_songs("sd:/music/", lister, 3, 1).size(), 1u);
    EXPECT_TRUE(riftwii::music_folder_songs("sd:/none/", lister).empty());
}

static void TestShuffle() {
    const Songs songs = {"a", "b", "c", "d", "e", "f", "g", "h"};
    Songs once = songs;
    riftwii::music_shuffle(once, 1234);
    Songs again = songs;
    riftwii::music_shuffle(again, 1234);
    EXPECT_EQ(once, again);  // a seed gives one order
    Songs sorted = once;
    std::sort(sorted.begin(), sorted.end());
    EXPECT_EQ(sorted, songs);  // nothing lost or doubled
    // Different seeds give different orders, now and then at least.
    std::set<Songs> orders;
    for (std::uint32_t seed = 1; seed <= 20; ++seed) {
        Songs s = songs;
        riftwii::music_shuffle(s, seed);
        orders.insert(s);
    }
    EXPECT_TRUE(orders.size() > 10);
    // A new round never starts on the song the last one ended on.
    for (std::uint32_t seed = 1; seed <= 200; ++seed) {
        Songs s = songs;
        riftwii::music_shuffle(s, seed, "c");
        EXPECT_TRUE(s.front() != "c");
    }
    Songs one = {"only"};
    riftwii::music_shuffle(one, 5, "only");
    EXPECT_EQ(one, Songs{"only"});
    Songs none;
    riftwii::music_shuffle(none, 5, "x");
    EXPECT_TRUE(none.empty());
}

static void TestSettingsFile() {
    riftwii::LoaderSettings s;
    EXPECT_EQ(s.music_file, "");
    s.parse("music_file = usb:/Music/\n");
    EXPECT_EQ(s.music_file, "usb:/Music/");
    EXPECT_TRUE(s.serialize().find("music_file = usb:/Music/\n") != std::string::npos);
    // Something it cannot play is left out, and the old pick kept.
    s.parse("music_file = sd:/notes.txt\n");
    EXPECT_EQ(s.music_file, "usb:/Music/");
    s.parse("music_file = \n");
    EXPECT_EQ(s.music_file, "");
    riftwii::LoaderSettings spaced;
    spaced.parse("music_file = sd:/My Music/Best Of.mp3\n");
    EXPECT_EQ(spaced.music_file, "sd:/My Music/Best Of.mp3");
}

int main() {
    TestFormats();
    TestChoices();
    TestPlaylists();
    TestFolders();
    TestShuffle();
    TestSettingsFile();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "musiclist: all passed" << std::endl;
    return 0;
}
