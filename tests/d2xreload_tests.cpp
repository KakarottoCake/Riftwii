// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the actual Wii DI client against a mocked IOS transport.
#include "di.hpp"
#include <gccore.h>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "log.hpp"

static int failures = 0;
#define CHECK(x) do { if (!(x)) { ++failures; std::cerr << "Failed: " #x " at " << __LINE__ << '\n'; } } while (0)
static s32 open_reply = 42, ioctl_reply = 0;
static unsigned opens = 0, calls = 0, closes = 0;
static u32 expected_mode = 2, expected_slot = 249;
static bool aligned(const void* p) { return (reinterpret_cast<std::uintptr_t>(p) & 31) == 0; }
s32 IOS_Open(const char* path, u32 mode) {
    ++opens;
    CHECK(std::string(path) == "/dev/es"); CHECK(mode == 0);
    return open_reply;
}
static unsigned async_closes = 0;
s32 IOS_Close(s32 fd) {
    if (fd == 7) { ++async_closes; return 0; }  // the stop-motor handle
    ++closes; CHECK(fd == 42); return 0;
}
s32 IOS_Ioctl(s32, s32, void*, u32, void*, u32) { CHECK(false); return -1; }
s32 IOS_Ioctlv(s32 fd, s32 command, u32 in, u32 out, ioctlv* vectors) {
    ++calls;
    CHECK(fd == 42); CHECK(command == 0xA0); CHECK(out == 0);
    CHECK(in == (expected_mode == 2 ? 2u : 1u)); CHECK(aligned(vectors));
    CHECK(vectors[0].len == 4); CHECK(aligned(vectors[0].data));
    CHECK(*static_cast<u32*>(vectors[0].data) == expected_mode);
    if (in == 2) {
        CHECK(vectors[1].len == 4); CHECK(aligned(vectors[1].data));
        CHECK(*static_cast<u32*>(vectors[1].data) == expected_slot);
    }
    return ioctl_reply;
}

// Logged lines, for the power-off line.
static std::vector<std::string> logged;
namespace riftwii::wii {
void logf(const char* format, ...) {
    char text[512];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    logged.push_back(text);
}
}  // namespace riftwii::wii

// The stop-motor path: asynchronous open and ioctl. `async_answers` false
// leaves a request unanswered, as a stuck drive does.
static bool async_answers = true;
static ipccallback pending_done = nullptr;  // the callback of a request left unanswered
static s32 async_open_reply = 7, async_stop_reply = 1;
static unsigned async_opens = 0, async_stops = 0;
s32 IOS_OpenAsync(const char* path, u32 mode, ipccallback done, void* user) {
    ++async_opens;
    CHECK(std::string(path) == "/dev/di"); CHECK(mode == 0); CHECK(aligned(path));
    if (async_answers) done(async_open_reply, user); else pending_done = done;
    return 0;
}
s32 IOS_IoctlAsync(s32 fd, s32 command, void* in, u32 in_len, void* out, u32 out_len, ipccallback done, void* user) {
    ++async_stops;
    CHECK(fd == async_open_reply); CHECK(command == 0xE3);
    CHECK(aligned(in) && aligned(out)); CHECK(in_len >= 12 && out_len >= 32);
    const u32* words = static_cast<const u32*>(in);
    CHECK(words[0] == 0xE3000000u); CHECK(words[1] == 0); CHECK(words[2] == 0);  // no eject, no kill
    if (async_answers) done(async_stop_reply, user);
    return 0;
}

static void test_stop_motor() {
    std::string error;
    CHECK(riftwii::di::stop_motor(50, error));
    CHECK(error.empty());
    CHECK(async_opens == 1 && async_stops == 1 && async_closes == 0);  // the handle is left open

    // /dev/di will not open: no stop is sent, the call returns at once.
    async_open_reply = -6;
    CHECK(!riftwii::di::stop_motor(50, error));
    CHECK(error.find("-6") != std::string::npos);
    CHECK(async_opens == 2 && async_stops == 1);
    async_open_reply = 7;

    // The drive answers with something other than success.
    async_stop_reply = 2;
    CHECK(!riftwii::di::stop_motor(50, error));
    CHECK(error.find("DI reply 2") != std::string::npos);
    async_stop_reply = 1;

}

// Last, because a stop that timed out blocks every later one by design.
static void test_stop_motor_timeout() {
    std::string error;
    // A drive that never answers: the call gives up near the limit.
    async_answers = false;
    const auto before = std::chrono::steady_clock::now();
    CHECK(!riftwii::di::stop_motor(40, error));
    const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - before);
    CHECK(waited.count() >= 40 && waited.count() < 1000);
    CHECK(error.find("did not answer") != std::string::npos);
    CHECK(pending_done != nullptr);
    CHECK(async_closes == 0);

    // Its answer comes late, and a second call neither waits nor sends
    // anything while the first request may still own the buffers.
    async_answers = true;
    const unsigned opens_before = async_opens, stops_before = async_stops;
    pending_done(1, nullptr);
    logged.clear();
    riftwii::di::stop_motor_for_power_off();  // returns at once; the power-off goes on
    CHECK(async_opens == opens_before && async_stops == stops_before);
    CHECK(logged.size() == 1 && logged[0].find("powering off anyway") != std::string::npos);
}

static void test_power_off_line() {
    logged.clear();
    riftwii::di::stop_motor_for_power_off();
    CHECK(logged.size() == 1);
    CHECK(logged.size() == 1 && logged[0].find("is stopped") != std::string::npos);
    logged.clear();
    async_open_reply = -6;
    riftwii::di::stop_motor_for_power_off();  // returns: the power-off goes on
    CHECK(logged.size() == 1);
    CHECK(logged.size() == 1 && logged[0].find("powering off anyway") != std::string::npos);
    async_open_reply = 7;
}

int main() {
    std::string error;
    CHECK(!riftwii::di::ios_reload_block_active());
    for (u32 slot : {200u, 249u, 250u, 255u}) {
        expected_slot = slot;
        CHECK(riftwii::di::set_ios_reload_block(true, slot, error));
        CHECK(error.empty());
        CHECK(riftwii::di::ios_reload_block_active());
    }
    CHECK(opens == 4 && calls == 4 && closes == 4);
    for (u32 slot : {0u, 36u, 58u, 199u, 256u}) {
        CHECK(!riftwii::di::set_ios_reload_block(true, slot, error));
        CHECK(!error.empty());
    }
    CHECK(opens == 4 && calls == 4 && closes == 4);
    expected_mode = 0;
    CHECK(riftwii::di::set_ios_reload_block(false, 249, error));
    CHECK(!riftwii::di::ios_reload_block_active());
    CHECK(opens == 5 && calls == 5 && closes == 5);
    expected_mode = 2; expected_slot = 249;
    ioctl_reply = -1017;
    CHECK(!riftwii::di::set_ios_reload_block(true, 249, error));
    CHECK(error.find("-1017") != std::string::npos);
    CHECK(!riftwii::di::ios_reload_block_active());
    CHECK(opens == 6 && calls == 6 && closes == 6);
    open_reply = -6;
    CHECK(!riftwii::di::set_ios_reload_block(true, 249, error));
    CHECK(error.find("-6") != std::string::npos);
    CHECK(opens == 7 && calls == 6 && closes == 6);
    open_reply = 42; ioctl_reply = 0;
    CHECK(riftwii::di::set_ios_reload_block(true, 249, error));
    expected_mode = 0; ioctl_reply = -1017;
    CHECK(!riftwii::di::set_ios_reload_block(false, 249, error));
    CHECK(riftwii::di::ios_reload_block_active());  // failed cleanup must remain visible
    ioctl_reply = 0;
    CHECK(riftwii::di::set_ios_reload_block(false, 249, error));
    CHECK(!riftwii::di::ios_reload_block_active());
    CHECK(opens == 10 && calls == 9 && closes == 9);
    test_stop_motor();
    test_power_off_line();
    test_stop_motor_timeout();
    if (failures) return 1;
    std::cout << "d2x ES reload-block protocol, alignment, bounds, error cleanup and the stop-motor request passed\n";
}
