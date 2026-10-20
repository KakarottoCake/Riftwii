// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// A Wii game's own restart keeping the launch's changes
// (riftwii/resetkeep.hpp, runtime/rtreset.h): the restore list the loader
// builds, the runtime writing it back, OSRestart as the test switch finds
// it and the switch's stub. The code here is made up instruction by
// instruction in the shape the SDK's function has.
#include "riftwii/resetkeep.hpp"
#include "rtreset.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

namespace {

using Bytes = std::vector<std::uint8_t>;
using riftwii::ResetRecord;

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

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

void differences_become_records() {
    constexpr std::uint32_t kAt = 0x80004000;
    Bytes before(64 * 4, 0x11);
    Bytes after = before;
    // A hook (four words), two words apart from a one-word patch: one
    // record. A lone word far off: a second record.
    for (int w = 4; w < 8; ++w) put_be32(after.data() + 4 * w, 0x48000000u + w);
    put_be32(after.data() + 4 * 10, 0x38600000);
    put_be32(after.data() + 4 * 40, 0x4E800020);
    std::vector<ResetRecord> records;
    riftwii::add_reset_differences(kAt, before.data(), after.data(), static_cast<std::uint32_t>(after.size()), records);
    EXPECT_EQ(records.size(), 2u);
    if (records.size() == 2) {
        EXPECT_EQ(records[0].address, kAt + 16);
        EXPECT_EQ(records[0].bytes.size(), 7u * 4);  // words 4..10, the two between carried
        EXPECT_EQ(be32(records[0].bytes.data() + 6 * 4), 0x38600000u);
        EXPECT_EQ(records[1].address, kAt + 160);
        EXPECT_EQ(records[1].bytes.size(), 4u);
    }
    // Nothing changed: no record.
    records.clear();
    riftwii::add_reset_differences(kAt, before.data(), before.data(), static_cast<std::uint32_t>(before.size()), records);
    EXPECT_TRUE(records.empty());
    // A change in the last word.
    after = before;
    put_be32(after.data() + after.size() - 4, 1);
    riftwii::add_reset_differences(kAt, before.data(), after.data(), static_cast<std::uint32_t>(after.size()), records);
    EXPECT_EQ(records.size(), 1u);
    if (!records.empty()) EXPECT_EQ(records[0].address, kAt + 63 * 4);
}

void ranges_are_whole_words() {
    Bytes memory(32);
    for (std::size_t i = 0; i < memory.size(); ++i) memory[i] = static_cast<std::uint8_t>(i);
    std::vector<ResetRecord> records;
    // A seven-byte patch at 0x80001802 (as Newer's save name is): the
    // record runs from 0x80001800 to 0x8000180C.
    riftwii::add_reset_range(0x80001802, memory.data() + 2, 7, records);
    EXPECT_EQ(records.size(), 1u);
    if (!records.empty()) {
        EXPECT_EQ(records[0].address, 0x80001800u);
        EXPECT_EQ(records[0].bytes.size(), 12u);
        EXPECT_EQ(records[0].bytes[0], 0u);
        EXPECT_EQ(records[0].bytes[11], 11u);
    }
    riftwii::add_reset_range(0x80001800, memory.data(), 0, records);
    EXPECT_EQ(records.size(), 1u);
}

void the_runtime_writes_the_list_back() {
    // Memory "at" 0x80004000, a buffer on the host.
    constexpr std::uint32_t kAt = 0x80004000;
    Bytes pristine(256, 0);
    for (std::size_t i = 0; i < pristine.size(); ++i) pristine[i] = static_cast<std::uint8_t>(i * 7);
    Bytes launched = pristine;
    put_be32(launched.data() + 0x20, 0x4BFFFFF0);
    put_be32(launched.data() + 0x24, 0x60000000);
    put_be32(launched.data() + 0xF0, 0x80001800);
    std::vector<ResetRecord> records;
    riftwii::add_reset_differences(kAt, pristine.data(), launched.data(), static_cast<std::uint32_t>(launched.size()), records);
    const Bytes list = riftwii::serialize_reset_list(records);
    EXPECT_EQ(list.size(), static_cast<std::size_t>(riftwii::reset_list_bytes(records)));
    EXPECT_EQ(be32(list.data()), RTRST_MAGIC);
    EXPECT_EQ(be32(list.data() + 4), static_cast<std::uint32_t>(records.size()));

    // The restart loaded the disc's bytes again; the list makes them the
    // launch's.
    Bytes memory = pristine;
    const std::uintptr_t bias = reinterpret_cast<std::uintptr_t>(memory.data()) - kAt;
    EXPECT_EQ(rtrst_apply(list.data(), bias), static_cast<std::int32_t>(records.size()));
    EXPECT_TRUE(memory == launched);

    // Not a list: nothing written.
    Bytes bad = list;
    bad[0] ^= 1;
    memory = pristine;
    EXPECT_EQ(rtrst_apply(bad.data(), bias), -1);
    EXPECT_TRUE(memory == pristine);
    EXPECT_EQ(rtrst_apply(nullptr, bias), -1);
    Bytes too_many = list;
    put_be32(too_many.data() + 4, RTRST_MAX_RECORDS + 1);
    EXPECT_EQ(rtrst_apply(too_many.data(), bias), -1);
    EXPECT_TRUE(memory == pristine);

    // An empty list is one.
    const Bytes empty = riftwii::serialize_reset_list({});
    EXPECT_EQ(empty.size(), 8u);
    EXPECT_EQ(rtrst_apply(empty.data(), bias), 0);
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

    // Two of them: neither is taken.
    std::vector<std::uint32_t> twice = os_restart_shape();
    const std::vector<std::uint32_t> again = os_restart_shape();
    twice.insert(twice.end(), again.begin(), again.end());
    const Bytes doubled = words_to_bytes(twice);
    EXPECT_EQ(riftwii::find_os_restart(one_range(doubled)), 0u);
}

void the_trigger_stub() {
    constexpr std::uint32_t kStub = 0x80001200;
    constexpr std::uint32_t kPadRead = 0x801B0000;
    constexpr std::uint32_t kOsRestart = 0x801A8004;
    const std::vector<std::uint32_t> w =
        riftwii::restart_trigger_stub(kStub, kPadRead, 0x9421FFE0 /* stwu r1,-32(r1) */, 1800, kOsRestart);
    EXPECT_EQ(w.size(), static_cast<std::size_t>(riftwii::kRestartTriggerBytes / 4));
    if (w.size() == riftwii::kRestartTriggerBytes / 4) {
        EXPECT_EQ(w[0], 0x3D808000u);  // lis r12,0x8000
        EXPECT_EQ(w[1], 0x618C1200u);  // ori r12,r12,0x1200
        EXPECT_EQ(w[12], 0x9421FFE0u);  // PADRead's own first word
        // b from 0x80001234 back to PADRead + 4.
        EXPECT_EQ(w[13], 0x48000000u | ((kPadRead + 4 - (kStub + 0x34)) & 0x03FFFFFCu));
        EXPECT_EQ(w[riftwii::kRestartTriggerCountOffset / 4], 0u);
        EXPECT_EQ(w[riftwii::kRestartTriggerCountOffset / 4 + 1], 1800u);
        EXPECT_EQ(w[riftwii::kRestartTriggerCountOffset / 4 + 2], kOsRestart);
    }
    // Another blob's hook first: its branch, moved to the stub.
    const std::uint32_t blob = 0x81700000;
    const std::uint32_t hook = 0x48000000u | ((blob - kPadRead) & 0x03FFFFFCu);
    const std::vector<std::uint32_t> chained = riftwii::restart_trigger_stub(kStub, kPadRead, hook, 10, kOsRestart);
    EXPECT_EQ(chained.size(), static_cast<std::size_t>(riftwii::kRestartTriggerBytes / 4));
    if (chained.size() > 12) EXPECT_EQ(chained[12], 0x48000000u | ((blob - (kStub + 0x30)) & 0x03FFFFFCu));
    // mflr and bl cannot move.
    EXPECT_TRUE(riftwii::restart_trigger_stub(kStub, kPadRead, 0x7C0802A6, 10, kOsRestart).empty());
    EXPECT_TRUE(riftwii::restart_trigger_stub(kStub, kPadRead, 0x48000101, 10, kOsRestart).empty());
}

}  // namespace

int main() {
    differences_become_records();
    ranges_are_whole_words();
    the_runtime_writes_the_list_back();
    os_restart_is_found();
    the_trigger_stub();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "resetkeep: all passed" << std::endl;
    return 0;
}
