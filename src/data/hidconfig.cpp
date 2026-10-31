// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: FIX94 and the Nintendont contributors, used with permission
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/hidconfig.hpp"

#include <array>
#include <cstdint>

namespace riftwii {
namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

bool number(std::string_view text, unsigned base, std::uint32_t limit, std::uint32_t& value) {
    text = trim(text);
    if (base == 16 && text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) text.remove_prefix(2);
    if (text.empty()) return false;
    value = 0;
    for (const char c : text) {
        unsigned digit = 16;
        if (c >= '0' && c <= '9') digit = static_cast<unsigned>(c - '0');
        if (c >= 'a' && c <= 'f') digit = static_cast<unsigned>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') digit = static_cast<unsigned>(c - 'A' + 10);
        if (digit >= base || digit > limit || value > (limit - digit) / base) return false;
        value = value * base + digit;
    }
    return true;
}

struct Field {
    std::string_view name;
    std::string_view value;
};

constexpr std::array<std::string_view, RTHID_BUTTONS> kButtons = {
    "A", "B", "X", "Y", "Z", "L", "R", "S", "Power", "ZL", "Left", "Down",
    "Right", "Up", "RightUp", "DownRight", "DownLeft", "UpLeft"
};
constexpr std::array<std::string_view, 4> kAxes = {"StickX", "StickY", "CStickX", "CStickY"};
constexpr std::array<std::string_view, 16> kOther = {
    "VID", "PID", "Polltype", "DPAD", "DigitalLR", "MultiIn", "MultiInValue", "LAnalog", "RAnalog",
    "Rumble", "RumbleType", "RumbleDataLen", "RumbleDataOn", "RumbleDataOff", "RumbleTransferLen", "RumbleTransfers"
};

bool known(std::string_view key) {
    for (const auto name : kButtons) if (name == key) return true;
    for (const auto name : kAxes) if (name == key) return true;
    for (const auto name : kOther) if (name == key) return true;
    return false;
}

bool values(std::string_view text, std::uint32_t* output, unsigned minimum, unsigned maximum, bool axis) {
    unsigned count = 0;
    while (!text.empty()) {
        if (count == maximum) return false;
        const auto comma = text.find(',');
        const auto part = text.substr(0, comma);
        const unsigned base = axis && count == 2 ? 10 : 16;
        const std::uint32_t limit = axis && count == 2 ? 1000 : 255;
        if (!number(part, base, limit, output[count])) return false;
        ++count;
        if (comma == std::string_view::npos) break;
        text.remove_prefix(comma + 1);
        if (trim(text).empty()) return false;
    }
    return count >= minimum;
}

}  // namespace

bool parse_hid_config(std::string_view text, rthid_mapping& mapping, std::string& error) {
    mapping = {};
    error.clear();
    if (text.size() > kHidConfigMaxBytes || text.find('\0') != std::string_view::npos) {
        error = "The controller file is too large or contains a zero byte.";
        return false;
    }
    std::array<Field, 64> fields{};
    unsigned count = 0;
    bool section = false;
    while (!text.empty()) {
        const auto newline = text.find('\n');
        auto line = trim(text.substr(0, newline));
        if (newline == std::string_view::npos) text = {};
        else text.remove_prefix(newline + 1);
        line = trim(line.substr(0, line.find_first_of("#;")));
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']') {
            if (section || count != 0 || line.size() > 128) {
                error = "The controller file must describe one device.";
                return false;
            }
            section = true;
            continue;
        }
        const auto equals = line.find('=');
        const auto key = trim(line.substr(0, equals));
        if (equals == std::string_view::npos || !known(key) || count == fields.size()) {
            error = "Unknown or malformed controller key: " + std::string(key.substr(0, 48));
            return false;
        }
        for (unsigned i = 0; i < count; ++i) {
            if (fields[i].name == key) {
                error = "Repeated controller key: " + std::string(key);
                return false;
            }
        }
        if (trim(line.substr(equals + 1)).empty()) {
            error = "Empty controller value: " + std::string(key);
            return false;
        }
        fields[count++] = {key, trim(line.substr(equals + 1))};
    }
    const auto get = [&](std::string_view name) {
        for (unsigned i = 0; i < count; ++i) if (fields[i].name == name) return fields[i].value;
        return std::string_view{};
    };
    const auto scalar = [&](std::string_view key, std::uint16_t& dest, std::uint32_t limit, bool required = false) {
        const auto value = get(key);
        if (value.empty() && !required) return true;
        std::uint32_t parsed = 0;
        if (!number(value, 16, limit, parsed)) {
            error = "Invalid controller value: " + std::string(key);
            return false;
        }
        dest = static_cast<std::uint16_t>(parsed);
        return true;
    };

    rthid_mapping result{};
    result.version = RTHID_VERSION;
    result.bytes = sizeof(result);
    result.report_bytes = 1;
    result.analog_l = result.analog_r = RTHID_ABSENT;
    for (auto& button : result.buttons) button.offset = RTHID_ABSENT;
    for (auto& axis : result.axes) {
        axis.offset = RTHID_ABSENT;
        axis.scale = 800;
    }
    if (!scalar("VID", result.vid, 65535, true) || !scalar("PID", result.pid, 65535, true) ||
        !scalar("Polltype", result.polltype, 1, true) || !scalar("DPAD", result.dpad, 1) ||
        !scalar("DigitalLR", result.digital_lr, 2) || !scalar("MultiIn", result.multi_in, 1) ||
        !scalar("MultiInValue", result.multi_value, 255) ||
        !scalar("LAnalog", result.analog_l, RTHID_REPORT_MAX - 1) ||
        !scalar("RAnalog", result.analog_r, RTHID_REPORT_MAX - 1)) return false;
    const auto include_offset = [&](std::uint16_t offset) {
        if (offset != RTHID_ABSENT && result.report_bytes <= offset) result.report_bytes = offset + 1;
    };
    include_offset(result.analog_l);
    include_offset(result.analog_r);
    for (unsigned i = 0; i < kButtons.size(); ++i) {
        if (get(kButtons[i]).empty()) continue;
        std::uint32_t pair[2]{};
        if (!values(get(kButtons[i]), pair, 2, 2, false) || pair[0] >= RTHID_REPORT_MAX) {
            error = "Invalid controller button: " + std::string(kButtons[i]);
            return false;
        }
        result.buttons[i] = {static_cast<std::uint16_t>(pair[0]), static_cast<std::uint16_t>(pair[1])};
        include_offset(result.buttons[i].offset);
    }
    for (unsigned i = 0; i < kAxes.size(); ++i) {
        if (get(kAxes[i]).empty()) continue;
        std::uint32_t parts[3] = {0, 0, 80};
        if (!values(get(kAxes[i]), parts, 1, 3, true) || parts[0] >= RTHID_REPORT_MAX || parts[1] >= 128) {
            error = "Invalid controller stick: " + std::string(kAxes[i]);
            return false;
        }
        const std::uint32_t radius = parts[2] ? parts[2] : 80;
        result.axes[i] = {static_cast<std::uint16_t>(parts[0]), static_cast<std::uint16_t>(parts[1]),
                          radius * 1280u / (128u - parts[1])};
        include_offset(result.axes[i].offset);
    }
    if (result.dpad) {
        std::uint16_t mask = 0;
        const auto offset = result.buttons[RTHID_LEFT].offset;
        bool same = true;
        for (unsigned i = RTHID_LEFT; i <= RTHID_UP_LEFT; ++i) {
            if (result.buttons[i].offset == RTHID_ABSENT) {
                error = "A hat D-pad needs all eight directions.";
                return false;
            }
            same = same && result.buttons[i].offset == offset;
            mask |= result.buttons[i].mask;
        }
        if (!same) mask = 255;
        else if ((mask & 0xF0) == 0) mask = 0x0F;
        else if ((mask & 0x0F) == 0) mask = 0xF0;
        result.dpad_mask = mask;
    }
    std::uint16_t rumble = 0;
    if (!scalar("Rumble", rumble, 1) || !scalar("RumbleType", result.rumble_type, 1) ||
        !scalar("RumbleDataLen", result.rumble_bytes, RTHID_RUMBLE_MAX) ||
        !scalar("RumbleTransferLen", result.rumble_transfer, RTHID_RUMBLE_MAX) ||
        !scalar("RumbleTransfers", result.rumble_count, 16)) return false;
    if (rumble) {
        std::array<std::uint32_t, RTHID_RUMBLE_MAX> data{};
        if (!result.rumble_bytes || !values(get("RumbleDataOn"), data.data(), result.rumble_bytes, result.rumble_bytes, false)) {
            error = "Invalid controller rumble data: RumbleDataOn";
            return false;
        }
        for (unsigned i = 0; i < result.rumble_bytes; ++i) result.rumble_on[i] = static_cast<std::uint8_t>(data[i]);
        if (!values(get("RumbleDataOff"), data.data(), result.rumble_bytes, result.rumble_bytes, false)) {
            error = "Invalid controller rumble data: RumbleDataOff";
            return false;
        }
        for (unsigned i = 0; i < result.rumble_bytes; ++i) result.rumble_off[i] = static_cast<std::uint8_t>(data[i]);
    } else if (result.rumble_bytes || result.rumble_transfer || result.rumble_count ||
               !get("RumbleDataOn").empty() || !get("RumbleDataOff").empty()) {
        error = "Controller rumble data needs Rumble=1.";
        return false;
    }
    if (!rthid_valid(&result)) {
        error = "The controller mapping has invalid limits.";
        return false;
    }
    mapping = result;
    return true;
}

}  // namespace riftwii
