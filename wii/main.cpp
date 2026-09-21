// SPDX-License-Identifier: GPL-3.0-or-later
#include <fat.h>
#include <gccore.h>
#include <ogc/system.h>
#include <unistd.h>
#include <cstdlib>
#include <string>
#include <vector>

#include "FreeTypeGX.h"
#include "audio.h"
#include "input.h"
#include "menu.h"
#include "video.h"
#include "font_ttf.h"

#include "autorun.hpp"
#include "console.hpp"
#include "log.hpp"

int ExitRequested = 0;

void ExitApp() {
    ShutoffRumble();
    ShutdownAudio();
    StopGX();
    std::exit(0);
}

namespace {

// Leaves the libwiigui renderer and shows the text console for the
// disc phase (the GUI thread is already halted by MainMenu).
void EnterConsolePhase() {
    ShutoffRumble();
    ShutdownAudio();
    StopGX();
    riftwii::wii::ConsoleStart(true);
}

}  // namespace

int main() {
    // The game's apploader is loaded at 0x81200000 and its DOL fills MEM1
    // from 0x80004000 up; this loader is linked at 0x80A00000 (see
    // Makefile.wii) and keeps its heap below the apploader.
    SYS_SetArena1Hi(reinterpret_cast<void*>(0x81200000));
    fatInitDefault();

    if (riftwii::wii::AutorunPresent()) {
        riftwii::wii::ConsoleStart(false);
        riftwii::wii::RunAutorun();
        riftwii::wii::WaitForExit();
        std::exit(0);
    }

    // Probe the available sources before drawing the source selector.
    FrontendState state;
    riftwii::wii::IdentifyDisc(state);

    InitVideo();
    SetupPads();
    InitAudio();
    InitFreeType(const_cast<u8*>(font_ttf), font_ttf_size);
    InitGUIThreads();
    const int action = MainMenu(MENU_SOURCE, state);
    const riftwii::wii::LaunchSource source = riftwii::wii::SelectedSource(state);

    EnterConsolePhase();
    std::string error;
    if (action == MENU_LAUNCH) {
        riftwii::wii::LogOpen("sd:/riftwii/boot.log");
        riftwii::wii::logf("Riftwii: launch %s with packages\n", state.game_id.c_str());
        const bool booted = (source.kind == riftwii::wii::LaunchSource::Kind::Disc && state.has_compiled)
                                ? riftwii::wii::BootCompiled(state.compiled, error, source, state.model.save_mode,
                                                             state.game_id)
                                : riftwii::wii::RunLaunch(state.model.selections(), error, source,
                                                          state.model.save_mode, state.game_id);
        if (!booted) {
            riftwii::wii::LogOpen("sd:/riftwii/boot.log", true);  // boot_game closed it and remounted the card
            riftwii::wii::logf("FAILED: %s\n", error.c_str());
        }
    } else if (action == MENU_BOOT) {
        riftwii::wii::LogOpen("sd:/riftwii/boot.log");
        riftwii::wii::logf("Riftwii: boot %s\n", source.kind == riftwii::wii::LaunchSource::Kind::Usb ? "USB" : source.kind == riftwii::wii::LaunchSource::Kind::Sd ? "SD" : "disc");
        if (!riftwii::wii::RunBoot(true, error, source)) {
            riftwii::wii::LogOpen("sd:/riftwii/boot.log", true);  // boot_game closed it and remounted the card
            riftwii::wii::logf("FAILED: %s\n", error.c_str());
        }
    } else if (action == MENU_DUMP) {
        riftwii::wii::LogOpen("sd:/riftwii/dump.log");
        riftwii::wii::logf("Riftwii: dump test files\n");
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
