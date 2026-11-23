// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/emunand.hpp"

#include <iostream>
#include <string>

using namespace riftwii;

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::cerr << "Line " << __LINE__ << ": " #condition "\n"; ++failures; } } while (0)

int main() {
    constexpr std::uint64_t title = 0x0001000157424D45ULL;
    std::uint64_t parsed = 0;
    CHECK(emunand_parse_title("00010001-57424d45", parsed));
    CHECK(parsed == title);
    CHECK(emunand_parse_title("0001000157424D45", parsed));
    CHECK(parsed == title);
    CHECK(emunand_title_text(title) == "00010001-57424d45");
    CHECK(emunand_title_code(title) == "WBME");
    CHECK(emunand_title_code(0x0001000052365245ULL) == "R6RE");
    CHECK(emunand_title_code(0x0001000161626331ULL) == "abc1");
    CHECK(emunand_title_code(0x0000000100000002ULL) == "00000001-00000002");
    CHECK(emunand_title_code(0x00010001412F4243ULL) == "00010001-412f4243");
    CHECK(emunand_title_code(0x00010001FF424344ULL) == "00010001-ff424344");

    for (const unsigned type : {1, 0x10002, 0x10008}) {
        CHECK(emunand_delete_protected((std::uint64_t(type) << 32) | 0x57424D45));
    }
    // System-channel families must stay protected in every region, even
    // when they use the ordinary downloadable-channel type.
    for (const unsigned family : {0x484142, 0x484354, 0x484356, 0x484358, 0x484359}) {
        for (const unsigned region : {'A', 'E', 'J', 'P', 'K'}) {
            const std::uint64_t system = 0x0001000100000000ULL | (std::uint64_t(family) << 8) | region;
            CHECK(emunand_delete_protected(system));
        }
    }
    const std::uint64_t games[] = {title, 0x0001000052365245ULL, 0x0001000148415445ULL,
                                  0x0001000148435745ULL, 0x0001000448414245ULL,
                                  0x0001000557354145ULL};
    for (const std::uint64_t game : games) {
        CHECK(!emunand_delete_protected(game));
    }
    for (const char* bad : {"", "00010001/57424d45", "00010001-57424d45/..", "00010001-57424d4g",
                            " 0010001-57424d45", "0001000-157424d45", "0001000157424d45x"}) {
        parsed = 42;
        CHECK(!emunand_parse_title(bad, parsed));
        CHECK(parsed == 42);
    }
    EmuNandTitlePaths paths;
    CHECK(emunand_title_paths(title, paths));
    CHECK(paths.title == "/title/00010001/57424d45");
    CHECK(paths.data == "/title/00010001/57424d45/data");
    CHECK(paths.ticket == "/ticket/00010001/57424d45.tik");
    for (const std::uint64_t system : {0x100000002ULL, 0x10000003AULL, 0x1000000FAULL}) {
        CHECK(emunand_system_title(system));
        CHECK(!emunand_manageable_title(system));
        CHECK(!emunand_title_paths(system, paths));
        CHECK(paths.title.empty() && paths.data.empty() && paths.ticket.empty());
    }
    CHECK(!emunand_manageable_title(0));
    CHECK(!emunand_manageable_title(0x0001000100000000ULL));
    CHECK(!emunand_manageable_title(0xFFFFFFFF57424D45ULL));
    for (const unsigned high : {0x10000, 0x10001, 0x10002, 0x10004, 0x10005, 0x10008})
        CHECK(emunand_manageable_title((std::uint64_t(high) << 32) | 0x57424D45));

    std::string path;
    CHECK(emunand_save_path(title, "", path));
    CHECK(path == "/title/00010001/57424d45/data");
    CHECK(emunand_save_path(title, "folder/banner.bin", path));
    CHECK(path == "/title/00010001/57424d45/data/folder/banner.bin");
    CHECK(emunand_save_path(title, std::string(12, 'a') + "/" + std::string(12, 'b'), path));
    for (const char* bad : {".", "..", "../ticket", "/banner.bin", "a//b", "a/", "sd:/secret", "a\\b",
                            "abcdefghijklX", "a\n", "a.", "a ", "a?", "a/../../sys"}) {
        CHECK(!emunand_save_path(title, bad, path));
        CHECK(path.empty());
    }
    CHECK(!emunand_save_path(0x100000002ULL, "setting.txt", path));
    CHECK(!emunand_save_path(title, std::string(12, 'a') + "/" + std::string(12, 'b') + "/" + std::string(12, 'c'), path));
    CHECK(!emunand_save_path(title, "a/b/c/d/e/f/g/h/i", path));
    CHECK(emunand_file_name(".riftwii-wad"));
    CHECK(!emunand_file_name(std::string("a\0b", 3)));

    CHECK(emunand_root("sd:/nands/01/", path));
    CHECK(path == "sd:/nands/01");
    CHECK(emunand_root("usb2:/nands/01", path));
    for (const char* bad : {"sd:/", "usb:/", "sd:/nands/../sys", "sd:/nands//01", "sd:/nands\\01", "nand:/title",
                            "sd:/nands./01", "sd:/nands /01", "sd:/nands/01."}) {
        CHECK(!emunand_root(bad, path));
        CHECK(path.empty());
    }
    CHECK(std::string(kEmuNandMiis) == "/shared2/menu/FaceLib/RFL_DB.dat");
    CHECK(std::string(kEmuNandSysconf) == "/shared2/sys/SYSCONF");
    CHECK(std::string(kEmuNandSettings) == "/title/00000001/00000002/data/setting.txt");
    return failures ? 1 : 0;
}
