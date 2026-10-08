// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/themepack.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " at line " << __LINE__ << std::endl; g_failures++; } } while (0)

static bool parse(const std::string& text, riftwii::ThemePack& pack, std::string& error) {
    return riftwii::parse_theme_pack(reinterpret_cast<const std::uint8_t*>(text.data()), text.size(), pack, error);
}

int main() {
    using riftwii::ThemePack;
    std::string error;
    {
        const std::string text = "RIFTWII THEMES 1\nretire Linen Bookshelf background.png theme.ini\n"
                                 "file Bookshelf/theme.ini 5\nab\ncd\nfile My Theme/bar wide.png 0\n\nretire Old -\nend\n";
        ThemePack pack;
        EXPECT_TRUE(parse(text, pack, error));
        EXPECT_EQ(pack.files.size(), 2u);
        EXPECT_EQ(pack.files[0].path, "Bookshelf/theme.ini");
        EXPECT_EQ(text.substr(pack.files[0].offset, pack.files[0].size), "ab\ncd");
        EXPECT_EQ(pack.files[1].path, "My Theme/bar wide.png");
        EXPECT_EQ(pack.files[1].size, 0u);
        EXPECT_EQ(pack.retired.size(), 2u);
        EXPECT_EQ(pack.retired[0].name, "Linen");
        EXPECT_EQ(pack.retired[0].replacement, "Bookshelf");
        EXPECT_EQ(pack.retired[0].files.size(), 2u);
        EXPECT_EQ(pack.retired[0].files[1], "theme.ini");
        EXPECT_TRUE(pack.retired[1].replacement.empty());
        EXPECT_TRUE(pack.retired[1].files.empty());
    }
    ThemePack pack;
    // Not a pack, cut short, or no end.
    EXPECT_TRUE(!parse("RIFTWII THEMES 2\nend\n", pack, error));
    EXPECT_TRUE(!parse("RIFTWII THEMES 1\nfile A/b.png 10\nabc\nend\n", pack, error));
    EXPECT_TRUE(!parse("RIFTWII THEMES 1\nfile A/b.png 3\nabc\n", pack, error));
    EXPECT_TRUE(!parse("RIFTWII THEMES 1\nfile A/b.png 3\nabcd\nend\n", pack, error));
    // Paths that leave one theme's folder.
    EXPECT_TRUE(!parse("RIFTWII THEMES 1\nfile ../b.png 1\na\nend\n", pack, error));
    EXPECT_TRUE(!parse("RIFTWII THEMES 1\nfile A/../b.png 1\na\nend\n", pack, error));
    EXPECT_TRUE(!parse("RIFTWII THEMES 1\nfile A/c/b.png 1\na\nend\n", pack, error));
    EXPECT_TRUE(!parse("RIFTWII THEMES 1\nfile b.png 1\na\nend\n", pack, error));
    EXPECT_TRUE(!parse("RIFTWII THEMES 1\nfile A/.hidden 1\na\nend\n", pack, error));
    EXPECT_TRUE(!parse("RIFTWII THEMES 1\nretire .. - x\nend\n", pack, error));
    EXPECT_TRUE(!parse("RIFTWII THEMES 1\nretire A - ../x\nend\n", pack, error));
    EXPECT_TRUE(!parse("RIFTWII THEMES 1\nfile A/b.png -1\na\nend\n", pack, error));
    EXPECT_TRUE(!parse("RIFTWII THEMES 1\nhello\nend\n", pack, error));
    // An empty pack is a pack.
    EXPECT_TRUE(parse("RIFTWII THEMES 1\nend\n", pack, error));
    EXPECT_TRUE(pack.files.empty());
    if (g_failures) return 1;
    std::cout << "themepack tests passed" << std::endl;
    return 0;
}
