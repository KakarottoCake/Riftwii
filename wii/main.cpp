// SPDX-License-Identifier: GPL-3.0-or-later
#include <fat.h>
#include <gccore.h>
#include <ogc/system.h>
#include <sdcard/wiisd_io.h>
#include <sys/stat.h>
#include <unistd.h>
#include <brotli/decode.h>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

#include "FreeTypeGX.h"
#include "audio.h"
#include "input.h"
#include "menu.h"
#include "video.h"
#include "menufont_bin.h"

#include "autorun.hpp"
#include "console.hpp"
#include "crash.hpp"
#include "gcadapter.hpp"
#include "guiscript.hpp"
#include "i18n.hpp"
#include "ios_reload.hpp"
#include "loadersettings.hpp"
#include "log.hpp"
#include "memlimits.hpp"
#include "menuios.hpp"
#include "online.hpp"
#include "progress.hpp"
#include "restart.hpp"
#include "skin.hpp"

int ExitRequested = 0;

void ExitApp() {
    riftwii::wii::GcAdapterMenuEnd();
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
    riftwii::wii::ConsoleStartInFrame(Menu_CurrentXfb(), Menu_XfbWidth(), Menu_XfbHeight(), 48, 176, 544, 208);  // whole 8x16 cells
    riftwii::wii::CrashSetPhase(riftwii::wii::CrashPhase::Console);
    // Under the log, still on the white card: the stage and the bar.
    riftwii::wii::ProgressAttach(Menu_CurrentXfb(), Menu_XfbWidth(), Menu_XfbHeight(), 48, 388, 544);
}

// After a launch that failed: back to Home (a fresh start, see
// wii/restart.hpp; also after two minutes untouched) or out to the
// Homebrew Channel.
void OfferRestart(const std::string& error) {
    // Players asking for help seldom know where the logs are: say it here,
    // where the failure is, in words a first-time user can follow.
    riftwii::wii::logf("\nTo get help, send boot.log and session.log. They are in the\n"
                       "riftwii folder on your SD card (put the card in a PC or phone).\n");
    if (!riftwii::wii::CanRestart()) return;
    riftwii::wii::logf("\nA: back to RiftWii   HOME: leave to the Homebrew Channel\n");
    if (riftwii::wii::WaitForChoice(120) != riftwii::wii::ExitChoice::Restart) std::exit(0);
    riftwii::wii::WarmRestart(riftwii::wii::RestartKind::LaunchFailed,
                              "The launch failed (for help, send sd:/riftwii/boot.log): " + error);
}

// libfat's default initializer probes USB as well as SD. Mount only the SD
// card here so autorun and the SD-backed package paths work; USB starts when
// Home reads the drives (wii/usbcatalog.cpp), after the menu IOS is up.
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
    riftwii::wii::mem::LogLimits();
    riftwii::wii::mem::LogUsage("start");
}

// The menu font ships brotli-compressed (tools/make_menu_font.py): a
// big-endian u32 of the TTF's size, then the stream. Unpacked into MEM2,
// where FreeType reads it for the whole menu phase, so neither the DOL nor
// the MEM1 heap carries the 1.7 MB TTF.
bool UnpackMenuFont(u8*& font, std::size_t& size) {
    if (menufont_bin_size < 4) return false;
    size = (std::size_t(menufont_bin[0]) << 24) | (menufont_bin[1] << 16) | (menufont_bin[2] << 8) | menufont_bin[3];
    font = riftwii::wii::skin::Mem2Alloc(size);
    if (font == nullptr) return false;
    std::size_t out = size;
    return BrotliDecoderDecompress(menufont_bin_size - 4, menufont_bin + 4, &out, font) == BROTLI_DECODER_RESULT_SUCCESS &&
           out == size;
}

}  // namespace

int main() {
    // Keeps the loader out of the memory the game's apploader and IOS
    // reloads overwrite (wii/memlimits.hpp).
    riftwii::wii::mem::Init();
    const riftwii::wii::RestartNote restart = riftwii::wii::TakeRestartNote();
    riftwii::wii::CrashInstall();
    const bool sd_mounted = MountStartupSd();

    if (riftwii::wii::AutorunPresent()) {
        riftwii::wii::ConsoleStart(false);
        riftwii::wii::CrashSetPhase(riftwii::wii::CrashPhase::Console);
        riftwii::wii::RunAutorun();
        if (riftwii::wii::reload_terminal_failure()) riftwii::wii::halt_after_terminal_reload();
        riftwii::wii::WaitForExit();
        std::exit(0);
    }

    // Home is on screen before the drives are read (a big USB drive takes a
    // while); the disc is only probed when its tile is picked.
    OpenSessionLog(sd_mounted);
    // A chosen cIOS (fakemote's USB pads) must be running before the pads
    // and the drives are brought up.
    if (restart.kind != riftwii::wii::RestartKind::None) {
        riftwii::wii::logf("Restarted: %s\n", restart.message.c_str());
    }
    riftwii::wii::StartMenuIos(sd_mounted, restart.kind != riftwii::wii::RestartKind::None);
    SetHomeNotice(restart.message);
    FrontendState state;
    riftwii::wii::InitializeFrontend(state);
    riftwii::wii::SetMenuLanguage(riftwii::wii::MenuLanguage());

    InitVideo();
    SetupPads();
    InitAudio();
    u8* font = nullptr;
    std::size_t font_size = 0;
    if (!UnpackMenuFont(font, font_size)) {
        // Only if MEM2 were already full: FreeType can't run without a face.
        riftwii::wii::logf("Menu font: unpacking failed\n");
        ExitApp();
    }
    InitFreeType(font, font_size);
    InitGUIThreads();
    riftwii::wii::CrashSetPhase(riftwii::wii::CrashPhase::Menu);
    const int action = MainMenu(MENU_SOURCE, state);
    // Before anything is launched: nothing of the menu's adapter may be
    // left in flight for the game (or the next IOS) to answer.
    riftwii::wii::GcAdapterMenuEnd();
    riftwii::wii::mem::LogUsage("menu closed");
    const riftwii::wii::LaunchSource source = riftwii::wii::SelectedSource(state);

    EnterConsolePhase();
    std::string error;
    if (action == MENU_LAUNCH) {
        riftwii::wii::LogOpen("sd:/riftwii/boot.log");
        riftwii::wii::logf("RiftWii %s: launch %s with packages\n", RIFTWII_VERSION, state.game_id.c_str());
        riftwii::wii::LogDeclinedUpdate();
        if (riftwii::wii::GuiScriptFailLaunch()) error = "a test failure the guiscript asked for";
        const bool booted = !error.empty() ? false : (source.kind == riftwii::wii::LaunchSource::Kind::Disc && state.has_compiled)
                                ? riftwii::wii::BootCompiled(state.compiled, error, source, state.model.save_mode,
                                                             state.game_id)
                                : riftwii::wii::RunLaunch(state.model.selections(), error, source,
                                                          state.model.save_mode, state.game_id);
        if (!booted) {
            if (riftwii::wii::reload_terminal_failure()) riftwii::wii::halt_after_terminal_reload();
            riftwii::wii::LogOpen("sd:/riftwii/boot.log", true);  // boot_game closed it and remounted the card
            riftwii::wii::logf("FAILED: %s\n", error.c_str());
            OfferRestart(error);
        }
    } else if (action == MENU_BOOT) {
        riftwii::wii::LogOpen("sd:/riftwii/boot.log");
        riftwii::wii::logf("RiftWii %s: boot %s\n", RIFTWII_VERSION, source.kind == riftwii::wii::LaunchSource::Kind::Usb ? "USB" : source.kind == riftwii::wii::LaunchSource::Kind::Sd ? "SD" : "disc");
        riftwii::wii::LogDeclinedUpdate();
        if (riftwii::wii::GuiScriptFailLaunch()) error = "a test failure the guiscript asked for";
        if (!error.empty() || !riftwii::wii::RunBoot(true, error, source)) {
            if (riftwii::wii::reload_terminal_failure()) riftwii::wii::halt_after_terminal_reload();
            riftwii::wii::LogOpen("sd:/riftwii/boot.log", true);  // boot_game closed it and remounted the card
            riftwii::wii::logf("FAILED: %s\n", error.c_str());
            OfferRestart(error);
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
