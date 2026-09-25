// SPDX-License-Identifier: GPL-3.0-or-later
#include "gcadapter.hpp"

#include <gccore.h>
#include <ogc/cache.h>
#include <ogc/ipc.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/machine/processor.h>
#include <ogc/usb.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>

#include "boot.hpp"
#include "loadersettings.hpp"
#include "log.hpp"
#include "padhook.hpp"
#include "skin.hpp"

namespace riftwii::wii {
namespace {

gcad* g_state = nullptr;   // in MEM2, taken once (skin::Mem2Alloc)
std::int32_t g_fd = -1;    // RiftWii's own /dev/usb/hid, -1 on libogc's
bool g_running = false;

// The driver's log events, queued in the IPC interrupt and written out
// by GcAdapterPoll (the log writes to the card).
struct Event {
    std::uint32_t code, a;
    std::int32_t b;
};
constexpr unsigned kEvents = 32;
Event g_events[kEvents];
volatile unsigned g_event_head = 0, g_event_tail = 0;
volatile unsigned g_events_lost = 0;
// The menu keeps the adapter running: a failure that repeats every
// second is logged this many times, then no more.
constexpr unsigned kErrorLines = 12;
unsigned g_error_lines = 0;

// The menu's USB storage started libogc's USB, which keeps v5's one
// device-change request pending, so ours is refused. The driver's lists
// then come from libogc's (a change it sees answers the waiting one) and
// its AttachFinish and the cancel are answered here.
bool g_shared = false;
// On libogc's handle (g_ogc) every device request goes through libogc as
// well: the ones below that libogc makes synchronously wait in g_ops
// for ServeShared, and the interrupt transfers are libogc's.
bool g_ogc = false;
volatile bool g_list_wanted = false;   // the driver's device list waits
volatile bool g_ogc_changed = false;   // libogc has a list not yet given
bool g_notify_armed = false;
std::uint32_t g_list_tag = 0;
void* g_list_out = nullptr;
int g_refused = 0;                     // what the refused request answered, logged once
struct Reply {
    std::uint32_t tag;
    std::int32_t result;
};
Reply g_replies[4];
unsigned g_reply_count = 0;
struct Op {
    std::uint32_t tag, cmd;
    std::int32_t dev;
};
Op g_ops[4];
unsigned g_op_count = 0;

// The menu's use (GcAdapterMenuPads).
bool g_menu_started = false;   // the menu started the driver (not the test page)
bool g_menu_ended = false;
bool g_menu_allowed = false;  // GcAdapterMenuAllowStart
bool g_menu_failed = false;    // it would not start: not tried again
bool g_menu_said_off = false;
u64 g_menu_checked = 0;

std::uint32_t Now() { return static_cast<std::uint32_t>(gettime()); }

bool ListStep(std::uint32_t tag) {
    return (tag & 15u) == GCAD_TAG_CHANGE && g_state->change_step == GCAD_STEP_CHANGE;
}

s32 OnReply(s32 result, void* tag) {
    if (g_state == nullptr) return 0;
    const std::uint32_t t = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(tag));
    if (result < 0 && !g_shared && g_state->version == 5 && !g_state->stopping && ListStep(t)) {
        g_shared = true;
        g_refused = result;
        g_list_wanted = true;
        g_ogc_changed = true;  // libogc's current list answers it
        return 0;
    }
    gcad_on_reply(g_state, t, result, Now());
    return 0;
}

s32 OnOgcChange(s32 result, void* data) {
    (void)result;
    (void)data;
    g_notify_armed = false;
    g_ogc_changed = true;
    return 0;
}

// Both run with interrupts off (the driver's calls into the environment
// come from its IPC replies or from under _CPU_ISR_Disable).
void Answer(std::uint32_t tag, std::int32_t result) {
    if (g_reply_count < sizeof(g_replies) / sizeof(g_replies[0])) g_replies[g_reply_count++] = Reply{tag, result};
}

bool Defer(std::uint32_t tag, std::uint32_t cmd, std::int32_t dev) {
    if (g_op_count >= sizeof(g_ops) / sizeof(g_ops[0])) return false;
    g_ops[g_op_count++] = Op{tag, cmd, dev};
    return true;
}

// Gives the shared mode's answers to the driver, and makes libogc's
// synchronous calls. Runs with interrupts on: libogc's calls use IPC.
void ServeShared() {
    if (!g_shared) return;
    if (g_refused != 0) {
        logf("GameCube adapter: the device list is libogc's (ours was refused, %d)\n", g_refused);
        g_refused = 0;
    }
    u32 level;
    Op ops[sizeof(g_ops) / sizeof(g_ops[0])];
    _CPU_ISR_Disable(level);
    const unsigned n_ops = g_op_count;
    std::memcpy(ops, g_ops, sizeof(ops));
    g_op_count = 0;
    _CPU_ISR_Restore(level);
    for (unsigned i = 0; i < n_ops; ++i) {
        s32 result = 0;
        if (ops[i].cmd == GCAD_V5_SUSPEND_RESUME) {
            // Resumes it and reads its descriptors, which v5 wants before
            // any transfer (libogc's USB_OpenDevice).
            s32 fd = -1;
            result = USB_OpenDevice(ops[i].dev, GCAD_VID_PID >> 16, GCAD_VID_PID & 0xFFFF, &fd);
        } else {
            result = USB_ClearHalt(ops[i].dev, 0);  // v5: cancels its control and interrupt transfers
        }
        _CPU_ISR_Disable(level);
        Answer(ops[i].tag, result);
        _CPU_ISR_Restore(level);
    }
    static usb_device_entry list[32] ATTRIBUTE_ALIGN(32);
    static_assert(sizeof(usb_device_entry) == 12, "v5 device entries are 12 bytes");
    u8 count = 0;
    bool have_list = false;
    if (g_list_wanted && g_ogc_changed) {
        g_ogc_changed = false;
        have_list = true;
        if (USB_GetDeviceList(list, 32, USB_CLASS_HID, &count) < 0) count = 0;
    } else if (g_list_wanted && !g_notify_armed) {
        g_notify_armed = true;  // left set when refused: libogc has no HID list to follow
        USB_DeviceChangeNotifyAsync(USB_CLASS_HID, OnOgcChange, nullptr);
    }
    _CPU_ISR_Disable(level);
    const std::uint32_t now = Now();
    Reply replies[sizeof(g_replies) / sizeof(g_replies[0])];
    const unsigned n = g_reply_count;
    std::memcpy(replies, g_replies, sizeof(replies));
    g_reply_count = 0;
    for (unsigned i = 0; i < n; ++i) gcad_on_reply(g_state, replies[i].tag, replies[i].result, now);
    if (have_list && g_list_wanted) {
        g_list_wanted = false;
        std::memset(g_list_out, 0, 0x180);
        std::memcpy(g_list_out, list, count * sizeof(usb_device_entry));
        DCFlushRange(g_list_out, 0x180);  // the driver invalidates it before reading
        gcad_on_reply(g_state, g_list_tag, count, now);
    }
    _CPU_ISR_Restore(level);
}

const char* StepName(unsigned step) {
    static const char* const names[] = {"list", "attach finish", "set protocol", "resume", "device info", "init",
                                        "rumble", "poll", "cancel", "set protocol"};
    return step < sizeof(names) / sizeof(names[0]) ? names[step] : "?";
}

bool ErrorLine() {
    if (g_error_lines > kErrorLines) return false;
    if (++g_error_lines > kErrorLines) {
        logf("GameCube adapter: more failures, no longer logged\n");
        return false;
    }
    return true;
}

void Drain() {
    while (g_event_tail != g_event_head) {
        const Event e = g_events[g_event_tail % kEvents];
        g_event_tail = g_event_tail + 1;
        switch (e.code) {
            case GCAD_EV_SUBMIT_FAIL:
                if (ErrorLine()) logf("GameCube adapter: %s not sent (%d)\n", StepName(e.a >> 8), static_cast<int>(e.b));
                break;
            case GCAD_EV_DEVICE:
                logf("GameCube adapter: USB device %08x, %04x:%04x\n", static_cast<unsigned>(e.a),
                     static_cast<unsigned>(e.b) >> 16, static_cast<unsigned>(e.b) & 0xFFFF);
                break;
            case GCAD_EV_FOUND: logf("GameCube adapter: found (device %08x)\n", static_cast<unsigned>(e.a)); break;
            case GCAD_EV_LOST: logf("GameCube adapter: unplugged\n"); break;
            case GCAD_EV_LINKED: logf("GameCube adapter: reports flowing\n"); break;
            case GCAD_EV_PORT:
                logf("GameCube adapter: port %u %s\n", static_cast<unsigned>(e.a) + 1,
                     (e.b & 0x30) != 0 ? "plugged in" : "empty");
                break;
            case GCAD_EV_BAD_REPORT:
                if (ErrorLine()) {
                    logf("GameCube adapter: odd report (first byte 0x%02x, result %d)\n", static_cast<unsigned>(e.a),
                         static_cast<int>(e.b));
                }
                break;
            case GCAD_EV_REPLY:
                if (e.b < 0 && ErrorLine()) {
                    logf("GameCube adapter: %s failed (%d)\n", StepName(e.a >> 8), static_cast<int>(e.b));
                }
                break;
            default: break;
        }
    }
    if (g_events_lost != 0) {
        logf("GameCube adapter: %u log line(s) dropped\n", g_events_lost);
        g_events_lost = 0;
    }
}

void ResetShared() {
    g_shared = false;
    g_ogc = false;
    g_list_wanted = false;
    g_ogc_changed = false;
    g_notify_armed = false;
    g_refused = 0;
    g_reply_count = 0;
    g_op_count = 0;
    g_error_lines = 0;
}

// Why the menu leaves the adapter alone, or nullptr.
const char* MenuOff() {
    if (Settings().gc_adapter == "off") return "the setting is Off";
    // A Wii U's d2x cIOS never answered /dev/usb/hid with an adapter in
    // (On tries anyway; every call at the start has a time limit).
    if (Settings().gc_adapter != "on" && IOS_GetVersion() != 58 && is_wii_u()) {
        return "on a Wii U it needs IOS 58 as the Menu IOS (or the setting On)";
    }
    return nullptr;
}

}  // namespace

bool GcAdapterStart(std::string& why) {
    if (g_running) return true;
    if (g_state == nullptr) {
        g_state = reinterpret_cast<gcad*>(skin::Mem2Alloc(sizeof(gcad)));
        if (g_state == nullptr) {
            why = "no memory for the adapter's buffers";
            return false;
        }
    }
    ResetShared();
    // A v5 device id in libogc's list: libogc keeps that list from
    // /dev/usb/hid's device changes when it is v5 (on v4 it comes from
    // /dev/usb/oh0, without ids).
    std::int32_t dev = -1;
    std::uint32_t version = 5;
    std::string devices;
    if (ogc_adapter(dev, devices) == AdapterSeen::Found && dev >= 0) {
        // libogc's USB owns v5's handle: its device-change notices, its
        // list and its transfers stand in for ours.
        g_fd = -1;
        g_shared = g_ogc = true;
        g_ogc_changed = true;  // libogc's current list answers the first request
        g_notify_armed = USB_DeviceChangeNotifyAsync(USB_CLASS_HID, OnOgcChange, nullptr) >= 0;
        logf("GameCube adapter: through libogc's USB (/dev/usb/hid v5 on IOS%d), device %08x\n", IOS_GetVersion(),
             static_cast<unsigned>(dev));
    } else {
        if (!open_usb_hid(g_fd, version, why)) {
            logf("GameCube adapter: %s (IOS%d)\n", why.c_str(), IOS_GetVersion());
            return false;
        }
        logf("GameCube adapter: /dev/usb/hid v%u on IOS%d, fd %d\n", version, IOS_GetVersion(),
             static_cast<int>(g_fd));
    }
    g_running = true;
    u32 level;
    _CPU_ISR_Disable(level);
    gcad_init(g_state, nullptr, g_fd, version, TB_TIMER_CLOCK);
    if (dev >= 0) gcad_expect(g_state, dev);
    gcad_tick(g_state, Now());
    _CPU_ISR_Restore(level);
    return true;
}

bool GcAdapterRunning() { return g_running; }

void GcAdapterPoll(GcAdapterView& out) {
    out = GcAdapterView{};
    if (!g_running || g_state == nullptr) return;
    ServeShared();
    u32 level;
    _CPU_ISR_Disable(level);
    const std::uint32_t now = Now();
    gcad_tick(g_state, now);
    out.open = true;
    out.through_ogc = g_ogc;
    out.version = g_state->version;
    out.link = g_state->link;
    out.reports = g_state->reports;
    out.listed = g_state->listed;
    out.last_error = g_state->last_error;
    for (unsigned p = 0; p < GCAD_PORTS; ++p) out.present[p] = gcad_port(g_state, p, &out.pads[p], now) != 0;
    _CPU_ISR_Restore(level);
    Drain();
}

void GcAdapterStop() {
    if (!g_running || g_state == nullptr) return;
    u32 level;
    _CPU_ISR_Disable(level);
    gcad_stop(g_state, Now());
    _CPU_ISR_Restore(level);
    // Cancels answer quickly; a second is plenty.
    for (int i = 0; i < 100 && !gcad_idle(g_state); ++i) {
        ServeShared();
        usleep(10000);
    }
    ServeShared();
    const bool idle = gcad_idle(g_state);
    gcad_release(g_state);
    if (g_fd >= 0) IOS_Close(g_fd);
    g_fd = -1;
    g_running = false;
    g_menu_started = false;
    Drain();
    logf("GameCube adapter: closed%s\n", idle ? "" : " (a request was still in flight)");
}

void GcAdapterMenuPads(GcAdapterView& out) {
    out = GcAdapterView{};
    if (g_menu_ended) return;
    if (g_running && g_menu_started && Settings().gc_adapter == "off") {
        GcAdapterStop();
        return;
    }
    if (!g_running) {
        if (!g_menu_allowed) return;
        // Looked for every 2 s, so it can be plugged in with the menu up.
        const u64 now = gettime();
        if (g_menu_failed || (g_menu_checked != 0 && diff_msec(g_menu_checked, now) < 2000)) return;
        g_menu_checked = now;
        if (const char* off = MenuOff()) {
            if (!g_menu_said_off) logf("GameCube adapter: not used in the menu: %s\n", off);
            g_menu_said_off = true;
            return;
        }
        g_menu_said_off = false;
        std::int32_t dev = -1;
        std::string devices;
        if (ogc_adapter(dev, devices) != AdapterSeen::Found) return;
        logf("GameCube adapter: plugged in (USB lists %s); the menu takes its controllers\n", devices.c_str());
        std::string why;
        if (!GcAdapterStart(why)) {
            g_menu_failed = true;
            return;
        }
        g_menu_started = true;
    }
    GcAdapterPoll(out);
}

void GcAdapterMenuAllowStart() {
    g_menu_allowed = true;
}

void GcAdapterMenuEnd() {
    g_menu_ended = true;
    GcAdapterStop();
}

}  // namespace riftwii::wii

// The driver's environment in the menu (runtime/rtgcad.h): libogc's
// asynchronous IPC, replies in its interrupt.
extern "C" {

int32_t gcad_env_ioctl(gcad* g, uint32_t tag, uint32_t cmd, void* in, uint32_t in_len, void* out, uint32_t out_len) {
    using namespace riftwii::wii;
    if (g->version == 5 && cmd == GCAD_V5_GET_DEVICE_CHANGE) {
        g_list_tag = tag;
        g_list_out = out;
    }
    if (g_shared) {
        switch (cmd) {
            case GCAD_V5_GET_DEVICE_CHANGE: g_list_wanted = true; return 0;
            case GCAD_V5_ATTACH_FINISH: Answer(tag, 0); return 0;  // libogc sends its own
            case GCAD_V5_SHUTDOWN:  // it would cancel libogc's
                if (g_list_wanted) {
                    g_list_wanted = false;
                    Answer(g_list_tag, 0);
                }
                Answer(tag, 0);
                return 0;
            default: break;
        }
    }
    if (g_ogc) {
        switch (cmd) {
            // libogc attached it (its AttachFinish) and releases nothing.
            case GCAD_V5_ATTACH:
            case GCAD_V5_RELEASE:
            // USB_OpenDevice read the descriptors with the resume.
            case GCAD_V5_GET_DEVICE_INFO: Answer(tag, 0); return 0;
            case GCAD_V5_SUSPEND_RESUME:
            case GCAD_V5_CANCEL_ENDPOINT:
                return Defer(tag, cmd, static_cast<std::int32_t>(gcad_get32(static_cast<const uint8_t*>(in)))) ? 0 : -22;
            default: return -4;
        }
    }
    return IOS_IoctlAsync(g->fd, static_cast<s32>(cmd), in, static_cast<s32>(in_len), out, static_cast<s32>(out_len),
                          riftwii::wii::OnReply, reinterpret_cast<void*>(static_cast<std::uintptr_t>(tag)));
}

int32_t gcad_env_ioctlv(gcad* g, uint32_t tag, uint32_t cmd, void* msg, uint32_t msg_len, void* data,
                        uint32_t data_len, int data_in) {
    if (riftwii::wii::g_ogc) {
        // The v5 interrupt transfer, as libogc sends it on its handle: the
        // device and the direction from the driver's message.
        if (cmd != GCAD_V5_INTERRUPT) return -4;
        const uint8_t* m = static_cast<const uint8_t*>(msg);
        const s32 dev = static_cast<s32>(gcad_get32(m));
        void* t = reinterpret_cast<void*>(static_cast<std::uintptr_t>(tag));
        return gcad_get32(m + 8) != 0
                   ? USB_WriteIntrMsgAsync(dev, 0x02, static_cast<u16>(data_len), data, riftwii::wii::OnReply, t)
                   : USB_ReadIntrMsgAsync(dev, 0x81, static_cast<u16>(data_len), data, riftwii::wii::OnReply, t);
    }
    // Each slot its own vectors, kept until the reply: libogc turns them
    // into physical addresses in place. The slots using ioctlv are 2-4.
    const uint32_t slot = (tag & 15u) - GCAD_TAG_SETUP;
    if (slot > 2) return -4;
    ioctlv* vec = reinterpret_cast<ioctlv*>(g->env + slot * 16u);
    vec[0].data = msg;
    vec[0].len = msg_len;
    vec[1].data = data;
    vec[1].len = data_len;
    return IOS_IoctlvAsync(g->fd, static_cast<s32>(cmd), data_in ? 2 : 1, data_in ? 0 : 1, vec,
                           riftwii::wii::OnReply, reinterpret_cast<void*>(static_cast<std::uintptr_t>(tag)));
}

uint32_t gcad_env_address(gcad* g, void* p) {
    (void)g;
    return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(p));  // v4 takes the virtual address
}

void gcad_env_flush(gcad* g, void* p, uint32_t len) {
    (void)g;
    DCFlushRange(p, len);
}

void gcad_env_invalidate(gcad* g, void* p, uint32_t len) {
    (void)g;
    DCInvalidateRange(p, len);
}

void gcad_env_event(gcad* g, uint32_t code, uint32_t a, int32_t b) {
    (void)g;
    using namespace riftwii::wii;
    if (g_event_head - g_event_tail >= kEvents) {
        g_events_lost = g_events_lost + 1;
        return;
    }
    g_events[g_event_head % kEvents] = Event{code, a, b};
    g_event_head = g_event_head + 1;
}

}  // extern "C"
