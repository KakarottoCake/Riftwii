// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gccontrols.hpp"

#include <cstring>

namespace riftwii {
namespace {

constexpr std::uint32_t kRemoteBit = 1u << GC_MAP_REMOTE;
constexpr std::uint32_t kNunchukBit = 1u << GC_MAP_NUNCHUK;
constexpr std::uint32_t kClassicBit = 1u << GC_MAP_CLASSIC;
constexpr std::uint32_t kEveryKind = kRemoteBit | kNunchukBit | kClassicBit;
constexpr std::uint32_t kWiiRemoteKinds = kRemoteBit | kNunchukBit;

// The headings, each a way of holding the controller.
struct KindName {
    const char* name;
    std::uint32_t kind;
    const char* controller;  // for the notes: "a Wii Remote held sideways has no ..."
};
constexpr KindName kKinds[] = {
    {"remote", GC_MAP_REMOTE, "A Wii Remote alone"},
    {"nunchuk", GC_MAP_NUNCHUK, "A Wii Remote with a Nunchuk"},
    {"classic", GC_MAP_CLASSIC, "A Classic Controller"},
};

// The buttons a line can name on the left of '=', and which kinds have
// them. The D-pad's directions are as the controller is held.
struct InputName {
    const char* name;
    std::uint32_t input;
    std::uint32_t kinds;
};
constexpr InputName kInputs[] = {
    {"up", GC_IN_UP, kEveryKind},       {"down", GC_IN_DOWN, kEveryKind},
    {"left", GC_IN_LEFT, kEveryKind},   {"right", GC_IN_RIGHT, kEveryKind},
    {"a", GC_IN_A, kEveryKind},         {"b", GC_IN_B, kEveryKind},
    {"1", GC_IN_1, kWiiRemoteKinds},    {"2", GC_IN_2, kWiiRemoteKinds},
    {"-", GC_IN_MINUS, kEveryKind},     {"minus", GC_IN_MINUS, kEveryKind},
    {"+", GC_IN_PLUS, kEveryKind},      {"plus", GC_IN_PLUS, kEveryKind},
    {"home", GC_IN_HOME, kEveryKind},   {"shake", GC_IN_SHAKE, kWiiRemoteKinds},
    {"c", GC_IN_C, kNunchukBit},        {"z", GC_IN_Z, kNunchukBit},
    {"x", GC_IN_X, kClassicBit},        {"y", GC_IN_Y, kClassicBit},
    {"l", GC_IN_L, kClassicBit},        {"r", GC_IN_R, kClassicBit},
    {"zl", GC_IN_ZL, kClassicBit},      {"zr", GC_IN_ZR, kClassicBit},
};

// What a button can press, on the right of '=': the GameCube controller's
// buttons, its D-pad, and its two sticks pushed all the way. The names
// are written here as gc_controls_describe prints them; reading ignores case.
struct TargetName {
    const char* name;
    std::uint32_t bits;
};
constexpr TargetName kTargets[] = {
    {"A", GC_PAD_A},
    {"B", GC_PAD_B},
    {"X", GC_PAD_X},
    {"Y", GC_PAD_Y},
    {"Z", GC_PAD_Z},
    {"L", GC_PAD_L},
    {"R", GC_PAD_R},
    {"Start", GC_PAD_START},
    {"Up", GC_PAD_UP},
    {"Down", GC_PAD_DOWN},
    {"Left", GC_PAD_LEFT},
    {"Right", GC_PAD_RIGHT},
    {"StickUp", GC_OUT_STICK_UP},
    {"StickDown", GC_OUT_STICK_DOWN},
    {"StickLeft", GC_OUT_STICK_LEFT},
    {"StickRight", GC_OUT_STICK_RIGHT},
    {"CStickUp", GC_OUT_CSTICK_UP},
    {"CStickDown", GC_OUT_CSTICK_DOWN},
    {"CStickLeft", GC_OUT_CSTICK_LEFT},
    {"CStickRight", GC_OUT_CSTICK_RIGHT},
};

std::string lower(const std::string& text) {
    std::string out = text;
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

std::string trim(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && is_space(text[begin])) ++begin;
    while (end > begin && is_space(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

// A name from the file as it may go into the log: printable ASCII only,
// and not too long, so a file of junk cannot fill the log with it.
std::string loggable(const std::string& word) {
    constexpr std::size_t kMaxShown = 24;
    std::string out;
    for (char c : word) {
        if (out.size() == kMaxShown) {
            out += "...";
            break;
        }
        out += (c >= 0x20 && c < 0x7F) ? c : '?';
    }
    return out;
}

const KindName* find_kind(const std::string& name) {
    const std::string wanted = lower(name);
    for (const KindName& k : kKinds)
        if (wanted == k.name) return &k;
    return nullptr;
}

const InputName* find_input(const std::string& name) {
    const std::string wanted = lower(name);
    for (const InputName& i : kInputs)
        if (wanted == i.name) return &i;
    return nullptr;
}

bool find_target(const std::string& name, std::uint32_t& bits) {
    const std::string wanted = lower(name);
    if (wanted == "none" || wanted == "nothing") {
        bits = 0;
        return true;
    }
    for (const TargetName& t : kTargets)
        if (wanted == lower(t.name)) {
            bits = t.bits;
            return true;
        }
    return false;
}

// The words after '=': split on spaces and commas.
std::vector<std::string> split_targets(const std::string& text) {
    std::vector<std::string> words;
    std::string word;
    for (char c : text) {
        if (is_space(c) || c == ',') {
            if (!word.empty()) words.push_back(word);
            word.clear();
        } else {
            word += c;
        }
    }
    if (!word.empty()) words.push_back(word);
    return words;
}

// The notes of one file, at most kMaxGcControlsNotes, then one line that
// says more were left out.
class Notes {
public:
    Notes(const std::string& name, std::vector<std::string>& out) : name_(name), out_(out) {}

    void add(std::size_t line, const std::string& text) {
        if (count_ == kMaxGcControlsNotes) {
            out_.push_back(name_ + ": more lines had problems; only the first " + std::to_string(kMaxGcControlsNotes) +
                           " are listed.");
        }
        ++count_;
        if (count_ > kMaxGcControlsNotes) return;
        std::string where = line != 0 ? " line " + std::to_string(line) : std::string();
        out_.push_back(name_ + where + ": " + text);
    }

private:
    const std::string& name_;
    std::vector<std::string>& out_;
    std::size_t count_ = 0;
};

}  // namespace

std::vector<std::string> gc_controls_paths(const std::string& game_id) {
    std::vector<std::string> paths;
    paths.push_back("sd:/riftwii/gc/controls/default.txt");
    // A game's own file only for an ID made of capitals and digits, as a
    // disc's always is: a name taken from a disc never climbs out of the
    // folder.
    bool plain_id = game_id.size() == 6;
    for (char c : game_id)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) plain_id = false;
    if (plain_id) paths.push_back("sd:/riftwii/gc/controls/" + game_id + ".txt");
    return paths;
}

void gc_controls_default(gc_map& map) {
    std::memset(&map, 0, sizeof(map));
    // A Wii Remote alone, held sideways: 2 is A and 1 is B (the two
    // buttons under the right thumb), the trigger R, A is X and HOME is
    // Y; the D-pad is both the D-pad and the control stick.
    std::uint32_t* remote = map.to[GC_MAP_REMOTE];
    remote[GC_IN_2] = GC_PAD_A;
    remote[GC_IN_1] = GC_PAD_B;
    remote[GC_IN_B] = GC_PAD_R;
    remote[GC_IN_A] = GC_PAD_X;
    remote[GC_IN_HOME] = GC_PAD_Y;
    remote[GC_IN_MINUS] = GC_PAD_Z;
    remote[GC_IN_PLUS] = GC_PAD_START;
    remote[GC_IN_UP] = GC_PAD_UP | GC_OUT_STICK_UP;
    remote[GC_IN_DOWN] = GC_PAD_DOWN | GC_OUT_STICK_DOWN;
    remote[GC_IN_LEFT] = GC_PAD_LEFT | GC_OUT_STICK_LEFT;
    remote[GC_IN_RIGHT] = GC_PAD_RIGHT | GC_OUT_STICK_RIGHT;
    // With a Nunchuk, upright: its stick is the control stick, Z and C
    // the triggers R and L, the remote's D-pad the C-stick (the camera in
    // most games). HOME presses nothing: it is the way back.
    std::uint32_t* nunchuk = map.to[GC_MAP_NUNCHUK];
    nunchuk[GC_IN_A] = GC_PAD_A;
    nunchuk[GC_IN_B] = GC_PAD_B;
    nunchuk[GC_IN_1] = GC_PAD_X;
    nunchuk[GC_IN_2] = GC_PAD_Y;
    nunchuk[GC_IN_Z] = GC_PAD_R;
    nunchuk[GC_IN_C] = GC_PAD_L;
    nunchuk[GC_IN_MINUS] = GC_PAD_Z;
    nunchuk[GC_IN_PLUS] = GC_PAD_START;
    nunchuk[GC_IN_UP] = GC_OUT_CSTICK_UP;
    nunchuk[GC_IN_DOWN] = GC_OUT_CSTICK_DOWN;
    nunchuk[GC_IN_LEFT] = GC_OUT_CSTICK_LEFT;
    nunchuk[GC_IN_RIGHT] = GC_OUT_CSTICK_RIGHT;
    // The Classic Controller is laid out like a GameCube controller: each
    // button the one of the same name, ZL and ZR both Z. Its - and HOME
    // press nothing.
    std::uint32_t* classic = map.to[GC_MAP_CLASSIC];
    classic[GC_IN_A] = GC_PAD_A;
    classic[GC_IN_B] = GC_PAD_B;
    classic[GC_IN_X] = GC_PAD_X;
    classic[GC_IN_Y] = GC_PAD_Y;
    classic[GC_IN_ZL] = GC_PAD_Z;
    classic[GC_IN_ZR] = GC_PAD_Z;
    classic[GC_IN_L] = GC_PAD_L;
    classic[GC_IN_R] = GC_PAD_R;
    classic[GC_IN_PLUS] = GC_PAD_START;
    classic[GC_IN_UP] = GC_PAD_UP;
    classic[GC_IN_DOWN] = GC_PAD_DOWN;
    classic[GC_IN_LEFT] = GC_PAD_LEFT;
    classic[GC_IN_RIGHT] = GC_PAD_RIGHT;
}

std::size_t gc_controls_apply(const std::string& text, const std::string& name, gc_map& map,
                              std::vector<std::string>& notes) {
    Notes note(name, notes);
    if (text.size() > kMaxGcControlsFile) {
        note.add(0, "the file is larger than " + std::to_string(kMaxGcControlsFile / 1024) +
                        " KB, more than any map needs, so none of it was used.");
        return 0;
    }
    const KindName* kind = nullptr;
    bool heading_unknown = false;
    std::size_t changed = 0;
    std::size_t line_number = 0;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        const std::string raw = text.substr(start, end - start);
        start = end + 1;
        ++line_number;
        if (line_number > kMaxGcControlsLines) {
            note.add(line_number, "the file has more than " + std::to_string(kMaxGcControlsLines) +
                                      " lines, so the rest of it was not read.");
            break;
        }
        if (raw.size() > kMaxGcControlsLine) {
            note.add(line_number, "the line is longer than " + std::to_string(kMaxGcControlsLine) +
                                      " characters, so it was skipped.");
            continue;
        }
        // '#' starts a comment; no name has one.
        const std::string line = trim(raw.substr(0, raw.find('#')));
        if (line.empty()) continue;

        if (line.front() == '[') {
            const std::size_t close = line.find(']');
            const std::string heading = trim(line.substr(1, close == std::string::npos ? std::string::npos : close - 1));
            kind = find_kind(heading);
            heading_unknown = kind == nullptr;
            if (heading_unknown)
                note.add(line_number, "[" + loggable(heading) +
                                          "] is not a heading RiftWii knows ([remote], [nunchuk] or [classic]), so the "
                                          "lines under it were skipped.");
            continue;
        }
        if (heading_unknown) continue;  // said once, at the heading
        if (kind == nullptr) {
            note.add(line_number, "the line comes before any heading ([remote], [nunchuk] or [classic]), so it was "
                                  "skipped.");
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            note.add(line_number, "the line has no '=' between a button and what it presses, so it was skipped.");
            continue;
        }
        const std::string input_name = trim(line.substr(0, equals));
        const InputName* input = find_input(input_name);
        if (input == nullptr) {
            note.add(line_number,
                     "\"" + loggable(input_name) + "\" is not a button RiftWii knows, so the line was skipped.");
            continue;
        }
        if (!(input->kinds & (1u << kind->kind))) {
            note.add(line_number, std::string(kind->controller) + " has no \"" + loggable(input_name) +
                                      "\", so the line was skipped.");
            continue;
        }

        const std::vector<std::string> words = split_targets(line.substr(equals + 1));
        std::uint32_t targets = 0;
        std::size_t understood = 0;
        for (std::size_t i = 0; i < words.size(); ++i) {
            if (i == kMaxGcControlsTargets) {
                note.add(line_number, "only the first " + std::to_string(kMaxGcControlsTargets) +
                                          " names after '=' were used.");
                break;
            }
            std::uint32_t bits = 0;
            if (!find_target(words[i], bits)) {
                note.add(line_number, "\"" + loggable(words[i]) +
                                          "\" is not a GameCube button or stick RiftWii knows, so it was left out.");
                continue;
            }
            targets |= bits;
            ++understood;
        }
        // Nothing after '=' clears the button, as "none" does; names that
        // were all unknown leave it as it was rather than clear it.
        if (!words.empty() && understood == 0) continue;
        map.to[kind->kind][input->input] = targets;
        ++changed;
    }
    return changed;
}

std::string gc_controls_describe(std::uint32_t targets) {
    std::string out;
    for (const TargetName& t : kTargets) {
        if (!(targets & t.bits)) continue;
        if (!out.empty()) out += ' ';
        out += t.name;
    }
    return out.empty() ? "nothing" : out;
}

}  // namespace riftwii
