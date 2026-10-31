/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-FileCopyrightText: FIX94 and the Nintendont contributors, used with permission */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Independently written controller.ini report conversion. Nintendont's
 * HID.c and PADReadGC.c document the format and its scale/trigger rules.
 * No heap, libc, floating point, globals or address-dependent tables. */
#include "rtgcad.h"

/* Microsoft and the makers of wired pads in the Xbox 360's protocol
 * (Mad Catz, PDP, Hori, RedOctane, Nacon, Razer, Numark, Joytech,
 * Harmonix, PowerA and its BDA name, 8BitDo, Hyperkin, Fanatec,
 * Thrustmaster, Logitech's F310/F710 in their Xbox mode). A comparison
 * chain, not a table: the game blobs carry no constant data. */
/* Here, not in rtgcad.c: the menu's USB code asks it without the driver. */
int gcad_xbox_vendor(uint32_t vid) {
    return vid == 0x045E || vid == 0x0738 || vid == 0x0E6F || vid == 0x0F0D || vid == 0x1430 ||
           vid == 0x146B || vid == 0x1532 || vid == 0x15E4 || vid == 0x162E || vid == 0x1689 ||
           vid == 0x1BAD || vid == 0x20D6 || vid == 0x24C6 || vid == 0x2DC8 || vid == 0x2E24 ||
           vid == 0x3767 || vid == 0x044F || vid == 0x046D;
}

static int valid_offset(uint16_t offset, uint16_t bytes) {
    return offset == RTHID_ABSENT || offset < bytes;
}

int rthid_valid(const rthid_mapping* m) {
    uint32_t i;
    if (!m || m->version != RTHID_VERSION || m->bytes != sizeof(*m) || !m->vid || !m->pid ||
        !m->report_bytes || m->report_bytes > RTHID_REPORT_MAX || m->polltype > 1 ||
        m->dpad > 1 || m->dpad_mask > 255 || m->digital_lr > 2 || m->multi_in > 1 ||
        m->multi_value > 255 || m->rumble_type > 1) return 0;
    if (!m->polltype && m->report_bytes > 49) return 0;
    if (!valid_offset(m->analog_l, m->report_bytes) || !valid_offset(m->analog_r, m->report_bytes)) return 0;
    for (i = 0; i < RTHID_BUTTONS; ++i) {
        if (!valid_offset(m->buttons[i].offset, m->report_bytes) || m->buttons[i].mask > 255) return 0;
    }
    for (i = 0; i < 4; ++i) {
        if (!valid_offset(m->axes[i].offset, m->report_bytes) || m->axes[i].deadzone >= 128 ||
            !m->axes[i].scale || m->axes[i].scale > 1280000u) return 0;
    }
    if (m->rumble_bytes > RTHID_RUMBLE_MAX) return 0;
    if (m->rumble_bytes) {
        if (!m->rumble_transfer || m->rumble_transfer > RTHID_RUMBLE_MAX ||
            !m->rumble_count || m->rumble_count > 16 ||
            (uint32_t)m->rumble_transfer * m->rumble_count != m->rumble_bytes) return 0;
    } else if (m->rumble_transfer || m->rumble_count) {
        return 0;
    }
    return 1;
}

int rthid_catalog_valid(const rthid_catalog* catalog) {
    uint32_t i;
    if (!catalog || catalog->adapter_disabled > 1 || catalog->count > RTHID_CONFIG_MAX ||
        catalog->known_count > RTHID_DEVICE_MAX || catalog->ven_known_count > RTHID_DEVICE_MAX) return 0;
    for (i = 0; i < catalog->count; ++i) {
        if (!rthid_valid(&catalog->maps[i])) return 0;
    }
    return 1;
}

const rthid_mapping* rthid_find(const rthid_catalog* catalog, uint32_t vid_pid) {
    uint32_t i;
    if (!catalog || catalog->count > RTHID_CONFIG_MAX) return 0;
    for (i = 0; i < catalog->count; ++i) {
        const rthid_mapping* m = &catalog->maps[i];
        if ((((uint32_t)m->vid << 16) | m->pid) == vid_pid && rthid_valid(m)) return m;
    }
    return 0;
}

static int pressed(const rthid_mapping* m, uint32_t index, const uint8_t* report) {
    const rthid_button_map* b = &m->buttons[index];
    return b->offset != RTHID_ABSENT && (report[b->offset] & b->mask) != 0;
}

static int direction(const rthid_mapping* m, uint32_t index, const uint8_t* report) {
    const rthid_button_map* b = &m->buttons[index];
    if (b->offset == RTHID_ABSENT) return 0;
    if (!m->dpad) return pressed(m, index, report);
    return (report[b->offset] & m->dpad_mask) == b->mask;
}

static int8_t axis_value(const rthid_axis* axis, const uint8_t* report, int vertical) {
    int32_t value, magnitude;
    if (axis->offset == RTHID_ABSENT) return 0;
    /* These centres and the radius in thousandths follow controller.ini,
     * including its 127-centred inverted Y axis. Clamp before narrowing. */
    value = vertical ? 127 - (int32_t)report[axis->offset] : (int32_t)report[axis->offset] - 128;
    magnitude = value < 0 ? -value : value;
    if (magnitude <= axis->deadzone) return 0;
    magnitude = ((magnitude - axis->deadzone) * (int32_t)axis->scale) / 1000;
    value = value < 0 ? -magnitude : magnitude;
    if (value < -128) value = -128;
    if (value > 127) value = 127;
    return (int8_t)value;
}

static uint8_t trigger_value(uint16_t offset, const uint8_t* report) {
    uint32_t value;
    if (offset == RTHID_ABSENT || report[offset] <= 26) return 0;
    value = ((uint32_t)report[offset] - 26u) * 111u / 100u;
    return (uint8_t)(value > 255u ? 255u : value);
}

int rthid_decode(const rthid_mapping* m, const uint8_t* report, uint32_t bytes, struct gcad_pad* out) {
    uint16_t buttons = 0;
    int left, right;
    if (!report || !out || !rthid_valid(m) || bytes < m->report_bytes || bytes > RTHID_REPORT_MAX) return 0;
    if (m->multi_in && report[0] != m->multi_value) return 0;
    if (pressed(m, RTHID_A, report)) buttons |= GCAD_PAD_A;
    if (pressed(m, RTHID_B, report)) buttons |= GCAD_PAD_B;
    if (pressed(m, RTHID_X, report)) buttons |= GCAD_PAD_X;
    if (pressed(m, RTHID_Y, report)) buttons |= GCAD_PAD_Y;
    if (pressed(m, RTHID_Z, report)) buttons |= GCAD_PAD_Z;
    if (pressed(m, RTHID_START, report)) buttons |= GCAD_PAD_START;
    if (direction(m, RTHID_LEFT, report) || (m->dpad &&
        (direction(m, RTHID_DOWN_LEFT, report) || direction(m, RTHID_UP_LEFT, report)))) buttons |= GCAD_PAD_LEFT;
    if (direction(m, RTHID_RIGHT, report) || (m->dpad &&
        (direction(m, RTHID_RIGHT_UP, report) || direction(m, RTHID_DOWN_RIGHT, report)))) buttons |= GCAD_PAD_RIGHT;
    if (direction(m, RTHID_DOWN, report) || (m->dpad &&
        (direction(m, RTHID_DOWN_RIGHT, report) || direction(m, RTHID_DOWN_LEFT, report)))) buttons |= GCAD_PAD_DOWN;
    if (direction(m, RTHID_UP, report) || (m->dpad &&
        (direction(m, RTHID_RIGHT_UP, report) || direction(m, RTHID_UP_LEFT, report)))) buttons |= GCAD_PAD_UP;

    left = pressed(m, RTHID_L, report);
    right = pressed(m, RTHID_R, report);
    if (m->digital_lr == 2) {
        const rthid_button_map* l = &m->buttons[RTHID_L];
        const rthid_button_map* r = &m->buttons[RTHID_R];
        left = l->offset != RTHID_ABSENT && report[l->offset] >= l->mask;
        right = r->offset != RTHID_ABSENT && report[r->offset] >= r->mask;
    }
    if (m->digital_lr == 1 && pressed(m, RTHID_ZL, report)) {
        out->trigger_l = left ? 127 : 0;
        out->trigger_r = right ? 127 : 0;
    } else {
        if (left) buttons |= GCAD_PAD_L;
        if (right) buttons |= GCAD_PAD_R;
        out->trigger_l = m->digital_lr == 1 ? (left ? 255 : 0) : trigger_value(m->analog_l, report);
        out->trigger_r = m->digital_lr == 1 ? (right ? 255 : 0) : trigger_value(m->analog_r, report);
    }
    out->buttons = buttons;
    out->stick_x = axis_value(&m->axes[0], report, 0);
    out->stick_y = axis_value(&m->axes[1], report, 1);
    out->substick_x = axis_value(&m->axes[2], report, 0);
    out->substick_y = axis_value(&m->axes[3], report, 1);
    out->wireless = m->rumble_bytes ? 0 : 1; /* existing callers use this as "no motor" */
    return 1;
}
