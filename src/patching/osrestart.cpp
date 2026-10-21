// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/osrestart.hpp"

namespace riftwii {
namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

constexpr std::uint32_t kStwuR1Mask = 0xFFFF0000u;
constexpr std::uint32_t kStwuR1 = 0x94210000u;          // stwu r1,d(r1)
constexpr std::uint32_t kClrlwiR0Mask = 0xFC1FFFFFu;
constexpr std::uint32_t kClrlwiR0To24 = 0x5400063Eu;    // rlwinm r0,rN,0,24,31
constexpr std::uint32_t kCmplwiR0Channel = 0x28000081u; // cmplwi r0,0x81
constexpr std::uint32_t kCmplwiR0Disc = 0x28000080u;    // cmplwi r0,0x80
constexpr std::size_t kDiscCompareWithin = 12;          // words after the channel's compare
constexpr std::size_t kPrologueWithin = 32;             // words back to the function's stwu

bool is_conditional_branch(std::uint32_t insn) { return (insn >> 26) == 16; }

// The function holding word `i` of `r`: its `stwu r1` frame, found going
// back, or the first word after the `blr` that ends the function before
// it, past the padding between functions (zeros and nops: New Super Mario
// Bros. Wii has one before OSRestart, and a call into it hung the game).
// The second is how a function whose first word is already hooked (a
// branch to the runtime, as OSRestart's is once the Reset is taken) is
// found again. 0 when neither is close enough.
std::uint32_t function_start(const CodeRange& r, std::size_t i) {
    constexpr std::uint32_t kBlr = 0x4E800020u;
    constexpr std::uint32_t kNop = 0x60000000u;
    for (std::size_t back = 1; back <= kPrologueWithin && back <= i; ++back) {
        const std::uint32_t insn = be32(r.bytes + 4 * (i - back));
        if ((insn & kStwuR1Mask) == kStwuR1) return r.address + static_cast<std::uint32_t>(4 * (i - back));
        if (insn != kBlr) continue;
        std::size_t first = i - back + 1;
        while (first < i && (be32(r.bytes + 4 * first) == 0 || be32(r.bytes + 4 * first) == kNop)) ++first;
        return r.address + static_cast<std::uint32_t>(4 * first);
    }
    return 0;
}

}  // namespace

std::uint32_t find_os_sleep_thread(const std::vector<CodeRange>& text) {
    constexpr std::uint32_t kLiR0Waiting = 0x38000004u;  // li r0,4 (OS_THREAD_STATE_WAITING)
    constexpr std::uint32_t kStateOffset = 0x2C8;        // OSThread.state, past its OSContext
    constexpr std::uint32_t kQueueOffset = 0x2DC;        // OSThread.queue
    std::uint32_t found = 0;
    unsigned matches = 0;
    for (const CodeRange& r : text) {
        if (r.bytes == nullptr) continue;
        const std::size_t words = r.size / 4;
        for (std::size_t i = 4; i + 2 <= words; ++i) {
            const std::uint8_t* at = r.bytes + 4 * i;
            const std::uint32_t state_store = be32(at);
            // sth r0,0x2C8(rT)
            if ((state_store >> 26) != 44 || ((state_store >> 21) & 31u) != 0 || (state_store & 0xFFFFu) != kStateOffset) {
                continue;
            }
            const std::uint32_t thread_reg = (state_store >> 16) & 31u;
            const std::uint32_t queue_store = be32(at + 4);  // stw rQ,0x2DC(rT)
            if ((queue_store >> 26) != 36 || ((queue_store >> 16) & 31u) != thread_reg ||
                (queue_store & 0xFFFFu) != kQueueOffset) {
                continue;
            }
            bool waiting = false;
            for (std::size_t back = 1; back <= 4 && !waiting; ++back) waiting = be32(at - 4 * back) == kLiR0Waiting;
            if (!waiting) continue;
            const std::uint32_t start = function_start(r, i);
            if (start == 0) continue;
            found = start;
            ++matches;
        }
    }
    return matches == 1 ? found : 0;
}

std::uint32_t find_os_restart(const std::vector<CodeRange>& text) {
    std::uint32_t found = 0;
    unsigned matches = 0;
    for (const CodeRange& r : text) {
        if (r.bytes == nullptr) continue;
        const std::size_t words = r.size / 4;
        for (std::size_t i = 0; i + 3 < words; ++i) {
            const std::uint8_t* at = r.bytes + 4 * i;
            if ((be32(at) & kClrlwiR0Mask) != kClrlwiR0To24 || be32(at + 4) != kCmplwiR0Channel ||
                !is_conditional_branch(be32(at + 8))) {
                continue;
            }
            bool disc_compare = false;
            for (std::size_t j = i + 3; j < words && j < i + 2 + kDiscCompareWithin && !disc_compare; ++j) {
                disc_compare = be32(r.bytes + 4 * j) == kCmplwiR0Disc;
            }
            if (!disc_compare) continue;
            const std::uint32_t start = function_start(r, i);
            if (start == 0) continue;
            found = start;
            ++matches;
        }
    }
    return matches == 1 ? found : 0;
}

std::vector<std::uint32_t> restart_trigger_stub(std::uint32_t stub_address, std::uint32_t sleep_thread,
                                                std::uint32_t displaced, std::uint32_t ticks,
                                                std::uint32_t os_restart) {
    const std::uint32_t opcode = displaced >> 26;
    // b/bl and bc are relative to where they sit, and mflr would see the
    // stub's link: none of them can run from the stub.
    if (opcode == 18 || opcode == 16 || (displaced & 0xFC1FFFFFu) == 0x7C0802A6u) return {};
    constexpr std::uint32_t kReplay = 0x74;
    const std::uint32_t resume = sleep_thread + 4;
    const std::int64_t delta = std::int64_t(resume) - std::int64_t(stub_address + kReplay + 4);
    if (delta < -0x2000000 || delta >= 0x2000000) return {};
    constexpr std::uint32_t d = kRestartTriggerDataOffset;
    std::vector<std::uint32_t> w(kRestartTriggerBytes / 4, 0);
    w[0x00 / 4] = 0x3D800000u | (stub_address >> 16);     // lis r12,stub@h
    w[0x04 / 4] = 0x618C0000u | (stub_address & 0xFFFFu); // ori r12,r12,stub@l
    w[0x08 / 4] = 0x3D608000u;                            // lis r11,0x8000
    w[0x0C / 4] = 0x814B00E4u;                            // lwz r10,0xE4(r11): the running thread
    w[0x10 / 4] = 0x816B00D8u;                            // lwz r11,0xD8(r11): the default (main) thread
    w[0x14 / 4] = 0x7C0A5840u;                            // cmplw r10,r11
    w[0x18 / 4] = 0x4082005Cu;                            // bne the replay
    w[0x1C / 4] = 0x7D6C42E6u;                            // mftb r11
    w[0x20 / 4] = 0x814C0000u | d;                        // lwz r10,start(r12)
    w[0x24 / 4] = 0x2C0A0000u;                            // cmpwi r10,0
    w[0x28 / 4] = 0x4082000Cu;                            // bne +0xC
    w[0x2C / 4] = 0x916C0000u | d;                        // stw r11,start(r12): the first sleep
    w[0x30 / 4] = 0x48000044u;                            // b the replay
    w[0x34 / 4] = 0x7D4A5850u;                            // subf r10,r10,r11: ticks since
    w[0x38 / 4] = 0x816C0000u | (d + 4);                  // lwz r11,ticks(r12)
    w[0x3C / 4] = 0x7C0A5840u;                            // cmplw r10,r11
    w[0x40 / 4] = 0x41800034u;                            // blt the replay
    w[0x44 / 4] = 0x814C0000u | (d + 12);                 // lwz r10,fired(r12)
    w[0x48 / 4] = 0x2C0A0000u;                            // cmpwi r10,0
    w[0x4C / 4] = 0x40820028u;                            // bne the replay: once only
    w[0x50 / 4] = 0x39400001u;                            // li r10,1
    w[0x54 / 4] = 0x914C0000u | (d + 12);                 // stw r10,fired(r12)
    w[0x58 / 4] = 0x7D6000A6u;                            // mfmsr r11
    w[0x5C / 4] = 0x616B8000u;                            // ori r11,r11,0x8000: interrupts on, as
    w[0x60 / 4] = 0x7D600124u;                            // mtmsr r11  the HOME Menu's Reset has them
    w[0x64 / 4] = 0x818C0000u | (d + 8);                  // lwz r12,OSRestart(r12)
    w[0x68 / 4] = 0x7D8903A6u;                            // mtctr r12
    w[0x6C / 4] = 0x38600000u;                            // li r3,0
    w[0x70 / 4] = 0x4E800420u;                            // bctr
    w[kReplay / 4] = displaced;                           // OSSleepThread's own first word
    w[kReplay / 4 + 1] = 0x48000000u | (static_cast<std::uint32_t>(delta) & 0x03FFFFFCu);  // b OSSleepThread+4
    w[d / 4] = 0;
    w[d / 4 + 1] = ticks;
    w[d / 4 + 2] = os_restart;
    w[d / 4 + 3] = 0;
    return w;
}

}  // namespace riftwii
