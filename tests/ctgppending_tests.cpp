// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// What the menu does with a CTGP-R update already on the card
// (riftwii/ctgppending.hpp).
#include <iostream>
#include <map>
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

    // What an update file is (ctgp_update_kind). A signed DIA0 for CTGPR or
    // HASH: used.
    EXPECT_TRUE(ctgp_update_kind(true, true, true, true) == CtgpUpdateKind::Usable);
    // A signed DIA0 for another package (GHOST): genuine, CTGP-R's channel
    // installs it; not bad.
    EXPECT_TRUE(ctgp_update_kind(true, true, true, false) == CtgpUpdateKind::ChannelInstalls);
    // Not a DIA0 of the right size, or a signature that fails: bad, whatever
    // package it names.
    EXPECT_TRUE(ctgp_update_kind(true, false, false, false) == CtgpUpdateKind::Bad);
    EXPECT_TRUE(ctgp_update_kind(true, true, false, true) == CtgpUpdateKind::Bad);
    EXPECT_TRUE(ctgp_update_kind(true, true, false, false) == CtgpUpdateKind::Bad);
    // The card failed to give it: says nothing about the file.
    EXPECT_TRUE(ctgp_update_kind(false, true, false, false) == CtgpUpdateKind::Unreadable);
    EXPECT_TRUE(ctgp_update_kind(false, false, false, false) == CtgpUpdateKind::Unreadable);
    // A GHOST download ends removed, not set aside (ctgp_finish_download with
    // neither checked nor bad).
    {
        std::set<std::string> card = {"update.dif.part"};
        const auto rm = [&](const char* path) { return card.erase(path) ? 0 : -1; };
        const auto mv = [&](const char* from, const char* to) {
            if (!card.erase(from)) return -1;
            card.insert(to);
            return 0;
        };
        EXPECT_TRUE(ctgp_finish_download(false, false, "update.dif.part", "update.dif", "update.dif.bad", rm, mv) ==
                    CtgpDownloadEnd::Removed);
        EXPECT_TRUE(card.empty());
    }

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

    // An update waiting: a real one is; an empty one (CTGP-R's channel
    // leaves one) is removed and isn't; none is not.
    {
        std::map<std::string, std::uint64_t> card = {{"update.dif", 12731484}};
        const auto size_of = [&card](const char* path, std::uint64_t* size) {
            const auto it = card.find(path);
            if (it == card.end()) return -1;
            *size = it->second;
            return 0;
        };
        const auto drop = [&card](const char* path) { return card.erase(path) ? 0 : -1; };
        EXPECT_TRUE(ctgp_update_waiting("update.dif", size_of, drop));
        EXPECT_TRUE(card.count("update.dif") == 1);
        card["update.dif"] = 0;
        EXPECT_TRUE(!ctgp_update_waiting("update.dif", size_of, drop));
        EXPECT_TRUE(card.empty());
        EXPECT_TRUE(!ctgp_update_waiting("update.dif", size_of, drop));
        // An empty one that can't be removed is still not an update.
        card["update.dif"] = 0;
        EXPECT_TRUE(!ctgp_update_waiting("update.dif", size_of, [](const char*) { return -1; }));
    }

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
