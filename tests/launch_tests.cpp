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

static const char* kModExactDisc =
    "<wiidisc version=\"1\"><id game=\"RMCE\" disc=\"1\" revision=\"2\"/></wiidisc>";

static const char* kModSimple =
    "<wiidisc version=\"1\"><id game=\"RMCE\"/>"
    "<options><section name=\"Solo\"><option name=\"Enable\" default=\"0\">"
    "<choice name=\"Apply\"><patch id=\"p\"/></choice></option></section></options>"
    "<patch id=\"p\"><file disc=\"/a.bin\" external=\"a.bin\"/></patch>"
    "</wiidisc>";

static const char* kModMultiChoice =
    "<wiidisc version=\"1\"><id game=\"RMCE\"/>"
    "<options><section name=\"Choices\"><option name=\"Pick\" default=\"0\">"
    "<choice name=\"One\"><patch id=\"a\"/></choice><choice name=\"Two\"><patch id=\"b\"/></choice>"
    "</option></section></options><patch id=\"a\"/><patch id=\"b\"/></wiidisc>";

static const char* kModMultiOption =
    "<wiidisc version=\"1\"><id game=\"RMCE\"/>"
    "<options><section name=\"Choices\"><option name=\"First\" default=\"0\">"
    "<choice name=\"One\"><patch id=\"a\"/></choice></option><option name=\"Second\" default=\"0\">"
    "<choice name=\"Two\"><patch id=\"b\"/></choice></option></section></options>"
    "<patch id=\"a\"/><patch id=\"b\"/></wiidisc>";

static void test_simple_package_activation() {
    riftwii::DiscIdentity disc{"RMCE01", 0, 0};
    riftwii::LaunchModel simple;
    simple.add("simple.xml", "sd:/riivolution/simple.xml", kModSimple, &disc);
    EXPECT_EQ(simple.choice_name(0, 0), std::string("Off"));
    EXPECT_TRUE(simple.set_enabled(0, true));
    EXPECT_EQ(simple.choice_name(0, 0), std::string("Apply"));

    // Disabling only removes the package from the launch set; it does not
    // throw away the user's already selected choice.
    EXPECT_TRUE(simple.set_enabled(0, false));
    EXPECT_EQ(simple.choice_name(0, 0), std::string("Apply"));
    EXPECT_TRUE(simple.set_enabled(0, true));
    EXPECT_EQ(simple.choice_name(0, 0), std::string("Apply"));

    // Turning the only choice off and enabling again supplies the simple
    // package's sole choice, as the home screen promises.
    EXPECT_TRUE(simple.cycle(0, 0, +1));
    EXPECT_EQ(simple.choice_name(0, 0), std::string("Off"));
    EXPECT_TRUE(simple.set_enabled(0, false));
    EXPECT_TRUE(simple.set_enabled(0, true));
    EXPECT_EQ(simple.choice_name(0, 0), std::string("Apply"));

    // A package needs both one option and one choice for this shortcut.
    riftwii::LaunchModel multi_choice;
    multi_choice.add("multi-choice.xml", "sd:/riivolution/multi-choice.xml", kModMultiChoice, &disc);
    EXPECT_TRUE(multi_choice.set_enabled(0, true));
    EXPECT_EQ(multi_choice.choice_name(0, 0), std::string("Off"));
    riftwii::LaunchModel multi_option;
    multi_option.add("multi-option.xml", "sd:/riivolution/multi-option.xml", kModMultiOption, &disc);
    EXPECT_TRUE(multi_option.set_enabled(0, true));
    EXPECT_EQ(multi_option.choice_name(0, 0), std::string("Off"));
    EXPECT_EQ(multi_option.choice_name(0, 1), std::string("Off"));

    // Restore migrates v0.3.4's enabled + empty-choice records to the sole
    // usable choice, in either line order.
    riftwii::LaunchModel restored_before;
    restored_before.add("simple.xml", "sd:/riivolution/simple.xml", kModSimple, &disc);
    restored_before.restore("simple.xml\tSolo/Enable\t\nsimple.xml\ton\n");
    EXPECT_TRUE(restored_before.packages[0].enabled);
    EXPECT_EQ(restored_before.choice_name(0, 0), std::string("Apply"));
    riftwii::LaunchModel restored_after;
    restored_after.add("simple.xml", "sd:/riivolution/simple.xml", kModSimple, &disc);
    restored_after.restore("simple.xml\ton\nsimple.xml\tSolo/Enable\t\n");
    EXPECT_TRUE(restored_after.packages[0].enabled);
    EXPECT_EQ(restored_after.choice_name(0, 0), std::string("Apply"));

    // Package Off preserves its saved choice instead of changing it during
    // restore, including for the simple shape.
    riftwii::LaunchModel restored_off;
    restored_off.add("simple.xml", "sd:/riivolution/simple.xml", kModSimple, &disc);
    restored_off.restore("simple.xml\tSolo/Enable\tApply\nsimple.xml\toff\n");
    EXPECT_FALSE(restored_off.packages[0].enabled);
    EXPECT_EQ(restored_off.choice_name(0, 0), std::string("Apply"));
}

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

    // Game ID alone is insufficient: XML may require a particular disc
    // number and revision, which the frontend now preserves for all sources.
    const riftwii::DiscIdentity exact_identity{"RMCE01", 2, 1};
    const riftwii::DiscIdentity wrong_revision_identity{"RMCE01", 1, 1};
    const riftwii::DiscIdentity wrong_number_identity{"RMCE01", 2, 0};
    riftwii::LaunchModel exact;
    exact.add("exact.xml", "sd:/riivolution/exact.xml", kModExactDisc, &exact_identity);
    EXPECT_TRUE(exact.packages[0].valid);
    EXPECT_TRUE(exact.packages[0].for_disc);
    riftwii::LaunchModel wrong_revision;
    wrong_revision.add("exact.xml", "sd:/riivolution/exact.xml", kModExactDisc, &wrong_revision_identity);
    EXPECT_FALSE(wrong_revision.packages[0].for_disc);
    riftwii::LaunchModel wrong_number;
    wrong_number.add("exact.xml", "sd:/riivolution/exact.xml", kModExactDisc, &wrong_number_identity);
    EXPECT_FALSE(wrong_number.packages[0].for_disc);
    EXPECT_TRUE(riftwii::same_disc_identity(exact_identity, exact_identity));
    EXPECT_FALSE(riftwii::same_disc_identity(exact_identity, wrong_revision_identity));
    EXPECT_FALSE(riftwii::same_disc_identity(exact_identity, wrong_number_identity));

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
    EXPECT_EQ(saved, std::string("*riftwii*\tsaves\tnand\n"
                                 "a.xml\ton\na.xml\tTracks/Pack\tBeta\na.xml\tTracks/Music\tOn\n"
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

static void test_saves() {
    riftwii::DiscIdentity disc{"RMCE01", 0, 0};
    riftwii::LaunchModel model;
    model.add("a.xml", "sd:/riivolution/a.xml", kModA, &disc);
    model.add("other.xml", "sd:/riivolution/other.xml", kModOther, &disc);
    model.add("broken.xml", "sd:/riivolution/broken.xml", "<wiidisc", &disc);
    model.add("broken-other.xml", "sd:/riivolution/broken-other.xml",
              "<wiidisc version=\"1\">\n<id game = 'SB4' />\n<options><bad", &disc);
    model.add("broken-mine.xml", "sd:/riivolution/broken-mine.xml",
              "<wiidisc version=\"1\"><id\n  game=\"RMC\"><region type=\"E\"/></id><options><bad", &disc);

    // Matching packs show; other-disc packs hide, broken or not. A broken
    // pack whose game cannot be read shows so its error can be seen.
    EXPECT_TRUE(riftwii::show_package(model.packages[0]));
    EXPECT_FALSE(riftwii::show_package(model.packages[1]));
    EXPECT_TRUE(riftwii::show_package(model.packages[2]));
    EXPECT_FALSE(model.packages[3].valid);
    EXPECT_FALSE(riftwii::show_package(model.packages[3]));
    EXPECT_FALSE(model.packages[4].valid);
    EXPECT_TRUE(riftwii::show_package(model.packages[4]));

    // Save mode round-trips; garbage never clobbers it.
    EXPECT_EQ(model.save_mode, std::string("nand"));
    model.save_mode = "separate";
    riftwii::LaunchModel again;
    again.add("a.xml", "sd:/riivolution/a.xml", kModA, &disc);
    again.restore(model.save());
    EXPECT_EQ(again.save_mode, std::string("separate"));
    again.restore("*riftwii*\tsaves\tbogus\n");
    EXPECT_EQ(again.save_mode, std::string("separate"));
    again.restore("*riftwii*\tsaves\tfresh\n");
    EXPECT_EQ(again.save_mode, std::string("fresh"));
    again.restore("*riftwii*\tsaves\tnand\n");
    EXPECT_EQ(again.save_mode, std::string("nand"));
    // Old choice files without the settings line restore as nand.
    riftwii::LaunchModel legacy;
    legacy.add("a.xml", "sd:/riivolution/a.xml", kModA, &disc);
    legacy.restore("a.xml\ton\n");
    EXPECT_EQ(legacy.save_mode, std::string("nand"));

    // Override resolution: XML wins, then mode, then nothing.
    riftwii::SaveOverride o = riftwii::resolve_save_override("separate", "sd:/riftwii/sg", "RMCE01");
    EXPECT_TRUE(o.dir.empty());
    o = riftwii::resolve_save_override("separate", "", "RMCE01");
    EXPECT_EQ(o.dir, std::string("sd:/riftwii/saves/RMCE01/clone"));
    EXPECT_TRUE(o.clone);
    EXPECT_FALSE(o.note.empty());
    o = riftwii::resolve_save_override("fresh", "", "RMCE01");
    EXPECT_EQ(o.dir, std::string("sd:/riftwii/saves/RMCE01/fresh"));
    EXPECT_FALSE(o.clone);

    // A plain boot still needs the resident path whenever the per-game save
    // mode supplies a directory; the Wii menu routes this case through
    // preflight/RunLaunch rather than the no-op plain-boot path.
    riftwii::LaunchModel plain;
    EXPECT_TRUE(plain.selections().empty());
    EXPECT_FALSE(riftwii::resolve_save_override("separate", "", "RMCE01").dir.empty());
    EXPECT_FALSE(riftwii::resolve_save_override("fresh", "", "RMCE01").dir.empty());
    EXPECT_FALSE(riftwii::needs_launch_pipeline(false, "nand"));
    EXPECT_TRUE(riftwii::needs_launch_pipeline(false, "separate"));
    EXPECT_TRUE(riftwii::needs_launch_pipeline(false, "fresh"));
    EXPECT_TRUE(riftwii::needs_launch_pipeline(true, "nand"));
    o = riftwii::resolve_save_override("nand", "", "RMCE01");
    EXPECT_TRUE(o.dir.empty());
    o = riftwii::resolve_save_override("bogus", "", "RMCE01");
    EXPECT_TRUE(o.dir.empty());
    o = riftwii::resolve_save_override("separate", "", "");
    EXPECT_TRUE(o.dir.empty());
}

int main() {
    test_model();
    test_simple_package_activation();
    test_persistence();
    test_saves();
    if (g_failures == 0) {
        std::cout << "ALL LAUNCH TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
