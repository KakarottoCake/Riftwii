// SPDX-License-Identifier: GPL-3.0-or-later
#include "ios_reload.hpp"

#include <gccore.h>
#include <ogc/es.h>
#include <ogc/ios.h>
#include <ogc/ipc.h>
#include <ogc/irq.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/machine/processor.h>
#include <wiiuse/wpad.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "memlimits.hpp"
#include "netsock.hpp"

extern "C" void udelay(int us);

namespace riftwii::wii {
namespace {

char g_dolphin_path[] ATTRIBUTE_ALIGN(32) = "/dev/dolphin";
tikview g_view ATTRIBUTE_ALIGN(32);
bool g_terminal_failure = false;
volatile std::uint32_t g_terminal_spin = 0;
std::string g_reload_detail;
bool g_wii_remotes_released = false;

constexpr std::uint32_t kIosVersionAddress = 0x80003140;
constexpr int kIpcStartRetries = 400;    // ~400 ms, what libogc allows
constexpr int kIosStartTimeoutMs = 10000;  // libogc waits forever; a message beats a black screen

void restore_subsystems() {
    __ES_Close();
    __IOS_InitializeSubsystems();
}

ReloadResult terminal_startup_failure(int version, const char* detail, std::string& error) {
    // IRQ_PI_ACR is masked and its libogc handler is removed here.  IOS IPC
    // calls, filesystem mounts and file logging can now block forever, so
    // print solely to the already-active console and leave recovery to the
    // top-level terminal path.
    error = "IOS" + std::to_string(version) + " " + detail + "; hold POWER to shut down";
    std::printf("%s\n", error.c_str());
    g_terminal_failure = true;
    return ReloadResult::Terminal;
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

bool reload_terminal_failure() { return g_terminal_failure; }

u64 g_reloaded_at = 0;  // gettime() at the last reload that worked

unsigned ms_since_ios_reload() {
    return g_reloaded_at == 0 ? 0xFFFFFFFFu : static_cast<unsigned>(ticks_to_millisecs(gettime() - g_reloaded_at));
}

const std::string& last_reload_detail() { return g_reload_detail; }

void release_wii_remotes() {
    if (g_wii_remotes_released) return;
    WPAD_Shutdown();
    g_wii_remotes_released = true;
}

[[noreturn]] void halt_after_terminal_reload() {
    // The IOS IPC interrupt is still masked and its handler is gone. Do not
    // return into CRT/libogc cleanup or call any subsystem here.
    for (;;) ++g_terminal_spin;
}

ReloadResult reload_ios(int version, std::string& error, bool force) {
    g_terminal_failure = false;
    g_reload_detail.clear();
    if (version < 3 || version > 0xFF) {
        error = "IOS" + std::to_string(version) + " is not a valid IOS number";
        return ReloadResult::Failed;
    }
    if (!force && IOS_GetVersion() == version) {
        error.clear();
        return ReloadResult::AlreadyRunning;
    }
    // The network's IOS state dies with the reload; close it first so
    // nothing of it is left half open (1.0.5 left it up after downloads).
    NetStop();
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
    // What the new kernel's staging does to low MEM2 on a Wii, in Dolphin.
    mem::PoisonReloadArea();

    // IOS is gone until it comes back up: hold the IPC interrupt, wait for
    // the new kernel to announce its version and open the IPC registers,
    // then hand the interrupt back and re-arm libogc's IPC layer.
    __MaskIrq(IM_PI_ACR);
    raw_irq_handler_t handler = IRQ_Free(IRQ_PI_ACR);
    int started_ms = 0;
    for (; (read32(kIosVersionAddress) >> 16) == 0; ++started_ms) {
        if (started_ms >= kIosStartTimeoutMs) {
            return terminal_startup_failure(version, "did not start within 10 s", error);
        }
        udelay(1000);
    }
    // libogc polls the IPC ack bit for up to 400 ms and then carries on
    // regardless; the bit is not always raised by the time the new kernel
    // has announced itself. Failing here stranded real consoles on the
    // power-off screen, so do the same and only note it.
    int ipc_ms = -1;
    for (int i = 0; i <= kIpcStartRetries; ++i) {
        if (read32(0x0D000004) & HW_IPC_PPC_MSG_ACK) {
            ipc_ms = i;
            break;
        }
        udelay(1000);
    }
    g_reload_detail = "IOS" + std::to_string(version) + " announced itself after " + std::to_string(started_ms) +
                      " ms; " + (ipc_ms >= 0 ? "IPC ready after " + std::to_string(ipc_ms) + " ms"
                                             : std::string("IPC ack bit not seen in 400 ms, continued as libogc does"));
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
    g_reloaded_at = gettime();
    return ReloadResult::Ok;
}

}  // namespace riftwii::wii
