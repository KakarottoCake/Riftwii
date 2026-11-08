/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
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

void rthid_builtin(rthid_mapping* m, uint32_t vid_pid, uint16_t report_bytes) {
    volatile uint8_t* bytes = (volatile uint8_t*)m;  /* volatile: not turned into a memset call */
    uint32_t i;
    for (i = 0; i < sizeof(*m); ++i) bytes[i] = 0;
    m->version = RTHID_VERSION;
    m->bytes = sizeof(*m);
    m->vid = (uint16_t)(vid_pid >> 16);
    m->pid = (uint16_t)vid_pid;
    m->report_bytes = report_bytes;
    m->polltype = 1;
    m->analog_l = RTHID_ABSENT;
    m->analog_r = RTHID_ABSENT;
    for (i = 0; i < RTHID_BUTTONS; ++i) m->buttons[i].offset = RTHID_ABSENT;
    for (i = 0; i < 4; ++i) {
        m->axes[i].offset = RTHID_ABSENT;
        m->axes[i].scale = 1000;
    }
}

/* Here, not in rtgcad.c, as gcad_xbox_vendor: the menu's controller files
 * call it without the driver. No rumble: the pad's own rumble messages
 * are not sent (yet), so games see a controller without a motor. */
void gcad_switch_mapping(rthid_mapping* m) {
    rthid_builtin(m, GCAD_SWITCH_VID_PID, GCAD_SWITCH_REPORT_BYTES);
}

#ifndef RT_TARGET_PPC
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

#endif

/* Every layout in a catalog was packed by rthid_pack, which takes only
 * mappings that pass rthid_valid, and added by rthid_catalog_add, which
 * checks its size (both in the menu, #506). The game only keeps its walk
 * inside the catalog's bytes, which is all the code it carries for this. */
static const rthid_layout* layout_from(const rthid_catalog* catalog, uint32_t at) {
    return (const rthid_layout*)(const void*)((const uint8_t*)catalog->layouts + at);
}

#ifndef RT_TARGET_PPC
static int layout_fits(const rthid_layout* l, uint32_t left) {
    return left >= sizeof(rthid_layout) && l->size >= sizeof(rthid_layout) && (l->size & 3u) == 0 &&
           l->size <= left && l->size == RTHID_LAYOUT_BYTES(l->rumble_bytes);
}
#endif

int rthid_catalog_valid(const rthid_catalog* catalog) {
    if (!catalog || catalog->adapter_disabled > 1 || catalog->count > RTHID_CONFIG_MAX ||
        catalog->known_count > RTHID_DEVICE_MAX || catalog->ven_known_count > RTHID_DEVICE_MAX ||
        catalog->layout_bytes > sizeof(catalog->layouts)) return 0;
#ifndef RT_TARGET_PPC
    {
        uint32_t i, at = 0;
        for (i = 0; i < catalog->count; ++i) {
            const rthid_layout* l = layout_from(catalog, at);
            if (!layout_fits(l, catalog->layout_bytes - at)) return 0;
            at += l->size;
        }
        if (at != catalog->layout_bytes) return 0;
    }
#endif
    return 1;
}

const rthid_layout* rthid_layout_at(const rthid_catalog* catalog, uint32_t index) {
    uint32_t at = 0;
    if (!catalog || index >= catalog->count || catalog->layout_bytes > sizeof(catalog->layouts)) return 0;
    while (at + sizeof(rthid_layout) <= catalog->layout_bytes) {
        const rthid_layout* l = layout_from(catalog, at);
        if (l->size < sizeof(rthid_layout)) return 0;
        if (index-- == 0) return l;
        at += l->size;
    }
    return 0;
}

const rthid_layout* rthid_find(const rthid_catalog* catalog, uint32_t vid_pid) {
    uint32_t i;
    const rthid_layout* l;
    for (i = 0; (l = rthid_layout_at(catalog, i)) != 0; ++i) {
        if ((((uint32_t)l->vid << 16) | l->pid) == vid_pid) return l;
    }
    return 0;
}

#ifndef RT_TARGET_PPC
static uint8_t packed_offset(uint16_t offset) {
    return offset == RTHID_ABSENT ? (uint8_t)RTHID_NONE : (uint8_t)offset;
}

uint32_t rthid_pack(const rthid_mapping* m, rthid_layout* out, uint32_t room) {
    uint32_t i, size;
    uint8_t* rumble;
    if (!out || !rthid_valid(m)) return 0;
    size = RTHID_LAYOUT_BYTES(m->rumble_bytes);
    if (size > room) return 0;
    rumble = (uint8_t*)(out + 1);
    for (i = 0; i < size; ++i) ((uint8_t*)out)[i] = 0;
    out->size = (uint16_t)size;
    out->vid = m->vid;
    out->pid = m->pid;
    out->report_bytes = (uint8_t)m->report_bytes;
    out->polltype = (uint8_t)m->polltype;
    out->dpad = (uint8_t)m->dpad;
    out->dpad_mask = (uint8_t)m->dpad_mask;
    out->digital_lr = (uint8_t)m->digital_lr;
    out->multi_in = (uint8_t)m->multi_in;
    out->multi_value = (uint8_t)m->multi_value;
    out->analog_l = packed_offset(m->analog_l);
    out->analog_r = packed_offset(m->analog_r);
    out->rumble_type = (uint8_t)m->rumble_type;
    out->rumble_bytes = (uint8_t)m->rumble_bytes;
    out->rumble_transfer = (uint8_t)m->rumble_transfer;
    out->rumble_count = (uint8_t)m->rumble_count;
    for (i = 0; i < RTHID_BUTTONS; ++i) {
        out->button_offset[i] = packed_offset(m->buttons[i].offset);
        out->button_mask[i] = (uint8_t)m->buttons[i].mask;
    }
    for (i = 0; i < 4; ++i) {
        out->axis_offset[i] = packed_offset(m->axes[i].offset);
        out->axis_deadzone[i] = (uint8_t)m->axes[i].deadzone;
        out->axis_scale[i] = m->axes[i].scale;
    }
    for (i = 0; i < m->rumble_bytes; ++i) {
        rumble[i] = m->rumble_on[i];
        rumble[m->rumble_bytes + i] = m->rumble_off[i];
    }
    return size;
}

int rthid_catalog_add(rthid_catalog* catalog, const rthid_layout* layout) {
    uint32_t i;
    uint8_t* to;
    if (!catalog || !layout || catalog->count >= RTHID_CONFIG_MAX ||
        catalog->layout_bytes > sizeof(catalog->layouts) ||
        !layout_fits(layout, (uint32_t)sizeof(catalog->layouts) - catalog->layout_bytes)) return 0;
    to = (uint8_t*)catalog->layouts + catalog->layout_bytes;
    for (i = 0; i < layout->size; ++i) to[i] = ((const uint8_t*)layout)[i];
    catalog->layout_bytes += layout->size;
    ++catalog->count;
    return 1;
}
#endif

void rthid_layout_builtin(rthid_layout* l, uint32_t vid_pid, uint8_t report_bytes) {
    volatile uint8_t* bytes = (volatile uint8_t*)l;  /* volatile: not turned into a memset call */
    uint32_t i;
    for (i = 0; i < sizeof(*l); ++i) bytes[i] = 0;
    l->size = (uint16_t)sizeof(*l);
    l->vid = (uint16_t)(vid_pid >> 16);
    l->pid = (uint16_t)vid_pid;
    l->report_bytes = report_bytes;
    l->polltype = 1;
    l->analog_l = RTHID_NONE;
    l->analog_r = RTHID_NONE;
    for (i = 0; i < RTHID_BUTTONS; ++i) l->button_offset[i] = RTHID_NONE;
    for (i = 0; i < 4; ++i) {
        l->axis_offset[i] = RTHID_NONE;
        l->axis_scale[i] = 1000;
    }
}

static int pressed(const rthid_layout* l, uint32_t index, const uint8_t* report) {
    const uint8_t offset = l->button_offset[index];
    return offset != RTHID_NONE && (report[offset] & l->button_mask[index]) != 0;
}

static int direction(const rthid_layout* l, uint32_t index, const uint8_t* report) {
    const uint8_t offset = l->button_offset[index];
    if (offset == RTHID_NONE) return 0;
    if (!l->dpad) return pressed(l, index, report);
    return (report[offset] & l->dpad_mask) == l->button_mask[index];
}

static int8_t axis_value(const rthid_layout* l, uint32_t axis, const uint8_t* report, int vertical) {
    const uint8_t offset = l->axis_offset[axis];
    const int32_t deadzone = l->axis_deadzone[axis];
    int32_t value, magnitude;
    if (offset == RTHID_NONE) return 0;
    /* These centres and the radius in thousandths follow controller.ini,
     * including its 127-centred inverted Y axis. Clamp before narrowing. */
    value = vertical ? 127 - (int32_t)report[offset] : (int32_t)report[offset] - 128;
    magnitude = value < 0 ? -value : value;
    if (magnitude <= deadzone) return 0;
    magnitude = ((magnitude - deadzone) * (int32_t)l->axis_scale[axis]) / 1000;
    value = value < 0 ? -magnitude : magnitude;
    if (value < -128) value = -128;
    if (value > 127) value = 127;
    return (int8_t)value;
}

static uint8_t trigger_value(uint8_t offset, const uint8_t* report) {
    uint32_t value;
    if (offset == RTHID_NONE || report[offset] <= 26) return 0;
    value = ((uint32_t)report[offset] - 26u) * 111u / 100u;
    return (uint8_t)(value > 255u ? 255u : value);
}

int rthid_decode(const rthid_layout* m, const uint8_t* report, uint32_t bytes, struct gcad_pad* out) {
    uint16_t buttons = 0;
    int left, right;
    if (!report || !out || !m || bytes < m->report_bytes || bytes > RTHID_REPORT_MAX) return 0;
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
        const uint8_t l_offset = m->button_offset[RTHID_L], r_offset = m->button_offset[RTHID_R];
        left = l_offset != RTHID_NONE && report[l_offset] >= m->button_mask[RTHID_L];
        right = r_offset != RTHID_NONE && report[r_offset] >= m->button_mask[RTHID_R];
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
    out->stick_x = axis_value(m, 0, report, 0);
    out->stick_y = axis_value(m, 1, report, 1);
    out->substick_x = axis_value(m, 2, report, 0);
    out->substick_y = axis_value(m, 3, report, 1);
    out->wireless = m->rumble_bytes ? 0 : 1; /* existing callers use this as "no motor" */
    return 1;
}
