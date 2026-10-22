// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/appmeta.hpp"

#include <cctype>
#include <cstddef>
#include <utility>

#include <pugixml.hpp>

namespace riftwii {
namespace {

constexpr std::size_t kNameMax = 64;
constexpr std::size_t kValueMax = 48;
constexpr std::size_t kDescriptionMax = 200;
constexpr std::size_t kArgumentMax = 96;
constexpr std::size_t kArgumentsMax = 16;

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string cleaned(const char* text, std::size_t most) {
    if (text == nullptr) return "";
    std::string out(text);
    std::size_t from = 0;
    while (from < out.size() && is_space(out[from])) ++from;
    std::size_t to = out.size();
    while (to > from && is_space(out[to - 1])) --to;
    out = out.substr(from, to - from);
    const std::size_t nul = out.find('\0');
    if (nul != std::string::npos) out.resize(nul);
    if (out.size() > most) {
        out.resize(most);
        while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80) out.pop_back();
        while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0xC0) out.pop_back();
    }
    return out;
}

std::string digits_of(const std::string& text) {
    std::string out;
    for (const char c : text)
        if (c >= '0' && c <= '9') out.push_back(c);
    return out;
}

int number(const std::string& text, std::size_t from, std::size_t count) {
    int value = 0;
    for (std::size_t i = 0; i < count; ++i) value = value * 10 + (text[from + i] - '0');
    return value;
}

}

bool parse_app_meta(const std::string& text, AppMeta& out) {
    out = AppMeta();
    if (text.empty()) return false;
    pugi::xml_document doc;
    const pugi::xml_parse_result result = doc.load_buffer(text.data(), text.size());
    if (!result) return false;
    const pugi::xml_node app = doc.child("app");
    if (!app) {
        out.valid = true;
        return true;
    }
    out.valid = true;
    out.name = cleaned(app.child_value("name"), kNameMax);
    out.coder = cleaned(app.child_value("coder"), kValueMax);
    out.version = cleaned(app.child_value("version"), kValueMax);
    out.release_date = cleaned(app.child_value("release_date"), kValueMax);
    out.description = cleaned(app.child_value("short_description"), kDescriptionMax);
    if (out.description.empty()) out.description = cleaned(app.child_value("long_description"), kDescriptionMax);
    std::size_t total = 0;
    for (pugi::xml_node arg = app.child("arguments").child("arg"); arg;
         arg = arg.next_sibling("arg")) {
        if (out.arguments.size() >= kArgumentsMax) break;
        std::string value = cleaned(arg.child_value(), kArgumentMax);
        if (value.empty()) continue;
        if (total + value.size() + 1 > kAppCommandLineMax / 2) break;
        total += value.size() + 1;
        out.arguments.push_back(std::move(value));
    }
    return true;
}

std::string app_display_name(const AppMeta& meta, const std::string& folder) {
    if (!meta.name.empty()) return meta.name;
    std::string name = folder;
    for (char& c : name)
        if (c == '_') c = ' ';
    if (!name.empty() && name[0] >= 'a' && name[0] <= 'z') name[0] = static_cast<char>(name[0] - 'a' + 'A');
    return name;
}

std::string app_release_date(const std::string& raw) {
    const std::string digits = digits_of(raw);
    if (digits.size() != 8 && digits.size() != 12 && digits.size() != 14) return "";
    const int year = number(digits, 0, 4), month = number(digits, 4, 2), day = number(digits, 6, 2);
    if (year < 1900 || month < 1 || month > 12 || day < 1 || day > 31) return "";
    const auto two = [](int value) {
        std::string digits = std::to_string(value);
        return digits.size() < 2 ? "0" + digits : digits;
    };
    return two(day) + "/" + two(month) + "/" + std::to_string(year);
}

std::string app_command_line(const std::string& path, const std::vector<std::string>& arguments) {
    // argv[0] must name the exact binary, never a truncated path.
    if (path.empty() || path.size() > kAppCommandLineMax - 2 || path.find('\0') != std::string::npos)
        return "";
    std::string line = path;
    for (const std::string& argument : arguments) {
        if (line.empty()) break;
        if (line.size() + argument.size() + 4 > kAppCommandLineMax) break;
        line.push_back('\0');
        line += argument;
    }
    line.push_back('\0');
    line.push_back('\0');
    return line;
}

}
