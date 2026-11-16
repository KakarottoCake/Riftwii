// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// wii/core/textfile.cpp's ReplaceWholeFile and ReadReplacedFile: a player's
// file (settings, a game's choices, Home's tiles, a controller's
// calibration) is never left empty or half-written, whichever step fails.
// The renames fail on purpose through the linker's --wrap (Linux and
// MSYS2, as wadinstall_tests); elsewhere only the plain cases run.

#include <sys/stat.h>

#include <cstdio>
#include <iostream>
#include <string>

#include "textfile.hpp"

using riftwii::wii::ReadReplacedFile;
using riftwii::wii::ReplaceWholeFile;

namespace riftwii::wii {
void logf(const char*, ...) {}
}  // namespace riftwii::wii

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl;      \
            ++g_failures;                                                            \
        }                                                                            \
    } while (0)

const std::string kPath = "choices.txt";

bool exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

std::string read(const std::string& path) {
    std::string text;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return "<missing>";
    char buffer[256];
    std::size_t got;
    while ((got = std::fread(buffer, 1, sizeof buffer, f)) > 0) text.append(buffer, got);
    std::fclose(f);
    return text;
}

void write(const std::string& path, const std::string& text) {
    FILE* f = std::fopen(path.c_str(), "wb");
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
}

void clean() {
    for (const char* suffix : {"", ".part", ".bak"}) std::remove((kPath + suffix).c_str());
}

bool replace(const std::string& text) { return ReplaceWholeFile(kPath, text.data(), text.size()); }

void test_first_save() {
    clean();
    CHECK(replace("new"));
    CHECK(read(kPath) == "new");
    CHECK(!exists(kPath + ".part"));
    CHECK(!exists(kPath + ".bak"));
}

void test_save_over_old() {
    clean();
    write(kPath, "old");
    CHECK(replace("new"));
    CHECK(read(kPath) == "new");
    CHECK(!exists(kPath + ".part"));
    CHECK(!exists(kPath + ".bak"));
}

// An empty save is still a whole file (a list with nothing in it).
void test_empty_save() {
    clean();
    write(kPath, "old");
    CHECK(replace(""));
    CHECK(exists(kPath));
    CHECK(read(kPath).empty());
}

// The moment between the two renames: only the .bak holds the old copy.
void test_read_falls_back_to_backup() {
    clean();
    write(kPath + ".bak", "old");
    std::string text;
    CHECK(ReadReplacedFile(kPath, text));
    CHECK(text == "old");
    write(kPath, "current");
    CHECK(ReadReplacedFile(kPath, text));
    CHECK(text == "current");
    clean();
    CHECK(!ReadReplacedFile(kPath, text));
}

// No file yet but a .bak from a save cut off earlier: it stays until this
// save has succeeded.
void test_backup_kept_without_file() {
    clean();
    write(kPath + ".bak", "older");
    CHECK(replace("new"));
    CHECK(read(kPath) == "new");
}

}  // namespace

#ifdef RIFTWII_TEST_WRAP_RENAME
namespace {
// Which rename fails: 0 none, 1 the old file to .bak, 2 the .part into place.
int g_fail_rename = 0;
int g_renames = 0;
}  // namespace

extern "C" int __real_rename(const char*, const char*);
extern "C" int __wrap_rename(const char* from, const char* to) {
    ++g_renames;
    if (g_fail_rename != 0 && g_renames == g_fail_rename) return -1;
    return __real_rename(from, to);
}

namespace {

void fail_rename(int which) {
    g_fail_rename = which;
    g_renames = 0;
}

// The old file can't step aside: nothing changes, the new copy is dropped.
void test_first_rename_fails() {
    clean();
    write(kPath, "old");
    fail_rename(1);
    CHECK(!replace("new"));
    fail_rename(0);
    CHECK(read(kPath) == "old");
    CHECK(!exists(kPath + ".part"));
    CHECK(!exists(kPath + ".bak"));
}

// The new copy can't take its name: the old one is put back, and the whole
// new one stays as .part (settings.txt reads that at the next start). The
// rename that puts the old copy back is the third.
void test_last_rename_fails() {
    clean();
    write(kPath, "old");
    fail_rename(2);
    CHECK(!replace("new"));
    fail_rename(0);
    CHECK(read(kPath) == "old");
    CHECK(read(kPath + ".part") == "new");
    std::string text;
    CHECK(ReadReplacedFile(kPath, text));
    CHECK(text == "old");
}

// Only a .bak from an earlier cut-off save, and this save's one rename (the
// .part into place) fails: the .bak is still what ReadReplacedFile reads,
// and a later save that works installs the new copy and removes it.
void test_failed_save_keeps_backup_only_state() {
    clean();
    write(kPath + ".bak", "older");
    fail_rename(1);
    CHECK(!replace("new"));
    fail_rename(0);
    CHECK(!exists(kPath));
    std::string text;
    CHECK(ReadReplacedFile(kPath, text));
    CHECK(text == "older");
    CHECK(replace("new"));
    CHECK(read(kPath) == "new");
    CHECK(!exists(kPath + ".bak"));
    CHECK(!exists(kPath + ".part"));
}

}  // namespace
#endif

int main() {
    test_first_save();
    test_save_over_old();
    test_empty_save();
    test_read_falls_back_to_backup();
    test_backup_kept_without_file();
#ifdef RIFTWII_TEST_WRAP_RENAME
    test_first_rename_fails();
    test_last_rename_fails();
    test_failed_save_keeps_backup_only_state();
#endif
    clean();
    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed" << std::endl;
        return 1;
    }
    std::cout << "textfile: all checks passed" << std::endl;
    return 0;
}
