// SPDX-License-Identifier: GPL-3.0-or-later
#include "ios_reload.hpp"

#include <gccore.h>
#include <ogc/es.h>
#include <ogc/ios.h>
#include <ogc/ipc.h>
#include <ogc/irq.h>
#include <ogc/machine/processor.h>

#include <cstdint>
#include <cstring>

extern "C" void udelay(int us);

namespace riftwii::wii {
namespace {

char g_dolphin_path[] ATTRIBUTE_ALIGN(32) = "/dev/dolphin";
tikview g_view ATTRIBUTE_ALIGN(32);

constexpr std::uint32_t kIosVersionAddress = 0x80003140;
constexpr int kIpcStartRetries = 400;  // ~400 ms, what libogc allows

void restore_subsystems() {
    __ES_Close();
    __IOS_InitializeSubsystems();
}

}  // namespace

bool running_in_dolphin() {
    static int cached = -1;
    if (cached < 0) {
        const s32 fd = IOS_Open(g_dolphin_path, 0);
        cached = fd >= 0 ? 1 : 0;
        if (fd >= 0) IOS_Close(fd);
    }
    return cached == 1;
}

ReloadResult reload_ios(int version, std::string& error) {
    if (version < 3 || version > 0xFF) {
        error = "IOS" + std::to_string(version) + " is not a valid IOS number";
        return ReloadResult::Failed;
    }
    if (IOS_GetVersion() == version) {
        error.clear();
        return ReloadResult::AlreadyRunning;
    }
    __IOS_ShutdownSubsystems();
    s32 res = __ES_Init();
    if (res < 0) {
        error = "ES init failed: " + std::to_string(res);
        __IOS_InitializeSubsystems();
        return ReloadResult::Failed;
    }
    const u64 title = 0x100000000ull | static_cast<u64>(version);
    std::memset(&g_view, 0, sizeof(g_view));
    u32 views = 0;
    res = ES_GetNumTicketViews(title, &views);
    if (res >= 0 && views >= 1) {
        res = ES_GetTicketViews(title, &g_view, 1);
        if (res < 0) {
            error = "cannot read the IOS ticket: " + std::to_string(res);
            restore_subsystems();
            return ReloadResult::Failed;
        }
    } else if (!running_in_dolphin()) {
        error = "IOS" + std::to_string(version) + " is not installed on this console";
        restore_subsystems();
        return ReloadResult::NotInstalled;
    }
    // A zeroed view is enough for Dolphin's HLE, which ignores it.

    write32(kIosVersionAddress, 0);
    res = ES_LaunchTitleBackground(title, &g_view);
    if (res < 0) {
        error = "ES launch failed: " + std::to_string(res);
        restore_subsystems();
        return ReloadResult::Failed;
    }
    __ES_Reset();

    // IOS is gone until it comes back up: hold the IPC interrupt, wait for
    // the new kernel to announce its version and open the IPC registers,
    // then hand the interrupt back and re-arm libogc's IPC layer.
    __MaskIrq(IM_PI_ACR);
    raw_irq_handler_t handler = IRQ_Free(IRQ_PI_ACR);
    while ((read32(kIosVersionAddress) >> 16) == 0) udelay(1000);
    for (int i = 0; i <= kIpcStartRetries; ++i) {
        udelay(1000);
        if (HW_IPC_PPCCTRL & HW_IPC_PPC_CTRL_REGS) break;
    }
    IRQ_Request(IRQ_PI_ACR, handler, nullptr);
    __UnmaskIrq(IM_PI_ACR);
    __IPC_Reinitialize();

    const s32 now = IOS_GetVersion();
    __IOS_InitializeSubsystems();
    if (now != version) {
        error = "asked for IOS" + std::to_string(version) + " but IOS" + std::to_string(now) + " came up";
        return ReloadResult::Failed;
    }
    error.clear();
    return ReloadResult::Ok;
}

}  // namespace riftwii::wii
