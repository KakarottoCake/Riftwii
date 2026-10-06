// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/theme.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace riftwii {
namespace {

std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t");
    if (first == std::string::npos) return std::string();
    const auto last = s.find_last_not_of(" \t");
    return s.substr(first, last - first + 1);
}

std::string to_lower(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c + ('a' - 'A'));
        }
    }
    return out;
}

void add_note(std::vector<std::string>& notes, int line_num, const std::string& msg) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "theme.ini line %d: ", line_num);
    notes.push_back(std::string(buf) + msg);
}

bool parse_decimal(const std::string& s, float& out) {
    if (s.empty()) return false;
    std::size_t i = 0;
    bool neg = false;
    if (s[i] == '-') {
        neg = true;
        ++i;
    } else if (s[i] == '+') {
        ++i;
    }
    if (i >= s.size()) return false;

    const std::size_t start_digits = i;
    double integer_part = 0.0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        integer_part = integer_part * 10.0 + (s[i] - '0');
        ++i;
    }
    const bool has_int_digits = (i > start_digits);

    double frac_num = 0.0;
    double divisor = 1.0;
    bool has_frac_digits = false;
    bool has_dot = false;

    if (i < s.size() && s[i] == '.') {
        has_dot = true;
        ++i;
        const std::size_t start_frac = i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            frac_num = frac_num * 10.0 + (s[i] - '0');
            divisor *= 10.0;
            ++i;
        }
        has_frac_digits = (i > start_frac);
    }

    if (i != s.size()) return false;
    if (!has_int_digits && !has_frac_digits) return false;
    if (has_dot && !has_frac_digits) return false;

    double result = integer_part + (frac_num / divisor);
    if (neg) result = -result;
    out = static_cast<float>(result);
    return true;
}

enum class Section {
    None,
    Theme,
    Colors,
    Shape,
    Backdrop,
    Unknown,
};

}  // namespace

Theme default_theme() {
    return Theme{};
}

bool parse_theme_color(const std::string& text, ThemeColor& out) {
    std::size_t offset = 0;
    if (!text.empty() && text[0] == '#') {
        offset = 1;
    }
    const std::size_t hex_len = text.size() - offset;
    if (hex_len != 6 && hex_len != 8) {
        return false;
    }
    for (std::size_t i = offset; i < text.size(); ++i) {
        const char c = text[i];
        const bool is_hex = (c >= '0' && c <= '9') ||
                            (c >= 'a' && c <= 'f') ||
                            (c >= 'A' && c <= 'F');
        if (!is_hex) {
            return false;
        }
    }
    auto hex_val = [](char c) -> unsigned int {
        if (c >= '0' && c <= '9') return static_cast<unsigned int>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned int>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<unsigned int>(c - 'A' + 10);
        return 0;
    };
    auto parse_byte = [&](std::size_t idx) -> std::uint8_t {
        return static_cast<std::uint8_t>((hex_val(text[idx]) << 4) | hex_val(text[idx + 1]));
    };

    ThemeColor parsed;
    parsed.r = parse_byte(offset);
    parsed.g = parse_byte(offset + 2);
    parsed.b = parse_byte(offset + 4);
    if (hex_len == 8) {
        parsed.a = parse_byte(offset + 6);
    } else {
        parsed.a = 255;
    }
    out = parsed;
    return true;
}

std::string format_theme_color(const ThemeColor& c) {
    char buf[12];
    if (c.a == 255) {
        std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.r, c.g, c.b);
    } else {
        std::snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", c.r, c.g, c.b, c.a);
    }
    return std::string(buf);
}

Theme parse_theme(const std::string& text, std::vector<std::string>& notes) {
    Theme theme = default_theme();
    Section section = Section::None;

    std::size_t at = 0;
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        at = 3;
    }

    int line_num = 0;
    while (at < text.size()) {
        ++line_num;
        std::size_t end = text.find('\n', at);
        const std::size_t next = (end == std::string::npos) ? text.size() : end + 1;
        if (end == std::string::npos) end = text.size();

        std::size_t len = end - at;
        if (len > 0 && text[at + len - 1] == '\r') {
            --len;
        }
        const std::string raw_line = text.substr(at, len);
        at = next;

        const std::string line = trim(raw_line);
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }

        if (line.front() == '[' && line.back() == ']' && line.size() >= 2) {
            const std::string sec_name = to_lower(trim(line.substr(1, line.size() - 2)));
            if (sec_name == "theme") {
                section = Section::Theme;
            } else if (sec_name == "colors") {
                section = Section::Colors;
            } else if (sec_name == "shape") {
                section = Section::Shape;
            } else if (sec_name == "backdrop") {
                section = Section::Backdrop;
            } else {
                section = Section::Unknown;
                add_note(notes, line_num, "unknown section [" + sec_name + "]; ignored");
            }
            continue;
        }

        if (section == Section::Unknown) {
            continue;
        }

        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            add_note(notes, line_num, "line without '=': \"" + line + "\"; ignored");
            continue;
        }

        const std::string key = to_lower(trim(line.substr(0, eq)));
        std::string val = trim(line.substr(eq + 1));
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
            val = val.substr(1, val.size() - 2);
        }

        if (section == Section::None) {
            add_note(notes, line_num, "key '" + key + "' before any section header; ignored");
            continue;
        }

        if (section == Section::Theme) {
            if (key == "name") {
                if (val.size() > 64) {
                    theme.name = val.substr(0, 64);
                    add_note(notes, line_num, "name longer than 64 characters; cut to 64");
                } else {
                    theme.name = val;
                }
            } else if (key == "author") {
                if (val.size() > 64) {
                    theme.author = val.substr(0, 64);
                    add_note(notes, line_num, "author longer than 64 characters; cut to 64");
                } else {
                    theme.author = val;
                }
            } else {
                add_note(notes, line_num, "unknown key '" + key + "' in [theme]; ignored");
            }
        } else if (section == Section::Colors) {
            const ThemeColorKey* found = nullptr;
            for (const auto& entry : theme_color_keys()) {
                if (key == entry.key) {
                    found = &entry;
                    break;
                }
            }
            if (!found) {
                add_note(notes, line_num, "unknown colour key '" + key + "'; ignored");
            } else {
                ThemeColor c;
                if (parse_theme_color(val, c)) {
                    theme.colors.*(found->field) = c;
                } else {
                    add_note(notes, line_num, key + ": \"" + val + "\" is not a colour (#RRGGBB or #RRGGBBAA); the default stays");
                }
            }
        } else if (section == Section::Shape) {
            if (key == "corners") {
                float corners_val = 0.0f;
                if (!parse_decimal(val, corners_val)) {
                    theme.corners = 1.0f;
                    add_note(notes, line_num, "corners: \"" + val + "\" is not a number; the default 1.0 stays");
                } else if (corners_val < 0.0f) {
                    theme.corners = 0.0f;
                    add_note(notes, line_num, "corners out of range; clamped to 0");
                } else if (corners_val > 2.0f) {
                    theme.corners = 2.0f;
                    add_note(notes, line_num, "corners out of range; clamped to 2");
                } else {
                    theme.corners = corners_val;
                }
            } else if (key == "gloss") {
                const std::string lower_val = to_lower(val);
                if (lower_val == "yes" || lower_val == "true" || lower_val == "on" || lower_val == "1") {
                    theme.gloss = true;
                } else if (lower_val == "no" || lower_val == "false" || lower_val == "off" || lower_val == "0") {
                    theme.gloss = false;
                } else {
                    add_note(notes, line_num, "gloss: \"" + val + "\" is not a boolean (yes/no/true/false/on/off/1/0); the default stays");
                }
            } else {
                add_note(notes, line_num, "unknown key '" + key + "' in [shape]; ignored");
            }
        } else if (section == Section::Backdrop) {
            if (key == "stripes") {
                const std::string lower_val = to_lower(val);
                if (lower_val == "yes" || lower_val == "true" || lower_val == "on" || lower_val == "1") {
                    theme.stripes = true;
                } else if (lower_val == "no" || lower_val == "false" || lower_val == "off" || lower_val == "0") {
                    theme.stripes = false;
                } else {
                    theme.stripes = true;
                    add_note(notes, line_num, "stripes: \"" + val + "\" is not a boolean (yes/no/true/false/on/off/1/0); the default stays");
                }
            } else {
                add_note(notes, line_num, "unknown key '" + key + "' in [backdrop]; ignored");
            }
        }
    }

    return theme;
}

const std::vector<ThemeColorKey>& theme_color_keys() {
    static const std::vector<ThemeColorKey> keys = {
        {"ink", &ThemeColors::ink},
        {"ink_soft", &ThemeColors::ink_soft},
        {"ink_dim", &ThemeColors::ink_dim},
        {"clock", &ThemeColors::clock},
        {"accent", &ThemeColors::accent},
        {"accent_ink", &ThemeColors::accent_ink},
        {"text_on_accent", &ThemeColors::text_on_accent},
        {"warn", &ThemeColors::warn},
        {"card", &ThemeColors::card},
        {"card_edge", &ThemeColors::card_edge},
        {"card_edge_strong", &ThemeColors::card_edge_strong},
        {"shadow", &ThemeColors::shadow},
        {"glow", &ThemeColors::glow},
        {"glyph", &ThemeColors::glyph},
        {"chip_on", &ThemeColors::chip_on},
        {"chip_off", &ThemeColors::chip_off},
        {"chip_off_edge", &ThemeColors::chip_off_edge},
        {"switch_off", &ThemeColors::switch_off},
        {"bar", &ThemeColors::bar},
        {"backdrop", &ThemeColors::backdrop},
        {"backdrop_stripe", &ThemeColors::backdrop_stripe},
        {"banner_stripe", &ThemeColors::banner_stripe},
        {"divider", &ThemeColors::divider},
        {"scroll_track", &ThemeColors::scroll_track},
        {"scroll_thumb", &ThemeColors::scroll_thumb},
        {"badge", &ThemeColors::badge},
        {"shelf", &ThemeColors::shelf},
        {"shelf_edge", &ThemeColors::shelf_edge},
        {"pointer1", &ThemeColors::pointer1},
        {"pointer2", &ThemeColors::pointer2},
        {"pointer3", &ThemeColors::pointer3},
        {"pointer4", &ThemeColors::pointer4},
    };
    return keys;
}

const std::vector<ThemeImage>& theme_images() {
    static const std::vector<ThemeImage> images = {
        {"background", 640, 480},
        {"tile", 140, 100},
        {"tile_over", 140, 100},
        {"cover_tile", 96, 128},
        {"cover_tile_over", 96, 128},
        {"round_button", 80, 80},
        {"round_button_over", 80, 80},
        {"pill", 252, 60},
        {"pill_over", 252, 60},
        {"pill_primary", 252, 60},
        {"pill_primary_over", 252, 60},
        {"home_button", 264, 88},
        {"home_button_over", 264, 88},
        {"chip_off", 212, 36},
        {"chip_on", 212, 36},
        {"row_focus", 548, 44},
        {"step_back", 44, 44},
        {"step_back_over", 44, 44},
        {"step_forward", 44, 44},
        {"step_forward_over", 44, 44},
        {"switch_on", 68, 40},
        {"switch_off", 68, 40},
        {"panel_game", 580, 240},
        {"panel_settings", 580, 284},
        {"bar", 640, 124},
        {"banner_stripes", 640, 192},
        {"arrow_left", 48, 48},
        {"arrow_left_over", 48, 48},
        {"arrow_right", 48, 48},
        {"arrow_right_over", 48, 48},
        {"scroll_up", 44, 44},
        {"scroll_up_over", 44, 44},
        {"scroll_down", 44, 44},
        {"scroll_down_over", 44, 44},
        {"icon_drives", 28, 28},
        {"icon_gear", 28, 28},
        {"icon_search", 28, 28},
        {"icon_disc", 40, 40},
        {"pointer1", 96, 96},
        {"pointer2", 96, 96},
        {"pointer3", 96, 96},
        {"pointer4", 96, 96},
        {"shelf", 256, 64},
        // A widescreen menu's: 856 across is 640 on a 16:9 TV's 4:3 middle.
        {"background_wide", 856, 480},
        {"bar_wide", 856, 124},
    };
    return images;
}

}  // namespace riftwii
