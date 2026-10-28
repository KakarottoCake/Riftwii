// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/emulatorfile.hpp"
#include "riftwii/emulatorart.hpp"

#include <algorithm>
#include <set>

#include "riftwii/checkedalloc.hpp"

namespace riftwii {
namespace {

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

bool printable(const std::string& text, bool allow_tabs = false) {
    for (unsigned char c : text) {
        if (allow_tabs && c == '\t') continue;
        if (c < 32 || c == 127) return false;
    }
    return true;
}

bool identifier(const std::string& text) {
    if (text.empty() || text.size() > 32) return false;
    for (char c : text) {
        if ((c < 'a' || c > 'z') && (c < '0' || c > '9') && c != '_') return false;
    }
    return true;
}

bool number(const std::string& text, int& value) {
    if (text.empty() || text.size() > 8) return false;
    std::size_t pos = text[0] == '-' ? 1 : 0;
    if (pos == text.size()) return false;
    int result = 0;
    for (; pos < text.size(); ++pos) {
        if (text[pos] < '0' || text[pos] > '9') return false;
        result = result * 10 + text[pos] - '0';
        if (result > 1000000) return false;
    }
    value = text[0] == '-' ? -result : result;
    return std::to_string(value) == text;
}

bool relative_path(const std::string& path) {
    if (path.empty() || path.size() > kEmulatorPathMax || !printable(path) ||
        path.find_first_of("\\:") != std::string::npos) return false;
    std::size_t from = 0;
    while (from < path.size()) {
        const auto end = path.find('/', from);
        const std::string part = path.substr(from, end == std::string::npos ? end : end - from);
        if (part.empty() || part == "." || part == ".." || part.size() > 255) return false;
        if (end == std::string::npos) return true;
        from = end + 1;
    }
    return false;
}

bool split_list(const std::string& text, std::vector<std::string>& out, std::size_t limit,
                std::size_t chars) {
    out.clear();
    std::size_t from = 0;
    do {
        const auto end = text.find(',', from);
        const std::string item = trim(text.substr(from, end == std::string::npos ? end : end - from));
        if (item.empty() || item.size() > chars || !printable(item) || out.size() == limit ||
            std::find(out.begin(), out.end(), item) != out.end()) return false;
        out.push_back(item);
        if (end == std::string::npos) return true;
        from = end + 1;
    } while (true);
}

// Quotes only group template words. Expanded values are never parsed again.
bool split_words(const std::string& text, std::vector<std::string>& words) {
    words.clear();
    std::string word;
    char quote = 0;
    bool started = false;
    for (char c : text) {
        if (c == '\0' || (static_cast<unsigned char>(c) < 32 && c != '\t')) return false;
        if (c == quote && quote != 0) {
            quote = 0;
        } else if (quote == 0 && (c == '\'' || c == '"')) {
            quote = c;
            started = true;
        } else if (quote == 0 && (c == ' ' || c == '\t')) {
            if (!started) continue;
            if (words.size() == 64 || word.empty()) return false;
            words.push_back(word);
            word.clear();
            started = false;
        } else {
            word += c;
            started = true;
            if (word.size() > kEmulatorPathMax) return false;
        }
    }
    if (quote != 0) return false;
    if (started) {
        if (word.empty()) return false;
        words.push_back(word);
    }
    return words.size() <= 64;
}

const EmulatorSetting* find_setting(const EmulatorFile& file, const std::string& key) {
    for (const auto& setting : file.settings) {
        if (setting.key == key) return &setting;
    }
    return nullptr;
}

bool check_placeholders(const EmulatorFile& file, const std::string& word, bool allow_settings) {
    for (std::size_t pos = 0; pos < word.size(); ++pos) {
        if (word[pos] == '}') return false;
        if (word[pos] != '{') continue;
        const auto end = word.find('}', pos + 1);
        if (end == std::string::npos) return false;
        const std::string key = word.substr(pos + 1, end - pos - 1);
        if (key != "game" && key != "game_name" && key != "game_folder") {
            const auto* setting = allow_settings ? find_setting(file, key) : nullptr;
            if (setting == nullptr) return false;
            if (setting->type == EmulatorSettingType::Switch && word != "{" + key + "}") return false;
        }
        pos = end;
    }
    return true;
}

std::string lower(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

struct EmulatorFileFields {
    std::string format;
    std::string dol;
    std::string args;
    std::set<std::string> keys;
    std::map<std::string, std::set<std::string>> setting_keys;
};

// A null error means the key was accepted, including a future key that the
// caller reports as a warning. Repeated keys are rejected even if unknown.
const char* parse_top_level_key(EmulatorFile& file, EmulatorFileFields& fields,
                                 const std::string& key, const std::string& value, bool& known) {
    if (!fields.keys.insert(key).second) return "An emulator key is repeated.";
    if (key == "format") {
        fields.format = value;
    } else if (key == "name" || key == "system") {
        if (value.empty() || value.size() > 64 || !printable(value)) {
            return "The emulator name or system is too long or empty.";
        }
        if (key == "name") {
            file.name = value;
        } else {
            file.system = value;
        }
    } else if (key == "covers") {
        if (value.size() > 64 || !emulator_art_component(value)) {
            return "The emulator covers folder must be one safe name of at most 64 bytes.";
        }
        file.covers = value;
    } else if (key == "dol") {
        fields.dol = value;
    } else if (key == "args") {
        fields.args = value;
    } else if (key == "games") {
        if (!split_list(value, file.folders, kEmulatorFoldersMax, kEmulatorPathMax)) {
            return "The emulator game folders are invalid or exceed 8 entries.";
        }
    } else if (key == "extensions") {
        if (!split_list(value, file.extensions, kEmulatorExtensionsMax, 16)) {
            return "The emulator extensions are invalid or exceed 16 entries.";
        }
    } else {
        known = false;
    }
    return nullptr;
}

const char* parse_setting_key(EmulatorSetting& setting, std::set<std::string>& keys,
                             const std::string& key, const std::string& value, bool& known) {
    if (!keys.insert(key).second) return "An emulator setting key is repeated.";
    if (key == "label" || key == "help" || key == "default") {
        std::size_t limit = 96;
        if (key == "help") {
            limit = 256;
        } else if (key == "label") {
            limit = 64;
        }
        if (value.size() > limit || !printable(value)) {
            return "An emulator setting string is too long or contains a control character.";
        }
        if (key == "label") {
            setting.label = value;
        } else if (key == "help") {
            setting.help = value;
        } else {
            setting.default_value = value;
        }
    } else if (key == "type") {
        if (value == "choice") {
            setting.type = EmulatorSettingType::Choice;
        } else if (value == "switch") {
            setting.type = EmulatorSettingType::Switch;
        } else if (value == "number") {
            setting.type = EmulatorSettingType::Number;
        } else {
            return "An emulator setting type is unknown.";
        }
    } else if (key == "values") {
        if (!split_list(value, setting.values, kEmulatorValuesMax, 96))
            return "An emulator choice list is invalid or exceeds 32 entries.";
    } else if (key == "min" || key == "max") {
        int parsed = 0;
        if (!number(value, parsed)) return "An emulator number must be a whole number from -1000000 to 1000000.";
        if (key == "min") {
            setting.minimum = parsed;
        } else {
            setting.maximum = parsed;
        }
    } else if (key == "on") {
        if (value.size() > 1024 || !split_words(value, setting.on) || setting.on.empty())
            return "The words for an enabled emulator switch are invalid.";
    } else {
        known = false;
    }
    return nullptr;
}

const char* validate_emulator_file(EmulatorFile& file, EmulatorFileFields& fields,
                                  const std::string& app_folder) {
    int version = 0;
    if (!number(fields.format, version) || version != 1)
        return "The emulator file format is not supported; this RiftWii reads format 1.";
    if (file.name.empty() || file.system.empty() || fields.dol.empty() || file.folders.empty() || file.extensions.empty() || !fields.keys.count("args"))
        return "The emulator file is missing a required field.";
    file.dol = fields.dol.find(':') == std::string::npos ? app_folder + "/" + fields.dol : fields.dol;
    if (!emulator_path_valid(file.dol) || lower(file.dol.substr(file.dol.size() > 4 ? file.dol.size() - 4 : 0)) != ".dol")
        return "The emulator program must be a safe sd:/ or usb:/ DOL path.";
    for (const auto& folder : file.folders) {
        if (!emulator_path_valid(folder)) return "An emulator game folder is not a safe sd:/ or usb:/ path.";
    }
    for (auto& extension : file.extensions) {
        extension = lower(extension);
        if (!identifier(extension) || extension.find('_') != std::string::npos)
            return "An emulator extension must contain only letters and digits.";
    }
    for (const auto& setting : file.settings) {
        const auto& keys = fields.setting_keys[setting.key];
        if (setting.label.empty() || !keys.count("type") || !keys.count("default") ||
            !emulator_value_valid(setting, setting.default_value)) return "An emulator setting has an invalid label, type or default.";
        if (setting.type == EmulatorSettingType::Number && (!keys.count("min") || !keys.count("max") || setting.minimum > setting.maximum))
            return "An emulator number needs a valid minimum and maximum.";
        if (setting.type == EmulatorSettingType::Switch && setting.on.empty()) return "An emulator switch has no on words.";
        for (const auto& word : setting.on) {
            if (!check_placeholders(file, word, false)) return "An emulator switch contains an unknown placeholder.";
        }
    }
    if (fields.args.size() > 1024 || !split_words(fields.args, file.arguments)) return "The emulator argument template is invalid or too long.";
    for (const auto& word : file.arguments) {
        if (!check_placeholders(file, word, true)) return "The emulator arguments contain an unknown or misplaced placeholder.";
    }
    return nullptr;
}

}  // namespace

bool emulator_path_valid(const std::string& path) {
    if (path.size() > kEmulatorPathMax) return false;
    if (path.rfind("sd:/", 0) != 0 && path.rfind("usb:/", 0) != 0) return false;
    return relative_path(path.substr(path.find('/') + 1));
}

bool emulator_value_valid(const EmulatorSetting& setting, const std::string& value) {
    if (value.empty() || value.size() > 96 || !printable(value)) return false;
    if (setting.type == EmulatorSettingType::Switch) return value == "on" || value == "off";
    if (setting.type == EmulatorSettingType::Choice)
        return std::find(setting.values.begin(), setting.values.end(), value) != setting.values.end();
    int parsed = 0;
    return number(value, parsed) && parsed >= setting.minimum && parsed <= setting.maximum;
}

bool parse_emulator_file(const std::string& text, const std::string& app_folder,
                         EmulatorFile& out, std::string& error, std::vector<std::string>& warnings) {
    out = {};
    error.clear();
    warnings.clear();
    const auto fail = [&](const std::string& why) {
        error = why;
        return false;
    };
    if (text.empty() || text.size() > kEmulatorFileMax) return fail("The emulator file must be 1 to 16384 bytes.");
    if (!emulator_path_valid(app_folder) ||
        (app_folder.rfind("sd:/apps/", 0) != 0 && app_folder.rfind("usb:/apps/", 0) != 0))
        return fail("The emulator folder must be inside sd:/apps or usb:/apps.");
    if (!can_allocate(text.size() * 8)) return fail("There is not enough memory to read the emulator file.");
    EmulatorFile file;
    EmulatorSetting* current = nullptr;
    EmulatorFileFields fields;
    bool unknown_section = false;
    std::size_t from = 0;
    while (from < text.size()) {
        const auto end = text.find('\n', from);
        const std::size_t length = end == std::string::npos ? text.size() - from : end - from;
        if (length > 2048) return fail("A line in the emulator file is too long.");
        const std::string line = trim(text.substr(from, length));
        from = end == std::string::npos ? text.size() : end + 1;
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (!printable(line, true)) return fail("The emulator file contains a control character.");
        if (line[0] == '[') {
            current = nullptr;
            unknown_section = true;
            if (line.size() < 3 || line.back() != ']') return fail("An emulator section is incomplete.");
            const std::string section = line.substr(1, line.size() - 2);
            if (section.rfind("setting ", 0) != 0) {
                if (warnings.size() < 32) warnings.push_back("An unknown emulator section was ignored: " + section.substr(0, 64));
                continue;
            }
            const std::string key = trim(section.substr(8));
            if (!identifier(key) || key == "game" || key == "game_name" || key == "game_folder" ||
                find_setting(file, key) != nullptr) return fail("An emulator setting name is invalid or repeated.");
            if (file.settings.size() == kEmulatorSettingsMax) return fail("The emulator declares more than 16 settings.");
            file.settings.emplace_back();
            current = &file.settings.back();
            current->key = key;
            unknown_section = false;
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos) return fail("An emulator line has no equals sign.");
        const std::string key = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));
        if (!identifier(key)) return fail("An emulator key is invalid.");
        if (unknown_section) continue;
        bool known = true;
        const char* key_error = nullptr;
        if (current == nullptr) {
            key_error = parse_top_level_key(file, fields, key, value, known);
        } else {
            key_error = parse_setting_key(*current, fields.setting_keys[current->key], key, value, known);
        }
        if (key_error != nullptr) {
            return fail(key_error);
        }
        if (!known && warnings.size() < 32) warnings.push_back("An unknown emulator key was ignored: " + key);
    }
    if (const char* why = validate_emulator_file(file, fields, app_folder)) {
        return fail(why);
    }
    out = std::move(file);
    return true;
}

std::string emulator_game_name(const std::string& path) {
    const auto slash = path.find_last_of('/');
    const std::size_t from = slash == std::string::npos ? 0 : slash + 1;
    const auto dot = path.find_last_of('.');
    return path.substr(from, dot != std::string::npos && dot > from ? dot - from : std::string::npos);
}

bool emulator_game_matches(const EmulatorFile& emulator, const std::string& path) {
    if (!emulator_path_valid(path)) return false;
    bool inside = false;
    for (const auto& folder : emulator.folders) {
        if (path.rfind(folder + "/", 0) == 0) inside = true;
    }
    const auto dot = path.find_last_of('.');
    if (!inside || dot == std::string::npos) return false;
    const auto extension = lower(path.substr(dot + 1));
    return std::find(emulator.extensions.begin(), emulator.extensions.end(), extension) != emulator.extensions.end();
}

EmulatorValues emulator_defaults(const EmulatorFile& emulator) {
    EmulatorValues values;
    for (const auto& setting : emulator.settings) values[setting.key] = setting.default_value;
    return values;
}

void read_emulator_values(const EmulatorFile& emulator, const std::string& text, EmulatorValues& values) {
    if (text.size() > kEmulatorFileMax) return;
    std::size_t from = 0;
    while (from < text.size()) {
        const auto end = text.find('\n', from);
        const auto line = text.substr(from, end == std::string::npos ? end : end - from);
        from = end == std::string::npos ? text.size() : end + 1;
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const auto* setting = find_setting(emulator, trim(line.substr(0, equals)));
        const auto value = trim(line.substr(equals + 1));
        if (setting && emulator_value_valid(*setting, value)) values[setting->key] = value;
    }
}

std::string write_emulator_values(const EmulatorFile& emulator, const EmulatorValues& values) {
    std::string text;
    for (const auto& setting : emulator.settings) {
        const auto found = values.find(setting.key);
        if (found != values.end() && emulator_value_valid(setting, found->second))
            text += setting.key + " = " + found->second + "\n";
    }
    return text;
}

std::string step_emulator_value(const EmulatorSetting& setting, const std::string& value, int direction) {
    if (setting.type == EmulatorSettingType::Switch) return value == "on" ? "off" : "on";
    if (setting.type == EmulatorSettingType::Number) {
        int current = setting.minimum;
        number(value, current);
        current += direction < 0 ? -1 : 1;
        if (current < setting.minimum) current = setting.maximum;
        if (current > setting.maximum) current = setting.minimum;
        return std::to_string(current);
    }
    if (setting.values.empty()) return setting.default_value;
    const auto found = std::find(setting.values.begin(), setting.values.end(), value);
    const auto index = found == setting.values.end() ? 0 : found - setting.values.begin();
    const auto count = static_cast<std::ptrdiff_t>(setting.values.size());
    return setting.values[static_cast<std::size_t>((index + (direction < 0 ? count - 1 : 1)) % count)];
}

bool build_emulator_arguments(const EmulatorFile& emulator, const std::string& game,
                              const EmulatorValues& values, std::vector<std::string>& argv,
                              std::string& command_line, std::string& error) {
    argv.clear();
    command_line.clear();
    error.clear();
    if (!emulator_path_valid(emulator.dol) || !emulator_game_matches(emulator, game)) {
        error = "The emulator program or game path is invalid.";
        return false;
    }
    EmulatorValues replacements = emulator_defaults(emulator);
    for (const auto& item : values) {
        const auto* setting = find_setting(emulator, item.first);
        if (!setting || !emulator_value_valid(*setting, item.second)) {
            error = "An emulator setting is not declared or has an invalid value.";
            return false;
        }
        replacements[item.first] = item.second;
    }
    replacements["game"] = game;
    replacements["game_name"] = emulator_game_name(game);
    replacements["game_folder"] = game.substr(0, game.find_last_of('/'));
    std::vector<std::string> ready{emulator.dol};
    std::size_t bytes = emulator.dol.size() + 2;
    std::set<std::string> switches;
    const auto expand = [&](const std::string& word, std::string& result) {
        result.clear();
        for (std::size_t pos = 0; pos < word.size();) {
            if (word[pos] != '{') {
                result += word[pos++];
                continue;
            }
            const auto end = word.find('}', pos + 1);
            if (end == std::string::npos) return false;
            const auto found = replacements.find(word.substr(pos + 1, end - pos - 1));
            if (found == replacements.end()) return false;
            if (found->second.size() > kEmulatorCommandMax - result.size()) return false;
            result += found->second;
            pos = end + 1;
        }
        return result.find('\0') == std::string::npos;
    };
    const auto append_word = [&](const std::string& word) {
        std::string result;
        if (!expand(word, result) || result.size() + 1 > kEmulatorCommandMax - bytes) {
            error = "The emulator arguments exceed the 1024-byte limit. Nothing was started.";
            return false;
        }
        bytes += result.size() + 1;
        if (!try_push_back(ready, std::move(result))) {
            error = "There is not enough memory for the emulator arguments.";
            return false;
        }
        return true;
    };
    const auto add_switch = [&](const EmulatorSetting& setting) {
        if (!switches.insert(setting.key).second) return true;
        if (replacements[setting.key] != "on") return true;
        for (const auto& word : setting.on) {
            if (!append_word(word)) return false;
        }
        return true;
    };
    for (const auto& word : emulator.arguments) {
        const auto* setting = word.size() > 2 && word.front() == '{' && word.back() == '}'
            ? find_setting(emulator, word.substr(1, word.size() - 2)) : nullptr;
        if (setting && setting->type == EmulatorSettingType::Switch) {
            if (!add_switch(*setting)) return false;
        } else if (!append_word(word)) {
            return false;
        }
    }
    for (const auto& setting : emulator.settings) {
        if (setting.type == EmulatorSettingType::Switch && !add_switch(setting)) return false;
    }
    std::string line;
    if (!try_reserve(line, bytes)) {
        error = "There is not enough memory for the emulator arguments.";
        return false;
    }
    for (const auto& word : ready) {
        line += word;
        line.push_back('\0');
    }
    line.push_back('\0');
    argv = std::move(ready);
    command_line = std::move(line);
    return true;
}

}  // namespace riftwii
