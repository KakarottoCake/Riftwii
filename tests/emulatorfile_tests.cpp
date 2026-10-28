// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/emulatorfile.hpp"

#include <algorithm>
#include <iostream>
#include <string>

namespace {
int failures = 0;
#define EXPECT(condition) do { if (!(condition)) { std::cerr << __LINE__ << ": " #condition "\n"; ++failures; } } while (0)

const std::string base =
    "format = 1\nname = Example\nsystem = Dreamcast\ndol = boot.dol\n"
    "games = sd:/roms/dreamcast, usb:/roms/dreamcast\nextensions = cdi, gdi, chd\n"
    "args = -game {game} -preset {preset} \"name={game_name}\" '{game_folder}' {speed}\n"
    "[setting preset]\nlabel = Preset\nhelp = Graphics fixes.\ntype = choice\n"
    "values = auto, power stone\ndefault = auto\n"
    "[setting frameskip]\nlabel = Frame skip\ntype = switch\non = -frameskip \"two words\"\ndefault = off\n"
    "[setting speed]\nlabel = Speed\ntype = number\nmin = -2\nmax = 3\ndefault = 0\n";

bool parse(const std::string& text, riftwii::EmulatorFile& file) {
    std::string error;
    std::vector<std::string> warnings;
    return riftwii::parse_emulator_file(text, "sd:/apps/example", file, error, warnings);
}

std::string replaced(const std::string& text, const std::string& before, const std::string& after) {
    std::string copy = text;
    const auto at = copy.find(before);
    EXPECT(at != std::string::npos);
    if (at != std::string::npos) copy.replace(at, before.size(), after);
    return copy;
}

void test_parsing() {
    riftwii::EmulatorFile file;
    EXPECT(parse(base, file));
    EXPECT(file.dol == "sd:/apps/example/boot.dol");
    EXPECT(file.settings.size() == 3);
    EXPECT(file.folders.size() == 2);
    EXPECT(file.arguments.size() == 7);
    const std::vector<std::pair<std::string, std::string>> bad = {
        {"format = 1", "format = 2"}, {"format = 1", "format = 0"},
        {"format = 1", "format = banana"}, {"name = Example", "name ="},
        {"system = Dreamcast", "system = " + std::string(65, 'x')},
        {"dol = boot.dol", "dol = ../boot.dol"}, {"dol = boot.dol", "dol = /boot.dol"},
        {"dol = boot.dol", "dol = nand:/boot.dol"}, {"dol = boot.dol", "dol = usb:/apps/x/../boot.dol"},
        {"dol = boot.dol", "dol = boot.elf"},
        {"sd:/roms/dreamcast, usb:/roms/dreamcast", "sd:/roms/../private"},
        {"sd:/roms/dreamcast, usb:/roms/dreamcast", "roms"},
        {"sd:/roms/dreamcast, usb:/roms/dreamcast", "sd:\\roms"},
        {"cdi, gdi, chd", "cdi, .chd"}, {"cdi, gdi, chd", "cdi,,chd"},
        {"type = choice", "type = text"}, {"default = auto", "default = arbitrary"},
        {"default = off", "default = true"}, {"min = -2", "min = 1000001"},
        {"max = 3", "max = -3"}, {"default = 0", "default = 4"},
        {"min = -2\n", ""}, {"label = Speed", "label = " + std::string(65, 'x')},
        {"[setting speed]", "[setting preset]"}, {"[setting speed]", "[setting game]"},
        {"[setting speed]", "[setting speed"}, {"{game}", "{unknown}"},
        {"{speed}", "prefix{frameskip}"}, {"{speed}", "{broken"}, {"{speed}", "broken}"},
        {"args = -game", "args = \"-game"}, {"on = -frameskip", "on = {preset}"},
        {"format = 1", "format = 1\nformat = 1"}, {"format = 1", "format 1"},
        {"default = auto", "default = " + std::string(97, 'x')},
        {"help = Graphics fixes.", "help = " + std::string(257, 'x')},
        {"{speed}", std::string(1025, 'x')},
        {"{speed}", "\"\""}, {"on = -frameskip \"two words\"", "on = "},
        {"format = 1\n", ""}, {"name = Example\n", ""}, {"system = Dreamcast\n", ""},
        {"dol = boot.dol\n", ""}, {"extensions = cdi, gdi, chd\n", ""},
        {"type = choice\n", ""}, {"default = off\n", ""},
        {"values = auto, power stone", "values = auto, auto"},
        {"max = 3", "max = 03"}, {"max = 3", "max = +3"}, {"default = 0", "default = -0"},
    };
    for (const auto& change : bad) {
        EXPECT(!parse(replaced(base, change.first, change.second), file));
        EXPECT(file.name.empty());
    }
    EXPECT(!parse("", file));
    EXPECT(!parse(std::string(16385, 'x'), file));
    EXPECT(!parse(base + std::string(2100, 'x'), file));
    EXPECT(!parse(base + std::string("key = a\0b\n", 11), file));
    std::string error;
    std::vector<std::string> warnings;
    EXPECT(riftwii::parse_emulator_file("unknown = value\n" + base + "extra = ok\n[future]\na = b\n",
                                       "usb:/apps/test", file, error, warnings));
    EXPECT(warnings.size() == 3);
    EXPECT(!riftwii::parse_emulator_file(base, "sd:/other", file, error, warnings));
    EXPECT(parse(replaced(base, "boot.dol", "usb:/apps/other/start.dol"), file));
    EXPECT(parse(replaced(base, "-game {game}", "-game\t{game}"), file));
    std::string crlf;
    for (char c : base) crlf += c == '\n' ? "\r\n" : std::string(1, c);
    EXPECT(parse(crlf, file));

    const auto list = [](unsigned count, const std::string& prefix) {
        std::string result;
        for (unsigned i = 0; i < count; ++i) {
            if (i != 0) result += ",";
            result += prefix + std::to_string(i);
        }
        return result;
    };
    EXPECT(!parse(replaced(base, "cdi, gdi, chd", list(17, "ext")), file));
    EXPECT(!parse(replaced(base, "sd:/roms/dreamcast, usb:/roms/dreamcast", list(9, "sd:/roms/")), file));
    EXPECT(!parse(replaced(base, "auto, power stone", list(33, "v")), file));
    EXPECT(!parse(replaced(base, "auto, power stone", "auto," + std::string(97, 'x')), file));
    EXPECT(!parse(replaced(base, "cdi, gdi, chd", std::string(17, 'x')), file));
    EXPECT(!parse(replaced(base, "{speed}", std::string(513, 'x')), file));
    std::string too_many_words;
    for (unsigned i = 0; i < 65; ++i) too_many_words += "word ";
    EXPECT(!parse(replaced(base, "{speed}", too_many_words), file));
    std::string too_many = base;
    for (int i = 0; i < 14; ++i)
        too_many += "[setting s" + std::to_string(i) + "]\nlabel = X\ntype = switch\non = -x\ndefault = off\n";
    EXPECT(!parse(too_many, file));
    EXPECT(!riftwii::emulator_path_valid("sd:/roms/./x"));
    EXPECT(!riftwii::emulator_path_valid("usb:/roms//x"));
    EXPECT(!riftwii::emulator_path_valid("sd:/" + std::string(256, 'x')));
    EXPECT(!riftwii::emulator_path_valid("sd:/" + std::string(250, 'x') + "/" + std::string(250, 'x') + "/longerpath"));
}

void test_values_and_arguments() {
    riftwii::EmulatorFile file;
    EXPECT(parse(base, file));
    auto values = riftwii::emulator_defaults(file);
    values["preset"] = "power stone";
    values["frameskip"] = "on";
    values["speed"] = "-2";
    std::string line, error;
    std::vector<std::string> argv;
    const std::string game = "sd:/roms/dreamcast/Power Stone 'Special'.CDI";
    EXPECT(riftwii::build_emulator_arguments(file, game, values, argv, line, error));
    const std::vector<std::string> expected = {file.dol, "-game", game, "-preset", "power stone",
        "name=Power Stone 'Special'", "sd:/roms/dreamcast", "-2", "-frameskip", "two words"};
    EXPECT(argv == expected);
    EXPECT(line.substr(0, file.dol.size() + 1) == file.dol + '\0');
    EXPECT(line[line.size() - 1] == 0 && line[line.size() - 2] == 0);
    EXPECT(riftwii::emulator_game_matches(file, "usb:/roms/dreamcast/sub/a.gdi"));
    EXPECT(!riftwii::emulator_game_matches(file, "sd:/roms/dreamcast-other/a.cdi"));
    EXPECT(!riftwii::emulator_game_matches(file, "sd:/roms/dreamcast/a.exe"));
    EXPECT(!riftwii::build_emulator_arguments(file, "sd:/outside.cdi", values, argv, line, error));
    EXPECT(argv.empty() && line.empty());
    values["preset"] = "-evil";
    EXPECT(!riftwii::build_emulator_arguments(file, game, values, argv, line, error));
    values = riftwii::emulator_defaults(file);
    values["undeclared"] = "injected";
    EXPECT(!riftwii::build_emulator_arguments(file, game, values, argv, line, error));
    values = riftwii::emulator_defaults(file);
    riftwii::read_emulator_values(file, "preset = power stone\nframeskip = on\nspeed = 999\nunknown = -arg\n", values);
    EXPECT(values["speed"] == "0" && values["frameskip"] == "on" && values.size() == 3);
    auto loaded = riftwii::emulator_defaults(file);
    riftwii::read_emulator_values(file, riftwii::write_emulator_values(file, values), loaded);
    EXPECT(values == loaded);
    EXPECT(riftwii::step_emulator_value(file.settings[0], "auto", -1) == "power stone");
    EXPECT(riftwii::step_emulator_value(file.settings[1], "on", 1) == "off");
    EXPECT(riftwii::step_emulator_value(file.settings[2], "3", 1) == "-2");
    EXPECT(riftwii::step_emulator_value(file.settings[2], "-2", -1) == "3");
    EXPECT(parse(replaced(base, "{speed}", "{frameskip} {speed}"), file));
    EXPECT(riftwii::build_emulator_arguments(file, game, values, argv, line, error));
    EXPECT(std::count(argv.begin(), argv.end(), "-frameskip") == 1);
    EXPECT(argv[7] == "-frameskip");
    values["frameskip"] = "off";
    EXPECT(riftwii::build_emulator_arguments(file, game, values, argv, line, error));
    EXPECT(std::count(argv.begin(), argv.end(), "-frameskip") == 0);
}

void test_command_limit() {
    riftwii::EmulatorFile file;
    EXPECT(parse(base, file));
    const std::string game = "sd:/roms/dreamcast/a.cdi";
    std::vector<std::string> argv;
    std::string line, error;
    // The builder counts the binary path, every separator, and the extra NUL.
    const std::size_t literal = 1024 - (file.dol.size() + 1) - 1 - 2;
    file.arguments = {std::string(literal / 2, 'a'), std::string(literal - literal / 2, 'b')};
    EXPECT(riftwii::build_emulator_arguments(file, game, {}, argv, line, error));
    EXPECT(line.size() == 1024);
    file.arguments[0] += 'x';
    EXPECT(!riftwii::build_emulator_arguments(file, game, {}, argv, line, error));
    EXPECT(argv.empty() && line.empty());
    EXPECT(error.find("1024") != std::string::npos);
}
}  // namespace

int main() {
    test_parsing();
    test_values_and_arguments();
    test_command_limit();
    return failures == 0 ? 0 : 1;
}
