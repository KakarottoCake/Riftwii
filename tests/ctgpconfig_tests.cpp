// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The CTGP Revolution config.ini defaults RiftWii adds (riftwii/ctgpconfig.hpp).
#include <iostream>
#include <string>

#include "riftwii/ctgpconfig.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

static void test_empty_file() {
    std::string t;
    EXPECT_TRUE(ctgp_config_defaults(t));
    EXPECT_EQ(t, std::string("[exploit]\ndisable_ios_exploit = yes\n"));
    // A second run changes nothing.
    EXPECT_FALSE(ctgp_config_defaults(t));
}

static void test_no_section() {
    std::string t = "# Auto generated config for ctgp-r channel\n\n[ctgpr]\nmy_stuff = 1\n";
    EXPECT_TRUE(ctgp_config_defaults(t));
    EXPECT_EQ(t, std::string("# Auto generated config for ctgp-r channel\n\n[ctgpr]\nmy_stuff = 1\n\n"
                             "[exploit]\ndisable_ios_exploit = yes\n"));
    // No final line break.
    std::string u = "[music]\nvolume = 3";
    EXPECT_TRUE(ctgp_config_defaults(u));
    EXPECT_EQ(u, std::string("[music]\nvolume = 3\n\n[exploit]\ndisable_ios_exploit = yes\n"));
}

static void test_section_present() {
    std::string t = "[exploit]\ndisable_patches = no\n\n[music]\nvolume = 3\n";
    EXPECT_TRUE(ctgp_config_defaults(t));
    EXPECT_EQ(t, std::string("[exploit]\ndisable_patches = no\ndisable_ios_exploit = yes\n\n[music]\nvolume = 3\n"));
    // An empty section.
    std::string u = "[exploit]\n[music]\nvolume = 3\n";
    EXPECT_TRUE(ctgp_config_defaults(u));
    EXPECT_EQ(u, std::string("[exploit]\ndisable_ios_exploit = yes\n[music]\nvolume = 3\n"));
}

static void test_player_choice_kept() {
    std::string t = "[exploit]\n  disable_ios_exploit=no\n";
    EXPECT_FALSE(ctgp_config_defaults(t));
    EXPECT_EQ(t, std::string("[exploit]\n  disable_ios_exploit=no\n"));
    // The same key in another section does not count.
    std::string u = "[ctgpr]\ndisable_ios_exploit = no\n";
    EXPECT_TRUE(ctgp_config_defaults(u));
    EXPECT_EQ(u, std::string("[ctgpr]\ndisable_ios_exploit = no\n\n[exploit]\ndisable_ios_exploit = yes\n"));
}

static void test_crlf() {
    std::string t = "[exploit]\r\ndisable_patches = no\r\n[music]\r\nvolume = 3\r\n";
    EXPECT_TRUE(ctgp_config_defaults(t));
    EXPECT_EQ(t, std::string("[exploit]\r\ndisable_patches = no\r\ndisable_ios_exploit = yes\r\n[music]\r\nvolume = 3\r\n"));
}

static void test_ini_set() {
    // The channel's own default is replaced, nothing else changes.
    std::string t = "# Auto generated config for ctgp-r channel\r\n\r\n[sd]\r\ncompatible_mode = no\r\n\r\n[video]\r\n"
                    "video_mode = -1\r\n";
    std::string old = "x";
    EXPECT_TRUE(ctgp_channel_disc_config(t, &old));
    EXPECT_EQ(old, std::string("no"));
    EXPECT_EQ(t, std::string("# Auto generated config for ctgp-r channel\r\n\r\n[sd]\r\ncompatible_mode = yes\r\n\r\n"
                             "[video]\r\nvideo_mode = -1\r\n"));
    // Already yes: unchanged.
    EXPECT_FALSE(ctgp_channel_disc_config(t, &old));
    EXPECT_EQ(old, std::string("yes"));
    // Indented, no spaces, no final line break.
    std::string u = "[sd]\n  compatible_mode=no";
    EXPECT_TRUE(ctgp_channel_disc_config(u));
    EXPECT_EQ(u, std::string("[sd]\n  compatible_mode = yes"));
    // The same key in another section does not count; a missing one is added.
    std::string v = "[video]\ncompatible_mode = no\n";
    EXPECT_TRUE(ctgp_channel_disc_config(v, &old));
    EXPECT_EQ(old, std::string(""));
    EXPECT_EQ(v, std::string("[video]\ncompatible_mode = no\n\n[sd]\ncompatible_mode = yes\n"));
    std::string w;
    EXPECT_TRUE(ctgp_channel_disc_config(w));
    EXPECT_EQ(w, std::string("[sd]\ncompatible_mode = yes\n"));
    // A commented-out line is not the key.
    std::string x = "[sd]\n# compatible_mode = no\n";
    EXPECT_TRUE(ctgp_channel_disc_config(x));
    EXPECT_EQ(x, std::string("[sd]\n# compatible_mode = no\ncompatible_mode = yes\n"));
}

static void test_ini_set_case() {
    // Section and key names match whatever their case; the file's own
    // spelling of them stays.
    std::string t = "[SD]\nCompatible_Mode = no\n";
    std::string old;
    EXPECT_TRUE(ctgp_channel_disc_config(t, &old));
    EXPECT_EQ(old, std::string("no"));
    EXPECT_EQ(t, std::string("[SD]\nCompatible_Mode = yes\n"));
    std::string u = "[EXPLOIT]\nDisable_IOS_Exploit = no\n";
    EXPECT_FALSE(ctgp_config_defaults(u));
}

static void test_disc_patches() {
    std::string t = "[exploit]\r\ndisable_patches = no\r\ndisable_ios_exploit = no\r\n";
    std::string old;
    EXPECT_TRUE(ctgp_channel_disc_patches(t, &old));
    EXPECT_EQ(old, std::string("no"));
    EXPECT_EQ(t, std::string("[exploit]\r\ndisable_patches = yes\r\ndisable_ios_exploit = no\r\n"));
    EXPECT_FALSE(ctgp_channel_disc_patches(t, &old));
    std::string u = "[sd]\ncompatible_mode = no\n";
    EXPECT_TRUE(ctgp_channel_disc_patches(u, &old));
    EXPECT_EQ(old, std::string(""));
    EXPECT_EQ(u, std::string("[sd]\ncompatible_mode = no\n\n[exploit]\ndisable_patches = yes\n"));
}

int main() {
    test_ini_set();
    test_ini_set_case();
    test_disc_patches();
    test_empty_file();
    test_no_section();
    test_section_present();
    test_player_choice_kept();
    test_crlf();
    if (g_failures == 0) {
        std::cout << "ALL CTGPCONFIG TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
