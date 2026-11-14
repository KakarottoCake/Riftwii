// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2014 Alex Chadwick (Brainslug) <https://github.com/Chadderz121/brainslug-wii>
// SPDX-FileCopyrightText: 2020 Florian Bach (Brainslug)
// SPDX-FileCopyrightText: Michael Wiedenbauer, Dave Murphy, Hector Martin (libogc) <https://github.com/devkitPro/libogc>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ios_reload.hpp"

#include "covers.hpp"

#include <fat.h>
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

#include "boot.hpp"
#include "d2xsd.hpp"
#include "d2xreturnto.hpp"
#include "di.hpp"
#include "disclight.hpp"
#include "gcadapter.hpp"
#include "memlimits.hpp"
#include "nandpermissions.hpp"
#include "menumusic.hpp"
#include "netsock.hpp"
#include "umsdev.hpp"
#include "nanduse.hpp"
#include "padhook.hpp"
#include "usbinput.hpp"

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

// For a while after ES_LaunchTitleBackground returns, the new IOS's start-up
// makes MEM2 read back as zeros: on a Wii, from about 10 ms to 30 ms after
// the call, with the kernel announcing itself at 25 ms (2611-219 and the same
// on main, every word sampled across RiftWii's MEM2 heap, the memory itself
// unchanged once it reads back). Anything RiftWii ran in that window and
// acted on, on any thread, could write a zero it read back into the heap:
// launches refused for "memory damaged during the reload", or a crash.
// So from the call's return no other thread or interrupt runs, and the
// interrupts come back only once check words in MEM2, written before the
// call, read back right a few times in a row, and no sooner than twice the
// window seen.
constexpr int kMem2BackTimeoutMs = 500;
constexpr int kMem2SteadyReads = 3;
constexpr int kMem2MinWaitMs = 60;  // twice the window seen on a Wii
constexpr u32 kCheckLine = 32;                  // one cache line of check words
constexpr u32 kCheckBlock = 4096 + kCheckLine;  // a line at each end, a page apart
volatile u32* g_check = nullptr;                // in MEM2, taken at start-up and kept
u32 g_check_value = 0;
// libogc starts main() at 64. The menu's GUI, watchdog and other threads run
// above that (70, 80), so across the call the reload runs at the top: when
// the call returns, it runs before any of them and switches interrupts off.
// Only the main thread (the one that called ReloadCheckInit) is raised and
// put back, so a reload from any other thread keeps its own priority.
constexpr u32 kMainThreadPriority = 64;
lwp_t g_main_thread = LWP_THREAD_NULL;

void raise_priority() {
    if (g_main_thread != LWP_THREAD_NULL && LWP_GetSelf() == g_main_thread)
        LWP_SetThreadPriority(g_main_thread, LWP_PRIO_HIGHEST);
}

void restore_priority() {
    if (g_main_thread != LWP_THREAD_NULL && LWP_GetSelf() == g_main_thread)
        LWP_SetThreadPriority(g_main_thread, kMainThreadPriority);
}

// The check words, read through the uncached mirror: what memory gives back.
bool mem2_reads_back() {
    if (g_check == nullptr) return true;
    const u32 base = reinterpret_cast<u32>(MEM_K0_TO_K1(const_cast<u32*>(g_check)));
    for (u32 line : {0u, kCheckBlock - kCheckLine}) {
        const volatile u32* raw = reinterpret_cast<const volatile u32*>(base + line);
        for (u32 i = 0; i < kCheckLine / 4; ++i)
            if (raw[i] != (g_check_value ^ (line + i))) return false;
    }
    return true;
}

void arm_mem2_check() {
    if (g_check == nullptr) return;
    // A new value each reload: words left from the last one prove nothing.
    g_check_value = 0x52574D32u ^ static_cast<u32>(gettime());
    for (u32 line : {0u, kCheckBlock - kCheckLine}) {
        volatile u32* words = reinterpret_cast<volatile u32*>(reinterpret_cast<u32>(g_check) + line);
        for (u32 i = 0; i < kCheckLine / 4; ++i) words[i] = g_check_value ^ (line + i);
    }
    DCFlushRange(const_cast<u32*>(g_check), kCheckBlock);
}

int ms_since(u64 from) { return static_cast<int>(ticks_to_millisecs(gettime() - from)); }

// MEM2 never read back: nothing may run on what it gives (another thread,
// a callback, the failure message's allocation), so interrupts stay off and
// the console stops here (no message: it would allocate). Hold POWER.
[[noreturn]] void halt_mem2_wrong() {
    g_terminal_failure = true;
    for (;;) ++g_terminal_spin;
}

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

void ReloadCheckInit() {
    if (g_check != nullptr) return;
    g_main_thread = LWP_GetSelf();
    // From the MEM2 arena's low end, as the menu's textures are taken (and
    // before them, so their own giving back is not blocked by it).
    u32 level;
    _CPU_ISR_Disable(level);
    const u32 lo = (reinterpret_cast<u32>(SYS_GetArena2Lo()) + 31) & ~31u;
    const u32 hi = reinterpret_cast<u32>(SYS_GetArena2Hi());
    if (lo <= hi && kCheckBlock <= hi - lo) {
        SYS_SetArena2Lo(reinterpret_cast<void*>(lo + kCheckBlock));
        g_check = reinterpret_cast<volatile u32*>(lo);
    }
    _CPU_ISR_Restore(level);
}

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
    // Only the PPC alarm and GPIO: this does not need the failed IOS.
    StopDiscLight();
    // The IOS IPC interrupt is still masked and its handler is gone. Do not
    // return into CRT/libogc cleanup or call any subsystem here.
    for (;;) ++g_terminal_spin;
}

ReloadResult reload_ios(int version, std::string& error, bool force) {
    CoverLoaderHold();  // no SD read in flight across the reload
    // Launches stop the music before they close the log; this is for any
    // reload that comes without that: no audio may run across a reload
    // (LaunchMusicPauseForReload says what it did on a Wii).
    LaunchMusicEnd("an IOS reload");
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
    // The adapter's transfers too: their replies must not outlive it. And
    // d2x's USB device, opened (or refused) by the IOS going away. And
    // the NAND's /dev/fs: a handle left open across the reload (the menu's
    // screenshot import left one) went with heap damage on the next IOS.
    NetStop();
    GcAdapterStop();
    UsbInputStop();
    ums::Forget();
    // d2x's SD device the same way (#270): a launch that failed on one
    // cIOS slot and was retried on the next read the card through the
    // first slot's dead handle. Callers are expected to have let libfat go
    // of the card by now; this is in case one has not, so its cache is
    // written while the device still answers and libfat is never left on
    // a dead handle.
    if (using_d2x_sd()) fatUnmount("sd:");
    d2x_sd_forget();
    // And /dev/di: every launch path closes it before reloading, and
    // di::open would return a handle left open as if it were still good.
    di::close();
    // And the launch's own /dev/usb/hid handle (#284): a launch that failed
    // after opening it would otherwise return the dead descriptor to the next
    // use on the menu's IOS.
    forget_usb_hid_handle();
    // The running IOS's NAND permission check, put back while that IOS is
    // still there to put it back in: open, it would read as open for the
    // next IOS too, which nobody patched.
    CloseAllNandPermissions("IOS reload");
    NandCloseAll();
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
    arm_mem2_check();
    const u64 launched = gettime();
    raise_priority();
    res = ES_LaunchTitleBackground(title, &g_view);
    const int returned_ms = ms_since(launched);
    if (res < 0) {
        restore_priority();
        error = "ES launch failed: " + std::to_string(res);
        restore_subsystems();
        return ReloadResult::Failed;
    }
    // Nothing else runs until MEM2 reads back (above). What follows up to
    // there touches only MEM1, the hardware registers and the check words
    // (uncached): no allocation, no log, no std::string. The window opened
    // about 10 ms after the call returned on a Wii; the call returns at once.
    u32 level;
    _CPU_ISR_Disable(level);
    int mem2_wrong_from = -1, mem2_wrong_to = -1;
    __ES_Reset();
    forget_d2x_return_to();
    // The old IOS is gone from here, and d2x's FAT module and NAND
    // emulation with it: RiftWii may use the card again. A reload refused
    // before this point leaves them running, and the card held.
    hold_sd_for_d2x_fat(false);

    // IOS is gone until it comes back up: hold the IPC interrupt, wait for
    // the new kernel to announce its version and open the IPC registers,
    // then hand the interrupt back and re-arm libogc's IPC layer.
    __MaskIrq(IM_PI_ACR);
    raw_irq_handler_t handler = IRQ_Free(IRQ_PI_ACR);
    int started_ms = 0;
    for (; (read32(kIosVersionAddress) >> 16) == 0; ++started_ms) {
        if (started_ms >= kIosStartTimeoutMs) {
            // The message allocates: only once MEM2 reads back steadily.
            for (int i = 0; i < kMem2SteadyReads; ++i) {
                if (!mem2_reads_back()) halt_mem2_wrong();
                udelay(1000);
            }
            _CPU_ISR_Restore(level);
            restore_priority();
            return terminal_startup_failure(version, "did not start within 10 s", error);
        }
        if (!mem2_reads_back()) {
            if (mem2_wrong_from < 0) mem2_wrong_from = ms_since(launched);
            mem2_wrong_to = ms_since(launched);
        }
        udelay(1000);
    }
    const int announced_ms = ms_since(launched);
    int mem2_back_ms = -1;
    int steady = 0;
    for (int waited = 0; waited <= kMem2BackTimeoutMs; ++waited) {
        if (mem2_reads_back()) {
            if (++steady >= kMem2SteadyReads && ms_since(launched) >= kMem2MinWaitMs) {
                mem2_back_ms = ms_since(launched);
                break;
            }
        } else {
            steady = 0;
            if (mem2_wrong_from < 0) mem2_wrong_from = ms_since(launched);
            mem2_wrong_to = ms_since(launched);
        }
        udelay(1000);
    }
    // MEM2 still wrong half a second on: going on, or even showing the
    // failure (it allocates), would be the damage this wait is for.
    if (mem2_back_ms < 0) halt_mem2_wrong();
    // Interrupts back; the priority stays at the top until libogc's IPC
    // layer is re-armed below, so no other thread runs on a dead one.
    _CPU_ISR_Restore(level);
    // What the new kernel's staging does to low MEM2 on a Wii, in Dolphin.
    mem::PoisonReloadArea();
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
    const std::string mem2_note =
        g_check == nullptr
            ? "no MEM2 check words (no memory for them), waited " + std::to_string(mem2_back_ms) + " ms after the call"
            : "MEM2 check words read back " + std::to_string(mem2_back_ms) + " ms after the call (" +
                  (mem2_wrong_from >= 0 ? "seen wrong from " + std::to_string(mem2_wrong_from) + " to " +
                                              std::to_string(mem2_wrong_to) + " ms"
                                        : std::string("never seen wrong")) +
                  ")";
    g_reload_detail = "IOS" + std::to_string(version) + " announced itself after " + std::to_string(started_ms) +
                      " ms (" + std::to_string(announced_ms) + " ms after the call, which returned after " +
                      std::to_string(returned_ms) + " ms); " + mem2_note + "; " +
                      (ipc_ms >= 0 ? "IPC ready after " + std::to_string(ipc_ms) + " ms"
                                   : std::string("IPC ack bit not seen in 400 ms, continued as libogc does"));
    IRQ_Request(IRQ_PI_ACR, handler, nullptr);
    __UnmaskIrq(IM_PI_ACR);
    __IPC_Reinitialize();

    const s32 now = IOS_GetVersion();
    __IOS_InitializeSubsystems();
    UsbInputIosReloaded();  // whatever IOS came up, the old one's reads are gone
    restore_priority();
    if (now != version) {
        error = "asked for IOS" + std::to_string(version) + " but IOS" + std::to_string(now) + " came up";
        return ReloadResult::Failed;
    }
    error.clear();
    g_reloaded_at = gettime();
    return ReloadResult::Ok;
}

}  // namespace riftwii::wii
