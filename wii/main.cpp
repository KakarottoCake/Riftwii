// SPDX-License-Identifier: GPL-3.0-or-later
#include <fat.h>
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/system.h>
#include <sdcard/wiisd_io.h>
#include <sys/stat.h>
#include <unistd.h>
#include <brotli/decode.h>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "menumusic.hpp"
#include "screenshot.hpp"
#include "FreeTypeGX.h"
#include "audio.h"
#include "input.h"
#include "menu.h"
#include "video.h"
#include "menufont_bin.h"

#include "autorun.hpp"
#include "headless.hpp"
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
#include "netsock.hpp"
#include "online.hpp"
#include "progress.hpp"
#include "reportsend.hpp"
#include "restart.hpp"
#include "channel.hpp"
#include "skin.hpp"

// Where the player leaves to (the HOME Menu, wii/rift_menu.cpp): 1 back
// to the loader that started RiftWii (the Homebrew Channel), 2 the Wii
// Menu, 3 Priiloader, 4 power off, 5 the power button (off, red light).
volatile int ExitRequested = 0;

namespace {

// Anywhere but the loader that started RiftWii, which std::exit returns to.
void LeaveTo(int where) {
    if (where == 4) {
        // Standby or off, as the Wii's own power setting says.
        SYS_ResetSystem(SYS_POWEROFF, 0, 0);
    } else if (where == 5) {
        // The power button: the drives finish their writes, then the Wii
        // turns fully off (red light), whatever WiiConnect24 is set to.
        fatUnmount("sd:");
        fatUnmount("usb:");
        SYS_ResetSystem(SYS_POWEROFF_STANDBY, 0, 0);
    } else if (where == 2 || where == 3) {
        if (where == 3) {
            // Priiloader looks for "Daco" at 0x8132FFFB when the Wii Menu
            // is loaded and opens its own menu. Without Priiloader the
            // word is ignored and the Wii Menu starts.
            volatile u8* magic = reinterpret_cast<volatile u8*>(0x8132FFFB);
            magic[0] = 'D';
            magic[1] = 'a';
            magic[2] = 'c';
            magic[3] = 'o';
            DCFlushRange(reinterpret_cast<void*>(0x8132FFE0), 0x40);
        }
        SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
    }
}

}  // namespace

void ExitApp() {
    // A background network job (the start's update check) ends first.
    riftwii::wii::NetWaitForBackground();
    riftwii::wii::MenuMusicStop();
    riftwii::wii::ScreenshotsStop();
    riftwii::wii::GcAdapterMenuEnd();
    ShutoffRumble();
    ShutdownAudio();
    StopGX();
    LeaveTo(ExitRequested);
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
    riftwii::wii::logf("\nTo get help, press A: RiftWii offers to send a problem report\n"
                       "(you don't need to take out your SD card).\n");
    if (!riftwii::wii::CanRestart()) return;
    riftwii::wii::logf("\nA: back to RiftWii   HOME: leave to the Homebrew Channel\n");
    if (riftwii::wii::WaitForChoice(120) != riftwii::wii::ExitChoice::Restart) std::exit(0);
    riftwii::wii::WarmRestart(riftwii::wii::RestartKind::LaunchFailed,
                              "The launch failed: " + error);
}

// libfat's default initializer probes USB as well as SD. Mount only the SD
// card here so autorun and the SD-backed package paths work; USB starts when
// Home reads the drives (wii/usbcatalog.cpp), after the menu IOS is up.
// A few tries: some cards are slow to answer right after the Homebrew
// Channel lets go of them.
bool MountStartupSd() {
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0) {
            __io_wiisd.shutdown();
            usleep(250000);
        }
        if (__io_wiisd.startup() && __io_wiisd.isInserted() && fatMountSimple("sd", &__io_wiisd)) return true;
    }
    return false;
}

// The Homebrew Channel passes the DOL's path, "usb:/apps/..." when it
// started RiftWii from a USB drive.
bool StartedFromUsb() {
    return __system_argv != nullptr && __system_argv->argvMagic == ARGV_MAGIC && __system_argv->argc > 0 &&
           __system_argv->argv != nullptr && __system_argv->argv[0] != nullptr &&
           std::strncmp(__system_argv->argv[0], "usb:", 4) == 0;
}

// The menu phase's log: every scan, probe and failure from startup until a
// launch opens boot.log. Each line is synced to the card, so after a hang
// its last line names the step that never finished.
void OpenSessionLog(bool sd_mounted) {
    if (!sd_mounted) return;
    mkdir("sd:/riftwii", 0777);
    riftwii::wii::RotateSessionLog();
    riftwii::wii::LogOpen("sd:/riftwii/session.log");
    riftwii::wii::logf("RiftWii %s on %s, IOS%d rev %d\n", RIFTWII_VERSION,
                       riftwii::wii::running_in_dolphin() ? "Dolphin" : "Wii", IOS_GetVersion(), IOS_GetRevision());
    // Who started it: the Homebrew Channel and the RiftWii channel both
    // pass the DOL's path.
    const bool has_path = __system_argv != nullptr && __system_argv->argvMagic == ARGV_MAGIC &&
                          __system_argv->argc > 0 && __system_argv->argv != nullptr && __system_argv->argv[0] != nullptr;
    riftwii::wii::logf("Started from %s\n", has_path ? __system_argv->argv[0] : "(no path given)");
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

    // Another loader (USB Loader GX) starting a game through RiftWii:
    // no menu (docs/HEADLESS.md).
    if (std::vector<std::string> args; riftwii::wii::HeadlessArguments(args)) {
        riftwii::wii::ConsoleStart(false);
        riftwii::wii::CrashSetPhase(riftwii::wii::CrashPhase::Console);
        riftwii::wii::RunHeadless(args);
        riftwii::wii::logf("Press HOME, Start or RESET to exit.\n");
        riftwii::wii::WaitForExit();
        std::exit(0);
    }

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
    riftwii::wii::StartMenuIos(sd_mounted, restart.kind != riftwii::wii::RestartKind::None,
                               restart.kind == riftwii::wii::RestartKind::BurnedDisc ? riftwii::wii::BurnedDiscSlot() : 0);
    SetHomeNotice(restart.message);
    if (sd_mounted) riftwii::wii::ImportGameCrash();
    FrontendState state;
    // Where the start's time goes (the log's own clock does the rest).
    u64 step = gettime();
    const auto timed = [&step](const char* what) {
        const u64 now = gettime();
        riftwii::wii::logf("Startup: %s in %u ms\n", what, static_cast<unsigned>(diff_msec(step, now)));
        step = now;
    };
    riftwii::wii::InitializeFrontend(state);
    riftwii::wii::SetMenuLanguage(riftwii::wii::MenuLanguage());
    timed("settings and language");

    InitVideo();
    SetupPads();
    InitAudio();
    timed("video, pads and audio");
    u8* font = nullptr;
    std::size_t font_size = 0;
    if (!UnpackMenuFont(font, font_size)) {
        // Only if MEM2 were already full: FreeType can't run without a face.
        riftwii::wii::logf("Menu font: unpacking failed\n");
        ExitApp();
    }
    timed("font unpacked");
    InitFreeType(font, font_size);
    InitGUIThreads();
    timed("FreeType and the GUI thread");
    riftwii::wii::ScreenshotsStart();
    riftwii::wii::CrashSetPhase(riftwii::wii::CrashPhase::Menu);
    if (!sd_mounted) SetNoSdCard(StartedFromUsb());
    const int action = MainMenu(sd_mounted ? MENU_SOURCE : MENU_NEEDS_SD, state);
    // The start's update check may still be running on its own thread, and
    // it writes to the card and the log: every way out of the menu (a USB
    // launch unmounts the card and reloads IOS before boot_game) waits for
    // it here first. A USB launch seconds after start had its heap damaged.
    if (riftwii::wii::NetBackgroundBusy()) {
        riftwii::wii::logf("Menu closed: waiting for the update check to finish\n");
        riftwii::wii::NetWaitForBackground();
        riftwii::wii::logf("Menu closed: the update check is done\n");
    }
    // Before the adapter stops: what the player had plugged in, for the
    // launch's log.
    const std::string controllers = riftwii::wii::DescribeControllers();
    riftwii::wii::MenuMusicStop();
    riftwii::wii::ScreenshotsStop();
    // Before anything is launched: nothing of the menu's adapter may be
    // left in flight for the game (or the next IOS) to answer.
    riftwii::wii::GcAdapterMenuEnd();
    riftwii::wii::mem::LogUsage("menu closed");
    riftwii::wii::mem::CheckHeap("menu closed");
    const riftwii::wii::LaunchSource source = riftwii::wii::SelectedSource(state);

    EnterConsolePhase();
    std::string error;
    if (action == MENU_LAUNCH) {
        riftwii::wii::LogOpen("sd:/riftwii/boot.log");
        riftwii::wii::logf("RiftWii %s: launch %s with packages\n", RIFTWII_VERSION, state.game_id.c_str());
        riftwii::wii::logf("Controllers: %s\n", controllers.c_str());
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
        riftwii::wii::logf("Controllers: %s\n", controllers.c_str());
        riftwii::wii::LogDeclinedUpdate();
        if (riftwii::wii::GuiScriptFailLaunch()) error = "a test failure the guiscript asked for";
        if (!error.empty() || !riftwii::wii::RunBoot(true, error, source)) {
            if (riftwii::wii::reload_terminal_failure()) riftwii::wii::halt_after_terminal_reload();
            riftwii::wii::LogOpen("sd:/riftwii/boot.log", true);  // boot_game closed it and remounted the card
            riftwii::wii::logf("FAILED: %s\n", error.c_str());
            OfferRestart(error);
        }
    } else if (action == MENU_CHANNEL) {
        // The channel installer app (wii/channel.hpp). Only comes back if
        // it cannot start; a fresh start then says why on Home.
        std::string error;
        riftwii::wii::StartChannelInstaller(error);
        riftwii::wii::WarmRestart(riftwii::wii::RestartKind::ChannelDone, error);
        riftwii::wii::logf("Press HOME, Start or RESET to exit.\n");
        riftwii::wii::WaitForExit();
        std::exit(0);
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
