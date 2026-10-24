// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The way back from a Wii game by a GameCube controller (runtime/rtleave.h,
// #224): the combo, what counts as writing a save, and when it leaves.
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

extern "C" {
#include "rtleave.h"
}

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

namespace {

// Four PADStatus records: no pad anywhere (err -1), unless set.
struct Pads {
    std::uint8_t bytes[RTLEAVE_PADS * RTLEAVE_STATUS_BYTES];
    Pads() {
        std::memset(bytes, 0, sizeof(bytes));
        for (unsigned i = 0; i < RTLEAVE_PADS; ++i) bytes[i * RTLEAVE_STATUS_BYTES + 10] = 0xFF;
    }
    void pad(unsigned port, std::uint16_t buttons) {
        std::uint8_t* s = bytes + port * RTLEAVE_STATUS_BYTES;
        s[0] = static_cast<std::uint8_t>(buttons >> 8);
        s[1] = static_cast<std::uint8_t>(buttons);
        s[10] = 0;
    }
};

constexpr std::uint16_t kStart = 0x1000, kL = 0x0040, kR = 0x0020, kZ = 0x0010, kA = 0x0100;

void TestHeld() {
    Pads none;
    EXPECT_FALSE(rtleave_held(none.bytes));
    // The combo on port 3 (others empty), with A held too: held.
    Pads third;
    third.pad(2, kStart | kL | kR | kZ | kA);
    EXPECT_TRUE(rtleave_held(third.bytes));
    // Missing Z: not held.
    Pads partial;
    partial.pad(0, kStart | kL | kR);
    EXPECT_FALSE(rtleave_held(partial.bytes));
    // The buttons in a port that reports no pad (err set): not held.
    Pads stale;
    stale.pad(1, kStart | kL | kR | kZ);
    stale.bytes[1 * RTLEAVE_STATUS_BYTES + 10] = 0xFD;  // PAD_ERR_TRANSFER
    EXPECT_FALSE(rtleave_held(stale.bytes));
}

// An SDK request block: command, result, fd, then the arguments.
std::vector<std::uint32_t> block(std::uint32_t command, std::uint32_t arg0, std::uint32_t arg1, std::uint32_t arg2) {
    return {command, 0, 3, arg0, arg1, arg2, 0, 0};
}

void TestSaveRequests() {
    const char kSave[] = "/title/00010000/53524a4a/data/save.dat";
    const char kHid[] = "/dev/usb/hid";
    const std::uint8_t* save = reinterpret_cast<const std::uint8_t*>(kSave);
    const std::uint8_t* hid = reinterpret_cast<const std::uint8_t*>(kHid);
    // Any write.
    EXPECT_TRUE(rtleave_save_request(block(4, 0x00123400, 0x200, 0).data(), nullptr, nullptr));
    // Opens: a NAND file for writing (2) or reading and writing (3)
    // counts; for reading, or a device, does not.
    EXPECT_TRUE(rtleave_save_request(block(1, 0x00100000, 2, 0).data(), save, nullptr));
    EXPECT_TRUE(rtleave_save_request(block(1, 0x00100000, 3, 0).data(), save, nullptr));
    EXPECT_FALSE(rtleave_save_request(block(1, 0x00100000, 1, 0).data(), save, nullptr));
    EXPECT_FALSE(rtleave_save_request(block(1, 0x00100000, 3, 0).data(), hid, nullptr));
    EXPECT_FALSE(rtleave_save_request(block(1, 0x00100000, 3, 0).data(), nullptr, nullptr));
    // /dev/fs changes: Delete (7) and Rename (8) with the path first.
    EXPECT_TRUE(rtleave_save_request(block(6, 7, 0x00100000, 64).data(), nullptr, save));
    EXPECT_TRUE(rtleave_save_request(block(6, 8, 0x00100000, 128).data(), nullptr, save));
    // CreateFile (9): owner (4 bytes) and group (2) before the path.
    std::vector<std::uint8_t> create(0x4A, 0);
    create[0] = 0x00;
    create[3] = 0x10;
    create[4] = 0x31;
    create[5] = 0x01;
    std::memcpy(&create[6], kSave, sizeof(kSave));
    EXPECT_TRUE(rtleave_save_request(block(6, 9, 0x00100000, 0x4A).data(), nullptr, create.data()));
    // Too short to hold a path after the owner: not a save.
    EXPECT_FALSE(rtleave_save_request(block(6, 9, 0x00100000, 6).data(), nullptr, create.data()));
    // The same numbers on other devices (the disc's 0x71, a HID ioctl's
    // input that is no path), no input, or ioctls that read: not a save.
    const std::uint8_t di_read[32] = {0x71};
    EXPECT_FALSE(rtleave_save_request(block(6, 0x71, 0x00100000, 32).data(), nullptr, di_read));
    EXPECT_FALSE(rtleave_save_request(block(6, 7, 0x00100000, 32).data(), nullptr, di_read));
    EXPECT_FALSE(rtleave_save_request(block(6, 7, 0, 64).data(), nullptr, nullptr));
    EXPECT_FALSE(rtleave_save_request(block(6, 7, 0x00100000, 0).data(), nullptr, save));
    EXPECT_FALSE(rtleave_save_request(block(6, 6, 0x00100000, 64).data(), nullptr, save));  // GetAttr: reads
    // Reads, closes, seeks and ioctlv (the Wii Remotes, the adapter): not a save.
    for (std::uint32_t command : {2u, 3u, 5u, 7u}) {
        EXPECT_FALSE(rtleave_save_request(block(command, 0x00100000, 2, 64).data(), save, save));
    }
}

void TestDue() {
    rtleave l{};
    l.hold = 100;
    l.quiet = 50;
    // The hold starts, then counts to `hold`.
    EXPECT_FALSE(rtleave_due(&l, 1, 10, 0, 0));
    EXPECT_FALSE(rtleave_due(&l, 1, 60, 0, 0));
    EXPECT_TRUE(rtleave_due(&l, 1, 111, 0, 0));
    // Letting go starts it over.
    EXPECT_FALSE(rtleave_due(&l, 0, 120, 0, 0));
    EXPECT_EQ(l.since, 0u);
    EXPECT_FALSE(rtleave_due(&l, 1, 130, 0, 0));
    EXPECT_FALSE(rtleave_due(&l, 1, 200, 0, 0));
    EXPECT_TRUE(rtleave_due(&l, 1, 231, 0, 0));

    // A save while it is held: the hold runs out but the save is recent,
    // so it waits; still held once the save has been quiet: it goes.
    rtleave s{};
    s.hold = 100;
    s.quiet = 50;
    EXPECT_FALSE(rtleave_due(&s, 1, 1000, 0, 0));
    rtleave_note_save(&s, 1090);
    EXPECT_FALSE(rtleave_due(&s, 1, 1101, 0, 0));
    EXPECT_FALSE(rtleave_due(&s, 1, 1130, 0, 0));
    EXPECT_TRUE(rtleave_due(&s, 1, 1142, 0, 0));
    // Let go while the save held it back: nothing, and a new hold is needed.
    rtleave t{};
    t.hold = 100;
    t.quiet = 50;
    EXPECT_FALSE(rtleave_due(&t, 1, 2000, 0, 0));
    rtleave_note_save(&t, 2095);
    EXPECT_FALSE(rtleave_due(&t, 1, 2101, 0, 0));
    EXPECT_FALSE(rtleave_due(&t, 0, 2120, 0, 0));
    EXPECT_FALSE(rtleave_due(&t, 1, 2200, 0, 0));
    EXPECT_FALSE(rtleave_due(&t, 1, 2250, 0, 0));
    EXPECT_TRUE(rtleave_due(&t, 1, 2301, 0, 0));

    // Saves on the SD card: the resident's last write counts the same,
    // once it has written at all.
    rtleave r{};
    r.hold = 100;
    r.quiet = 50;
    EXPECT_FALSE(rtleave_due(&r, 1, 3000, 0, 0));
    EXPECT_FALSE(rtleave_due(&r, 1, 3101, 1, 3080));
    EXPECT_TRUE(rtleave_due(&r, 1, 3131, 1, 3080));
    EXPECT_TRUE(rtleave_due(&r, 1, 3131, 0, 3120));  // no write yet: last_write means nothing
    // A write's command still with IOS: never quiet, however long since it
    // went out (a stuck command on a failing card); once it is back, the
    // usual quiet second after its last step.
    EXPECT_FALSE(rtleave_due(&r, 1, 3131, RTLEAVE_RESIDENT_WITH_IOS, 3080));
    EXPECT_FALSE(rtleave_quiet(&r, 900000, RTLEAVE_RESIDENT_WITH_IOS, 3080));
    EXPECT_TRUE(rtleave_quiet(&r, 900000, 1, 3080));
    EXPECT_FALSE(rtleave_quiet(&r, 900000, 1, 899990));

    // The time base's low word wraps every 70 seconds or so.
    rtleave w{};
    w.hold = 100;
    w.quiet = 50;
    EXPECT_FALSE(rtleave_due(&w, 1, 0xFFFFFFC0u, 0, 0));
    EXPECT_FALSE(rtleave_due(&w, 1, 0x00000010u, 0, 0));
    EXPECT_TRUE(rtleave_due(&w, 1, 0x00000030u, 0, 0));
}

// The check at the main thread's sleep, after the ask (CodeRabbit on the
// fork's test PR): a save between the PADRead that asked and that sleep
// is seen, so the way back waits for it.
void TestQuietAtLeave() {
    rtleave l{};
    l.hold = 100;
    l.quiet = 50;
    EXPECT_FALSE(rtleave_due(&l, 1, 4000, 0, 0));
    EXPECT_TRUE(rtleave_due(&l, 1, 4101, 0, 0));  // asked at this PADRead
    EXPECT_TRUE(rtleave_quiet(&l, 4102, 0, 0));    // nothing since: it goes
    rtleave_note_save(&l, 4103);                   // a save begins before the sleep
    EXPECT_FALSE(rtleave_quiet(&l, 4104, 0, 0));   // the sleep takes the ask back
    EXPECT_FALSE(rtleave_due(&l, 1, 4120, 0, 0));  // still held: waits for quiet
    EXPECT_TRUE(rtleave_due(&l, 1, 4154, 0, 0));   // quiet again: asks again
    EXPECT_TRUE(rtleave_quiet(&l, 4155, 0, 0));
    // The resident's SD save write counts the same at the sleep.
    EXPECT_FALSE(rtleave_quiet(&l, 4160, 1, 4158));
    EXPECT_TRUE(rtleave_quiet(&l, 4209, 1, 4158));
}

}  // namespace

int main() {
    TestHeld();
    TestSaveRequests();
    TestDue();
    TestQuietAtLeave();
    if (g_failures == 0) {
        std::cout << "ALL PAD LEAVE TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
