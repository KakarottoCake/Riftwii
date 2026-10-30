/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-FileCopyrightText: FIX94 and the Nintendont contributors, used with permission */
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

typedef struct rthid_device {
    int32_t id;
    uint32_t vid_pid;
} rthid_device;

typedef struct rthid_catalog {
    uint32_t adapter_disabled; /* preserve the separate adapter setting */
    uint32_t count;
    rthid_mapping maps[RTHID_CONFIG_MAX];
    /* A v5 list already consumed by the menu may never be sent again
     * until hotplug. Keep that snapshot alongside the mappings. */
    uint32_t known_count;
    rthid_device known[RTHID_DEVICE_MAX];
} rthid_catalog;

#ifdef __cplusplus
extern "C" {
#endif
struct gcad_pad;
int rthid_valid(const rthid_mapping* mapping);
int rthid_catalog_valid(const rthid_catalog* catalog);
const rthid_mapping* rthid_find(const rthid_catalog* catalog, uint32_t vid_pid);
int rthid_decode(const rthid_mapping* mapping, const uint8_t* report,
                 uint32_t bytes, struct gcad_pad* out);
#ifdef __cplusplus
}
#endif
#endif
