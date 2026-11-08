// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "codemodstart.hpp"

#include <cstdio>
#include <sdcard/wiisd_io.h>
#include <sys/stat.h>

#include "log.hpp"

namespace riftwii::wii {
namespace {

constexpr const char* kCodeModsPath = "sd:/riftwii/codemods.txt";

CodeModAnswers g_answers;
riftwii::CodeModCrashLogs g_crash_logs;
bool g_loaded = false;
// There but unreadable: never written over, so its answers aren't lost.
bool g_unreadable = false;
// The card was taken out and isn't mounted again yet: nothing is read or
// written, so no answer of the old card reaches the next one.
bool g_held = false;

// At most kCodeModsMaxBytes, cut back to its last whole line.
bool ReadCodeModsFile(CodeModAnswers& answers, riftwii::CodeModCrashLogs& crash_logs) {
    struct stat st;
    // Only the old copy: a save stopped between moving it aside and putting
    // the new one in.
    const std::string old = std::string(kCodeModsPath) + ".old";
    const char* path = stat(kCodeModsPath, &st) == 0 ? kCodeModsPath : stat(old.c_str(), &st) == 0 ? old.c_str() : nullptr;
    if (!path) return true;  // no file yet: nothing remembered
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::string head(kCodeModsMaxBytes, '\0');
    const std::size_t got = std::fread(&head[0], 1, head.size(), f);
    const bool failed = std::ferror(f) != 0;
    // One byte more than the cap means the file went on past it.
    const bool cut = got == kCodeModsMaxBytes && std::fgetc(f) != EOF;
    std::fclose(f);
    if (failed) return false;
    head.resize(got);
    if (cut)
        logf("Code mods: %s is larger than %u bytes, so only its first %u are read\n", kCodeModsPath,
             static_cast<unsigned>(kCodeModsMaxBytes), static_cast<unsigned>(kCodeModsMaxBytes));
    const std::string whole = code_mods_whole_lines(head, cut);
    answers = parse_code_mods(whole);
    crash_logs = parse_code_mod_crash_logs(whole);
    return true;
}

const CodeModAnswers& Answers() {
    static const CodeModAnswers none;
    if (g_held) return none;
    if (!g_loaded) {
        g_loaded = true;
        g_unreadable = !ReadCodeModsFile(g_answers, g_crash_logs);
        if (g_unreadable)
            logf("Code mods: %s is there but could not be read, so answers are not saved over it\n", kCodeModsPath);
        else
            logf("Code mods: %u answer(s) and %u with crash logs on in %s\n", static_cast<unsigned>(g_answers.size()),
                 static_cast<unsigned>(g_crash_logs.size()), kCodeModsPath);
    }
    return g_answers;
}

// Written beside it, then put in its place. FAT can't rename over a file,
// so the old one is moved aside first and only deleted once the new one
// is in; a failed step puts it back, so the card never loses its answers.
bool Save() {
    // Also a card pulled while a screen of its own is open, before Home
    // notices: the slot is empty now.
    if (g_unreadable || g_held || !__io_wiisd.isInserted()) return false;
    mkdir("sd:/riftwii", 0777);
    const std::string temp = std::string(kCodeModsPath) + ".part";
    const std::string old = std::string(kCodeModsPath) + ".old";
    const std::string text = serialize_code_mods(g_answers, g_crash_logs);
    FILE* f = std::fopen(temp.c_str(), "wb");
    bool ok = f != nullptr;
    if (f) {
        ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
        ok = std::fclose(f) == 0 && ok;
    }
    struct stat st;
    const bool had = stat(kCodeModsPath, &st) == 0;
    if (ok && had) {
        std::remove(old.c_str());
        ok = std::rename(kCodeModsPath, old.c_str()) == 0;
    }
    if (ok) {
        ok = std::rename(temp.c_str(), kCodeModsPath) == 0;
        if (!ok && had) std::rename(old.c_str(), kCodeModsPath);
    }
    if (ok && had) std::remove(old.c_str());
    if (!ok) {
        std::remove(temp.c_str());
        logf("Code mods: %s could not be written, so the answer lasts until RiftWii closes\n", kCodeModsPath);
    }
    return ok;
}

}  // namespace

CodeModStart CodeModAnswer(const std::string& game_id, const std::string& key) {
    return code_mod_answer(Answers(), game_id, key);
}

CodeModStart SharedCodeModAnswer(const std::string& game_id, const std::vector<std::string>& keys) {
    return shared_code_mod_answer(Answers(), game_id, keys);
}

CodeModAnswerKept SetCodeModAnswer(const std::string& game_id, const std::string& key, CodeModStart start) {
    if (g_held) return CodeModAnswerKept::NotKept;
    Answers();
    // A full file takes no new answer, and nothing changes in memory either.
    if (!set_code_mod_answer(g_answers, game_id, key, start)) return CodeModAnswerKept::NotKept;
    return Save() ? CodeModAnswerKept::Saved : CodeModAnswerKept::UntilClose;
}

bool CodeModCrashLogs(const std::string& game_id, const std::string& key) {
    Answers();
    return !g_held && code_mod_crash_logs(g_crash_logs, game_id, key);
}

bool SharedCodeModCrashLogs(const std::string& game_id, const std::vector<std::string>& keys) {
    Answers();
    return !g_held && shared_code_mod_crash_logs(g_crash_logs, game_id, keys);
}

CodeModAnswerKept SetCodeModCrashLogs(const std::string& game_id, const std::string& key, bool on) {
    if (g_held) return CodeModAnswerKept::NotKept;
    Answers();
    if (!set_code_mod_crash_logs(g_crash_logs, game_id, key, on)) return CodeModAnswerKept::NotKept;
    return Save() ? CodeModAnswerKept::Saved : CodeModAnswerKept::UntilClose;
}

void ForgetCodeModAnswers() {
    g_answers.clear();
    g_crash_logs.clear();
    g_loaded = false;
    g_unreadable = false;
}

void HoldCodeModAnswers(bool held) {
    if (held) ForgetCodeModAnswers();
    g_held = held;
}

}  // namespace riftwii::wii
