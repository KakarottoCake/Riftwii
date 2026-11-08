/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef RIFTWII_RTHID_H
#define RIFTWII_RTHID_H

#include <stdint.h>

#define RTHID_VERSION 1u
#define RTHID_REPORT_MAX 128u
#define RTHID_RUMBLE_MAX 128u
#define RTHID_CONFIG_MAX 16u
#define RTHID_DEVICE_MAX 32u
#define RTHID_ABSENT 0xFFFFu
#define RTHID_BUTTONS 18u

/* Fixed-width data only: the loader compiles controller.ini into this
 * table before handing it to either game runtime. No pointers or strings
 * cross that boundary. Offsets are bytes, masks are Nintendont's hex masks. */
enum rthid_button {
    RTHID_A, RTHID_B, RTHID_X, RTHID_Y, RTHID_Z, RTHID_L, RTHID_R,
    RTHID_START, RTHID_POWER, RTHID_ZL, RTHID_LEFT, RTHID_DOWN,
    RTHID_RIGHT, RTHID_UP, RTHID_RIGHT_UP, RTHID_DOWN_RIGHT,
    RTHID_DOWN_LEFT, RTHID_UP_LEFT
};

typedef struct rthid_button_map {
    uint16_t offset;
    uint16_t mask;
} rthid_button_map;

typedef struct rthid_axis {
    uint16_t offset;
    uint16_t deadzone;
    uint32_t scale; /* thousandths, including Nintendont's deadzone adjustment */
} rthid_axis;

typedef struct rthid_mapping {
    uint16_t version;
    uint16_t bytes;
    uint16_t vid, pid;
    uint16_t report_bytes; /* minimum report length needed by every offset */
    uint16_t polltype;     /* 0: GET_REPORT (PS3), 1: interrupt IN */
    uint16_t dpad;         /* 0: bit masks, 1: hat values */
    uint16_t dpad_mask;
    uint16_t digital_lr;   /* 0: analog + buttons, 1: digital, 2: thresholds */
    uint16_t multi_in;     /* 0: all reports, 1: filter by first byte */
    uint16_t multi_value;
    uint16_t analog_l, analog_r;
    rthid_button_map buttons[RTHID_BUTTONS];
    rthid_axis axes[4];    /* X, Y, C-X, C-Y; USB Y increases downwards */
    uint16_t rumble_type;  /* 0: SET_REPORT, 1: interrupt OUT */
    uint16_t rumble_bytes;
    uint16_t rumble_transfer;
    uint16_t rumble_count;
    uint8_t rumble_on[RTHID_RUMBLE_MAX];
    uint8_t rumble_off[RTHID_RUMBLE_MAX];
} rthid_mapping;

/* A layout as the driver uses it, in the menu and in games: an
 * rthid_mapping packed by rthid_pack (in the menu, after rthid_valid).
 * Every offset is under RTHID_REPORT_MAX, so one byte holds it
 * (RTHID_NONE: none), as do the masks and dead zones rthid_valid allows.
 * The two rumble messages follow it at their own length, `rumble_bytes`
 * each (on, then off); `size` covers them and is a multiple of 4. 396
 * bytes as an rthid_mapping, 80 plus the rumble here (#506). */
#define RTHID_NONE 0xFFu
typedef struct rthid_layout {
    uint16_t size;
    uint16_t vid, pid;
    uint8_t report_bytes;
    uint8_t polltype, dpad, dpad_mask, digital_lr, multi_in, multi_value;
    uint8_t analog_l, analog_r;
    uint8_t rumble_type, rumble_bytes, rumble_transfer, rumble_count;
    uint8_t button_offset[RTHID_BUTTONS];
    uint8_t button_mask[RTHID_BUTTONS];
    uint8_t axis_offset[4], axis_deadzone[4]; /* X, Y, C-X, C-Y */
    uint8_t reserved;
    uint32_t axis_scale[4];
} rthid_layout;
#define RTHID_LAYOUT_BYTES(rumble) ((uint32_t)((sizeof(rthid_layout) + 2u * (rumble) + 3u) & ~3u))
#define RTHID_LAYOUT_MAX RTHID_LAYOUT_BYTES(RTHID_RUMBLE_MAX)
/* The rumble messages after a layout. */
#define RTHID_RUMBLE_ON(l) ((const uint8_t*)((l) + 1))
#define RTHID_RUMBLE_OFF(l) (RTHID_RUMBLE_ON(l) + (l)->rumble_bytes)

typedef struct rthid_device {
    int32_t id;
    uint32_t vid_pid;
} rthid_device;

typedef struct rthid_catalog {
    uint32_t adapter_disabled; /* preserve the separate adapter setting */
    uint32_t count;
    /* A v5 list already consumed by the menu may never be sent again
     * until hotplug. Keep that snapshot alongside the mappings. */
    uint32_t known_count;
    rthid_device known[RTHID_DEVICE_MAX];
    /* The same for /dev/usb/ven's list (wired Xbox 360 pads): the menu's
     * libogc took its one answer, and the game's request waits for the
     * next plug or unplug. */
    uint32_t ven_known_count;
    rthid_device ven_known[RTHID_DEVICE_MAX];
    /* Last: `count` layouts back to back, `layout_bytes` in all; a game's
     * copy holds only those bytes (gcad_layout). */
    uint32_t layout_bytes;
    uint32_t layouts[RTHID_CONFIG_MAX * RTHID_LAYOUT_MAX / 4u];
} rthid_catalog;

#ifdef __cplusplus
extern "C" {
#endif
struct gcad_pad;
int rthid_valid(const rthid_mapping* mapping);
int rthid_catalog_valid(const rthid_catalog* catalog);
/* The `index`th layout, 0 past the last or in a catalog that doesn't add up. */
const rthid_layout* rthid_layout_at(const rthid_catalog* catalog, uint32_t index);
const rthid_layout* rthid_find(const rthid_catalog* catalog, uint32_t vid_pid);
/* In the menu: `m` (which passes rthid_valid) packed into `out`, which has
 * room for `room` bytes; its size, 0 when it doesn't pass or fit. */
uint32_t rthid_pack(const rthid_mapping* m, rthid_layout* out, uint32_t room);
/* In the menu: a layout added at the end of the catalog; 0 when it is full. */
int rthid_catalog_add(rthid_catalog* catalog, const rthid_layout* layout);
/* A built-in pad's mapping, with no config file: only its IDs, its report
 * length and the interrupt poll are used, and the pad's own decoder in
 * rtgcad.c reads its reports. Valid for rthid_valid, decoding to nothing. */
void rthid_builtin(rthid_mapping* m, uint32_t vid_pid, uint16_t report_bytes);
/* The same as a layout (no rumble), for the driver's own Xbox 360 pads. */
void rthid_layout_builtin(rthid_layout* l, uint32_t vid_pid, uint8_t report_bytes);
int rthid_decode(const rthid_layout* layout, const uint8_t* report,
                 uint32_t bytes, struct gcad_pad* out);
#ifdef __cplusplus
}
#endif
#endif
