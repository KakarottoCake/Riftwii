// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2009 Tantric (libwiigui template) <https://github.com/dborth/libwiigui>
// SPDX-License-Identifier: GPL-3.0-or-later
#include <fat.h>
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/system.h>
#include <sdcard/wiisd_io.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "boot.hpp"
#include "nandchannels.hpp"
#include "menumusic.hpp"
#include "screenshot.hpp"
#include "FreeTypeGX.h"
#include "audio.h"
#include "input.h"
#include "menu.h"
#include "video.h"
#include "zstd.h"

#include "autorun.hpp"
#include "headless.hpp"
#include "console.hpp"
#include <cstdio>
#include <ogc/machine/processor.h>

#include "crash.hpp"
#include "gcadapter.hpp"
#include "guiscript.hpp"
#include "i18n.hpp"
#include "ios_reload.hpp"
#include "loadersettings.hpp"
#include "d2xsd.hpp"
#include "covers.hpp"
#include "entropy.hpp"
#include "log.hpp"
#include "memlimits.hpp"
#include "menuios.hpp"
#include "usbprobe.hpp"
#include "netsock.hpp"
#include "online.hpp"
#include "progress.hpp"
#include <ogc/wiilaunch.h>

#include "reportsend.hpp"
#include "restart.hpp"
#include "wiifont.hpp"
#include "channel.hpp"
#include "skin.hpp"
#include "stash.hpp"
#include "plugins.hpp"

// Where the player leaves to (the HOME Menu, wii/menu/rift_menu.cpp): 1 back
// to the loader that started RiftWii (the Homebrew Channel), 2 the Wii
// Menu, 3 Priiloader, 4 power off, 5 the power button.
volatile int ExitRequested = 0;

namespace {

// The Homebrew Channel as a title (channel.hpp's HomebrewChannelTitle).
// None installed, or one that will not start, the caller goes on to exit
// (std::exit goes back through the Homebrew Channel's stub when it started
// RiftWii, else to the Wii Menu).
void StartHomebrewChannelTitle() {
    const u64 title = riftwii::wii::HomebrewChannelTitle();
    fatUnmount("sd:");
    fatUnmount("usb:");
    if (title == 0 || WII_Initialize() < 0) return;
    WII_LaunchTitle(title);  // returns only when it could not
}

// A WiiWare or Virtual Console game, on the launch screen (boot.log open):
// RiftWii's own loader starts it with the extras a disc game gets
// (boot_channel). When the loader cannot, a game installed on the Wii is
// started by the system as the Wii Menu starts it, without them;
// WII_LaunchTitle returns only when it could not, and the Wii Menu is then
// the way out. A game of an emulated NAND goes back to RiftWii instead.
void StartChannel(u64 title) {
    riftwii::wii::logf("Wii channels: starting %08x-%08x\n", static_cast<unsigned>(title >> 32),
                       static_cast<unsigned>(title));
    riftwii::wii::ProgressStage("Getting the game ready", 30);
    std::string error;
    riftwii::wii::ChannelBootFiles files;
    const std::string emu_nand = riftwii::wii::ChannelToStartNand();
    if (riftwii::wii::read_channel_boot(title, emu_nand, files, error)) {
        riftwii::wii::boot_channel(std::move(files), error);  // returns only when it did not start
        if (riftwii::wii::reload_terminal_failure()) riftwii::wii::halt_after_terminal_reload();
    }
    if (!emu_nand.empty()) {
        // The Wii does not have it: back to RiftWii, which says why.
        riftwii::wii::logf("Wii channels: it did not start from %s: %s\n", emu_nand.c_str(), error.c_str());
        if (riftwii::wii::CanRestart())
            riftwii::wii::WarmRestart(riftwii::wii::RestartKind::LaunchFailed, "The game did not start: " + error);
        std::exit(0);
    }
    riftwii::wii::logf("Wii channels: RiftWii's loader did not start it (%s); the system starts it as the Wii Menu "
                       "does, without RiftWii's extras (screenshots, the crash recorder, the GameCube adapter, the "
                       "Wii Remote log, cheats, Return to RiftWii)\n",
                       error.c_str());
    riftwii::wii::LogClose();
    fatUnmount("sd:");
    fatUnmount("usb:");
    if (WII_Initialize() >= 0) WII_LaunchTitle(title);
    SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
}

// Anywhere but the loader that started RiftWii, which std::exit returns to.
void LeaveTo(int where) {
    if (where == 1 && !riftwii::wii::StartedByHomebrewChannel()) {
        StartHomebrewChannelTitle();
    } else if (where == 4) {
        // Standby or off, as the Wii's own power setting says; the drives
        // finish their writes first, as for the power button.
        fatUnmount("sd:");
        fatUnmount("usb:");
        SYS_ResetSystem(SYS_POWEROFF, 0, 0);
    } else if (where == 5) {
        // The power button: the drives finish their writes, then standby
        // or off as the Wii's own setting says, as the Wii Menu's power
        // button does (a tester's Wii, set to standby, went fully off).
        // SYS_POWEROFF_STANDBY would always mean off, with the red light.
        fatUnmount("sd:");
        fatUnmount("usb:");
        SYS_ResetSystem(SYS_POWEROFF, 0, 0);
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
    // A background network job (the start's update check) ends first; a
    // theme download is stopped (the next start fetches it again).
    riftwii::wii::NetCancelBackground();
    riftwii::wii::NetWaitForBackground();
    // The cover loader finishes the file it is on before the card goes.
    riftwii::wii::CoverLoaderHold();
    riftwii::wii::MenuMusicStop();
    riftwii::wii::ScreenshotsStop();
    riftwii::wii::GcAdapterMenuEnd();
    ShutoffRumble();
    ShutdownAudio();
    StopGX();
    riftwii::wii::plugin::ShutdownAll("RiftWii is closing");
    LeaveTo(ExitRequested);
    std::exit(0);
}

namespace {

// Leaves the libwiigui renderer for the disc phase (the GUI thread is
// already halted by MainMenu): its last frame, the launch screen, stays
// up and the log prints into the white card on it. Music kept for the
// launch plays on, and LaunchMusicEnd shuts the audio down later.
void EnterConsolePhase(bool music_kept) {
    ShutoffRumble();
    if (!music_kept) ShutdownAudio();
    StopGXKeepPicture();
    // The card's inside (menu units 48,176 to 592,384, and the bar's row at
    // 388; wider by PopupExtra on each side on a widescreen menu, as the
    // card is) as frame-buffer pixels: a widescreen menu or a smaller
    // screen size draws the card narrower or smaller than 640x480 would.
    const int extra = PopupExtra();
    int left, top, right, bottom, barTop;
    Menu_MenuToXfb(48 - extra, 176, &left, &top);
    Menu_MenuToXfb(592 + extra, 384, &right, &bottom);
    Menu_MenuToXfb(48 - extra, 388, &left, &barTop);
    left = (left + 1) & ~1;
    const int width = (right - left) & ~7, height = (bottom - top) & ~15;  // whole 8x16 cells
    riftwii::wii::ConsoleStartInFrame(Menu_CurrentXfb(), Menu_XfbWidth(), Menu_XfbHeight(), left, top, width, height);
    riftwii::wii::CrashSetPhase(riftwii::wii::CrashPhase::Console);
    // Under the log, still on the white card: the stage and the bar.
    riftwii::wii::ProgressAttach(Menu_CurrentXfb(), Menu_XfbWidth(), Menu_XfbHeight(), left, barTop, width);
}

// After a launch that failed: back to Home (a fresh start, see
// wii/core/restart.hpp; also after two minutes untouched) or out to the
// Homebrew Channel.
void OfferRestart(const std::string& error) {
    // The failure is the news now: no music under it or the choice below.
    riftwii::wii::LaunchMusicEnd("the launch failed");
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
// Home reads the drives (wii/storage/usbcatalog.cpp), after the menu IOS is up.
// A few tries: some cards are slow to answer right after the Homebrew
// Channel lets go of them.
bool MountStartupSd() {
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0) {
            __io_wiisd.shutdown();
            usleep(250000);
        }
        if (__io_wiisd.startup() && __io_wiisd.isInserted() && fatMountSimple("sd", riftwii::wii::sd_interface())) return true;
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
    if (!sd_mounted) {
        riftwii::wii::EntropyStart();  // the console and the clock, at least
        return;
    }
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
    // AHBPROT off (the Homebrew Channel, the channel since version 9): what
    // needs the hardware (IOS patches, the GameCube adapter) can work.
    riftwii::wii::logf("Hardware access: %s\n", read32(0x0D800064) == 0xFFFFFFFF ? "yes" : "no (AHBPROT on)");
    riftwii::wii::EntropyStart();
    // Which title the system is running: the RiftWii channel or the
    // Homebrew Channel (both pass the same path), and the channel installed.
    {
        u64 title = 0;
        std::string who = "unknown";
        if (ES_GetTitleID(&title) >= 0) {
            char id[5] = {};
            for (int i = 0; i < 4; ++i) {
                const char c = static_cast<char>(title >> (24 - 8 * i));
                id[i] = c >= 0x20 && c < 0x7F ? c : '?';
            }
            char text[64];
            std::snprintf(text, sizeof(text), "%08X-%08X (%s)", static_cast<unsigned>(title >> 32),
                          static_cast<unsigned>(title), id);
            who = text;
            if (title == riftwii::wii::ChannelTitle()) who += ", the RiftWii channel";
        }
        unsigned version = 0;
        const bool channel = riftwii::wii::ChannelInstalled(version);
        riftwii::wii::logf("Running title: %s; RiftWii channel %s\n", who.c_str(),
                           channel ? ("version " + std::to_string(version) + " installed").c_str() : "not installed");
    }
    riftwii::wii::EnsureMetaAhbAccess();
    riftwii::wii::mem::LogLimits();
    riftwii::wii::mem::LogUsage("start");
}

// The menu font (wii/font/rounded.ttf) ships zstd-compressed in the
// start-up stash (Makefile.wii, wii/core/stash.hpp), already in MEM2.
// Unpacked into MEM2 too, where FreeType reads it for the whole menu
// phase, so neither the DOL's resident part nor the MEM1 heap carries it.
bool UnpackMenuFont(u8*& font, std::size_t& size) {
    const u8* packed = nullptr;
    u32 packed_size = 0;
    if (!riftwii::wii::stash::Find("menufont.zst", packed, packed_size)) return false;
    const unsigned long long full = ZSTD_getFrameContentSize(packed, packed_size);
    if (full == ZSTD_CONTENTSIZE_UNKNOWN || full == ZSTD_CONTENTSIZE_ERROR || full == 0) return false;
    size = static_cast<std::size_t>(full);
    font = riftwii::wii::skin::Mem2Alloc(size);
    if (font == nullptr) return false;
    return ZSTD_decompress(font, size, packed, packed_size) == size;
}

}  // namespace

int main() {
    // Keeps the loader out of the memory the game's apploader and IOS
    // reloads overwrite (wii/core/memlimits.hpp).
    riftwii::wii::mem::Init();
    const riftwii::wii::RestartNote restart = riftwii::wii::TakeRestartNote();
    riftwii::wii::CrashInstall();
    bool sd_mounted = MountStartupSd();
    riftwii::wii::mem::TestBallast();  // test builds only

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
    // A restart mounts the card before the fresh IOS above, under whatever
    // the last run left (a tester got "RiftWii needs an SD card" right
    // after a game failed to start, with the card in). The fresh IOS has
    // let go of it, so once more before saying there is no card.
    if (!sd_mounted && restart.kind != riftwii::wii::RestartKind::None) {
        sd_mounted = MountStartupSd();
        if (sd_mounted) {
            OpenSessionLog(true);
            riftwii::wii::logf("Restarted: %s\n", restart.message.c_str());
            riftwii::wii::logf("SD card: read only after the restart's fresh IOS%d\n", IOS_GetVersion());
            // The menu's saved cIOS (menu_ios.txt) could not be read before.
            riftwii::wii::StartMenuIos(true);
        }
    }
    // A marker on the card while the Wii Menu's font is read: reading it
    // may open IOS's NAND permission check, which hung consoles before
    // 3.3.3. Found at the next start, the font is skipped, so a console it
    // stops is not stopped at every start.
    constexpr const char* kFontTry = "sd:/riftwii/menu_font_try.txt";
    bool font_tried_before = false;
    if (riftwii::wii::Settings().menu_font == "wii" && sd_mounted) {
        if (FILE* f = std::fopen(kFontTry, "rb")) {
            std::fclose(f);
            font_tried_before = true;
        }
    }
    u8* font = nullptr;
    std::size_t font_size = 0;
    std::string font_why;
    bool font_read = false;
    const auto read_wii_font = [&] {
        if (sd_mounted) {
            if (FILE* f = std::fopen(kFontTry, "wb")) {
                std::fputs("RiftWii is reading the Wii Menu's font\n", f);
                std::fclose(f);
            }
        }
        font = riftwii::wii::LoadWiiMenuFont(font_size, font_why);
        if (sd_mounted) std::remove(kFontTry);
        font_read = true;
    };
    // The Wii Menu's font is behind IOS's NAND permission check, which
    // RiftWii can only open with hardware access (open_nand_permissions).
    // Without it (an older Homebrew Channel, the RiftWii channel before
    // version 9, or a restart's fresh IOS58, which takes it away) the menu
    // moves onto a d2x cIOS for a moment, as it does for a burned disc (d2x
    // leaves the check out), reads the font, which copies it to the SD card,
    // and goes back to IOS58: a d2x on another base cannot read USB drives
    // in the menu (a tester's drive went missing). Later starts read the
    // copy. Asked after the IOS above is up, since a restart's reload
    // changes what the menu has. Not when a menu IOS is chosen in Settings.
    if (sd_mounted && riftwii::wii::Settings().menu_font == "wii" && !font_tried_before &&
        read32(0x0D800064) != 0xFFFFFFFF && !riftwii::wii::running_in_dolphin() &&
        riftwii::wii::MenuCiosSlot() == 0 && riftwii::wii::LoadMenuIos() == 0 && !riftwii::wii::WiiMenuFontCached()) {
        const int slot = riftwii::wii::BurnedDiscSlot();
        if (slot != 0 && riftwii::wii::StartMenuIos(sd_mounted, false, slot,
                                                    "to copy the Wii Menu's font to the SD card (no hardware access)")) {
            read_wii_font();
            riftwii::wii::LeaveSessionCios(sd_mounted, "the font is read");
        }
    }
    {
        // After a crash the note's second line holds the registers: logged
        // (a crash that could not save crash.txt still reaches a report),
        // its first line shown on Home.
        std::string notice = restart.message;
        if (restart.kind == riftwii::wii::RestartKind::Crashed) {
            riftwii::wii::logf("Restarted after a crash: %s\n", notice.c_str());
            notice = notice.substr(0, notice.find('\n'));
        }
        SetHomeNotice(notice);
    }
    // Settings > Check the GameCube adapter > Check each cIOS.
    if (restart.kind == riftwii::wii::RestartKind::UsbCheck) SetHomeNotice(riftwii::wii::RunUsbCheck(sd_mounted));
    if (sd_mounted) riftwii::wii::ImportGameCrash();
    if (sd_mounted) riftwii::wii::ImportWpadLog();
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
    // Logged either side: with four Wii Remotes connected the menu froze on a
    // Wii (#138), and the last line of the log names how far it got.
    riftwii::wii::logf("Pads: starting libogc's Wii Remote stack (WPAD_Init, then each channel's data format)\n");
    SetupPads();
    riftwii::wii::logf("Pads: started\n");
    InitAudio();
    timed("video, pads and audio");
    // Settings > Menu font: the Wii Menu's, read from the NAND, else ours.
    bool font_ready = false;
    if (font_tried_before) {
        riftwii::wii::logf("Menu font: the Wii Menu's stopped RiftWii last time (%s was left); RiftWii's instead\n",
                           kFontTry);
        std::remove(kFontTry);
        SetHomeNotice(riftwii::wii::tr("RiftWii stopped while reading the Wii Menu's font last time, so it uses its own. Send a problem report so this can be fixed."));
    } else if (riftwii::wii::Settings().menu_font == "wii") {
        if (!font_read) read_wii_font();
        std::string& why = font_why;
        if (font != nullptr && !InitFreeType(font, font_size, riftwii::wii::kWiiMenuFontFace)) {
            DeinitFreeType();
            why = "FreeType could not read it";
            font = nullptr;
        }
        font_ready = font != nullptr;
        if (!font_ready) SetBitmapGlyphSource(nullptr);  // RiftWii's font alone
        if (font_ready) {
            riftwii::wii::logf("Menu font: the Wii Menu's (%u bytes)\n", static_cast<unsigned>(font_size));
        } else {
            riftwii::wii::logf("Menu font: the Wii Menu's could not be used (%s); RiftWii's instead\n", why.c_str());
            SetHomeNotice(read32(0x0D800064) != 0xFFFFFFFF
                ? riftwii::wii::tr("The Wii Menu's font needs hardware access or a d2x cIOS, and neither worked this time, so RiftWii's font is used. Start RiftWii from an up to date Homebrew Channel or the RiftWii channel (version 9).")
                : riftwii::wii::tr("The Wii Menu's font could not be read, so RiftWii's is used."));
        }
        timed("Wii Menu font read");
    }
    if (!font_ready) {
        if (!UnpackMenuFont(font, font_size)) {
            // Only if MEM2 were already full: FreeType can't run without a face.
            riftwii::wii::logf("Menu font: unpacking failed\n");
            ExitApp();
        }
        timed("font unpacked");
        InitFreeType(font, font_size);
    }
    if (!riftwii::wii::MenuLanguageDrawable(riftwii::wii::MenuLanguage())) {
        riftwii::wii::SetMenuLanguage("en");
        riftwii::wii::logf("Language: %s cannot be drawn with this font; English instead\n",
                           riftwii::wii::MenuLanguage().c_str());
        SetHomeNotice(riftwii::wii::kKoreanNeedsFont);
    }
    InitGUIThreads();
    timed("FreeType and the GUI thread");
    riftwii::wii::ScreenshotsStart();
    riftwii::wii::mem::LogUsage("threads started");
    riftwii::wii::CrashSetPhase(riftwii::wii::CrashPhase::Menu);
    // The plugin test switch (docs/DEVELOPING.md): only with
    // sd:/riftwii/plugin_test.txt on the card; nothing loads at a plain
    // start. Here, with the menu's crash screen ready, so a plugin that
    // crashes shows it as a player would see it.
    if (std::string refused; sd_mounted && riftwii::wii::plugin::RunTestSwitch(refused)) {
        if (!refused.empty()) SetHomeNotice(refused);
        timed("plugin test");
        riftwii::wii::mem::LogUsage("plugin test");
    }
    if (!sd_mounted) SetNoSdCard(StartedFromUsb());
    const int action = MainMenu(sd_mounted ? MENU_SOURCE : MENU_NEEDS_SD, state);
    MenuWatchdogStop();
    // The start's update check or a cover download may still be running on
    // the network's thread, and it writes to the card and the log: every way
    // out of the menu (a USB launch unmounts the card and reloads IOS before
    // boot_game) waits for it here first. A USB launch seconds after start had its heap damaged.
    if (riftwii::wii::NetBackgroundBusy()) {
        riftwii::wii::logf("Menu closed: waiting for the network's background job (update check or cover) to finish\n");
        riftwii::wii::NetCancelBackground();  // a theme download stops; the next start fetches it again
        riftwii::wii::NetWaitForBackground();
        riftwii::wii::logf("Menu closed: the background job is done\n");
        // The launch frame said it was waiting; it is not any more.
        if (action == MENU_LAUNCH || action == MENU_BOOT || action == MENU_DUMP || action == MENU_CHANNEL ||
            action == MENU_PLAY_CHANNEL)
            RefreshLaunchFrame(state, action);
    }
    // Before the adapter stops: what the player had plugged in, for the
    // launch's log.
    const std::string controllers = riftwii::wii::DescribeControllers();
    // A launch keeps the music under its log, so the launch screen is not
    // silent while the disc is checked and the launch prepared (a tester
    // found the quiet gap awkward); it fades out before the game's IOS or
    // the game itself takes over (LaunchMusicEnd).
    const bool game_launch = action == MENU_LAUNCH || action == MENU_BOOT || action == MENU_PLAY_CHANNEL;
    const bool music_kept = game_launch && riftwii::wii::MenuMusicKeepForLaunch();
    if (!music_kept) riftwii::wii::MenuMusicFadeOut();  // also stops it for a launch that keeps none
    riftwii::wii::ScreenshotsStop();
    // Before anything is launched: nothing of the menu's adapter may be
    // left in flight for the game (or the next IOS) to answer.
    riftwii::wii::GcAdapterMenuEnd();
    // Stage (a)'s plugins have nothing to do in a launch: shut down before
    // it, their arena given back to the heap (docs/PLUGINS.md, "Loading").
    riftwii::wii::plugin::ShutdownAll("the menu closed");
    riftwii::wii::mem::LogUsage("menu closed");
    const bool heap_whole = riftwii::wii::mem::CheckHeap("menu closed");
    const riftwii::wii::LaunchSource source = riftwii::wii::SelectedSource(state);

    EnterConsolePhase(music_kept);
    // A tester's heap broke between "menu closed" and the first reload
    // (once; the same launch worked when tried again): checked at the steps
    // between too (the cIOS search checks after itself), to name the one.
    riftwii::wii::mem::CheckHeap("console phase");
    std::string error;
    if (!heap_whole && game_launch) {
        // Damaged while the menu ran: a launch would only fail later, at
        // the cIOS reload, blaming that step (a tester's Wii U tried three
        // slots this way). A fresh start of RiftWii has a whole heap.
        riftwii::wii::LogOpen("sd:/riftwii/boot.log");
        riftwii::wii::logf("RiftWii %s: %s %s\n", RIFTWII_VERSION, action == MENU_BOOT ? "boot" : "launch",
                           state.game_id.c_str());
        error = "RiftWii's memory was damaged while the menu was open (the details are in session.log); "
                "start the game again once RiftWii has restarted";
        riftwii::wii::logf("FAILED: %s\n", error.c_str());
        OfferRestart(error);
    } else if (action == MENU_LAUNCH) {
        riftwii::wii::LogOpen("sd:/riftwii/boot.log");
        riftwii::wii::logf("RiftWii %s: launch %s with packages\n", RIFTWII_VERSION, state.game_id.c_str());
        riftwii::wii::logf("Controllers: %s\n", controllers.c_str());
        riftwii::wii::LogDeclinedUpdate();
        if (riftwii::wii::GuiScriptFailLaunch()) error = "a test failure the guiscript asked for";
        const bool booted = !error.empty() ? false : riftwii::wii::mem::OutOfMemoryAsError(error, [&] {
            return (source.kind == riftwii::wii::LaunchSource::Kind::Disc && state.has_compiled)
                       ? riftwii::wii::BootCompiled(std::move(state.compiled), error, source, state.model.save_mode, state.game_id)
                       : riftwii::wii::RunLaunch(state.model.selections(), error, source, state.model.save_mode,
                                                 state.game_id);
        });
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
        if (!error.empty() || !riftwii::wii::mem::OutOfMemoryAsError(error, [&] { return riftwii::wii::RunBoot(true, error, source); })) {
            if (riftwii::wii::reload_terminal_failure()) riftwii::wii::halt_after_terminal_reload();
            riftwii::wii::LogOpen("sd:/riftwii/boot.log", true);  // boot_game closed it and remounted the card
            riftwii::wii::logf("FAILED: %s\n", error.c_str());
            OfferRestart(error);
        }
    } else if (action == MENU_PLAY_CHANNEL) {
        // A WiiWare or Virtual Console game, on the same launch screen as a
        // disc game. StartChannel returns only by leaving RiftWii.
        riftwii::wii::LogOpen("sd:/riftwii/boot.log");
        riftwii::wii::logf("RiftWii %s: launch %s (Wii channel)\n", RIFTWII_VERSION, state.game_id.c_str());
        riftwii::wii::logf("Controllers: %s\n", controllers.c_str());
        riftwii::wii::LogDeclinedUpdate();
        StartChannel(riftwii::wii::ChannelToStart());
    } else if (action == MENU_CHANNEL) {
        // The channel installer app (wii/launch/channel.hpp). Only comes back if
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
    riftwii::wii::LaunchMusicEnd("the launch ended");
    riftwii::wii::LogClose();
    riftwii::wii::logf("Press HOME, Start or RESET to exit.\n");
    riftwii::wii::WaitForExit();
    std::exit(0);
    return 0;
}
