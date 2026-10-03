// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the actual Wii DI client against a mocked IOS transport.
#include "di.hpp"
#include <gccore.h>
#include <iostream>
#include <string>

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
s32 IOS_Close(s32 fd) { ++closes; CHECK(fd == 42); return 0; }
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
    if (failures) return 1;
    std::cout << "d2x ES reload-block protocol, alignment, bounds and error cleanup passed\n";
}
