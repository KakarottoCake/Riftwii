// SPDX-License-Identifier: GPL-3.0-or-later
#include "sdio.hpp"

#include "d2xsd.hpp"

#include <gccore.h>
#include <ogc/ipc.h>

#include <cstring>

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

constexpr std::uint32_t kCmdSelect = 0x07;
constexpr std::uint32_t kCmdSetBlockLength = 0x10;
constexpr std::uint32_t kCmdReadMultiBlock = 0x12;
constexpr std::uint32_t kCmdApp = 0x37;
constexpr std::uint32_t kAcmdSetBusWidth = 0x06;

constexpr std::uint32_t kTypeAc = 3;
constexpr std::uint32_t kResponseR1 = 1;
constexpr std::uint32_t kResponseR1b = 2;

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
    std::int32_t ret = IOS_Ioctl(card.fd, kIoctlResetCard, nullptr, 0, g_word, 4);
    if (ret < 0) {
        error = ipc_error("card reset", ret);
        close_card(card);
        return false;
    }
    card.rca = static_cast<std::uint16_t>(g_word[0] >> 16);
    ret = IOS_Ioctl(card.fd, kIoctlGetStatus, nullptr, 0, g_word, 4);
    if (ret < 0) {
        error = ipc_error("status", ret);
        close_card(card);
        return false;
    }
    const std::uint32_t status = g_word[0];
    if (!(status & kStatusInserted)) {
        error = "sdio: no card";
        close_card(card);
        return false;
    }
    if (!(status & kStatusInitialized)) {
        error = "sdio: IOS did not initialise the card (host-controller setup not implemented)";
        close_card(card);
        return false;
    }
    card.sdhc = (status & kStatusSdhc) != 0;

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
