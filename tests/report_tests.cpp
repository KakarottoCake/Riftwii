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
    }

    if (g_failures == 0) {
        std::cout << "ALL REPORT TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
