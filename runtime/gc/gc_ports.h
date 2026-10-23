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
 *    free port sits out until one is free.
 *  - A game that asks SIProbe what is in a port hears "a GameCube
 *    controller" for a port a remote or the adapter plays on, as long as
 *    no device of its own answers there (gc_ports_probe). */
#ifndef RIFTWII_GC_PORTS_H
#define RIFTWII_GC_PORTS_H

#include <stdint.h>

#include "gc_bt.h"

#define GC_PORTS 4u
#define GC_PORT_NONE 0xFFu
/* One remote slot per port: a slot's own port is its number. */
typedef char gc_ports_one_slot_each[(GC_BT_REMOTES == GC_PORTS) ? 1 : -1];

/* Controllers currently identified by the SDK's four SI type words.
 * Wireless flags do not change the controller class; errors and BUSY do.
 * SIGetStatus can replace even a disabled port's type with NOREP, so this
 * classification alone does not preserve ownership across PADSetAnalogMode. */
static inline uint32_t gc_ports_native(const volatile uint32_t* types) {
    uint32_t pads = 0, port;
    if (types == 0) return 0;
    for (port = 0; port < GC_PORTS; ++port) {
        const uint32_t type = types[port];
        if ((type & 0x190000FFu) == 0x09000000u) pads |= 1u << port;
    }
    return pads;
}

/* The SDK uses the high four bits for EnabledBits. The remaining bits
 * are zero, so this sentinel means its layout was not recognised. */
#define GC_PORTS_ENABLED_UNKNOWN 0xFFFFFFFFu

/* Remember an identified controller through a disabled/resetting interval.
 * Only NOREP returned for a port that was enabled BEFORE PADRead releases
 * it: PADRead itself disables an unplugged port before returning. Requiring
 * its original err -1 also preserves a wireless receiver's waiting state.
 * A port never identified as a controller remains available to a remote.
 * Unsupported SDK layouts retain the current-type rule without guessing. */
static inline uint32_t gc_ports_native_update(const volatile uint32_t* types, const uint8_t* status,
                                               uint32_t enabled_before, uint32_t previous) {
    uint32_t port;
    if (types == 0 || enabled_before == GC_PORTS_ENABLED_UNKNOWN) return gc_ports_native(types);
    for (port = 0; port < GC_PORTS; ++port) {
        const uint32_t type = types[port];
        const uint32_t bit = 1u << port;
        if ((type & 0x190000FFu) == 0x09000000u) {
            previous |= bit;
        } else if (type == 8u && (enabled_before & (0x80000000u >> port)) != 0 &&
                   status != 0 && status[12 * port + 10] == 0xFFu) {
            previous &= ~bit;
        }
    }
    return previous & 0xFu;
}

/* The ports a Wii Remote or the adapter must leave alone: `native`, plus
 * every port whose err in the game's PADStatus[4] (`status`) is anything
 * but -1, as a controller still starting or a transfer error is a
 * controller there too. */
static inline uint32_t gc_ports_taken(const uint8_t* status, uint32_t native) {
    uint32_t port;
    for (port = 0; port < GC_PORTS; ++port)
        if (status[12 * port + 10] != 0xFFu) native |= 1u << port;
    return native;
}

/* The ports a Wii Remote or the adapter played on in the last PADRead:
 * the game's own read found no controller there (err -1 in `raw_errs`)
 * and the fill left a controller's err 0 (`filled_errs`). Both are a byte
 * a port, port 1 in the top byte, as gc_rt.c's gc_pad_errors packs them.
 * The test presses (GC_FLAG_PRESS) come after the fill, so they never
 * count here. */
static inline uint32_t gc_ports_played(uint32_t raw_errs, uint32_t filled_errs) {
    uint32_t port, played = 0;
    for (port = 0; port < GC_PORTS; ++port) {
        const uint32_t shift = 24u - 8u * port;
        const uint32_t raw = (raw_errs >> shift) & 0xFFu;
        const uint32_t filled = (filled_errs >> shift) & 0xFFu;
        if (raw == 0xFFu && filled == 0u) played |= 1u << port;
    }
    return played;
}

/* SIProbe's answer for a GameCube controller (the SDK's SI_GC_CONTROLLER). */
#define GC_PORTS_SI_CONTROLLER 0x09000000u

/* What the game's SIProbe(chan) answers, from the SDK's own answer
 * `probed` (gc_hook_si_probe). Some games ask SIProbe which device is in
 * a port and only read a port's PADStatus when it is a controller:
 * Mario Kart: Double Dash!!'s JUTGamePad::read takes a port only when
 * SIProbe says a controller or a WaveBird. A Wii U has no GameCube ports,
 * and a Wii's empty port has nothing in it either, so SIProbe answers "no
 * response" there and the Wii Remote's port is skipped although its
 * PADStatus has err 0. A port in `played` (gc_ports_played) answers as a
 * controller instead, but only while nothing answers there: any device
 * the SDK identified (a controller plugged in later, a GBA, a keyboard)
 * keeps its own answer. The low byte of an answer holds SIDecodeType's
 * errors (no response, busy, unknown); a device's type has it 0. */
static inline uint32_t gc_ports_probe(uint32_t chan, uint32_t probed, uint32_t played) {
    const int nothing_answers = (probed & 0xFFu) != 0;
    if (chan >= GC_PORTS || (played & (1u << chan)) == 0 || !nothing_answers) return probed;
    return GC_PORTS_SI_CONTROLLER;
}

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
