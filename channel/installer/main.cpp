// SPDX-License-Identifier: GPL-3.0-or-later
//
// The RiftWii channel installer: a Homebrew Channel app of its own
// (apps/riftwii_channel), shipped beside RiftWii in the release zip and
// started from RiftWii's Settings. It holds the channel package
// (tools/make_channel.py), so RiftWii itself carries none of it.

#include <gccore.h>
#include <fat.h>
#include <ogc/es.h>
#include <sdcard/wiisd_io.h>
#include <wiiuse/wpad.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>

#include "dolboot.h"
#include "install.hpp"

namespace {

constexpr const char* kRiftWii = "sd:/apps/riftwii/boot.dol";
constexpr const char* kLogPath = "sd:/riftwii/channel.log";

GXRModeObj* g_mode;
void* g_xfb;
bool g_sd;
int g_cios;  // the d2x slot this runs under, 0 if none

void video_init() {
    VIDEO_Init();
    g_mode = VIDEO_GetPreferredMode(nullptr);
    g_xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(g_mode));
    CON_Init(g_xfb, 20, 20, g_mode->fbWidth, g_mode->xfbHeight, g_mode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(g_mode);
    VIDEO_SetNextFramebuffer(g_xfb);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (g_mode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
}

bool in_dolphin() {
    const s32 fd = IOS_Open("/dev/dolphin", 0);
    if (fd >= 0) IOS_Close(fd);
    return fd >= 0;
}

// A d2x cIOS in 249, 250 or 251, before anything else is opened: its
// signature patches are what let the fakesigned channel in.
void enter_cios() {
    for (int slot : {249, 250, 251}) {
        u32 views = 0;
        if (ES_GetNumTicketViews(0x0000000100000000ull | slot, &views) < 0 || views == 0) continue;
        if (IOS_ReloadIOS(slot) >= 0 && IOS_GetVersion() == slot) {
            g_cios = slot;
            return;
        }
    }
}

enum Button { kNone, kInstall, kRemove, kBack };

Button read_button() {
    WPAD_ScanPads();
    PAD_ScanPads();
    const u32 w = WPAD_ButtonsDown(0);
    const u32 g = PAD_ButtonsDown(0);
    if ((w & WPAD_BUTTON_A) || (g & PAD_BUTTON_A)) return kInstall;
    if ((w & WPAD_BUTTON_MINUS) || (g & PAD_BUTTON_Y)) return kRemove;
    if ((w & (WPAD_BUTTON_HOME | WPAD_BUTTON_B)) || (g & (PAD_BUTTON_B | PAD_BUTTON_START))) return kBack;
    if (SYS_ResetButtonDown()) return kBack;
    return kNone;
}

Button wait_button() {
    for (;;) {
        const Button b = read_button();
        if (b != kNone) return b;
        VIDEO_WaitVSync();
    }
}

bool riftwii_on_card() {
    struct stat st;
    return g_sd && stat(kRiftWii, &st) == 0;
}

// Colours: 36 cyan, 37 white, 33 yellow, 31 red, 32 green; 1 bright.
void screen(const std::string& status, const std::string& note) {
    unsigned installed = 0;
    const bool present = installer::Installed(installed);
    const unsigned ours = installer::PackageVersion();
    std::printf("\x1b[2J\x1b[2;0H");
    std::printf("\x1b[36;1m  RiftWii Channel Installer\x1b[37;0m                          version %u\n", ours);
    std::printf("  \x1b[36m--------------------------------------------------------------\x1b[37m\n\n");
    std::printf("  Puts a RiftWii channel on the Wii Menu. The channel holds no\n");
    std::printf("  copy of RiftWii: it starts apps/riftwii/boot.dol from the SD card\n");
    std::printf("  (or the USB drive), so RiftWii's own updates never need it\n");
    std::printf("  installed again.\n\n");
    std::printf("  On this Wii:  ");
    if (!present) std::printf("\x1b[33mnot installed\x1b[37m\n");
    else if (installed < ours) std::printf("\x1b[33mversion %u (this installer has %u)\x1b[37m\n", installed, ours);
    else std::printf("\x1b[32minstalled\x1b[37m (version %u)\n", installed);
    if (g_cios) std::printf("  Running on:   IOS%d (d2x cIOS)\n\n", g_cios);
    else std::printf("  Running on:   IOS%d\n\n", IOS_GetVersion());
    if (!status.empty()) std::printf("  %s\n\n", status.c_str());
    else std::printf("\n\n");
    if (g_cios) {
        std::printf("  \x1b[36;1m[A]\x1b[37;0m      %s\n", !present ? "Install" : installed < ours ? "Update" : "Install again");
        if (present) std::printf("  \x1b[36;1m[-]\x1b[37;0m      Remove            (Y on a GameCube controller)\n");
    }
    std::printf("  \x1b[36;1m[HOME]\x1b[37;0m   %s   (B or START)\n\n", riftwii_on_card() ? "Back to RiftWii" : "Back");
    if (!note.empty()) std::printf("  %s\n", note.c_str());
}

void leave() {
    installer::Log("Leaving\n");
    WPAD_Shutdown();
    if (riftwii_on_card()) {
        u32 size = 0;
        u8* dol = dolboot_read(kRiftWii, &size, [](u32 n) -> void* {
            // MEM2, off the arena directly: RiftWii lands in MEM1.
            const u32 lo = (reinterpret_cast<u32>(SYS_GetArena2Lo()) + 31) & ~31u;
            if (lo + n > reinterpret_cast<u32>(SYS_GetArena2Hi())) return nullptr;
            SYS_SetArena2Lo(reinterpret_cast<void*>(lo + ((n + 31) & ~31u)));
            return reinterpret_cast<void*>(lo);
        });
        if (dol && dolboot_valid(dol, size)) {
            fatUnmount("sd:");
            __io_wiisd.shutdown();
            IOS_ReloadIOS(58);  // as the Homebrew Channel starts it
            dolboot_run(dol, kRiftWii);
        }
    }
    if (g_sd) fatUnmount("sd:");
    std::exit(0);  // back to the Homebrew Channel
}

}  // namespace

namespace installer {

void Log(const char* format, ...) {
    char line[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (!g_sd) return;
    if (FILE* f = std::fopen(kLogPath, "a")) {
        std::fputs(line, f);
        std::fclose(f);
    }
}

}  // namespace installer

int main() {
    const bool dolphin = in_dolphin();
    if (!dolphin) enter_cios();
    video_init();
    WPAD_Init();
    PAD_Init();
    g_sd = fatMountSimple("sd", &__io_wiisd);
    if (g_sd) mkdir("sd:/riftwii", 0777);
    installer::Log("RiftWii Channel Installer %u, IOS%d\n", installer::PackageVersion(), IOS_GetVersion());

    std::string status, note;
    if (dolphin) {
        note = "Dolphin checks real signatures, so the channel can only be\n  installed on a Wii.";
    } else if (!g_cios) {
        note = "\x1b[33mThis needs a d2x cIOS in slot 249, 250 or 251 (the one\n  RiftWii uses).\x1b[37m";
    }
    for (;;) {
        screen(status, note);
        const Button b = wait_button();
        if (b == kBack) leave();
        if (!g_cios) continue;
        unsigned installed = 0;
        const bool present = installer::Installed(installed);
        std::string error;
        if (b == kInstall) {
            screen("\x1b[36mInstalling...\x1b[37m", "");
            status = installer::Install(error)
                         ? "\x1b[32mDone. The RiftWii channel is on the Wii Menu.\x1b[37m"
                         : "\x1b[31mNot installed: " + error + ".\x1b[37m Nothing else on the Wii changed.";
        } else if (b == kRemove && present) {
            screen("Remove the RiftWii channel from the Wii Menu?", "Press [-] (or Y) again to remove it, anything else to keep it.");
            if (wait_button() != kRemove) {
                status.clear();
                continue;
            }
            status = installer::Remove(error) ? "\x1b[32mRemoved from the Wii Menu.\x1b[37m"
                                              : "\x1b[31mNot removed: " + error + ".\x1b[37m";
        }
    }
}
