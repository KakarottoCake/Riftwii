/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Which GameCube port each Wii Remote plays on (gc_bt.c), and which slot
 * a remote gets when it connects: kept apart from the Bluetooth driver so
 * the host tests can check the rules.
 *
 * The rule, so that no controller is ever left out while a port is free:
 *  - A GameCube controller keeps the port it is plugged into: the game
 *    reads it there itself.
 *  - A Wii Remote's own port is its slot's, the one its LED shows when it
 *    connects (the first remote port 1, the second port 2, ...). A remote
 *    that drops out and comes back gets its slot again (gc_bt.c), so it
 *    comes back on the same port.
 *  - A remote whose own port has a GameCube controller in it moves to the
 *    next free port after it (wrapping round to port 1), instead of being
 *    hidden behind that controller. Remotes whose own port is free stay
 *    where they are: a controller plugged in moves only the remote it
 *    lands on.
 *  - With more controllers than the four ports, the remote left without a
 *    free port sits out until one is free. */
#ifndef RIFTWII_GC_PORTS_H
#define RIFTWII_GC_PORTS_H

#include <stdint.h>

#include "gc_bt.h"

#define GC_PORTS 4u
#define GC_PORT_NONE 0xFFu
/* One remote slot per port: a slot's own port is its number. */
typedef char gc_ports_one_slot_each[(GC_BT_REMOTES == GC_PORTS) ? 1 : -1];

/* `pads`: bit p set when a GameCube controller answers in port p.
 * `remotes`: bit s set when the Wii Remote in slot s is ready to play.
 * `port_of[s]`: the port slot s's remote plays on, GC_PORT_NONE when
 * there is no remote there or no free port for it. */
static inline void gc_ports_assign(uint32_t pads, uint32_t remotes, uint8_t* port_of) {
    uint32_t taken = pads & ((1u << GC_PORTS) - 1u);
    uint32_t slot;
    /* First every remote whose own port is free takes it. */
    for (slot = 0; slot < GC_PORTS; ++slot) {
        const uint32_t own = 1u << slot;
        port_of[slot] = (uint8_t)GC_PORT_NONE;
        if (!(remotes & own) || (taken & own)) continue;
        port_of[slot] = (uint8_t)slot;
        taken |= own;
    }
    /* Then each remote a GameCube controller pushed out of its own port
     * takes the next free one after it. */
    for (slot = 0; slot < GC_PORTS; ++slot) {
        uint32_t step;
        if (!(remotes & (1u << slot)) || port_of[slot] != GC_PORT_NONE) continue;
        for (step = 1; step < GC_PORTS; ++step) {
            const uint32_t port = (slot + step) % GC_PORTS;
            if (taken & (1u << port)) continue;
            port_of[slot] = (uint8_t)port;
            taken |= 1u << port;
            break;
        }
    }
}

/* The slot, and so the port, for a remote that connects (`bdaddr`, as
 * the controller gives it): the one it had before when that is free, so a
 * remote that drops out in the game comes back on the same port; else a
 * slot no remote has had yet, so it does not take the place of one that
 * is away; else any free one. GC_PORT_NONE when all four are in use. A
 * slot never used has an address of zeros (the loader zeroes the state),
 * and a remote's address stays in its slot after it leaves. */
static inline uint32_t gc_ports_slot_for(const struct gc_bt_remote* remotes, const uint8_t* bdaddr) {
    uint32_t fresh = GC_PORT_NONE, any = GC_PORT_NONE;
    uint32_t slot, i;
    for (slot = 0; slot < GC_BT_REMOTES; ++slot) {
        const struct gc_bt_remote* r = &remotes[slot];
        uint32_t same = 1, address_bits = 0;
        if (r->used) continue;
        for (i = 0; i < 6; ++i) {
            if (r->bdaddr[i] != bdaddr[i]) same = 0;
            address_bits |= r->bdaddr[i];
        }
        if (same) return slot;
        if (fresh == GC_PORT_NONE && address_bits == 0) fresh = slot;
        if (any == GC_PORT_NONE) any = slot;
    }
    return fresh != GC_PORT_NONE ? fresh : any;
}

#endif
