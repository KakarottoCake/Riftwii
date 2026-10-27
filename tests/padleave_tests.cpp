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
    // The resident's engine with a request in hand or queued (a write not
    // yet out, or waiting for the card): the same, whatever wrote says.
    EXPECT_EQ(rtleave_resident(1, 0, 0), 1);
    EXPECT_EQ(rtleave_resident(0, 0, 0), 0);
    EXPECT_EQ(rtleave_resident(1, 1, 0), RTLEAVE_RESIDENT_WITH_IOS);
    EXPECT_EQ(rtleave_resident(0, 0, 2), RTLEAVE_RESIDENT_WITH_IOS);
    EXPECT_EQ(rtleave_resident(RTLEAVE_RESIDENT_WITH_IOS, 0, 0), RTLEAVE_RESIDENT_WITH_IOS);
    EXPECT_FALSE(rtleave_quiet(&r, 900000, rtleave_resident(1, 1, 0), 3080));

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

// Which sleep leaves. The main thread's always does (every game that
// works today). In N64 Virtual Console games the main thread never sleeps
// and a second thread reads the pads (Cruis'n USA, in Dolphin): that
// thread's sleep leaves once the fallback wait has passed since the ask,
// any other thread's never does.
void TestSleepLeaves() {
    const std::uint32_t kMain = 0x8018df78u, kPadThread = 0x80186048u, kOther = 0x80184af0u;
    const std::uint32_t kAsked = 1000u | 1u, kFallback = 500u;
    // The main thread: at once, whoever read the pads.
    EXPECT_TRUE(rtleave_sleep_leaves(kMain, kMain, kMain, kAsked, 1002, kFallback));
    EXPECT_TRUE(rtleave_sleep_leaves(kMain, kMain, kPadThread, kAsked, 1002, kFallback));
    // The pad-reading thread: only after the wait.
    EXPECT_FALSE(rtleave_sleep_leaves(kPadThread, kMain, kPadThread, kAsked, 1002, kFallback));
    EXPECT_FALSE(rtleave_sleep_leaves(kPadThread, kMain, kPadThread, kAsked, 1499, kFallback));
    EXPECT_TRUE(rtleave_sleep_leaves(kPadThread, kMain, kPadThread, kAsked, 1500, kFallback));
    // Any other thread: never.
    EXPECT_FALSE(rtleave_sleep_leaves(kOther, kMain, kPadThread, kAsked, 9000, kFallback));
    // No ask open, or no thread noted at it (a PADRead from an interrupt
    // with no thread running): only the main thread.
    EXPECT_FALSE(rtleave_sleep_leaves(kPadThread, kMain, kPadThread, 0, 9000, kFallback));
    EXPECT_FALSE(rtleave_sleep_leaves(0, kMain, 0, kAsked, 9000, kFallback));
    // The time base's low word wraps: the wait still counts across it.
    EXPECT_FALSE(rtleave_sleep_leaves(kPadThread, kMain, kPadThread, 0xFFFFFF01u, 0x000000F0u, kFallback));
    EXPECT_TRUE(rtleave_sleep_leaves(kPadThread, kMain, kPadThread, 0xFFFFFF01u, 0x00000200u, kFallback));
    // OSSuspendThread: only the pad-reading thread suspending itself waits
    // there (Cruis'n USA's, every frame); suspending another thread, a
    // third thread's own suspend, or no thread running, is not where it
    // leaves.
    EXPECT_TRUE(rtleave_self_suspend(kPadThread, kPadThread, kPadThread));
    EXPECT_FALSE(rtleave_self_suspend(kOther, kPadThread, kPadThread));
    EXPECT_FALSE(rtleave_self_suspend(kPadThread, kMain, kPadThread));
    EXPECT_FALSE(rtleave_self_suspend(kOther, kOther, kPadThread));
    EXPECT_FALSE(rtleave_self_suspend(kMain, kMain, kPadThread));
    EXPECT_FALSE(rtleave_self_suspend(0, 0, 0));
}

// The game's own save requests while IOS holds them (KakarottoCake and
// CodeRabbit on #314): a single NAND write longer than the quiet window, a
// synchronous save whose thread sleeps in the very OSSleepThread that
// leaves, and several requests in flight at once. Until a request's command
// word changes (IOS's reply) it is never quiet; after, the quiet window
// counts from the reply; a block whose word never changes stops counting
// after RTLEAVE_PENDING_QUIETS windows, and so does a request that found no
// free slot.
namespace {
const std::uint32_t kWrite = 4;  // a block's command word as submitted
const std::uint32_t kReply = 8;  // what IOS writes there when it answers
const std::uint32_t kBlockA = 0x80401000u;
const std::uint32_t kBlockB = 0x80401040u;

rtleave Fresh() {
    rtleave l{};
    l.hold = 100;
    l.quiet = 50;
    return l;
}

// Each slot's command word as the game would read it: `a` for kBlockA's
// slot, `b` for kBlockB's, a reply for anything else.
int Check(rtleave& l, std::uint32_t now, std::uint32_t a, std::uint32_t b = kReply) {
    std::uint32_t words[RTLEAVE_PENDING_SLOTS + 1];
    for (std::uint32_t i = 0; i <= RTLEAVE_PENDING_SLOTS; ++i) {
        const std::uint32_t entry = i < RTLEAVE_PENDING_SLOTS ? l.pending[i] : l.newest;
        const std::uint32_t block = RTLEAVE_PENDING_BLOCK(entry);
        words[i] = block == kBlockA ? a : block == kBlockB ? b : kReply;
    }
    return rtleave_pending(&l, now, words);
}
}  // namespace

void TestPendingSave() {
    // One request: long after its submit, no reply: still with IOS.
    rtleave l = Fresh();
    EXPECT_EQ(Check(l, 5000, kWrite), 0);  // nothing pending yet
    rtleave_note_save(&l, 5000);
    rtleave_note_pending(&l, 5000, kBlockA, kWrite);
    EXPECT_EQ(Check(l, 5300, kWrite), 1);
    EXPECT_FALSE(rtleave_quiet(&l, 5300, 0, 0));
    EXPECT_FALSE(rtleave_due(&l, 1, 5300, 0, 0));
    EXPECT_FALSE(rtleave_due(&l, 1, 5420, 0, 0));  // held long enough, still not
    // The reply: over, and the quiet window counts from here.
    EXPECT_EQ(Check(l, 5430, kReply), 0);
    EXPECT_EQ(l.pending[0], 0u);
    EXPECT_FALSE(rtleave_quiet(&l, 5440, 0, 0));
    EXPECT_TRUE(rtleave_quiet(&l, 5481, 0, 0));
    EXPECT_TRUE(rtleave_due(&l, 1, 5481, 0, 0));

    // Two in flight at once: the second's reply doesn't end the first.
    rtleave t = Fresh();
    rtleave_note_pending(&t, 6000, kBlockA, kWrite);
    rtleave_note_pending(&t, 6010, kBlockB, kWrite);
    EXPECT_EQ(Check(t, 6100, kWrite, kReply), 1);  // B answered, A not
    EXPECT_FALSE(rtleave_quiet(&t, 6200, 0, 0));
    EXPECT_FALSE(rtleave_quiet(&t, 6300, 0, 0));  // a quiet window after B's reply: A still holds it
    EXPECT_EQ(Check(t, 6310, kReply), 0);          // A answered too
    EXPECT_FALSE(rtleave_quiet(&t, 6320, 0, 0));
    EXPECT_TRUE(rtleave_quiet(&t, 6361, 0, 0));

    // A reused block (another command in the word) ends it the same way.
    rtleave r = Fresh();
    rtleave_note_pending(&r, 6000, kBlockA, kWrite);
    EXPECT_EQ(Check(r, 6010, 1u), 0);
    EXPECT_EQ(r.pending[0], 0u);

    // A word that never changes: counted as with IOS for the cap, then not.
    rtleave c = Fresh();
    rtleave_note_pending(&c, 7000, kBlockA, kWrite);
    const std::uint32_t cap = 7000 + RTLEAVE_PENDING_QUIETS * 50;
    EXPECT_EQ(Check(c, cap - 1, kWrite), 1);
    EXPECT_EQ(Check(c, cap, kWrite), 0);
    EXPECT_EQ(c.pending[0], 0u);
    EXPECT_FALSE(rtleave_quiet(&c, cap + 10, 0, 0));  // then a quiet window, as after a reply
    EXPECT_TRUE(rtleave_quiet(&c, cap + 51, 0, 0));

    // More requests than slots (Brawl's first save keeps dozens in flight):
    // the overflow holds until the newest request has its reply, since the
    // file system answers in order, so every earlier one is done too.
    rtleave o = Fresh();
    for (std::uint32_t i = 0; i < RTLEAVE_PENDING_SLOTS; ++i) rtleave_note_pending(&o, 8000, kBlockB + 0x40u * (i + 1), kWrite);
    EXPECT_EQ(o.overflow, 0u);
    rtleave_note_pending(&o, 8000, kBlockA, kWrite);  // no free slot: the newest, untracked
    EXPECT_TRUE(o.overflow != 0u);
    EXPECT_EQ(o.newest, kBlockA | kWrite);
    EXPECT_EQ(Check(o, 8100, kWrite), 1);  // every slot answered, the newest not: still waiting
    EXPECT_FALSE(rtleave_quiet(&o, 8100, 0, 0));
    EXPECT_EQ(Check(o, 8150, kReply), 0);  // the newest answered: the overflow is over
    EXPECT_EQ(o.overflow, 0u);
    EXPECT_FALSE(rtleave_quiet(&o, 8160, 0, 0));
    EXPECT_TRUE(rtleave_quiet(&o, 8201, 0, 0));
    // Its newest never answering: the cap ends it.
    rtleave n = Fresh();
    for (std::uint32_t i = 0; i < RTLEAVE_PENDING_SLOTS; ++i) rtleave_note_pending(&n, 8000, kBlockB + 0x40u * (i + 1), kWrite);
    rtleave_note_pending(&n, 8000, kBlockA, kWrite);
    const std::uint32_t ocap = 8000 + RTLEAVE_PENDING_QUIETS * 50;
    EXPECT_EQ(Check(n, ocap - 1, kWrite), 1);
    EXPECT_EQ(Check(n, ocap, kWrite), 0);
    EXPECT_TRUE(rtleave_quiet(&n, ocap + 51, 0, 0));
    // One that can't be read back (not 32-byte aligned, a command above 7,
    // or no address): counted as overflow, and as a newest that never reads
    // as answered, so only a later one's reply or the cap ends it.
    rtleave u = Fresh();
    rtleave_note_pending(&u, 9000, kBlockA + 4u, kWrite);
    EXPECT_TRUE(u.overflow != 0u && u.pending[0] == 0u && u.newest == 1u);
    EXPECT_EQ(Check(u, 9100, kReply), 1);
    rtleave_note_pending(&u, 9200, kBlockA, kWrite);  // a later, readable one
    EXPECT_EQ(Check(u, 9300, kReply), 0);             // its reply ends both
    rtleave v = Fresh();
    rtleave_note_pending(&v, 9000, kBlockA, 9u);
    EXPECT_TRUE(v.overflow != 0u && v.pending[0] == 0u);
    EXPECT_EQ(Check(v, 9100, kReply), 1);
    rtleave z = Fresh();
    rtleave_note_pending(&z, 9000, 0, kWrite);
    EXPECT_TRUE(z.overflow != 0u);
    EXPECT_EQ(Check(z, 9000 + RTLEAVE_PENDING_QUIETS * 50, kReply), 0);  // the cap

    // Across the time base's low word wrapping.
    rtleave w = Fresh();
    rtleave_note_pending(&w, 0xFFFFFFF0u, kBlockA, kWrite);
    EXPECT_EQ(Check(w, 0x00000100u, kWrite), 1);
    EXPECT_EQ(Check(w, 0x00000110u, kReply), 0);

    // The reply seen at the very tick of the quiet check, an even one: its
    // stamp (low bit set: one tick ahead) must not read as long ago. In
    // Dolphin this left Brawl mid-save 78 ms after a write.
    rtleave s = Fresh();
    rtleave_note_pending(&s, 8000, kBlockA, kWrite);
    EXPECT_EQ(Check(s, 8200, kReply), 0);
    EXPECT_FALSE(rtleave_quiet(&s, 8200, 0, 0));
    EXPECT_FALSE(rtleave_due(&s, 1, 8200, 0, 0));
    // A save noted at the same even tick as the check, the same.
    rtleave q = Fresh();
    rtleave_note_save(&q, 9000);
    EXPECT_FALSE(rtleave_quiet(&q, 9000, 0, 0));
    // And a hold that began at this same even tick has not lasted yet.
    rtleave h = Fresh();
    EXPECT_FALSE(rtleave_due(&h, 1, 9100, 0, 0));
    EXPECT_FALSE(rtleave_due(&h, 1, 9100, 0, 0));
}

}  // namespace

int main() {
    TestHeld();
    TestSaveRequests();
    TestDue();
    TestQuietAtLeave();
    TestSleepLeaves();
    TestPendingSave();
    if (g_failures == 0) {
        std::cout << "ALL PAD LEAVE TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
