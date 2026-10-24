// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The WUP-028 driver (runtime/rtgcad.c) against a fake /dev/usb/hid that
// follows the documented behaviour of both versions: v4 (wiibrew
// "/dev/usb/hid (v4)", Dolphin's USB_HIDv4) and v5 (wiibrew "/dev/usb/hid
// (v5)" and "/dev/usb/ven", Dolphin's USB_HIDv5): exact buffer sizes,
// the first device list at once and later ones only on a change, the v5
// lock until AttachFinish, ownership through Attach, SuspendResume
// refusing a state it is already in, GetDeviceInfo before transfers,
// the interrupt direction word, transfers cancelled with -7022 when the
// adapter is unplugged. Reports and rumble are the adapter's (Dolphin's
// GCAdapter.cpp).
#include "rtgcad.h"
#include "riftwii/symsearch.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <map>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { const auto va_ = (a); const auto vb_ = (b); if (va_ != vb_) { std::cerr << "FAILED: " #a " == " #b " (" << va_ << " != " << vb_ << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

namespace {

constexpr std::int32_t kEinval = -4;
constexpr std::int32_t kEnoent = -6;
constexpr std::int32_t kCancelled = -7022;

struct Request {
    std::uint32_t tag = 0;
    std::uint32_t cmd = 0;
    bool ioctlv = false;
    std::vector<std::uint8_t> in;     // ioctl input or ioctlv first vector (copied)
    std::uint8_t* out = nullptr;      // ioctl output
    std::uint32_t out_len = 0;
    std::uint8_t* data = nullptr;     // ioctlv second vector
    std::uint32_t data_len = 0;
    bool data_in = false;
};

std::uint32_t be32(const std::uint8_t* p) { return gcad_get32(p); }

// One report: 0x21, then 9 bytes per port.
std::vector<std::uint8_t> Report(std::uint8_t status0, std::uint8_t b1, std::uint8_t b2, std::uint8_t sx,
                                 std::uint8_t sy, std::uint8_t l = 30, std::uint8_t r = 30) {
    std::vector<std::uint8_t> rep(37, 0);
    rep[0] = 0x21;
    for (int p = 0; p < 4; ++p) rep[1 + p * 9] = 0x04;  // powered, nothing plugged
    rep[1] = status0;
    rep[2] = b1;
    rep[3] = b2;
    rep[4] = sx;
    rep[5] = sy;
    rep[6] = 128;
    rep[7] = 128;
    rep[8] = l;
    rep[9] = r;
    return rep;
}

// The fake IOS. Requests are queued; the test decides when IOS answers.
struct FakeHid {
    std::uint32_t version = 5;
    bool adapter = true;
    std::uint32_t dev_id = 0x00010042;  // v5: interface byte 0, index 1, number 0x42
    bool first_list_done = false;
    bool list_changed = false;          // a change happened while nobody listened
    bool locked = false;                // v5: locked to our handle until AttachFinish
    bool owned_by_other = false;        // v5: another handle attached it
    bool ours = false;
    int attach_calls = 0;                // v5 Attach/Release requests seen
    int control_answer = 0;              // v5 SET_PROTOCOL: 0 ok, <0 refused (Mayflash), 1 no answer
    bool resumed = false;
    bool info_read = false;
    std::deque<Request> queue;          // answered at once when possible
    std::vector<Request> waiting_list;  // device list hooks
    std::vector<Request> waiting_in;    // polls waiting for a report
    std::vector<std::vector<std::uint8_t>> sent_out;  // OUT transfers' bytes
    std::vector<std::uint32_t> control_setup;          // bmRequestType << 8 | bRequest
    std::vector<std::pair<std::uint32_t, std::int32_t>> replies;  // tag, result
    std::map<std::uint32_t, std::uint8_t*> addresses;  // v4 virtual pointers
    std::uint32_t next_address = 0x90001000;
    int in_flight_transfers = 0;
    int max_in_flight = 0;
};

FakeHid* g_hid = nullptr;

void FillList(Request& r) {
    FakeHid& h = *g_hid;
    std::memset(r.out, 0, r.out_len);
    if (h.version == 4) {
        std::uint32_t at = 0;
        // A keyboard first, then the adapter (0x44 bytes, as wiibrew shows).
        gcad_put32(r.out + at, 0x44);
        gcad_put32(r.out + at + 4, 3);
        gcad_put32(r.out + at + 8, 0x12010110);
        gcad_put32(r.out + at + 16, 0x046D0001);
        at += 0x44;
        if (h.adapter) {
            gcad_put32(r.out + at, 0x44);
            gcad_put32(r.out + at + 4, h.dev_id);
            gcad_put32(r.out + at + 8, 0x12010200);
            gcad_put32(r.out + at + 16, GCAD_VID_PID);
            at += 0x44;
        }
        gcad_put32(r.out + at, 0xFFFFFFFFu);
        h.replies.push_back({r.tag, 0});
    } else {
        std::uint32_t n = 0;
        gcad_put32(r.out, 0x00000105);
        gcad_put32(r.out + 4, 0x046D0001);
        ++n;
        if (h.adapter) {
            gcad_put32(r.out + 12, h.dev_id);
            gcad_put32(r.out + 16, GCAD_VID_PID);
            ++n;
        }
        h.locked = true;
        h.replies.push_back({r.tag, static_cast<std::int32_t>(n)});
    }
}

std::int32_t Handle(Request& r) {
    FakeHid& h = *g_hid;
    if (h.version == 4) {
        switch (r.cmd) {
        case GCAD_V4_GET_DEVICE_CHANGE:
            if (r.out_len != 0x600 || r.out == nullptr) return kEinval;
            if (!h.first_list_done || h.list_changed) {
                h.first_list_done = true;
                h.list_changed = false;
                FillList(r);
            } else {
                h.waiting_list.push_back(r);
            }
            return 0;
        case GCAD_V4_CONTROL:
        case GCAD_V4_INTERRUPT_IN:
        case GCAD_V4_INTERRUPT_OUT: {
            if (r.in.size() != 32) return kEinval;
            if (!h.adapter || be32(r.in.data() + 16) != h.dev_id) {
                h.replies.push_back({r.tag, kEnoent});
                return 0;
            }
            if (r.cmd == GCAD_V4_CONTROL) {
                h.control_setup.push_back((r.in[20] << 8) | r.in[21]);
                h.replies.push_back({r.tag, 0});
                return 0;
            }
            const std::uint32_t endpoint = be32(r.in.data() + 20);
            const std::uint32_t len = be32(r.in.data() + 24);
            std::uint8_t* data = h.addresses[be32(r.in.data() + 28)];
            if (data == nullptr) return kEinval;
            if (r.cmd == GCAD_V4_INTERRUPT_OUT) {
                if (endpoint != 0x02) return kEinval;
                h.sent_out.emplace_back(data, data + len);
                h.replies.push_back({r.tag, static_cast<std::int32_t>(len)});
            } else {
                if (endpoint != 0x81 || len != 37) return kEinval;
                r.data = data;
                r.data_len = len;
                h.waiting_in.push_back(r);
            }
            return 0;
        }
        case GCAD_V4_SHUTDOWN:
            for (Request& w : h.waiting_list) h.replies.push_back({w.tag, -1});
            h.waiting_list.clear();
            h.replies.push_back({r.tag, 0});
            return 0;
        case GCAD_V4_CANCEL_INTERRUPT:
            if (r.in.size() != 8) return kEinval;
            if (r.in[4] == 0x81) {
                for (Request& w : h.waiting_in) h.replies.push_back({w.tag, kCancelled});
                h.waiting_in.clear();
            }
            h.replies.push_back({r.tag, 0});
            return 0;
        default:
            return kEinval;
        }
    }
    switch (r.cmd) {
    case GCAD_V5_GET_DEVICE_CHANGE:
        if (r.out_len != 0x180 || !r.in.empty()) return kEinval;
        if (!h.waiting_list.empty()) return kEinval;
        if (!h.first_list_done || h.list_changed) {
            h.first_list_done = true;
            h.list_changed = false;
            FillList(r);
        } else {
            h.waiting_list.push_back(r);
        }
        return 0;
    case GCAD_V5_ATTACH_FINISH:
        if (!h.locked) {
            h.replies.push_back({r.tag, kEinval});
            return 0;
        }
        h.locked = false;
        h.replies.push_back({r.tag, 0});
        return 0;
    case GCAD_V5_SHUTDOWN:
        for (Request& w : h.waiting_list) h.replies.push_back({w.tag, 0});
        h.waiting_list.clear();
        h.replies.push_back({r.tag, 0});
        return 0;
    case GCAD_V5_ATTACH:
    case GCAD_V5_RELEASE:
    case GCAD_V5_SUSPEND_RESUME:
    case GCAD_V5_GET_DEVICE_INFO:
    case GCAD_V5_CANCEL_ENDPOINT: {
        if (r.in.size() != 0x20) return kEinval;
        if (!h.adapter || be32(r.in.data()) != h.dev_id) {
            h.replies.push_back({r.tag, kEinval});
            return 0;
        }
        std::int32_t result = 0;
        if (r.cmd == GCAD_V5_ATTACH || r.cmd == GCAD_V5_RELEASE) {
            h.attach_calls += 1;  // IOS 58 needs neither; the driver sends none
        } else if (r.cmd == GCAD_V5_SUSPEND_RESUME) {
            const bool want = r.in[11] != 0;
            if (want == h.resumed) result = kEinval;
            else h.resumed = want;
        } else if (r.cmd == GCAD_V5_GET_DEVICE_INFO) {
            // Another handle owns it: refused until it lets go.
            if (r.out_len != 0x60 || r.in[8] != 0 || h.owned_by_other || !h.resumed) {
                result = kEinval;
            } else {
                std::memset(r.out, 0, 0x60);
                gcad_put32(r.out, h.dev_id);
                r.out[80 + 2] = 0x81;
                r.out[88 + 2] = 0x02;
                h.info_read = true;
                h.ours = true;
            }
        } else {
            const std::uint8_t which = r.in[8];
            if (which == 1) {
                for (Request& w : h.waiting_in) {
                    h.replies.push_back({w.tag, kCancelled});
                    --h.in_flight_transfers;
                }
                h.waiting_in.clear();
            }
        }
        h.replies.push_back({r.tag, result});
        return 0;
    }
    case GCAD_V5_CONTROL:
    case GCAD_V5_INTERRUPT: {
        if (!r.ioctlv || r.in.size() != 0x40) return kEinval;
        if (!h.adapter || be32(r.in.data()) != h.dev_id || !h.ours || !h.info_read) {
            h.replies.push_back({r.tag, kEinval});
            return 0;
        }
        if (h.in_flight_transfers >= 32) return -5;
        if (r.cmd == GCAD_V5_CONTROL) {
            if (!r.data_in) return kEinval;
            h.control_setup.push_back((r.in[8] << 8) | r.in[9]);
            if (h.control_answer != 1) h.replies.push_back({r.tag, h.control_answer});
            return 0;
        }
        const bool out = be32(r.in.data() + 8) != 0;
        if (out != r.data_in) return kEinval;  // OUT is 2 in / 0 io, IN 1 in / 1 io
        if (out) {
            h.sent_out.emplace_back(r.data, r.data + r.data_len);
            h.replies.push_back({r.tag, static_cast<std::int32_t>(r.data_len)});
        } else {
            h.waiting_in.push_back(r);
            ++h.in_flight_transfers;
            if (h.in_flight_transfers > h.max_in_flight) h.max_in_flight = h.in_flight_transfers;
        }
        return 0;
    }
    default:
        return kEinval;
    }
}

struct Driver {
    gcad* g = nullptr;
    std::uint32_t now = 1000;
    std::vector<std::string> events;
};
Driver* g_driver = nullptr;

// IOS answers: every queued reply goes back to the driver, which may
// submit more; until nothing is left.
void Pump() {
    for (int guard = 0; guard < 1000 && !g_hid->replies.empty(); ++guard) {
        const auto reply = g_hid->replies.front();
        g_hid->replies.erase(g_hid->replies.begin());
        gcad_on_reply(g_driver->g, reply.first, reply.second, g_driver->now);
    }
}

// The adapter sends a report: the oldest waiting poll gets it.
bool Deliver(const std::vector<std::uint8_t>& report) {
    FakeHid& h = *g_hid;
    if (h.waiting_in.empty()) return false;
    Request r = h.waiting_in.front();
    h.waiting_in.erase(h.waiting_in.begin());
    if (h.version == 5) --h.in_flight_transfers;
    std::memcpy(r.data, report.data(), report.size());
    h.replies.push_back({r.tag, static_cast<std::int32_t>(report.size())});
    Pump();
    return true;
}

// A pad appearing: the readings at rest its origin waits for
// (GCAD_ORIGIN_STEADY_READS in a row, 8 ms apart).
bool DeliverSettled(const std::vector<std::uint8_t>& report) {
    for (int k = 0; k < GCAD_ORIGIN_STEADY_READS; ++k) {
        if (k != 0) g_driver->now += 8;
        if (!Deliver(report)) return false;
    }
    return true;
}

void Unplug() {
    FakeHid& h = *g_hid;
    h.adapter = false;
    h.ours = false;
    h.resumed = false;
    h.info_read = false;
    for (Request& w : h.waiting_in) h.replies.push_back({w.tag, kCancelled});
    if (h.version == 5) h.in_flight_transfers = 0;
    h.waiting_in.clear();
    if (!h.waiting_list.empty()) {
        Request w = h.waiting_list.front();
        h.waiting_list.clear();
        FillList(w);
    } else {
        h.list_changed = true;
    }
    Pump();
}

void Plug(std::uint32_t new_id) {
    FakeHid& h = *g_hid;
    h.adapter = true;
    h.dev_id = new_id;
    if (!h.waiting_list.empty()) {
        Request w = h.waiting_list.front();
        h.waiting_list.clear();
        FillList(w);
    } else {
        h.list_changed = true;
    }
    Pump();
    // v5: the driver sets a newly listed adapter up after a short wait.
    if (h.version == 5 && g_driver != nullptr) {
        g_driver->now += GCAD_SETTLE_MS;
        gcad_tick(g_driver->g, g_driver->now);
        Pump();
    }
}

gcad* NewState() {
    // 32-byte aligned, as MEM2 buffers must be.
    static std::vector<std::uint8_t> storage;
    storage.assign(sizeof(gcad) + 64, 0);
    std::uintptr_t p = reinterpret_cast<std::uintptr_t>(storage.data());
    p = (p + 31) & ~std::uintptr_t(31);
    return reinterpret_cast<gcad*>(p);
}

void Start(FakeHid& hid, Driver& driver, std::uint32_t version) {
    hid.version = version;
    g_hid = &hid;
    g_driver = &driver;
    driver.g = NewState();
    gcad_init(driver.g, &driver, 7, version, 1);
    gcad_tick(driver.g, driver.now);
    Pump();
    if (version == 5) {
        driver.now += GCAD_SETTLE_MS;
        gcad_tick(driver.g, driver.now);
        Pump();
    }
}

}  // namespace

extern "C" {
std::int32_t gcad_env_ioctl(gcad* g, std::uint32_t tag, std::uint32_t cmd, void* in, std::uint32_t in_len, void* out,
                            std::uint32_t out_len) {
    (void)g;
    Request r;
    r.tag = tag;
    r.cmd = cmd;
    if (in != nullptr) r.in.assign(static_cast<std::uint8_t*>(in), static_cast<std::uint8_t*>(in) + in_len);
    r.out = static_cast<std::uint8_t*>(out);
    r.out_len = out_len;
    return Handle(r);
}

std::int32_t gcad_env_ioctlv(gcad* g, std::uint32_t tag, std::uint32_t cmd, void* msg, std::uint32_t msg_len,
                             void* data, std::uint32_t data_len, int data_in) {
    (void)g;
    Request r;
    r.tag = tag;
    r.cmd = cmd;
    r.ioctlv = true;
    r.in.assign(static_cast<std::uint8_t*>(msg), static_cast<std::uint8_t*>(msg) + msg_len);
    r.data = static_cast<std::uint8_t*>(data);
    r.data_len = data_len;
    r.data_in = data_in != 0;
    return Handle(r);
}

std::uint32_t gcad_env_address(gcad* g, void* p) {
    (void)g;
    for (const auto& a : g_hid->addresses) {
        if (a.second == p) return a.first;
    }
    const std::uint32_t address = g_hid->next_address;
    g_hid->next_address += 0x40;
    g_hid->addresses[address] = static_cast<std::uint8_t*>(p);
    return address;
}

void gcad_env_flush(gcad*, void*, std::uint32_t) {}
void gcad_env_invalidate(gcad*, void*, std::uint32_t) {}

void gcad_env_event(gcad* g, std::uint32_t code, std::uint32_t a, std::int32_t b) {
    (void)g;
    g_driver->events.push_back(std::to_string(code) + ":" + std::to_string(a) + ":" + std::to_string(b));
}
}

static void TestHelpers() {
    std::uint8_t raw[9] = {0x14, 0x01 | 0x80, 0x01 | 0x02 | 0x04 | 0x08, 200, 60, 128, 128, 255, 20};
    const std::uint8_t origin[6] = {130, 126, 128, 128, 30, 30};
    gcad_pad pad{};
    EXPECT_TRUE(gcad_decode(raw, origin, &pad));
    EXPECT_EQ(pad.buttons, GCAD_PAD_A | GCAD_PAD_UP | GCAD_PAD_START | GCAD_PAD_Z | GCAD_PAD_R | GCAD_PAD_L);
    EXPECT_EQ(static_cast<int>(pad.stick_x), 70);
    EXPECT_EQ(static_cast<int>(pad.stick_y), -66);
    EXPECT_EQ(static_cast<int>(pad.trigger_l), 225);
    EXPECT_EQ(static_cast<int>(pad.trigger_r), 0);  // below the origin reads 0
    EXPECT_EQ(static_cast<int>(pad.wireless), 0);
    raw[0] = 0x24;  // a WaveBird
    EXPECT_TRUE(gcad_decode(raw, origin, &pad));
    EXPECT_EQ(static_cast<int>(pad.wireless), 1);
    raw[0] = 0x04;  // powered, nothing plugged
    EXPECT_FALSE(gcad_decode(raw, origin, &pad));
    // Far from the origin, the value clamps instead of wrapping.
    std::uint8_t far[9] = {0x10, 0, 0, 0, 255, 128, 128, 0, 0};
    const std::uint8_t off[6] = {200, 10, 128, 128, 0, 0};
    EXPECT_TRUE(gcad_decode(far, off, &pad));
    EXPECT_EQ(static_cast<int>(pad.stick_x), -128);
    EXPECT_EQ(static_cast<int>(pad.stick_y), 127);

    std::vector<std::uint8_t> v4(0x600, 0);
    gcad_put32(v4.data(), 0x44);
    gcad_put32(v4.data() + 4, 0);
    gcad_put32(v4.data() + 16, 0x1BAD3330);
    gcad_put32(v4.data() + 0x44, 0x48);
    gcad_put32(v4.data() + 0x48, 5);
    gcad_put32(v4.data() + 0x54, GCAD_VID_PID);
    gcad_put32(v4.data() + 0x8C, 0xFFFFFFFF);
    EXPECT_EQ(gcad_find_v4(v4.data(), 0x600), 5);
    gcad_put32(v4.data() + 0x44, 0xFFFFFFFF);
    EXPECT_EQ(gcad_find_v4(v4.data(), 0x600), -1);
    gcad_put32(v4.data(), 3);  // a broken size ends the walk
    EXPECT_EQ(gcad_find_v4(v4.data(), 0x600), -1);

    std::vector<std::uint8_t> v5(0x180, 0);
    gcad_put32(v5.data() + 12, 0x00020007);
    gcad_put32(v5.data() + 16, GCAD_VID_PID);
    EXPECT_EQ(gcad_find_v5(v5.data(), 2), 0x00020007);
    EXPECT_EQ(gcad_find_v5(v5.data(), 1), -1);
}

// What keeps the screen lit: a button, or a stick or trigger moved
// GCAD_ACTIVE_STEP or more since the last read; a resting stick's wobble
// does not.
static void TestPadMoved() {
    gcad_pad before{};
    before.stick_x = 3;
    before.stick_y = -2;
    before.trigger_l = 12;
    gcad_pad now = before;
    EXPECT_FALSE(gcad_pad_moved(&before, &now));
    now.buttons = GCAD_PAD_A;
    EXPECT_TRUE(gcad_pad_moved(&before, &now));
    now = before;
    now.stick_x = static_cast<std::int8_t>(before.stick_x + GCAD_ACTIVE_STEP - 1);  // wobble
    now.stick_y = static_cast<std::int8_t>(before.stick_y - GCAD_ACTIVE_STEP + 1);
    EXPECT_FALSE(gcad_pad_moved(&before, &now));
    now.stick_x = static_cast<std::int8_t>(before.stick_x + GCAD_ACTIVE_STEP);
    EXPECT_TRUE(gcad_pad_moved(&before, &now));
    now = before;
    now.substick_y = -GCAD_ACTIVE_STEP;  // down counts as much as up
    EXPECT_TRUE(gcad_pad_moved(&before, &now));
    now = before;
    now.trigger_r = GCAD_ACTIVE_STEP;
    EXPECT_TRUE(gcad_pad_moved(&before, &now));
    now = before;
    now.wireless = 1;  // not something the player does
    EXPECT_FALSE(gcad_pad_moved(&before, &now));
}

static void ExpectPort(gcad* g, std::uint32_t port, std::uint16_t buttons, int stick_x, std::uint32_t now) {
    gcad_pad pad{};
    EXPECT_TRUE(gcad_port(g, port, &pad, now));
    EXPECT_EQ(pad.buttons, buttons);
    EXPECT_EQ(static_cast<int>(pad.stick_x), stick_x);
}

static void TestV5() {
    FakeHid hid;
    Driver d;
    Start(hid, d, 5);
    gcad* g = d.g;
    // List, a short wait, resume, info, AttachFinish, SET_PROTOCOL, init;
    // no Attach (IOS 58 lists it attached; libogc and Nintendont send none).
    EXPECT_FALSE(hid.locked);
    EXPECT_TRUE(hid.ours);
    EXPECT_TRUE(hid.resumed);
    EXPECT_TRUE(hid.info_read);
    EXPECT_EQ(g->link, GCAD_LINK_POLL);
    EXPECT_EQ(hid.attach_calls, 0);
    EXPECT_EQ(hid.control_setup.size(), 1u);
    if (!hid.control_setup.empty()) EXPECT_EQ(hid.control_setup[0], 0x210B);
    EXPECT_TRUE(!hid.sent_out.empty() && hid.sent_out[0].size() == 1 && hid.sent_out[0][0] == 0x13);
    EXPECT_EQ(hid.waiting_in.size(), 1u);
    EXPECT_EQ(hid.waiting_list.size(), 1u);  // listening for the next change
    EXPECT_FALSE(gcad_port(g, 0, nullptr, d.now));

    // A pad on port 1: its first readings at rest are its origin.
    EXPECT_TRUE(DeliverSettled(Report(0x14, 0, 0, 131, 125)));
    ExpectPort(g, 0, 0, 0, d.now);
    // The rumble-off command goes between polls.
    EXPECT_EQ(hid.sent_out.size(), 2u);
    if (hid.sent_out.size() >= 2) {
        const std::vector<std::uint8_t> off = {0x11, 0, 0, 0, 0};
        EXPECT_TRUE(hid.sent_out[1] == off);
    }
    d.now += 8;
    EXPECT_TRUE(Deliver(Report(0x14, 0x01 | 0x10, 0x01, 200, 125)));
    ExpectPort(g, 0, GCAD_PAD_A | GCAD_PAD_LEFT | GCAD_PAD_START, 69, d.now);
    gcad_pad unused{};
    EXPECT_FALSE(gcad_port(g, 1, &unused, d.now));

    // Rumble: one command, then nothing while it is unchanged.
    gcad_rumble(g, 0, 1);
    d.now += 8;
    EXPECT_TRUE(Deliver(Report(0x14, 0, 0, 131, 125)));
    EXPECT_EQ(hid.sent_out.size(), 3u);
    if (hid.sent_out.size() >= 3) EXPECT_EQ(static_cast<int>(hid.sent_out[2][1]), 1);
    d.now += 8;
    EXPECT_TRUE(Deliver(Report(0x14, 0, 0, 131, 125)));
    EXPECT_EQ(hid.sent_out.size(), 3u);
    gcad_rumble(g, 0, 2);  // hard stop is a stop
    d.now += 8;
    EXPECT_TRUE(Deliver(Report(0x14, 0, 0, 131, 125)));
    EXPECT_EQ(hid.sent_out.size(), 4u);
    if (hid.sent_out.size() >= 4) EXPECT_EQ(static_cast<int>(hid.sent_out[3][1]), 0);
    EXPECT_TRUE(hid.max_in_flight <= 1);

    // A malformed report changes nothing; the poll goes on.
    d.now += 8;
    std::vector<std::uint8_t> junk(37, 0x55);
    EXPECT_TRUE(Deliver(junk));
    EXPECT_EQ(g->bad_reports, 1u);
    ExpectPort(g, 0, 0, 0, d.now);

    // Silence: after the timeout every port reads as unplugged.
    d.now += GCAD_TIMEOUT_MS + 1;
    gcad_tick(g, d.now);
    EXPECT_FALSE(gcad_port(g, 0, &unused, d.now));
    d.now += 8;
    EXPECT_TRUE(DeliverSettled(Report(0x14, 0, 0, 140, 125)));
    ExpectPort(g, 0, 0, 0, d.now);  // a fresh origin after the gap

    // X+Y+Start held for three seconds: a new origin.
    d.now += 8;
    EXPECT_TRUE(Deliver(Report(0x14, 0x0C, 0x01, 150, 125)));
    d.now += GCAD_RECAL_MS;
    EXPECT_TRUE(Deliver(Report(0x14, 0x0C, 0x01, 150, 125)));
    d.now += 8;
    EXPECT_TRUE(Deliver(Report(0x14, 0, 0, 150, 125)));
    ExpectPort(g, 0, 0, 0, d.now);

    // Unplugged: the poll is cancelled (-7022), the list says it is gone.
    Unplug();
    EXPECT_EQ(g->dev_id, -1);
    EXPECT_FALSE(gcad_port(g, 0, &unused, d.now));
    EXPECT_FALSE(hid.locked);
    // Back, with a new device number: set up again.
    Plug(0x00010043);
    EXPECT_EQ(g->dev_id, 0x00010043);
    EXPECT_EQ(g->link, GCAD_LINK_POLL);
    EXPECT_FALSE(hid.locked);
    d.now += 8;
    EXPECT_TRUE(DeliverSettled(Report(0x14, 0x02, 0, 128, 128)));
    ExpectPort(g, 0, GCAD_PAD_B, 0, d.now);

    // Stop: the list hook and the poll are cancelled, the device released.
    gcad_stop(g, d.now);
    Pump();
    EXPECT_TRUE(gcad_idle(g));
    EXPECT_EQ(hid.attach_calls, 0);  // nothing taken, nothing released
    EXPECT_TRUE(hid.waiting_list.empty());
    EXPECT_TRUE(hid.waiting_in.empty());
    EXPECT_FALSE(gcad_port(g, 0, &unused, d.now));
}

static void TestV5OwnedElsewhere() {
    // Another handle (fakemote while it looks for a driver) has it: the
    // info read is refused, AttachFinish still goes out, and the next
    // change (its release) brings it to us.
    FakeHid hid;
    hid.owned_by_other = true;
    Driver d;
    Start(hid, d, 5);
    gcad* g = d.g;
    EXPECT_EQ(g->link, GCAD_LINK_BUSY);
    EXPECT_FALSE(hid.locked);
    EXPECT_EQ(hid.waiting_list.size(), 1u);
    EXPECT_TRUE(hid.waiting_in.empty());
    hid.owned_by_other = false;
    Plug(hid.dev_id);  // the release shows up as a change
    EXPECT_EQ(g->link, GCAD_LINK_POLL);
    EXPECT_TRUE(DeliverSettled(Report(0x24, 0, 0, 128, 128)));
    gcad_pad pad{};
    EXPECT_TRUE(gcad_port(g, 0, &pad, d.now));
    EXPECT_EQ(static_cast<int>(pad.wireless), 1);
    gcad_rumble(g, 0, 1);  // no motor in a WaveBird
    const std::size_t sent = hid.sent_out.size();
    EXPECT_TRUE(Deliver(Report(0x24, 0, 0, 128, 128)));
    EXPECT_EQ(hid.sent_out.size(), sent);

    // Or it frees up without a change: tried again after a while.
    FakeHid hid2;
    hid2.owned_by_other = true;
    Driver d2;
    Start(hid2, d2, 5);
    EXPECT_EQ(d2.g->link, GCAD_LINK_BUSY);
    hid2.owned_by_other = false;
    d2.now += GCAD_RESCAN_MS;
    gcad_tick(d2.g, d2.now);
    Pump();
    EXPECT_EQ(d2.g->link, GCAD_LINK_POLL);
}

static void TestV5AlreadyResumed() {
    // Resumed by an earlier user: SuspendResume answers -4, which is fine.
    FakeHid hid;
    hid.resumed = true;
    Driver d;
    Start(hid, d, 5);
    EXPECT_EQ(d.g->resume_result, kEinval);
    EXPECT_EQ(d.g->link, GCAD_LINK_POLL);
}

static void TestV5SetProtocolRefused() {
    // Mayflash adapters refuse SET_PROTOCOL: init follows anyway.
    FakeHid hid;
    hid.control_answer = -4;
    Driver d;
    Start(hid, d, 5);
    EXPECT_EQ(d.g->ctrl_result, -4);
    EXPECT_EQ(d.g->link, GCAD_LINK_POLL);
    EXPECT_TRUE(!hid.sent_out.empty() && hid.sent_out[0][0] == 0x13);

    // No answer at all: init after GCAD_CTRL_MS.
    FakeHid hid2;
    hid2.control_answer = 1;
    Driver d2;
    Start(hid2, d2, 5);
    EXPECT_EQ(d2.g->link, GCAD_LINK_CTRL);
    EXPECT_TRUE(hid2.sent_out.empty());
    d2.now += GCAD_CTRL_MS;
    gcad_tick(d2.g, d2.now);
    Pump();
    EXPECT_EQ(d2.g->link, GCAD_LINK_POLL);
    EXPECT_TRUE(!hid2.sent_out.empty() && hid2.sent_out[0][0] == 0x13);
}

static void TestV5KnownDevice() {
    // The menu's USB took v5's list: the driver's own request waits for
    // the next change, and the device the loader passed links anyway.
    FakeHid hid;
    hid.version = 5;
    hid.first_list_done = true;
    g_hid = &hid;
    Driver d;
    g_driver = &d;
    d.g = NewState();
    gcad_init(d.g, &d, 7, 5, 1);
    gcad_expect(d.g, static_cast<std::int32_t>(hid.dev_id));
    gcad_tick(d.g, d.now);
    Pump();
    EXPECT_EQ(hid.waiting_list.size(), 1u);  // still listening
    EXPECT_TRUE(hid.ours);
    EXPECT_EQ(d.g->link, GCAD_LINK_POLL);
    EXPECT_TRUE(!hid.sent_out.empty() && hid.sent_out[0].size() == 1 && hid.sent_out[0][0] == 0x13);
    EXPECT_TRUE(DeliverSettled(Report(0x14, 0, 0, 128, 128)));
    gcad_pad pad{};
    EXPECT_TRUE(gcad_port(d.g, 0, &pad, d.now));
}

static void TestV4() {
    FakeHid hid;
    hid.dev_id = 2;
    Driver d;
    Start(hid, d, 4);
    gcad* g = d.g;
    EXPECT_EQ(g->dev_id, 2);
    EXPECT_EQ(g->link, GCAD_LINK_POLL);
    EXPECT_EQ(g->listed, 2u);
    EXPECT_EQ(hid.control_setup.size(), 1u);  // SET_PROTOCOL, answer ignored
    EXPECT_TRUE(!hid.sent_out.empty() && hid.sent_out[0][0] == 0x13);
    EXPECT_EQ(hid.waiting_list.size(), 1u);
    EXPECT_TRUE(DeliverSettled(Report(0x14, 0x08, 0, 128, 128)));
    gcad_pad pad{};
    EXPECT_TRUE(gcad_port(g, 0, &pad, d.now));
    EXPECT_EQ(pad.buttons, GCAD_PAD_Y);
    // A second pad on port 3.
    std::vector<std::uint8_t> rep = Report(0x14, 0, 0, 128, 128);
    rep[1 + 2 * 9] = 0x14;
    rep[1 + 2 * 9 + 1] = 0x01;
    rep[1 + 2 * 9 + 3] = 128;
    rep[1 + 2 * 9 + 4] = 128;
    rep[1 + 2 * 9 + 5] = 128;  // the C-stick at rest too
    rep[1 + 2 * 9 + 6] = 128;
    EXPECT_TRUE(DeliverSettled(rep));
    rep[1 + 2 * 9 + 1] = 0x02;
    EXPECT_TRUE(Deliver(rep));
    EXPECT_TRUE(gcad_port(g, 2, &pad, d.now));
    EXPECT_EQ(pad.buttons, GCAD_PAD_B);

    Unplug();
    EXPECT_EQ(g->dev_id, -1);
    Plug(4);
    EXPECT_EQ(g->dev_id, 4);
    EXPECT_EQ(g->link, GCAD_LINK_POLL);
    EXPECT_TRUE(DeliverSettled(Report(0x14, 0x01, 0, 128, 128)));
    EXPECT_TRUE(gcad_port(g, 0, &pad, d.now));

    gcad_stop(g, d.now);
    Pump();
    EXPECT_TRUE(gcad_idle(g));
    EXPECT_TRUE(hid.waiting_list.empty());
    EXPECT_TRUE(hid.waiting_in.empty());
}

static unsigned OriginEvents(const Driver& d, std::uint32_t port, std::uint32_t how) {
    const std::string prefix = std::to_string(GCAD_EV_ORIGIN) + ":";
    unsigned count = 0;
    for (const std::string& e : d.events) {
        if (e.compare(0, prefix.size(), prefix) != 0) continue;
        const std::uint32_t a = static_cast<std::uint32_t>(std::stoul(e.substr(prefix.size())));
        if ((a & 0xFF) == port && ((a >> 8) & 0xFF) == how) ++count;
    }
    return count;
}

// The origin comes only from the pad at rest: a reading far off (a stick
// pinned to a corner at RiftWii's start on a Wii U) is passed over and the
// port reads empty until one at rest; a pad held off the middle gets the
// middle for those axes after GCAD_ORIGIN_WAIT_MS.
static void TestOriginAtRest() {
    FakeHid hid;
    hid.dev_id = 2;
    Driver d;
    Start(hid, d, 4);
    gcad* g = d.g;
    gcad_pad pad{};

    // Sticks at 0 in the pad's first reading: no origin from it.
    EXPECT_TRUE(Deliver(Report(0x14, 0, 0, 0, 0)));
    EXPECT_FALSE(gcad_port(g, 0, &pad, d.now));
    EXPECT_EQ(OriginEvents(d, 0, GCAD_ORIGIN_RESTING), 0u);
    EXPECT_EQ(OriginEvents(d, 0, GCAD_ORIGIN_PASSED), 1u);  // logged once
    // The next readings at rest, steady, are the origin.
    d.now += 8;
    EXPECT_TRUE(DeliverSettled(Report(0x14, 0, 0, 131, 125)));
    ExpectPort(g, 0, 0, 0, d.now);
    EXPECT_EQ(OriginEvents(d, 0, GCAD_ORIGIN_RESTING), 1u);
    d.now += 8;
    EXPECT_TRUE(Deliver(Report(0x14, 0, 0, 231, 125)));
    ExpectPort(g, 0, 0, 100, d.now);

    // Plugged in again with the stick held right: empty while it waits.
    d.now += 8;
    EXPECT_TRUE(Deliver(Report(0x04, 0, 0, 128, 128)));
    EXPECT_FALSE(gcad_port(g, 0, &pad, d.now));
    d.now += 8;
    EXPECT_TRUE(Deliver(Report(0x14, 0, 0, 230, 128)));
    EXPECT_FALSE(gcad_port(g, 0, &pad, d.now));
    d.now += GCAD_ORIGIN_WAIT_MS - 8;
    EXPECT_TRUE(Deliver(Report(0x14, 0, 0, 230, 128)));
    EXPECT_FALSE(gcad_port(g, 0, &pad, d.now));
    // Still held at the end of the wait: x takes the middle, y (at rest)
    // its reading, so the stick reads as held, and centred once let go.
    d.now += 8;
    EXPECT_TRUE(Deliver(Report(0x14, 0, 0, 230, 128)));
    ExpectPort(g, 0, 0, 102, d.now);
    EXPECT_EQ(OriginEvents(d, 0, GCAD_ORIGIN_MIDDLE), 1u);
    EXPECT_EQ(OriginEvents(d, 0, GCAD_ORIGIN_PASSED), 2u);  // once for the whole wait, not per reading
    d.now += 8;
    EXPECT_TRUE(Deliver(Report(0x14, 0, 0, 128, 128)));
    ExpectPort(g, 0, 0, 0, d.now);

    // A trigger held at the start: it takes 0, the rest their readings.
    d.now += 8;
    EXPECT_TRUE(Deliver(Report(0x04, 0, 0, 128, 128)));
    d.now += 8;
    EXPECT_TRUE(DeliverSettled(Report(0x14, 0, 0, 126, 128, 200, 30)));  // the axes at rest settle
    d.now += GCAD_ORIGIN_WAIT_MS;
    EXPECT_TRUE(Deliver(Report(0x14, 0, 0, 126, 128, 200, 30)));
    EXPECT_TRUE(gcad_port(g, 0, &pad, d.now));
    EXPECT_EQ(static_cast<int>(pad.stick_x), 0);
    EXPECT_EQ(static_cast<int>(pad.trigger_l), 200);
    EXPECT_EQ(static_cast<int>(pad.trigger_r), 0);

    // X+Y+Start still takes the reading as it is, as the pad does.
    d.now += 8;
    EXPECT_TRUE(Deliver(Report(0x14, 0x0C, 0x01, 200, 128)));
    d.now += GCAD_RECAL_MS;
    EXPECT_TRUE(Deliver(Report(0x14, 0x0C, 0x01, 200, 128)));
    d.now += 8;
    EXPECT_TRUE(Deliver(Report(0x14, 0, 0, 200, 128)));
    ExpectPort(g, 0, 0, 0, d.now);
    EXPECT_EQ(OriginEvents(d, 0, GCAD_ORIGIN_RECAL), 1u);

    // A pad that sends readings jumping about for a moment after it
    // appears, as one did at RiftWii's start on a Wii U (2610-603):
    // 233,147 then 99,83 while sitting still, then its own rest. None of
    // the jumping readings is the origin; three steady ones at rest are.
    const unsigned resting = OriginEvents(d, 0, GCAD_ORIGIN_RESTING);
    const unsigned middle = OriginEvents(d, 0, GCAD_ORIGIN_MIDDLE);
    d.now += 8;
    EXPECT_TRUE(Deliver(Report(0x04, 0, 0, 128, 128)));
    d.now += 8;
    EXPECT_TRUE(Deliver(Report(0x14, 0, 0, 233, 147)));
    d.now += 8;
    EXPECT_TRUE(Deliver(Report(0x14, 0, 0, 99, 83)));  // 45 below the middle: not at rest
    EXPECT_FALSE(gcad_port(g, 0, &pad, d.now));
    d.now += 8;
    EXPECT_TRUE(Deliver(Report(0x14, 0, 0, 110, 120)));  // at rest, but
    d.now += 8;
    EXPECT_TRUE(Deliver(Report(0x14, 0, 0, 125, 131)));  // still moving
    EXPECT_FALSE(gcad_port(g, 0, &pad, d.now));
    d.now += 8;
    EXPECT_TRUE(Deliver(Report(0x14, 0, 0, 132, 134)));
    d.now += 8;
    EXPECT_TRUE(Deliver(Report(0x14, 0, 0, 131, 134)));
    EXPECT_FALSE(gcad_port(g, 0, &pad, d.now));  // two steady so far
    d.now += 8;
    EXPECT_TRUE(Deliver(Report(0x14, 0, 0, 132, 135)));
    ExpectPort(g, 0, 0, 0, d.now);
    EXPECT_EQ(OriginEvents(d, 0, GCAD_ORIGIN_RESTING), resting + 1);
    EXPECT_EQ(OriginEvents(d, 0, GCAD_ORIGIN_MIDDLE), middle);  // and not the middle

    // A stick jumping between two readings that are each at rest (100 and
    // 120) never settles: at the end of the wait it takes the middle, not
    // whichever it read last, while the y axis, steady, keeps its reading.
    d.now += 8;
    EXPECT_TRUE(Deliver(Report(0x04, 0, 0, 128, 128)));
    const std::uint32_t appeared = d.now + 8;
    for (int k = 0; d.now < appeared + GCAD_ORIGIN_WAIT_MS; ++k) {
        d.now += 8;
        EXPECT_TRUE(Deliver(Report(0x14, 0, 0, (k & 1) ? 120 : 100, 130)));
    }
    EXPECT_EQ(OriginEvents(d, 0, GCAD_ORIGIN_MIDDLE), middle + 1);
    d.now += 8;
    EXPECT_TRUE(Deliver(Report(0x14, 0, 0, 128, 130)));
    ExpectPort(g, 0, 0, 0, d.now);  // centred: x took the middle, y its own 130
}

static void TestNoAdapter() {
    FakeHid hid;
    hid.adapter = false;
    Driver d;
    Start(hid, d, 5);
    EXPECT_EQ(d.g->dev_id, -1);
    EXPECT_FALSE(hid.locked);  // AttachFinish went out
    EXPECT_EQ(hid.waiting_list.size(), 1u);
    Plug(0x00030009);
    EXPECT_EQ(d.g->link, GCAD_LINK_POLL);
    gcad_stop(d.g, d.now);
    Pump();
    EXPECT_TRUE(gcad_idle(d.g));
}

// A replacement controller must not inherit the unplugged pad's motor request.
static void TestRumbleDisconnect() {
    for (std::uint32_t version : {4u, 5u}) {
        FakeHid hid;
        Driver driver;
        Start(hid, driver, version);
        gcad* g = driver.g;
        EXPECT_TRUE(DeliverSettled(Report(0x14, 0, 0, 128, 128)));
        gcad_rumble(g, 0, 1);
        EXPECT_TRUE(Deliver(Report(0x14, 0, 0, 128, 128)));
        EXPECT_EQ(hid.sent_out.back()[1], 1);

        // The game stops addressing the port once it reads as unplugged.
        EXPECT_TRUE(Deliver(Report(0x04, 0, 0, 128, 128)));
        EXPECT_EQ(hid.sent_out.back()[1], 0);
        EXPECT_TRUE(DeliverSettled(Report(0x14, 0, 0, 128, 128)));
        EXPECT_EQ(hid.sent_out.back()[1], 0);

        // Losing the whole adapter must forget its motors as well.
        gcad_rumble(g, 0, 1);
        EXPECT_TRUE(Deliver(Report(0x14, 0, 0, 128, 128)));
        EXPECT_EQ(hid.sent_out.back()[1], 1);
        Unplug();
        Plug(hid.dev_id + 1);
        EXPECT_TRUE(DeliverSettled(Report(0x14, 0, 0, 128, 128)));
        EXPECT_EQ(hid.sent_out.back()[1], 0);
    }
}

static void TestTransferErrorRelinks() {
    FakeHid hid;
    Driver d;
    Start(hid, d, 5);
    gcad* g = d.g;
    // A poll fails while the adapter stays listed: linked again later.
    Request r = hid.waiting_in.front();
    hid.waiting_in.clear();
    hid.in_flight_transfers = 0;
    hid.replies.push_back({r.tag, -7005});
    Pump();
    EXPECT_EQ(g->link, GCAD_LINK_FAILED);
    d.now += GCAD_RELINK_MS;
    gcad_tick(g, d.now);
    Pump();
    EXPECT_EQ(g->link, GCAD_LINK_POLL);
    EXPECT_EQ(g->links, 2u);
    EXPECT_TRUE(DeliverSettled(Report(0x14, 0, 0, 128, 128)));
    gcad_pad pad{};
    EXPECT_TRUE(gcad_port(g, 0, &pad, d.now));
}

static void TestListErrorRetries() {
    FakeHid hid;
    Driver d;
    hid.version = 5;
    g_hid = &hid;
    g_driver = &d;
    d.g = NewState();
    gcad_init(d.g, &d, 7, 5, 1);
    hid.waiting_list.push_back(Request{});  // someone else's hook: the first list is refused
    gcad_tick(d.g, d.now);
    Pump();
    EXPECT_TRUE(d.g->change_failed);
    hid.waiting_list.clear();
    d.now += GCAD_RESCAN_MS;
    gcad_tick(d.g, d.now);
    Pump();
    EXPECT_EQ(d.g->link, GCAD_LINK_SETTLE);  // listed now, set up after the wait
    d.now += GCAD_SETTLE_MS;
    gcad_tick(d.g, d.now);
    Pump();
    EXPECT_EQ(d.g->link, GCAD_LINK_POLL);
}

// The PAD search (riftwii/symsearch.hpp) on made-up functions shaped like
// the SDK's: no game code is needed or kept here.
struct Asm {
    std::uint32_t base;
    std::vector<std::uint32_t> words;
    std::uint32_t here() const { return base + static_cast<std::uint32_t>(words.size() * 4); }
    void op(std::uint32_t w) { words.push_back(w); }
    void bl(std::uint32_t target) { op(0x48000001u | ((target - here()) & 0x03FFFFFCu)); }
    void prologue() {
        op(0x9421FFB0u);  // stwu r1,-80(r1)
        op(0x7C0802A6u);  // mflr r0
    }
    void blr() { op(0x4E800020u); }
    // The error store PADRead makes for a port it cannot read.
    void pad_error(unsigned err_reg, unsigned status_reg, std::uint32_t memset) {
        op(0x9800000Au | (err_reg << 21) | (status_reg << 16));                    // stb rE,10(rS)
        op(0x7C000378u | (status_reg << 21) | (3u << 16) | (status_reg << 11));  // mr r3,rS
        op(0x38800000u);                                                         // li r4,0
        op(0x38A0000Au);                                                         // li r5,10
        bl(memset);
    }
    void motor_body() {
        op(0x3C608000u);  // lis r3,0x8000
        op(0x880330E3u);  // lbz r0,0x30E3(r3)
        op(0x540006B5u);  // rlwinm. r0,r0,0,26,26
        op(0x64840040u);  // oris r4,r4,0x40
    }
    std::vector<std::uint8_t> bytes() const {
        std::vector<std::uint8_t> out;
        for (std::uint32_t w : words) {
            out.push_back(static_cast<std::uint8_t>(w >> 24));
            out.push_back(static_cast<std::uint8_t>(w >> 16));
            out.push_back(static_cast<std::uint8_t>(w >> 8));
            out.push_back(static_cast<std::uint8_t>(w));
        }
        return out;
    }
};

static void TestPadSearch() {
    constexpr std::uint32_t kMemset = 0x80004000u;
    Asm a{0x80200000u, {}};
    a.op(0x60000000u);
    a.blr();
    const std::uint32_t read = a.here();
    a.prologue();
    for (unsigned k = 0; k < 4; ++k) {
        a.op(0x60000000u);
        a.pad_error(27, 19, kMemset);
    }
    a.blr();
    const std::uint32_t motor = a.here();
    a.prologue();
    a.op(0x60000000u);
    a.motor_body();
    a.blr();
    std::vector<std::uint8_t> code = a.bytes();
    std::vector<riftwii::CodeRange> text = {{a.base, code.data(), code.size()}};
    riftwii::PadSymbols pad;
    std::string error;
    EXPECT_TRUE(riftwii::find_pad_symbols(text, pad, error));
    EXPECT_EQ(pad.read, read);
    EXPECT_EQ(pad.read_sites, 4u);
    EXPECT_EQ(pad.control_motor, motor);

    // A game without the PAD library: nothing, and no error.
    Asm none{0x80200000u, {}};
    none.prologue();
    none.pad_error(27, 19, kMemset);  // one lone site is not PADRead
    none.op(0x3C608000u);             // the flag read without the command
    none.op(0x880330E3u);
    none.blr();
    code = none.bytes();
    text = {{none.base, code.data(), code.size()}};
    EXPECT_TRUE(riftwii::find_pad_symbols(text, pad, error));
    EXPECT_EQ(pad.read, 0u);
    EXPECT_EQ(pad.control_motor, 0u);

    // Sites calling different functions are not PADRead's memsets.
    Asm mixed{0x80200000u, {}};
    mixed.prologue();
    mixed.pad_error(27, 19, kMemset);
    mixed.pad_error(27, 19, kMemset + 0x40);
    mixed.pad_error(27, 19, kMemset);
    mixed.blr();
    code = mixed.bytes();
    text = {{mixed.base, code.data(), code.size()}};
    EXPECT_TRUE(riftwii::find_pad_symbols(text, pad, error));
    EXPECT_EQ(pad.read, 0u);

    // Two functions that both look like PADRead: refused, not guessed.
    Asm twice{0x80200000u, {}};
    for (unsigned f = 0; f < 2; ++f) {
        twice.prologue();
        for (unsigned k = 0; k < 3; ++k) twice.pad_error(27, 19, kMemset);
        twice.blr();
    }
    code = twice.bytes();
    text = {{twice.base, code.data(), code.size()}};
    EXPECT_FALSE(riftwii::find_pad_symbols(text, pad, error));
    EXPECT_TRUE(error.find("two PADRead") != std::string::npos);

    // Sites outside any framed function (a blr comes before a prologue).
    Asm loose{0x80200000u, {}};
    loose.blr();
    for (unsigned k = 0; k < 3; ++k) loose.pad_error(27, 19, kMemset);
    loose.blr();
    code = loose.bytes();
    text = {{loose.base, code.data(), code.size()}};
    EXPECT_TRUE(riftwii::find_pad_symbols(text, pad, error));
    EXPECT_EQ(pad.read, 0u);
}

// The screen-dimming reset: li r0,0, then the store and li r3,1 in the
// order `store_first` gives, then blr. Returns its address.
static std::uint32_t idle_reset_function(Asm& a, std::int16_t sda_offset, bool store_first) {
    const std::uint32_t start = a.here();
    const std::uint32_t store = 0x900D0000u | static_cast<std::uint16_t>(sda_offset);  // stw r0,d(r13)
    a.op(0x38000000u);  // li r0,0
    a.op(store_first ? store : 0x38600001u);
    a.op(store_first ? 0x38600001u : store);
    a.blr();
    return start;
}

static void TestIdleResetSearch() {
    constexpr std::uint32_t kMemset = 0x80004000u;
    std::vector<std::uint8_t> code;
    std::vector<riftwii::CodeRange> text;
    riftwii::PadSymbols pad;
    std::string error;

    // Called from PADRead itself (Mario Kart Wii, Tatsunoko vs. Capcom),
    // next to the video code's other reset, which the Wii Remote side calls.
    Asm inside{0x80200000u, {}};
    inside.blr();
    const std::uint32_t remotes = idle_reset_function(inside, -0x6034, false);
    const std::uint32_t ports = idle_reset_function(inside, -0x6038, false);
    inside.prologue();
    for (unsigned k = 0; k < 3; ++k) inside.pad_error(27, 19, kMemset);
    inside.bl(ports);
    inside.blr();
    code = inside.bytes();
    text = {{inside.base, code.data(), code.size()}};
    EXPECT_TRUE(riftwii::find_pad_symbols(text, pad, error));
    EXPECT_TRUE(pad.read != 0);
    EXPECT_EQ(pad.idle_reset, ports);
    EXPECT_TRUE(pad.idle_reset != remotes);

    // Called from the status helper linked after PADRead (Brawl), with the
    // store before the return value (Kamen Rider), after its Wii Remote twin.
    Asm helper{0x80200000u, {}};
    helper.blr();
    idle_reset_function(helper, -0x69B4, true);
    const std::uint32_t store_first = idle_reset_function(helper, -0x69B8, true);
    helper.prologue();
    for (unsigned k = 0; k < 3; ++k) helper.pad_error(27, 19, kMemset);
    helper.blr();
    helper.prologue();
    helper.op(0x60000000u);
    helper.bl(store_first);
    helper.blr();
    code = helper.bytes();
    text = {{helper.base, code.data(), code.size()}};
    EXPECT_TRUE(riftwii::find_pad_symbols(text, pad, error));
    EXPECT_EQ(pad.idle_reset, store_first);

    // A call past the window is someone else's: none found.
    Asm distant{0x80200000u, {}};
    distant.blr();
    idle_reset_function(distant, -0x6034, false);
    const std::uint32_t distant_reset = idle_reset_function(distant, -0x6038, false);
    const std::uint32_t distant_read = distant.here();
    distant.prologue();
    for (unsigned k = 0; k < 3; ++k) distant.pad_error(27, 19, kMemset);
    distant.blr();
    while (distant.here() < distant_read + 0x1000) distant.op(0x60000000u);
    distant.bl(distant_reset);
    code = distant.bytes();
    text = {{distant.base, code.data(), code.size()}};
    EXPECT_TRUE(riftwii::find_pad_symbols(text, pad, error));
    EXPECT_EQ(pad.read, distant_read);
    EXPECT_EQ(pad.idle_reset, 0u);

    // Two different resets (each after its twin) called near PADRead:
    // unsure, so none.
    Asm two{0x80200000u, {}};
    two.blr();
    idle_reset_function(two, -0x6034, false);
    const std::uint32_t first = idle_reset_function(two, -0x6038, false);
    idle_reset_function(two, -0x5034, false);
    const std::uint32_t second = idle_reset_function(two, -0x5038, false);
    two.prologue();
    for (unsigned k = 0; k < 3; ++k) two.pad_error(27, 19, kMemset);
    two.bl(first);
    two.bl(second);
    two.blr();
    code = two.bytes();
    text = {{two.base, code.data(), code.size()}};
    EXPECT_TRUE(riftwii::find_pad_symbols(text, pad, error));
    EXPECT_EQ(pad.idle_reset, 0u);

    // The right shape but alone, or after a twin for a flag that isn't next
    // to it: some other function, not the video library's pair.
    for (int twin_flag : {0, -0x7000}) {  // 0: no twin before it
        Asm lone{0x80200000u, {}};
        lone.blr();
        if (twin_flag != 0) idle_reset_function(lone, static_cast<std::int16_t>(twin_flag), false);
        const std::uint32_t reset = idle_reset_function(lone, -0x6038, false);
        lone.prologue();
        for (unsigned k = 0; k < 3; ++k) lone.pad_error(27, 19, kMemset);
        lone.bl(reset);
        lone.blr();
        code = lone.bytes();
        text = {{lone.base, code.data(), code.size()}};
        EXPECT_TRUE(riftwii::find_pad_symbols(text, pad, error));
        EXPECT_TRUE(pad.read != 0);
        EXPECT_EQ(pad.idle_reset, 0u);
    }

    // Clearing a flag but returning 0 is not the reset.
    Asm other{0x80200000u, {}};
    other.blr();
    const std::uint32_t returns_zero = other.here();
    other.op(0x38000000u);  // li r0,0
    other.op(0x900D9FC8u);  // stw r0,-24632(r13)
    other.op(0x38600000u);  // li r3,0
    other.blr();
    other.prologue();
    for (unsigned k = 0; k < 3; ++k) other.pad_error(27, 19, kMemset);
    other.bl(returns_zero);
    other.blr();
    code = other.bytes();
    text = {{other.base, code.data(), code.size()}};
    EXPECT_TRUE(riftwii::find_pad_symbols(text, pad, error));
    EXPECT_EQ(pad.idle_reset, 0u);
}

// A GameCube game's PADControlMotor: the command's low two bits ORed with
// 0x40 in the top half, then SISetCommand and SITransferCommands in a row.
static void gc_motor_function(Asm& a, std::uint32_t set_command, std::uint32_t transfer, bool second_call) {
    a.op(0x7C0802A6u);  // mflr r0
    a.op(0x90010004u);  // stw r0,4(r1)
    a.op(0x9421FFD8u);  // stwu r1,-40(r1)
    a.op(0x808D8CE0u);  // lwz r4,-29472(r13): the analog mode
    a.op(0x57C007BEu);  // clrlwi r0,r30,30
    a.op(0x387D0000u);  // addi r3,r29,0
    a.op(0x64840040u);  // oris r4,r4,0x40
    a.op(0x7C840378u);  // or r4,r4,r0
    a.bl(set_command);
    if (second_call) a.bl(transfer);
    else a.op(0x60000000u);
    a.op(0x80010034u);  // lwz r0,52(r1)
    a.op(0x38210028u);  // addi r1,r1,40
    a.op(0x7C0803A6u);  // mtlr r0
    a.blr();
}

static void TestGcPadMotorSearch() {
    constexpr std::uint32_t kSetCommand = 0x80004000u;
    constexpr std::uint32_t kTransfer = 0x80004040u;
    std::string error;
    std::uint32_t found = 0;

    Asm one{0x80200000u, {}};
    one.op(0x60000000u);
    one.blr();
    const std::uint32_t motor = one.here();
    gc_motor_function(one, kSetCommand, kTransfer, true);
    std::vector<std::uint8_t> code = one.bytes();
    std::vector<riftwii::CodeRange> text = {{one.base, code.data(), code.size()}};
    EXPECT_TRUE(riftwii::find_gc_pad_motor(text, found, error));
    EXPECT_EQ(found, motor);

    // The same words with only one call after them: not the motor.
    Asm other{0x80200000u, {}};
    other.blr();
    gc_motor_function(other, kSetCommand, kTransfer, false);
    code = other.bytes();
    text = {{other.base, code.data(), code.size()}};
    EXPECT_TRUE(riftwii::find_gc_pad_motor(text, found, error));
    EXPECT_EQ(found, 0u);
    EXPECT_TRUE(error.empty());

    // An early return inside it (a `blr` before the motor's words): the
    // word after that `blr` is no function's start, so nothing is hooked.
    Asm early{0x80200000u, {}};
    early.blr();
    early.op(0x7C0802A6u);  // mflr r0
    early.op(0x9421FFD8u);  // stwu r1,-40(r1)
    early.op(0x2C030000u);  // cmpwi r3,0
    early.blr();            // (a return the compiler kept in the middle)
    early.op(0x808D8CE0u);  // lwz r4,-29472(r13)
    early.op(0x57C007BEu);  // clrlwi r0,r30,30
    early.op(0x64840040u);  // oris r4,r4,0x40
    early.bl(kSetCommand);
    early.bl(kTransfer);
    early.blr();
    code = early.bytes();
    text = {{early.base, code.data(), code.size()}};
    EXPECT_TRUE(!riftwii::find_gc_pad_motor(text, found, error));
    EXPECT_EQ(found, 0u);

    EXPECT_TRUE(!error.empty());

    // A function that opens with stwu instead of mflr is a function too.
    Asm framed{0x80200000u, {}};
    framed.blr();
    const std::uint32_t framed_start = framed.here();
    framed.op(0x9421FFD8u);  // stwu r1,-40(r1)
    framed.op(0x7C0802A6u);  // mflr r0
    framed.op(0x90010030u);  // stw r0,48(r1)
    framed.op(0x808D8CE0u);  // lwz r4,-29472(r13)
    framed.op(0x57C007BEu);  // clrlwi r0,r30,30
    framed.op(0x64840040u);  // oris r4,r4,0x40
    framed.bl(kSetCommand);
    framed.bl(kTransfer);
    framed.blr();
    code = framed.bytes();
    text = {{framed.base, code.data(), code.size()}};
    EXPECT_TRUE(riftwii::find_gc_pad_motor(text, found, error));
    EXPECT_EQ(found, framed_start);

    // A branch among the four words the replay slot copies (here the
    // fourth) is refused with the reason; the first word alone is not all.
    Asm branchy{0x80200000u, {}};
    branchy.blr();
    branchy.op(0x7C0802A6u);  // mflr r0
    branchy.op(0x90010004u);  // stw r0,4(r1)
    branchy.op(0x9421FFD8u);  // stwu r1,-40(r1)
    branchy.op(0x4182000Cu);  // beq +12
    branchy.op(0x57C007BEu);  // clrlwi r0,r30,30
    branchy.op(0x64840040u);  // oris r4,r4,0x40
    branchy.bl(kSetCommand);
    branchy.bl(kTransfer);
    branchy.blr();
    code = branchy.bytes();
    text = {{branchy.base, code.data(), code.size()}};
    EXPECT_TRUE(!riftwii::find_gc_pad_motor(text, found, error));
    EXPECT_EQ(found, 0u);
    EXPECT_TRUE(error.find("branch") != std::string::npos);

    // GCC's PADControlMotor (Sonic Riders Tournament Edition): its early
    // return's epilogue and blr come before the motor's words, then mflr r0
    // takes the link register mid-function. The start is the stwu after the
    // previous function's blr, not the word after the early blr.
    Asm gcc{0x80200000u, {}};
    gcc.blr();
    const std::uint32_t gcc_start = gcc.here();
    gcc.op(0x9421FFE8u);  // stwu r1,-24(r1)
    gcc.op(0x93E10014u);  // stw r31,20(r1)
    gcc.op(0x3BE00000u);  // li r31,0
    gcc.op(0x7FE9FB78u);  // mr r9,r31
    gcc.op(0x40820010u);  // bne +16 (to the mflr)
    gcc.op(0x83E10014u);  // lwz r31,20(r1)
    gcc.op(0x38210018u);  // addi r1,r1,24
    gcc.blr();            // the early return
    gcc.op(0x7C0802A6u);  // mflr r0
    gcc.op(0x93A1000Cu);  // stw r29,12(r1)
    gcc.op(0x57A407BEu);  // clrlwi r4,r29,30
    gcc.op(0x64840040u);  // oris r4,r4,0x40
    gcc.op(0x60840300u);  // ori r4,r4,0x300
    gcc.op(0x7FC3F378u);  // mr r3,r30
    gcc.bl(kSetCommand);
    gcc.bl(kTransfer);
    gcc.blr();
    code = gcc.bytes();
    text = {{gcc.base, code.data(), code.size()}};
    EXPECT_TRUE(riftwii::find_gc_pad_motor(text, found, error));
    EXPECT_EQ(found, gcc_start);

    // Two such functions: refused, not guessed.
    Asm twice{0x80200000u, {}};
    twice.blr();
    gc_motor_function(twice, kSetCommand, kTransfer, true);
    gc_motor_function(twice, kSetCommand, kTransfer, true);
    code = twice.bytes();
    text = {{twice.base, code.data(), code.size()}};
    EXPECT_FALSE(riftwii::find_gc_pad_motor(text, found, error));
    EXPECT_TRUE(error.find("PADControlMotor") != std::string::npos);
}

// A self-authored SIGetStatus-shaped function with a different register
// allocation and a cache whose signed low half requires a high-half carry.
static void si_status_function(Asm& code, std::uint32_t cache, bool wii) {
    code.prologue();
    code.op(0x3D200000u | (wii ? 0xCD00u : 0xCC00u));  // lis r9,MMIO
    code.op(0x21430003u);  // subfic r10,r3,3
    code.op(0x81696438u);  // lwz r11,0x6438(r9)
    code.op(0x554A1838u);  // slwi r10,r10,3
    code.op(0x7D6B5430u);  // srw r11,r11,r10
    code.op(0x55600739u);  // rlwinm. r0,r11,0,28,28
    code.op(0x41820028u);  // beq past update
    code.op(0x3D800000u | ((cache + 0x8000u) >> 16));  // lis r12,ha(cache)
    code.op(0x5469103Au);  // slwi r9,r3,2
    code.op(0x398C0000u | (cache & 0xFFFFu));  // addi r12,r12,lo(cache)
    code.op(0x7D8C4A14u);  // add r12,r12,r9
    code.op(0x812C0000u);  // lwz r9,0(r12)
    code.op(0x55200631u);  // rlwinm. r0,r9,0,24,24
    code.op(0x4082000Cu);  // bne past store
    code.op(0x39200008u);  // li r9,8
    code.op(0x912C0000u);  // stw r9,0(r12)
    code.blr();
}

static void TestGcSiTypeCacheSearch() {
    constexpr std::uint32_t kCache = 0x8034ABC0u;
    for (const bool wii : {false, true}) {
        Asm code{0x80004000u, {}};
        code.bl(code.base + 12);
        code.bl(code.base + 12);  // repeated calls to one table agree
        code.blr();
        si_status_function(code, kCache, wii);
        const auto find = [&](const Asm& input) {
            const auto bytes = input.bytes();
            return riftwii::find_gc_si_type_cache({{input.base, bytes.data(), bytes.size()}}, input.base);
        };
        EXPECT_EQ(find(code), kCache);
        // Neither a different hardware block, a mismatched channel
        // index, a different error bit nor an incomplete update qualifies.
        for (unsigned index : {2u, 3u, 4u, 5u, 6u, 7u, 8u, 10u, 12u, 13u, 14u, 15u, 16u, 17u}) {
            Asm changed = code;
            changed.words[3 + index] ^= 4u;
            EXPECT_EQ(find(changed), 0u);
        }
        Asm truncated = code;
        truncated.words.resize(truncated.words.size() - 2);
        EXPECT_EQ(find(truncated), 0u);
        Asm unrelated = code;
        unrelated.words[0] = 0x60000000u;
        unrelated.words[1] = 0x60000000u;
        EXPECT_EQ(find(unrelated), 0u);
        Asm invalid = code;
        invalid.words[3 + 7] = 0x3D80CD00u;  // hardware address in place of RAM
        EXPECT_EQ(find(invalid), 0u);
        Asm conflict = code;
        const std::uint32_t second = conflict.here();
        si_status_function(conflict, kCache + 16, wii);
        conflict.words[1] = 0x48000001u | ((second - (conflict.base + 4)) & 0x03FFFFFCu);
        EXPECT_EQ(find(conflict), 0u);
    }
    EXPECT_EQ(riftwii::find_gc_si_type_cache({}, 0), 0u);
}

// A self-authored SIProbe: SIDecodeType(SIGetType(chan)) in a small
// frame, with the prologue and epilogue orders the two compilers use.
static void si_probe_function(Asm& code, std::uint32_t get_type, std::uint32_t decode, bool older) {
    if (older) {
        code.op(0x7C0802A6u);  // mflr r0
        code.op(0x90010004u);  // stw r0,4(r1)
        code.op(0x9421FFF8u);  // stwu r1,-8(r1)
    } else {
        code.op(0x9421FFF0u);  // stwu r1,-16(r1)
        code.op(0x7C0802A6u);  // mflr r0
        code.op(0x90010014u);  // stw r0,20(r1)
    }
    code.bl(get_type);
    code.bl(decode);
    if (older) {
        code.op(0x8001000Cu);  // lwz r0,12(r1)
        code.op(0x38210008u);  // addi r1,r1,8
        code.op(0x7C0803A6u);  // mtlr r0
    } else {
        code.op(0x80010014u);  // lwz r0,20(r1)
        code.op(0x7C0803A6u);  // mtlr r0
        code.op(0x38210010u);  // addi r1,r1,16
    }
    code.blr();
}

static void TestGcSiProbeSearch() {
    for (const bool older : {true, false}) {
        // PADRead calls SIGetType and something else; SIDecodeType returns
        // early several times before it gives the WaveBird's type.
        Asm code{0x80004000u, {}};
        const std::uint32_t pad_read = code.here();
        code.bl(pad_read + 12);
        code.bl(pad_read + 20);
        code.blr();
        const std::uint32_t get_type = code.here();
        code.op(0x38600008u);  // li r3,8
        code.blr();
        code.blr();  // PADRead's other call
        const std::uint32_t decode = code.here();
        code.op(0x38600008u);  // li r3,8
        code.blr();
        code.op(0x3C608B10u);  // lis r3,0x8B10
        code.blr();
        const std::uint32_t no_wavebird = code.here();
        code.op(0x3C600900u);  // lis r3,0x0900
        code.blr();
        const std::uint32_t probe = code.here();
        si_probe_function(code, get_type, decode, older);
        const auto find = [&](const Asm& input, std::uint32_t pad) {
            const auto bytes = input.bytes();
            return riftwii::find_gc_si_probe({{input.base, bytes.data(), bytes.size()}}, pad);
        };
        EXPECT_EQ(find(code, pad_read), probe);
        EXPECT_EQ(find(code, 0), 0u);
        // A wrapper whose first call PADRead does not make, or whose second
        // never gives a WaveBird's type, is not SIProbe.
        Asm not_get_type = code;
        si_probe_function(not_get_type, decode, decode, older);
        EXPECT_EQ(find(not_get_type, pad_read), probe);
        Asm wrong_first = code;
        wrong_first.words.resize((probe - code.base) / 4);
        si_probe_function(wrong_first, no_wavebird, decode, older);
        EXPECT_EQ(find(wrong_first, pad_read), 0u);
        Asm wrong_second = code;
        wrong_second.words.resize((probe - code.base) / 4);
        si_probe_function(wrong_second, get_type, no_wavebird, older);
        EXPECT_EQ(find(wrong_second, pad_read), 0u);
        // Two candidates: nothing is hooked rather than the wrong one.
        Asm twice = code;
        si_probe_function(twice, get_type, decode, !older);
        EXPECT_EQ(find(twice, pad_read), 0u);
        // Anything but lwz r0 after the two calls is not the small shape.
        Asm bigger = code;
        bigger.words[(probe - code.base) / 4 + 5] = 0x93E1000Cu;  // stw r31,12(r1)
        EXPECT_EQ(find(bigger, pad_read), 0u);
    }
}

// Self-authored disabled-port block, with a shifted channel mask and
// register allocation distinct from F-Zero's executable.
static void pad_enabled_block(Asm& code, std::int16_t offset) {
    code.op(0x3D408000u);  // lis r10,0x8000
    code.op(0x7D494C30u);  // srw r9,r10,r9
    code.op(0x80AD0000u | static_cast<std::uint16_t>(offset));  // lwz r5,d(r13)
    code.op(0x7CA54839u);  // and. r5,r5,r9
    code.op(0x40820020u);
    code.op(0x38C0FFFFu);  // li r6,-1
    code.op(0x98DE000Au);  // stb r6,10(r30)
    code.op(0x387E0000u);  // addi r3,r30,0
    code.op(0x38800000u);
    code.op(0x38A0000Au);
    code.bl(code.base + 0x500);
}

static void TestGcPadEnabledSearch() {
    const auto find = [](const Asm& input) {
        const auto bytes = input.bytes();
        return riftwii::find_gc_pad_enabled_offset({{input.base, bytes.data(), bytes.size()}}, input.base);
    };
    for (const std::int16_t offset : {-0x7134, 0x1234}) {
        Asm code{0x80004000u, {}};
        pad_enabled_block(code, offset);
        code.blr();
        EXPECT_EQ(find(code), static_cast<std::int32_t>(offset));
        for (unsigned index = 0; index < 11; ++index) {
            Asm changed = code;
            std::uint32_t changed_field = 4u;
            if (index == 2) changed_field = 0x10000u;
            if (index == 10) changed_field = 1u;
            changed.words[index] ^= changed_field;
            EXPECT_EQ(find(changed), 0);
        }
        Asm truncated = code;
        truncated.words.resize(9);
        EXPECT_EQ(find(truncated), 0);
        Asm conflict = code;
        conflict.words.pop_back();
        pad_enabled_block(conflict, offset + 4);
        conflict.blr();
        EXPECT_EQ(find(conflict), 0);
    }
    for (const std::int16_t offset : {0, 3}) {
        Asm invalid{0x80004000u, {}};
        pad_enabled_block(invalid, offset);
        invalid.blr();
        EXPECT_EQ(find(invalid), 0);
    }
    EXPECT_EQ(riftwii::find_gc_pad_enabled_offset({}, 0), 0);
}

int main() {
    EXPECT_EQ(sizeof(gcad) % 4, 0u);
    EXPECT_EQ(offsetof(gcad, setup_in) % 32, 0u);
    EXPECT_EQ(offsetof(gcad, in_data) % 32, 0u);
    EXPECT_EQ(offsetof(gcad, out_data) % 32, 0u);
    EXPECT_EQ(offsetof(gcad, env) % 32, 0u);
    TestHelpers();
    TestPadMoved();
    TestV5();
    TestV5OwnedElsewhere();
    TestV5AlreadyResumed();
    TestV5SetProtocolRefused();
    TestV5KnownDevice();
    TestV4();
    TestOriginAtRest();
    TestNoAdapter();
    TestRumbleDisconnect();
    TestTransferErrorRelinks();
    TestListErrorRetries();
    TestPadSearch();
    TestIdleResetSearch();
    TestGcPadMotorSearch();
    TestGcSiTypeCacheSearch();
    TestGcSiProbeSearch();
    TestGcPadEnabledSearch();
    if (g_failures == 0) std::cout << "gcad tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
