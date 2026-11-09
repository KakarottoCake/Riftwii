// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/padcal.hpp"

#include <cstdio>
#include <cstring>

#include "riftwii/textlines.hpp"

namespace riftwii {
namespace {

constexpr std::uint32_t kPorts = RTPADCAL_PORTS;
// The values on a line after its slot: two sticks' four directions, two
// triggers and the dead zone.
constexpr std::size_t kLineValues = 11;
// The furthest an unstretched direction goes: 127 right and up, 128 left
// and down (a signed byte).
constexpr std::uint8_t kPositiveTop = 127;
constexpr std::uint8_t kNegativeTop = 128;

std::size_t usb_count(const std::vector<PadCalibrations::Entry>& entries) {
    std::size_t count = 0;
    for (const PadCalibrations::Entry& e : entries)
        if (e.slot.kind == PadSlot::Kind::Usb) ++count;
    return count;
}

// A number from 0 to `top`, all digits; false for anything else.
bool parse_small(const std::string& word, unsigned top, std::uint8_t& out) {
    if (word.empty() || word.size() > 3) return false;
    unsigned value = 0;
    for (char ch : word) {
        if (ch < '0' || ch > '9') return false;
        value = value * 10 + static_cast<unsigned>(ch - '0');
    }
    if (value > top) return false;
    out = static_cast<std::uint8_t>(value);
    return true;
}

std::vector<std::string> split_words(const std::string& text) {
    std::vector<std::string> words;
    std::string word;
    for (char ch : text) {
        if (ch == ' ' || ch == '\t' || ch == '\r') {
            if (!word.empty()) words.push_back(word);
            word.clear();
        } else {
            word += ch;
        }
    }
    if (!word.empty()) words.push_back(word);
    return words;
}

// One direction's stretch: values past `dead` spread so that the measured
// `reach` becomes the healthy `target`, and none past it. A direction that
// reaches the target already keeps its reach (only the dead zone is taken
// out below it) and may go on to the signed byte's end `top` (127 or 128),
// as may one that was not measured.
void stretch_direction(std::uint8_t reach, std::uint8_t target, std::uint8_t dead, std::uint8_t top,
                       std::uint16_t& scale, std::uint8_t& limit) {
    const bool measured = reach != 0;
    const bool stretched = measured && reach < target;
    const unsigned from = measured ? (reach < top ? reach : top) : top;
    const unsigned to = stretched ? target : from;
    // kPadMinReach and kPadDeadZoneMax keep a measured reach well past the
    // dead zone; this only guards a hand-edited file.
    const unsigned span = from > dead ? from - dead : 1;
    const unsigned rounded = (to * RTPADCAL_ONE + span / 2) / span;
    scale = static_cast<std::uint16_t>(rounded > 0xFFFFu ? 0xFFFFu : rounded);
    limit = static_cast<std::uint8_t>(stretched ? target : top);
}

void stretch_trigger(std::uint8_t reach, std::uint16_t& scale, std::uint8_t& limit) {
    if (reach == 0 || reach >= kPadTriggerReach) {
        scale = RTPADCAL_ONE;
        limit = 255;
        return;
    }
    scale = static_cast<std::uint16_t>((kPadTriggerReach * RTPADCAL_ONE + reach / 2u) / reach);
    limit = kPadTriggerReach;
}

}  // namespace

bool PadCalibration::is_default() const {
    for (std::uint8_t reach : main)
        if (reach != 0) return false;
    for (std::uint8_t reach : c_stick)
        if (reach != 0) return false;
    return trigger_l == 0 && trigger_r == 0 && dead_zone == 0;
}

std::string pad_slot_key(const PadSlot& slot) {
    char key[24];
    switch (slot.kind) {
        case PadSlot::Kind::Wii: std::snprintf(key, sizeof(key), "wii%u", static_cast<unsigned>(slot.id + 1)); break;
        case PadSlot::Kind::Adapter:
            std::snprintf(key, sizeof(key), "adapter%u", static_cast<unsigned>(slot.id + 1));
            break;
        case PadSlot::Kind::Usb: std::snprintf(key, sizeof(key), "usb-%08x", static_cast<unsigned>(slot.id)); break;
    }
    return key;
}

bool parse_pad_slot(const std::string& key, PadSlot& slot) {
    const auto port = [&](const char* prefix, PadSlot::Kind kind) {
        const std::size_t length = std::strlen(prefix);
        if (key.size() != length + 1 || key.compare(0, length, prefix) != 0) return false;
        const char digit = key[length];
        if (digit < '1' || digit > '0' + static_cast<char>(kPorts)) return false;
        slot.kind = kind;
        slot.id = static_cast<std::uint32_t>(digit - '1');
        return true;
    };
    if (port("wii", PadSlot::Kind::Wii) || port("adapter", PadSlot::Kind::Adapter)) return true;
    if (key.size() != 12 || key.compare(0, 4, "usb-") != 0) return false;
    std::uint32_t id = 0;
    for (std::size_t i = 4; i < key.size(); ++i) {
        const char ch = key[i];
        std::uint32_t nibble = 0;
        if (ch >= '0' && ch <= '9') {
            nibble = static_cast<std::uint32_t>(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            nibble = static_cast<std::uint32_t>(ch - 'a' + 10);
        } else {
            return false;
        }
        id = id << 4 | nibble;
    }
    if (id == 0) return false;  // 0 marks an empty USB row in the game's table
    slot.kind = PadSlot::Kind::Usb;
    slot.id = id;
    return true;
}

void PadCalibrations::parse(const std::string& text) {
    entries_.clear();
    LineReader lines(text);
    std::string line;
    while (lines.next(line)) {
        const std::size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::vector<std::string> key = split_words(line.substr(0, equals));
        const std::vector<std::string> values = split_words(line.substr(equals + 1));
        PadSlot slot;
        if (key.size() != 1 || !parse_pad_slot(key[0], slot) || values.size() != kLineValues) continue;
        PadCalibration c;
        bool ok = true;
        for (std::size_t i = 0; i < 4; ++i) {
            ok = ok && parse_small(values[i], kNegativeTop, c.main[i]);
            ok = ok && parse_small(values[4 + i], kNegativeTop, c.c_stick[i]);
        }
        ok = ok && parse_small(values[8], 255, c.trigger_l) && parse_small(values[9], 255, c.trigger_r) &&
             parse_small(values[10], kPadDeadZoneMax, c.dead_zone);
        // A later line for the same slot wins, as a file edited by hand expects.
        if (ok) set(slot, c);
    }
}

std::string PadCalibrations::serialize() const {
    std::string out =
        "# GameCube controller calibration (Settings > Controllers). One line per controller:\n"
        "# slot = stick right left up down, C stick right left up down, L, R, dead zone\n";
    for (const Entry& e : entries_) {
        const PadCalibration& c = e.calibration;
        char line[96];
        std::snprintf(line, sizeof(line), "%s = %u %u %u %u %u %u %u %u %u %u %u\n", pad_slot_key(e.slot).c_str(),
                      c.main[0], c.main[1], c.main[2], c.main[3], c.c_stick[0], c.c_stick[1], c.c_stick[2],
                      c.c_stick[3], c.trigger_l, c.trigger_r, c.dead_zone);
        out += line;
    }
    return out;
}

const PadCalibration* PadCalibrations::find(const PadSlot& slot) const {
    for (const Entry& e : entries_)
        if (e.slot == slot) return &e.calibration;
    return nullptr;
}

bool PadCalibrations::set(const PadSlot& slot, const PadCalibration& calibration) {
    if (calibration.is_default()) {
        forget(slot);
        return true;
    }
    for (Entry& e : entries_) {
        if (e.slot == slot) {
            e.calibration = calibration;
            return true;
        }
    }
    if (slot.kind == PadSlot::Kind::Usb && usb_count(entries_) >= RTPADCAL_USB_PADS) return false;
    if (slot.kind != PadSlot::Kind::Usb && slot.id >= kPorts) return false;
    entries_.push_back(Entry{slot, calibration});
    return true;
}

void PadCalibrations::forget(const PadSlot& slot) {
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].slot == slot) {
            entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
    }
}

rtpadcal_pad pad_calibration_stretch(const PadCalibration& calibration) {
    rtpadcal_pad pad{};
    const std::uint8_t dead = calibration.dead_zone > kPadDeadZoneMax ? kPadDeadZoneMax : calibration.dead_zone;
    for (std::size_t direction = 0; direction < 4; ++direction) {
        // Right and up are the positive directions (127 at most), left and
        // down the negative ones (128).
        const std::uint8_t top = (direction & 1) == 0 ? kPositiveTop : kNegativeTop;
        stretch_direction(calibration.main[direction], kPadMainStickReach, dead, top, pad.scale[direction],
                          pad.limit[direction]);
        stretch_direction(calibration.c_stick[direction], kPadCStickReach, dead, top, pad.scale[4 + direction],
                          pad.limit[4 + direction]);
    }
    pad.dead[0] = dead;
    pad.dead[1] = dead;
    stretch_trigger(calibration.trigger_l, pad.trigger_scale[0], pad.trigger_limit[0]);
    stretch_trigger(calibration.trigger_r, pad.trigger_scale[1], pad.trigger_limit[1]);
    pad.used = calibration.is_default() ? 0 : 1;
    return pad;
}

bool pad_calibration_table(const PadCalibrations& calibrations, rtpadcal_table& table) {
    table = rtpadcal_table{};
    std::uint32_t usb = 0;
    bool any = false;
    for (const PadCalibrations::Entry& e : calibrations.entries()) {
        if (e.calibration.is_default()) continue;
        const rtpadcal_pad pad = pad_calibration_stretch(e.calibration);
        switch (e.slot.kind) {
            case PadSlot::Kind::Wii:
                if (e.slot.id >= kPorts) continue;
                table.console[e.slot.id] = pad;
                break;
            case PadSlot::Kind::Adapter:
                if (e.slot.id >= kPorts) continue;
                table.adapter[e.slot.id] = pad;
                break;
            case PadSlot::Kind::Usb:
                if (usb >= RTPADCAL_USB_PADS || e.slot.id == 0) continue;
                table.usb_id[usb] = e.slot.id;
                table.usb[usb] = pad;
                ++usb;
                break;
        }
        any = true;
    }
    return any;
}

void pad_calibration_note(PadCalibration& measured, const std::int8_t sticks[4], std::uint8_t trigger_l,
                          std::uint8_t trigger_r) {
    // Each axis's value goes to the direction it points: right or left for
    // an x, up or down for a y (right, left, up, down).
    const auto widen = [](std::uint8_t& furthest, int value) {
        const int size = value < 0 ? -value : value;
        if (size > furthest) furthest = static_cast<std::uint8_t>(size);
    };
    const auto note = [&](std::array<std::uint8_t, 4>& reach, std::int8_t x, std::int8_t y) {
        const std::size_t x_direction = x >= 0 ? 0 : 1;
        const std::size_t y_direction = y >= 0 ? 2 : 3;
        widen(reach[x_direction], x);
        widen(reach[y_direction], y);
    };
    note(measured.main, sticks[0], sticks[1]);
    note(measured.c_stick, sticks[2], sticks[3]);
    if (trigger_l > measured.trigger_l) measured.trigger_l = trigger_l;
    if (trigger_r > measured.trigger_r) measured.trigger_r = trigger_r;
}

bool pad_stick_measured(const std::array<std::uint8_t, 4>& reach) {
    for (std::uint8_t r : reach)
        if (r < kPadMinReach) return false;
    return true;
}

}  // namespace riftwii
