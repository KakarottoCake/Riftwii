// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the real raw SD client at the libfat-to-runtime handoff.
#include "sdio.hpp"

#include <gccore.h>

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;
#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << "Failed: " #condition " at " << __LINE__ << '\n'; \
            ++failures; \
        } \
    } while (0)

struct Scenario {
    s32 reset_result = 0;
    s32 status_result = 0;
    u32 status = 0x110001;
    u32 if_condition = 0x1AA;
    u32 operation_condition = 0xC0300000;
    u32 rca = 0x1234;
    u32 rca_flags = 0;  // CMD3's R6 status bits below the address
    u32 app_status = 0x120;  // CMD55's R1: ready for data, application command
    u32 fail_command = 0xFFFFFFFF;
    bool reset_stuck = false;
    bool clock_stuck = false;
    bool reopen_fails = false;
    bool d2x = false;
    bool held_by_d2x_fat = false;
    unsigned opens = 0;
    unsigned closes = 0;
    unsigned waits = 0;
    unsigned op_condition_calls = 0;
    unsigned d2x_reads = 0;
    s32 live_fd = -1;
    std::vector<u32> commands;
    u32 read_argument = 0;
} scenario;

bool aligned(const void* pointer) {
    return (reinterpret_cast<std::uintptr_t>(pointer) & 31) == 0;
}

s32 command(void* request, void* response) {
    CHECK(aligned(request));
    CHECK(aligned(response));
    const auto* words = static_cast<const u32*>(request);
    auto* reply = static_cast<u32*>(response);
    const u32 cmd = words[0];
    scenario.commands.push_back(cmd);
    if (cmd == scenario.fail_command) return -77;
    if (cmd == 8) reply[0] = scenario.if_condition;
    if (cmd == 0x29) {
        ++scenario.op_condition_calls;
        reply[0] = scenario.operation_condition;
    }
    if (cmd == 3) reply[0] = scenario.rca << 16 | scenario.rca_flags;
    if (cmd == 0x37) reply[0] = scenario.app_status;
    if (cmd == 7 && words[3] != 0) CHECK(words[3] == scenario.rca << 16);
    if (cmd == 0x12) scenario.read_argument = words[3];
    return 0;
}

void expect_failed_open(const std::string& detail) {
    riftwii::wii::sdio::Card card;
    std::string error;
    CHECK(!riftwii::wii::sdio::open_card(card, error));
    CHECK(error.find(detail) != std::string::npos);
    CHECK(card.fd == -1);
    CHECK(!card.selected);
    CHECK(scenario.live_fd == -1);
}

void test_ios_initialized_card() {
    for (bool sdhc : {false, true}) {
        scenario = Scenario{};
        scenario.status = 0x10001 | (sdhc ? 0x100000 : 0);
        riftwii::wii::sdio::Card card;
        std::string error;
        CHECK(riftwii::wii::sdio::open_card(card, error));
        CHECK(error.empty());
        CHECK(card.selected && card.sdhc == sdhc && card.rca == scenario.rca);
        CHECK(scenario.opens == 1);
        CHECK(std::find(scenario.commands.begin(), scenario.commands.end(), 0) == scenario.commands.end());
        alignas(32) unsigned char sector[512]{};
        CHECK(riftwii::wii::sdio::read_sectors(card, 17, 1, sector, error));
        CHECK(scenario.read_argument == (sdhc ? 17u : 17u * 512u));
        riftwii::wii::sdio::close_card(card);
        CHECK(scenario.closes == 1);
        CHECK(scenario.live_fd == -1);
    }
}

void test_manual_initialization() {
    for (bool reset_failed : {false, true}) {
        for (bool sdhc : {false, true}) {
            scenario = Scenario{};
            scenario.reset_result = reset_failed ? static_cast<s32>(0xC1000020u) : 0;
            scenario.status = 1;  // inserted, but IOS did not initialise it
            scenario.operation_condition = sdhc ? 0xC0300000 : 0x80300000;
            riftwii::wii::sdio::Card card;
            std::string error;
            CHECK(riftwii::wii::sdio::open_card(card, error));
            CHECK(error.empty());
            CHECK(card.selected && card.sdhc == sdhc && card.rca == scenario.rca);
            CHECK(scenario.opens == 2 && scenario.closes == 1);
            const std::vector<u32> identity{0, 8, 0x37, 0x29, 2, 3};
            CHECK(scenario.commands.size() >= identity.size());
            CHECK(std::equal(identity.begin(), identity.end(), scenario.commands.begin()));
            alignas(32) unsigned char sector[512]{};
            CHECK(riftwii::wii::sdio::read_sectors(card, 17, 1, sector, error));
            CHECK(scenario.read_argument == (sdhc ? 17u : 17u * 512u));
            riftwii::wii::sdio::close_card(card);
            CHECK(scenario.closes == 2 && scenario.live_fd == -1);
        }
    }

    // An IOS error must not make the client reuse the reset output's RCA,
    // even if GETSTATUS still reports its earlier initialized state.
    scenario = Scenario{};
    scenario.reset_result = static_cast<s32>(0xC1000020u);
    riftwii::wii::sdio::Card card;
    std::string error;
    CHECK(riftwii::wii::sdio::open_card(card, error));
    CHECK(scenario.opens == 2);
    riftwii::wii::sdio::close_card(card);
    CHECK(scenario.live_fd == -1);
}

// A card status error in an R1 reply fails the manual initialization even
// though IOS itself answered (CodeRabbit on #230).
void test_manual_initialization_card_error() {
    scenario = Scenario{};
    scenario.status = 1;  // inserted, but IOS did not initialise it
    scenario.app_status = 0x00400000;  // illegal command
    expect_failed_open("the card answered with an error (status 00400000)");

    // CMD3's R6 reply with its illegal-command flag (bit 14): no address taken.
    scenario = Scenario{};
    scenario.status = 1;
    scenario.rca_flags = 0x4000;
    expect_failed_open("the card answered with an error (status 12344000)");
}

void test_failures_are_bounded_and_closed() {
    scenario = Scenario{};
    scenario.status_result = -88;
    expect_failed_open("status failed");
    CHECK(scenario.opens == 1);

    // Reset and status both refused: the manual initialization still runs,
    // and it finds the card (CodeRabbit on #230).
    scenario = Scenario{};
    scenario.reset_result = static_cast<s32>(0xC1000020u);
    scenario.status_result = -88;
    {
        riftwii::wii::sdio::Card card;
        std::string error;
        CHECK(riftwii::wii::sdio::open_card(card, error));
        CHECK(card.selected && card.rca == scenario.rca);
        CHECK(std::find(scenario.commands.begin(), scenario.commands.end(), 0u) != scenario.commands.end());
        riftwii::wii::sdio::close_card(card);
    }

    scenario = Scenario{};
    scenario.status = 0;
    expect_failed_open("no card");
    CHECK(scenario.opens == 1);

    scenario = Scenario{};
    scenario.status = 1;
    scenario.reset_stuck = true;
    expect_failed_open("100 checks");
    CHECK(scenario.waits == 100);

    scenario = Scenario{};
    scenario.status = 1;
    scenario.clock_stuck = true;
    expect_failed_open("100 checks");
    CHECK(scenario.waits == 100);

    scenario = Scenario{};
    scenario.status = 1;
    scenario.operation_condition = 0x40300000;
    expect_failed_open("100 checks");
    CHECK(scenario.op_condition_calls == 100);
    CHECK(scenario.waits == 100);

    scenario = Scenario{};
    scenario.status = 1;
    scenario.if_condition = 0;
    expect_failed_open("check pattern");

    scenario = Scenario{};
    scenario.status = 1;
    scenario.rca = 0;
    expect_failed_open("no relative address");

    scenario = Scenario{};
    scenario.status = 1;
    scenario.reset_result = static_cast<s32>(0xC0FFFFE0u);
    scenario.fail_command = 2;
    expect_failed_open("IPC -1056964640");

    scenario = Scenario{};
    scenario.status = 1;
    scenario.reset_result = static_cast<s32>(0xC1000020u);
    scenario.fail_command = 2;
    expect_failed_open("IPC -1056964576");

    scenario = Scenario{};
    scenario.status = 1;
    scenario.reopen_fails = true;
    expect_failed_open("reopen");
}

void test_d2x_sharing() {
    scenario = Scenario{};
    scenario.d2x = true;
    riftwii::wii::sdio::Card card;
    std::string error;
    CHECK(riftwii::wii::sdio::open_card(card, error));
    CHECK(card.d2x && card.fd == 88);
    CHECK(scenario.opens == 0);
    alignas(32) unsigned char sector[512]{};
    CHECK(riftwii::wii::sdio::read_sectors(card, 0, 1, sector, error));
    CHECK(scenario.d2x_reads == 1);
    riftwii::wii::sdio::close_card(card);
    CHECK(scenario.closes == 0);
}

// d2x's NAND emulation holds the card: RiftWii's raw card is refused
// before any device is opened, on either driver.
void test_refused_while_d2x_fat_holds_the_card() {
    for (bool d2x : {false, true}) {
        scenario = Scenario{};
        scenario.d2x = d2x;
        scenario.held_by_d2x_fat = true;
        riftwii::wii::sdio::Card card;
        std::string error;
        CHECK(!riftwii::wii::sdio::open_card(card, error));
        CHECK(error == "d2x's NAND emulation has the SD card, and RiftWii stays off it while d2x does");
        CHECK(scenario.opens == 0 && scenario.commands.empty());
    }
}

}  // namespace

s32 IOS_Open(const char* path, u32 mode) {
    CHECK(std::string(path) == "/dev/sdio/slot0" && mode == 1);
    CHECK(scenario.live_fd == -1);
    ++scenario.opens;
    if (scenario.opens == 2 && scenario.reopen_fails) return -66;
    scenario.live_fd = static_cast<s32>(40 + scenario.opens);
    return scenario.live_fd;
}

s32 IOS_Close(s32 fd) {
    CHECK(fd == scenario.live_fd);
    scenario.live_fd = -1;
    ++scenario.closes;
    return 0;
}

s32 IOS_Ioctl(s32 fd, s32 ioctl, void* input, u32, void* output, u32) {
    CHECK(fd == scenario.live_fd);
    if (ioctl == 4) {
        *static_cast<u32*>(output) = scenario.rca << 16;
        return scenario.reset_result;
    }
    if (ioctl == 0x0B) {
        *static_cast<u32*>(output) = scenario.status;
        return scenario.status_result;
    }
    if (ioctl == 2) {
        const u32 reg = *static_cast<u32*>(input);
        u32 value = 0;
        if (reg == 0x2F && scenario.reset_stuck) value = 7;
        if (reg == 0x2C && !scenario.clock_stuck) value = 2;
        *static_cast<u32*>(output) = value;
        return 0;
    }
    if (ioctl == 1 || ioctl == 6) return 0;
    if (ioctl == 7) return command(input, output);
    CHECK(false);
    return -1;
}

s32 IOS_Ioctlv(s32 fd, s32 ioctl, u32 inputs, u32 outputs, ioctlv* vectors) {
    CHECK(fd == scenario.live_fd && ioctl == 7);
    CHECK(inputs == 2 && outputs == 1);
    CHECK(aligned(vectors) && vectors[2].len == 16);
    return command(vectors[0].data, vectors[2].data);
}

extern "C" void udelay(int) {
    ++scenario.waits;
}

namespace riftwii::wii {
bool using_d2x_sd() { return scenario.d2x; }
bool sd_held_by_d2x_fat() { return scenario.held_by_d2x_fat; }
bool d2x_sd_open(std::string&) { return true; }
std::int32_t d2x_sd_fd() { return 88; }
bool d2x_sd_read(std::uint32_t, std::uint32_t, void*) {
    ++scenario.d2x_reads;
    return true;
}
}  // namespace riftwii::wii

int main() {
    test_ios_initialized_card();
    test_manual_initialization();
    test_manual_initialization_card_error();
    test_failures_are_bounded_and_closed();
    test_d2x_sharing();
    test_refused_while_d2x_fat_holds_the_card();
    if (failures == 0) std::cout << "Raw SD handoff and bounded manual initialization passed\n";
    return failures == 0 ? 0 : 1;
}
