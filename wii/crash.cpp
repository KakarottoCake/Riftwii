// SPDX-License-Identifier: GPL-3.0-or-later
#include "crash.hpp"

#include <gccore.h>
#include <ogc/console.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/video.h>
#include <tuxedo/ppc/exception.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "console.hpp"
#include "guiscript.hpp"
#include "log.hpp"
#include "menu.h"
#include "restart.hpp"
#include "video.h"

namespace riftwii::wii {
namespace {

// Crashes this soon after a crash restart stop the loop: RiftWii then
// leaves to the Homebrew Channel instead.
constexpr u64 kCrashLoopSeconds = 30;
constexpr int kAutoRestartSeconds = 60;
constexpr unsigned kBacktraceFrames = 12;

struct CrashInfo {
    unsigned exid;
    u32 pc, lr, msr, cr, ctr, dar, dsisr;
    u32 gpr[32];
};

CrashInfo g_info;
volatile int g_crashing = 0;
CrashPhase g_phase = CrashPhase::Early;
u64 g_started = 0;
alignas(16) u8 g_stack[0x8000];
char g_report[3072];

const char* ExceptionName(unsigned exid) {
    switch (exid) {
        case PPC_EXCPT_MCHK: return "machine check";
        case PPC_EXCPT_DSI: return "bad data address (DSI)";
        case PPC_EXCPT_ISI: return "bad code address (ISI)";
        case PPC_EXCPT_ALIGN: return "misaligned access";
        case PPC_EXCPT_UNDEF: return "illegal instruction";
        case PPC_EXCPT_FPU: return "floating point unavailable";
        case PPC_EXCPT_TRACE: return "trace";
        case PPC_EXCPT_PM: return "performance monitor";
        case PPC_EXCPT_BKPT: return "breakpoint";
        default: return "exception";
    }
}

bool Plausible(u32 address) {
    return (address & 3) == 0 && ((address >= 0x80000000 && address < 0x81800000) ||
                                  (address >= 0x90000000 && address < 0x94000000));
}

void Append(std::size_t& at, const char* format, ...) __attribute__((format(printf, 2, 3)));
void Append(std::size_t& at, const char* format, ...) {
    if (at >= sizeof(g_report)) return;
    va_list args;
    va_start(args, format);
    const int n = std::vsnprintf(g_report + at, sizeof(g_report) - at, format, args);
    va_end(args);
    if (n > 0) at += static_cast<std::size_t>(n);
}

void BuildReport() {
    std::size_t at = 0;
    const CrashInfo& c = g_info;
    Append(at, "RiftWii %s crashed: %s\n", RIFTWII_VERSION, ExceptionName(c.exid));
    Append(at, "PC %08X  LR %08X  MSR %08X  CR %08X  CTR %08X\n", c.pc, c.lr, c.msr, c.cr, c.ctr);
    if (c.exid == PPC_EXCPT_DSI) Append(at, "DAR %08X  DSISR %08X\n", c.dar, c.dsisr);
    for (int r = 0; r < 32; r += 4) {
        Append(at, "R%02d %08X  R%02d %08X  R%02d %08X  R%02d %08X\n", r, c.gpr[r], r + 1, c.gpr[r + 1], r + 2,
               c.gpr[r + 2], r + 3, c.gpr[r + 3]);
    }
    // The stack's saved return addresses: back chain at 0(sp), LR at 4(sp).
    Append(at, "Stack:");
    u32 sp = c.gpr[1];
    for (unsigned i = 0; i < kBacktraceFrames && Plausible(sp); ++i) {
        const u32 next = *reinterpret_cast<const u32*>(sp);
        if (!Plausible(next) || next <= sp) break;
        Append(at, " %08X", *reinterpret_cast<const u32*>(next + 4));
        sp = next;
    }
    Append(at, "\nIOS%d. The addresses are in this release's riftwii.elf.\n", IOS_GetVersion());
}

// Prints `text` with every line indented, clear of the TV's overscan.
void PrintIndented(const char* text) {
    while (*text != '\0') {
        const char* end = std::strchr(text, '\n');
        const int n = end ? static_cast<int>(end - text) : static_cast<int>(std::strlen(text));
        std::printf("   %.*s\n", n, text);
        text += n + (end ? 1 : 0);
    }
}

// Text straight onto the picture on screen, with no allocation (the
// crash may have happened inside malloc).
void ShowOnScreen() {
    if (g_phase == CrashPhase::Menu) {
        MenuHaltForCrash();
        void* xfb = Menu_CurrentXfb();
        const int width = Menu_XfbWidth();
        const int height = Menu_XfbHeight();
        CON_Init(xfb, 0, 0, width, height, width * VI_DISPLAY_PIX_SZ);
        VIDEO_SetNextFramebuffer(xfb);
        VIDEO_SetBlack(false);
        VIDEO_Flush();
        std::printf("\x1b[37m\x1b[44m\x1b[2J\x1b[2;0H");
    } else if (g_phase == CrashPhase::Console) {
        std::printf("\n\n");
    } else {
        return;  // no picture yet
    }
    PrintIndented("RiftWii stopped because of a bug. This report helps fix it:\n\n");
    PrintIndented(g_report);
    if (g_phase == CrashPhase::Menu) GuiScriptCrashShot(Menu_CurrentXfb(), Menu_XfbWidth(), Menu_XfbHeight());
}

[[noreturn]] void CrashContinue() {
    LogEchoToScreen(false);
    BuildReport();
    ShowOnScreen();
    const bool loop = CurrentRestartNote().kind == RestartKind::Crashed &&
                      ticks_to_secs(gettime() - g_started) < kCrashLoopSeconds;
    if (g_phase != CrashPhase::Early) {
        PrintIndented("\nSaving this to sd:/riftwii/crash.txt...");
    }
    // The open log gets it line by line (logf takes 1 KB at a time).
    const char* line = g_report;
    while (*line != '\0') {
        const char* end = std::strchr(line, '\n');
        const std::size_t n = end ? static_cast<std::size_t>(end - line) : std::strlen(line);
        logf("%.*s\n", static_cast<int>(n), line);
        line += n + (end ? 1 : 0);
    }
    if (FILE* f = std::fopen("sd:/riftwii/crash.txt", "w")) {
        std::fputs(g_report, f);
        std::fclose(f);
    }
    char summary[160];
    std::snprintf(summary, sizeof(summary), "RiftWii restarted after a crash (%s at %08X). Details: sd:/riftwii/crash.txt",
                  ExceptionName(g_info.exid), g_info.pc);
    if (g_phase != CrashPhase::Early) {
        PrintIndented("Saved. To get help, send crash.txt and session.log from the\n"
                      "riftwii folder on your SD card (put the card in a PC or phone).\n");
        PrintIndented(loop ? "It crashed again right after restarting: press A to leave."
                           : "A or RESET: start RiftWii again.\nHOME: leave to the Homebrew Channel.");
    }
    const ExitChoice choice =
        g_phase == CrashPhase::Early ? ExitChoice::Restart : WaitForChoice(kAutoRestartSeconds);
    if (!loop && choice == ExitChoice::Restart) WarmRestart(RestartKind::Crashed, summary, false);
    std::exit(0);
}

// libogc's panic hook, in exception context: note the registers, then let
// the handler resume the crashed thread in CrashContinue on g_stack.
void RiftPanic(unsigned exid, PPCContext* ctx) {
    if (g_crashing) {
        for (;;) {
        }
    }
    g_crashing = 1;
    g_info.exid = exid;
    g_info.pc = ctx->pc;
    g_info.lr = ctx->lr;
    g_info.msr = ctx->msr;
    g_info.cr = ctx->cr;
    g_info.ctr = ctx->ctr;
    asm volatile("mfspr %0, 19" : "=r"(g_info.dar));
    asm volatile("mfspr %0, 18" : "=r"(g_info.dsisr));
    std::memcpy(g_info.gpr, ctx->gpr, sizeof(g_info.gpr));
    u32 sda2, sda;
    asm volatile("mr %0, 2" : "=r"(sda2));
    asm volatile("mr %0, 13" : "=r"(sda));
    u32* top = reinterpret_cast<u32*>(g_stack + sizeof(g_stack) - 16);
    top[0] = 0;  // the back chain ends here
    ctx->gpr[1] = reinterpret_cast<u32>(top);
    ctx->gpr[2] = sda2;
    ctx->gpr[13] = sda;
    ctx->lr = 0;
    ctx->pc = reinterpret_cast<u32>(&CrashContinue);
    ctx->msr |= 0x8000 | 0x30 | 0x2;  // EE, IR and DR, RI
}

}  // namespace

void CrashInstall() {
    g_started = gettime();
    PPCExcptCurPanicFn = RiftPanic;
}

void CrashSetPhase(CrashPhase phase) { g_phase = phase; }

}  // namespace riftwii::wii
