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

namespace riftwii::wii {
namespace {

constexpr std::uint32_t kMem2ArenaEndField = 0x80003128;
constexpr std::uint32_t kBusClockField = 0x800000F8;  // the time base runs at a quarter of it
constexpr unsigned kScratchRegister = 12;             // pad_entry.S's jump back
constexpr std::uint32_t kNop = 0x60000000;
// v5 serves 16 handles, picked by the open mode: libogc's USB takes 0 and
// fakemote (a cIOS module) 15, so this one is ours. v4 ignores it.
constexpr int kHidHandle = 1;

char g_hid_path[] ATTRIBUTE_ALIGN(32) = "/dev/usb/hid";
char g_ven_path[] ATTRIBUTE_ALIGN(32) = "/dev/usb/ven";
std::uint32_t g_version_out[8] ATTRIBUTE_ALIGN(32);
std::uint32_t align_up(std::uint32_t v) { return (v + 31) & ~31u; }

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

// v5 comes with /dev/usb/ven, v4 without it (libogc tells them apart
// the same way). Each is asked only its own GetVersion: v4's number is
// AttachFinish on v5, and v5's is GetDeviceChange on v4, which can wait.
// v4's answers 0x40001 itself, v5's writes 0x50001 into its output.
bool open_usb_hid(std::int32_t& fd, std::uint32_t& version, std::string& why) {
    const s32 ven = IOS_Open(g_ven_path, IPC_OPEN_NONE);
    if (ven >= 0) IOS_Close(ven);
    fd = IOS_Open(g_hid_path, kHidHandle);
    if (fd < 0) {
        why = "/dev/usb/hid did not open (" + std::to_string(fd) + "): this IOS has no USB HID";
        return false;
    }
    char buf[160];
    if (ven >= 0) {
        std::memset(g_version_out, 0, sizeof(g_version_out));
        const s32 ret = IOS_Ioctl(fd, GCAD_V5_GET_VERSION, nullptr, 0, g_version_out, sizeof(g_version_out));
        if (ret == 0 && g_version_out[0] == GCAD_V5_VERSION) {
            version = 5;
            return true;
        }
        std::snprintf(buf, sizeof(buf), "/dev/usb/hid (with /dev/usb/ven) is not v5: GetVersion %d, %08x",
                      static_cast<int>(ret), static_cast<unsigned>(g_version_out[0]));
    } else {
        const s32 ret = IOS_Ioctl(fd, GCAD_V4_GET_VERSION, nullptr, 0, nullptr, 0);
        if (ret == static_cast<s32>(GCAD_V4_VERSION)) {
            version = 4;
            return true;
        }
        std::snprintf(buf, sizeof(buf), "/dev/usb/hid (no /dev/usb/ven) is not v4: GetVersion %d (ven %d)",
                      static_cast<int>(ret), static_cast<int>(ven));
    }
    IOS_Close(fd);
    fd = -1;
    why = buf;
    return false;
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

bool usb_hid_present() {
    const s32 fd = IOS_Open(g_hid_path, kHidHandle);
    if (fd < 0) return false;
    IOS_Close(fd);
    return true;
}

AdapterSeen look_for_gc_adapter(std::string& how) {
    std::int32_t fd = -1;
    std::uint32_t version = 0;
    if (!open_usb_hid(fd, version, how)) return AdapterSeen::Missing;
    IOS_Close(fd);
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
          " (/dev/usb/hid v" + std::to_string(version) + ", USB lists " + devices + extra + ")";
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

    // 2. The adapter's device, with the IOS the game will run. The menu's
    // USB storage left libogc's device-change requests pending: v5 takes
    // one at a time (Dolphin refuses a second), and their answers would
    // reach the game. libogc cancels its own; the Shutdown on ours clears
    // one it re-armed while going.
    std::int32_t known_dev = -1;
    std::string devices;
    ogc_adapter(known_dev, devices);  // v5's list goes with libogc's USB
    USB_Deinitialize();
    usleep(50000);
    if (!open_usb_hid(out.fd, out.version, why)) {
        if (!demo) return false;
        logf("GameCube adapter: %s; demo mode goes on without it\n", why.c_str());
        out.fd = -1;
        out.version = 5;
    } else if (out.version == 5) {
        IOS_Ioctl(out.fd, GCAD_V5_SHUTDOWN, nullptr, 0, nullptr, 0);  // an error when nothing was pending
    }
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
    ctx->known_dev = out.version == 5 ? known_dev : -1;
    out.active = true;
    logf("GameCube adapter: /dev/usb/hid v%u fd %d, adapter device %d; blob %u bytes at 0x%08x, state %u bytes at "
         "0x%08x%s\n",
         out.version, out.fd, static_cast<int>(ctx->known_dev), h.size, out.code_base, out.state_bytes, out.state_base,
         demo ? " (demo)" : "");
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
