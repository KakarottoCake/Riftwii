// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The GameCube blob's port rule (runtime/gc/gc_ports.h): which port each
// Wii Remote plays on next to the GameCube controllers, and which slot a
// remote gets when it connects again.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "gc_ports.h"

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

constexpr std::uint32_t kPort1 = 1u << 0;
constexpr std::uint32_t kPort2 = 1u << 1;
constexpr std::uint32_t kPort4 = 1u << 3;
constexpr std::uint32_t kAllPorts = 0xFu;
constexpr std::uint32_t kSlot1 = 1u << 0;
constexpr std::uint32_t kSlot2 = 1u << 1;
constexpr std::uint32_t kSlot4 = 1u << 3;

unsigned count_bits(std::uint32_t bits) {
    unsigned count = 0;
    for (unsigned i = 0; i < GC_PORTS; ++i) count += (bits >> i) & 1u;
    return count;
}

struct Ports {
    std::uint8_t of[GC_PORTS];
};

Ports assign(std::uint32_t pads, std::uint32_t remotes) {
    Ports ports;
    std::memset(ports.of, 0x5A, sizeof(ports.of));  // the rule must write every slot
    gc_ports_assign(pads, remotes, ports.of);
    return ports;
}

// One Wii Remote and nothing in the GameCube ports: port 1, as before.
void test_remote_only() {
    const Ports ports = assign(0, kSlot1);
    EXPECT_EQ(ports.of[0], 0);
    EXPECT_EQ(ports.of[1], GC_PORT_NONE);
    EXPECT_EQ(ports.of[2], GC_PORT_NONE);
    EXPECT_EQ(ports.of[3], GC_PORT_NONE);
}

// A GameCube controller alone: no remote is placed anywhere.
void test_gamecube_only() {
    for (std::uint32_t pads = kPort1; pads <= kAllPorts; ++pads) {
        const Ports ports = assign(pads, 0);
        for (unsigned slot = 0; slot < GC_PORTS; ++slot) EXPECT_EQ(ports.of[slot], GC_PORT_NONE);
    }
}

// The bug: a GameCube controller in port 1 hid the remote that had it.
// The remote now plays on port 2 and the controller keeps port 1.
void test_remote_and_gamecube_in_port_1() {
    const Ports ports = assign(kPort1, kSlot1);
    EXPECT_EQ(ports.of[0], 1);
}

// Two remotes (ports 1 and 2) and a GameCube controller in port 2: the
// first remote stays on port 1, the second moves to port 3.
void test_two_remotes_and_gamecube_in_port_2() {
    const Ports ports = assign(kPort2, kSlot1 | kSlot2);
    EXPECT_EQ(ports.of[0], 0);
    EXPECT_EQ(ports.of[1], 2);
}

// A controller plugged in moves only the remote it lands on: with remotes
// on ports 1 and 2, one in port 1 moves the first remote, past the second,
// to port 3.
void test_only_the_displaced_remote_moves() {
    const Ports ports = assign(kPort1, kSlot1 | kSlot2);
    EXPECT_EQ(ports.of[0], 2);
    EXPECT_EQ(ports.of[1], 1);
}

// The search wraps: the fourth remote pushed out of port 4 goes to the
// first free port from port 1.
void test_wraps_round() {
    const Ports ports = assign(kPort4, kSlot4 | kSlot2);
    EXPECT_EQ(ports.of[3], 0);
    EXPECT_EQ(ports.of[1], 1);
}

// Every port and remote: no two controllers ever share a port, a GameCube
// controller's port never gets a remote, and a remote sits out only when
// all four ports are taken.
void test_never_shared_never_dropped() {
    for (std::uint32_t pads = 0; pads <= kAllPorts; ++pads) {
        for (std::uint32_t remotes = 0; remotes <= kAllPorts; ++remotes) {
            const Ports ports = assign(pads, remotes);
            std::uint32_t used = pads;
            unsigned placed = 0;
            for (unsigned slot = 0; slot < GC_PORTS; ++slot) {
                const unsigned port = ports.of[slot];
                if (!((remotes >> slot) & 1u)) {
                    EXPECT_EQ(port, GC_PORT_NONE);
                    continue;
                }
                if (port == GC_PORT_NONE) continue;
                EXPECT_EQ(port < GC_PORTS, 1);
                if (port >= GC_PORTS) continue;
                EXPECT_EQ((used >> port) & 1u, 0);
                used |= 1u << port;
                ++placed;
                // A remote whose own port is free keeps it.
                if (!((pads >> slot) & 1u)) EXPECT_EQ(port, slot);
            }
            const unsigned remote_count = count_bits(remotes);
            const unsigned free_ports = GC_PORTS - count_bits(pads);
            EXPECT_EQ(placed, remote_count < free_ports ? remote_count : free_ports);
        }
    }
}

void set_address(gc_bt_remote& r, std::uint8_t last) {
    const std::uint8_t address[6] = {last, 0x22, 0x33, 0x44, 0x55, 0x66};
    std::memcpy(r.bdaddr, address, sizeof(address));
}

// A remote that drops out in the game and connects again gets its slot,
// and so its port, back: matched by its Bluetooth address, even when an
// earlier slot is free.
void test_remote_reconnect() {
    gc_bt_remote remotes[GC_BT_REMOTES];
    std::memset(remotes, 0, sizeof(remotes));  // as the loader leaves it: slots 3 and 4 never used
    set_address(remotes[0], 0xA1);
    set_address(remotes[1], 0xB2);
    std::uint8_t first[6], second[6], third[6];
    std::memcpy(first, remotes[0].bdaddr, 6);
    std::memcpy(second, remotes[1].bdaddr, 6);
    std::memcpy(third, first, 6);
    third[0] = 0xC3;
    remotes[0].used = 1;
    remotes[1].used = 0;  // the second remote has left; its address stays

    // A remote never seen before does not take the second one's slot.
    EXPECT_EQ(gc_ports_slot_for(remotes, third), 2);

    // The second remote comes back: slot 2, port 2 again.
    EXPECT_EQ(gc_ports_slot_for(remotes, second), 1);

    // The first one leaves too, then reconnects: slot 1 though slot 2 is
    // free and earlier slots would otherwise win.
    remotes[0].used = 0;
    EXPECT_EQ(gc_ports_slot_for(remotes, first), 0);

    // With slots 1-3 used (the third remote took slot 3) the second remote
    // still gets its own slot when that is free.
    remotes[0].used = 1;
    remotes[2].used = 1;
    std::memcpy(remotes[2].bdaddr, third, 6);
    EXPECT_EQ(gc_ports_slot_for(remotes, second), 1);
}

// The first remote of all goes to slot 1; when every slot is in use a
// new one waits; when the never-used slots are gone a newcomer takes a
// free slot an absent remote had.
void test_slot_for_new_remotes() {
    gc_bt_remote remotes[GC_BT_REMOTES];
    std::memset(remotes, 0, sizeof(remotes));
    std::uint8_t address[6] = {0x01, 2, 3, 4, 5, 6};
    EXPECT_EQ(gc_ports_slot_for(remotes, address), 0);

    for (unsigned slot = 0; slot < GC_BT_REMOTES; ++slot) {
        set_address(remotes[slot], static_cast<std::uint8_t>(0x10 + slot));
        remotes[slot].used = 1;
    }
    EXPECT_EQ(gc_ports_slot_for(remotes, address), GC_PORT_NONE);

    remotes[2].used = 0;  // its remote left; every slot has had one
    EXPECT_EQ(gc_ports_slot_for(remotes, address), 2);
}

}  // namespace

int main() {
    test_remote_only();
    test_gamecube_only();
    test_remote_and_gamecube_in_port_1();
    test_two_remotes_and_gamecube_in_port_2();
    test_only_the_displaced_remote_moves();
    test_wraps_round();
    test_never_shared_never_dropped();
    test_remote_reconnect();
    test_slot_for_new_remotes();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("gcports: all passed\n");
    return 0;
}
