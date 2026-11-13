// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// What the menu does with a CTGP-R update already on the card
// (riftwii/ctgppending.hpp).
#include <iostream>
#include <set>
#include <string>

#include "riftwii/ctgppending.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

int main() {
    const std::uint32_t v1192 = 0x01031192u, v1193 = 0x01031193u, v1194 = 0x01031194u;
    // A file the plugin read, for the pack it is on: offered.
    EXPECT_TRUE(ctgp_pending_step(true, false, v1192, v1193, v1192) == CtgpPendingStep::Install);
    // The pack's version unknown: offered (the install checks again).
    EXPECT_TRUE(ctgp_pending_step(true, false, 0, v1193, v1192) == CtgpPendingStep::Install);
    // Already installed (a power cut between the install and its rename): retired.
    EXPECT_TRUE(ctgp_pending_step(true, false, v1193, v1193, v1192) == CtgpPendingStep::Retire);
    // Made for another version of the pack: set aside.
    EXPECT_TRUE(ctgp_pending_step(true, false, v1194, v1193, v1192) == CtgpPendingStep::SetAside);
    // The file itself is broken (not a DIA0, a bad signature, broken commands): set aside.
    EXPECT_TRUE(ctgp_pending_step(false, true, v1192, 0, 0) == CtgpPendingStep::SetAside);
    // It could not be read now (the card, memory, the plugin): left for next time.
    EXPECT_TRUE(ctgp_pending_step(false, false, v1192, 0, 0) == CtgpPendingStep::Stop);

    // The move: an older file of the target name goes first, then the rename.
    std::set<std::string> files = {"update.dif", "update.dif.bad"};
    bool locked = false;
    const auto remove = [&](const char* path) {
        if (locked || !files.count(path)) return -1;
        files.erase(path);
        return 0;
    };
    const auto rename = [&](const char* from, const char* to) {
        if (locked || !files.count(from) || files.count(to)) return -1;
        files.erase(from);
        files.insert(to);
        return 0;
    };
    EXPECT_TRUE(ctgp_move_aside("update.dif", "update.dif.bad", remove, rename));
    EXPECT_TRUE(files == std::set<std::string>{"update.dif.bad"});
    // A locked card: nothing moves, and the caller is told so.
    files = {"update.dif"};
    locked = true;
    EXPECT_TRUE(!ctgp_move_aside("update.dif", "update.dif.installed", remove, rename));
    EXPECT_TRUE(files == std::set<std::string>{"update.dif"});

    // How a download ends: checked → update.dif; found unusable → kept as
    // update.dif.bad (an older one replaced); anything else → removed.
    locked = false;
    files = {"update.dif.part", "update.dif.bad"};
    EXPECT_TRUE(ctgp_finish_download(false, true, "update.dif.part", "update.dif", "update.dif.bad", remove, rename) ==
                CtgpDownloadEnd::SetAside);
    EXPECT_TRUE(files == std::set<std::string>{"update.dif.bad"});
    files = {"update.dif.part"};
    EXPECT_TRUE(ctgp_finish_download(true, false, "update.dif.part", "update.dif", "update.dif.bad", remove, rename) ==
                CtgpDownloadEnd::Kept);
    EXPECT_TRUE(files == std::set<std::string>{"update.dif"});
    files = {"update.dif.part"};
    EXPECT_TRUE(ctgp_finish_download(false, false, "update.dif.part", "update.dif", "update.dif.bad", remove, rename) ==
                CtgpDownloadEnd::Removed);
    EXPECT_TRUE(files.empty());
    // A bad one that can't be moved is removed, and is not reported as set aside.
    files = {"update.dif.part", "update.dif.bad"};
    const auto no_rename = [](const char*, const char*) { return -1; };
    EXPECT_TRUE(ctgp_finish_download(false, true, "update.dif.part", "update.dif", "update.dif.bad", remove,
                                     no_rename) == CtgpDownloadEnd::Removed);
    EXPECT_TRUE(files.empty());

    // The size of a file, and a stream the card can't size (a pipe: its seeks
    // fail), which must not look like an empty or huge file.
    {
        const std::string path = "ctgppending_size_test.bin";
        std::FILE* out = std::fopen(path.c_str(), "wb");
        EXPECT_TRUE(out != nullptr);
        if (out != nullptr) {
            std::fwrite("0123456789", 1, 10, out);
            std::fclose(out);
        }
        std::FILE* in = std::fopen(path.c_str(), "rb");
        std::uint64_t size = 99;
        EXPECT_TRUE(in != nullptr && ctgp_file_size(in, size) && size == 10);
        char first = 0;
        EXPECT_TRUE(in != nullptr && std::fread(&first, 1, 1, in) == 1 && first == '0');  // left at the start
        if (in != nullptr) std::fclose(in);
        std::remove(path.c_str());
        std::FILE* pipe = popen("true", "r");
        EXPECT_TRUE(pipe != nullptr);
        if (pipe != nullptr) {
            size = 99;
            EXPECT_TRUE(!ctgp_file_size(pipe, size));
            EXPECT_TRUE(size == 0);
            pclose(pipe);
        }
    }

    if (g_failures == 0) std::cout << "ctgppending: all tests passed" << std::endl;
    return g_failures ? 1 : 0;
}
