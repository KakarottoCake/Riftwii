// SPDX-License-Identifier: GPL-3.0-or-later
// Problem reports: the bundle, its size cap, and what is read from the
// boot log, the choices file and paste.rs's answer.
#include "riftwii/problemreport.hpp"

#include <iostream>
#include <string>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

static bool has(const std::string& text, const std::string& what) { return text.find(what) != std::string::npos; }

static std::string numbered_lines(int count, const char* tag) {
    std::string text;
    for (int i = 0; i < count; ++i) text += std::string(tag) + " line " + std::to_string(i) + "\n";
    return text;
}

int main() {
    // Small enough: every part whole, in order, the missing one named.
    {
        const std::vector<ReportPart> parts = {{"crash.txt", "PC 80001234\n"}, {"boot.log", "a\nb\n"},
                                               {"update.txt", "", false}};
        const std::string r = assemble_report("RiftWii 2.4.3-beta problem report", parts, 10000);
        EXPECT_TRUE(r.compare(0, 33, "RiftWii 2.4.3-beta problem report") == 0);
        EXPECT_TRUE(has(r, "  crash.txt (12 bytes)\n"));
        EXPECT_TRUE(has(r, "  update.txt (not there)\n"));
        EXPECT_TRUE(has(r, "\n===== crash.txt (12 bytes) =====\nPC 80001234\n"));
        EXPECT_TRUE(has(r, "\n===== boot.log (4 bytes) =====\na\nb\n"));
        EXPECT_TRUE(has(r, "\n===== update.txt (not there) =====\n"));
        EXPECT_TRUE(r.find("crash.txt (12") < r.find("===== boot.log"));
        EXPECT_FALSE(has(r, "cut"));
    }
    // Too big: the big log loses its middle, the small crash stays whole,
    // and the whole stays under the limit.
    {
        const std::string log = numbered_lines(20000, "session");
        const std::string crash = numbered_lines(20, "crash");
        const std::vector<ReportPart> parts = {{"crash.txt", crash}, {"session.log", log}};
        const std::string r = assemble_report("summary", parts, 64 * 1024);
        EXPECT_TRUE(r.size() <= 64 * 1024);
        EXPECT_TRUE(r.size() > 60 * 1024);
        EXPECT_TRUE(has(r, crash));
        EXPECT_TRUE(has(r, "session line 0\n"));
        EXPECT_TRUE(has(r, "session line 19999\n"));
        EXPECT_FALSE(has(r, "session line 10000\n"));
        EXPECT_TRUE(has(r, "cut from the middle) =====\n"));
        EXPECT_TRUE(has(r, " bytes cut ...]\nsession line "));
        // Whole lines on both sides of the cut.
        const std::size_t mark = r.find("\n[... ");
        EXPECT_TRUE(mark != std::string::npos && r[mark - 1] == '\n');
        // More of the end than of the start.
        const std::size_t start = r.find("===== session.log");
        const std::size_t end = r.find("===== end");
        EXPECT_TRUE(mark - start < end - mark);
    }
    // Two big parts share the room; neither is dropped.
    {
        const std::vector<ReportPart> parts = {{"a.log", numbered_lines(9000, "a")}, {"b.xml", numbered_lines(9000, "b")}};
        const std::string r = assemble_report("s", parts, 40 * 1024);
        EXPECT_TRUE(r.size() <= 40 * 1024);
        EXPECT_TRUE(has(r, "a line 8999\n"));
        EXPECT_TRUE(has(r, "b line 8999\n"));
        EXPECT_TRUE(has(r, "a line 0\n"));
        EXPECT_TRUE(has(r, "b line 0\n"));
    }
    // The real limit holds with room to spare for paste.rs.
    {
        const std::vector<ReportPart> parts = {{"session.log", std::string(900000, 'x')},
                                               {"boot.log", numbered_lines(30000, "boot")}};
        EXPECT_TRUE(assemble_report("s", parts, kReportLimit).size() <= kReportLimit);
        EXPECT_TRUE(kReportLimit < 384u * 1024u);
    }

    // The launched game.
    EXPECT_EQ(launched_game_id("RiftWii 2.4.3-beta: launch RMCE01 with packages\nDrive: rev\n"), "RMCE01");
    EXPECT_EQ(launched_game_id("[     0.000] RiftWii 2.4.3-beta: launch SB4E01 with packages\n"), "SB4E01");
    EXPECT_EQ(launched_game_id("RiftWii 2.4.3-beta: boot USB\n"), "");
    // A plain boot: the disc line names it.
    EXPECT_EQ(launched_game_id("[    19.217] RiftWii 2.5.0-beta: boot USB\n[    21.440] Drive: rev 0000\n"
                               "[    21.473] Disc: RSBE01  \"Super Smash Bros. Brawl\"  disc 0 version 2\n"
                               "[    30.000] Disc: SB4E01  \"later\"\n"),
              "RSBE01");
    EXPECT_EQ(launched_game_id("RiftWii 2.5.0-beta: boot disc\nDisc: RMCE01  \"Mario Kart Wii\"\n"), "RMCE01");
    EXPECT_EQ(launched_game_id("[ 1.0] RiftWii 2.5.0-beta: boot disc\n[ 2.0] Home: no disc in the drive\n"), "");
    EXPECT_EQ(launched_game_id(""), "");
    EXPECT_EQ(launched_game_id("RiftWii x: launch ../../ with packages\n"), "");

    // The packs a choices file turns on.
    {
        const std::string choices =
            "*riftwii*\tsaves\tsd\n*riftwii*\tcheats\ton\nRetroRewind6.xml\ton\r\n"
            "RetroRewind6.xml\tRetro Rewind/Pack\tEnabled\nOld.xml\toff\nmod.xml @ 192.168.1.20:1137\ton\n"
            "RetroRewind6.xml\ton\n";
        const std::vector<std::string> on = enabled_pack_files(choices);
        EXPECT_EQ(on.size(), 2u);
        if (on.size() == 2) {
            EXPECT_EQ(on[0], "RetroRewind6.xml");
            EXPECT_EQ(on[1], "mod.xml @ 192.168.1.20:1137");
        }
        EXPECT_TRUE(enabled_pack_files("").empty());
    }

    // paste.rs's answers.
    {
        std::string link, error;
        bool partial = true;
        EXPECT_TRUE(paste_link(201, "https://paste.rs/AbCd\n", link, partial, error));
        EXPECT_EQ(link, "https://paste.rs/AbCd");
        EXPECT_FALSE(partial);
        EXPECT_TRUE(paste_link(206, "https://paste.rs/Xy", link, partial, error));
        EXPECT_TRUE(partial);
        EXPECT_FALSE(paste_link(429, "", link, partial, error));
        EXPECT_TRUE(has(error, "busy"));
        EXPECT_FALSE(paste_link(500, "oops", link, partial, error));
        EXPECT_EQ(error, "paste.rs answered 500: oops");
        EXPECT_FALSE(paste_link(201, "<html>x y</html>", link, partial, error));
        // dpaste.com, the fallback: the same shape of answer.
        EXPECT_TRUE(paste_link(201, "https://dpaste.com/CSX5L2N8W\n", link, partial, error, "dpaste.com"));
        EXPECT_EQ(link, "https://dpaste.com/CSX5L2N8W");
        EXPECT_FALSE(paste_link(503, "down", link, partial, error, "dpaste.com"));
        EXPECT_EQ(error, "dpaste.com answered 503: down");
    }

    // The text as valid UTF-8: good sequences kept, anything else '?', one
    // byte for one, control characters too (tab and line ends kept).
    {
        EXPECT_EQ(printable_utf8("plain\ttext\r\n"), "plain\ttext\r\n");
        EXPECT_EQ(printable_utf8("Pok\xC3\xA9mon \xE3\x83\x9E \xF0\x9F\x8E\xAE"), "Pok\xC3\xA9mon \xE3\x83\x9E \xF0\x9F\x8E\xAE");
        EXPECT_EQ(printable_utf8("Pok\xE9mon"), "Pok?mon");                 // Latin-1
        EXPECT_EQ(printable_utf8("\x83\x7D\x83\x8A"), "?}??");            // Shift-JIS
        EXPECT_EQ(printable_utf8("a\xC0\xAF" "b"), "a??b");                   // overlong '/'
        EXPECT_EQ(printable_utf8("\xE0\x80\xAF"), "???");                  // overlong, 3 bytes
        EXPECT_EQ(printable_utf8("\xF0\x80\x80\xAF"), "????");             // overlong, 4 bytes
        EXPECT_EQ(printable_utf8("\xED\xA0\x80"), "???");                  // a surrogate
        EXPECT_EQ(printable_utf8("\xF4\x90\x80\x80"), "????");            // past U+10FFFF
        EXPECT_EQ(printable_utf8("cut \xE3\x83"), "cut ??");               // cut short
        EXPECT_EQ(printable_utf8(std::string("nul\0bell\x07" "del\x7F", 13)), "nul?bell?del?");
        const std::string raw = std::string("x\xFF\xFEy", 4);
        EXPECT_EQ(printable_utf8(raw).size(), raw.size());
        const std::vector<ReportPart> parts = {{"boot.log", std::string("Disc: \x83\x7D title\n")}};
        const std::string r = assemble_report("s", parts, 10000);
        EXPECT_TRUE(has(r, "Disc: ?} title"));
        EXPECT_EQ(printable_utf8(r), r);
    }

    if (g_failures == 0) {
        std::cout << "ALL REPORT TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
