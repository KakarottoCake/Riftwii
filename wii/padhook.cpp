// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2017 Alex Chadwick (wup-028-bslug) <https://github.com/Chadderz121/wup-028-bslug>
// SPDX-FileCopyrightText: FIX94 and the Nintendont contributors, used with permission <https://github.com/FIX94/Nintendont>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "padhook.hpp"

#include <gccore.h>
#include <ogc/cache.h>
#include <ogc/ipc.h>
#include <ogc/machine/processor.h>
#include <ogc/usb.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#include "ios_reload.hpp"
#include "log.hpp"
#include "pad_hook.h"
#include "riftwii/hook.hpp"
#include "riftwii/symsearch.hpp"
#include "riftwii_pad_bin.h"
#include "rtgcad.h"
#include "skin.hpp"

namespace riftwii::wii {
namespace {

constexpr std::uint32_t kMem2ArenaEndField = 0x80003128;
constexpr std::uint32_t kBusClockField = 0x800000F8;  // the time base runs at a quarter of it
constexpr unsigned kScratchRegister = 12;             // pad_entry.S's jump back
constexpr std::uint32_t kNop = 0x60000000;
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
std::int32_t g_own_fd = -1;
std::uint32_t g_own_version = 0;
std::int32_t g_own_dev = -1;  // the adapter's v5 device id, -1: not listed

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

const rt_pad_header& header() { return *reinterpret_cast<const rt_pad_header*>(riftwii_pad_bin); }

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
        std::memset(g_version_out, 0, kVersionBytes);
        DCFlushRange(g_version_out, kVersionBytes);
        s32 v5 = 0;
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
    s32 ret = USB_GetDeviceList(list, 32, USB_CLASS_HID, &count);
    if (ret < 0 && USB_Initialize() >= 0) {
        // libogc's USB was not started (it is on the menu's paths):
        // started now, its list comes in its first device-change reply.
        for (int i = 0; i < 30; ++i) {
            ret = USB_GetDeviceList(list, 32, USB_CLASS_HID, &count);
            if (ret < 0 || count > 0) break;
            usleep(10000);
        }
    }
    if (ret < 0) {
        devices = "no list: error " + std::to_string(ret);
        return AdapterSeen::Unknown;
    }
    bool found = false;
    for (unsigned i = 0; i < count && i < 32; ++i) {
        char one[16];
        std::snprintf(one, sizeof(one), "%s%04x:%04x", i ? ", " : "", list[i].vid, list[i].pid);
        devices += one;
        if ((static_cast<std::uint32_t>(list[i].vid) << 16 | list[i].pid) == GCAD_VID_PID && !found) {
            found = true;
            if (list[i].device_id != 0) dev_id = list[i].device_id;
        }
    }
    if (devices.empty()) devices = "no devices";
    return found ? AdapterSeen::Found : AdapterSeen::Missing;
}

void forget_usb_hid_stuck() { g_hid_stuck = false; }

bool usb_hid_present() {
    if (g_own_fd >= 0) return true;
    std::int32_t fd = -1;
    std::uint32_t version = 0;
    std::string why;
    if (!open_usb_hid(fd, version, why)) return false;
    IOS_Close(fd);
    return true;
}

void close_adapter_session() {
    if (g_own_fd >= 0) IOS_Close(g_own_fd);  // no Shutdown (see g_own_fd)
    g_own_fd = -1;
    g_own_version = 0;
    g_own_dev = -1;
}

// v5 through our own handle: the list from a fresh handle, once more
// after the devices of a fresh IOS have had 2.5 s to show up.
AdapterSeen own_adapter_list(std::string& how) {
    std::string why;
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (g_own_fd < 0 && !open_usb_hid(g_own_fd, g_own_version, why)) {
            how = "unknown, taken as plugged in (" + why + ")";
            logf("GameCube adapter: our own USB handle failed: %s\n", why.c_str());
            g_own_fd = -1;
            return AdapterSeen::Unknown;
        }
        if (g_own_version != 5) return AdapterSeen::Unknown;  // v4: libogc's oh0 list (the caller)
        s32 count = 0;
        if (!own_list(g_own_fd, count)) {
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
        g_own_dev = gcad_find_v5(list, static_cast<std::uint32_t>(count));
        // As libogc does after each device change.
        s32 finish = 0;
        g_async_done = false;
        await_async(IOS_IoctlAsync(g_own_fd, GCAD_V5_ATTACH_FINISH, nullptr, 0, nullptr, 0, on_async, nullptr), finish);
        if (g_own_dev >= 0 || attempt == 1 || ms_since_ios_reload() >= 2500) {
            how = std::string(g_own_dev >= 0 ? "plugged in" : "not plugged in") + " (our own " + g_usb_path_used + " v5 lists " +
                  (devices.empty() ? std::string("no devices") : devices) + ")";
            return g_own_dev >= 0 ? AdapterSeen::Found : AdapterSeen::Missing;
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
        if (g_own_version == 5) return seen;
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
    how = std::string(seen == AdapterSeen::Found     ? "plugged in"
                      : seen == AdapterSeen::Missing ? "not plugged in"
                                                     : "unknown, taken as plugged in") +
          " (USB lists " + devices + extra + ")";
    return seen;
}

bool find_pad_functions(const DolHeader& dol, bool demo, PadHook& out, std::string& why) {
    out = PadHook{};
    out.demo = demo;
    const rt_pad_header& h = header();
    if (riftwii_pad_bin_size < sizeof(rt_pad_header) || h.magic != RT_PAD_MAGIC || h.version != RT_PAD_VERSION ||
        h.size > riftwii_pad_bin_size || h.context_offset + RT_PAD_CONTEXT_BYTES > h.size) {
        why = "the embedded pad blob is damaged";
        return false;
    }
    std::vector<CodeRange> text;
    for (std::size_t i = 0; i < kDolTextSections; ++i) {
        const DolSection& s = dol.sections[i];
        if (s.used()) text.push_back({s.address, reinterpret_cast<const std::uint8_t*>(s.address), s.size});
    }
    PadSymbols pad;
    std::string error;
    if (!find_pad_symbols(text, pad, error)) {
        why = "the PAD search is unsure: " + error;
        return false;
    }
    if (pad.read == 0) {
        why = "this game has no GameCube controller support (no PADRead)";
        return false;
    }
    out.read = pad.read;
    out.read_sites = pad.read_sites;
    out.motor = pad.control_motor;
    logf("GameCube adapter: PADRead at 0x%08x (%u error stores), PADControlMotor at 0x%08x\n", out.read,
         out.read_sites, out.motor);

    // The adapter's device, with the IOS the game will run. The menu's
    // USB storage left libogc's device-change requests pending: v5 takes
    // one at a time (Dolphin refuses a second), and their answers would
    // reach the game. libogc cancels its own; the Shutdown on ours clears
    // one it re-armed while going.
    if (g_own_fd >= 0 && g_own_version == 5) {
        // The launch's own handle (look_for_gc_adapter), never shut down.
        out.fd = g_own_fd;
        out.version = 5;
        out.known_dev = g_own_dev;
        g_own_fd = -1;  // the game's now
        logf("GameCube adapter: %s v5 fd %d (opened before libogc's USB), adapter device %d\n", g_usb_path_used,
             static_cast<int>(out.fd), static_cast<int>(out.known_dev));
        return true;
    }
    std::string devices;
    ogc_adapter(out.known_dev, devices);  // v5's list goes with libogc's USB
    logf("GameCube adapter: libogc lists %s (adapter device %d)\n", devices.c_str(), static_cast<int>(out.known_dev));
    USB_Deinitialize();
    usleep(50000);
    if (!open_usb_hid(out.fd, out.version, why)) {
        logf("GameCube adapter diag: %s\n", usb_open_matrix().c_str());
        if (!demo) return false;
        logf("GameCube adapter: %s; demo mode goes on without it\n", why.c_str());
        out.fd = -1;
        out.version = 5;
    }
    // No v5 Shutdown on the game's handle: after one, a Wii U's IOS
    // answered -4 to everything on it (libogc's USB closed before this).
    logf("GameCube adapter: %s v%u fd %d, adapter device %d\n", g_usb_path_used, out.version, static_cast<int>(out.fd),
         static_cast<int>(out.known_dev));
    return true;
}

bool plan_pad_hook(const DolHeader& dol, std::uint32_t arena1_hi, std::uint32_t mem1_floor, std::uint32_t arena2_lo,
                   std::uint32_t ioctl_async, std::uint32_t ioctlv_async,
                   const std::vector<MemoryPatch>& patches, PadHook& out, std::string& why) {
    const rt_pad_header& h = header();
    const bool demo = out.demo;
    if (out.read == 0) {
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
        if (!find_ipc_symbols(text, ipc, error)) {
            why = "the game's IOS_IoctlAsync was not found: " + error;
            return false;
        }
        ioctl_async = ipc.ioctl_async;
        ioctlv_async = ipc.ioctlv_async;
        if (ioctlv_async == 0) {
            IpcApi api;
            if (find_ipc_api(text, ipc, api, error)) ioctlv_async = api.async[kIpcIoctlvCmd];
        }
    }

    // 2. /dev/usb/hid, opened by find_pad_functions.
    if (out.version == 5 && ioctlv_async == 0) {
        why = "/dev/usb/hid v5 needs the game's IOS_IoctlvAsync, which was not found";
        if (out.fd >= 0) IOS_Close(out.fd);
        return false;
    }

    // 3. Memory: the blob below the MEM1 arena's top, the state at the
    //    bottom of the MEM2 arena. A pack's patch there turns it off.
    out.code_bytes = align_up(h.size);
    out.code_base = (arena1_hi - out.code_bytes) & ~31u;
    out.state_bytes = align_up(sizeof(gcad));
    out.state_base = align_up(arena2_lo);
    const std::uint32_t arena2_end = read32(kMem2ArenaEndField);
    if (arena1_hi < out.code_bytes || out.code_base < mem1_floor || out.state_base + out.state_bytes > arena2_end) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "no room (MEM1 arena top 0x%08x, floor 0x%08x; MEM2 arena 0x%08x-0x%08x)",
                      arena1_hi, mem1_floor, arena2_lo, arena2_end);
        why = buf;
        if (out.fd >= 0) IOS_Close(out.fd);
        return false;
    }
    for (const MemoryPatch& p : patches) {
        if (overlaps(p, out.code_base, out.code_bytes) || overlaps(p, out.state_base, out.state_bytes)) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "a pack's memory patch at 0x%08x uses the memory it needs",
                          static_cast<unsigned>(p.offset));
            why = buf;
            if (out.fd >= 0) IOS_Close(out.fd);
            return false;
        }
    }
    out.new_arena1_hi = out.code_base;
    out.new_arena2_lo = out.state_base + out.state_bytes;

    // 4. The blob and its context; the hooks come after the patches.
    std::memset(reinterpret_cast<void*>(out.code_base), 0, out.code_bytes);
    std::memcpy(reinterpret_cast<void*>(out.code_base), riftwii_pad_bin, h.size);
    rt_pad_context* ctx = reinterpret_cast<rt_pad_context*>(out.code_base + h.context_offset);
    if (ctx->magic != RT_PAD_CONTEXT_MAGIC) {
        why = "the pad blob's context is not where its header says";
        if (out.fd >= 0) IOS_Close(out.fd);
        return false;
    }
    ctx->state = out.state_base;
    ctx->ioctl_async = ioctl_async;
    ctx->ioctlv_async = ioctlv_async;
    ctx->complete_entry = out.code_base + h.complete;
    ctx->fd = out.fd;
    ctx->version = out.version;
    ctx->ticks_per_ms = read32(kBusClockField) / 4000u;
    ctx->inited = 0;
    ctx->flags = demo ? RT_PAD_FLAG_DEMO : 0u;
    ctx->known_dev = out.version == 5 ? out.known_dev : -1;
    out.active = true;
    logf("GameCube adapter: blob %u bytes at 0x%08x, state %u bytes at 0x%08x%s\n", h.size, out.code_base,
         out.state_bytes, out.state_base, demo ? " (demo)" : "");
    return true;
}

bool install_pad_hook(PadHook& hook, std::string& why) {
    if (!hook.active) return false;
    const rt_pad_header& h = header();
    struct Site {
        std::uint32_t function, hook, replay, resume;
        const char* name;
    } sites[2] = {{hook.read, h.hook_read, h.replay_read, h.continue_read, "PADRead"},
                  {hook.motor, h.hook_motor, h.replay_motor, h.continue_motor, "PADControlMotor"}};
    // Checked now, after the packs' patches: an instruction a pack
    // replaced with a branch of its own cannot be moved.
    for (Site& s : sites) {
        if (s.function == 0) continue;
        const std::uint32_t first = *reinterpret_cast<const std::uint32_t*>(s.function);
        std::string reason;
        if (!displaceable(first, kScratchRegister, reason)) {
            if (&s == &sites[0]) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "PADRead's first instruction (0x%08x)", first);
                why = std::string(buf) + " cannot be moved: " + reason + "; a pack or code has hooked it";
                hook.active = false;
                return false;
            }
            logf("GameCube adapter: PADControlMotor cannot be hooked (%s); no rumble\n", reason.c_str());
            s.function = 0;
            continue;
        }
        const std::uint32_t replay[4] = {first, kNop, kNop, kNop};
        store_words(hook.code_base + s.replay, replay, 4);
        const auto resume = encode_absolute_jump(kScratchRegister, s.function + 4);
        store_words(hook.code_base + s.resume, resume.data(), 4);
    }
    sync_code(hook.code_base, hook.code_bytes);
    for (const Site& s : sites) {
        if (s.function == 0) continue;
        std::uint32_t branch = 0;
        if (!encode_branch(s.function, hook.code_base + s.hook, branch)) {
            why = std::string(s.name) + " is out of a branch's reach";
            if (&s == &sites[0]) {
                hook.active = false;
                return false;
            }
            continue;
        }
        store_words(s.function, &branch, 1);
        sync_code(s.function & ~31u, 32);
    }
    logf("GameCube adapter: hooked%s\n", sites[1].function == 0 ? " (PADRead only)" : "");
    return true;
}

}  // namespace riftwii::wii
