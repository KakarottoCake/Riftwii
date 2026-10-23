// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: Michael Wiedenbauer, Dave Murphy, Sven Peter (libogc) <https://github.com/devkitPro/libogc>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "sdio.hpp"

#include "d2xsd.hpp"

#include <gccore.h>
#include <ogc/ipc.h>

#include <cstdio>
#include <cstring>

extern "C" void udelay(int us);

namespace riftwii::wii::sdio {
namespace {

// wiibrew /dev/sdio/slot0
constexpr std::int32_t kIoctlWriteHcr = 0x01;
constexpr std::int32_t kIoctlReadHcr = 0x02;
constexpr std::int32_t kIoctlResetCard = 0x04;
constexpr std::int32_t kIoctlSetClock = 0x06;
constexpr std::int32_t kIoctlSendCommand = 0x07;
constexpr std::int32_t kIoctlGetStatus = 0x0B;

constexpr std::uint32_t kStatusInserted = 0x1;
constexpr std::uint32_t kStatusInitialized = 0x10000;
constexpr std::uint32_t kStatusSdhc = 0x100000;

constexpr std::uint32_t kHcrHostControl = 0x28;
constexpr std::uint32_t kHostControl4Bit = 0x02;
constexpr std::uint32_t kHcrPowerControl = 0x29;
constexpr std::uint32_t kHcrClockControl = 0x2C;
constexpr std::uint32_t kHcrTimeoutControl = 0x2E;
constexpr std::uint32_t kHcrSoftwareReset = 0x2F;

constexpr std::uint32_t kCmdGoIdle = 0x00;
constexpr std::uint32_t kCmdAllSendCid = 0x02;
constexpr std::uint32_t kCmdSendRca = 0x03;
constexpr std::uint32_t kCmdSelect = 0x07;
constexpr std::uint32_t kCmdSendIfCond = 0x08;
constexpr std::uint32_t kCmdSetBlockLength = 0x10;
constexpr std::uint32_t kCmdReadMultiBlock = 0x12;
constexpr std::uint32_t kCmdApp = 0x37;
constexpr std::uint32_t kAcmdSetBusWidth = 0x06;
constexpr std::uint32_t kAcmdSendOpCond = 0x29;

constexpr std::uint32_t kTypeNoData = 0;
constexpr std::uint32_t kTypeAc = 3;
constexpr std::uint32_t kResponseR1 = 1;
constexpr std::uint32_t kResponseR1b = 2;
// The card status bits of an R1 reply that mean the command failed: out of
// range, address error, CRC error, illegal command, ECC failed, controller
// error, general error. (The other bits are state, not errors.)
constexpr std::uint32_t kR1Errors = 0xC0F80000;
constexpr std::uint32_t kResponseR2 = 3;
constexpr std::uint32_t kResponseR3 = 4;
// IOS's response selectors, as used by libogc (not SD response numbers).
constexpr std::uint32_t kResponseRca = 6;
constexpr std::uint32_t kResponseIfCond = 7;

constexpr std::uint32_t kSectorBytes = 512;

// Same layout as the runtime's rt_sdio_request.
struct Request {
    std::uint32_t cmd;
    std::uint32_t cmd_type;
    std::uint32_t rsp_type;
    std::uint32_t arg;
    std::uint32_t blk_cnt;
    std::uint32_t blk_size;
    void* dma_addr;
    std::uint32_t isdma;
    std::uint32_t pad0;
};

// IPC buffers: 32-byte aligned and alone in their lines.
char g_path[32] ATTRIBUTE_ALIGN(32) = "/dev/sdio/slot0";
Request g_request ATTRIBUTE_ALIGN(32);
std::uint32_t g_response[8] ATTRIBUTE_ALIGN(32);
std::uint32_t g_word[8] ATTRIBUTE_ALIGN(32);
std::uint32_t g_hcr[8] ATTRIBUTE_ALIGN(32);
ioctlv g_vec[4] ATTRIBUTE_ALIGN(32);

std::string ipc_error(const char* what, std::int32_t ret) {
    return std::string("sdio: ") + what + " failed (IPC " + std::to_string(ret) + ")";
}

// SENDCMD. `buffer`/`bytes` describe the DMA target of a data command.
// Like libogc, the vector form is used for data commands and on SDHC
// cards, the plain ioctl otherwise.
std::int32_t send_command(const Card& card, std::uint32_t cmd, std::uint32_t type, std::uint32_t response,
                          std::uint32_t arg, std::uint32_t blocks, std::uint32_t block_size, void* buffer,
                          std::uint32_t bytes) {
    g_request.cmd = cmd;
    g_request.cmd_type = type;
    g_request.rsp_type = response;
    g_request.arg = arg;
    g_request.blk_cnt = blocks;
    g_request.blk_size = block_size;
    g_request.dma_addr = buffer;
    g_request.isdma = buffer != nullptr ? 1 : 0;
    g_request.pad0 = 0;
    std::memset(g_response, 0, sizeof(g_response));
    if (buffer != nullptr || card.sdhc) {
        g_vec[0].data = &g_request;
        g_vec[0].len = sizeof(g_request);
        g_vec[1].data = buffer;
        g_vec[1].len = bytes;
        g_vec[2].data = g_response;
        g_vec[2].len = 16;
        return IOS_Ioctlv(card.fd, kIoctlSendCommand, 2, 1, g_vec);
    }
    return IOS_Ioctl(card.fd, kIoctlSendCommand, &g_request, sizeof(g_request), g_response, 16);
}

bool read_hcr(const Card& card, std::uint32_t reg, std::uint32_t size, std::uint32_t& value, std::string& error) {
    std::memset(g_hcr, 0, sizeof(g_hcr));
    g_hcr[0] = reg;
    g_hcr[3] = size;
    g_word[0] = 0;
    const std::int32_t ret = IOS_Ioctl(card.fd, kIoctlReadHcr, g_hcr, 24, g_word, 4);
    if (ret < 0) {
        error = ipc_error("host controller register read", ret);
        return false;
    }
    value = g_word[0];
    return true;
}

bool write_hcr(const Card& card, std::uint32_t reg, std::uint32_t size, std::uint32_t value, std::string& error) {
    std::memset(g_hcr, 0, sizeof(g_hcr));
    g_hcr[0] = reg;
    g_hcr[3] = size;
    g_hcr[4] = value;
    const std::int32_t ret = IOS_Ioctl(card.fd, kIoctlWriteHcr, g_hcr, 24, nullptr, 0);
    if (ret < 0) {
        error = ipc_error("host controller register write", ret);
        return false;
    }
    return true;
}

bool wait_hcr(const Card& card, std::uint32_t reg, std::uint32_t size, std::uint32_t mask,
              std::uint32_t expected, std::string& error) {
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        std::uint32_t value = 0;
        if (!read_hcr(card, reg, size, value, error)) return false;
        if ((value & mask) == expected) return true;
        udelay(1000);
    }
    error = "sdio: host controller did not become ready after 100 checks";
    return false;
}

bool initialize_card(Card& card, std::string& error) {
    // Older IOS SDI modules can expose the card's registers but cannot
    // initialise an SDHC card. libogc's mounted view already handles this;
    // the raw handoff needs the same fallback after libfat has shut down.
    IOS_Close(card.fd);
    card.fd = IOS_Open(g_path, 1);
    if (card.fd < 0) {
        error = ipc_error("reopen for manual initialization", card.fd);
        return false;
    }
    card.rca = 0;
    card.sdhc = true;  // SENDCMD's vector form during SD v2 identification
    if (!write_hcr(card, kHcrSoftwareReset, 1, 7, error) ||
        !wait_hcr(card, kHcrSoftwareReset, 1, 7, 0, error) ||
        !write_hcr(card, 0x34, 4, 0x13F00C3, error) ||
        !write_hcr(card, 0x38, 4, 0x13F00C3, error) ||
        !write_hcr(card, kHcrPowerControl, 1, 0x0E, error) ||
        !write_hcr(card, kHcrPowerControl, 1, 0x0F, error) ||
        !write_hcr(card, kHcrClockControl, 2, 0, error) ||
        !write_hcr(card, kHcrClockControl, 2, 0x101, error) ||
        !wait_hcr(card, kHcrClockControl, 2, 2, 2, error) ||
        !write_hcr(card, kHcrClockControl, 2, 0x107, error) ||
        !write_hcr(card, kHcrTimeoutControl, 1, 0x0E, error)) {
        return false;
    }

    const auto command = [&](std::uint32_t cmd, std::uint32_t type, std::uint32_t response, std::uint32_t arg) {
        const std::int32_t ret = send_command(card, cmd, type, response, arg, 0, 0, nullptr, 0);
        const std::string operation = "manual initialization command " + std::to_string(cmd);
        if (ret < 0) {
            error = ipc_error(operation.c_str(), ret);
            return false;
        }
        // IOS answered, but an R1 reply carries the card's own verdict too.
        // Only R1 replies do: the others hold the voltage echo, the
        // operating conditions, the card's ID or its address.
        const bool card_status = response == kResponseR1 || response == kResponseR1b;
        if (card_status && (g_response[0] & kR1Errors) != 0) {
            char status[16];
            std::snprintf(status, sizeof(status), "%08X", static_cast<unsigned>(g_response[0]));
            error = "sdio: " + operation + ": the card answered with an error (status " + status + ")";
            return false;
        }
        return true;
    };
    if (!command(kCmdGoIdle, kTypeNoData, 0, 0) ||
        !command(kCmdSendIfCond, kTypeNoData, kResponseIfCond, 0x1AA)) return false;
    if ((g_response[0] & 0xFFF) != 0x1AA) {
        error = "sdio: the card did not accept SD v2 voltage and check pattern";
        return false;
    }

    bool ready = false;
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        if (!command(kCmdApp, kTypeAc, kResponseR1, 0) ||
            !command(kAcmdSendOpCond, kTypeNoData, kResponseR3, 0x40300000)) return false;
        if (g_response[0] & 0x80000000) {
            card.sdhc = (g_response[0] & 0x40000000) != 0;
            ready = true;
            break;
        }
        udelay(10000);
    }
    if (!ready) {
        error = "sdio: the card did not finish SD v2 power-up after 100 checks";
        return false;
    }
    if (!command(kCmdAllSendCid, kTypeNoData, kResponseR2, 0) ||
        !command(kCmdSendRca, kTypeNoData, kResponseRca, 0)) return false;
    card.rca = static_cast<std::uint16_t>(g_response[0] >> 16);
    if (card.rca == 0) {
        error = "sdio: the card returned no relative address during manual initialization";
        return false;
    }
    return true;
}

}  // namespace

bool open_card(Card& card, std::string& error) {
    card = Card{};
    if (using_d2x_sd()) {
        // The game is on this card and d2x drives it: share its device.
        if (!d2x_sd_open(error)) return false;
        card.fd = d2x_sd_fd();
        card.sdhc = true;
        card.selected = true;
        card.d2x = true;
        error.clear();
        return true;
    }
    card.fd = IOS_Open(g_path, 1);
    if (card.fd < 0) {
        error = ipc_error("open", card.fd);
        return false;
    }
    g_word[0] = 0;
    const std::int32_t reset_result = IOS_Ioctl(card.fd, kIoctlResetCard, nullptr, 0, g_word, 4);
    if (reset_result >= 0) card.rca = static_cast<std::uint16_t>(g_word[0] >> 16);
    g_word[0] = 0;
    std::int32_t ret = IOS_Ioctl(card.fd, kIoctlGetStatus, nullptr, 0, g_word, 4);
    // IOS refusing both the reset and the status says nothing about the
    // card: the manual initialization below reopens the slot and resets the
    // host itself, so it gets its chance (a missing card fails there, each
    // wait bounded). A status refused after a good reset still stops here.
    const bool status_known = ret >= 0;
    if (!status_known && reset_result >= 0) {
        error = ipc_error("status", ret);
        close_card(card);
        return false;
    }
    const std::uint32_t status = status_known ? g_word[0] : 0;
    if (status_known && !(status & kStatusInserted)) {
        error = "sdio: no card";
        close_card(card);
        return false;
    }
    if (reset_result < 0 || !(status & kStatusInitialized) || card.rca == 0) {
        if (!initialize_card(card, error)) {
            if (reset_result < 0) error = ipc_error("card reset", reset_result) + "; " + error;
            close_card(card);
            return false;
        }
    } else {
        card.sdhc = (status & kStatusSdhc) != 0;
    }

    ret = send_command(card, kCmdSelect, kTypeAc, kResponseR1b, static_cast<std::uint32_t>(card.rca) << 16, 0, 0,
                       nullptr, 0);
    if (ret < 0) {
        error = ipc_error("select", ret);
        close_card(card);
        return false;
    }
    card.selected = true;
    ret = send_command(card, kCmdSetBlockLength, kTypeAc, kResponseR1, kSectorBytes, 0, 0, nullptr, 0);
    if (ret < 0) {
        error = ipc_error("set block length", ret);
        close_card(card);
        return false;
    }
    ret = send_command(card, kCmdApp, kTypeAc, kResponseR1, static_cast<std::uint32_t>(card.rca) << 16, 0, 0, nullptr,
                       0);
    if (ret >= 0) ret = send_command(card, kAcmdSetBusWidth, kTypeAc, kResponseR1, 2, 0, 0, nullptr, 0);
    if (ret < 0) {
        error = ipc_error("4-bit bus", ret);
        close_card(card);
        return false;
    }
    std::uint32_t host_control = 0;
    if (!read_hcr(card, kHcrHostControl, 1, host_control, error) ||
        !write_hcr(card, kHcrHostControl, 1, (host_control & 0xFF) | kHostControl4Bit, error)) {
        close_card(card);
        return false;
    }
    g_word[0] = 1;
    ret = IOS_Ioctl(card.fd, kIoctlSetClock, g_word, 4, nullptr, 0);
    if (ret < 0) {
        error = ipc_error("set clock", ret);
        close_card(card);
        return false;
    }
    error.clear();
    return true;
}

bool read_sectors(const Card& card, std::uint32_t sector, std::uint32_t count, void* buffer, std::string& error) {
    if (card.fd < 0 || !card.selected) {
        error = "sdio: card not open";
        return false;
    }
    if (count == 0 || (reinterpret_cast<std::uintptr_t>(buffer) & 31) != 0) {
        error = "sdio: read needs a 32-byte aligned buffer and a sector count";
        return false;
    }
    if (card.d2x) {
        if (!d2x_sd_read(sector, count, buffer)) {
            error = "d2x SD read failed at sector " + std::to_string(sector);
            return false;
        }
        error.clear();
        return true;
    }
    const std::uint32_t arg = card.sdhc ? sector : sector * kSectorBytes;
    const std::int32_t ret = send_command(card, kCmdReadMultiBlock, kTypeAc, kResponseR1, arg, count, kSectorBytes,
                                          buffer, count * kSectorBytes);
    if (ret < 0) {
        error = ipc_error("read", ret);
        return false;
    }
    error.clear();
    return true;
}

void close_card(Card& card) {
    if (card.d2x) {  // the shared handle stays open (d2xsd.hpp)
        card = Card{};
        return;
    }
    if (card.fd >= 0) {
        if (card.selected) send_command(card, kCmdSelect, kTypeAc, kResponseR1b, 0, 0, 0, nullptr, 0);
        IOS_Close(card.fd);
    }
    card = Card{};
}

}  // namespace riftwii::wii::sdio
