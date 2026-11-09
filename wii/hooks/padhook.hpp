// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2017 Alex Chadwick (wup-028-bslug) <https://github.com/Chadderz121/wup-028-bslug>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/dol.hpp"
#include "riftwii/patch.hpp"

// The GameCube controller adapter for Wii U in games that support the
// GameCube controller: the game's PADRead and PADControlMotor are hooked
// (riftwii/symsearch.hpp finds them) and the adapter's controllers fill
// the ports that have none plugged in, rumble included. The blob
// (runtime/pad, embedded as riftwii_pad_bin) goes just below the MEM1
// arena's top, below the resident runtime when there is one; the driver
// state (runtime/rtgcad.h, buffers IOS reaches) at the MEM2 arena's
// bottom, above the resident's data. The driver zeroes its state at the
// game's first PADRead, so nothing is staged there. /dev/usb/hid is
// opened here, after the last IOS reload, and handed to the game.
//
// A code build (Project+, PMEX Remix) sizes Brawl's heaps to all of MEM1
// and reads its MEM2 data at fixed addresses from the arena's start, so
// there the blob and its state go together at the top of the MEM2 arena,
// whose end comes down (as the virtual SD card and the crash recorder do),
// and its hooks reach the blob through veneers in low MEM1 (kPadHookSites,
// 16 bytes each): MEM2 is out of a relative branch's reach.
//
// The way back (#224): with OSReturnToMenu, OSSleepThread and the SDK's
// IPC submit routine found and hooked, L, R, Z and Start held on a
// GameCube controller leave the game through its own OSReturnToMenu (the
// HOME Menu's Wii Menu button, which Return to RiftWii already points at
// the channel), once no save has been written for a moment. Missing any
// of them, the adapter works as before, without the way back.
// OSSuspendThread, hooked too where it is found, lets a thread that
// suspends itself to wait leave as a sleeping one would (N64 Virtual
// Console's pad-reading thread); without it only sleeps leave.
//
// Anything that stands in the way turns the adapter off with a reason
// for the log, never fails the launch: no PADRead in the game, a pack's
// memory patch on the memory it would take, or (after the patches) a
// first instruction of PADRead that can no longer be moved (a pack or
// code hooked it). A Gecko code that later writes over the hook simply
// wins: its own branch replaces ours and the adapter goes quiet.
namespace riftwii::wii {
// Connects the shared pad driver's events to the game's saved controller log.
void log_pad_diagnostics(std::uint32_t code_base, std::uint32_t print_entry);

// The blob's hooks: PADRead, PADControlMotor, the IPC submit routine,
// OSSleepThread and OSSuspendThread (a code build's veneers, one each).
constexpr std::uint32_t kPadHookSites = 5;

struct PadHook {
    bool active = false;             // planned; install_pad_hook hooks it
    bool demo = false;               // no adapter: the first empty port presses A (Dolphin tests)
    std::uint32_t read = 0;          // PADRead
    unsigned read_sites = 0;         // its error stores
    std::uint32_t motor = 0;         // PADControlMotor, 0: no rumble
    std::uint32_t idle_reset = 0;    // the screen-dimming reset PADRead calls, 0: adapter pads don't keep the screen lit
    std::uint32_t code_base = 0;     // the blob (MEM1)
    std::uint32_t code_bytes = 0;
    std::uint32_t state_base = 0;    // the driver state (MEM2)
    std::uint32_t state_bytes = 0;
    bool adapter_only = false;       // the state is the adapter's alone (no other USB pad could be used)
    bool pads_only = false;          // the USB pads blob: no adapter, Switch Pro or DualShock 3 code (a code build)
    std::uint32_t link_count = 0;    // pad links in the state (4, or a code build's pads plugged in at the launch)
    std::uint32_t map_count = 0;     // the catalog's layouts the game keeps
    std::uint32_t map_bytes = 0;     // and their bytes (packed, #506)
    bool hid_dormant = false;        // a code build with only Xbox 360 pads: the /dev/usb/hid list owner lies dormant
    std::uint32_t new_arena1_hi = 0; // == code_base (unchanged with a veneer)
    std::uint32_t new_arena2_lo = 0; // the end of the state (unchanged with a veneer)
    std::uint32_t veneer = 0;        // in MEM2: the two veneers' address in low MEM1, else 0
    std::uint32_t new_arena2_hi = 0; // in MEM2: the MEM2 arena's new end (== code_base), else 0
    std::int32_t fd = -1;            // /dev/usb/hid
    std::uint32_t version = 0;       // 4 or 5
    std::int32_t known_dev = -1;     // the adapter's v5 device id from libogc's list, -1: none
    bool devices_unknown = false;    // libogc's device list could not be read at the launch
    std::int32_t ven_fd = -1;        // /dev/usb/ven for Xbox 360 pads, -1: none
    // The way back; return_to_menu 0: off (missing, or a hook failed).
    std::uint32_t return_to_menu = 0;  // OSReturnToMenu
    std::uint32_t sleep_thread = 0;    // OSSleepThread
    std::uint32_t suspend_thread = 0;  // OSSuspendThread, 0: only sleeps leave (N64 VC's pad thread suspends itself)
    std::uint32_t ipc_submit = 0;      // the SDK's IPC submit routine
    std::uint32_t resident_last_write = 0;  // the resident's save-write time (SD saves), 0: none
    std::uint32_t resident_busy = 0;        // its savegame engine's busy word (with the above)
    std::uint32_t resident_queued = 0;      // and its queue count
    // The game's own Reset (#458), in a launch with no resident runtime to
    // take it: the caller fills these after find_pad_functions and before
    // plan_pad_hook. restart_os 0: left alone (a code build never has it).
    std::uint32_t restart_os = 0;           // OSRestart
    std::uint32_t restart_ioctlv = 0;       // the game's synchronous IOS_Ioctlv
    std::uint32_t restart_lba = 0;          // the request's sector on the card
    std::int32_t restart_sdio_fd = -1;      // the card, held open for the game since the launch
    std::uint32_t restart_sdio_mode = 0;    // RTRESTART_SD_*
    std::vector<std::uint8_t> restart_armed;  // the sector the Reset writes
    std::vector<std::uint8_t> restart_idle;   // the sector as the file holds it
    std::uint32_t restart_block = 0;        // planned: the block in MEM2, at the end of the state
    std::uint32_t restart_block_bytes = 0;  // and its bytes (part of state_bytes)
    bool restart_hooked = false;            // install_pad_hook put the hook on OSRestart
};

// Opens /dev/usb/hid on RiftWii's own handle and tells v4 from v5.
bool open_usb_hid(std::int32_t& fd, std::uint32_t& version, std::string& why);
// After an IOS reload: a /dev/usb/hid request that timed out went with
// the old IOS, so the new one may be asked again.
void forget_usb_hid_stuck();
// Before an IOS reload (reload_ios), while the old IOS still answers: the
// launch's own /dev/usb/hid handle is closed and forgotten, so the next use
// opens a fresh one on the new IOS. A handle whose request never came back
// is dropped without a close (the close would wait behind it), and the stuck
// state stays. Does nothing when no handle is held.
void forget_usb_hid_handle();
// Keep this IOS's live device list before libogc drops its cache. Cleared
// by forget_usb_hid_handle on an actual IOS reload.
void release_menu_usb_for_game();
// The running IOS's /dev/usb/hid is v4 (a d2x cIOS on base 56 or older):
// it is asked only v4's GetVersion. v5's (request 0) is v4's
// GetDeviceChange, which waits for the next plug or unplug.
void usb_hid_is_v4();

// Whether the running IOS has /dev/usb/hid (IOS36, which many games
// ask for, has none: with the adapter on, the running IOS is kept).
bool usb_hid_present();

// Whether an adapter (VID/PID 057E:0337, which third-party ones show in
// their Wii U mode) is plugged in, for the Auto setting: `how` says what
// was listed. Unknown when this IOS cannot tell (v5), taken as plugged in.
enum class AdapterSeen { Found, Missing, Unknown };
AdapterSeen look_for_gc_adapter(std::string& how);
// Closes the handle look_for_gc_adapter opened (without v5's Shutdown),
// when no game takes it.
void close_adapter_session();
// Diagnostics for a problem report (the GameCube adapter on a Wii U):
// opens /dev/usb/hid and /dev/usb/ven in several open modes and asks
// each for its v5 version, with the answer's buffer in MEM2 and in MEM1,
// then closes them (no Shutdown). One line: the IOS, the time since its
// reload, and each try's open and GetVersion results.
std::string usb_open_matrix();
// One look at libogc's USB device list (starting libogc's USB if need
// be): the adapter's v5 device id when it has one, and every VID:PID.
AdapterSeen ogc_adapter(std::int32_t& dev_id, std::string& devices);

// While boot.log is still open: PADRead and PADControlMotor in the
// loaded game, into a fresh `out`, then /dev/usb/hid opened for the game
// (libogc's USB is shut down for it). False (with a reason) turns the
// adapter off.
bool find_pad_functions(const DolHeader& dol, bool demo, PadHook& out, std::string& why);

// The adapter's /dev/usb/hid handle for the game, with its HID version and
// the adapter's v5 device id from the USB list (-1: none): the one
// look_for_gc_adapter opened, else a fresh one once libogc's USB has let
// go. False, with `why`, when /dev/usb/hid could not be opened.
// `list_unknown` (optional): set when libogc's USB device list could not be
// read, so whether the adapter is plugged in is not known.
bool take_adapter_handle(std::int32_t& fd, std::uint32_t& version, std::int32_t& known_dev, std::string& why,
                         bool* list_unknown = nullptr);

// After take_adapter_handle (v5): /dev/usb/ven for the game's wired Xbox
// 360 pads, on a handle of its own, or -1 when none was plugged in at the
// launch (HidPadsXboxPlugged) or the handle failed (logged).
std::int32_t open_usb_ven_for_game();


// Before the <memory> patches, on what find_pad_functions filled in.
// `arena1_hi` and `arena2_lo` are the arena ends left by the resident
// runtime (or the game's own without it); the IOS entries are the
// resident's unhooked ones, 0 to search the game. `mem1_veneer`, for a
// code build, puts the blob at the top of MEM2 with its veneers there
// (kPadHookSites * 16 bytes); the caller then lowers the MEM2 arena's end to
// `new_arena2_hi`. A nonzero `stage_base` keeps that blob below the
// resident runtime's staging area until the handover copies it down.
bool plan_pad_hook(const DolHeader& dol, std::uint32_t arena1_hi, std::uint32_t mem1_floor, std::uint32_t arena2_lo,
                   std::uint32_t ioctl_async, std::uint32_t ioctlv_async,
                   const std::vector<MemoryPatch>& patches, PadHook& out, std::string& why,
                   std::uint32_t mem1_veneer = 0, std::uint32_t stage_base = 0);

// After the patches and the cheats, just before the handover.
bool install_pad_hook(PadHook& hook, std::string& why);

// After Return to RiftWii has been patched in for the channel (the Reset
// leaves through the game's OSReturnToMenu, which starts it): turns the
// Reset on, in the words install_pad_hook left off. False when the hook
// is not on OSRestart.
bool enable_pad_reset(PadHook& hook, std::uint32_t os_return_to_menu);

}  // namespace riftwii::wii
