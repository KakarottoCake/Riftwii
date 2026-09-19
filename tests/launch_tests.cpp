// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/launch.hpp"

#include <iostream>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

static const char* kModA =
    "<wiidisc version=\"1\"><id game=\"RMCE\"/>"
    "<options><section name=\"Tracks\">"
    "<option name=\"Pack\" default=\"1\"><choice name=\"Alpha\"><patch id=\"a\"/></choice><choice name=\"Beta\"><patch id=\"b\"/></choice></option>"
    "<option name=\"Music\" default=\"0\"><choice name=\"On\"><patch id=\"a\"/></choice></option>"
    "</section></options>"
    "<patch id=\"a\"><file disc=\"/a.bin\" external=\"a.bin\"/></patch>"
    "<patch id=\"b\"><file disc=\"/b.bin\" external=\"b.bin\"/></patch>"
    "</wiidisc>";

static const char* kModOther =
    "<wiidisc version=\"1\"><id game=\"RSBE\"/>"
    "<options><section name=\"S\"><option name=\"O\" default=\"1\"><choice name=\"C\"><patch id=\"p\"/></choice></option></section></options>"
    "<patch id=\"p\"><file disc=\"/x.bin\" external=\"x.bin\"/></patch>"
    "</wiidisc>";

static void test_model() {
    riftwii::DiscIdentity disc{"RMCE01", 0, 0};
    riftwii::LaunchModel model;
    model.add("a.xml", "sd:/riivolution/a.xml", kModA, &disc);
    model.add("other.xml", "sd:/riivolution/other.xml", kModOther, &disc);
    model.add("broken.xml", "sd:/riivolution/broken.xml", "<wiidisc", &disc);
    EXPECT_EQ(model.packages.size(), std::size_t(3));
    EXPECT_TRUE(model.packages[0].valid);
    EXPECT_TRUE(model.packages[0].for_disc);
    EXPECT_EQ(model.packages[0].detail, std::string("2 option(s), 3 choice(s), 2 patch definition(s)"));
    EXPECT_TRUE(model.packages[1].valid);
    EXPECT_FALSE(model.packages[1].for_disc);
    EXPECT_EQ(model.packages[1].detail, std::string("1 option(s), 1 choice(s), 1 patch definition(s); for RSBE"));
    EXPECT_FALSE(model.packages[2].valid);
    EXPECT_FALSE(model.packages[2].detail.empty());

    // Only a valid package for this disc can be enabled.
    EXPECT_TRUE(model.set_enabled(0, true));
    EXPECT_FALSE(model.set_enabled(1, true));
    EXPECT_FALSE(model.set_enabled(2, true));
    EXPECT_FALSE(model.set_enabled(3, true));
    EXPECT_TRUE(model.set_enabled(1, false));

    // Choices cycle through off; names follow.
    EXPECT_EQ(model.choice_name(0, 0), std::string("Alpha"));
    EXPECT_EQ(model.choice_name(0, 1), std::string("Off"));
    EXPECT_TRUE(model.cycle(0, 0, +1));
    EXPECT_EQ(model.choice_name(0, 0), std::string("Beta"));
    EXPECT_TRUE(model.cycle(0, 0, +1));
    EXPECT_EQ(model.choice_name(0, 0), std::string("Off"));
    EXPECT_TRUE(model.cycle(0, 0, +1));
    EXPECT_EQ(model.choice_name(0, 0), std::string("Alpha"));
    EXPECT_TRUE(model.cycle(0, 0, -1));
    EXPECT_EQ(model.choice_name(0, 0), std::string("Off"));
    EXPECT_TRUE(model.cycle(0, 1, +1));
    EXPECT_EQ(model.choice_name(0, 1), std::string("On"));
    EXPECT_FALSE(model.cycle(0, 2, +1));
    EXPECT_FALSE(model.cycle(2, 0, +1));
    EXPECT_EQ(model.choice_name(2, 0), std::string(""));

    // Selections state every option of every enabled package.
    std::vector<riftwii::PackageChoices> sel = model.selections();
    EXPECT_EQ(sel.size(), std::size_t(1));
    if (sel.size() == 1) {
        EXPECT_EQ(sel[0].xml_sd_path, std::string("sd:/riivolution/a.xml"));
        EXPECT_EQ(sel[0].choices.size(), std::size_t(2));
        if (sel[0].choices.size() == 2) {
            EXPECT_EQ(sel[0].choices[0].first, std::string("Tracks/Pack"));
            EXPECT_EQ(sel[0].choices[0].second, std::string(""));
            EXPECT_EQ(sel[0].choices[1].first, std::string("Tracks/Music"));
            EXPECT_EQ(sel[0].choices[1].second, std::string("On"));
        }
    }
    EXPECT_TRUE(model.set_enabled(0, false));
    EXPECT_TRUE(model.selections().empty());
}

static void test_persistence() {
    riftwii::DiscIdentity disc{"RMCE01", 0, 0};
    riftwii::LaunchModel model;
    model.add("a.xml", "sd:/riivolution/a.xml", kModA, &disc);
    model.add("other.xml", "sd:/riivolution/other.xml", kModOther, &disc);
    model.set_enabled(0, true);
    model.cycle(0, 0, +1);  // Beta
    model.cycle(0, 1, +1);  // On
    const std::string saved = model.save();
    EXPECT_EQ(saved, std::string("a.xml\ton\na.xml\tTracks/Pack\tBeta\na.xml\tTracks/Music\tOn\n"
                                 "other.xml\toff\nother.xml\tS/O\tC\n"));

    // A fresh model with the same packages takes the saved state back;
    // lines for things that no longer exist are ignored.
    riftwii::LaunchModel again;
    again.add("a.xml", "sd:/riivolution/a.xml", kModA, &disc);
    again.add("other.xml", "sd:/riivolution/other.xml", kModOther, &disc);
    again.restore(saved + "gone.xml\ton\na.xml\tTracks/Nope\tX\na.xml\tTracks/Pack\tGamma\r\nbroken line\n");
    EXPECT_TRUE(again.packages[0].enabled);
    EXPECT_FALSE(again.packages[1].enabled);
    EXPECT_EQ(again.choice_name(0, 0), std::string("Beta"));
    EXPECT_EQ(again.choice_name(0, 1), std::string("On"));
    // "other" is for another disc: an "on" line cannot enable it.
    again.restore("other.xml\ton\n");
    EXPECT_FALSE(again.packages[1].enabled);
    // An empty choice turns an option off.
    again.restore("a.xml\tTracks/Pack\t\n");
    EXPECT_EQ(again.choice_name(0, 0), std::string("Off"));

    // No disc known: everything valid is for the disc.
    riftwii::LaunchModel nodisc;
    nodisc.add("other.xml", "sd:/riivolution/other.xml", kModOther, nullptr);
    EXPECT_TRUE(nodisc.packages[0].for_disc);
    EXPECT_TRUE(nodisc.set_enabled(0, true));
}

int main() {
    test_model();
    test_persistence();
    if (g_failures == 0) {
        std::cout << "ALL LAUNCH TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
