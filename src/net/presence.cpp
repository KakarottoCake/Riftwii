// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/presence.hpp"

#include <cstdio>

#include "riftwii/textlines.hpp"

namespace riftwii {
namespace {

constexpr std::size_t kMaxValueBytes = 128;
constexpr std::size_t kMaxMods = 8;

const char* event_name(PresenceEvent event) {
    switch (event) {
    case PresenceEvent::Game: return "game";
    case PresenceEvent::Bye: return "bye";
    case PresenceEvent::Menu: break;
    }
    return "menu";
}

// `text` on one line and at most kMaxValueBytes: control characters become
// spaces, and a cut never splits a UTF-8 character (Discord refuses text
// that is not valid UTF-8).
std::string one_line(const std::string& text) {
    std::string out;
    for (unsigned char c : text) out += c < 0x20 || c == 0x7F ? ' ' : static_cast<char>(c);
    if (out.size() <= kMaxValueBytes) return out;
    std::size_t cut = kMaxValueBytes;
    while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
    out.resize(cut);
    return out;
}

void add_line(std::string& packet, const char* key, const std::string& value) {
    if (!value.empty()) packet += std::string(key) + " " + one_line(value) + "\n";
}

}  // namespace

std::string presence_packet(const PresenceMessage& message) {
    std::string packet = std::string(kPresenceMagic) + "\n";
    packet += std::string("event ") + event_name(message.event) + "\n";
    add_line(packet, "console", message.console);
    add_line(packet, "version", message.version);
    if (message.event == PresenceEvent::Game) {
        add_line(packet, "id", message.game_id);
        add_line(packet, "title", message.title);
        add_line(packet, "platform", message.platform);
        add_line(packet, "system", message.system);
        if (message.restart) packet += "restart 1\n";
    }
    if (message.event == PresenceEvent::Menu && message.next_seconds > 0)
        packet += "next " + std::to_string(message.next_seconds) + "\n";
    // Every line above is bounded, so only the mods can run past the end.
    std::size_t mods = 0;
    for (const std::string& mod : message.event == PresenceEvent::Game ? message.mods : std::vector<std::string>()) {
        const std::string line = "mod " + one_line(mod) + "\n";
        if (mod.empty() || mods == kMaxMods || packet.size() + line.size() > kPresenceMaxBytes) break;
        packet += line;
        ++mods;
    }
    return packet;
}

bool parse_presence_packet(const std::string& packet, PresenceMessage& out) {
    out = PresenceMessage{};
    if (packet.size() > kPresenceMaxBytes) return false;
    LineReader in(packet);
    std::string line;
    if (!in.next(line) || line != kPresenceMagic) return false;
    bool has_event = false;
    while (in.next(line)) {
        const std::size_t space = line.find(' ');
        if (space == std::string::npos) continue;
        const std::string key = line.substr(0, space);
        const std::string value = line.substr(space + 1);
        if (key == "event") {
            has_event = true;
            if (value == "menu") out.event = PresenceEvent::Menu;
            else if (value == "game") out.event = PresenceEvent::Game;
            else if (value == "bye") out.event = PresenceEvent::Bye;
            else return false;
        } else if (key == "console") {
            out.console = value;
        } else if (key == "version") {
            out.version = value;
        } else if (key == "id") {
            out.game_id = value;
        } else if (key == "title") {
            out.title = value;
        } else if (key == "platform") {
            out.platform = value;
        } else if (key == "system") {
            out.system = value;
        } else if (key == "mod") {
            if (out.mods.size() < kMaxMods) out.mods.push_back(value);
        } else if (key == "restart") {
            out.restart = value == "1";
        } else if (key == "next") {
            unsigned seconds = 0;
            for (char c : value) {
                if (c < '0' || c > '9' || seconds > 86400) return false;
                seconds = seconds * 10 + unsigned(c - '0');
            }
            out.next_seconds = seconds;
        }
    }
    if (!has_event) return false;
    // A game without its ID cannot be shown.
    return out.event != PresenceEvent::Game || !out.game_id.empty();
}

std::string presence_console_tag(std::uint32_t device_id) {
    // FNV-1a over the ID's bytes: the same Wii always gets the same tag.
    // It only tells Wiis apart; it is no secret (32 bits can be tried
    // through), just not the ID as the Wii's own screens show it.
    std::uint32_t hash = 2166136261u;
    for (int shift = 24; shift >= 0; shift -= 8) {
        hash ^= (device_id >> shift) & 0xFF;
        hash *= 16777619u;
    }
    char text[9];
    std::snprintf(text, sizeof(text), "%08x", static_cast<unsigned>(hash));
    return text;
}

}  // namespace riftwii
