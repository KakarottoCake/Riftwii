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
    bool resumed = false;
    bool info_read = false;
    bool stall_control = false;
    bool answer_control = true;
    std::deque<Request> queue;          // answered at once when possible
    std::vector<Request> waiting_list;  // device list hooks
    std::vector<Request> waiting_in;    // polls waiting for a report
    std::vector<Request> waiting_control;
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
                if (h.answer_control) h.replies.push_back({r.tag, h.stall_control ? -7003 : 0});
                else h.waiting_control.push_back(r);
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
        if (r.cmd == GCAD_V5_ATTACH) {
            if (h.owned_by_other) result = kEinval;
            else h.ours = true;
        } else if (r.cmd == GCAD_V5_RELEASE) {
            if (!h.ours) result = kEinval;
            h.ours = false;
        } else if (r.cmd == GCAD_V5_SUSPEND_RESUME) {
            const bool want = r.in[11] != 0;
            if (!h.ours || want == h.resumed) result = kEinval;
            else h.resumed = want;
        } else if (r.cmd == GCAD_V5_GET_DEVICE_INFO) {
            if (r.out_len != 0x60 || r.in[8] != 0 || !h.ours || !h.resumed) {
                result = kEinval;
            } else {
                std::memset(r.out, 0, 0x60);
                gcad_put32(r.out, h.dev_id);
                r.out[80 + 2] = 0x81;
                r.out[88 + 2] = 0x02;
                h.info_read = true;
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
            if (h.answer_control) h.replies.push_back({r.tag, h.stall_control ? -7003 : 0});
            else h.waiting_control.push_back(r);
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
    // List, Attach, resume, info, AttachFinish, SET_PROTOCOL, init.
    EXPECT_FALSE(hid.locked);
    EXPECT_TRUE(hid.ours);
    EXPECT_TRUE(hid.resumed);
    EXPECT_TRUE(hid.info_read);
    EXPECT_EQ(g->link, GCAD_LINK_POLL);
    EXPECT_EQ(hid.control_setup.size(), 1u);
    if (!hid.control_setup.empty()) EXPECT_EQ(hid.control_setup[0], 0x210Bu);
    EXPECT_TRUE(!hid.sent_out.empty() && hid.sent_out[0].size() == 1 && hid.sent_out[0][0] == 0x13);
    EXPECT_EQ(hid.waiting_in.size(), 1u);
    EXPECT_EQ(hid.waiting_list.size(), 1u);  // listening for the next change
    EXPECT_FALSE(gcad_port(g, 0, nullptr, d.now));

    // A pad on port 1: the first report is its origin.
    EXPECT_TRUE(Deliver(Report(0x14, 0, 0, 131, 125)));
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
    EXPECT_TRUE(Deliver(Report(0x14, 0, 0, 140, 125)));
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
    EXPECT_TRUE(Deliver(Report(0x14, 0x02, 0, 128, 128)));
    ExpectPort(g, 0, GCAD_PAD_B, 0, d.now);

    // Stop: the list hook and the poll are cancelled, the device released.
    gcad_stop(g, d.now);
    Pump();
    EXPECT_TRUE(gcad_idle(g));
    EXPECT_FALSE(hid.ours);
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
    EXPECT_TRUE(Deliver(Report(0x24, 0, 0, 128, 128)));
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

static void TestControlUnanswered() {
    // SET_PROTOCOL refused: init goes on at once.
    FakeHid hid;
    hid.stall_control = true;
    Driver d;
    Start(hid, d, 5);
    EXPECT_EQ(d.g->link, GCAD_LINK_POLL);
    EXPECT_TRUE(d.g->ctrl_result < 0);
    // Never answered: init after GCAD_CTRL_MS.
    FakeHid hid2;
    hid2.answer_control = false;
    Driver d2;
    Start(hid2, d2, 5);
    EXPECT_EQ(d2.g->link, GCAD_LINK_CTRL);
    d2.now += GCAD_CTRL_MS;
    gcad_tick(d2.g, d2.now);
    Pump();
    EXPECT_EQ(d2.g->link, GCAD_LINK_POLL);
    EXPECT_TRUE(Deliver(Report(0x14, 0, 0, 128, 128)));
    gcad_pad pad{};
    EXPECT_TRUE(gcad_port(d2.g, 0, &pad, d2.now));
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
    EXPECT_EQ(hid.control_setup.size(), 1u);
    EXPECT_TRUE(!hid.sent_out.empty() && hid.sent_out[0][0] == 0x13);
    EXPECT_EQ(hid.waiting_list.size(), 1u);
    EXPECT_TRUE(Deliver(Report(0x14, 0x08, 0, 128, 128)));
    gcad_pad pad{};
    EXPECT_TRUE(gcad_port(g, 0, &pad, d.now));
    EXPECT_EQ(pad.buttons, GCAD_PAD_Y);
    // A second pad on port 3.
    std::vector<std::uint8_t> rep = Report(0x14, 0, 0, 128, 128);
    rep[1 + 2 * 9] = 0x14;
    rep[1 + 2 * 9 + 1] = 0x01;
    rep[1 + 2 * 9 + 3] = 128;
    rep[1 + 2 * 9 + 4] = 128;
    EXPECT_TRUE(Deliver(rep));
    rep[1 + 2 * 9 + 1] = 0x02;
    EXPECT_TRUE(Deliver(rep));
    EXPECT_TRUE(gcad_port(g, 2, &pad, d.now));
    EXPECT_EQ(pad.buttons, GCAD_PAD_B);

    Unplug();
    EXPECT_EQ(g->dev_id, -1);
    Plug(4);
    EXPECT_EQ(g->dev_id, 4);
    EXPECT_EQ(g->link, GCAD_LINK_POLL);
    EXPECT_TRUE(Deliver(Report(0x14, 0x01, 0, 128, 128)));
    EXPECT_TRUE(gcad_port(g, 0, &pad, d.now));

    gcad_stop(g, d.now);
    Pump();
    EXPECT_TRUE(gcad_idle(g));
    EXPECT_TRUE(hid.waiting_list.empty());
    EXPECT_TRUE(hid.waiting_in.empty());
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
    EXPECT_TRUE(Deliver(Report(0x14, 0, 0, 128, 128)));
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

int main() {
    EXPECT_EQ(sizeof(gcad) % 4, 0u);
    EXPECT_EQ(offsetof(gcad, setup_in) % 32, 0u);
    EXPECT_EQ(offsetof(gcad, in_data) % 32, 0u);
    EXPECT_EQ(offsetof(gcad, out_data) % 32, 0u);
    EXPECT_EQ(offsetof(gcad, env) % 32, 0u);
    TestHelpers();
    TestV5();
    TestV5OwnedElsewhere();
    TestV5AlreadyResumed();
    TestControlUnanswered();
    TestV4();
    TestNoAdapter();
    TestTransferErrorRelinks();
    TestListErrorRetries();
    TestPadSearch();
    if (g_failures == 0) std::cout << "gcad tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
