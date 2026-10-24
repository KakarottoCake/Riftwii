// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "otpkey.hpp"

#include <gccore.h>
#include <ogc/machine/processor.h>

namespace riftwii::wii {
namespace {

constexpr u32 kAhbprot = 0x0D800064;
constexpr u32 kOtpCommand = 0x0D8001EC;
constexpr u32 kOtpData = 0x0D8001F0;
constexpr u32 kOtpRead = 0x80000000;
constexpr u32 kCommonKeyWord = 5;
constexpr u32 kConsoleIdWord = 9;
constexpr u32 kPrngKeyWord = 26;

u32 read_otp_word(u32 word) {
    write32(kOtpCommand, kOtpRead | word);
    return read32(kOtpData);
}

// Four OTP words from `first` as a 16-byte key; false when all are zero.
bool read_otp_key(u32 first, std::uint8_t out[16]) {
    u32 any = 0;
    for (u32 w = 0; w < 4; ++w) {
        const u32 v = read_otp_word(first + w);
        out[4 * w] = static_cast<std::uint8_t>(v >> 24);
        out[4 * w + 1] = static_cast<std::uint8_t>(v >> 16);
        out[4 * w + 2] = static_cast<std::uint8_t>(v >> 8);
        out[4 * w + 3] = static_cast<std::uint8_t>(v);
        any |= v;
    }
    return any != 0;
}

}  // namespace

bool ConsoleCommonKey(std::uint8_t out[16]) {
    if (read32(kAhbprot) != 0xFFFFFFFF) return false;
    return read_otp_key(kCommonKeyWord, out);
}

bool ConsoleBackupKey(BackupKey& out) {
    if (read32(kAhbprot) != 0xFFFFFFFF) return false;
    out.console_id = read_otp_word(kConsoleIdWord);
    return read_otp_key(kPrngKeyWord, out.prng) && out.console_id != 0;
}

}  // namespace riftwii::wii
