// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The GameCube controller adapter in a GameCube game (runtime/gc/gc_gcad.c):
// its controllers written into the game's PADStatus[4] on the ports
// gc_ports.h's rule gives them, next to real GameCube controllers, and the
// game's rumble sent to the controller on that port. The driver's state is
// set as reports flowing would leave it (runtime/rtgcad.c is tested on its
// own in gcad_tests).
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "gc_gcad.h"

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
    gc_gcad_fill(&ad, status.bytes);
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
    gc_gcad_fill(&ad, status.bytes);
    EXPECT_EQ(ad.port_of[0], 1);
    EXPECT_EQ(ad.filled, 0x2u);
    EXPECT_EQ(status.bytes[1], 0x10);
    EXPECT_EQ(status.buttons(1), GCAD_PAD_A);
    EXPECT_EQ(status.err(1), 0);
}

// Four real controllers: no port left, nothing written.
void test_all_ports_taken() {
    gc_gcad ad;
    set_up(ad, 0xFu);
    Status status;
    for (int port = 0; port < 4; ++port) status.err(port) = 0;
    gc_gcad_fill(&ad, status.bytes);
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
    gc_gcad_fill(&ad, status.bytes);
    EXPECT_EQ(ad.filled, 0u);
    EXPECT_EQ(status.err(0), kNoController);
}

// Not set up yet (the first poll does it): the ports are left alone.
void test_not_inited() {
    gc_gcad ad;
    set_up(ad, 0x1u);
    ad.inited = 0;
    Status status;
    gc_gcad_fill(&ad, status.bytes);
    EXPECT_EQ(status.err(0), kNoController);
}

// PADControlMotor reaches the controller on that game port, through the
// port it was moved to; Settings' rumble off and a WaveBird stay still.
void test_motor() {
    gc_gcad ad;
    set_up(ad, 0x1u);
    Status status;
    status.err(0) = 0;  // a real controller: the adapter's moves to port 2
    gc_gcad_fill(&ad, status.bytes);
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
    gc_gcad_fill(&wavebird, free_ports.bytes);
    gc_gcad_motor(&wavebird, 0, 1);
    EXPECT_EQ(wavebird.g.rumble_want[0], 0);
}

}  // namespace

extern "C" std::uint32_t gc_gcad_host_now(void) { return g_now; }

// The driver's environment, which only its requests reach: none here (the
// state is set as reports flowing leave it). Any request is refused.
std::int32_t gcad_env_ioctl(gcad*, std::uint32_t, std::uint32_t, void*, std::uint32_t, void*, std::uint32_t) {
    ++failures;
    return -1;
}
std::int32_t gcad_env_ioctlv(gcad*, std::uint32_t, std::uint32_t, void*, std::uint32_t, void*, std::uint32_t, int) {
    ++failures;
    return -1;
}
std::uint32_t gcad_env_address(gcad*, void* p) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)); }
void gcad_env_flush(gcad*, void*, std::uint32_t) {}
void gcad_env_invalidate(gcad*, void*, std::uint32_t) {}
void gcad_env_event(gcad*, std::uint32_t, std::uint32_t, std::int32_t) {}

int main() {
    test_own_ports();
    test_real_pad_keeps_its_port();
    test_all_ports_taken();
    test_stale_reports();
    test_not_inited();
    test_motor();
    if (failures == 0) std::printf("gcgcad: all passed\n");
    return failures == 0 ? 0 : 1;
}
