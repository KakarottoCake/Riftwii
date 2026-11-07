// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// How each code mod starts (riftwii/codemods.hpp, #368): the file, and the
// rule for several code mods on together.
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/codemods.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) EXPECT_TRUE((a) == (b))

using riftwii::CodeModAnswers;
using riftwii::CodeModStart;
using Keys = std::vector<std::string>;

static void TestFile() {
    const CodeModAnswers answers = riftwii::parse_code_mods(
        "# comment\n"
        "RSBE01\tProject+\twith\r\n"
        "RSBE01\trex_\twithout\n"
        "RSBE01\tProject+\twithout\n"     // named twice: the first answer is kept
        "RSBE01\tsd:/codes\twith\n"
        "\n"
        "rsbe01\tlower\twith\n"           // no game ID
        "RSBE01\t\twith\n"                // no key
        "RSBE01\tProject+\tmaybe\n"       // no answer
        "RSBE01\tone\ttwo\twith\n"        // a tab too many
        "RSBE01 Project+ with\n");        // spaces, not tabs
    EXPECT_EQ(answers.size(), std::size_t(3));
    EXPECT_TRUE(riftwii::code_mod_answer(answers, "RSBE01", "Project+") == CodeModStart::With);
    EXPECT_TRUE(riftwii::code_mod_answer(answers, "RSBE01", "rex_") == CodeModStart::Without);
    EXPECT_TRUE(riftwii::code_mod_answer(answers, "RSBE01", "sd:/codes") == CodeModStart::With);
    EXPECT_TRUE(riftwii::code_mod_answer(answers, "RSBE01", "Other") == CodeModStart::Unset);
    EXPECT_TRUE(riftwii::code_mod_answer(answers, "RMCE01", "Project+") == CodeModStart::Unset);
    // Written and read back: the same answers.
    EXPECT_TRUE(riftwii::parse_code_mods(riftwii::serialize_code_mods(answers)) == answers);
}

static void TestSet() {
    CodeModAnswers answers;
    EXPECT_TRUE(riftwii::set_code_mod_answer(answers, "RSBE01", "Project+", CodeModStart::With));
    EXPECT_TRUE(riftwii::set_code_mod_answer(answers, "RSBE01", "Project+", CodeModStart::Without));
    EXPECT_TRUE(riftwii::code_mod_answer(answers, "RSBE01", "Project+") == CodeModStart::Without);
    // Ask each time: forgotten.
    EXPECT_TRUE(riftwii::set_code_mod_answer(answers, "RSBE01", "Project+", CodeModStart::Unset));
    EXPECT_TRUE(answers.empty());
    // Names the file can't hold.
    EXPECT_TRUE(!riftwii::set_code_mod_answer(answers, "RSBE01", "a\tb", CodeModStart::With));
    EXPECT_TRUE(!riftwii::set_code_mod_answer(answers, "RSB", "Project+", CodeModStart::With));
    EXPECT_TRUE(answers.empty());
}

static void TestBounds() {
    CodeModAnswers answers;
    for (std::size_t i = 0; i < riftwii::kCodeModsMaxAnswers; ++i)
        EXPECT_TRUE(riftwii::set_code_mod_answer(answers, "RSBE01", "mod" + std::to_string(i), CodeModStart::With));
    // Full: no new mod, but a known one can still change.
    EXPECT_TRUE(!riftwii::set_code_mod_answer(answers, "RSBE01", "one more", CodeModStart::With));
    EXPECT_TRUE(riftwii::set_code_mod_answer(answers, "RSBE01", "mod0", CodeModStart::Without));
    EXPECT_EQ(riftwii::parse_code_mods(riftwii::serialize_code_mods(answers)).size(), riftwii::kCodeModsMaxAnswers);
    // The fullest file the menu can write is read back whole: every answer
    // with a key as long as it may be fits the read limit.
    CodeModAnswers longest;
    for (std::size_t i = 0; i < riftwii::kCodeModsMaxAnswers; ++i) {
        std::string key = std::to_string(i);
        key.resize(riftwii::kCodeModsMaxKey, 'k');
        EXPECT_TRUE(riftwii::set_code_mod_answer(longest, "RSBE01", key, CodeModStart::Without));
    }
    EXPECT_TRUE(!riftwii::set_code_mod_answer(longest, "RSBE01", std::string(riftwii::kCodeModsMaxKey + 1, 'k'),
                                              CodeModStart::Without));
    EXPECT_TRUE(riftwii::serialize_code_mods(longest).size() <= riftwii::kCodeModsMaxBytes);
    // A full file takes no new mod's answer, and says so.
    EXPECT_TRUE(!riftwii::set_code_mod_answer(longest, "RSBE01", "one more", CodeModStart::With));
    EXPECT_TRUE(riftwii::code_mod_answer(longest, "RSBE01", "one more") == CodeModStart::Unset);
    // A file read up to the limit is cut back to its last whole line.
    EXPECT_EQ(riftwii::code_mods_whole_lines("RSBE01\tA\twith\nRSBE01\tB\twi", true), std::string("RSBE01\tA\twith\n"));
    EXPECT_EQ(riftwii::code_mods_whole_lines("RSBE01\tA\twith", false), std::string("RSBE01\tA\twith"));
}

static void TestSeveral() {
    CodeModAnswers answers;
    riftwii::set_code_mod_answer(answers, "RSBE01", "A", CodeModStart::With);
    riftwii::set_code_mod_answer(answers, "RSBE01", "B", CodeModStart::With);
    riftwii::set_code_mod_answer(answers, "RSBE01", "C", CodeModStart::Without);
    EXPECT_TRUE(riftwii::shared_code_mod_answer(answers, "RSBE01", Keys{"A", "B"}) == CodeModStart::With);
    // They differ, or one has no answer yet: asked.
    EXPECT_TRUE(riftwii::shared_code_mod_answer(answers, "RSBE01", Keys{"A", "C"}) == CodeModStart::Unset);
    EXPECT_TRUE(riftwii::shared_code_mod_answer(answers, "RSBE01", Keys{"A", "D"}) == CodeModStart::Unset);
    EXPECT_TRUE(riftwii::shared_code_mod_answer(answers, "RSBE01", Keys{}) == CodeModStart::Unset);
}

// "Ask every launch" is kept as a setting of its own (the file says "ask"),
// and asks just as a mod set to nothing does (#496: no answer is remembered).
static void TestAskEachTime() {
    CodeModAnswers answers;
    EXPECT_TRUE(riftwii::set_code_mod_answer(answers, "RSBE01", "rex_", CodeModStart::AskEachTime));
    EXPECT_TRUE(riftwii::code_mod_answer(answers, "RSBE01", "rex_") == CodeModStart::AskEachTime);
    EXPECT_TRUE(riftwii::shared_code_mod_answer(answers, "RSBE01", Keys{"rex_"}) == CodeModStart::AskEachTime);
    EXPECT_TRUE(riftwii::serialize_code_mods(answers).find("RSBE01\trex_\task\n") != std::string::npos);
    EXPECT_TRUE(riftwii::parse_code_mods(riftwii::serialize_code_mods(answers)) == answers);
    // Beside a mod that is set to nothing: they differ, so asked.
    EXPECT_TRUE(riftwii::shared_code_mod_answer(answers, "RSBE01", Keys{"rex_", "Project+"}) == CodeModStart::Unset);
}

// Crash logs (#496): their own lines, which a build from before skips
// without losing the answers, off unless switched on, and on for several
// mods together only when each has them on.
void CrashLogs() {
    const std::string text =
        "# a comment\nRSBE01\tProject+\twith\nRSBE01\tProject+\tlogs\nRSBE01\trex_\tlogs\n"
        "RSBE01\tbad\tlogs\textra\nRSB\tshort\tlogs\n";
    const riftwii::CodeModCrashLogs logs = riftwii::parse_code_mod_crash_logs(text);
    EXPECT_EQ(logs.size(), 2u);
    EXPECT_TRUE(riftwii::code_mod_crash_logs(logs, "RSBE01", "Project+"));
    EXPECT_TRUE(riftwii::code_mod_crash_logs(logs, "RSBE01", "rex_"));
    EXPECT_TRUE(!riftwii::code_mod_crash_logs(logs, "RSBE01", "Other"));
    // The answers' reader skips the logs lines, as a build from before #496 does.
    const CodeModAnswers answers = riftwii::parse_code_mods(text);
    EXPECT_EQ(answers.size(), 1u);
    EXPECT_TRUE(riftwii::code_mod_answer(answers, "RSBE01", "Project+") == CodeModStart::With);
    EXPECT_TRUE(riftwii::code_mod_answer(answers, "RSBE01", "rex_") == CodeModStart::Unset);
    // Written and read back.
    const std::string written = riftwii::serialize_code_mods(answers, logs);
    EXPECT_TRUE(riftwii::parse_code_mods(written) == answers);
    EXPECT_TRUE(riftwii::parse_code_mod_crash_logs(written) == logs);
    EXPECT_TRUE(riftwii::parse_code_mod_crash_logs(riftwii::serialize_code_mods(answers)).empty());
    // Switching on and off.
    riftwii::CodeModCrashLogs set;
    EXPECT_TRUE(riftwii::set_code_mod_crash_logs(set, "RSBE01", "A", true));
    EXPECT_TRUE(riftwii::code_mod_crash_logs(set, "RSBE01", "A"));
    EXPECT_TRUE(riftwii::set_code_mod_crash_logs(set, "RSBE01", "A", false));
    EXPECT_TRUE(!riftwii::code_mod_crash_logs(set, "RSBE01", "A"));
    EXPECT_TRUE(!riftwii::set_code_mod_crash_logs(set, "RSBE01", "a\tb", true));
    // Several mods on together.
    riftwii::set_code_mod_crash_logs(set, "RSBE01", "A", true);
    riftwii::set_code_mod_crash_logs(set, "RSBE01", "B", true);
    EXPECT_TRUE(riftwii::shared_code_mod_crash_logs(set, "RSBE01", Keys{"A", "B"}));
    EXPECT_TRUE(!riftwii::shared_code_mod_crash_logs(set, "RSBE01", Keys{"A", "C"}));
    EXPECT_TRUE(!riftwii::shared_code_mod_crash_logs(set, "RSBE01", Keys{}));
    // A full set takes no new mod, but switching one off always works.
    riftwii::CodeModCrashLogs full;
    for (std::size_t i = 0; i < riftwii::kCodeModsMaxCrashLogs; ++i)
        EXPECT_TRUE(riftwii::set_code_mod_crash_logs(full, "RSBE01", "mod" + std::to_string(i), true));
    EXPECT_TRUE(!riftwii::set_code_mod_crash_logs(full, "RSBE01", "one more", true));
    EXPECT_TRUE(riftwii::set_code_mod_crash_logs(full, "RSBE01", "mod0", false));
    // The longest file: every answer and every crash log at the longest key fits the cap.
    CodeModAnswers longest;
    riftwii::CodeModCrashLogs longest_logs;
    for (std::size_t i = 0; i < riftwii::kCodeModsMaxAnswers; ++i) {
        std::string key = std::to_string(i);
        key.resize(riftwii::kCodeModsMaxKey, 'k');
        riftwii::set_code_mod_answer(longest, "RSBE01", key, CodeModStart::Without);
        if (i < riftwii::kCodeModsMaxCrashLogs) riftwii::set_code_mod_crash_logs(longest_logs, "RSBE01", key, true);
    }
    EXPECT_TRUE(riftwii::serialize_code_mods(longest, longest_logs).size() <= riftwii::kCodeModsMaxBytes);
}

int main() {
    TestFile();
    TestAskEachTime();
    TestSet();
    TestBounds();
    TestSeveral();
    CrashLogs();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "codemods tests passed" << std::endl;
    return 0;
}
