// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The USB controller layouts RiftWii carries (riftwii/builtinpads.hpp):
// each one read the way an SD card file is, so a layout the reader would
// refuse never ships.
#include "riftwii/builtinpads.hpp"
#include "riftwii/hidconfig.hpp"
#include "rthid.h"
#include "rtgcad.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

unsigned failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::cerr << __LINE__ << ": " #condition "\n"; ++failures; } } while (0)

// Every layout reads, names the IDs it is filed under, and fits the
// reader's size limit; the table is sorted with no ID twice.
void test_every_layout_reads() {
    CHECK(riftwii::kBuiltinPadCount > 0);
    for (std::size_t i = 0; i < riftwii::kBuiltinPadCount; ++i) {
        const riftwii::BuiltinPad& pad = riftwii::kBuiltinPads[i];
        rthid_mapping mapping{};
        std::string error;
        const bool valid = riftwii::parse_hid_config(pad.text, mapping, error);
        if (!valid) std::cerr << pad.file << ": " << error << '\n';
        CHECK(valid);
        CHECK(rthid_valid(&mapping));
        CHECK(((static_cast<std::uint32_t>(mapping.vid) << 16) | mapping.pid) == pad.vid_pid);
        CHECK(std::strlen(pad.text) <= riftwii::kHidConfigMaxBytes);
        CHECK(pad.name != nullptr && pad.name[0] != 0);
        if (i > 0) CHECK(riftwii::kBuiltinPads[i - 1].vid_pid < pad.vid_pid);
        // The Wii U GameCube adapter is RiftWii's own driver's.
        CHECK(pad.vid_pid != 0x057E0337u);
    }
}

// The pads players ask about are there, and an unknown one is not.
void test_lookup() {
    const riftwii::BuiltinPad* ds3 = riftwii::find_builtin_pad(0x054C0268u);
    CHECK(ds3 != nullptr);
    if (ds3) CHECK(std::string(ds3->name) == "DualShock 3");
    CHECK(riftwii::find_builtin_pad(0x054C05C4u) != nullptr);  // DualShock 4, first model
    CHECK(riftwii::find_builtin_pad(0x054C09CCu) != nullptr);  // DualShock 4, second model
    CHECK(riftwii::find_builtin_pad(0x054C0CE6u) != nullptr);  // DualSense
    // The 8BitDo SNES30 Pro shares the first DualShock 4's IDs; the
    // DualShock 4's layout is the one kept.
    const riftwii::BuiltinPad* ds4 = riftwii::find_builtin_pad(0x054C05C4u);
    if (ds4) CHECK(std::string(ds4->file) == "controller_ps4.ini");
    CHECK(riftwii::find_builtin_pad(0x12345678u) == nullptr);
    CHECK(riftwii::find_builtin_pad(0x057E0337u) == nullptr);
}

// The decoder as it read the full 396-byte mappings before the packed
// layouts (#506), kept word for word as the reference they must match.
static int ref_pressed(const rthid_mapping* m, uint32_t index, const uint8_t* report) {
    const rthid_button_map* b = &m->buttons[index];
    return b->offset != RTHID_ABSENT && (report[b->offset] & b->mask) != 0;
}

static int ref_direction(const rthid_mapping* m, uint32_t index, const uint8_t* report) {
    const rthid_button_map* b = &m->buttons[index];
    if (b->offset == RTHID_ABSENT) return 0;
    if (!m->dpad) return ref_pressed(m, index, report);
    return (report[b->offset] & m->dpad_mask) == b->mask;
}

static int8_t ref_axis_value(const rthid_axis* axis, const uint8_t* report, int vertical) {
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

static uint8_t ref_trigger_value(uint16_t offset, const uint8_t* report) {
    uint32_t value;
    if (offset == RTHID_ABSENT || report[offset] <= 26) return 0;
    value = ((uint32_t)report[offset] - 26u) * 111u / 100u;
    return (uint8_t)(value > 255u ? 255u : value);
}

int reference_decode(const rthid_mapping* m, const uint8_t* report, uint32_t bytes, struct gcad_pad* out) {
    uint16_t buttons = 0;
    int left, right;
    if (!report || !out || !rthid_valid(m) || bytes < m->report_bytes || bytes > RTHID_REPORT_MAX) return 0;
    if (m->multi_in && report[0] != m->multi_value) return 0;
    if (ref_pressed(m, RTHID_A, report)) buttons |= GCAD_PAD_A;
    if (ref_pressed(m, RTHID_B, report)) buttons |= GCAD_PAD_B;
    if (ref_pressed(m, RTHID_X, report)) buttons |= GCAD_PAD_X;
    if (ref_pressed(m, RTHID_Y, report)) buttons |= GCAD_PAD_Y;
    if (ref_pressed(m, RTHID_Z, report)) buttons |= GCAD_PAD_Z;
    if (ref_pressed(m, RTHID_START, report)) buttons |= GCAD_PAD_START;
    if (ref_direction(m, RTHID_LEFT, report) || (m->dpad &&
        (ref_direction(m, RTHID_DOWN_LEFT, report) || ref_direction(m, RTHID_UP_LEFT, report)))) buttons |= GCAD_PAD_LEFT;
    if (ref_direction(m, RTHID_RIGHT, report) || (m->dpad &&
        (ref_direction(m, RTHID_RIGHT_UP, report) || ref_direction(m, RTHID_DOWN_RIGHT, report)))) buttons |= GCAD_PAD_RIGHT;
    if (ref_direction(m, RTHID_DOWN, report) || (m->dpad &&
        (ref_direction(m, RTHID_DOWN_RIGHT, report) || ref_direction(m, RTHID_DOWN_LEFT, report)))) buttons |= GCAD_PAD_DOWN;
    if (ref_direction(m, RTHID_UP, report) || (m->dpad &&
        (ref_direction(m, RTHID_RIGHT_UP, report) || ref_direction(m, RTHID_UP_LEFT, report)))) buttons |= GCAD_PAD_UP;

    left = ref_pressed(m, RTHID_L, report);
    right = ref_pressed(m, RTHID_R, report);
    if (m->digital_lr == 2) {
        const rthid_button_map* l = &m->buttons[RTHID_L];
        const rthid_button_map* r = &m->buttons[RTHID_R];
        left = l->offset != RTHID_ABSENT && report[l->offset] >= l->mask;
        right = r->offset != RTHID_ABSENT && report[r->offset] >= r->mask;
    }
    if (m->digital_lr == 1 && ref_pressed(m, RTHID_ZL, report)) {
        out->trigger_l = left ? 127 : 0;
        out->trigger_r = right ? 127 : 0;
    } else {
        if (left) buttons |= GCAD_PAD_L;
        if (right) buttons |= GCAD_PAD_R;
        out->trigger_l = m->digital_lr == 1 ? (left ? 255 : 0) : ref_trigger_value(m->analog_l, report);
        out->trigger_r = m->digital_lr == 1 ? (right ? 255 : 0) : ref_trigger_value(m->analog_r, report);
    }
    out->buttons = buttons;
    out->stick_x = ref_axis_value(&m->axes[0], report, 0);
    out->stick_y = ref_axis_value(&m->axes[1], report, 1);
    out->substick_x = ref_axis_value(&m->axes[2], report, 0);
    out->substick_y = ref_axis_value(&m->axes[3], report, 1);
    out->wireless = m->rumble_bytes ? 0 : 1; /* existing callers use this as "no motor" */
    return 1;
}

// The packed layouts decode every report exactly as the full mappings did:
// each built-in layout, in each L/R mode, with and without a first-byte
// filter and with rumble, against thousands of random reports.
void test_packed_decodes_the_same() {
    std::uint32_t seed = 0x2611u;
    auto next = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<std::uint8_t>(seed >> 24);
    };
    std::vector<rthid_mapping> layouts;
    for (std::size_t i = 0; i < riftwii::kBuiltinPadCount; ++i) {
        rthid_mapping base{};
        std::string error;
        if (!riftwii::parse_hid_config(riftwii::kBuiltinPads[i].text, base, error)) continue;
        for (std::uint16_t lr = 0; lr <= 2; ++lr) {
            rthid_mapping m = base;
            m.digital_lr = lr;
            layouts.push_back(m);
        }
        rthid_mapping filtered = base;
        filtered.multi_in = 1;
        filtered.multi_value = 0x01;
        layouts.push_back(filtered);
        rthid_mapping rumble = base;
        rumble.rumble_type = 1;
        rumble.rumble_bytes = 6;
        rumble.rumble_transfer = 6;
        rumble.rumble_count = 1;
        for (unsigned j = 0; j < 6; ++j) {
            rumble.rumble_on[j] = next();
            rumble.rumble_off[j] = next();
        }
        layouts.push_back(rumble);
    }
    CHECK(layouts.size() >= 5 * riftwii::kBuiltinPadCount);
    unsigned differences = 0, compared = 0;
    for (const rthid_mapping& m : layouts) {
        alignas(4) std::uint8_t packed_bytes[RTHID_LAYOUT_MAX] = {};
        auto* packed = reinterpret_cast<rthid_layout*>(packed_bytes);
        CHECK(rthid_pack(&m, packed, sizeof(packed_bytes)) == RTHID_LAYOUT_BYTES(m.rumble_bytes));
        CHECK(packed->size <= RTHID_LAYOUT_MAX);
        CHECK(std::memcmp(RTHID_RUMBLE_ON(packed), m.rumble_on, m.rumble_bytes) == 0);
        CHECK(std::memcmp(RTHID_RUMBLE_OFF(packed), m.rumble_off, m.rumble_bytes) == 0);
        for (unsigned n = 0; n < 2000; ++n) {
            std::uint8_t report[RTHID_REPORT_MAX] = {};
            const std::uint32_t bytes = m.report_bytes + n % 3 <= RTHID_REPORT_MAX ? m.report_bytes + n % 3 : m.report_bytes;
            for (std::uint32_t k = 0; k < bytes; ++k) report[k] = next();
            if (n % 4 == 0) report[0] = static_cast<std::uint8_t>(m.multi_value);
            if (n % 50 == 1) std::memset(report, 0, sizeof(report));
            if (n % 50 == 2) std::memset(report, 0xFF, sizeof(report));
            gcad_pad before{}, after{};
            const int was = reference_decode(&m, report, bytes, &before);
            const int is = rthid_decode(packed, report, bytes, &after);
            if (was != is || std::memcmp(&before, &after, sizeof(before)) != 0) ++differences;
            ++compared;
        }
    }
    if (differences) std::cerr << differences << " of " << compared << " reports decoded differently\n";
    CHECK(differences == 0);
    CHECK(compared > 10000);
}

}  // namespace

int main() {
    test_every_layout_reads();
    test_lookup();
    test_packed_decodes_the_same();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "builtinpads tests passed\n";
    return 0;
}
