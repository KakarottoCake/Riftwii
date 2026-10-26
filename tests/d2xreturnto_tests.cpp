// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The actual return-to client against IOS transport with d2x's title guard.
#include "d2xreturnto.hpp"

#include <gccore.h>

#include <cstdarg>
#include <cstdio>
#include <cstdint>
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

constexpr std::uint64_t kChannel = 0x0001000155465457ull;
int ios = 249;
int base = 56;
bool title_running = false;
s32 open_result = 42;
s32 request_result = 0;
unsigned opens = 0;
unsigned requests = 0;
unsigned closes = 0;
std::uint64_t accepted_title = 0;
std::vector<std::string> logs;

bool aligned(const void* pointer) {
    return (reinterpret_cast<std::uintptr_t>(pointer) & 31) == 0;
}

}  // namespace

s32 IOS_GetVersion() {
    return ios;
}

s32 IOS_Open(const char* path, u32 mode) {
    CHECK(std::string(path) == "/dev/es");
    CHECK(mode == 0);
    ++opens;
    return open_result;
}

s32 IOS_Close(s32 descriptor) {
    CHECK(descriptor == 42);
    ++closes;
    return 0;
}

s32 IOS_Ioctlv(s32 descriptor, s32 command, u32 inputs, u32 outputs, ioctlv* vectors) {
    CHECK(descriptor == 42);
    CHECK(command == 0xA1);
    CHECK(inputs == 1 && outputs == 0);
    CHECK(aligned(vectors));
    CHECK(aligned(vectors[0].data));
    CHECK(vectors[0].len == sizeof(std::uint64_t));
    ++requests;
    if (title_running) return -6;
    if (request_result < 0) return request_result;
    accepted_title = *static_cast<const std::uint64_t*>(vectors[0].data);
    return request_result;
}

namespace riftwii::wii {

int d2x_base(int slot) {
    CHECK(slot == ios);
    return base;
}

void logf(const char* format, ...) {
    char text[256];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    logs.emplace_back(text);
}

}  // namespace riftwii::wii

int main() {
    using namespace riftwii::wii;

    prepare_d2x_return_to(0);
    ios = 58;
    prepare_d2x_return_to(kChannel);
    ios = 249;
    base = 0;
    prepare_d2x_return_to(kChannel);
    CHECK(opens == 0 && requests == 0 && logs.empty());

    base = 56;
    prepare_d2x_return_to(kChannel);
    CHECK(accepted_title == kChannel);
    CHECK(requests == 1 && closes == 1);
    CHECK(logs.size() == 1 && logs.back().find("accepted") != std::string::npos);

    // The probe identifies a title. Preparing again before handoff must
    // retain the accepted setting rather than send a refused late request.
    title_running = true;
    prepare_d2x_return_to(kChannel);
    CHECK(requests == 1 && logs.size() == 1);

    // A real reload (including the same slot) loses both ES title context
    // and the setting. Preparation must send the request again.
    forget_d2x_return_to();
    title_running = false;
    accepted_title = 0;
    prepare_d2x_return_to(kChannel);
    CHECK(requests == 2 && accepted_title == kChannel);

    // A changed selection can clear an earlier setting before a title runs.
    prepare_d2x_return_to(0);
    CHECK(requests == 3 && accepted_title == 0);

    title_running = true;
    prepare_d2x_return_to(kChannel);
    CHECK(logs.back().find("refused") != std::string::npos);
    title_running = false;
    request_result = -1017;
    prepare_d2x_return_to(kChannel);
    CHECK(requests == 5 && accepted_title == 0);
    request_result = 0;
    open_result = -6;
    prepare_d2x_return_to(kChannel);
    CHECK(requests == 5 && opens == 6 && closes == 5);
    CHECK(logs.back().find("(-6)") != std::string::npos);
    open_result = 42;
    prepare_d2x_return_to(kChannel);
    CHECK(requests == 6 && accepted_title == kChannel);

    return failures == 0 ? 0 : 1;
}
