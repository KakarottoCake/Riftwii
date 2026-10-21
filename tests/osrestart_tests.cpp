// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// A Wii game's own restart (riftwii/osrestart.hpp): OSRestart and
// OSSleepThread as the runtime and the test switch find them, and the
// switch's stub. The code here is made up instruction by instruction in
// the shape the SDK's function has.
#include "riftwii/osrestart.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

namespace {

using Bytes = std::vector<std::uint8_t>;

void put_be32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

Bytes words_to_bytes(const std::vector<std::uint32_t>& words) {
    Bytes out(words.size() * 4);
    for (std::size_t i = 0; i < words.size(); ++i) put_be32(out.data() + 4 * i, words[i]);
    return out;
}

constexpr std::uint32_t kBase = 0x801A8000;
constexpr std::uint32_t kNop = 0x60000000;

// OSRestart's shape: its frame, the application type into r0, the
// channel's compare and branch, the channel's calls, the disc's compare.
std::vector<std::uint32_t> os_restart_shape() {
    return {
        0x4E800020,  // the function before ends
        0x9421FFF0,  // stwu r1,-16(r1)
        0x7C0802A6,  // mflr r0
        0x90010014,  // stw r0,20(r1)
        0x7C7E1B78,  // mr r30,r3
        0x48000001,  // bl (the application type)
        0x7C7F1B78,  // mr r31,r3
        0x57E0063E,  // clrlwi r0,r31,24
        0x28000081,  // cmplwi r0,0x81
        0x4082001C,  // bne
        kNop, kNop, kNop,
        0x28000080,  // cmplwi r0,0x80
        0x40820020,  // bne
        0x7FC3F378,  // mr r3,r30
        0x48000001,  // bl (__OSReboot)
        0x4E800020,
    };
}

std::vector<riftwii::CodeRange> one_range(const Bytes& bytes, std::uint32_t at = kBase) {
    return {riftwii::CodeRange{at, bytes.data(), bytes.size()}};
}

void os_restart_is_found() {
    const Bytes text = words_to_bytes(os_restart_shape());
    EXPECT_EQ(riftwii::find_os_restart(one_range(text)), kBase + 4);

    // The disc's compare missing: not OSRestart.
    std::vector<std::uint32_t> words = os_restart_shape();
    words[13] = kNop;
    const Bytes no_disc = words_to_bytes(words);
    EXPECT_EQ(riftwii::find_os_restart(one_range(no_disc)), 0u);

    // Hooked for the Reset (its first word a branch to the runtime),
    // as the test switch then sees it: still found, at the same address.
    std::vector<std::uint32_t> hooked = os_restart_shape();
    hooked[1] = 0x48000000u | ((0x817E37E0u - (kBase + 4)) & 0x03FFFFFCu);
    const Bytes hooked_text = words_to_bytes(hooked);
    EXPECT_EQ(riftwii::find_os_restart(one_range(hooked_text)), kBase + 4);
    // With padding between the function before and the hooked first word
    // (New Super Mario Bros. Wii's OSRestart): the padding is not the start.
    std::vector<std::uint32_t> padded = os_restart_shape();
    padded[1] = 0x48000000u | ((0x817E37E0u - (kBase + 8)) & 0x03FFFFFCu);
    padded.insert(padded.begin() + 1, 0x00000000u);
    const Bytes padded_text = words_to_bytes(padded);
    EXPECT_EQ(riftwii::find_os_restart(one_range(padded_text)), kBase + 8);
    // Not hooked, the frame wins over the padding.
    std::vector<std::uint32_t> padded_plain = os_restart_shape();
    padded_plain.insert(padded_plain.begin() + 1, 0x60000000u);
    const Bytes padded_plain_text = words_to_bytes(padded_plain);
    EXPECT_EQ(riftwii::find_os_restart(one_range(padded_plain_text)), kBase + 8);

    // Two of them: neither is taken.
    std::vector<std::uint32_t> twice = os_restart_shape();
    const std::vector<std::uint32_t> again = os_restart_shape();
    twice.insert(twice.end(), again.begin(), again.end());
    const Bytes doubled = words_to_bytes(twice);
    EXPECT_EQ(riftwii::find_os_restart(one_range(doubled)), 0u);
}

// OSSleepThread's shape: its frame, interrupts off, the running thread
// marked waiting on the queue, then its place in the queue.
std::vector<std::uint32_t> sleep_thread_shape() {
    return {
        0x4E800020,  // the function before ends
        0x9421FFF0,  // stwu r1,-16(r1)
        0x7C0802A6,  // mflr r0
        0x90010014,  // stw r0,20(r1)
        0x7C7E1B78,  // mr r30,r3
        0x48000001,  // bl (interrupts off)
        0x3C808000,  // lis r4,0x8000
        0x38000004,  // li r0,4
        0x808400E4,  // lwz r4,0xE4(r4): the running thread
        0xB00402C8,  // sth r0,0x2C8(r4): its state
        0x93C402DC,  // stw r30,0x2DC(r4): its queue
        0x80BE0000,  // lwz r5,0(r30)
        0x4E800020,
    };
}

void os_sleep_thread_is_found() {
    const Bytes text = words_to_bytes(sleep_thread_shape());
    EXPECT_EQ(riftwii::find_os_sleep_thread(one_range(text)), kBase + 4);
    // Another state than waiting: not it.
    std::vector<std::uint32_t> words = sleep_thread_shape();
    words[7] = 0x38000002;
    const Bytes other_state = words_to_bytes(words);
    EXPECT_EQ(riftwii::find_os_sleep_thread(one_range(other_state)), 0u);
    // The queue stored in another thread's record: not it either.
    words = sleep_thread_shape();
    words[10] = 0x93C502DC;
    const Bytes other_thread = words_to_bytes(words);
    EXPECT_EQ(riftwii::find_os_sleep_thread(one_range(other_thread)), 0u);
}

void the_trigger_stub() {
    constexpr std::uint32_t kStub = 0x80001200;
    constexpr std::uint32_t kSleep = 0x801AA9B8;
    constexpr std::uint32_t kOsRestart = 0x801A8688;
    constexpr std::uint32_t kTicks = 30 * riftwii::kTimeBaseTicksPerSecond;
    const std::vector<std::uint32_t> w =
        riftwii::restart_trigger_stub(kStub, kSleep, 0x9421FFF0 /* stwu r1,-16(r1) */, kTicks, kOsRestart);
    EXPECT_EQ(w.size(), static_cast<std::size_t>(riftwii::kRestartTriggerBytes / 4));
    if (w.size() == riftwii::kRestartTriggerBytes / 4) {
        EXPECT_EQ(w[0], 0x3D808000u);  // lis r12,0x8000
        EXPECT_EQ(w[1], 0x618C1200u);  // ori r12,r12,0x1200
        EXPECT_EQ(w[7], 0x7D6C42E6u);  // mftb r11
        EXPECT_EQ(w[0x74 / 4], 0x9421FFF0u);  // OSSleepThread's own first word
        // b from 0x80001278 back to OSSleepThread + 4.
        EXPECT_EQ(w[0x78 / 4], 0x48000000u | ((kSleep + 4 - (kStub + 0x78)) & 0x03FFFFFCu));
        constexpr std::uint32_t d = riftwii::kRestartTriggerDataOffset / 4;
        EXPECT_EQ(w[d], 0u);
        EXPECT_EQ(w[d + 1], kTicks);
        EXPECT_EQ(w[d + 2], kOsRestart);
        EXPECT_EQ(w[d + 3], 0u);
        // Every branch inside the stub lands on the replay at 0x74.
        const std::uint32_t branches[] = {0x18, 0x30, 0x40, 0x4C};
        for (std::uint32_t at : branches) {
            const std::uint32_t insn = w[at / 4];
            const std::uint32_t offset = (insn >> 26) == 18 ? (insn & 0x03FFFFFCu) : (insn & 0xFFFCu);
            EXPECT_EQ(at + offset, 0x74u);
        }
        // The skip over the first sleep's note lands on the elapsed time.
        EXPECT_EQ(0x28u + (w[0x28 / 4] & 0xFFFCu), 0x34u);
    }
    // mflr and bl cannot move.
    EXPECT_TRUE(riftwii::restart_trigger_stub(kStub, kSleep, 0x7C0802A6, kTicks, kOsRestart).empty());
    EXPECT_TRUE(riftwii::restart_trigger_stub(kStub, kSleep, 0x48000101, kTicks, kOsRestart).empty());
}

}  // namespace

int main() {
    os_restart_is_found();
    os_sleep_thread_is_found();
    the_trigger_stub();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "osrestart: all passed" << std::endl;
    return 0;
}
