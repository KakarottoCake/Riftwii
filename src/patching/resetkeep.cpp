// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/resetkeep.hpp"

#include "rtreset.h"

namespace riftwii {
namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

// Two unchanged words between two changed ones cost what a new record's
// header does: up to that many are carried in the record instead.
constexpr std::uint32_t kMergeGapWords = 2;

constexpr std::uint32_t kStwuR1Mask = 0xFFFF0000u;
constexpr std::uint32_t kStwuR1 = 0x94210000u;          // stwu r1,d(r1)
constexpr std::uint32_t kClrlwiR0Mask = 0xFC1FFFFFu;
constexpr std::uint32_t kClrlwiR0To24 = 0x5400063Eu;    // rlwinm r0,rN,0,24,31
constexpr std::uint32_t kCmplwiR0Channel = 0x28000081u; // cmplwi r0,0x81
constexpr std::uint32_t kCmplwiR0Disc = 0x28000080u;    // cmplwi r0,0x80
constexpr std::size_t kDiscCompareWithin = 12;          // words after the channel's compare
constexpr std::size_t kPrologueWithin = 32;             // words back to the function's stwu

bool is_conditional_branch(std::uint32_t insn) { return (insn >> 26) == 16; }

}  // namespace

void add_reset_differences(std::uint32_t address, const std::uint8_t* before, const std::uint8_t* after,
                           std::uint32_t length, std::vector<ResetRecord>& out) {
    const std::uint32_t words = length / 4;
    std::uint32_t w = 0;
    while (w < words) {
        if (be32(before + 4 * w) == be32(after + 4 * w)) {
            ++w;
            continue;
        }
        // A run of changed words, carried across short unchanged gaps.
        const std::uint32_t first = w;
        std::uint32_t last = w;  // the last changed word of the run
        std::uint32_t probe = w + 1;
        while (probe < words && probe - last <= kMergeGapWords + 1) {
            if (be32(before + 4 * probe) != be32(after + 4 * probe)) last = probe;
            ++probe;
        }
        ResetRecord record;
        record.address = address + 4 * first;
        record.bytes.assign(after + 4 * first, after + 4 * (last + 1));
        out.push_back(std::move(record));
        w = last + 1;
    }
}

void add_reset_range(std::uint32_t address, const std::uint8_t* bytes, std::uint32_t length,
                     std::vector<ResetRecord>& out) {
    if (length == 0) return;
    // Whole words: the bytes around the range as they are now are part of
    // the record, so `bytes` must start at `address` and the caller passes
    // memory, not a patch's value.
    const std::uint32_t start = address & ~3u;
    const std::uint32_t end = (address + length + 3) & ~3u;
    const std::uint8_t* from = bytes - (address - start);
    ResetRecord record;
    record.address = start;
    record.bytes.assign(from, from + (end - start));
    out.push_back(std::move(record));
}

std::uint32_t reset_list_bytes(const std::vector<ResetRecord>& records) {
    std::uint32_t bytes = 8;
    for (const ResetRecord& r : records) bytes += 8 + static_cast<std::uint32_t>(r.bytes.size());
    return bytes;
}

std::vector<std::uint8_t> serialize_reset_list(const std::vector<ResetRecord>& records) {
    std::vector<std::uint8_t> out;
    out.reserve(reset_list_bytes(records));
    put32(out, RTRST_MAGIC);
    put32(out, static_cast<std::uint32_t>(records.size()));
    for (const ResetRecord& r : records) {
        put32(out, r.address);
        put32(out, static_cast<std::uint32_t>(r.bytes.size()));
        out.insert(out.end(), r.bytes.begin(), r.bytes.end());
    }
    return out;
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
            std::uint32_t start = 0;
            for (std::size_t back = 1; back <= kPrologueWithin && back <= i && start == 0; ++back) {
                if ((be32(at - 4 * back) & kStwuR1Mask) == kStwuR1) {
                    start = r.address + static_cast<std::uint32_t>(4 * (i - back));
                }
            }
            if (start == 0) continue;
            found = start;
            ++matches;
        }
    }
    return matches == 1 ? found : 0;
}

std::vector<std::uint32_t> restart_trigger_stub(std::uint32_t stub_address, std::uint32_t pad_read,
                                                std::uint32_t displaced, std::uint32_t calls,
                                                std::uint32_t os_restart) {
    const std::uint32_t opcode = displaced >> 26;
    const std::uint32_t replay_at = stub_address + 0x30;
    // Another blob's hook already on PADRead (the screenshots', the
    // GameCube adapter's) is a plain `b`: the stub goes there instead.
    const bool plain_branch = opcode == 18 && (displaced & 3u) == 0;
    std::uint32_t replay = displaced;
    if (plain_branch) {
        std::int32_t offset = static_cast<std::int32_t>(displaced & 0x03FFFFFCu);
        if (offset & 0x02000000) offset -= 0x04000000;
        const std::int64_t to = std::int64_t(pad_read) + offset - std::int64_t(replay_at);
        if (to < -0x2000000 || to >= 0x2000000) return {};
        replay = 0x48000000u | (static_cast<std::uint32_t>(to) & 0x03FFFFFCu);
    } else if (opcode == 18 || opcode == 16 || (displaced & 0xFC1FFFFFu) == 0x7C0802A6u) {
        // bl and bc are relative to where they sit, and mflr would see
        // the stub's link: they cannot run from the stub.
        return {};
    }
    const std::uint32_t resume = pad_read + 4;
    const std::int64_t delta = std::int64_t(resume) - std::int64_t(replay_at + 4);
    if (delta < -0x2000000 || delta >= 0x2000000) return {};
    std::vector<std::uint32_t> w(kRestartTriggerBytes / 4, 0);
    const std::uint32_t hi = stub_address >> 16;
    const std::uint32_t lo = stub_address & 0xFFFFu;
    w[0] = 0x3D800000u | hi;                                // lis r12,stub@h
    w[1] = 0x618C0000u | lo;                                // ori r12,r12,stub@l
    w[2] = 0x816C0000u | kRestartTriggerCountOffset;        // lwz r11,count(r12)
    w[3] = 0x396B0001u;                                     // addi r11,r11,1
    w[4] = 0x916C0000u | kRestartTriggerCountOffset;        // stw r11,count(r12)
    w[5] = 0x814C0000u | (kRestartTriggerCountOffset + 4);  // lwz r10,target(r12)
    w[6] = 0x7C0B5040u;                                     // cmplw r11,r10
    w[7] = 0x40820014u;                                     // bne +0x14 (the replay)
    w[8] = 0x818C0000u | (kRestartTriggerCountOffset + 8);  // lwz r12,OSRestart(r12)
    w[9] = 0x7D8903A6u;                                     // mtctr r12
    w[10] = 0x38600000u;                                    // li r3,0
    w[11] = 0x4E800420u;                                    // bctr
    w[12] = replay;
    w[13] = 0x48000000u | (static_cast<std::uint32_t>(delta) & 0x03FFFFFCu);  // b PADRead+4
    w[kRestartTriggerCountOffset / 4] = 0;
    w[kRestartTriggerCountOffset / 4 + 1] = calls;
    w[kRestartTriggerCountOffset / 4 + 2] = os_restart;
    return w;
}

}  // namespace riftwii
