// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2017 Alex Chadwick (wup-028-bslug) <https://github.com/Chadderz121/wup-028-bslug>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "padhook.hpp"
#include "hidpads.hpp"

#include <gccore.h>
#include <malloc.h>
#include <ogc/cache.h>
#include <ogc/ipc.h>
#include <ogc/machine/processor.h>
#include <ogc/usb.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "hidsession.hpp"
#include "i18n.hpp"
#include "ios_reload.hpp"
#include "loadersettings.hpp"
#include "log.hpp"
#include "pad_hook.h"
#include "rtgcwii.h"
#include "pentablet.h"
#include "plugins.hpp"
#include "progress.hpp"
#include "riftwii/byteorder.hpp"
#include "riftwii/hbmsearch.hpp"
#include "riftwii/hook.hpp"
#include "riftwii/mempatch.hpp"
#include "riftwii/osrestart.hpp"
#include "riftwii/plugin_api.h"
#include "riftwii/plugin_pads.h"
#include "riftwii/symsearch.hpp"
#include "riftwii_pad_sizes.h"
#include "rtgcad.h"
#include "rtrestart.h"
#include "skin.hpp"

namespace riftwii::wii {
namespace {

constexpr std::uint32_t kMem2ArenaEndField = 0x80003128;
// A constant: boot.cpp writes the bus clock to 0x800000F8 only at the handover, after this is planned (read there,
// a launch from the RiftWii channel got 0, leaving the adapter's timeouts at a few microseconds and its ports empty).
constexpr std::uint32_t kBusClock = 0x0E7BE2C0;  // the time base runs at a quarter of it
constexpr unsigned kScratchRegister = 12;             // pad_entry.S's jump back
constexpr std::uint32_t kNop = 0x60000000;
// The way back (#224): the combo held as long as in GameCube games
// (runtime/gc's leave_hold), and the quiet a save needs before it goes.
// The quiet is over twice the longest gap between writes measured inside
// a save in Dolphin: 0.418 s in Super Smash Bros. Brawl's first save
// (28,874 calls over 11 s), 0.244 s in Mario Kart Wii's.
constexpr std::uint32_t kLeaveHoldMs = 2000;
constexpr std::uint32_t kSaveQuietMs = 1000;
// The HOME Menu (#224 part 1), from the same hold. HOME is held on the
// stand-in long enough for a Virtual Console game, which opens its menu on
// a HOME held half a second (Cruis'n USA, Bomberman Hero, Dr. Mario Online
// Rx in Dolphin, 25 Oct). The menu then gets two seconds to open (Brawl
// builds it for 20 frames first); after that the game has refused, mid-save
// or loading, showing its crossed-out HOME icon. It counts as closed once
// HBMCalc has not run for a while (it runs through the closing animation).
constexpr std::uint32_t kHomePressMs = 600;
// Before it presses HOME, the stand-in is there with nothing pressed for a
// moment: Naruto Shippuden: Clash of Ninja Revolution III takes no new
// press on the frame a controller appears, and so never opened its menu
// on a HOME held from the start (9 Nov, Dolphin).
constexpr std::uint32_t kHomeAnnounceMs = 200;
constexpr std::uint32_t kHomeWaitMs = 2000;
constexpr std::uint32_t kHomeLingerMs = 300;
// A GameCube controller as a Classic Controller (#587): how long its port
// may report nothing (a wireless pad's dropped reports) before the game
// hears the controller left.
constexpr std::uint32_t kGcwiiGraceMs = 500;
// The Wii Remote + Nunchuk form's pointer stays shown this long after the
// C-stick last moved it, then hides (games that hide the cursor when
// nothing points still do).
constexpr std::uint32_t kGcwiiPointerShowMs = 2000;
// A game that reads WPAD itself and hasn't read the stand-in since it was
// told of it hears "connected" again after this long (rtgcwii_retell).
constexpr std::uint32_t kGcwiiRetellMs = 1000;
// The stand-in's shake steps every this many ms: a half-swing of about
// 2.5 frames, as Dolphin's 15 Hz shake measures (rtgcwii.shake_flip).
constexpr std::uint32_t kGcwiiShakeFlipMs = 40;
// v5 serves 16 handles, picked by the open mode: libogc's USB takes 0 and
// fakemote (a cIOS module) 15. 0 is tried first (free once libogc's USB
// is shut down; a Wii U's IOS 58 answered nothing on 1), then 1. v4
// ignores it.
constexpr int kHidHandles[2] = {0, 1};
// A USB HID module that does not answer (a Wii U's d2x cIOS never
// returned from opening /dev/usb/hid with an adapter plugged in) turns
// the adapter off instead of hanging the launch.
constexpr unsigned kHidTimeoutMs = 1500;

char g_hid_path[] ATTRIBUTE_ALIGN(32) = "/dev/usb/hid";
// libogc's other v5 USB device: when /dev/usb/hid answers no version, it
// uses this one (the same v5 requests), and so does the adapter here. Two
// testers' Wii Us: /dev/usb/hid gave GetVersion -4 on IOS 58 and d2x 251,
// while the menu, on libogc's handles, had the adapter.
char g_ven_path[] ATTRIBUTE_ALIGN(32) = "/dev/usb/ven";
const char* g_usb_path_used = "/dev/usb/hid";
// The buffers of our own USB requests, in MEM2 as libogc's (its IPC heap):
// with them in MEM1 a Wii U's IOS answered -4 to every request that has
// one (GetVersion, the device list, on hid and ven alike), while the
// ones without a buffer went through, and libogc's own USB worked a
// moment later on the same IOS. Taken once (usb_buffers).
constexpr std::size_t kVersionBytes = 32;
constexpr std::size_t kOwnListBytes = 0x180;
std::uint32_t* g_version_out = nullptr;
std::uint32_t* g_own_list = nullptr;
volatile bool g_async_done = false;
volatile s32 g_async_result = 0;
bool g_hid_stuck = false;  // a request timed out: /dev/usb/hid is left alone from then on
bool g_hid_v4 = false;     // usb_hid_is_v4: v5's GetVersion is not asked

s32 on_async(s32 result, void*) {
    g_async_result = result;
    g_async_done = true;
    return 0;
}

// Waits for the reply of a request just submitted (`submitted` is what
// the submit returned). False on a timeout: the request stays with IOS.
bool await_async(s32 submitted, s32& result) {
    if (submitted < 0) {
        result = submitted;
        return true;
    }
    for (unsigned i = 0; i < kHidTimeoutMs && !g_async_done; ++i) usleep(1000);
    if (!g_async_done) {
        g_hid_stuck = true;
        return false;
    }
    result = g_async_result;
    return true;
}
std::uint32_t align_up(std::uint32_t v) { return (v + 31) & ~31u; }

// The launch's own /dev/usb/hid, opened before libogc's USB touches it:
// libogc's USB_Deinitialize sends v5 a Shutdown, after which a Wii U's
// IOS 58 and d2x 251 answered -4 to every handle opened (a tester's two
// reports: the menu, on libogc's handle, had the adapter; the game's
// open was refused). Kept open for the game (find_pad_functions).
// It belongs to the IOS that gave it out: forget_usb_hid_handle drops it
// before an IOS reload (hidsession.hpp).
OwnHidHandle g_own;
HidDeviceHandoff g_handoff_devices;
// The handle take_adapter_handle took from g_own for this launch, -1:
// none (it opened its own, or took nothing). A launch refused before the
// game starts gives that one back (give_back_or_close).
std::int32_t g_handed_fd = -1;

// For usb_open_matrix only: the same request with its buffer in MEM1.
std::uint32_t g_mem1_probe[8] ATTRIBUTE_ALIGN(32);

bool usb_buffers() {
    if (g_version_out) return true;
    u8* p = skin::Mem2Alloc(kVersionBytes + kOwnListBytes);
    if (!p) return false;
    g_version_out = reinterpret_cast<std::uint32_t*>(p);
    g_own_list = reinterpret_cast<std::uint32_t*>(p + kVersionBytes);
    return true;
}

// v5's first device-change request on a handle answers at once with the
// devices there (its result is their number). True with the list.
bool own_list(std::int32_t fd, s32& count) {
    std::memset(g_own_list, 0, kOwnListBytes);
    DCFlushRange(g_own_list, kOwnListBytes);
    g_async_done = false;
    if (!await_async(IOS_IoctlAsync(fd, GCAD_V5_GET_DEVICE_CHANGE, nullptr, 0, g_own_list, kOwnListBytes,
                                    on_async, nullptr),
                     count))
        return false;
    DCInvalidateRange(g_own_list, kOwnListBytes);
    return count >= 0;
}

// Three builds of the same blob: the full one; one without the code for
// other USB pads (HID, Xbox 360, Switch Pro), used when the adapter is the
// only USB controller the game can use (PadHook::adapter_only); and, for a
// code build started with USB pads but no adapter, Switch Pro Controller
// or DualShock 3, one without those three's code (PadHook::pads_only, #496).
// Each also without the HOME Menu by the controller (#387; kBlobNoHome
// added): the blob as before it, with the smaller context, for code builds
// and with Settings > Leaving a game on HOME only (PadHook::home_blob).
// And the full and adapter-only HOME Menu builds again with the Wii
// controller stand-ins (#587, RT_PAD_STANDIN; kBlobClassic, plus one for
// adapter only), for a game whose GameCube controllers setting is on
// (PadHook::standin): every other game keeps the first six, unchanged.
// The same two again with the Wii Remote + Nunchuk form (kBlobNunchuk,
// RT_PAD_FORM_NUNCHUK), for a game set to that form
// (PadHook::standin_nunchuk), with the sideways Wii Remote form
// (kBlobSideways, RT_PAD_FORM_SIDEWAYS; PadHook::standin_sideways), and with
// the pointing Wii Remote form (kBlobPointing, RT_PAD_FORM_POINTING;
// PadHook::standin_pointing).
enum BlobKind {
    kBlobFull = 0,
    kBlobAdapter = 1,
    kBlobPads = 2,
    kBlobNoHome = 3,
    kBlobClassic = 6,
    kBlobNunchuk = 8,
    kBlobSideways = 10,
    kBlobPointing = 12,
    kBlobKinds = 14
};
int kind_of(bool adapter_only, bool pads_only, bool home, bool standin, bool nunchuk, bool sideways, bool pointing) {
    if (home && standin)
        return (nunchuk     ? kBlobNunchuk
                : sideways ? kBlobSideways
                : pointing ? kBlobPointing
                           : kBlobClassic) +
               (adapter_only ? 1 : 0);
    return (adapter_only ? kBlobAdapter : pads_only ? kBlobPads : kBlobFull) + (home ? 0 : kBlobNoHome);
}
int kind_of(const PadHook& hook) {
    return kind_of(hook.adapter_only, hook.pads_only, hook.home_blob, hook.standin, hook.standin_nunchuk,
                   hook.standin_sideways, hook.standin_pointing);
}
// A build without the HOME Menu (its smaller context and header).
bool no_home(int kind) { return kind >= kBlobNoHome && kind < kBlobClassic; }
// A build with the stand-ins (its own header after rt_pad_header).
bool standin_kind(int kind) { return kind >= kBlobClassic; }
const char* build_name(int kind) {
    static const char* const names[kBlobKinds] = {"full build",
                                                  "adapter's build",
                                                  "USB pads' build",
                                                  "full build without the HOME Menu",
                                                  "adapter's build without the HOME Menu",
                                                  "USB pads' build without the HOME Menu",
                                                  "full build with the stand-ins",
                                                  "adapter's build with the stand-ins",
                                                  "full build with the stand-ins as a Wii Remote + Nunchuk",
                                                  "adapter's build with the stand-ins as a Wii Remote + Nunchuk",
                                                  "full build with the stand-ins as a sideways Wii Remote",
                                                  "adapter's build with the stand-ins as a sideways Wii Remote",
                                                  "full build with the stand-ins as a pointing Wii Remote",
                                                  "adapter's build with the stand-ins as a pointing Wii Remote"};
    return names[kind];
}
// The builds are the pads plugin's (plugins/pads, docs/PLUGINS.md "The
// pads plugin"), not the DOL's: the DOL knows their sizes, and
// fetch_pad_build copies the one a game takes into g_build while the card
// is still RiftWii's, before the plugin is shut down. Only that one is
// ever read (blob(kind) of another is a bug, and nullptr).
static_assert(kBlobKinds == RWP_PAD_BUILDS, "padhook.cpp's builds and the pads plugin's differ");
constexpr std::uint32_t kBuildBytes[kBlobKinds] = RIFTWII_PAD_BUILD_BYTES;
constexpr char kPadsPlugin[] = "pads";
std::uint8_t* g_build = nullptr;
int g_build_kind = -1;
bool g_pads_refused = false;  // fetch_pad_build (or the check before the adapter's handle) stopped this launch
std::string g_pads_refusal;   // the sentence for the player when the check before the handle refused

// Whether the pads plugin can be used, asked before the adapter's handle is
// taken: refused here, the launch stops with the adapter left as the menu had
// it, so the failure screen can start it (console, 24 Nov: refused after the
// handle was taken and closed, the screen's adapter answered -4 to everything,
// and a player with GameCube controllers only couldn't leave it).
bool pads_plugin_usable() {
    std::string message;
    if (plugin::Use(kPadsPlugin, &message) != nullptr) return true;
    g_pads_refused = true;
    g_pads_refusal = !message.empty()
                         ? message
                         : tr("A part of RiftWii couldn't be used (the {1} add-on). Update RiftWii again.", {kPadsPlugin});
    return false;
}
void forget_build() {
    std::free(g_build);
    g_build = nullptr;
    g_build_kind = -1;
}
const std::uint8_t* blob(int kind) { return kind == g_build_kind ? g_build : nullptr; }
std::uint32_t blob_size(int kind) { return kBuildBytes[kind]; }
const rt_pad_header& header(int kind) { return *reinterpret_cast<const rt_pad_header*>(blob(kind)); }
// The context's size in that blob.
std::uint32_t context_bytes(int kind) { return no_home(kind) ? RT_PAD_CONTEXT_BASE_BYTES : RT_PAD_CONTEXT_BYTES; }

bool blob_whole(int kind) {
    const rt_pad_header& h = header(kind);
    const std::uint32_t size = blob_size(kind);
    // The blob without the HOME Menu has a header that ends before its
    // hooks (rt_pad_header's last five, after SIProbe's), which are never
    // read from it. The stand-in builds' own header follows rt_pad_header,
    // so it counts as header too: its offsets are checked with the rest
    // below (CodeRabbit CLI on the stack's rebase onto #593).
    const std::uint32_t header_bytes =
        no_home(kind) ? offsetof(rt_pad_header, hook_kpad)
                      : sizeof(rt_pad_header) + (standin_kind(kind) ? sizeof(rt_pad_standin_header) : 0u);
    // The context's end checked by subtraction, so a huge offset can't wrap
    // round the sum and pass (CodeRabbit on #610); the build's own size must
    // hold its header too.
    if (size < header_bytes || h.magic != RT_PAD_MAGIC || h.version != RT_PAD_VERSION || h.size > size ||
        h.size < header_bytes || h.context_offset > h.size || context_bytes(kind) > h.size - h.context_offset ||
        // The context is read as an rt_pad_context: a damaged offset that
        // isn't aligned for it is the damaged-add-on error too (CodeRabbit
        // on #609).
        h.context_offset % alignof(rt_pad_context) != 0)
        return false;
    // The copy's context must be one too (plan_pad_hook reads the same
    // bytes): a damaged one is refused here, with the damaged-add-on
    // error, not left to turn the pads off later (CodeRabbit on #593).
    if (reinterpret_cast<const rt_pad_context*>(blob(kind) + h.context_offset)->magic != RT_PAD_CONTEXT_MAGIC)
        return false;
    // Every offset after the context's (the hooks, the replays and jumps
    // back the install writes 16 bytes at, the IPC callback) must name a
    // word inside the blob with room for those 16 bytes: a damaged one
    // would have the install write outside the blob's code (CodeRabbit on
    // #593). A hook a blob doesn't have is 0, which passes; any other must
    // be past the header, or the install would write over it (CodeRabbit
    // on #651), and clear of the context, which is set up before those
    // writes (CodeRabbit on #650).
    constexpr std::uint32_t kSiteBytes = 16;
    const std::uint32_t* words = reinterpret_cast<const std::uint32_t*>(&h);
    const std::size_t first_site = offsetof(rt_pad_header, hook_read) / sizeof(std::uint32_t);
    const std::uint64_t context_end = std::uint64_t(h.context_offset) + context_bytes(kind);
    for (std::size_t index = first_site; index < header_bytes / sizeof(std::uint32_t); ++index) {
        const std::uint32_t offset = words[index];
        if (offset % sizeof(std::uint32_t) != 0 || offset > h.size || h.size - offset < kSiteBytes ||
            (offset != 0 && offset < header_bytes) ||
            (offset != 0 && offset < context_end && h.context_offset < std::uint64_t(offset) + kSiteBytes))
            return false;
    }
    // The install writes 16 bytes at each replay and each way back: two that
    // overlap would have the second write over the first, so each must stay
    // clear of every other, and of every hook's entry and the IPC callback
    // (their first word, where the game's branch lands), which a write over
    // would leave the game branching into replaced code (CodeRabbit on #650).
    // A site's replay falls through into its own way back, so the way back
    // is the 16 bytes right after it: one paired with another site's would
    // have the game's call resume in the wrong function (CodeRabbit on #650).
    // And a site is whole (its hook, replay and way back) or absent: a hook
    // with no replay would have the install write at the blob's start. The
    // sites the install uses in every game (PADRead, PADControlMotor, the
    // IPC submit routine, OSSleepThread, OSSuspendThread, SIProbe) are in
    // every build, so one missing there is a damaged build (CodeRabbit on
    // #651); OSRestart's is optional, and the HOME Menu's and the
    // stand-in's are checked below.
    {
        std::uint32_t slots[30];    // two for each of the 15 sites a build may have
        std::uint32_t entries[16];  // each site's hook, and the IPC callback
        std::size_t count = 0, entry_count = 0;
        bool whole = true;
        const auto add = [&](std::uint32_t hook, std::uint32_t replay, std::uint32_t resume, bool needed) {
            if (hook != 0) entries[entry_count++] = hook;
            if (replay != 0) slots[count++] = replay;
            if (resume != 0) slots[count++] = resume;
            const bool absent = hook == 0 && replay == 0 && resume == 0;
            if (absent ? needed : hook == 0 || replay == 0 || resume != std::uint64_t(replay) + kSiteBytes)
                whole = false;
        };
        add(h.hook_read, h.replay_read, h.continue_read, true);
        add(h.hook_motor, h.replay_motor, h.continue_motor, true);
        if (h.complete != 0) entries[entry_count++] = h.complete;  // a callback: no replay of its own
        add(h.hook_submit, h.replay_submit, h.continue_submit, true);
        add(h.hook_sleep, h.replay_sleep, h.continue_sleep, true);
        add(h.hook_suspend, h.replay_suspend, h.continue_suspend, true);
        add(h.hook_restart, h.replay_restart, h.continue_restart, false);
        add(h.hook_si_probe, h.replay_si_probe, h.continue_si_probe, true);
        if (!no_home(kind)) {
            add(h.hook_kpad, h.replay_kpad, h.continue_kpad, false);
            add(h.hook_probe, h.replay_probe, h.continue_probe, false);
            add(h.hook_menu, h.replay_menu, h.continue_menu, false);
            add(h.hook_select, h.replay_select, h.continue_select, false);
            add(h.hook_unified, h.replay_unified, h.continue_unified, false);
        }
        if (standin_kind(kind)) {
            const rt_pad_standin_header& own =
                *reinterpret_cast<const rt_pad_standin_header*>(blob(kind) + sizeof(rt_pad_header));
            add(own.hook_info, own.replay_info, own.continue_info, false);
            add(own.hook_wread, own.replay_wread, own.continue_wread, false);
            add(own.hook_format, own.replay_format, own.continue_format, false);
        }
        if (!whole) return false;
        for (std::size_t i = 0; i < count; ++i) {
            for (std::size_t j = 0; j < i; ++j)
                if (slots[i] < std::uint64_t(slots[j]) + kSiteBytes && slots[j] < std::uint64_t(slots[i]) + kSiteBytes)
                    return false;
            for (std::size_t e = 0; e < entry_count; ++e)
                if (entries[e] < std::uint64_t(slots[i]) + kSiteBytes && slots[i] < std::uint64_t(entries[e]) + 4)
                    return false;
        }
    }
    if (no_home(kind)) return true;
    // The HOME Menu's five sites, each with its replay and way back: one
    // without them would have the install write at the blob's start
    // (CodeRabbit on #616).
    const std::uint32_t home_sites[][3] = {{h.hook_kpad, h.replay_kpad, h.continue_kpad},
                                           {h.hook_probe, h.replay_probe, h.continue_probe},
                                           {h.hook_menu, h.replay_menu, h.continue_menu},
                                           {h.hook_select, h.replay_select, h.continue_select},
                                           {h.hook_unified, h.replay_unified, h.continue_unified}};
    for (const auto& site : home_sites)
        if (site[0] == 0 || site[1] == 0 || site[2] == 0) return false;
    // Every stand-in build has WPADGetInfoAsync's site (the 2006 KPAD games
    // wait for its answer); a copy with none would have the install write at
    // the blob's start (CodeRabbit CLI). WPADRead's and WPADSetDataFormat's
    // are 0 in the Classic Controller's builds, so they stay optional.
    if (!standin_kind(kind)) return true;
    const rt_pad_standin_header& sh =
        *reinterpret_cast<const rt_pad_standin_header*>(blob(kind) + sizeof(rt_pad_header));
    if (sh.hook_info == 0 || sh.replay_info == 0 || sh.continue_info == 0) return false;
    // WPADRead's and WPADSetDataFormat's: all three of each or none (the
    // Classic Controller's builds have none); a hook with no replay or way
    // back would have the install write at the blob's start (CodeRabbit on
    // #616).
    const auto whole_or_none = [](std::uint32_t hook, std::uint32_t replay, std::uint32_t resume) {
        return hook == 0 ? replay == 0 && resume == 0 : replay != 0 && resume != 0;
    };
    if (!whole_or_none(sh.hook_wread, sh.replay_wread, sh.continue_wread) ||
        !whole_or_none(sh.hook_format, sh.replay_format, sh.continue_format))
        return false;
    // Every Wii Remote form's build has both (pad_entry.S): one with none
    // would leave a game that reads WPAD itself without the stand-in's
    // reports (CodeRabbit on #650).
    return kind < kBlobNunchuk || (sh.hook_wread != 0 && sh.hook_format != 0);
}

void store_words(std::uint32_t address, const std::uint32_t* words, std::size_t count) {
    volatile std::uint32_t* p = reinterpret_cast<volatile std::uint32_t*>(address);
    for (std::size_t i = 0; i < count; ++i) p[i] = words[i];
}

void sync_code(std::uint32_t address, std::uint32_t bytes) {
    DCFlushRange(reinterpret_cast<void*>(address), bytes);
    ICInvalidateRange(reinterpret_cast<void*>(address), bytes);
}

bool overlaps(const MemoryPatch& p, std::uint32_t start, std::uint32_t bytes) {
    if (!p.has_offset || p.value.empty()) return false;
    return p.offset < static_cast<std::uint64_t>(start) + bytes && p.offset + p.value.size() > start;
}

}  // namespace

// v4's GetVersion answers 0x40001 itself, v5's writes 0x50001 into its
// output; v5 is asked first, as libogc does (v4's number is v5's
// AttachFinish). Every call is asynchronous with a timeout.
bool open_usb_hid(std::int32_t& fd, std::uint32_t& version, std::string& why) {
    fd = -1;
    g_usb_path_used = "/dev/usb/hid";
    if (!usb_buffers()) {
        why = "no MEM2 for the USB requests";
        return false;
    }
    if (g_hid_stuck) {
        why = "/dev/usb/hid did not answer earlier; left alone";
        return false;
    }
    std::string tried;
    for (const int handle : kHidHandles) {
        s32 ret = 0;
        g_async_done = false;
        if (!await_async(IOS_OpenAsync(g_hid_path, handle, on_async, nullptr), ret)) {
            why = "/dev/usb/hid did not answer when opened (handle " + std::to_string(handle) + ", " +
                  std::to_string(kHidTimeoutMs) + " ms)";
            return false;
        }
        if (ret < 0) {
            tried += (tried.empty() ? "" : "; ") + std::string("handle ") + std::to_string(handle) + ": open " +
                     std::to_string(ret);
            continue;
        }
        fd = ret;
        // v5's GetVersion first, as libogc asks: v4's (request 6) is v5's
        // AttachFinish, and after it a Wii U's IOS 58 and d2x 251 answered
        // -4 to GetVersion and to everything else (every tester's report:
        // "v4 GetVersion 0", AttachFinish done; Dolphin does not mind).
        // On v4, request 0 is GetDeviceChange, which answers at once the
        // first time; the handle is then opened again for v4's question.
        // Not the first time, though: on a Wii's d2x 249 (base 56) it never
        // answered at launch, and the game's disc reads failed after it.
        s32 v5 = 0;  // not asked on v4
        if (!g_hid_v4) {
            std::memset(g_version_out, 0, kVersionBytes);
            DCFlushRange(g_version_out, kVersionBytes);
            g_async_done = false;
            if (!await_async(IOS_IoctlAsync(fd, GCAD_V5_GET_VERSION, nullptr, 0, g_version_out, kVersionBytes,
                                            on_async, nullptr),
                             v5)) {
                why = "/dev/usb/hid did not answer its v5 GetVersion";
                fd = -1;  // left open: closing it could wait too
                return false;
            }
            DCInvalidateRange(g_version_out, kVersionBytes);
            if (v5 == 0 && g_version_out[0] == GCAD_V5_VERSION) {
                version = 5;
                return true;
            }
        }
        IOS_Close(fd);
        fd = -1;
        s32 v4 = 0;
        g_async_done = false;
        if (!await_async(IOS_OpenAsync(g_hid_path, handle, on_async, nullptr), ret)) {
            why = "/dev/usb/hid did not answer when opened again for v4";
            return false;
        }
        if (ret >= 0) {
            fd = ret;
            g_async_done = false;
            if (!await_async(IOS_IoctlAsync(fd, GCAD_V4_GET_VERSION, nullptr, 0, nullptr, 0, on_async, nullptr), v4)) {
                why = "/dev/usb/hid did not answer its v4 GetVersion";
                fd = -1;
                return false;
            }
            if (v4 == static_cast<s32>(GCAD_V4_VERSION)) {
                version = 4;
                return true;
            }
            IOS_Close(fd);
            fd = -1;
        } else {
            v4 = ret;
        }
        char buf[96];
        std::snprintf(buf, sizeof(buf), "handle %d: v5 GetVersion %d (%08x), v4 GetVersion %d", handle,
                      static_cast<int>(v5), static_cast<unsigned>(g_version_out[0]), static_cast<int>(v4));
        tried += (tried.empty() ? "" : "; ") + std::string(buf);
    }
    // libogc's way when /dev/usb/hid has no version: /dev/usb/ven, v5 only.
    {
        s32 ret = 0;
        g_async_done = false;
        if (!await_async(IOS_OpenAsync(g_ven_path, 0, on_async, nullptr), ret)) {
            why = "/dev/usb/hid answers neither as v4 nor as v5 (" + tried + "); /dev/usb/ven did not answer when opened";
            return false;
        }
        if (ret >= 0) {
            fd = ret;
            std::memset(g_version_out, 0, kVersionBytes);
            DCFlushRange(g_version_out, kVersionBytes);
            s32 v5 = 0;
            g_async_done = false;
            if (!await_async(IOS_IoctlAsync(fd, GCAD_V5_GET_VERSION, nullptr, 0, g_version_out, kVersionBytes,
                                            on_async, nullptr),
                             v5)) {
                why = "/dev/usb/ven did not answer its v5 GetVersion";
                fd = -1;
                return false;
            }
            DCInvalidateRange(g_version_out, kVersionBytes);
            if (v5 == 0 && g_version_out[0] == GCAD_V5_VERSION) {
                version = 5;
                g_usb_path_used = "/dev/usb/ven";
                logf("GameCube adapter: /dev/usb/hid has no version here (%s); /dev/usb/ven v5 instead\n", tried.c_str());
                return true;
            }
            IOS_Close(fd);
            fd = -1;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "/dev/usb/ven: v5 GetVersion %d (%08x)", static_cast<int>(v5),
                          static_cast<unsigned>(g_version_out[0]));
            tried += "; " + std::string(buf);
        } else {
            tried += "; /dev/usb/ven: open " + std::to_string(ret);
        }
    }
    why = "/dev/usb/hid answers neither as v4 nor as v5 (" + tried + ")";
    return false;
}

std::string usb_open_matrix() {
    char head[160], since[40] = "not reloaded this run";
    const unsigned ms = ms_since_ios_reload();
    if (ms != 0xFFFFFFFFu) std::snprintf(since, sizeof(since), "%u ms after its reload", ms);
    std::snprintf(head, sizeof(head), "IOS%d rev %d, %s, MEM2 buffer 0x%08x, MEM1 buffer 0x%08x;", IOS_GetVersion(),
                  IOS_GetRevision(), since,
                  usb_buffers() ? static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(g_version_out)) : 0u,
                  static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(g_mem1_probe)));
    std::string out = head;
    if (!usb_buffers()) return out + " no MEM2";
    if (g_hid_stuck) return out + " /dev/usb/hid stuck earlier, not tried";
    struct Try {
        char* path;
        const char* name;
        int mode;
        bool mem2;
    };
    const Try tries[] = {{g_hid_path, "hid", 0, true}, {g_hid_path, "hid", 0, false}, {g_hid_path, "hid", 1, true},
                         {g_hid_path, "hid", 2, true}, {g_ven_path, "ven", 0, true}, {g_ven_path, "ven", 0, false},
                         {g_ven_path, "ven", 1, true}};
    for (const Try& t : tries) {
        char one[96];
        s32 fd = 0;
        g_async_done = false;
        if (!await_async(IOS_OpenAsync(t.path, t.mode, on_async, nullptr), fd)) {
            std::snprintf(one, sizeof(one), " %s/%d: open did not answer; stopped", t.name, t.mode);
            out += one;
            break;
        }
        if (fd < 0) {
            std::snprintf(one, sizeof(one), " %s/%d: open %d;", t.name, t.mode, static_cast<int>(fd));
            out += one;
            continue;
        }
        std::uint32_t* buf = t.mem2 ? g_version_out : g_mem1_probe;
        std::memset(buf, 0, kVersionBytes);
        DCFlushRange(buf, kVersionBytes);
        s32 v5 = 0;
        g_async_done = false;
        if (!await_async(IOS_IoctlAsync(fd, GCAD_V5_GET_VERSION, nullptr, 0, buf, kVersionBytes, on_async, nullptr),
                         v5)) {
            std::snprintf(one, sizeof(one), " %s/%d %s: GetVersion did not answer; stopped", t.name, t.mode,
                          t.mem2 ? "MEM2" : "MEM1");
            out += one;
            break;
        }
        DCInvalidateRange(buf, kVersionBytes);
        std::snprintf(one, sizeof(one), " %s/%d %s: fd %d, GetVersion %d %08x;", t.name, t.mode,
                      t.mem2 ? "MEM2" : "MEM1", static_cast<int>(fd), static_cast<int>(v5),
                      static_cast<unsigned>(buf[0]));
        out += one;
        IOS_Close(fd);
    }
    return out;
}

// The adapter in libogc's HID list, which it keeps from IOS's device
// changes when /dev/usb/hid is v5 (its device ids are v5's); on v4,
// /dev/usb/oh0's list, with no ids, which leaves /dev/usb/hid's first
// device list to the game. `devices` lists every VID:PID, or the error
// when there is no list.
AdapterSeen ogc_adapter(std::int32_t& dev_id, std::string& devices) {
    static usb_device_entry list[32] ATTRIBUTE_ALIGN(32);
    u8 count = 0;
    dev_id = -1;
    devices.clear();
    s32 ret = USB_GetDeviceListUnfiltered(list, 32, USB_CLASS_HID, &count);
    if (ret < 0 && USB_Initialize() >= 0) {
        // libogc's USB was not started (it is on the menu's paths):
        // started now, its list comes in its first device-change reply.
        for (int i = 0; i < 30; ++i) {
            ret = USB_GetDeviceListUnfiltered(list, 32, USB_CLASS_HID, &count);
            if (ret < 0 || count > 0) break;
            usleep(10000);
        }
    }
    // A live USB manager can still have an empty list just after reload.
    // Allow its first enumeration to arrive before reporting absence.
    while (ret >= 0 && count == 0 && ms_since_ios_reload() < 2500) {
        usleep(100000);
        ret = USB_GetDeviceListUnfiltered(list, 32, USB_CLASS_HID, &count);
    }
    if (ret < 0) {
        devices = "no list: error " + std::to_string(ret);
        return AdapterSeen::Unknown;
    }
    rthid_device hid_devices[RTHID_DEVICE_MAX];
    for (unsigned i = 0; i < count && i < RTHID_DEVICE_MAX; ++i) {
        hid_devices[i] = {list[i].device_id, (static_cast<std::uint32_t>(list[i].vid) << 16) | list[i].pid};
    }
    const unsigned listed = g_handoff_devices.restore_if_empty(hid_devices, count);
    if (listed != count) {
        logf("USB controllers: using the device list read before USB shutdown; the game will verify each interface.\n");
    }
    count = static_cast<u8>(listed);
    HidPadsRemember(hid_devices, count);
    // And /dev/usb/ven's list (every device that is not HID), for wired
    // Xbox 360 pads, which need no config.
    {
        static usb_device_entry ven[32] ATTRIBUTE_ALIGN(32);
        u8 ven_count = 0;
        rthid_device ven_devices[RTHID_DEVICE_MAX];
        // Only a list with devices in it is news here. libogc's USB is
        // shut down and started again on the way to a game, and its first
        // list after that came back empty in Dolphin with the pad plugged
        // in: taken as an unplug, the game got no Xbox handle. The launch's
        // own refresh (HidPadsPreload) still notices a real unplug.
        const bool listed = USB_GetDeviceListUnfiltered(ven, 32, 0xFF, &ven_count) >= 0;
        if (listed && ven_count != 0) {
            for (unsigned i = 0; i < ven_count && i < RTHID_DEVICE_MAX; ++i) {
                ven_devices[i] = {ven[i].device_id, (static_cast<std::uint32_t>(ven[i].vid) << 16) | ven[i].pid};
            }
            HidPadsRememberVen(ven_devices, ven_count < RTHID_DEVICE_MAX ? ven_count : RTHID_DEVICE_MAX);
        }
    }
    const std::uint32_t xbox = HidPadsXboxPlugged();
    bool found = false;
    bool hid_found = xbox != 0;
    for (unsigned i = 0; i < count && i < 32; ++i) {
        char one[16];
        const auto id = hid_devices[i].vid_pid;
        std::snprintf(one, sizeof(one), "%s%04x:%04x", i ? ", " : "",
                      static_cast<unsigned>(id >> 16), static_cast<unsigned>(id & 65535u));
        devices += one;
        hid_found = hid_found || HidPadsConfigured(hid_devices[i].vid_pid);
        if (id == GCAD_VID_PID && !found) {
            found = true;
            if (hid_devices[i].id != 0) dev_id = hid_devices[i].id;
        }
    }
    if (devices.empty()) devices = "no devices";
    if (xbox != 0) {
        char one[48];
        std::snprintf(one, sizeof(one), "; Xbox 360 pad %04x:%04x on /dev/usb/ven", static_cast<unsigned>(xbox >> 16),
                      static_cast<unsigned>(xbox & 65535u));
        devices += one;
    }
    return found || hid_found ? AdapterSeen::Found : AdapterSeen::Missing;
}

void forget_usb_hid_stuck() { g_hid_stuck = false; }

void usb_hid_is_v4() { g_hid_v4 = true; }

bool usb_hid_present() {
    if (g_own.fd >= 0) return true;
    std::int32_t fd = -1;
    std::uint32_t version = 0;
    std::string why;
    if (!open_usb_hid(fd, version, why)) return false;
    IOS_Close(fd);
    return true;
}

void close_adapter_session() {
    // Always closed, stuck or not, as before: own_adapter_list relies on the
    // close answering to tell a live module from a dead one. No Shutdown
    // (see g_own).
    g_own.close(false);
}

void forget_usb_hid_handle() {
    // Called by reload_ios while the old IOS still answers. A stuck module
    // keeps its stuck state: nothing says the new IOS answers better, and
    // forget_usb_hid_stuck is for whoever has tested it.
    g_own.forget_for_reload(g_hid_stuck);
    g_handoff_devices.clear();
}

void release_menu_usb_for_game() {
    const auto& catalog = HidPadsCatalog();
    g_handoff_devices.remember(catalog.known, catalog.known_count);
    USB_Deinitialize();
}

// v5 through our own handle: the list from a fresh handle, once more
// after the devices of a fresh IOS have had 2.5 s to show up.
AdapterSeen own_adapter_list(std::string& how) {
    std::string why;
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (g_own.fd < 0 && !open_usb_hid(g_own.fd, g_own.version, why)) {
            how = "unknown, taken as plugged in (" + why + ")";
            logf("GameCube adapter: our own USB handle failed: %s\n", why.c_str());
            g_own.fd = -1;
            return AdapterSeen::Unknown;
        }
        if (g_own.version != 5) return AdapterSeen::Unknown;  // v4: libogc's oh0 list (the caller)
        s32 count = 0;
        if (!own_list(g_own.fd, count)) {
            // No answer (Dolphin with nothing plugged in) or an error: the
            // handle goes, with the request still in flight, so no reply can
            // reach the game; the caller takes libogc's list instead.
            const bool timed_out = g_hid_stuck;
            close_adapter_session();
            if (timed_out) forget_usb_hid_stuck();  // the close answered: the module is alive
            logf("GameCube adapter: our own /dev/usb/hid v5 gave no device list (%s); libogc's instead\n",
                 timed_out ? "no answer" : std::to_string(count).c_str());
            return AdapterSeen::Unknown;
        }
        std::string devices;
        const auto* list = reinterpret_cast<const std::uint8_t*>(g_own_list);
        for (s32 i = 0; i < count && i < 32; ++i) {
            char one[16];
            std::snprintf(one, sizeof(one), "%s%04x:%04x", i ? ", " : "",
                          static_cast<unsigned>(gcad_get32(list + i * 12 + 4) >> 16),
                          static_cast<unsigned>(gcad_get32(list + i * 12 + 4) & 0xFFFF));
            devices += one;
        }
        rthid_device hid_devices[RTHID_DEVICE_MAX];
        bool hid_found = false;
        for (s32 i = 0; i < count && i < static_cast<s32>(RTHID_DEVICE_MAX); ++i) {
            hid_devices[i] = {static_cast<std::int32_t>(gcad_get32(list + i * 12)), gcad_get32(list + i * 12 + 4)};
        }
        const unsigned restored = g_handoff_devices.restore_if_empty(hid_devices, static_cast<unsigned>(count));
        if (restored != static_cast<unsigned>(count)) {
            count = static_cast<s32>(restored);
            devices.clear();
            for (s32 i = 0; i < count; ++i) {
                const auto id = hid_devices[i].vid_pid;
                char one[16];
                std::snprintf(one, sizeof(one), "%s%04x:%04x", i ? ", " : "",
                              static_cast<unsigned>(id >> 16), static_cast<unsigned>(id & 65535u));
                devices += one;
            }
            logf("USB controllers: using the device list read before USB shutdown; the game will verify each interface.\n");
        }
        HidPadsRemember(hid_devices, count);
        for (s32 i = 0; i < count && i < static_cast<s32>(RTHID_DEVICE_MAX); ++i) {
            hid_found = hid_found || HidPadsConfigured(hid_devices[i].vid_pid);
        }
        g_own.device = -1;
        for (s32 i = 0; i < count; ++i) {
            if (hid_devices[i].vid_pid == GCAD_VID_PID) {
                g_own.device = hid_devices[i].id;
                break;
            }
        }
        // As libogc does after each device change.
        s32 finish = 0;
        g_async_done = false;
        await_async(IOS_IoctlAsync(g_own.fd, GCAD_V5_ATTACH_FINISH, nullptr, 0, nullptr, 0, on_async, nullptr), finish);
        if (g_own.device >= 0 || hid_found || attempt == 1 || ms_since_ios_reload() >= 2500) {
            const char* status = "no mapped USB controller or GameCube adapter plugged in";
            if (g_own.device >= 0) status = "GameCube adapter plugged in";
            else if (hid_found) status = "GameCube adapter not plugged in; mapped USB controller(s) found";
            how = std::string(status) + " (our own " + g_usb_path_used + " v5 lists " +
                  (devices.empty() ? std::string("no devices") : devices) + ")";
            return (g_own.device >= 0 || hid_found) ? AdapterSeen::Found : AdapterSeen::Missing;
        }
        // Just after an IOS reload: a fresh handle once more, later.
        close_adapter_session();
        while (ms_since_ios_reload() < 2500) usleep(100000);
    }
    return AdapterSeen::Unknown;
}

AdapterSeen look_for_gc_adapter(std::string& how) {
    if (!g_hid_stuck) {
        const AdapterSeen seen = own_adapter_list(how);
        if (g_own.version == 5) return seen;
        close_adapter_session();  // v4 (or none): libogc's list as before
    }
    // The USB device list only: /dev/usb/hid is opened later, once, with
    // a timeout (a Wii U's d2x cIOS never answered the open).
    // v5 lists devices once per change, to whoever asks first: the menu's
    // USB (libogc) had the list and keeps it. v4 is asked through oh0.
    // Just after an IOS reload the devices are still being found: up to
    // 2.5 s after it, look again.
    std::int32_t dev_id = -1;
    std::string devices;
    AdapterSeen seen = ogc_adapter(dev_id, devices);
    unsigned waited = 0;
    while (seen == AdapterSeen::Missing && ms_since_ios_reload() < 2500) {
        usleep(100000);
        waited += 100;
        seen = ogc_adapter(dev_id, devices);
    }
    char extra[64] = "";
    if (waited) std::snprintf(extra, sizeof(extra), ", after %u ms more", waited);
    const char* status = "unknown, taken as plugged in";
    if (seen == AdapterSeen::Missing) status = "not plugged in";
    else if (seen == AdapterSeen::Found) {
        status = dev_id >= 0 ? "GameCube adapter plugged in" : "GameCube adapter not plugged in; mapped USB controller(s) found";
    }
    how = std::string(status) +
          " (USB lists " + devices + extra + ")";
    return seen;
}

namespace {

// The way back's three functions: OSReturnToMenu, OSSleepThread (the main
// thread's, every frame) and the SDK's IPC submit routine (save activity).
// Any missing: the adapter works without it, and boot.log says why.
void find_way_back(const DolHeader& dol, const std::vector<CodeRange>& text, PadHook& out) {
    std::vector<CodeRange> all;
    for (std::size_t i = 0; i < kDolSections; ++i) {
        const DolSection& s = dol.sections[i];
        if (s.used()) all.push_back({s.address, reinterpret_cast<const std::uint8_t*>(s.address), s.size});
    }
    std::string error;
    std::uint32_t menu = 0;
    if (!find_os_return_to_menu(text, all, menu, error)) {
        logf("GameCube adapter: no way back to RiftWii by the controller: %s\n", error.c_str());
        return;
    }
    const std::uint32_t sleep = find_os_sleep_thread(text);
    if (sleep == 0) {
        logf("GameCube adapter: no way back to RiftWii by the controller: OSSleepThread was not found\n");
        return;
    }
    IpcSymbols ipc;
    IpcApi api;
    if (!find_ipc_symbols(text, ipc, error) || !find_ipc_api(text, ipc, api, error)) {
        logf("GameCube adapter: no way back to RiftWii by the controller: the IPC submit routine was not found (%s)\n",
             error.c_str());
        return;
    }
    out.return_to_menu = menu;
    out.sleep_thread = sleep;
    out.ipc_submit = api.submit;
    // Optional: without it, a thread that suspends itself to wait (N64
    // Virtual Console's pad thread) can't leave, and every other way still can.
    out.suspend_thread = find_os_suspend_thread(text);
    logf("GameCube adapter: the way back (L, R, Z and Start held %u seconds): OSReturnToMenu at 0x%08x, "
         "OSSleepThread at 0x%08x, the IPC submit routine at 0x%08x, OSSuspendThread %s0x%08x\n",
         static_cast<unsigned>(kLeaveHoldMs / 1000), menu, sleep, api.submit,
         out.suspend_thread != 0 ? "at " : "not found, ", out.suspend_thread);
}

// The HOME Menu's four functions (riftwii/hbmsearch.hpp): the KPAD read,
// WPADProbe, HBMCalc and HBMGetSelectBtnNum. Any missing: the combo stays
// the way back. HBMGetSelectBtnNum is what tells the Operations Guide from
// Close: without it, the stand-in would end when the guide opens and leave
// it without a controller.
void find_home_menu(const DolHeader& dol, const std::vector<CodeRange>& text, const std::vector<CodeRange>& patched,
                    PadHook& out) {
    std::vector<CodeRange> all;
    for (std::size_t i = 0; i < kDolSections; ++i) {
        const DolSection& s = dol.sections[i];
        if (s.used()) all.push_back({s.address, reinterpret_cast<const std::uint8_t*>(s.address), s.size});
    }
    const KpadRead kpad = find_kpad_read(text);
    const std::uint32_t probe = find_wpad_probe(text);
    const HbmFunctions hbm = find_hbm_functions(text, all, find_small_data_base(all, dol.entry));
    // The 2006 KPAD's 136-byte status lays its last fields out apart from
    // the later ones', and only the stand-in builds write it that way: the
    // pad builds without a stand-in never get its read. Nor does the
    // Classic Controller stand-in: that KPAD knows no Classic Controller,
    // so only the Wii Remote forms take it.
    const bool old_kpad = kpad.status_bytes == 136;
    const bool takes_old_kpad = out.standin_nunchuk || out.standin_sideways || out.standin_pointing;
    const char* missing_read = kpad.address == 0                ? "the KPAD read"
                               : old_kpad && !out.standin       ? "a KPAD read for the HOME Menu (the 2006 KPAD's)"
                               : old_kpad && !takes_old_kpad    ? "a KPAD read for Classic Controllers (the 2006 KPAD's)"
                               : probe == 0                     ? "WPADProbe"
                                                                : nullptr;
    const char* missing_menu = hbm.calc == 0 ? "HBMCalc" : hbm.select == 0 ? "HBMGetSelectBtnNum" : nullptr;
    // GameCube controllers as Classic Controllers need only the KPAD read
    // and WPADProbe: without the HOME Menu's functions (Donkey Kong Country
    // Returns' HBMCalc isn't found) they still go in, and the combo stays
    // the way back.
    if (missing_read != nullptr || (missing_menu != nullptr && !out.standin)) {
        logf("GameCube adapter: no HOME Menu by the controller (the combo is the way back): %s was not found\n",
             missing_read != nullptr ? missing_read : missing_menu);
        return;
    }
    out.kpad_read = kpad.address;
    out.kpad_status_bytes = kpad.status_bytes;
    out.kpad_takes_error = kpad.takes_error;
    out.wpad_probe = probe;
    // WPADGetInfoAsync for every stand-in launch: a 2006 KPAD game waits on
    // its answer (the stand-in gives one); a later game's blocking
    // WPADGetInfo would sleep for a real Wii Remote's reply (Mario Strikers
    // Charged after its title), so there it's busy and returns at once.
    // The 2006 WPAD's by its marks; a later one's through its blocking
    // caller, which loads WPAD's control-block table (the 2006 marks also
    // fit another function there: Mario Strikers Charged).
    // A later WPAD whose blocking WPADGetInfo has the request built in (no
    // WPADGetInfoAsync to answer: Scarface: The World Is Yours, whose open
    // world checks the battery every 20 to 60 s) sleeps even with no Wii
    // Remote there; that function is hooked instead, busy before it runs.
    // One scan of the later WPAD's connect table serves all its uses (here
    // and the callback slot below; CodeRabbit on #616).
    WpadConnect2006 later_connect;
    if (out.standin && !old_kpad) later_connect = find_wpad_connect_later(text);
    const std::uint32_t later_table = later_connect.table;
    out.wpad_info = !out.standin ? 0 : old_kpad ? find_wpad_get_info(text) : find_wpad_get_info_later(text, later_table);
    out.info_answers = old_kpad ? RTGCWII_INFO_ANSWER : RTGCWII_INFO_BUSY;
    if (out.standin && !old_kpad && out.wpad_info == 0) {
        out.wpad_info = find_wpad_get_info_blocking(text, later_table);
        if (out.wpad_info != 0) out.info_answers = RTGCWII_INFO_BLOCKING;
    }
    if (out.standin) {
        if (out.info_answers == RTGCWII_INFO_BLOCKING)
            logf("GameCube adapter: WPADGetInfo at 0x%08x, its request built in (busy on the stand-in's channel "
                 "before it runs, so it doesn't sleep)\n",
                 out.wpad_info);
        else if (out.wpad_info != 0)
            logf("GameCube adapter: WPADGetInfoAsync at 0x%08x (%s)\n", out.wpad_info,
                 old_kpad ? "the 2006 KPAD game waits on its answer; the stand-in gives one"
                          : "busy on the stand-in's channel, so a blocking WPADGetInfo doesn't sleep");
        else
            logf("GameCube adapter: WPADGetInfoAsync not found: %s\n",
                 old_kpad ? "a 2006 KPAD game may not take the stand-in"
                          : "a game that waits on a Wii Remote's info may stop there");
    }
    if (old_kpad) {
        const WpadConnect2006 connect06 = find_wpad_connect_2006(text);
        out.wpad_connect_table = connect06.table;
        out.wpad_connect_offset = connect06.offset;
        if (connect06.table != 0)
            logf("GameCube adapter: the 2006 WPAD's connect callbacks (blocks from the table at 0x%08x, the callback "
                 "at +%u): the stand-in is reported connected to the game's callback\n",
                 connect06.table, connect06.offset);
    }
    if (missing_menu != nullptr) {
        logf("GameCube adapter: no HOME Menu by the controller (the combo is the way back): %s was not found; the "
             "KPAD read at 0x%08x (%u-byte statuses%s) and WPADProbe at 0x%08x go in for the GameCube "
             "controllers' stand-in\n",
             missing_menu, kpad.address, kpad.status_bytes, kpad.takes_error ? ", with the error's address" : "",
             probe);
    } else {
        out.hbm_calc = hbm.calc;
        out.hbm_select = hbm.select;
        logf("GameCube adapter: the HOME Menu by the controller: the KPAD read at 0x%08x (%u-byte statuses%s), "
             "WPADProbe at 0x%08x, HBMCalc at 0x%08x, HBMGetSelectBtnNum at 0x%08x\n",
             kpad.address, kpad.status_bytes, kpad.takes_error ? ", with the error's address" : "", probe, hbm.calc,
             hbm.select);
    }
    // Optional, for the Wii Remote + Nunchuk form: KPAD's camera switch,
    // which a game turns on for its pointer menus, shows the stand-in's
    // pointer while it's on.
    // In the code as the packs' patches leave it (CodeRabbit on #610): a
    // pack that moves or changes the switch isn't read at its old place.
    std::vector<CodeRange> dpd_view;
    const bool have_view = code_with_patches(text, patched, dpd_view);
    if (have_view) out.kpad_dpd = find_kpad_enable_dpd(dpd_view);
    if (out.kpad_dpd.enable != 0) {
        logf("GameCube adapter: KPADEnableDPD at 0x%08x (its switch at 0x%08x + channel * %u + 0x%x: with GameCube "
             "controllers as Wii Remotes + Nunchuks, the pointer shows while the game has it on)\n",
             out.kpad_dpd.enable, out.kpad_dpd.base, out.kpad_dpd.stride, out.kpad_dpd.offset);
    } else if (have_view) {
        // A game that never links KPADEnableDPD keeps the camera as KPAD
        // set it at its start (Super Mario Galaxy 2: on): the same switch,
        // found where KPAD reads it.
        out.kpad_dpd = find_kpad_dpd_switch(dpd_view);
        if (out.kpad_dpd.enable != 0)
            logf("GameCube adapter: KPAD's camera switch at 0x%08x + channel * %u + 0x%x (read at 0x%08x; the game "
                 "has no KPADEnableDPD): the pointer shows while it's on\n",
                 out.kpad_dpd.base, out.kpad_dpd.stride, out.kpad_dpd.offset, out.kpad_dpd.enable);
    }
    // Optional: where the game has it (Mario Kart Wii), it reads a Classic
    // Controller's buttons from there, and the stand-in fills it too.
    const KpadUnified unified = find_kpad_unified(text);
    out.kpad_unified = unified.address;
    out.kpad_unified_bytes = unified.status_bytes;
    if (unified.address != 0)
        logf("GameCube adapter: KPADGetUnifiedWpadStatus at 0x%08x (%u-byte statuses): the stand-in's raw status "
             "goes there too\n",
             unified.address, unified.status_bytes);
    // Optional too: the game's own connect callbacks, told as the stand-in
    // comes and goes (GoldenEye 007 reads only the channels they heard of).
    const KpadConnect connect = find_kpad_connect(text, kpad.address);
    if (connect.address != 0 && connect.stride <= 0xFFFFu) {
        out.kpad_connect = connect.block + connect.offset;
        out.kpad_connect_stride = connect.stride;
        logf("GameCube adapter: KPADSetConnectCallback at 0x%08x (callbacks at 0x%08x, every %u bytes): the stand-in "
             "is reported connected to the game's callback\n",
             connect.address, out.kpad_connect, connect.stride);
    }
    // The stand-in below KPAD, for a game that reads WPAD itself and hears
    // of its controllers through WPAD's own connect callback (Punch-Out!!):
    // WPADRead gives the stand-in's report and WPADSetDataFormat is answered
    // for it (in the Wii Remote forms' builds only: the form is picked after
    // this, and install_pad_hook leaves them out of the others), and WPAD's
    // callback is told where KPAD has none of its own.
    if (out.standin) {
        out.wpad_read = find_wpad_read(text);
        out.wpad_set_format = find_wpad_set_format(text);
        logf("GameCube adapter: WPADRead %s0x%08x, WPADSetDataFormat %s0x%08x (with GameCube controllers as Wii "
             "Remotes, a game that reads WPAD itself gets the stand-in's reports)\n",
             out.wpad_read != 0 ? "at " : "not found, ", out.wpad_read,
             out.wpad_set_format != 0 ? "at " : "not found, ", out.wpad_set_format);
        // WPAD's connect callback slot: the 2006 one (find_home_menu), else
        // the later one, which is told only where KPAD has no callback of
        // its own; the extension callback's sits beside it either way.
        WpadConnect2006 slot;
        slot.table = out.wpad_connect_table;
        slot.offset = out.wpad_connect_offset;
        if (slot.table == 0) slot = old_kpad ? find_wpad_connect_later(text) : later_connect;
        if (out.kpad_connect == 0 && out.wpad_connect_table == 0) {
            out.wpad_connect_table = slot.table;
            out.wpad_connect_offset = slot.offset;
            out.wpad_connect_later = slot.table != 0;
            if (slot.table != 0)
                logf("GameCube adapter: WPAD's connect callbacks (blocks from the table at 0x%08x, the callback at "
                     "+%u): the stand-in is reported connected to the game's callback\n",
                     slot.table, slot.offset);
        }
        // The game's WPAD extension callback, told the stand-in's attachment
        // after each "connected" (Super Mario Galaxy 2 asks for a Nunchuk
        // until it hears).
        const WpadConnect2006 ext = find_wpad_extension(text, slot);
        out.wpad_ext_table = ext.table;
        out.wpad_ext_offset = ext.offset;
        if (ext.table != 0)
            logf("GameCube adapter: WPAD's extension callbacks (blocks from the table at 0x%08x, the callback at "
                 "+%u): the game hears of the stand-in's attachment\n",
                 ext.table, ext.offset);
        else
            logf("GameCube adapter: WPAD's extension callbacks not found\n");
    }
}

// The game as loaded, read in place (nothing here writes to it).
class LoadedGame final : public MemoryAccess {
public:
    bool read(std::uint32_t address, std::uint8_t* out, std::size_t length) override {
        std::memcpy(out, reinterpret_cast<const void*>(address), length);
        return true;
    }
    bool write(std::uint32_t, const std::uint8_t*, std::size_t) override { return false; }
};

}  // namespace

bool pad_read_in_place(std::uint32_t address) {
    // As far as the search took an error store to belong to a function
    // (function_start walks back 0x1000 bytes), plus the store's own five
    // words; it stops at the end of its RAM.
    constexpr std::uint32_t kWindow = 0x1000 + 5 * 4;
    const std::uint32_t ram_end = address < 0x90000000u ? 0x81800000u : 0x94000000u;
    if (address >= ram_end || (address & 3u) != 0) return false;
    const std::uint32_t bytes = std::min(kWindow, ram_end - address);
    const std::vector<CodeRange> code = {{address, reinterpret_cast<const std::uint8_t*>(address), bytes}};
    return is_pad_read(code, address);
}

// What the patcher will write, as apply_memory_patches goes: in document
// order, a plain patch with an `original` guard only where the guard
// matches the game as loaded with the earlier writes over it, and a search
// patch wherever its `original` is found in the game's program (`image`,
// its DOL's sections) with the earlier writes over it, at every match. An
// ocarina patch's value is the pattern it looks for (it writes only a
// branch), so it writes nothing here. `applies`: one flag per patch (a
// search with no match writes nothing); `writes`: each write in order.
struct PackWrite {
    std::uint32_t at;
    std::size_t patch;
};
struct PackWrites {
    std::vector<bool> applies;
    std::vector<PackWrite> writes;
    bool incomplete = false;  // more writes than the planning follows (plan_pack_writes)
    std::vector<CodeRange> program;  // the game's program as loaded (its DOL's sections)
    // Where file patches that apply go (their bytes aren't read yet here),
    // and which patch each is (document order: a later write over its bytes
    // decides them).
    struct File {
        std::uint32_t at;
        std::size_t patch;
    };
    std::vector<File> files;
};

PackWrites plan_pack_writes(const std::vector<MemoryPatch>& patches, const std::vector<CodeRange>& image) {
    // Past these the planning would hold the launch up (each byte it checks
    // looks through the earlier writes): the plan is then incomplete, and
    // the checks can't vouch for the pack (as PatchedCode gives up past
    // kMaxPreviewedPatches).
    constexpr std::size_t kMaxMatches = 1024;  // per search patch
    constexpr std::size_t kMaxWrites = PatchedCode::kMaxPreviewedPatches;
    PackWrites out;
    out.applies.assign(patches.size(), false);
    out.program = image;
    const auto byte_at = [&](std::uint32_t address, std::uint8_t& value) {
        const std::uint32_t ram_end = address < 0x90000000u ? 0x81800000u : 0x94000000u;
        if (address < 0x80000000u || address >= ram_end) return false;
        // libogc's exception stubs at 0x80001000-0x800012ff are cleared
        // before the patches go in (boot.cpp), so a guard there reads 0.
        value = address - 0x80001000u < 0x300u ? 0 : *reinterpret_cast<const volatile std::uint8_t*>(address);
        for (const PackWrite& w : out.writes) {  // the latest write over it wins
            const std::vector<std::uint8_t>& bytes = patches[w.patch].value;
            if (address >= w.at && address - w.at < bytes.size()) value = bytes[address - w.at];
        }
        return true;
    };
    const auto matches_at = [&](std::uint32_t at, const std::vector<std::uint8_t>& pattern) {
        for (std::size_t k = 0; k < pattern.size(); ++k) {
            std::uint8_t now = 0;
            if (!byte_at(at + static_cast<std::uint32_t>(k), now) || now != pattern[k]) return false;
        }
        return true;
    };
    // Where an earlier file patch that applies may have written: its bytes
    // aren't read yet here, nor its length (up to the largest a file may
    // be), so a guard or a search that reads there can't be judged.
    const auto in_files = [&](std::uint64_t from, std::uint64_t to) {  // [from, to)
        if (from >= to) return false;  // a guard that reads nothing (an unconditional patch)
        for (const PackWrites::File& f : out.files) {
            if (from < std::uint64_t(f.at) + kMaxMemoryValueBytes && f.at < to) return true;
        }
        return false;
    };
    const auto program_in_files = [&]() {
        for (const CodeRange& r : image) {
            if (in_files(r.address, std::uint64_t(r.address) + r.size)) return true;
        }
        return false;
    };
    for (std::size_t i = 0; i < patches.size(); ++i) {
        const MemoryPatch& patch = patches[i];
        // A file patch: its guard judged like any other's, its place noted.
        // A file-backed search can't be planned (its bytes aren't read yet
        // and its places are wherever its pattern matches): the checks
        // can't vouch for such a pack.
        if (patch.search && !patch.ocarina && !patch.original.empty() && program_in_files()) {
            out.incomplete = true;
            return out;
        }
        if (!patch.valuefile.empty() && patch.value.empty() && patch.search) {
            // ...when its pattern is there: one that matches nowhere
            // writes nothing.
            // Only the program image, as the search itself reads only the
            // loaded sections: the game's own bytes (a plain compare), or,
            // where an earlier write reaches, the writes over them.
            const std::uint64_t align = patch.align == 0 ? 1 : patch.align;
            const std::size_t n = patch.original.size();
            // Starts whose pattern reaches into an earlier write are checked
            // with the writes over them: a pattern an earlier patch replaced
            // isn't there when this search runs (CodeRabbit, fork #25).
            std::vector<std::pair<std::uint64_t, std::uint64_t>> near;
            for (const PackWrite& w : out.writes) {
                const std::uint64_t lo = w.at >= n ? w.at - n + 1 : 0;
                near.push_back({lo, std::uint64_t(w.at) + patches[w.patch].value.size()});
            }
            std::sort(near.begin(), near.end());
            bool found = false;
            for (const CodeRange& r : image) {
                if (found || n == 0 || r.size < n) continue;
                std::size_t next = 0;  // sections aren't in address order
                std::uint64_t pos = r.address;
                if (pos % align != 0) pos += align - pos % align;
                const std::uint64_t last = std::uint64_t(r.address) + r.size - n;
                for (; pos <= last && !found; pos += align) {
                    while (next < near.size() && near[next].second <= pos) ++next;
                    bool overlaid = false;
                    for (std::size_t k = next; !overlaid && k < near.size() && near[k].first <= pos; ++k)
                        overlaid = pos < near[k].second;
                    const std::uint8_t* raw = r.bytes + (pos - r.address);
                    found = overlaid ? matches_at(static_cast<std::uint32_t>(pos), patch.original)
                                     : raw[0] == patch.original[0] && std::memcmp(raw, patch.original.data(), n) == 0;
                }
            }
            if (found) {
                out.incomplete = true;
                return out;
            }
            continue;
        }
        if (!patch.valuefile.empty() && patch.value.empty()) {
            if (patch.has_offset && !patch.search && !patch.ocarina &&
                in_files(memory_patch_target(patch), std::uint64_t(memory_patch_target(patch)) + patch.original.size())) {
                out.incomplete = true;
                return out;
            }
            if (patch.has_offset && !patch.search && !patch.ocarina &&
                matches_at(memory_patch_target(patch), patch.original)) {
                out.applies[i] = true;
                out.files.push_back({memory_patch_target(patch), i});
            }
            continue;
        }
        if (patch.ocarina || patch.value.empty()) continue;
        if (patch.search) {
            if (patch.original.empty()) continue;
            const std::uint64_t align = patch.align == 0 ? 1 : patch.align;
            const std::size_t step = std::max(patch.original.size(), patch.value.size());
            // Starts whose pattern reaches into an earlier write: the bytes
            // there may be the write's, not the game's, so those are checked
            // whatever the game's byte (as disjoint ranges, in order).
            std::vector<std::pair<std::uint64_t, std::uint64_t>> near;
            for (const PackWrite& w : out.writes) {
                const std::uint64_t lo = w.at >= patch.original.size() ? w.at - patch.original.size() + 1 : 0;
                near.push_back({lo, std::uint64_t(w.at) + patches[w.patch].value.size()});
            }
            std::sort(near.begin(), near.end());
            std::size_t next = 0;
            const auto near_write = [&](std::uint64_t pos) {
                while (next < near.size() && near[next].second <= pos) ++next;
                for (std::size_t n = next; n < near.size() && near[n].first <= pos; ++n) {
                    if (pos < near[n].second) return true;
                }
                return false;
            };
            std::size_t found = 0;
            // One cursor for all the sections, in the loader's order, as
            // apply_search keeps it: after a match in a higher section, a
            // lower one is searched only past it.
            std::uint64_t from = 0;
            for (const CodeRange& r : image) {
                if (r.size < patch.original.size()) continue;
                next = 0;  // sections aren't in address order
                std::uint64_t pos = std::max<std::uint64_t>(r.address, from);
                if (pos % align != 0) pos += align - pos % align;
                const std::uint64_t last = std::uint64_t(r.address) + r.size - patch.original.size();
                while (pos <= last) {
                    // Where no earlier write reaches, the game's own bytes
                    // decide (a plain compare); near one, the pattern is
                    // checked with the earlier writes over it.
                    const std::uint8_t* raw = r.bytes + (pos - r.address);
                    const bool overlaid = near_write(pos);
                    if (overlaid ? matches_at(static_cast<std::uint32_t>(pos), patch.original)
                                 : raw[0] == patch.original[0] &&
                                       std::memcmp(raw, patch.original.data(), patch.original.size()) == 0) {
                        // One more write than the planning follows: incomplete
                        // (a plan with exactly the limit is complete).
                        if (found >= kMaxMatches || out.writes.size() >= kMaxWrites) {
                            out.incomplete = true;
                            return out;
                        }
                        out.writes.push_back({static_cast<std::uint32_t>(pos), i});
                        out.applies[i] = true;
                        ++found;
                        pos += step;
                        from = pos;
                        if (pos % align != 0) pos += align - pos % align;
                        continue;
                    }
                    pos += align;
                }
            }
            continue;
        }
        if (!patch.has_offset) continue;
        const std::uint32_t at = memory_patch_target(patch);
        if (in_files(at, std::uint64_t(at) + patch.original.size())) {
            out.incomplete = true;
            return out;
        }
        if (!matches_at(at, patch.original)) continue;
        if (out.writes.size() >= kMaxWrites) {
            out.incomplete = true;
            return out;
        }
        out.applies[i] = true;
        out.writes.push_back({at, i});
    }
    return out;
}

// The pack's writes as they end up, joined: writes whose words touch or
// overlap make one span, widened to whole words (the searches read
// instructions at word steps from a range's start: a one-byte write just
// before a PADRead would otherwise shift every instruction after it,
// CodeRabbit CLI, fork #16), so a PADRead or a port read split across two
// writes is still one (fork #16; CodeRabbit, fork #25). Each byte is the
// last planned write's over it, plain or search, else the game's (only in a
// word a write touches: no game instruction is taken in whole). Each span
// is built once from the writes over it, so the copies are no bigger than
// the memory written; past kMaxSpanBytes in all, false (the write limit
// doesn't bound what many large matches of one place copy: a plan the
// checks can't follow, CodeRabbit, fork #25).
using SearchSpans = std::vector<std::pair<std::uint32_t, std::vector<std::uint8_t>>>;
constexpr std::uint64_t kMaxSpanBytes = std::uint64_t(2) << 20;
bool pack_spans(const std::vector<MemoryPatch>& patches, const PackWrites& plan, SearchSpans& spans) {
    spans.clear();
    std::vector<std::pair<std::uint64_t, std::uint64_t>> words;  // [from, to), whole words
    for (const PackWrite& w : plan.writes) {
        const std::uint64_t from = w.at & ~std::uint64_t(3);
        const std::uint64_t to = (std::uint64_t(w.at) + patches[w.patch].value.size() + 3) & ~std::uint64_t(3);
        words.push_back({from, to});
    }
    std::sort(words.begin(), words.end());
    std::vector<std::pair<std::uint64_t, std::uint64_t>> joined;
    for (const auto& r : words) {
        if (!joined.empty() && r.first <= joined.back().second) {
            joined.back().second = std::max(joined.back().second, r.second);
            continue;
        }
        joined.push_back(r);
    }
    std::uint64_t total = 0;
    for (const auto& r : joined) {
        // Only game RAM is read below (MEM1, or MEM2 to its end, as
        // byte_at's guards): a write reaching past it is one the checks
        // can't follow (CodeRabbit, fork #25).
        const bool mem1 = r.first >= 0x80000000u && r.second <= 0x81800000u;
        const bool mem2 = r.first >= 0x90000000u && r.second <= 0x94000000u;
        if (!mem1 && !mem2) return false;
        total += r.second - r.first;
    }
    if (total > kMaxSpanBytes) return false;
    // Which patch last wrote each byte (kNone: none, the game's).
    constexpr std::size_t kNone = ~std::size_t(0);
    std::vector<std::vector<std::size_t>> last;
    for (const auto& r : joined) {
        spans.push_back({static_cast<std::uint32_t>(r.first), std::vector<std::uint8_t>(r.second - r.first)});
        last.emplace_back(static_cast<std::size_t>(r.second - r.first), kNone);
    }
    // Every write, in order, over the spans it reaches (a later one wins).
    for (const PackWrite& w : plan.writes) {
        const std::vector<std::uint8_t>& value = patches[w.patch].value;
        const std::uint64_t at = w.at, end = at + value.size();
        std::size_t k = static_cast<std::size_t>(
            std::upper_bound(joined.begin(), joined.end(), at,
                             [](std::uint64_t x, const std::pair<std::uint64_t, std::uint64_t>& r) { return x < r.first; }) -
            joined.begin());
        if (k != 0) --k;
        for (; k < joined.size() && joined[k].first < end; ++k) {
            const std::uint64_t lo = std::max(at, joined[k].first), hi = std::min(end, joined[k].second);
            for (std::uint64_t x = lo; x < hi; ++x) {
                spans[k].second[x - joined[k].first] = value[x - at];
                last[k][x - joined[k].first] = w.patch;
            }
        }
    }
    // The game's own bytes in the rest of the words a write touches.
    for (std::size_t k = 0; k < spans.size(); ++k) {
        for (std::size_t x = 0; x < spans[k].second.size(); ++x) {
            if (last[k][x] != kNone) continue;
            const std::uint32_t address = spans[k].first + static_cast<std::uint32_t>(x);
            // libogc's exception stubs are cleared before the patches go
            // in (boot.cpp), as byte_at reads them.
            spans[k].second[x] =
                address - 0x80001000u < 0x300u ? 0 : *reinterpret_cast<const volatile std::uint8_t*>(address);
        }
    }
    // A word a later file patch may still write over (its bytes aren't
    // read yet here, up to the largest a file may be) where no write after
    // it decides every byte is cut out, and the span split there: the
    // launch may leave other bytes there, and a file's own bytes aren't
    // looked at either; the words around it stay (CodeRabbit CLI, fork #25).
    if (!plan.files.empty()) {
        SearchSpans kept;
        for (std::size_t k = 0; k < spans.size(); ++k) {
            const std::uint64_t lo = joined[k].first;
            std::vector<bool> open(spans[k].second.size(), false);
            for (const PackWrites::File& f : plan.files) {
                const std::uint64_t from = std::max<std::uint64_t>(lo, f.at);
                const std::uint64_t to = std::min<std::uint64_t>(joined[k].second, std::uint64_t(f.at) + kMaxMemoryValueBytes);
                for (std::uint64_t x = from; x < to; ++x) {
                    const std::size_t by = last[k][x - lo];
                    if (by == kNone || by < f.patch) open[(x - lo) & ~std::uint64_t(3)] = true;  // its word
                }
            }
            std::size_t run = 0;  // the start of the words kept so far
            for (std::size_t w = 0; w <= spans[k].second.size(); w += 4) {
                if (w < spans[k].second.size() && !open[w]) continue;
                if (w > run)
                    kept.push_back({spans[k].first + static_cast<std::uint32_t>(run),
                                    std::vector<std::uint8_t>(spans[k].second.begin() + static_cast<std::ptrdiff_t>(run),
                                                              spans[k].second.begin() + static_cast<std::ptrdiff_t>(w))});
                run = w + 4;
            }
        }
        spans = std::move(kept);
    }
    return true;
}

// Whether a pack's own bytes (what its memory patches write, joined where
// they touch: pack_spans; not the game code around them: a game's SI
// library reads the same registers) read the GameCube controller ports
// straight from the hardware (reads_gc_ports). A value kept in a file isn't
// loaded yet here, so isn't looked at.
bool pack_reads_gc_ports(const SearchSpans& spans) {
    for (const auto& span : spans) {
        if (reads_gc_ports(span.second.data(), span.second.size())) return true;
    }
    return false;
}

// Whether a pack's memory patches take over the game's Wii controller
// reads: a write that turns a `bl` (as loaded, before the patches) to
// WPADProbe or to the KPAD read (or the short KPADRead just before it, which
// hands its call on) into a `bl` into the pack's own code (a write of its,
// or anywhere in a program file it brings), as our own full packs reroute
// them (Kamen Rider: Dragon Knight's); or any write over the first words of
// WPADProbe or the KPAD read, where the stand-in's hooks would go and so
// can't (quatric's Kirby's Return to Dream Land patch hooks both there),
// or over WPADRead's (`wpad_read`, 0 for a form that doesn't hook it), which
// the Wii Remote forms' stand-in hooks too (CodeRabbit, fork #25).
bool pack_wraps_wii_reads(const std::vector<MemoryPatch>& patches, const PackWrites& plan, std::uint32_t kpad_read,
                          std::uint32_t wpad_probe, std::uint32_t wpad_read, bool program_from_pack) {
    constexpr std::uint32_t kReadEntry = 0x20;  // KPADRead's few words ahead of the read itself
    const auto is_bl = [](std::uint32_t w) { return (w & 0xFC000003u) == 0x48000001u; };
    const auto bl_target = [](std::uint32_t at, std::uint32_t w) {
        std::uint32_t offset = w & 0x03FFFFFCu;
        if (offset & 0x02000000u) offset |= 0xFC000000u;  // a call back: sign-extended
        return at + offset;
    };
    // The word at `address` once every write is done, in order (a later
    // write over an earlier one wins), and whether any wrote a byte of it.
    const auto final_word = [&](std::uint32_t address, bool& written) {
        std::uint32_t word = *reinterpret_cast<const volatile std::uint32_t*>(address);
        written = false;
        for (const PackWrite& w : plan.writes) {
            const std::vector<std::uint8_t>& bytes = patches[w.patch].value;
            for (std::uint32_t b = 0; b < 4; ++b) {
                const std::uint32_t where = address + b;
                if (where < w.at || where - w.at >= bytes.size()) continue;
                const unsigned shift = 8u * (3u - b);
                word = (word & ~(0xFFu << shift)) | (std::uint32_t(bytes[where - w.at]) << shift);
                written = true;
            }
        }
        return word;
    };
    // The pack's own code: bytes one of its writes put there, in a program
    // file it brings (its loaded sections), or where a patch whose value is
    // in a file goes (not loaded yet here, so up to the largest such file:
    // our LEGO Star Wars III pack's code blob).
    const auto pack_code = [&](std::uint32_t address) {
        if (program_from_pack) {
            for (const CodeRange& r : plan.program) {
                if (address >= r.address && address - r.address < r.size) return true;
            }
        }
        // (Only file patches that apply, as planned; a file's length isn't
        // known until it's read, so the largest a file may be stands in.)
        for (const PackWrites::File& f : plan.files) {
            if (address >= f.at && address - f.at < kMaxMemoryValueBytes) return true;
        }
        for (const PackWrite& w : plan.writes) {
            if (address >= w.at && address - w.at < patches[w.patch].value.size()) return true;
        }
        return false;
    };
    // Over the function's first few words, changed: the pack's hook is there,
    // at the entry or just past it (a hook that keeps the `stwu` and
    // branches from the next word still runs beside the stand-in's); a
    // write putting the same words back hooks nothing.
    constexpr std::uint32_t kEntryWords = 4;
    // Whether a byte of the word at `address` may end up a file patch's
    // (its bytes not read yet here, up to the largest a file may be): one
    // in a file patch's reach that no later write of known bytes covers
    // (a later write decides that byte, as final_word reads it).
    const auto file_left = [&](std::uint32_t address) {
        for (const PackWrites::File& f : plan.files) {
            for (std::uint32_t b = 0; b < 4; ++b) {
                const std::uint32_t byte = address + b;
                if (byte < f.at || byte - f.at >= kMaxMemoryValueBytes) continue;
                bool decided = false;
                for (const PackWrite& w : plan.writes) {
                    if (w.patch > f.patch && byte >= w.at && byte - w.at < patches[w.patch].value.size()) {
                        decided = true;
                        break;
                    }
                }
                if (!decided) return true;
            }
        }
        return false;
    };
    for (const std::uint32_t entry : {wpad_probe, kpad_read, wpad_read}) {
        if (entry == 0 || entry >= 0x81800000u - 4 * kEntryWords) continue;
        for (std::uint32_t w = 0; w < kEntryWords; ++w) {
            if (file_left(entry + 4 * w)) return true;
        }
        for (std::uint32_t w = 0; w < kEntryWords; ++w) {
            const std::uint32_t at = entry + 4 * w;
            bool written = false;
            const std::uint32_t final = final_word(at, written);
            if (written && final != *reinterpret_cast<const volatile std::uint32_t*>(at)) return true;
        }
    }
    // A call to either in the game's program where a file patch may write
    // (any byte of the call's word), unless later known writes decide it.
    for (const PackWrites::File& file : plan.files) {
        for (const CodeRange& r : plan.program) {
            const std::uint64_t from = std::max<std::uint64_t>(file.at, r.address) & ~std::uint64_t(3);
            const std::uint64_t to = std::min<std::uint64_t>(std::uint64_t(file.at) + kMaxMemoryValueBytes,
                                                             std::uint64_t(r.address) + r.size);
            for (std::uint64_t at = from; at < to && at + 4 <= std::uint64_t(r.address) + r.size; at += 4) {
                const std::uint32_t old = *reinterpret_cast<const volatile std::uint32_t*>(static_cast<std::uint32_t>(at));
                if (!is_bl(old) || !file_left(static_cast<std::uint32_t>(at))) continue;
                const std::uint32_t target = bl_target(static_cast<std::uint32_t>(at), old);
                if ((wpad_probe != 0 && target == wpad_probe) ||
                    (kpad_read != 0 && target <= kpad_read && kpad_read - target <= kReadEntry))
                    return true;
            }
        }
    }
    // Every word a write touches (a call rerouted inside a longer write
    // counts as much as a 4-byte one), as it ends up after all the writes.
    for (const PackWrite& w : plan.writes) {
        const std::size_t size = patches[w.patch].value.size();
        for (std::uint32_t at = w.at & ~3u; at < w.at + size && at < 0x81800000u - 4; at += 4) {
            // Only a word that called either can be rerouted: the others
            // aren't rebuilt (each rebuild looks through every write).
            const std::uint32_t old = *reinterpret_cast<const volatile std::uint32_t*>(at);
            if (!is_bl(old)) continue;
            const std::uint32_t target = bl_target(at, old);
            if (!((wpad_probe != 0 && target == wpad_probe) ||
                  (kpad_read != 0 && target <= kpad_read && kpad_read - target <= kReadEntry)))
                continue;
            bool written = false;
            const std::uint32_t now = final_word(at, written);
            // A call the pack sends into its own code: the same call written
            // back, or one moved to other game code, reroutes nothing.
            if (!written || !is_bl(now) || bl_target(at, now) == target) continue;
            if (pack_code(bl_target(at, now))) return true;
        }
    }
    return false;
}

// Whether a pack's writes, in the plan's order, leave a PADRead (or one the
// search can't be sure of): a search patch's writes, or a plain patch let
// through by one, aren't in PatchedCode's view (plain patches alone), so
// they're searched here (`spans`: the writes joined, pack_spans). `found`:
// what was found (its `read` 0 when unsure of it). Two PADControlMotor candidates there leave a single
// PADRead found, as find_pad_symbols_with_patches takes a plain patch's
// (CodeRabbit, fork #25).
bool search_writes_pad_read(const SearchSpans& spans, PadSymbols& found) {
    found = PadSymbols{};
    if (spans.empty()) return false;
    std::vector<CodeRange> ranges;
    for (const auto& span : spans) ranges.push_back({span.first, span.second.data(), span.second.size()});
    std::string error;
    const bool sure = find_pad_symbols(ranges, found, error);
    const bool read_unsure = !sure && error.find("PADRead") != std::string::npos;
    if (read_unsure) found.read = 0;
    if (!sure) found.control_motor = 0;
    return found.read != 0 || read_unsure;
}

void read_patched_code(const std::vector<MemoryPatch>& patches, PatchedCode& out) {
    LoadedGame game;
    std::string error;
    if (!out.build(patches, game, error))
        logf("GameCube adapter: a PADRead in the packs' memory patches isn't looked for: %s\n", error.c_str());
}

bool find_pad_functions(const DolHeader& dol, bool demo, const std::vector<MemoryPatch>& patches,
                        bool program_from_pack, bool standin, bool nunchuk, bool sideways, bool pointing, bool usb,
                        PadHook& out, std::string& why) {
    out = PadHook{};
    out.demo = demo;
    out.standin = standin;
    out.standin_nunchuk = standin && nunchuk;
    out.standin_sideways = standin && sideways;
    out.standin_pointing = standin && pointing;
    std::vector<CodeRange> text;
    for (std::size_t i = 0; i < kDolTextSections; ++i) {
        const DolSection& s = dol.sections[i];
        if (s.used()) text.push_back({s.address, reinterpret_cast<const std::uint8_t*>(s.address), s.size});
    }
    // A pack's PADRead in its <memory> patches is searched where it will
    // be written; the hooks go in after the patches, so they find it there.
    PatchedCode patched_code;
    read_patched_code(patches, patched_code);
    const std::vector<CodeRange>& patched = patched_code.ranges();
    PadSymbols pad;
    bool from_patch = false;
    std::string error;
    if (!find_pad_symbols_with_patches(text, patched, pad, from_patch, error)) {
        why = "the PAD search is unsure: " + error;
        return false;
    }
    if (pad.read == 0 && !standin) {
        why = "this game has no GameCube controller support (no PADRead)";
        return false;
    }
    // (With no PADRead and no adapter nothing reads the Wii's own GameCube
    // ports, unless a pack's search patch adds one: decided below, once the
    // pack's writes are planned; CodeRabbit, fork #25.)
    if (pad.read == 0) {
        // GameCube controllers as Classic Controllers: the hook reads the
        // adapter itself from the game's KPAD reads (#587's question 4).
        // The screen stays lit through the Wii Remotes' dimming reset,
        // which GameCube presses never reach (there's no ports' one to use).
        // In the code as the packs' patches leave it, so a pack that
        // changes the reset (or the call to it) isn't missed: not found
        // there, none is used (CodeRabbit on #609).
        std::vector<CodeRange> view;
        pad.idle_reset =
            code_with_patches(text, patched, view) ? find_wii_remote_idle_reset(view, find_wpad_probe(view)) : 0;
        if (usb)
            logf("GameCube adapter: no PADRead in this game; the hook reads the GameCube controllers itself for "
                 "their stand-in\n");
    }
    out.read = pad.read;
    out.read_in_patch = from_patch;
    // A pack or the setting, never both, on the controller's input (#587).
    // A pack handles the controller itself (our full packs: their own
    // layout and their own stand-in code), so the setting stays off for
    // this launch, when it has its own PADRead in its memory patches, when
    // it reroutes the game's calls to its Wii controller reads into its own
    // code (Kamen Rider: Dragon Knight's), or when it replaced the game's
    // program file with one that has a PADRead (our LEGO Star Wars and
    // Batman: The Brave and the Bold packs: a PADRead added to their own
    // main.dol, already loaded here; their code beside it is in a file
    // that isn't) or whose code reads the GameCube ports. A port-reading
    // patch over the game's own program (quatric's) isn't one, though the
    // game links a PADRead it never calls (Kirby's Return to Dream Land).
    // What the packs' memory patches will write, with search patches found
    // in the game's program as loaded (its code and data sections).
    std::vector<CodeRange> image;
    for (std::size_t i = 0; standin && i < kDolSections; ++i) {
        const DolSection& s = dol.sections[i];
        if (s.used()) image.push_back({s.address, reinterpret_cast<const std::uint8_t*>(s.address), s.size});
    }
    const PackWrites plan = standin ? plan_pack_writes(patches, image) : PackWrites{};
    // The pack's writes as they end up, in order, joined (pack_spans): where
    // a PADRead they write is looked for, and its port reads and SIProbe.
    // Past their byte limit the checks can't follow the plan (as with too
    // many writes).
    SearchSpans all_spans;
    const bool spans_fit = standin && !plan.incomplete && pack_spans(patches, plan, all_spans);
    // Its PADRead in a plain patch (found above), or in what its writes leave
    // as the plan orders them: a search patch's, and a plain patch whose
    // guard only matches once an earlier search wrote it, which the plain
    // patches' preview alone misses (CodeRabbit, fork #25).
    PadSymbols searched;
    const bool pack_reads_pad = standin && (from_patch || (spans_fit && search_writes_pad_read(all_spans, searched)));
    // The pack's writes as ranges, for the SIProbe search when the hooked
    // PADRead is one a search patch adds: the game's code read as all the
    // writes leave it, plain ones too (CodeRabbit CLI, fork #16; fork #25).
    std::vector<CodeRange> written;
    // A PADRead found there is the one hooked for the adapter, as a plain
    // patch's would be: over none of the game's, or
    // over the game's own (a copy the pack doesn't call: CodeRabbit, fork
    // #25). Its place is checked again once the patches are in
    // (pad_read_in_place: CodeRabbit CLI, fork #16).
    if (searched.read != 0) {
        pad.read = searched.read;
        pad.read_sites = searched.read_sites;
        out.read = pad.read;
        out.read_in_patch = true;
        for (const auto& span : all_spans) written.push_back({span.first, span.second.data(), span.second.size()});
    }
    // The Wii's own ports are read only through a PADRead (the game's, or
    // one a pack's patch adds, plain or search): with none and no adapter,
    // nothing could read a GameCube controller.
    if (pad.read == 0 && !usb) {
        why = "this game has no GameCube controller code, and with no adapter nothing reads the Wii's own GameCube ports";
        return false;
    }
    const bool program_pad = standin && !pack_reads_pad && program_from_pack && pad.read != 0;
    // (Below: a pack with more memory writes than the checks follow counts
    // as bringing its own controller code: they can't vouch that it doesn't.)
    const bool too_many = standin && !pack_reads_pad && !program_pad && (plan.incomplete || !spans_fit);
    // WPADRead's entry counts where the stand-in's form hooks it.
    const std::uint32_t wpad_read_hooked =
        standin && (nunchuk || sideways || pointing) ? find_wpad_read(text) : 0u;
    const bool pack_wraps = standin && !pack_reads_pad && !program_pad && !too_many &&
                            pack_wraps_wii_reads(patches, plan, find_kpad_read(text).address, find_wpad_probe(text),
                                                 wpad_read_hooked, program_from_pack);
    const bool pack_ports =
        standin && !pack_reads_pad && !program_pad && !too_many && !pack_wraps && pack_reads_gc_ports(all_spans);
    if (pack_reads_pad || program_pad || too_many || pack_wraps || (pack_ports && program_from_pack)) {
        // (Its own code wins over the game's setting and over a pack's
        // riftwii-gc-controllers request alike: one input, never both.)
        logf("GameCube adapter: a pack brings its own controller code (%s), so the GameCube controllers' stand-in "
             "stays off for this launch\n",
             pack_reads_pad ? "its PADRead is in its memory patches"
             : program_pad  ? "the program file it brings has a PADRead"
             : too_many     ? "its memory patches write more than the checks follow"
             : pack_wraps   ? "it hooks or reroutes the game's Wii controller reads"
                            : "it reads the GameCube ports, in the program file it brings");
        // The menu can't know this before the game's code is in memory:
        // said on the launch screen too.
        // (Written out twice: tools/lang_source.py finds tr's literals.)
        ProgressNote(tr("A pack brings its own controls: 'Controllers act as' is off."),
                     "A pack brings its own controls: 'Controllers act as' is off.");
        standin = false;
        out.standin = out.standin_nunchuk = out.standin_sideways = out.standin_pointing = false;
    } else if (pack_ports) {
        // A pack that reads the GameCube ports straight from the hardware
        // over the game's own program (quatric's GameCube controller patches)
        // gets nothing there from the adapter, which isn't a port: until
        // RiftWii routes the adapter into such a pack, the stand-in keeps
        // the controls (the pack's code then has no pad to read). Once it
        // does, the stand-in becomes "connected only" (standin_inputless).
        // quatric's Excite Truck patch also takes its input inside KPAD's
        // handling of a Wii Remote's reports, which a connection alone
        // doesn't start.
        logf("GameCube adapter: a pack reads the GameCube controller ports itself, which the adapter doesn't "
             "answer; the GameCube controllers' stand-in keeps the controls for this launch\n");
    }
    // No PADRead and the stand-in off for the pack: nothing to hook, so no
    // adapter handle is taken (the caller starts the game without one).
    if (pad.read == 0 && !standin) {
        why = "a pack brings its own controller code, and this game has no PADRead for the GameCube controllers";
        return false;
    }
    out.read_sites = pad.read_sites;
    out.motor = pad.control_motor;
    out.idle_reset = pad.idle_reset;
    // A screen-dimming reset of 0 means none was found: with the Wii's
    // burn-in reduction on, adapter-only play may then dim the screen.
    if (out.read != 0) {
        logf("GameCube adapter: PADRead at 0x%08x (%u error stores%s), PADControlMotor at 0x%08x, screen-dimming reset "
             "at 0x%08x\n",
             out.read, out.read_sites, out.read_in_patch ? ", in a pack's memory patch" : "", out.motor,
             out.idle_reset);
    } else {
        logf("GameCube adapter: the Wii Remotes' screen-dimming reset at 0x%08x (0: none found; with the Wii's burn-in "
             "reduction on, play on GameCube controllers alone may dim the screen)\n",
             out.idle_reset);
    }
    // Settings > Leaving a game, HOME only: the combo leaves nothing, so
    // its hooks are not put in at all (HOME opens the game's HOME Menu).
    if (Settings().leave_buttons == "off") {
        logf("GameCube adapter: no way back to RiftWii by the controller: Settings > Leaving a game is HOME only\n");
        // GameCube controllers as Classic Controllers use the HOME Menu's
        // KPAD hooks; its stand-in stays off.
        if (standin) {
            find_home_menu(dol, text, patched, out);
            out.home_combo_off = true;
        }
    } else {
        find_way_back(dol, text, out);
        find_home_menu(dol, text, patched, out);
    }
    // The stand-in also starts from the game's sleeps (a game that reads
    // nothing until its connect callback hears of a controller: Donkey
    // Kong Country Returns), so OSSleepThread is hooked for it even where
    // the way back isn't (HOME only, or its other pieces not found).
    if (standin && out.sleep_thread == 0) {
        out.sleep_thread = find_os_sleep_thread(text);
        if (out.sleep_thread != 0)
            logf("GameCube adapter: OSSleepThread at 0x%08x, for the GameCube controllers' stand-in (the "
                 "game's sleeps start them)\n",
                 out.sleep_thread);
        else
            logf("GameCube adapter: OSSleepThread not found: a game that reads nothing until its connect callback "
                 "hears of a controller won't start the GameCube controllers' stand-in\n");
    }
    // SIProbe is told by PADRead's own calls, so with a pack's PADRead it
    // is searched in the game's code as the patches will leave it, and in
    // the pack's writes outside it too (a wrapper it adds beside its
    // PADRead: CodeRabbit CLI, fork #25).
    std::vector<CodeRange> text_and_written = text;
    text_and_written.insert(text_and_written.end(), written.begin(), written.end());
    out.si_probe = !written.empty()      ? find_gc_si_probe_with_patches(text_and_written, written, out.read)
                   : from_patch            ? find_gc_si_probe_with_patches(text, patched, out.read)
                                           : find_gc_si_probe(text, out.read);
    // Where SIProbe is missing the adapter still plays: only a game that
    // asks it before reading a port misses the adapter's ports (#510).
    if (out.si_probe != 0)
        logf("GameCube adapter: SIProbe at 0x%08x: a port the adapter plays on answers as a GameCube controller\n",
             out.si_probe);
    else
        logf("GameCube adapter: SIProbe not found: a game that asks it which ports have a controller sees none there\n");

    if (!usb) {
        // A pack that brings its own controller code turned the stand-in off:
        // with no adapter there's nothing left for the hook to do, and the
        // builds without the stand-in would ask USB on every PADRead (only
        // the stand-in's honour RT_PAD_FLAG_NO_USB; CodeRabbit CLI, fork #25).
        if (!out.standin) {
            why = "no adapter, and a pack brings its own controller code (the stand-in is off), so there's nothing "
                  "for the hook to do";
            return false;
        }
        // No adapter: the hook is there only for the stand-in, which needs
        // the game's KPAD read and WPADProbe (plan_pad_hook's
        // wants_home_build). Without them it would do nothing, so no hook,
        // and no pads plugin to fail the launch over (CodeRabbit on #616).
        if (out.kpad_read == 0 || out.wpad_probe == 0) {
            why = "no adapter, and the game's KPAD read or WPADProbe that the GameCube controllers' stand-in needs "
                  "wasn't found";
            return false;
        }
        // The Wii's own ports play as Classic Controllers, and
        // USB is never asked anything (on some d2x a USB check broke the
        // game's disc reads, settle_gc_adapter).
        logf("GameCube adapter: none in use; the hook goes in for the GameCube controllers' stand-in (the "
             "Wii's own GameCube ports)\n");
        out.fd = -1;
        out.version = 5;
        out.no_usb = true;
        out.ven_fd = -1;
        return true;
    }
    // A game with no PADRead is hooked only for the stand-in, which needs
    // the KPAD read and WPADProbe: without them there is nothing to hook,
    // so the launch goes on without the pad code, as it did before the
    // stand-in, instead of stopping over the pads plugin (CodeRabbit on
    // #609).
    if (out.read == 0 && (out.kpad_read == 0 || out.wpad_probe == 0)) {
        why = "no PADRead, and the game's KPAD read or WPADProbe that the GameCube controllers' stand-in needs "
              "wasn't found";
        return false;
    }
    // The pad code's plugin first: a launch it can't serve stops before the
    // adapter's handle is taken (pads_plugin_usable).
    if (!pads_plugin_usable()) {
        why = "the pads plugin can't be used (its own line above says why); the launch stops before the adapter's "
              "handle is taken";
        return false;
    }
    if (!take_adapter_handle(out.fd, out.version, out.known_dev, why, &out.devices_unknown)) {
        if (!demo) return false;
        logf("GameCube adapter: %s; demo mode goes on without it\n", why.c_str());
        out.fd = -1;
        out.version = 5;
    }
    out.ven_fd = out.fd >= 0 && out.version == 5 ? open_usb_ven_for_game() : -1;
    return true;
}

bool take_adapter_handle(std::int32_t& fd, std::uint32_t& version, std::int32_t& known_dev, std::string& why,
                         bool* list_unknown) {
    HidPadsPreload();
    // The adapter's device, with the IOS the game will run. The menu's
    // USB storage left libogc's device-change requests pending: v5 takes
    // one at a time (Dolphin refuses a second), and their answers would
    // reach the game. libogc cancels its own; the Shutdown on ours clears
    // one it re-armed while going.
    fd = -1;
    version = 0;
    known_dev = -1;
    g_handed_fd = -1;
    if (list_unknown != nullptr) *list_unknown = false;
    if (g_own.fd >= 0 && g_own.version == 5) {
        // The launch's own handle (look_for_gc_adapter), never shut down.
        fd = g_own.fd;
        version = 5;
        known_dev = g_own.device;
        g_own.hand_over();  // the game's now
        g_handed_fd = fd;
        logf("GameCube adapter: %s v5 fd %d (opened before libogc's USB), adapter device %d\n", g_usb_path_used,
             static_cast<int>(fd), static_cast<int>(known_dev));
        return true;
    }
    std::string devices;
    const AdapterSeen seen = ogc_adapter(known_dev, devices);  // v5's list goes with libogc's USB
    if (list_unknown != nullptr) *list_unknown = seen == AdapterSeen::Unknown;
    logf("GameCube adapter: libogc lists %s (adapter device %d)\n", devices.c_str(), static_cast<int>(known_dev));
    USB_Deinitialize();
    usleep(50000);
    // No usb_open_matrix() when this fails: its probes broke the disc's
    // emulation on d2x (wii/launch/boot.cpp), and the game still reads its disc.
    if (!open_usb_hid(fd, version, why)) {
        fd = -1;
        return false;
    }
    // No v5 Shutdown on the game's handle: after one, a Wii U's IOS
    // answered -4 to everything on it (libogc's USB closed before this).
    logf("GameCube adapter: %s v%u fd %d, adapter device %d\n", g_usb_path_used, version, static_cast<int>(fd),
         static_cast<int>(known_dev));
    return true;
}

std::int32_t open_usb_ven_for_game() {
    // Pads plugged in when the game starts, as for the HID pads: no
    // handle, and nothing more in the game, without one.
    const std::uint32_t xbox = HidPadsXboxPlugged();
    if (xbox == 0) return -1;
    if (std::strcmp(g_usb_path_used, "/dev/usb/ven") == 0) {
        logf("USB controllers: no Xbox 360 pads in this game: /dev/usb/ven already serves the GameCube adapter.\n");
        return -1;
    }
    if (!usb_buffers() || g_hid_stuck) return -1;
    s32 fd = 0;
    g_async_done = false;
    if (!await_async(IOS_OpenAsync(g_ven_path, 0, on_async, nullptr), fd) || fd < 0) {
        logf("USB controllers: /dev/usb/ven could not be opened (%d); no Xbox 360 pads in this game.\n",
             static_cast<int>(fd));
        return -1;
    }
    std::memset(g_version_out, 0, kVersionBytes);
    DCFlushRange(g_version_out, kVersionBytes);
    s32 v5 = 0;
    g_async_done = false;
    if (!await_async(IOS_IoctlAsync(fd, GCAD_V5_GET_VERSION, nullptr, 0, g_version_out, kVersionBytes, on_async,
                                    nullptr),
                     v5)) {
        logf("USB controllers: /dev/usb/ven did not answer its GetVersion; no Xbox 360 pads in this game.\n");
        return -1;  // left open: closing it could wait too
    }
    DCInvalidateRange(g_version_out, kVersionBytes);
    if (v5 != 0 || g_version_out[0] != GCAD_V5_VERSION) {
        logf("USB controllers: /dev/usb/ven is not v5 (%d, %08x); no Xbox 360 pads in this game.\n",
             static_cast<int>(v5), static_cast<unsigned>(g_version_out[0]));
        IOS_Close(fd);
        return -1;
    }
    logf("USB controllers: /dev/usb/ven v5 fd %d for Xbox 360 pads (%04x:%04x plugged in).\n", static_cast<int>(fd),
         static_cast<unsigned>(xbox >> 16), static_cast<unsigned>(xbox & 65535u));
    return fd;
}

namespace {

// A plan that fails after find_pad_functions gives both USB handles back:
// the launch goes on without the hook, and nothing else would close them.
void close_pad_handles(PadHook& out) {
    if (out.fd >= 0) IOS_Close(out.fd);
    if (out.ven_fd >= 0) IOS_Close(out.ven_fd);
    out.fd = -1;
    out.ven_fd = -1;
}

// A launch the pads plugin refused after the adapter's handle was taken:
// a handle that was the menu's session (take_adapter_handle) goes back to
// it, still open, for the failure screen (players with only GameCube
// controllers read it with them); any other handle is closed.
void give_back_or_close(PadHook& out) {
    if (out.fd >= 0 && out.fd == g_handed_fd) {
        g_own.take_back(out.fd, out.version, out.known_dev);
        logf("GameCube adapter: the launch stopped; fd %d goes back to the menu's adapter session\n",
             static_cast<int>(out.fd));
        out.fd = -1;
    }
    g_handed_fd = -1;
    close_pad_handles(out);
}

}  // namespace

namespace {

// Which USB controllers the game's state makes room for, and so which
// build of the pad code it takes: from the menu's catalog of USB pads and
// the handles find_pad_functions opened. fetch_pad_build takes the
// catalog (`snapshot`), and plan_pad_hook decides again from that same
// copy, so the two always pick the same build. The kept catalog
// (g_pad_kept) is what plan_pad_hook copies into the game.
rthid_catalog g_pad_catalog;
rthid_catalog g_pad_kept;
void decide_pad_inputs(PadHook& out, bool code_build, bool snapshot) {
    rthid_catalog& catalog = g_pad_catalog;
    if (snapshot) HidPadsCopyCatalog(catalog);
    // A code build (its blob goes to MEM2's top, through veneers) takes the
    // USB controllers plugged in when it starts and no others: room for
    // those, their mappings, and the blob without what none of them needs
    // (#496). Code builds use all of the game's memory; other games keep
    // room for any pad the menu has seen, which can be plugged in mid-game.
    // A device list that could not be read at the launch is no sign the
    // adapter is out: planned for then, as it may be plugged in.
    bool adapter_in = catalog.adapter_disabled == 0 && (out.known_dev >= 0 || out.devices_unknown);
    for (unsigned i = 0; i < catalog.known_count && i < RTHID_DEVICE_MAX; ++i) {
        if (catalog.adapter_disabled == 0 && catalog.known[i].vid_pid == GCAD_VID_PID) adapter_in = true;
    }
    rthid_catalog& kept = g_pad_kept;
    std::memcpy(&kept, &catalog, offsetof(rthid_catalog, layouts));
    kept.count = 0;
    kept.layout_bytes = 0;
    unsigned hid_pads = 0, xbox_pads = 0;
    bool special_pad = false;  // a Switch Pro Controller or DualShock 3: their start-ups are in the full blob
    for (unsigned i = 0; i < catalog.known_count && i < RTHID_DEVICE_MAX; ++i) {
        const std::uint32_t id = catalog.known[i].vid_pid;
        const rthid_layout* m = id == GCAD_VID_PID ? nullptr : rthid_find(&catalog, id);
        if (m == nullptr) continue;
        ++hid_pads;
        if (id == GCAD_SWITCH_VID_PID || id == GCAD_PS3_VID_PID) special_pad = true;
        bool have = false;
        have = rthid_find(&kept, id) != nullptr;
        if (!have) rthid_catalog_add(&kept, m);
    }
    if (out.ven_fd >= 0) {
        for (unsigned i = 0; i < catalog.ven_known_count && i < RTHID_DEVICE_MAX; ++i) {
            if (gcad_xbox_vendor(catalog.ven_known[i].vid_pid >> 16)) ++xbox_pads;
        }
    }
    if (code_build) {
        // Only what is plugged in now; an adapter plugged in later is not looked for.
        out.adapter_only = adapter_in && hid_pads == 0 && out.ven_fd < 0;
        out.pads_only = !adapter_in && !special_pad;
        out.hid_dormant = !adapter_in && hid_pads == 0 && out.ven_fd >= 0;
        const unsigned links = hid_pads + xbox_pads;
        out.link_count = links == 0 ? 1u : links < GCAD_HID_PADS ? links : GCAD_HID_PADS;
        out.map_count = kept.count;
        out.map_bytes = kept.layout_bytes;
        if (!adapter_in) kept.adapter_disabled = 1;
    } else {
        // As before (#494): the adapter alone when no other USB pad could be
        // used; otherwise room for four and every mapping the menu loaded.
        out.adapter_only = catalog.count == 0 && out.ven_fd < 0 && catalog.adapter_disabled == 0;
        out.link_count = GCAD_HID_PADS;
        out.map_count = catalog.count;
        out.map_bytes = catalog.layout_bytes;
        std::memcpy(&kept, &catalog, sizeof(kept));
    }
    // With no USB (the Wii's own ports only) the driver is never asked
    // anything: the smallest layout, without room for other USB pads.
    if (out.no_usb) out.adapter_only = true;
    if (out.adapter_only) {
        out.link_count = 0;
        out.map_count = 0;
        out.map_bytes = 0;
        out.pads_only = false;
        out.hid_dormant = false;
    }
    if (out.adapter_only && out.known_dev < 0) {
        // The hub would find the adapter in the menu's device list
        // (gcad_links_init); alone, the driver is told it directly.
        for (unsigned i = 0; i < catalog.known_count && i < RTHID_DEVICE_MAX; ++i) {
            if (catalog.known[i].vid_pid == GCAD_VID_PID) out.known_dev = catalog.known[i].id;
        }
    }
}

// The HOME Menu by the controller (#387) only in ordinary Wii games,
// and only where its functions were found: a code build needs every
// byte, and with Settings > Leaving a game on HOME only none were
// looked for. Else the build without it, as before it.
// The build with the HOME Menu's hooks: for the HOME Menu (all four of its
// functions) or for GameCube controllers as Classic Controllers (the KPAD
// read and WPADProbe, which are among them).
bool wants_home_build(const PadHook& out, bool code_build) {
    return !code_build && out.kpad_read != 0 && out.wpad_probe != 0 &&
           ((out.hbm_calc != 0 && out.hbm_select != 0) || out.standin);
}

}  // namespace

PadsPluginForLaunch::PadsPluginForLaunch(bool wanted) {
    // Every launch starts with no refusal: one that never asks for the pad
    // code (fetch_pad_build) can't carry an earlier launch's over to the
    // channel fallback (CodeRabbit CLI, #609).
    g_pads_refused = false;
    g_pads_refusal.clear();
    if (!wanted) return;
    // A refusal is logged by the loader; fetch_pad_build asks again and
    // stops the launch with the loader's sentence if the game needs it.
    if (plugin::Use(kPadsPlugin) != nullptr)
        logf("GameCube adapter and USB controllers: the %s plugin loaded while the card is RiftWii's, for the pad code "
             "later in the launch\n",
             kPadsPlugin);
}

// Only a launch that failed and came back gets here (a started game never
// returns): the copy goes with the plugin.
PadsPluginForLaunch::~PadsPluginForLaunch() {
    plugin::ShutdownAll("the launch no longer needs it");
    forget_build();
}

bool pads_plugin_stopped_launch() { return g_pads_refused; }

void pads_plugin_forget_refusal() {
    g_pads_refused = false;
    g_pads_refusal.clear();
}

std::string pads_plugin_refusal() { return g_pads_refusal; }

void release_pads_plugin() {
    plugin::ShutdownAll("this game takes no pad code");
    forget_build();
}

bool fetch_pad_build(PadHook& hook, bool code_build, std::string& message) {
    forget_build();
    g_pads_refused = false;
    decide_pad_inputs(hook, code_build, true);
    const int kind =
        kind_of(hook.adapter_only, hook.pads_only, wants_home_build(hook, code_build), hook.standin, hook.standin_nunchuk,
                hook.standin_sideways, hook.standin_pointing);
    const std::uint32_t bytes = kBuildBytes[kind];
    g_build = static_cast<std::uint8_t*>(memalign(32, (bytes + 31u) & ~31u));
    if (g_build == nullptr) {
        logf("GameCube adapter and USB controllers: no memory for the pad code's copy (%u bytes)\n",
             static_cast<unsigned>(bytes));
        message = tr("There was not enough memory for the {1} add-on. Restart RiftWii and try again.", {kPadsPlugin});
        give_back_or_close(hook);
        g_pads_refused = true;
        return false;
    }
    rwp_pads_request request{};
    request.size = sizeof(request);
    request.build = static_cast<std::uint32_t>(kind);
    request.out = g_build;
    request.capacity = bytes;
    // Only what this launch's loader says reaches the player: an earlier
    // value of the caller's string never does (CodeRabbit on #616).
    message.clear();
    const std::int32_t result = plugin::Launch(kPadsPlugin, &request, sizeof(request), message);
    // The copy needs none of the plugin's code: its arena goes back to the
    // heap before anything else of the launch is placed.
    plugin::ShutdownAll("the pad code is copied");
    const auto fail = [&](const std::string& why, bool damaged) {
        logf("GameCube adapter and USB controllers: the game can't start without the pad code from RiftWii's %s "
             "plugin in %s: %s\n",
             kPadsPlugin, plugin::Directory().c_str(), why.c_str());
        if (damaged) message = tr("A part of RiftWii is damaged (the {1} add-on). Update RiftWii again.", {kPadsPlugin});
        give_back_or_close(hook);
        forget_build();
        g_pads_refused = true;
        return false;
    };
    if (result == -1) {
        // plugin::Launch gives its own sentence for the player with -1 (a
        // missing plugin: "A part of RiftWii is missing ..."): kept, with a
        // general one only where it gave none (console, 24 Nov: the general
        // one had replaced "missing").
        if (message.empty())
            message = tr("A part of RiftWii couldn't be used (the {1} add-on). Update RiftWii again.", {kPadsPlugin});
        return fail("the plugin could not be used (its own line above says why)", false);
    }
    if (result != RWP_PADS_COPIED)
        return fail("build " + std::to_string(kind) + " was not copied: " +
                        (request.error[0] != '\0' ? std::string(request.error) : std::string("no reason given")),
                    true);
    g_build_kind = kind;
    if (request.written != bytes || !blob_whole(kind))
        return fail("build " + std::to_string(kind) + " is not the one this RiftWii was built with (" +
                        std::to_string(request.written) + " bytes, " + std::to_string(bytes) + " expected)",
                    true);
    logf("GameCube adapter and USB controllers: the pad code's %s (%u bytes) copied from the %s plugin\n", build_name(kind),
         static_cast<unsigned>(bytes), kPadsPlugin);
    return true;
}

namespace {

}  // namespace

bool plan_pad_hook(const DolHeader& dol, std::uint32_t arena1_hi, std::uint32_t mem1_floor, std::uint32_t arena2_lo,
                   std::uint32_t ioctl_async, std::uint32_t ioctlv_async,
                   const std::vector<MemoryPatch>& patches, PadHook& out, std::string& why,
                   std::uint32_t mem1_veneer, std::uint32_t stage_base) {
    const bool demo = out.demo;
    // Without PADRead only GameCube controllers as Classic Controllers can
    // go on (checked again once their KPAD hooks are known, below).
    if (out.read == 0 && !out.standin) {
        why = "PADRead was not found";
        return false;
    }

    // 1. The game's IOS calls.
    std::vector<CodeRange> text;
    for (std::size_t i = 0; i < kDolTextSections; ++i) {
        const DolSection& s = dol.sections[i];
        if (s.used()) text.push_back({s.address, reinterpret_cast<const std::uint8_t*>(s.address), s.size});
    }
    std::string error;
    if (ioctl_async == 0) {
        IpcSymbols ipc;
        // With no USB (the Wii's own ports only) the blob makes no IOS
        // calls of its own, so the game's aren't needed: a failed search
        // leaves them at zero.
        const bool found = find_ipc_symbols(text, ipc, error);
        if (!found && !out.no_usb) {
            why = "the game's IOS_IoctlAsync was not found: " + error;
            close_pad_handles(out);
            return false;
        }
        if (found) {
            ioctl_async = ipc.ioctl_async;
            ioctlv_async = ipc.ioctlv_async;
            if (ioctlv_async == 0) {
                IpcApi api;
                if (find_ipc_api(text, ipc, api, error)) ioctlv_async = api.async[kIpcIoctlvCmd];
            }
        }
    }

    // 2. /dev/usb/hid, opened by find_pad_functions.
    if (out.version == 5 && ioctlv_async == 0 && !out.no_usb) {
        why = "/dev/usb/hid v5 needs the game's IOS_IoctlvAsync, which was not found";
        close_pad_handles(out);
        return false;
    }

    // 3. Memory: the blob below the MEM1 arena's top, the state at the
    //    bottom of the MEM2 arena. For a code build both at the top of
    //    the MEM2 arena instead, the arena's start (and the build's fixed
    //    addresses) left alone. A pack's patch there turns it off.
    // Only the adapter's own state when no other USB pad could be used in
    // the game: a pad's mapping is loaded in the menu when it is plugged
    // in there, and an Xbox 360 pad needs /dev/usb/ven opened before the
    // launch (open_usb_ven_for_game). Without either, the four pad links,
    // the catalog and the ven list are about 26 KB the game never uses
    // (Project+ with the adapter had 512 bytes to spare).
    const std::uint32_t version = out.version == 4 ? 4u : 5u;
    const bool code_build = mem1_veneer != 0;
    decide_pad_inputs(out, code_build, false);  // the catalog fetch_pad_build took
    const rthid_catalog& kept = g_pad_kept;
    gcad_layout layout{};
    gcad_layout_for(&layout, out.hid_dormant ? GCAD_LINK_BYTES : GCAD_OWNER_BYTES(version), out.link_count,
                    out.ven_fd >= 0, out.map_bytes);
    // The HOME Menu by the controller (#387) only in ordinary Wii games,
    // and only where its functions were found: a code build needs every
    // byte, and with Settings > Leaving a game on HOME only none were
    // looked for. Else the blob without it, as before it.
    out.home_blob = wants_home_build(out, code_build);
    // With no adapter the hook is there only for the stand-in, which needs
    // the HOME Menu's hooks; a build without them would ask USB on every
    // PADRead anyway (only the stand-in builds leave it alone).
    if (out.no_usb && !out.home_blob) {
        why = "GameCube controllers as Wii controllers need the game's KPAD read, WPADProbe and HOME Menu, which "
              "weren't found";
        close_pad_handles(out);
        return false;
    }
    if (out.read == 0 && !out.home_blob) {
        why = "the game has no PADRead, and the KPAD read, WPADProbe or HOME Menu that GameCube controllers as "
              "Classic Controllers would use instead wasn't found";
        close_pad_handles(out);
        return false;
    }
    if (!out.home_blob) {
        if (code_build && out.kpad_read != 0)
            logf("GameCube adapter: no HOME Menu by the controller in a code build (the combo is the way back)\n");
        out.kpad_read = out.wpad_probe = out.hbm_calc = out.hbm_select = 0;
        out.kpad_unified = out.kpad_connect = 0;
    }
    const int kind = kind_of(out);
    if (kind != g_build_kind) {
        why = "the pad code copied at the launch is another build (" + std::to_string(g_build_kind) + ", this game takes " +
              std::to_string(kind) + ")";
        close_pad_handles(out);
        return false;
    }
    const rt_pad_header& h = header(kind);
    out.code_bytes = align_up(h.size);
    out.state_bytes = align_up(out.adapter_only ? GCAD_OWNER_BYTES(version) : layout.bytes);
    // The game's own Reset (#458): its block follows the driver's state, in
    // MEM2 (a code build, whose blob is in MEM2 out of OSRestart's reach,
    // never has one).
    // GameCube controllers as Classic Controllers keep their state after
    // all of it (below); only with the HOME Menu's blob, whose KPAD read and
    // WPADProbe hooks it takes.
    const bool gcwii = out.standin && out.home_blob;
    const std::uint32_t gcwii_bytes = gcwii ? align_up(sizeof(rtgcwii)) : 0;
    std::uint32_t restart_block_at = 0;
    if (out.restart_os != 0 && mem1_veneer == 0 && out.restart_armed.size() == RTRESTART_SECTOR &&
        out.restart_idle.size() == RTRESTART_SECTOR && h.hook_restart != 0) {
        const std::uint32_t block_bytes = align_up(sizeof(rtrestart_block));
        const std::uint32_t calibration_bytes = out.calibration != nullptr ? align_up(sizeof(rtpadcal_table)) : 0;
        const std::uint32_t mem2_start = align_up(arena2_lo);
        const std::uint32_t mem2_end = read32(kMem2ArenaEndField);
        // The block is the Reset's alone: where it is the bytes that would
        // not let the controllers' own state fit (the stand-in's included),
        // the controllers win and the game's Reset stays its own.
        if (mem2_end >= mem2_start &&
            mem2_end - mem2_start >= out.state_bytes + block_bytes + calibration_bytes + gcwii_bytes) {
            restart_block_at = out.state_bytes;
            out.restart_block_bytes = block_bytes;
            out.state_bytes += out.restart_block_bytes;
        } else {
            logf("Reset: the game's own Reset restarts it without the adapter or USB controllers: no room in MEM2 for "
                 "its request block (the controllers still work)\n");
            out.restart_os = 0;
        }
    } else {
        out.restart_os = 0;
    }
    // The controllers' calibrations after the driver's state, which the
    // driver never zeroes past its own end; only when one is stored, as a
    // code build may have no room to spare.
    const std::uint32_t calibration_at = out.calibration != nullptr ? out.state_bytes : 0;
    if (out.calibration != nullptr) out.state_bytes += align_up(sizeof(rtpadcal_table));
    // GameCube controllers as Classic Controllers, after them.
    const std::uint32_t gcwii_at = gcwii ? out.state_bytes : 0;
    out.state_bytes += gcwii_bytes;
    if (gcwii)
        logf("GameCube adapter: GameCube controllers as Wii controllers (the game's setting): %u bytes of MEM2 "
             "for their state\n",
             static_cast<unsigned>(align_up(sizeof(rtgcwii))));
    // The reason, as it is: a code build never takes the stand-in's build
    // (it needs every byte, wants_home_build), whatever was found.
    if (out.standin && !out.home_blob)
        logf(code_build ? "GameCube adapter: GameCube controllers don't stand in for Wii controllers here: a code "
                          "build has no room for the stand-in (the setting applies to ordinary launches)\n"
                        : "GameCube adapter: GameCube controllers don't stand in for Wii controllers here: the game's "
                          "KPAD read or WPADProbe wasn't found\n");
    // The build by its own name, as the fetch logs it (CodeRabbit on #616).
    logf("USB controllers: the %s's driver (%u bytes), room for %u other USB pad(s) and %u layout(s) (%u bytes)%s, state %u bytes%s\n",
         build_name(kind),
         static_cast<unsigned>(h.size), static_cast<unsigned>(out.link_count), static_cast<unsigned>(out.map_count),
         static_cast<unsigned>(out.map_bytes),
         out.ven_fd >= 0 ? " and the Xbox 360 list" : "", static_cast<unsigned>(out.state_bytes),
         code_build ? " (a code build: what was plugged in at the start)" : "");
    // A code build puts this blob at MEM2's top. The resident runtime's
    // staged data is still there until the jump, so keep below it too.
    const std::uint32_t arena2_end = stage_base != 0 && mem1_veneer != 0
                                       ? std::min(read32(kMem2ArenaEndField), stage_base)
                                       : read32(kMem2ArenaEndField);
    if (mem1_veneer != 0) {
        const std::uint32_t bytes = out.code_bytes + out.state_bytes;
        const std::uint32_t base = (arena2_end - bytes) & ~31u;
        // Below this loader's own MEM2 the blob would be overwritten before
        // the jump; below the arena's start it would be the game's heap.
        const std::uint32_t loader_top = reinterpret_cast<std::uint32_t>(SYS_GetArena2Hi());
        const bool fits = arena2_end >= bytes && base >= loader_top && base >= align_up(arena2_lo);
        if (!fits) {
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                          "no room in the MEM2 arena (0x%08x-0x%08x, this loader's memory ends at 0x%08x)", arena2_lo,
                          arena2_end, loader_top);
            why = buf;
            close_pad_handles(out);
            return false;
        }
        out.code_base = base;
        out.state_base = base + out.code_bytes;
        out.veneer = mem1_veneer;
        out.new_arena2_hi = base;
    } else {
        out.code_base = (arena1_hi - out.code_bytes) & ~31u;
        out.state_base = align_up(arena2_lo);
        const bool fits = arena1_hi >= out.code_bytes && out.code_base >= mem1_floor &&
                          out.state_base + out.state_bytes <= arena2_end;
        if (!fits) {
            char buf[160];
            std::snprintf(buf, sizeof(buf), "no room (MEM1 arena top 0x%08x, floor 0x%08x; MEM2 arena 0x%08x-0x%08x)",
                          arena1_hi, mem1_floor, arena2_lo, arena2_end);
            why = buf;
            close_pad_handles(out);
            return false;
        }
    }
    for (const MemoryPatch& p : patches) {
        if (overlaps(p, out.code_base, out.code_bytes) || overlaps(p, out.state_base, out.state_bytes)) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "a pack's memory patch at 0x%08x uses the memory it needs",
                          static_cast<unsigned>(p.offset));
            why = buf;
            close_pad_handles(out);
            return false;
        }
    }
    // In MEM2 the MEM1 arena and the MEM2 arena's start stay as they were.
    if (out.restart_os != 0) out.restart_block = out.state_base + restart_block_at;
    out.new_arena1_hi = mem1_veneer != 0 ? arena1_hi : out.code_base;
    out.new_arena2_lo = mem1_veneer != 0 ? arena2_lo : out.state_base + out.state_bytes;

    // 4. The blob and its context; the hooks come after the patches.
    std::memset(reinterpret_cast<void*>(out.code_base), 0, out.code_bytes);
    std::memcpy(reinterpret_cast<void*>(out.code_base), blob(kind), h.size);
    rt_pad_context* ctx = reinterpret_cast<rt_pad_context*>(out.code_base + h.context_offset);
    if (ctx->magic != RT_PAD_CONTEXT_MAGIC) {
        why = "the pad blob's context is not where its header says";
        close_pad_handles(out);
        return false;
    }
    if (!out.adapter_only) {
        auto* game_catalog = reinterpret_cast<rthid_catalog*>(out.state_base + layout.catalog);
        std::memcpy(game_catalog, &kept, offsetof(rthid_catalog, layouts) + kept.layout_bytes);
        for (unsigned i = 0; i < kept.count; ++i) {
            const rthid_layout* mapping = rthid_layout_at(&kept, i);
            if (mapping == nullptr) break;
            logf("USB controller %04x:%04x: layout sent to the Wii game's pad hook (%u report bytes, %u bytes).\n",
                 static_cast<unsigned>(mapping->vid), static_cast<unsigned>(mapping->pid),
                 static_cast<unsigned>(mapping->report_bytes), static_cast<unsigned>(mapping->size));
        }
    }
    out.calibration_base = 0;
    if (out.calibration != nullptr) {
        out.calibration_base = out.state_base + calibration_at;
        std::memcpy(reinterpret_cast<void*>(out.calibration_base), out.calibration, sizeof(rtpadcal_table));
        logf("GameCube controllers: calibrations at 0x%08x (%u bytes)\n", out.calibration_base,
             static_cast<unsigned>(sizeof(rtpadcal_table)));
    }
    out.gcwii_base = 0;
    if (gcwii) {
        out.gcwii_base = out.state_base + gcwii_at;
        auto* state = reinterpret_cast<rtgcwii*>(out.gcwii_base);
        std::memset(state, 0, sizeof(rtgcwii));
        state->slot.grace = kGcwiiGraceMs * (kBusClock / 4000u);
        state->shape.pointer_show = kGcwiiPointerShowMs * (kBusClock / 4000u);
        state->shape.shake_flip = kGcwiiShakeFlipMs * (kBusClock / 4000u);
        state->kpad_dpd = out.kpad_dpd.base;
        state->kpad_dpd_stride = out.kpad_dpd.stride;
        state->kpad_dpd_offset = out.kpad_dpd.offset;
        state->wpad_connect_table = out.wpad_connect_table;
        state->wpad_connect_offset = out.wpad_connect_offset;
        state->wpad_connect_later = out.wpad_connect_later ? 1u : 0u;
        state->slot.inputless = out.standin_inputless ? 1u : 0u;
        state->wpad_ext_table = out.wpad_ext_table;
        state->wpad_ext_offset = out.wpad_ext_offset;
        state->info_answers = out.info_answers;
    }
    DCFlushRange(reinterpret_cast<void*>(out.state_base), out.state_bytes);
    ctx->state = out.state_base;
    ctx->cal = out.calibration_base;
    ctx->ioctl_async = ioctl_async;
    ctx->ioctlv_async = ioctlv_async;
    ctx->complete_entry = out.code_base + h.complete;
    ctx->fd = out.fd;
    ctx->version = out.version;
    ctx->ticks_per_ms = kBusClock / 4000u;
    ctx->inited = 0;
    ctx->flags = (demo ? RT_PAD_FLAG_DEMO : 0u) | (out.adapter_only ? RT_PAD_FLAG_ADAPTER_ONLY : 0u) |
                 (out.hid_dormant ? RT_PAD_FLAG_HID_DORMANT : 0u) | (out.no_usb ? RT_PAD_FLAG_NO_USB : 0u);
    ctx->known_dev = out.version == 5 ? out.known_dev : -1;
    ctx->ven_fd = out.ven_fd;
    ctx->link_count = out.link_count;
    ctx->ven_at = layout.ven;
    ctx->xbox_at = layout.xbox;
    ctx->links_at = layout.links;
    ctx->catalog_at = layout.catalog;

    ctx->idle_reset = out.idle_reset;
    if (out.restart_os != 0) {
        // Everything but the words that turn it on (enable_pad_reset).
        auto* block = reinterpret_cast<rtrestart_block*>(out.restart_block);
        std::memset(block, 0, sizeof(*block));
        std::memcpy(block->armed, out.restart_armed.data(), RTRESTART_SECTOR);
        std::memcpy(block->idle, out.restart_idle.data(), RTRESTART_SECTOR);
        block->state.lba = out.restart_lba;
        block->state.sdio_fd = static_cast<std::uint32_t>(out.restart_sdio_fd);
        block->state.sdio_mode = out.restart_sdio_mode;
        block->state.ioctlv = out.restart_ioctlv;
        DCFlushRange(block, sizeof(*block));
        ctx->restart = out.restart_block;
    }
    out.active = true;
    logf("GameCube adapter: blob %u bytes at 0x%08x, state %u bytes at 0x%08x%s%s%s\n", h.size, out.code_base,
         out.state_bytes, out.state_base, out.adapter_only ? " (the adapter alone: no other USB pad)" : "",
         demo ? " (demo)" : "",
         mem1_veneer != 0 ? "; in MEM2, through veneers (a code build)" : "");
    return true;
}

void log_pad_diagnostics(std::uint32_t code_base, std::uint32_t print_entry) {
    // The header of the blob as installed (either build).
    const auto* installed = reinterpret_cast<const rt_pad_header*>(code_base);
    auto* ctx = reinterpret_cast<rt_pad_context*>(code_base + installed->context_offset);
    std::strcpy(ctx->diagnostic_format, "RiftWii USB controller diagnostic");
    ctx->diagnostic_print = print_entry;
    DCFlushRange(ctx, RT_PAD_CONTEXT_BASE_BYTES);  // its words are in either blob's context
}

bool install_pad_hook(PadHook& hook, std::string& why) {
    if (!hook.active) return false;
    // A PADRead from the packs' memory patches was chosen from a preview
    // that doesn't run search patches: checked again now they're all in,
    // before anything is hooked.
    if (hook.read_in_patch && !pad_read_in_place(hook.read)) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "the pack's PADRead at 0x%08x isn't there once its memory patches are in",
                      hook.read);
        why = buf;
        hook.active = false;
        close_pad_handles(hook);  // the plan's USB handles back (CodeRabbit on #609)
        return false;
    }
    const int kind = kind_of(hook);
    const rt_pad_header& h = header(kind);
    // The HOME Menu's hooks, read only from the blob that has them.
    static const rt_pad_header none{};
    const rt_pad_header& hh = hook.home_blob ? h : none;
    // The stand-in builds' own header, after rt_pad_header (the others have none).
    const rt_pad_standin_header sh =
        standin_kind(kind) ? *reinterpret_cast<const rt_pad_standin_header*>(blob(kind) + sizeof(rt_pad_header))
                           : rt_pad_standin_header{};
    struct Site {
        std::uint32_t function, hook, replay, resume;
        const char* name;
    } sites[kPadHookSites + 4] = {{hook.read, h.hook_read, h.replay_read, h.continue_read, "PADRead"},
                              {hook.motor, h.hook_motor, h.replay_motor, h.continue_motor, "PADControlMotor"},
                              {hook.ipc_submit, h.hook_submit, h.replay_submit, h.continue_submit, "the IPC submit routine"},
                              {hook.sleep_thread, h.hook_sleep, h.replay_sleep, h.continue_sleep, "OSSleepThread"},
                              {hook.suspend_thread, h.hook_suspend, h.replay_suspend, h.continue_suspend,
                               "OSSuspendThread"},
                              {hook.restart_os, h.hook_restart, h.replay_restart, h.continue_restart,
                               "OSRestart"},
                              {hook.kpad_read, hh.hook_kpad, hh.replay_kpad, hh.continue_kpad, "the KPAD read"},
                              {hook.wpad_probe, hh.hook_probe, hh.replay_probe, hh.continue_probe, "WPADProbe"},
                              {hook.hbm_calc, hh.hook_menu, hh.replay_menu, hh.continue_menu, "HBMCalc"},
                              {hook.hbm_select, hh.hook_select, hh.replay_select, hh.continue_select,
                               "HBMGetSelectBtnNum"},
                              {hook.kpad_unified, hh.hook_unified, hh.replay_unified, hh.continue_unified,
                               "KPADGetUnifiedWpadStatus"},
                              {hook.si_probe, h.hook_si_probe, h.replay_si_probe, h.continue_si_probe, "SIProbe"},
                              // A stand-in build is never in MEM2 (the HOME Menu's builds
                              // aren't), so this hook never needs a twelfth veneer.
                              {standin_kind(kind) && hook.veneer == 0 ? hook.wpad_info : 0u, sh.hook_info,
                               sh.replay_info, sh.continue_info, "WPADGetInfoAsync"},
                              // The stand-in below KPAD: only the Wii Remote forms' builds have them.
                              {standin_kind(kind) && hook.veneer == 0 && sh.hook_wread != 0 ? hook.wpad_read : 0u,
                               sh.hook_wread, sh.replay_wread, sh.continue_wread, "WPADRead"},
                              {standin_kind(kind) && hook.veneer == 0 && sh.hook_format != 0 ? hook.wpad_set_format : 0u,
                               sh.hook_format, sh.replay_format, sh.continue_format, "WPADSetDataFormat"}};
    Site& read_site = sites[0];
    Site& motor_site = sites[1];
    Site& submit_site = sites[2];
    Site& sleep_site = sites[3];
    Site& suspend_site = sites[4];
    Site& restart_site = sites[5];
    Site* const home_sites[4] = {&sites[6], &sites[7], &sites[8], &sites[9]};
    // Optional for the HOME Menu (a game without it reads the KPAD status
    // only): missing or not hooked, the rest works as before.
    Site& unified_site = sites[10];
    // In either blob (#510).
    Site& probe_site = sites[11];
    // The stand-in builds', for a 2006 KPAD game.
    Site& info_site = sites[12];
    // The stand-in below KPAD's, in the Wii Remote forms.
    Site& wread_site = sites[13];
    Site& format_site = sites[14];
    const auto is_wpad_site = [&](const Site& s) { return &s == &wread_site || &s == &format_site; };
    const auto is_home_site = [&](const Site& s) {
        for (const Site* home_site : home_sites)
            if (&s == home_site) return true;
        return false;
    };
    // The HOME Menu needs all four of its hooks: without one, none goes in
    // and the combo stays the way back (find_home_menu says why
    // HBMGetSelectBtnNum is one of them).
    bool home = hook.kpad_read != 0 && hook.wpad_probe != 0 && hook.hbm_calc != 0 && hook.hbm_select != 0;
    // GameCube controllers as Classic Controllers: the KPAD read's and
    // WPADProbe's hooks alone, with or without the HOME Menu's.
    bool standin_reads = hook.standin && hook.kpad_read != 0 && hook.wpad_probe != 0;
    const auto reads_site = [&](const Site& s) { return &s == home_sites[0] || &s == home_sites[1]; };
    const auto home_off = [&](const char* name, const std::string& reason) {
        if (!home) return;
        logf("GameCube adapter: no HOME Menu by the controller (the combo is the way back): %s %s\n", name,
             reason.c_str());
        home = false;
    };
    // The way back needs both of its hooks: without either it stays off
    // (the one that went in is harmless alone).
    const auto way_back_off = [&](const char* name, const std::string& reason) {
        if (hook.return_to_menu == 0) return;
        logf("GameCube adapter: no way back to RiftWii by the controller: %s %s\n", name, reason.c_str());
        hook.return_to_menu = 0;
    };
    if (hook.return_to_menu == 0) {
        submit_site.function = 0;
        // Kept for the stand-in, which its sleeps start (pad_on_sleep);
        // the way back's part of it does nothing without the way back.
        if (!hook.standin) sleep_site.function = 0;
        suspend_site.function = 0;
    }
    // Checked now, after the packs' patches: an instruction a pack
    // replaced with a branch of its own cannot be moved.
    for (Site& s : sites) {
        if (s.function == 0) continue;
        const std::uint32_t first = *reinterpret_cast<const std::uint32_t*>(s.function);
        std::string reason;
        if (!displaceable(first, kScratchRegister, reason)) {
            if (&s == &read_site) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "PADRead's first instruction (0x%08x)", first);
                why = std::string(buf) + " cannot be moved: " + reason + "; a pack or code has hooked it";
                hook.active = false;
                close_pad_handles(hook);  // the plan's USB handles back (CodeRabbit on #609)
                return false;
            }
            if (&s == &motor_site) {
                logf("GameCube adapter: PADControlMotor cannot be hooked (%s); no rumble\n", reason.c_str());
            } else if (is_home_site(s)) {
                home_off(s.name, "cannot be hooked (" + reason + ")");
                if (reads_site(s)) standin_reads = false;
            } else if (&s == &unified_site) {
                logf("GameCube adapter: KPADGetUnifiedWpadStatus cannot be hooked (%s); the stand-in's raw status "
                     "isn't given\n",
                     reason.c_str());
            } else if (&s == &suspend_site) {
                logf("GameCube adapter: OSSuspendThread cannot be hooked (%s); the way back leaves only from sleeps\n",
                     reason.c_str());
            } else if (&s == &restart_site) {
                logf("Reset: the game's own Reset restarts it without the adapter or USB controllers: OSRestart "
                     "cannot be hooked (%s)\n", reason.c_str());
            } else if (&s == &probe_site) {
                logf("GameCube adapter: SIProbe cannot be hooked (%s); a game that asks it sees no controller there\n",
                     reason.c_str());
            } else if (&s == &info_site) {
                logf("GameCube adapter: WPADGetInfoAsync cannot be hooked (%s); the 2006 KPAD game may not take the "
                     "stand-in\n",
                     reason.c_str());
            } else if (is_wpad_site(s)) {
                logf("GameCube adapter: %s cannot be hooked (%s); a game that reads WPAD itself may not take the "
                     "stand-in\n",
                     s.name, reason.c_str());
            } else {
                way_back_off(s.name, "cannot be hooked (" + reason + ")");
            }
            s.function = 0;
            continue;
        }
        const std::uint32_t replay[4] = {first, kNop, kNop, kNop};
        store_words(hook.code_base + s.replay, replay, 4);
        const auto resume = encode_absolute_jump(kScratchRegister, s.function + 4);
        store_words(hook.code_base + s.resume, resume.data(), 4);
    }
    if (!home && !standin_reads) {
        for (Site* s : home_sites) s->function = 0;
        unified_site.function = 0;
    } else if (!home) {
        // The stand-in's reads without the HOME Menu: its own two stay out
        // (each would return at once anyway, home_on being 0).
        home_sites[2]->function = 0;
        home_sites[3]->function = 0;
    }
    // A game with no PADRead (GameCube controllers as Classic Controllers):
    // the KPAD read's and WPADProbe's hooks are its only way in (the HOME
    // Menu's own two are optional, as for any game), so one of those that
    // can't go in turns the adapter off here, before anything is hooked.
    // Their reach is checked now too (the HOME Menu's build is never in
    // MEM2, so there are no veneers to go through).
    if (hook.read == 0) {
        bool reachable = standin_reads;
        for (Site* s : home_sites) {
            std::uint32_t branch = 0;
            if (reachable && reads_site(*s) && s->function != 0 &&
                !encode_branch(s->function, hook.code_base + s->hook, branch)) {
                why = std::string(s->name) + " is out of a branch's reach";
                reachable = false;
            }
        }
        if (!reachable) {
            if (why.empty())
                why = "the KPAD read's and WPADProbe's hooks, the only way in for a game with no PADRead, can't go in";
            hook.active = false;
            close_pad_handles(hook);  // the plan's USB handles back (CodeRabbit on #609)
            return false;
        }
    }
    // WPADRead's hook goes in only with WPADSetDataFormat's (CodeRabbit on
    // #650): with WPADRead's in, a game that reads WPAD itself is told of the
    // stand-in again until it reads it, and only WPADSetDataFormat's hook
    // answers the format the game then asks for on the stand-in's channel.
    // So where WPADSetDataFormat's can't go in, WPADRead's stays out too,
    // before either is hooked; the KPAD read's hooks don't depend on them,
    // and WPADSetDataFormat's alone still answers a KPAD game's format.
    if (wread_site.function != 0) {
        std::uint32_t branch = 0;
        const bool format_in = format_site.function != 0 &&
                               encode_branch(format_site.function, hook.code_base + format_site.hook, branch);
        if (!format_in) {
            logf("GameCube adapter: WPADRead isn't hooked, as WPADSetDataFormat %s; a game that reads WPAD itself may "
                 "not take the stand-in\n",
                 format_site.function == 0 ? "isn't" : "is out of a branch's reach");
            wread_site.function = 0;
        }
    }
    sync_code(hook.code_base, hook.code_bytes);
    unsigned veneers = 0;
    bool unified = false, wpad_read_in = false;
    for (const Site& s : sites) {
        if (s.function == 0) continue;
        std::uint32_t target = hook.code_base + s.hook;
        if (hook.veneer != 0) {
            // The blob is in MEM2, out of a branch's reach: the function
            // branches to an absolute jump in low MEM1, which goes there.
            const std::uint32_t at = hook.veneer + veneers * 16;
            ++veneers;
            const auto jump = encode_absolute_jump(kScratchRegister, target);
            store_words(at, jump.data(), 4);
            sync_code(at, 16);
            target = at;
        }
        std::uint32_t branch = 0;
        if (!encode_branch(s.function, target, branch)) {
            why = std::string(s.name) + " is out of a branch's reach";
            if (&s == &read_site) {
                hook.active = false;
                close_pad_handles(hook);  // the plan's USB handles back (CodeRabbit on #609)
                return false;
            }
            if (&s == &submit_site || &s == &sleep_site) way_back_off(s.name, "is out of a branch's reach");
            // Another of the four may already be in: harmless alone, as each
            // returns at once while home_on is 0.
            if (is_home_site(s)) home_off(s.name, "is out of a branch's reach");
            if (reads_site(s)) standin_reads = false;
            if (&s == &unified_site)
                logf("GameCube adapter: KPADGetUnifiedWpadStatus is out of a branch's reach; the stand-in's raw "
                     "status isn't given\n");
            if (&s == &suspend_site)
                logf("GameCube adapter: OSSuspendThread is out of a branch's reach; the way back leaves only from sleeps\n");
            if (&s == &restart_site)
                logf("Reset: the game's own Reset restarts it without the adapter or USB controllers: OSRestart is "
                     "out of a branch's reach\n");
            if (&s == &probe_site)
                logf("GameCube adapter: SIProbe is out of a branch's reach; a game that asks it sees no controller there\n");
            if (&s == &info_site)
                logf("GameCube adapter: WPADGetInfoAsync is out of a branch's reach; the 2006 KPAD game may not take "
                     "the stand-in\n");
            if (is_wpad_site(s))
                logf("GameCube adapter: %s is out of a branch's reach; a game that reads WPAD itself may not take the "
                     "stand-in\n",
                     s.name);
            continue;
        }
        store_words(s.function, &branch, 1);
        sync_code(s.function & ~31u, 32);
        if (&s == &unified_site) unified = true;
        if (&s == &wread_site) wpad_read_in = true;
    }
    // The loop above stored a branch on OSRestart's first word when it hooked it.
    if (hook.restart_os != 0 && restart_site.function != 0) {
        const std::uint32_t word = *reinterpret_cast<const std::uint32_t*>(hook.restart_os);
        std::uint32_t offset = word & 0x03FFFFFCu;
        if (offset & 0x02000000u) offset |= 0xFC000000u;
        hook.restart_hooked = (word >> 26) == 18 && hook.restart_os + offset == hook.code_base + h.hook_restart;
    }
    // The way back's words in the context, last: return_to_menu, which
    // turns it on, only once both its hooks are in.
    rt_pad_context* ctx = reinterpret_cast<rt_pad_context*>(hook.code_base + h.context_offset);
    ctx->leave.hold = kLeaveHoldMs * ctx->ticks_per_ms;
    ctx->leave.quiet = kSaveQuietMs * ctx->ticks_per_ms;
    ctx->resident_last_write = hook.resident_last_write;
    ctx->resident_busy = hook.resident_busy;
    ctx->resident_queued = hook.resident_queued;
    ctx->return_to_menu = hook.return_to_menu;
    // The HOME Menu's words, home_on (which turns it on) last; the blob
    // without it has no room for them.
    if (!hook.home_blob) {
        DCFlushRange(ctx, context_bytes(kind));
        logf("GameCube adapter: hooked%s\n", motor_site.function == 0 ? " (PADRead only)" : "");
        if (hook.return_to_menu != 0)
            logf("GameCube adapter: the way back is on: the combo held %u ms (%u time base ticks), %u ms after a save\n",
                 static_cast<unsigned>(kLeaveHoldMs), static_cast<unsigned>(ctx->leave.hold),
                 static_cast<unsigned>(kSaveQuietMs));
        return true;
    }
    ctx->kpad_status_bytes = hook.kpad_status_bytes;
    ctx->kpad_takes_error = hook.kpad_takes_error ? 1u : 0u;
    ctx->pad_read = hook.read;
    ctx->kpad_unified_bytes = unified ? hook.kpad_unified_bytes : 0u;
    ctx->kpad_connect = home || standin_reads ? hook.kpad_connect : 0u;
    ctx->kpad_connect_info = hook.kpad_connect_stride & 0xFFFFu;
    ctx->home.hold = kLeaveHoldMs * ctx->ticks_per_ms;
    ctx->home.press = kHomePressMs * ctx->ticks_per_ms;
    ctx->home.announce = kHomeAnnounceMs * ctx->ticks_per_ms;
    ctx->home.wait = kHomeWaitMs * ctx->ticks_per_ms;
    ctx->home.linger = kHomeLingerMs * ctx->ticks_per_ms;
    ctx->home.channel = RTHOME_NO_CHANNEL;
    // The permanent stand-in needs the KPAD read's and WPADProbe's hooks,
    // in with the HOME Menu's or, without its functions, alone.
    ctx->gcwii = home || standin_reads ? hook.gcwii_base : 0u;
    if (ctx->gcwii != 0 && wpad_read_in) {
        // A game that reads WPAD itself is told of the stand-in again until
        // it reads it (rtgcwii_retell); every other game is told once.
        auto* state = reinterpret_cast<rtgcwii*>(ctx->gcwii);
        state->slot.retell_ticks = kGcwiiRetellMs * ctx->ticks_per_ms;
        DCFlushRange(state, sizeof(*state));
    }
    ctx->home_on = home && !hook.home_combo_off ? 1u : 0u;
    DCFlushRange(ctx, context_bytes(kind));
    logf("GameCube adapter: hooked%s\n", motor_site.function == 0 ? " (PADRead only)" : "");
    if (ctx->gcwii != 0) {
        logf("GameCube adapter: GameCube controllers are %s in this game (port 1 plays as Wii Remote 1 and so on; "
             "state at 0x%08x)%s\n",
             hook.standin_nunchuk    ? "Wii Remotes + Nunchuks"
             : hook.standin_sideways ? "sideways Wii Remotes"
             : hook.standin_pointing ? "pointing Wii Remotes"
                                     : "Classic Controllers",
             ctx->gcwii,
             hook.kpad_connect != 0 ? "; the game's connect callback hears of each" : "");
    } else if (hook.standin && hook.gcwii_base != 0) {
        logf("GameCube adapter: GameCube controllers don't stand in for Wii controllers here: the KPAD read's or WPADProbe's "
             "hook didn't go in\n");
    }
    // What the combo does as the context has it (ctx->home_on), not which
    // hooks went in: the KPAD read and WPADProbe also go in for Classic
    // Controllers alone, with the HOME Menu off (CodeRabbit on #609).
    if (ctx->home_on) {
        logf("GameCube adapter: the HOME Menu by the controller is on: the combo held %u ms opens the game's own "
             "HOME Menu (a Classic Controller on a free channel, HOME held %u ms)%s\n",
             static_cast<unsigned>(kLeaveHoldMs), static_cast<unsigned>(kHomePressMs),
             hook.return_to_menu != 0 ? "; in a game that never reads it, the combo is the way back" : "");
    } else if (hook.return_to_menu != 0) {
        logf("GameCube adapter: the way back is on: the combo held %u ms (%u time base ticks), %u ms after a save\n",
             static_cast<unsigned>(kLeaveHoldMs), static_cast<unsigned>(ctx->leave.hold),
             static_cast<unsigned>(kSaveQuietMs));
    }
    return true;
}

bool enable_pad_reset(PadHook& hook, std::uint32_t os_return_to_menu) {
    if (!hook.active || !hook.restart_hooked || hook.restart_block == 0 || os_return_to_menu == 0) return false;
    auto* block = reinterpret_cast<rtrestart_block*>(hook.restart_block);
    block->state.return_to_menu = os_return_to_menu;
    DCFlushRange(block, sizeof(block->state));
    return true;
}

}  // namespace riftwii::wii
