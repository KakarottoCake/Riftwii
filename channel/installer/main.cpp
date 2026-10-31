// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The RiftWii channel installer: a Homebrew Channel app of its own
// (apps/riftwii_channel), shipped beside RiftWii in the release zip and
// started from RiftWii's Settings. It holds the channel package
// (tools/make_channel.py), so RiftWii itself carries none of it.

#include <gccore.h>
#include <fat.h>
#include <ogc/es.h>
#include <ogc/isfs.h>
#include <ogc/machine/processor.h>
#include <sdcard/wiisd_io.h>
#include <wiiuse/wpad.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>

#include "cios.hpp"
#include "dolboot.h"
#include "install.hpp"

namespace {

constexpr const char* kRiftWii = "sd:/apps/riftwii/boot.dol";
constexpr const char* kLogPath = "sd:/riftwii/channel.log";
constexpr const char* kInstallerMarker = "sd:/riftwii/channel_installer_opened.txt";

GXRModeObj* g_mode;
void* g_xfb;
bool g_sd;
bool g_pads;
int g_cios;  // the cIOS slot this runs under, 0 if none
// Whether g_cios was read and found to be d2x. False after the fallback
// that tries slots by ticket when no slot could be read: the install is
// still offered there, but the screen doesn't claim a d2x.
bool g_cios_identified;
bool g_ios_state_safe = true;
// Started by RiftWii's update prompt with "update": the player chose Update
// there already, so nothing here waits for a button. This installer reads
// only the Wii Remote's own buttons and the Wii's own GameCube ports, and a
// Wii U's vWii has no GameCube ports: a player with only a GameCube
// controller on an adapter could press nothing here (31 Oct, on a Wii U).
bool g_update;

enum Button { kNone, kInstall, kRemove, kBack };
Button wait_button();
void leave();

bool wait_for_release() {
    // Allow a newly connected Wii Remote time to report its held buttons.
    // A stuck controller must not trap the player or accept the fallback.
    for (unsigned frame = 0; frame < 180; ++frame) {
        VIDEO_WaitVSync();
        WPAD_ScanPads();
        PAD_ScanPads();
        bool held = SYS_ResetButtonDown();
        for (int channel = 0; channel < 4; ++channel) {
            held = held || WPAD_ButtonsHeld(channel) != 0 || PAD_ButtonsHeld(channel) != 0;
        }
        if (frame >= 60 && !held) return true;
    }
    return false;
}

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

void mount_sd() {
    g_sd = fatMountSimple("sd", &__io_wiisd);
    if (g_sd) mkdir("sd:/riftwii", 0777);
}

void init_pads() {
    if (g_pads) return;
    WPAD_Init();
    PAD_Init();
    g_pads = true;
}

// Keeps what's on screen up for a while, so the player can read it.
void show_for_seconds(int seconds) {
    for (int frame = 0; frame < seconds * 60; ++frame) VIDEO_WaitVSync();
}

void shutdown_pads() {
    if (!g_pads) return;
    WPAD_Shutdown();
    g_pads = false;
}

bool reload_cios(int slot, const riftwii::D2xInfo& info) {
    installer::Log("trying IOS%d\n", slot);
    if (info.d2x) std::printf("  trying IOS%d (%s, base %d)\n", slot, riftwii::d2x_name(info).c_str(), info.base);
    else std::printf("  trying IOS%d (not identified)\n", slot);
    VIDEO_WaitVSync();
    shutdown_pads();
    if (g_sd) fatUnmount("sd:");
    __io_wiisd.shutdown();
    g_sd = false;
    const s32 result = IOS_ReloadIOS(slot);
    // Like leave(): shutting down before the reload lets libfat's next
    // mount start the SD interface under the new IOS.
    mount_sd();
    if (result < 0 || IOS_GetVersion() != slot) {
        installer::Log("Reloading IOS%d failed (%d); IOS%d revision %d is running.\n", slot, result,
                       IOS_GetVersion(), IOS_GetRevision());
        std::printf("  Reloading IOS%d failed (%d).\n", slot, result);
        return false;
    }
    g_cios = slot;
    g_cios_identified = info.d2x;
    installer::Log("The installer is running on IOS%d revision %d.\n", IOS_GetVersion(), IOS_GetRevision());
    return true;
}

// Its signature patches let the fakesigned channel in. Identify all
// candidates before a reload drops the Homebrew Channel's AHBPROT access.
void enter_cios() {
    constexpr int slots[] = {249, 250, 251};
    riftwii::D2xInfo candidates[3];
    bool any_read = false;
    const s32 isfs = ISFS_Initialize();
    if (isfs < 0) {
        installer::Log("The cIOS search could not open the NAND file system (%d).\n", isfs);
    } else {
        for (unsigned i = 0; i < 3; ++i) {
            bool read = false;
            bool restored = true;
            candidates[i] = installer::ReadCios(slots[i], read, restored);
            if (!restored) {
                g_ios_state_safe = false;
                ISFS_Deinitialize();
                installer::Log("The installer cannot continue safely because NAND access was not restored. "
                               "No IOS will be reloaded; restart the installer from the Homebrew Channel.\n");
                return;
            }
            any_read = any_read || read;
        }
        ISFS_Deinitialize();
    }

    // Nothing could be read (no hardware access: started from a RiftWii
    // running on a cIOS, or IOS's NAND check could not be opened). The
    // player may choose the old ticket fallback after an explicit warning.
    // Each attempt is logged first, so a hang still leaves its slot behind.
    if (!any_read) {
        if (g_update) {
            // No one to ask: the safe answer. Without a cIOS the update screen
            // says why before going back; RiftWii then says the channel is
            // still the old version, and the installer can be opened by hand.
            installer::Log("No cIOS slot could be checked; in update mode the unchecked ticket fallback is not tried.\n");
            return;
        }
        installer::Log("No cIOS slot could be checked; waiting for permission to try slots with a ticket.\n");
        std::printf("\n  An unchecked cIOS can freeze the console; if it does, hold\n"
                    "  POWER to turn it off.\n\n");
        std::printf("  No cIOS slot could be checked. Try the slots with a ticket\n  anyway?\n\n");
        std::printf("  [A] Try     [B] Go back\n");
        // WPAD belongs to its IOS. Keep it if no ticket can be tried;
        // reload_cios tears it down immediately before an actual reload.
        init_pads();
        if (!wait_for_release()) {
            installer::Log("Buttons were still held after waiting for release; no unchecked cIOS was reloaded.\n");
            leave();
        }
        for (;;) {
            const Button button = wait_button();
            if (button == kBack) {
                installer::Log("The player declined the unchecked ticket fallback; no cIOS was reloaded.\n");
                leave();
            }
            if (button == kInstall) break;
        }
        installer::Log("The player accepted the unchecked ticket fallback; trying slots with a ticket.\n");
        for (int slot : slots) {
            u32 views = 0;
            if (ES_GetNumTicketViews(0x0000000100000000ull | slot, &views) < 0 || views == 0) continue;
            if (reload_cios(slot, riftwii::D2xInfo{})) return;
        }
        installer::Log("No cIOS in slots 249, 250 or 251 could be started.\n");
        return;
    }

    // Prefer base 58 for the SD card; bases 56 and 57 and other d2x bases
    // remain candidates. Only positively identified d2x slots are tried.
    installer::Log("The cIOS search read slot information; only identified d2x slots will be tried.\n");
    for (bool base58_pass : {true, false}) {
        for (unsigned i = 0; i < 3; ++i) {
            const auto& info = candidates[i];
            if (!info.d2x) continue;
            const bool has_base58 = info.base == 58;
            if (has_base58 != base58_pass) continue;
            if (reload_cios(slots[i], info)) return;
        }
    }
    installer::Log("No identified d2x cIOS in slots 249, 250 or 251 could be started.\n");
}

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
    const char* console = installer::OnVWii() ? "Wii U (vWii)" : "Wii";
    if (g_cios && g_cios_identified) std::printf("  Running on:   %s, IOS%d (d2x cIOS)\n\n", console, g_cios);
    else if (g_cios) std::printf("  Running on:   %s, IOS%d (cIOS, not checked for d2x)\n\n", console, g_cios);
    else std::printf("  Running on:   %s, IOS%d\n\n", console, IOS_GetVersion());
    if (!status.empty()) std::printf("  %s\n\n", status.c_str());
    else std::printf("\n\n");
    if (g_cios) {
        std::printf("  \x1b[36;1m[A]\x1b[37;0m      %s\n", !present ? "Install" : installed < ours ? "Update" : "Install again");
        if (present) std::printf("  \x1b[36;1m[-]\x1b[37;0m      Remove            (Y on a GameCube controller)\n");
    }
    const char* back = !g_ios_state_safe ? "Back to the Homebrew Channel"
                       : riftwii_on_card() ? "Back to RiftWii" : "Back";
    std::printf("  \x1b[36;1m[HOME]\x1b[37;0m   %s   (B or START)\n\n", back);
    if (!note.empty()) std::printf("  %s\n", note.c_str());
}

void leave() {
    installer::Log("Leaving\n");
    // RiftWii is cold-started below, without a ChannelDone note. Preserve
    // unsuccessful attempts so its next start still offers Not now.
    if (g_sd) {
        unsigned installed = 0;
        const bool current = g_ios_state_safe && installer::Installed(installed) && installed >= installer::PackageVersion();
        if (current) {
            if (std::remove(kInstallerMarker) == 0) {
                installer::Log("The installer launch marker was cleared because the channel is up to date.\n");
            }
        } else {
            bool saved = false;
            if (FILE* marker = std::fopen(kInstallerMarker, "w")) {
                const bool wrote = std::fputs("returned\n", marker) >= 0;
                const bool closed = std::fclose(marker) == 0;
                saved = wrote && closed;
            }
            if (saved) installer::Log("The installer return was recorded before unmounting the SD card.\n");
            else installer::Log("The installer return could not be recorded on the SD card.\n");
        }
    }
    const bool back = g_ios_state_safe && riftwii_on_card();
    shutdown_pads();
    if (g_sd) {
        fatUnmount("sd:");
        __io_wiisd.shutdown();
        g_sd = false;
    }
    if (back) {
        // IOS 58 first, as the Homebrew Channel starts RiftWii, and only
        // then RiftWii read: a reload overwrites the bottom of MEM2, where
        // it is read to (RiftWii came back with its code damaged).
        IOS_ReloadIOS(58);
        if (fatMountSimple("sd", &__io_wiisd)) {
            u32 size = 0;
            u8* dol = dolboot_read(kRiftWii, &size, [](u32 n) -> void* {
                // MEM2, off the arena directly: RiftWii lands in MEM1.
                const u32 lo = (reinterpret_cast<u32>(SYS_GetArena2Lo()) + 31) & ~31u;
                if (lo + n > reinterpret_cast<u32>(SYS_GetArena2Hi())) return nullptr;
                SYS_SetArena2Lo(reinterpret_cast<void*>(lo + ((n + 31) & ~31u)));
                return reinterpret_cast<void*>(lo);
            }, [](void* data) { SYS_SetArena2Lo(data); });
            fatUnmount("sd:");
            __io_wiisd.shutdown();
            if (dol && dolboot_valid(dol, size)) dolboot_run(dol, kRiftWii);
        }
    }
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

int main(int argc, char** argv) {
    const bool dolphin = in_dolphin();
    for (int i = 1; i < argc; ++i)
        if (argv[i] && std::string(argv[i]) == "update") g_update = true;
    video_init();
    mount_sd();
    installer::Log("RiftWii Channel Installer %u, IOS%d revision %d\n", installer::PackageVersion(),
                   IOS_GetVersion(), IOS_GetRevision());
    if (g_update) installer::Log("Started by RiftWii's update prompt: updating without waiting for a button.\n");
    installer::Log("The starting AHBPROT register is 0x%08x.\n", static_cast<unsigned>(read32(0x0D800064)));
    std::printf("  RiftWii Channel Installer: checking d2x cIOS slots...\n");
    if (!dolphin) enter_cios();
    else installer::Log("Dolphin was detected; the cIOS search and reloads were skipped.\n");
    init_pads();

    std::string status, note;
    if (!g_ios_state_safe) {
        note = "\x1b[31mThe installer can't continue safely: NAND access was not\n"
               "  restored. Restart it from the Homebrew Channel.\x1b[37m";
    } else if (dolphin) {
        note = "Dolphin checks real signatures, so the channel can only be\n  installed on a Wii.";
    } else if (!g_cios) {
        note = "\x1b[33mThis needs a d2x cIOS in slot 249, 250 or 251 (the one\n  RiftWii uses).\x1b[37m";
    }
    if (g_update) {
        // The update the player already chose in RiftWii, then the result on
        // screen for a few seconds and back to RiftWii, all without a button.
        if (g_ios_state_safe && !dolphin && g_cios) {
            screen("\x1b[36mUpdating...\x1b[37m", "");
            std::string error;
            const bool updated = installer::Install(error);
            status = updated ? "\x1b[32mDone. The RiftWii channel is on the Wii Menu.\x1b[37m"
                             : "\x1b[31mNot updated: " + error + ".\x1b[37m Nothing else on the Wii changed.";
            installer::Log("Update mode: %s.\n", updated ? "the channel was updated" : error.c_str());
        } else {
            installer::Log("Update mode: the channel could not be updated here.\n");
        }
        screen(status, note + (note.empty() ? "" : "\n\n  ") + "Going back to RiftWii...");
        show_for_seconds(4);
        leave();
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
