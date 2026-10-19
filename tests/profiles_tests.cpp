// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <iostream>
#include <string>

#include "riftwii/playhistory.hpp"
#include "riftwii/profiles.hpp"
#include "riftwii/settingsfile.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

static void test_keys() {
    EXPECT_TRUE(plausible_game_id("RMCE01"));
    EXPECT_TRUE(plausible_game_id("RMCE"));
    EXPECT_TRUE(!plausible_game_id("rmce01"));
    EXPECT_TRUE(!plausible_game_id("RMC"));
    EXPECT_TRUE(!plausible_game_id("RMCE012"));

    EXPECT_TRUE(valid_profile_slug("retro-rewind"));
    EXPECT_TRUE(valid_profile_slug("mkw2"));
    EXPECT_TRUE(!valid_profile_slug(""));
    EXPECT_TRUE(!valid_profile_slug("-retro"));
    EXPECT_TRUE(!valid_profile_slug("retro-"));
    EXPECT_TRUE(!valid_profile_slug("retro--rewind"));
    EXPECT_TRUE(!valid_profile_slug("Retro"));
    EXPECT_TRUE(!valid_profile_slug("../x"));
    EXPECT_TRUE(!valid_profile_slug("a/b"));
    EXPECT_TRUE(!valid_profile_slug(std::string(kMaxProfileSlug + 1, 'a')));

    EXPECT_TRUE(valid_tile_key("RMCE01"));
    EXPECT_TRUE(valid_tile_key("RMCE01@retro-rewind"));
    EXPECT_TRUE(!valid_tile_key("RMCE01@"));
    EXPECT_TRUE(!valid_tile_key("@retro"));
    EXPECT_TRUE(!valid_tile_key("RMCE01@retro@x"));
    EXPECT_TRUE(!valid_tile_key("RMCE01@../../settings"));
    EXPECT_EQ(tile_game_id("RMCE01@retro-rewind"), std::string("RMCE01"));
    EXPECT_EQ(tile_game_id("RMCE01"), std::string("RMCE01"));
}

static void test_names_and_slugs() {
    EXPECT_EQ(clean_profile_name("  Retro   Rewind  "), std::string("Retro Rewind"));
    EXPECT_EQ(clean_profile_name("MKW\tDe\nluxe"), std::string("MKWDeluxe"));
    EXPECT_EQ(clean_profile_name("   "), std::string(""));
    // Cut to the limit on a whole character: "é" is two bytes.
    const std::string long_name = std::string(kMaxProfileName - 1, 'a') + "\xC3\xA9";
    const std::string cut = clean_profile_name(long_name);
    EXPECT_EQ(cut.size(), kMaxProfileName - 1);
    EXPECT_EQ(clean_profile_name(std::string(40, 'b')).size(), kMaxProfileName);

    EXPECT_EQ(profile_slug("Retro Rewind"), std::string("retro-rewind"));
    EXPECT_EQ(profile_slug("MKW: Deluxe (v2)!"), std::string("mkw-deluxe-v2"));
    EXPECT_EQ(profile_slug("../../settings"), std::string("settings"));
    EXPECT_EQ(profile_slug("\xE3\x83\x9E\xE3\x83\xAA\xE3\x82\xAA"), std::string("mods"));  // nothing ASCII left
    EXPECT_EQ(profile_slug("---"), std::string("mods"));
    const std::string slug = profile_slug("The Quick Brown Fox Jumps Over The Lazy Dog");
    EXPECT_TRUE(slug.size() <= kMaxProfileSlug);
    EXPECT_TRUE(valid_profile_slug(slug));
    EXPECT_TRUE(valid_profile_slug(profile_slug("a b c d e f g h i j k l m n")));

    EXPECT_TRUE(valid_profile_art("sd:/riivolution/RetroRewind.png"));
    EXPECT_TRUE(valid_profile_art("usb:/riivolution/Deluxe.PNG"));
    EXPECT_TRUE(!valid_profile_art("sd:/riivolution/../x.png"));
    EXPECT_TRUE(!valid_profile_art("http://example.com/x.png"));
    EXPECT_TRUE(!valid_profile_art("sd:/riivolution/x.jpg"));
    EXPECT_TRUE(!valid_profile_art(""));
    EXPECT_TRUE(!valid_profile_art("sd:/" + std::string(kMaxProfileArtPath, 'a') + ".png"));
}

static void test_list() {
    ProfileList list;
    std::string key;
    EXPECT_TRUE(list.add("RMCE01", "Retro Rewind", "sd:/riivolution/RR.png", key) == ProfileList::AddResult::Added);
    EXPECT_EQ(key, std::string("RMCE01@retro-rewind"));
    // The same name again: a slug of its own.
    EXPECT_TRUE(list.add("RMCE01", "Retro Rewind", "", key) == ProfileList::AddResult::Added);
    EXPECT_EQ(key, std::string("RMCE01@retro-rewind-2"));
    EXPECT_TRUE(list.add("RMCE01", "   ", "", key) == ProfileList::AddResult::NoName);
    EXPECT_TRUE(key.empty());
    EXPECT_TRUE(list.add("bad", "Name", "", key) == ProfileList::AddResult::BadGame);
    EXPECT_TRUE(list.add("SMNE01", "Newer", "sd:/../evil.png", key) == ProfileList::AddResult::Added);
    EXPECT_TRUE(list.find(key) != nullptr && list.find(key)->art.empty());  // a path that climbs is not kept
    EXPECT_EQ(list.for_game("RMCE01").size(), std::size_t(2));

    // A long name's slug keeps room for its number.
    const std::string long_name(30, 'x');
    EXPECT_TRUE(list.add("RSBE01", long_name, "", key) == ProfileList::AddResult::Added);
    EXPECT_TRUE(list.add("RSBE01", long_name, "", key) == ProfileList::AddResult::Added);
    EXPECT_TRUE(valid_tile_key(key));
    EXPECT_EQ(key, "RSBE01@" + std::string(kMaxProfileSlug - 2, 'x') + "-2");

    // Renaming keeps the key (its files keep their names).
    EXPECT_TRUE(list.rename("RMCE01@retro-rewind-2", "MKW Deluxe"));
    EXPECT_EQ(list.find("RMCE01@retro-rewind-2")->name, std::string("MKW Deluxe"));
    EXPECT_TRUE(!list.rename("RMCE01@retro-rewind-2", " "));
    EXPECT_TRUE(!list.rename("RMCE01@nothing", "X"));

    // Round trip.
    ProfileList again;
    again.parse(list.serialize());
    EXPECT_EQ(again.all().size(), list.all().size());
    EXPECT_EQ(again.find("RMCE01@retro-rewind")->art, std::string("sd:/riivolution/RR.png"));
    EXPECT_EQ(again.find("RMCE01@retro-rewind-2")->name, std::string("MKW Deluxe"));

    EXPECT_TRUE(list.remove("RMCE01@retro-rewind"));
    EXPECT_TRUE(!list.remove("RMCE01@retro-rewind"));
    EXPECT_TRUE(list.find("RMCE01@retro-rewind") == nullptr);

    // Limits: per game, then the whole list.
    ProfileList full;
    for (std::size_t i = 0; i < kMaxProfilesPerGame; ++i)
        EXPECT_TRUE(full.add("RMCE01", "Tile", "", key) == ProfileList::AddResult::Added);
    EXPECT_TRUE(full.add("RMCE01", "Tile", "", key) == ProfileList::AddResult::GameFull);
    ProfileList many;
    for (std::size_t i = 0; i < kMaxProfiles; ++i) {
        const std::string game = "G" + std::to_string(100 + i / kMaxProfilesPerGame) + "E";
        EXPECT_TRUE(many.add(game, "Tile", "", key) == ProfileList::AddResult::Added);
    }
    EXPECT_TRUE(many.add("ZZZE01", "Tile", "", key) == ProfileList::AddResult::ListFull);
}

static void test_parse_untrusted() {
    ProfileList list;
    list.parse(
        "# comment\r\n"
        "RMCE01@retro-rewind\tRetro Rewind\tsd:/riivolution/RR.png\r\n"
        "RMCE01@retro-rewind\tTwice\n"            // the same key again
        "RMCE01@../../x\tClimbs\n"                // not a slug
        "RMCE01\tNo slug\n"                       // a game's own key is not a profile
        "SMNE01@newer\t   \n"                     // no name
        "SMNE01@newer2\tNewer\tsd:/a/../b.png\n"  // the picture climbs: dropped, the tile kept
        "no tab at all\n"
        "\n");
    EXPECT_EQ(list.all().size(), std::size_t(2));
    EXPECT_EQ(list.find("RMCE01@retro-rewind")->name, std::string("Retro Rewind"));
    EXPECT_TRUE(list.find("SMNE01@newer2") != nullptr && list.find("SMNE01@newer2")->art.empty());

    // More lines than the limits allow: the first ones only.
    std::string big;
    for (int i = 0; i < 20; ++i) big += "RMCE01@t" + std::to_string(i) + "\tTile\n";
    list.parse(big);
    EXPECT_EQ(list.all().size(), kMaxProfilesPerGame);
    big.clear();
    for (int i = 0; i < 200; ++i) big += "G" + std::to_string(1000 + i) + "@t\tTile\n";
    list.parse(big);
    EXPECT_EQ(list.all().size(), kMaxProfiles);
    // A huge file is read only so far.
    list.parse(std::string(kMaxProfilesFile, '#') + "\nRMCE01@late\tLate\n");
    EXPECT_TRUE(list.all().empty());
    // A line cut off at the limit is dropped, not kept shortened.
    const std::string first = "RMCE01@first\tFirst\n";
    std::string cut = first + std::string(kMaxProfilesFile - first.size() - 20, '#') + "\nRMCE01@cut\tA long name";
    cut += std::string(100, 'x') + "\n";
    list.parse(cut);
    EXPECT_EQ(list.all().size(), std::size_t(1));
    EXPECT_TRUE(list.find("RMCE01@first") != nullptr && list.find("RMCE01@cut") == nullptr);
}

// Home shows a game's profiles only for a game on a drive: not for the
// disc drive's tile or the Wii's channels.
static void test_tiles_for() {
    ProfileList list;
    std::string key;
    list.add("RMCE01", "Retro Rewind", "", key);
    list.add("WBME01", "Hand edited", "", key);  // a channel's ID, as a hand-edited file could have
    EXPECT_EQ(list.tiles_for("RMCE01", true).size(), std::size_t(1));
    EXPECT_TRUE(list.tiles_for("RMCE01", false).empty());
    EXPECT_TRUE(list.tiles_for("WBME01", false).empty());
}

// The choices a problem report shows: a profile's only when it is of the game launched.
static void test_launched_choices_key() {
    EXPECT_EQ(launched_choices_key("RMCE01", "RMCE01@retro-rewind\n"), std::string("RMCE01@retro-rewind"));
    EXPECT_EQ(launched_choices_key("RMCE01", "RMCE01@retro-rewind\r\n"), std::string("RMCE01@retro-rewind"));
    EXPECT_EQ(launched_choices_key("RMCE01", "RMCE01\n"), std::string("RMCE01"));
    EXPECT_EQ(launched_choices_key("SMNE01", "RMCE01@retro-rewind\n"), std::string("SMNE01"));  // another game since
    EXPECT_EQ(launched_choices_key("RMCE01", "RMCE01@../../x\n"), std::string("RMCE01"));
    EXPECT_EQ(launched_choices_key("RMCE01", ""), std::string("RMCE01"));
    EXPECT_EQ(launched_choices_key("", "RMCE01@retro-rewind"), std::string(""));
}

// Home keys a profile's tile on its own in the play history and the favourites.
static void test_keyed_files() {
    PlayHistory history;
    history.record("RMCE01", 100);
    history.record("RMCE01@retro-rewind", 200);
    history.record("RMCE01@../../x", 300);  // not a key: not kept
    EXPECT_EQ(history.size(), std::size_t(2));
    EXPECT_EQ(history.recent(1).front(), std::string("RMCE01@retro-rewind"));
    PlayHistory again;
    again.parse(history.serialize());
    EXPECT_TRUE(again.find("RMCE01@retro-rewind") != nullptr);
    EXPECT_EQ(again.find("RMCE01@retro-rewind")->last, 200);
    again.forget("RMCE01@retro-rewind");
    EXPECT_TRUE(again.find("RMCE01@retro-rewind") == nullptr);
    EXPECT_TRUE(again.find("RMCE01") != nullptr);

    LoaderSettings settings;
    settings.parse("favorites = RMCE01, RMCE01@retro-rewind, RMCE01@Bad Slug, SMNE01@../x\n");
    EXPECT_EQ(settings.favorites.size(), std::size_t(2));
    EXPECT_EQ(settings.favorites.count("RMCE01@retro-rewind"), std::size_t(1));
    LoaderSettings back;
    back.parse(settings.serialize());
    EXPECT_TRUE(back.favorites == settings.favorites);
}

static void test_settings_after_reset() {
    LoaderSettings before;
    before.theme = "night";
    before.home_sort = "recent";
    before.online = false;
    before.riitag_key = "secret";
    before.favorites = {"RMCE01", "SMNE01@retro"};
    before.game_folders = {"/Wii Games"};
    before.other["debug_off"] = "x";
    const LoaderSettings after = settings_after_reset(before);
    EXPECT_EQ(after.theme, std::string("default"));
    EXPECT_EQ(after.home_sort, std::string("az"));
    EXPECT_TRUE(after.online);
    EXPECT_TRUE(after.riitag_key.empty());
    EXPECT_TRUE(after.other.empty());
    EXPECT_TRUE(after.favorites == before.favorites);
    EXPECT_TRUE(after.game_folders == before.game_folders);
    // What is written back is what a fresh start plus the kept parts reads as.
    LoaderSettings reread;
    reread.parse(after.serialize());
    EXPECT_TRUE(reread.favorites == before.favorites);
    EXPECT_EQ(reread.theme, std::string("default"));
}

int main() {
    test_keys();
    test_names_and_slugs();
    test_list();
    test_parse_untrusted();
    test_keyed_files();
    test_tiles_for();
    test_launched_choices_key();
    test_settings_after_reset();
    if (g_failures == 0) std::cout << "profiles tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
