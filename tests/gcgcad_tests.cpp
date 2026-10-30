// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The GameCube controller adapter in a GameCube game (runtime/gc/gc_gcad.c):
// its controllers written into the game's PADStatus[4] on the ports
// gc_ports.h's rule gives them, next to real GameCube controllers, and the
// game's rumble sent to the controller on that port. Also exercises the
// blob's reply routing, late discovery and saved setup records against
// mock IOS requests, without relying on calls to the game's PADRead.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "gc_gcad.h"
#include "gc_rt.h"
#include "riftwii/gccrash.hpp"
#include "riftwii/hidconfig.hpp"
#include <string>
#include <vector>

namespace {

int failures = 0;

#define EXPECT_EQ(a, b)                                                                   \
    do {                                                                                  \
        const unsigned expect_a = static_cast<unsigned>(a);                               \
        const unsigned expect_b = static_cast<unsigned>(b);                               \
        if (expect_a != expect_b) {                                                       \
            std::printf("FAILED: %s == %s (%u, %u) at line %d\n", #a, #b, expect_a, expect_b, \
                        __LINE__);                                                        \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

constexpr std::uint32_t kStatusBytes = 12;  // a PADStatus
constexpr std::uint8_t kNoController = 0xFF;
constexpr std::uint32_t kTicksPerMs = 1000;

std::uint32_t g_now = 5000000;
bool g_mock_ios = false;

void answer(gc_gcad& ad, unsigned slot, int result) {
    auto& request = ad.rq[slot];
    EXPECT_EQ(request.state, GC_GCAD_RQ_SENT);
    request.ipc[1] = static_cast<std::uint32_t>(result);
    const auto physical = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(request.ipc)) & 0x3FFFFFFFu;
    EXPECT_EQ(gc_gcad_on_reply(&ad, physical), 1);
    EXPECT_EQ(request.state, GC_GCAD_RQ_DONE);
    gc_gcad_service(&ad, g_now);
}

void descriptors(gcad& pad, unsigned number, unsigned interface_class) {
    auto* bytes = pad.setup_out;
    std::memset(bytes, 0, sizeof(pad.setup_out));
    bytes[36] = 18;
    bytes[37] = 1;
    bytes[56] = 9;
    bytes[57] = 2;
    bytes[68] = 9;
    bytes[69] = 4;
    bytes[70] = static_cast<std::uint8_t>(number);
    bytes[73] = static_cast<std::uint8_t>(interface_class);
    bytes[80] = 7;
    bytes[81] = 5;
    bytes[82] = 0x84;
    bytes[83] = 3;
    bytes[85] = 64;
}

// Exercise the blob's real request completion and service loop, without
// calling PADRead: the heartbeat can now perform the entire setup.
void test_late_composite_pad() {
    gc_gcad ad{};
    ad.fd = 4;
    ad.version = 5;
    ad.known_dev = -1;
    ad.ticks_per_ms = 1;
    std::string error;
    EXPECT_EQ(riftwii::parse_hid_config("VID=054C\nPID=09CC\nPolltype=1\nA=0,1\n", ad.catalog.maps[0], error), true);
    ad.catalog.count = 1;
    g_mock_ios = true;
    gc_gcad_service(&ad, g_now);
    answer(ad, GCAD_TAG_CHANGE, 0); // reload's first list is empty
    EXPECT_EQ(ad.rq[GCAD_TAG_CHANGE].ipc[3], GCAD_V5_ATTACH_FINISH);
    answer(ad, GCAD_TAG_CHANGE, 0);
    EXPECT_EQ(ad.rq[GCAD_TAG_CHANGE].ipc[3], GCAD_V5_GET_DEVICE_CHANGE);

    const std::uint32_t ids[] = {0x001F0021, 0x011E0021, 0x021D0021, 0x031C0021};
    for (unsigned i = 0; i < 4; ++i) {
        gcad_put32(ad.g.change + 12 * i, ids[i]);
        gcad_put32(ad.g.change + 12 * i + 4, 0x054C09CC);
    }
    answer(ad, GCAD_TAG_CHANGE, 4);
    g_now += 600;
    gc_gcad_service(&ad, g_now);
    for (unsigned i = 0; i < 4; ++i) {
        auto& pad = ad.pads[i];
        const unsigned setup = (i + 1) * GCAD_TAGS + GCAD_TAG_SETUP;
        EXPECT_EQ(pad.dev_id, ids[i]);
        EXPECT_EQ(ad.rq[setup].ipc[3], GCAD_V5_SUSPEND_RESUME);
        answer(ad, setup, 0);
        descriptors(pad, i, i == 3 ? 3 : 1);
        answer(ad, setup, 0);
        gc_gcad_service(&ad, g_now);
    }
    EXPECT_EQ(ad.g.excluded_count, 3);
    for (unsigned i = 0; i < 3; ++i) EXPECT_EQ(ad.pads[i].dev_id, -1);
    EXPECT_EQ(ad.pads[3].link, GCAD_LINK_POLL);
    EXPECT_EQ(ad.pads[3].interface_number, 3);
    EXPECT_EQ(ad.pads[3].endpoint_in, 0x84);
    ad.pads[3].in_data[0] = 1;
    answer(ad, 4 * GCAD_TAGS + GCAD_TAG_IN, 64);
    EXPECT_EQ(ad.pads[3].reports, 1);
    EXPECT_EQ(ad.rq[GCAD_TAG_CHANGE].ipc[3], GCAD_V5_ATTACH_FINISH);
    answer(ad, GCAD_TAG_CHANGE, 0);

    // A second pad gets one of the released slots. The old audio entries
    // remain listed, but are never resumed or announced again.
    gcad_put32(ad.g.change + 48, 0x041B0022);
    gcad_put32(ad.g.change + 52, 0x054C09CC);
    answer(ad, GCAD_TAG_CHANGE, 5);
    g_now += 600;
    gc_gcad_service(&ad, g_now);
    EXPECT_EQ(ad.pads[0].dev_id, 0x041B0022);
    answer(ad, GCAD_TAGS + GCAD_TAG_SETUP, 0);
    descriptors(ad.pads[0], 4, 3);
    answer(ad, GCAD_TAGS + GCAD_TAG_SETUP, 0);
    EXPECT_EQ(ad.pads[0].link, GCAD_LINK_POLL);
    EXPECT_EQ(ad.pads[1].dev_id, -1);
    EXPECT_EQ(ad.pads[2].dev_id, -1);

    unsigned records = 0, excluded = 0, inputs = 0;
    std::vector<std::uint8_t> trace;
    while (const auto* record = gc_gcad_next_diagnostic(&ad)) {
        const auto saved = *record;
        // A skipped or failed SD write leaves the same record pending.
        EXPECT_EQ(gc_gcad_next_diagnostic(&ad)->diagnostic, saved.diagnostic);
        EXPECT_EQ(record->id, 0x054C09CC);
        std::uint32_t words[128]{};
        words[GC_TR_MAGIC] = GC_TRACE_MAGIC;
        words[GC_TR_WHERE] = GC_TRACE_HID;
        words[GC_TR_SEQ] = records + 1;
        EXPECT_EQ(gc_gcad_trace_record(&ad, words), 1);
        const auto offset = trace.size();
        trace.resize(offset + 512);
        for (unsigned i = 0; i < 128; ++i) gcad_put32(trace.data() + offset + 4 * i, words[i]);
        const unsigned step = record->diagnostic & 255;
        if (step == GCAD_HID_INTERFACE) ++excluded;
        if (step == GCAD_HID_INPUT) ++inputs;
        ++records;
        gc_gcad_diagnostic_written(&ad);
    }
    EXPECT_EQ(excluded, 3);
    EXPECT_EQ(inputs, 1);
    EXPECT_EQ(records, 24); // discovery, three rejections, two HID setups, one input
    std::string text;
    EXPECT_EQ(riftwii::describe_gc_usb_pads(trace, text), true);
    EXPECT_EQ(text.find("interface 0 is not a HID interface, left alone") != std::string::npos, true);
    EXPECT_EQ(text.find("interface 1 is not a HID interface, left alone") != std::string::npos, true);
    EXPECT_EQ(text.find("interface 2 is not a HID interface, left alone") != std::string::npos, true);
    EXPECT_EQ(text.find("input arrived (64 bytes)") != std::string::npos, true);
    EXPECT_EQ(text.find("USB device list answered (result 0)") != std::string::npos, true);
    // A noisy device cannot overwrite records still waiting for the SD card.
    for (unsigned i = 0; i < GC_GCAD_DIAGNOSTICS + 3; ++i) {
        gc_gcad_event(&ad, &ad.pads[3], GCAD_EV_HID_DIAG, GCAD_HID_INPUT, static_cast<int>(i));
    }
    EXPECT_EQ(ad.diagnostic_head - ad.diagnostic_tail, GC_GCAD_DIAGNOSTICS);
    EXPECT_EQ(gc_gcad_next_diagnostic(&ad)->result, 0);

    // IOS may have given libogc the only list before manager shutdown.
    // With the saved list, setup must start while GetDeviceChange remains
    // unanswered: leaving the pad plugged in is not a new device change.
    gc_gcad carried{};
    carried.fd = 4;
    carried.version = 5;
    carried.known_dev = -1;
    carried.ticks_per_ms = 1;
    carried.catalog.maps[0] = ad.catalog.maps[0];
    carried.catalog.count = 1;
    carried.catalog.known_count = 1;
    carried.catalog.known[0] = {static_cast<std::int32_t>(ids[3]), 0x054C09CC};
    gc_gcad_service(&carried, g_now);
    answer(carried, GCAD_TAGS + GCAD_TAG_SETUP, 0);
    descriptors(carried.pads[0], 3, 3);
    answer(carried, GCAD_TAGS + GCAD_TAG_SETUP, 0);
    EXPECT_EQ(carried.pads[0].link, GCAD_LINK_POLL);
    EXPECT_EQ(carried.rq[GCAD_TAG_CHANGE].state, GC_GCAD_RQ_SENT);
    EXPECT_EQ(carried.rq[GCAD_TAG_CHANGE].ipc[3], GCAD_V5_GET_DEVICE_CHANGE);
    g_mock_ios = false;
}

// PADStatus[4] as PADRead leaves it with no controller in any port.
struct Status {
    std::uint8_t bytes[4 * kStatusBytes];
    Status() {
        std::memset(bytes, 0, sizeof(bytes));
        for (int port = 0; port < 4; ++port) err(port) = kNoController;
    }
    std::uint8_t& err(int port) { return bytes[port * kStatusBytes + 10]; }
    std::uint16_t buttons(int port) const {
        return static_cast<std::uint16_t>(bytes[port * kStatusBytes] << 8 | bytes[port * kStatusBytes + 1]);
    }
    std::int8_t stick_x(int port) const { return static_cast<std::int8_t>(bytes[port * kStatusBytes + 2]); }
};

// The adapter with reports flowing: a controller in each adapter port of
// `ports` (bit per port), wired, pressing A, its stick at origin + 20.
void set_up(gc_gcad& ad, std::uint32_t ports, bool wireless = false) {
    std::memset(&ad, 0, sizeof(ad));
    ad.inited = 1;
    ad.rumble = 1;
    ad.g.link = GCAD_LINK_POLL;
    ad.g.ticks_per_ms = kTicksPerMs;
    ad.g.data_time = g_now;
    ad.g.dev_id = 1;
    for (std::uint32_t i = 0; i < GCAD_PORTS; ++i) {
        ad.port_of[i] = GC_PORT_NONE;
        if (!(ports & (1u << i))) continue;
        std::uint8_t* raw = ad.g.raw[i];
        raw[0] = wireless ? 0x20 : 0x10;  // wired, or a WaveBird receiver
        raw[1] = 0x01;                    // A
        raw[3] = 128 + 20;                // stick x
        raw[4] = 128;
        raw[5] = 128;
        raw[6] = 128;
        const std::uint8_t origin[6] = {128, 128, 128, 128, 0, 0};
        std::memcpy(ad.g.origin[i], origin, sizeof(origin));
        ad.g.origin_valid[i] = 1;
    }
}

// Two adapter controllers, nothing in the Wii's ports: each plays on its
// adapter port's number.
void test_own_ports() {
    gc_gcad ad;
    set_up(ad, 0x5u);  // adapter ports 1 and 3
    Status status;
    gc_gcad_fill(&ad, status.bytes, 0);
    EXPECT_EQ(ad.filled, 0x5u);
    EXPECT_EQ(status.err(0), 0);
    EXPECT_EQ(status.err(2), 0);
    EXPECT_EQ(status.err(1), kNoController);
    EXPECT_EQ(status.err(3), kNoController);
    EXPECT_EQ(status.buttons(0), GCAD_PAD_A);
    EXPECT_EQ(status.buttons(2), GCAD_PAD_A);
    EXPECT_EQ(status.stick_x(0), 20);
    EXPECT_EQ(ad.port_of[0], 0);
    EXPECT_EQ(ad.port_of[2], 2);
}

// A real GameCube controller in the Wii's port 1 keeps it: the adapter's
// port-1 controller moves to the next free port, and port 1 is left as
// PADRead filled it.
void test_real_pad_keeps_its_port() {
    gc_gcad ad;
    set_up(ad, 0x1u);
    Status status;
    status.err(0) = 0;
    status.bytes[1] = 0x10;  // the real controller presses Z
    gc_gcad_fill(&ad, status.bytes, 0);
    EXPECT_EQ(ad.port_of[0], 1);
    EXPECT_EQ(ad.filled, 0x2u);
    EXPECT_EQ(status.bytes[1], 0x10);
    EXPECT_EQ(status.buttons(1), GCAD_PAD_A);
    EXPECT_EQ(status.err(1), 0);
}

// A disabled native controller is still identified by SI: neither the
// adapter nor the remotes may hide the err -1 its game needs to reset it.
void test_disabled_native_pad_keeps_its_port() {
    for (unsigned port = 0; port < GC_PORTS; ++port) {
        gc_gcad ad;
        set_up(ad, 0xFu);
        Status status;
        std::uint8_t original[kStatusBytes];
        std::memcpy(original, status.bytes + port * kStatusBytes, sizeof(original));
        const std::uint32_t native = 1u << port;
        gc_gcad_fill(&ad, status.bytes, native);
        EXPECT_EQ(std::memcmp(original, status.bytes + port * kStatusBytes, sizeof(original)), 0);
        EXPECT_EQ(ad.filled, 0xFu & ~native);
        EXPECT_EQ(ad.port_of[port], GC_PORT_NONE);
        // The next fill sees the adapter's three controllers too, leaving
        // no place for a Wii Remote while the native pad resets.
        std::uint8_t remote_ports[GC_PORTS];
        gc_ports_assign(gc_ports_taken(status.bytes, native), 1u, remote_ports);
        EXPECT_EQ(remote_ports[0], GC_PORT_NONE);
    }
}

// Four real controllers: no port left, nothing written.
void test_all_ports_taken() {
    gc_gcad ad;
    set_up(ad, 0xFu);
    Status status;
    for (int port = 0; port < 4; ++port) status.err(port) = 0;
    gc_gcad_fill(&ad, status.bytes, 0);
    EXPECT_EQ(ad.filled, 0u);
    for (int port = 0; port < 4; ++port) EXPECT_EQ(status.buttons(port), 0);
}

// No report for longer than the driver's timeout: its controllers are gone
// and their ports are the game's "no controller" again.
void test_stale_reports() {
    gc_gcad ad;
    set_up(ad, 0x1u);
    ad.g.data_time = g_now - 10000u * kTicksPerMs;
    Status status;
    gc_gcad_fill(&ad, status.bytes, 0);
    EXPECT_EQ(ad.filled, 0u);
    EXPECT_EQ(status.err(0), kNoController);
}

// Not set up yet (the first poll does it): the ports are left alone.
void test_not_inited() {
    gc_gcad ad;
    set_up(ad, 0x1u);
    ad.inited = 0;
    Status status;
    gc_gcad_fill(&ad, status.bytes, 0);
    EXPECT_EQ(status.err(0), kNoController);
}

// PADControlMotor reaches the controller on that game port, through the
// port it was moved to; Settings' rumble off and a WaveBird stay still.
void test_motor() {
    gc_gcad ad;
    set_up(ad, 0x1u);
    Status status;
    status.err(0) = 0;  // a real controller: the adapter's moves to port 2
    gc_gcad_fill(&ad, status.bytes, 0);
    gc_gcad_motor(&ad, 0, 1);  // port 1 is the real controller's
    EXPECT_EQ(ad.g.rumble_want[0], 0);
    gc_gcad_motor(&ad, 1, 1);
    EXPECT_EQ(ad.g.rumble_want[0], 1);
    gc_gcad_motor(&ad, 1, 2);  // stop hard
    EXPECT_EQ(ad.g.rumble_want[0], 0);
    ad.rumble = 0;
    gc_gcad_motor(&ad, 1, 1);
    EXPECT_EQ(ad.g.rumble_want[0], 0);

    gc_gcad wavebird;
    set_up(wavebird, 0x1u, true);
    Status free_ports;
    gc_gcad_fill(&wavebird, free_ports.bytes, 0);
    gc_gcad_motor(&wavebird, 0, 1);
    EXPECT_EQ(wavebird.g.rumble_want[0], 0);
}

void test_hid_after_adapter() {
    gc_gcad ad;
    set_up(ad, 0x1u);
    ad.g.peers = ad.pads;
    ad.catalog.maps[0].vid = 0x1234;
    ad.catalog.maps[0].pid = 0x5678;
    ad.catalog.maps[0].rumble_bytes = 2;
    gcad& hid = ad.pads[0];
    hid.mapping = &ad.catalog.maps[0];
    hid.link = GCAD_LINK_POLL;
    hid.data_time = g_now;
    hid.ticks_per_ms = kTicksPerMs;
    hid.hid_pad.buttons = GCAD_PAD_B;
    Status status;
    // An SI-identified native pad reserves port 1 even before PADRead's
    // status becomes ready. Adapter moves to port 2; HID takes port 3.
    gc_gcad_fill(&ad, status.bytes, 1);
    EXPECT_EQ(status.err(0), kNoController);
    EXPECT_EQ(status.buttons(1), GCAD_PAD_A);
    EXPECT_EQ(status.buttons(2), GCAD_PAD_B);
    EXPECT_EQ(ad.filled, 6u);
    gc_gcad_motor(&ad, 2, 1);
    EXPECT_EQ(hid.rumble_want[0], 1);
    gc_gcad_motor(&ad, 1, 1);
    EXPECT_EQ(ad.g.rumble_want[0], 1);
    ad.rumble = 0;
    gc_gcad_motor(&ad, 2, 1);
    EXPECT_EQ(hid.rumble_want[0], 0);
    hid.stale = 1;
    Status unplugged;
    gc_gcad_fill(&ad, unplugged.bytes, 0);
    EXPECT_EQ(ad.filled, 1u);
}

// Relocating adapter pad 0 must leave adapter pad 1 on its own free port.
// HID pads wait until both adapter pads have a place, including for rumble.
void test_adapter_own_port_before_displaced_and_hid() {
    gc_gcad ad;
    set_up(ad, 0x3u);
    ad.g.raw[1][1] = 0x02;  // adapter pad 1 presses B
    ad.g.peers = ad.pads;
    ad.catalog.maps[0].vid = 0x1234;
    ad.catalog.maps[0].pid = 0x5678;
    ad.catalog.maps[0].rumble_bytes = 2;
    gcad& hid = ad.pads[0];
    hid.mapping = &ad.catalog.maps[0];
    hid.link = GCAD_LINK_POLL;
    hid.data_time = g_now;
    hid.ticks_per_ms = kTicksPerMs;
    hid.announced_port = 255;
    hid.hid_pad.buttons = GCAD_PAD_X;
    Status status;
    status.err(0) = 0;
    status.bytes[1] = 0x10;  // native pad presses Z

    gc_gcad_fill(&ad, status.bytes, 1u);
    EXPECT_EQ(ad.filled, 0xEu);
    EXPECT_EQ(status.buttons(0), GCAD_PAD_Z);
    EXPECT_EQ(status.buttons(1), GCAD_PAD_B);
    EXPECT_EQ(status.buttons(2), GCAD_PAD_A);
    EXPECT_EQ(status.buttons(3), GCAD_PAD_X);
    EXPECT_EQ(ad.g.port_source[0], 255);
    EXPECT_EQ(ad.g.port_source[1], 1);
    EXPECT_EQ(ad.g.port_source[2], 0);
    EXPECT_EQ(ad.g.port_source[3], GCAD_PORTS);
    EXPECT_EQ(hid.announced_port, 3);

    gc_gcad_motor(&ad, 1, 1);
    EXPECT_EQ(ad.g.rumble_want[1], 1);
    EXPECT_EQ(ad.g.rumble_want[0], 0);
    gc_gcad_motor(&ad, 2, 1);
    EXPECT_EQ(ad.g.rumble_want[0], 1);
    gc_gcad_motor(&ad, 3, 1);
    EXPECT_EQ(hid.rumble_want[0], 1);
}

}  // namespace

extern "C" std::uint32_t gc_gcad_host_now(void) { return g_now; }

// The blob service tests queue requests in its real request slots. Port
// tests set state directly and must never send anything to IOS.
std::int32_t gcad_env_ioctl(gcad* g, std::uint32_t tag, std::uint32_t command, void*, std::uint32_t, void*, std::uint32_t) {
    if (g_mock_ios) {
        auto& ad = *static_cast<gc_gcad*>(g->env_ctx);
        auto& request = ad.rq[GCAD_REQUEST_SLOT(tag)];
        EXPECT_EQ(request.state, GC_GCAD_RQ_IDLE);
        request.tag = tag;
        request.ipc[3] = command;
        request.state = GC_GCAD_RQ_SENT;
        return 0;
    }
    ++failures;
    return -1;
}
std::int32_t gcad_env_ioctlv(gcad* g, std::uint32_t tag, std::uint32_t command, void* message, std::uint32_t length, void* data, std::uint32_t bytes, int) {
    return gcad_env_ioctl(g, tag, command, message, length, data, bytes);
}
std::uint32_t gcad_env_address(gcad*, void* p) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)); }
void gcad_env_flush(gcad*, void*, std::uint32_t) {}
void gcad_env_invalidate(gcad*, void*, std::uint32_t) {}
void gcad_env_event(gcad* g, std::uint32_t code, std::uint32_t a, std::int32_t b) {
    if (g_mock_ios) gc_gcad_event(static_cast<gc_gcad*>(g->env_ctx), g, code, a, b);
}

int main() {
    test_late_composite_pad();
    test_disabled_native_pad_keeps_its_port();
    test_own_ports();
    test_real_pad_keeps_its_port();
    test_all_ports_taken();
    test_stale_reports();
    test_not_inited();
    test_motor();
    test_hid_after_adapter();
    test_adapter_own_port_before_displaced_and_hid();
    if (failures == 0) std::printf("gcgcad: all passed\n");
    return failures == 0 ? 0 : 1;
}
