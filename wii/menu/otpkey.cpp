// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "otpkey.hpp"

#include <gccore.h>
#include <ogc/es.h>
#include <ogc/machine/processor.h>

#include <cstdio>
#include <cstring>

#include "log.hpp"

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

// BootMii's backup of a Wii's NAND (movedchannels.cpp reads the same
// file): a 0x100-byte header, then the 0x80-byte OTP. The common key is
// OTP words 5 to 8, so 16 bytes at 0x100 + 5 * 4.
static bool read_bootmii_common_key(std::uint8_t out[16]) {
    constexpr const char* kBootMiiKeys = "sd:/keys.bin";
    constexpr std::size_t kMaxKeysBytes = 0x400;
    constexpr std::size_t kKeysCommonKeyOffset = 0x100 + 5 * 4;
    constexpr std::size_t kKeysMinBytes = 0x100 + 0x80;
    FILE* file = std::fopen(kBootMiiKeys, "rb");
    if (file == nullptr) return false;
    std::uint8_t bytes[kMaxKeysBytes];
    const std::size_t got = std::fread(bytes, 1, sizeof bytes, file);
    const bool read = std::ferror(file) == 0;
    const bool closed = std::fclose(file) == 0;
    bool ok = false;
    if (read && closed && got >= kKeysMinBytes) {
        std::memcpy(out, bytes + kKeysCommonKeyOffset, 16);
        std::uint32_t any = 0;
        for (int i = 0; i < 16; ++i) any |= out[i];
        ok = any != 0;
    }
    clear_wad_key(bytes, sizeof bytes);
    if (!ok) clear_wad_key(out, 16);
    return ok;
}

bool ConsoleWadTitleKey(WadKeySource source, const std::vector<std::uint8_t>& ticket,
                        std::uint8_t out[16]) {
    clear_wad_key(out, 16);
    if (ticket.size() < kTicketBytes || ticket[0x1F1] == 1 || ticket[0x1F1] == 2) return false;
    if (source == WadKeySource::Ios) {
        // Public ES_Decrypt (0x2d), IOSC common-key handle 4. IOS may deny
        // access; that is a normal failure, handled by the next source.
        // Separate cache-line-aligned buffers satisfy IOS IPC requirements.
        alignas(32) u32 iv[8] = {};
        alignas(32) std::uint8_t encrypted[32] = {};
        alignas(32) std::uint8_t decrypted[32] = {};
        std::memcpy(iv, ticket.data() + 0x1DC, 8);
        std::memcpy(encrypted, ticket.data() + 0x1BF, 16);
        const s32 result = ES_Decrypt(4, iv, encrypted, 16, decrypted);
        if (result >= 0) std::memcpy(out, decrypted, 16);
        else logf("WADs: IOS could not decrypt the ticket's title key (error %d); trying sd:/keys.bin.\n", result);
        clear_wad_key(decrypted, sizeof decrypted);
        clear_wad_key(encrypted, sizeof encrypted);
        clear_wad_key(reinterpret_cast<std::uint8_t*>(iv), sizeof iv);
        return result >= 0;
    }
    std::uint8_t common_key[16] = {};
    const bool read = source == WadKeySource::Otp ? ConsoleCommonKey(common_key)
                                                : read_bootmii_common_key(common_key);
    std::string error;
    const bool decrypted = read && decrypt_title_key(ticket.data(), ticket.size(), common_key, out, error);
    clear_wad_key(common_key, sizeof common_key);
    return decrypted;
}

bool ConsoleBackupKey(BackupKey& out) {
    if (read32(kAhbprot) != 0xFFFFFFFF) return false;
    out.console_id = read_otp_word(kConsoleIdWord);
    // An empty OTP reads back console ID 0. The PRNG key may be all zeros on
    // a console that has an ID (Dolphin's default one), so it is kept.
    read_otp_key(kPrngKeyWord, out.prng);
    return out.console_id != 0;
}

}  // namespace riftwii::wii
