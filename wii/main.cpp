// SPDX-License-Identifier: GPL-3.0-or-later
#include <fat.h>
#include <gccore.h>
#include <ogc/system.h>
#include <sdcard/wiisd_io.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <string>
#include <vector>

#include "FreeTypeGX.h"
#include "audio.h"
#include "input.h"
#include "menu.h"
#include "video.h"
#include "rounded_ttf.h"

#include "autorun.hpp"
#include "console.hpp"
#include "guiscript.hpp"
#include "i18n.hpp"
#include "ios_reload.hpp"
#include "loadersettings.hpp"
#include "log.hpp"
#include "menuios.hpp"

int ExitRequested = 0;

void ExitApp() {
    ShutoffRumble();
    ShutdownAudio();
    StopGX();
    std::exit(0);
}

namespace {

// Leaves the libwiigui renderer for the disc phase (the GUI thread is
// already halted by MainMenu): its last frame, the launch screen, stays
// up and the log prints into the white card on it.
void EnterConsolePhase() {
    ShutoffRumble();
    ShutdownAudio();
    StopGXKeepPicture();
    riftwii::wii::ConsoleStartInFrame(Menu_CurrentXfb(), Menu_XfbWidth(), Menu_XfbHeight(), 48, 176, 544, 224);  // whole 8x16 cells
}

// libfat's default initializer probes USB as well as SD. Mount only the SD
// card here so autorun and the normal SD-backed package paths work, while USB
// remains untouched until the user explicitly selects it from the source menu.
bool MountStartupSd() {
    return __io_wiisd.startup() && __io_wiisd.isInserted() && fatMountSimple("sd", &__io_wiisd);
}

// The menu phase's log: every scan, probe and failure from startup until a
// launch opens boot.log. Each line is synced to the card, so after a hang
// its last line names the step that never finished.
void OpenSessionLog(bool sd_mounted) {
    if (!sd_mounted) return;
    mkdir("sd:/riftwii", 0777);
    riftwii::wii::LogOpen("sd:/riftwii/session.log");
    riftwii::wii::logf("RiftWii %s on %s, IOS%d rev %d\n", RIFTWII_VERSION,
                       riftwii::wii::running_in_dolphin() ? "Dolphin" : "Wii", IOS_GetVersion(), IOS_GetRevision());
    riftwii::wii::logf("Memory: MEM1 heap 0x%08x-0x%08x, MEM2 heap 0x%08x-0x%08x\n",
                       reinterpret_cast<u32>(SYS_GetArena1Lo()), reinterpret_cast<u32>(SYS_GetArena1Hi()),
                       reinterpret_cast<u32>(SYS_GetArena2Lo()), reinterpret_cast<u32>(SYS_GetArena2Hi()));
}

}  // namespace

// The start of MEM2 an IOS reload may use (see main).
constexpr u32 kMem2Reserved = 0x90800000;

int main() {
    // The game's apploader is loaded at 0x81200000 and its DOL fills MEM1
    // from 0x80004000 up; this loader is linked at 0x80A00000 (see
    // Makefile.wii) and keeps its heap below the apploader.
    SYS_SetArena1Hi(reinterpret_cast<void*>(0x81200000));
    // An IOS reload (Menu IOS, a USB or SD game's cIOS, a disc game's own
    // IOS) stages the new kernel in low MEM2 and overwrites it: with the
    // 1.0.5 font the heap outgrew MEM1 and spilled there, and every launch
    // then crashed in malloc right after the reload (heap chunks at
    // 0x9011E000-0x9014C500 destroyed). Nothing of ours lives below
    // kMem2Reserved: the heap and the menu's textures start above it.
    if (reinterpret_cast<u32>(SYS_GetArena2Lo()) < kMem2Reserved) {
        SYS_SetArena2Lo(reinterpret_cast<void*>(kMem2Reserved));
    }
    const bool sd_mounted = MountStartupSd();

    if (riftwii::wii::AutorunPresent()) {
        riftwii::wii::ConsoleStart(false);
        riftwii::wii::RunAutorun();
        if (riftwii::wii::reload_terminal_failure()) riftwii::wii::halt_after_terminal_reload();
        riftwii::wii::WaitForExit();
        std::exit(0);
    }

    // The source screen must be visible before touching a potentially slow
    // image device or physical drive. Each source probes only on selection.
    OpenSessionLog(sd_mounted);
    // A chosen cIOS (fakemote's USB pads) must be running before the pads
    // and the drives are brought up.
    riftwii::wii::StartMenuIos(sd_mounted);
    FrontendState state;
    riftwii::wii::InitializeFrontend(state);
    riftwii::wii::SetMenuLanguage(riftwii::wii::MenuLanguage());

    InitVideo();
    SetupPads();
    InitAudio();
    InitFreeType(const_cast<u8*>(rounded_ttf), rounded_ttf_size);
    InitGUIThreads();
    const int action = MainMenu(MENU_SOURCE, state);
    const riftwii::wii::LaunchSource source = riftwii::wii::SelectedSource(state);

    EnterConsolePhase();
    std::string error;
    if (action == MENU_LAUNCH) {
        riftwii::wii::LogOpen("sd:/riftwii/boot.log");
        riftwii::wii::logf("RiftWii %s: launch %s with packages\n", RIFTWII_VERSION, state.game_id.c_str());
        const bool booted = (source.kind == riftwii::wii::LaunchSource::Kind::Disc && state.has_compiled)
                                ? riftwii::wii::BootCompiled(state.compiled, error, source, state.model.save_mode,
                                                             state.game_id)
                                : riftwii::wii::RunLaunch(state.model.selections(), error, source,
                                                          state.model.save_mode, state.game_id);
        if (!booted) {
            if (riftwii::wii::reload_terminal_failure()) riftwii::wii::halt_after_terminal_reload();
            riftwii::wii::LogOpen("sd:/riftwii/boot.log", true);  // boot_game closed it and remounted the card
            riftwii::wii::logf("FAILED: %s\n", error.c_str());
        }
    } else if (action == MENU_BOOT) {
        riftwii::wii::LogOpen("sd:/riftwii/boot.log");
        riftwii::wii::logf("RiftWii %s: boot %s\n", RIFTWII_VERSION, source.kind == riftwii::wii::LaunchSource::Kind::Usb ? "USB" : source.kind == riftwii::wii::LaunchSource::Kind::Sd ? "SD" : "disc");
        if (!riftwii::wii::RunBoot(true, error, source)) {
            if (riftwii::wii::reload_terminal_failure()) riftwii::wii::halt_after_terminal_reload();
            riftwii::wii::LogOpen("sd:/riftwii/boot.log", true);  // boot_game closed it and remounted the card
            riftwii::wii::logf("FAILED: %s\n", error.c_str());
        }
    } else if (action == MENU_DUMP) {
        riftwii::wii::LogOpen("sd:/riftwii/dump.log");
        riftwii::wii::logf("RiftWii: dump test files\n");
        riftwii::wii::GuiScriptFinalShot(Menu_CurrentXfb(), Menu_XfbWidth(), Menu_XfbHeight());
        const std::vector<std::string> files = {"/opening.bnr"};
        if (riftwii::wii::RunDump(files, "sd:/riftwii/dump", error)) {
            riftwii::wii::logf("Done.\n");
        } else {
            riftwii::wii::logf("FAILED: %s\n", error.c_str());
        }
    }
    riftwii::wii::LogClose();
    riftwii::wii::logf("Press HOME, Start or RESET to exit.\n");
    riftwii::wii::WaitForExit();
    std::exit(0);
    return 0;
}
