// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <iostream>
#include <string>

#include "riftwii/presence.hpp"
#include "riftwii/settingsfile.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

// A game with two packs, as the menu sends it, comes back whole.
static void test_game_round_trip() {
    PresenceMessage sent;
    sent.event = PresenceEvent::Game;
    sent.console = presence_console_tag(0x0403AC68);
    sent.version = "2610-541";
    sent.game_id = "RMCP01";
    sent.title = "Mario Kart Wii";
    sent.platform = "wii";
    sent.mods = {"CTGP Revolution", "Retro Rewind"};
    const std::string packet = presence_packet(sent);
    EXPECT_EQ(packet.compare(0, 19, "RIFTWII-PRESENCE 1\n"), 0);
    PresenceMessage got;
    EXPECT_TRUE(parse_presence_packet(packet, got));
    EXPECT_TRUE(got.event == PresenceEvent::Game);
    EXPECT_EQ(got.console, sent.console);
    EXPECT_EQ(got.version, std::string("2610-541"));
    EXPECT_EQ(got.game_id, std::string("RMCP01"));
    EXPECT_EQ(got.title, std::string("Mario Kart Wii"));
    EXPECT_EQ(got.platform, std::string("wii"));
    EXPECT_EQ(got.mods.size(), std::size_t(2));
    if (got.mods.size() == 2) EXPECT_EQ(got.mods[1], std::string("Retro Rewind"));
    EXPECT_FALSE(got.restart);
}

// The menu's message has no game in it, only when the next one comes.
static void test_menu_and_bye() {
    PresenceMessage menu;
    menu.event = PresenceEvent::Menu;
    menu.game_id = "RMCP01";  // left over from a launch: not sent
    menu.next_seconds = kPresenceMenuSeconds;
    const std::string packet = presence_packet(menu);
    EXPECT_EQ(packet.find("id "), std::string::npos);
    PresenceMessage got;
    EXPECT_TRUE(parse_presence_packet(packet, got));
    EXPECT_TRUE(got.event == PresenceEvent::Menu);
    EXPECT_EQ(got.next_seconds, kPresenceMenuSeconds);

    PresenceMessage bye;
    bye.event = PresenceEvent::Bye;
    EXPECT_TRUE(parse_presence_packet(presence_packet(bye), got));
    EXPECT_TRUE(got.event == PresenceEvent::Bye);
}

// A title with a line break cannot add a line of its own, a long one is
// cut without splitting a character, and too many packs stop at the limit.
static void test_bounds() {
    PresenceMessage sent;
    sent.event = PresenceEvent::Game;
    sent.game_id = "RSBE01";
    sent.title = "Brawl\nevent bye";
    std::string long_title;
    for (int i = 0; i < 100; ++i) long_title += "\xC3\xA9";  // é, two bytes each
    sent.platform = long_title;
    for (int i = 0; i < 40; ++i) sent.mods.push_back(std::string(120, 'm'));
    const std::string packet = presence_packet(sent);
    EXPECT_TRUE(packet.size() <= kPresenceMaxBytes);
    PresenceMessage got;
    EXPECT_TRUE(parse_presence_packet(packet, got));
    EXPECT_TRUE(got.event == PresenceEvent::Game);
    EXPECT_EQ(got.title, std::string("Brawl event bye"));
    EXPECT_EQ(got.platform.size() % 2, std::size_t(0));
    EXPECT_TRUE(got.platform.size() <= 128);
    EXPECT_TRUE(!got.mods.empty() && got.mods.size() <= 8);
}

static void test_refused() {
    PresenceMessage got;
    EXPECT_FALSE(parse_presence_packet("", got));
    EXPECT_FALSE(parse_presence_packet("RIFTWII-PRESENCE 2\nevent menu\n", got));
    EXPECT_FALSE(parse_presence_packet("RIFTWII-PRESENCE 1\n", got));
    EXPECT_FALSE(parse_presence_packet("RIFTWII-PRESENCE 1\nevent dance\n", got));
    EXPECT_FALSE(parse_presence_packet("RIFTWII-PRESENCE 1\nevent game\ntitle No ID\n", got));
    EXPECT_FALSE(parse_presence_packet("RIFTWII-PRESENCE 1\nevent menu\nnext 99999999999\n", got));
    EXPECT_FALSE(parse_presence_packet(std::string("RIFTWII-PRESENCE 1\nevent menu\n") + std::string(2000, 'x'), got));
    // A newer RiftWii's key is skipped.
    EXPECT_TRUE(parse_presence_packet("RIFTWII-PRESENCE 1\nevent menu\ncolour blue\n", got));
}

static void test_console_tag() {
    EXPECT_EQ(presence_console_tag(0x0403AC68).size(), std::size_t(8));
    EXPECT_EQ(presence_console_tag(1), presence_console_tag(1));
    EXPECT_TRUE(presence_console_tag(1) != presence_console_tag(2));
}

// settings.txt: on unless turned off, and a computer named only by a
// plain address or host name.
static void test_settings() {
    LoaderSettings s;
    EXPECT_TRUE(s.presence);
    EXPECT_TRUE(s.presence_host.empty());
    s.parse("presence = off\npresence_host = 192.168.1.20\n");
    EXPECT_FALSE(s.presence);
    EXPECT_EQ(s.presence_host, std::string("192.168.1.20"));
    LoaderSettings again;
    again.parse(s.serialize());
    EXPECT_FALSE(again.presence);
    EXPECT_EQ(again.presence_host, std::string("192.168.1.20"));
    LoaderSettings bad;
    bad.parse("presence_host = http://evil/../x\n");
    EXPECT_TRUE(bad.presence_host.empty());
    bad.parse("presence_host = my-pc.local\n");
    EXPECT_EQ(bad.presence_host, std::string("my-pc.local"));
}

int main() {
    test_game_round_trip();
    test_menu_and_bye();
    test_bounds();
    test_refused();
    test_console_tag();
    test_settings();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "presence tests passed" << std::endl;
    return 0;
}
