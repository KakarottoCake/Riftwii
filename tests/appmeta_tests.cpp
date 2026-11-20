// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/appmeta.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

void test_metadata() {
    riftwii::AppMeta meta;
    expect(riftwii::parse_app_meta(
        "<app><name>  Example &amp; App  </name><coder>Author</coder>"
        "<version>1.2</version><release_date>20261022093000</release_date>"
        "<long_description>Long description</long_description>"
        "<arguments><arg> first </arg><arg></arg><arg>two words</arg></arguments></app>", meta),
        "valid metadata parses");
    expect(meta.valid && meta.name == "Example & App" && meta.coder == "Author", "XML fields and whitespace");
    expect(meta.version == "1.2" && meta.description == "Long description", "version and description fallback");
    expect(meta.arguments == std::vector<std::string>({"first", "two words"}), "arguments preserve spaces inside a value");
    expect(riftwii::app_release_date(meta.release_date) == "22/10/2026", "release timestamp formats as a date");
    expect(!riftwii::parse_app_meta("<app>", meta) && !meta.valid && meta.name.empty(), "failure resets prior metadata");
    expect(!riftwii::parse_app_meta("", meta), "empty metadata fails");
    expect(riftwii::app_display_name(meta, "example_app") == "Example app", "folder name fallback");
    expect(riftwii::app_release_date("20261301").empty(), "invalid month is refused");

    std::string xml = "<app><name>" + std::string(100, 'n') + "</name><short_description>Short</short_description>"
        "<long_description>Long</long_description><arguments>";
    for (int i = 0; i < 30; ++i) xml += "<arg>value</arg>";
    xml += "</arguments></app>";
    expect(riftwii::parse_app_meta(xml, meta), "bounded metadata parses");
    expect(meta.name.size() == 64 && meta.description == "Short", "name limit and short-description precedence");
    expect(meta.arguments.size() == 16, "argument count is bounded");

    xml = "<app><arguments>";
    for (int i = 0; i < 20; ++i) xml += "<arg>" + std::string(200, 'x') + "</arg>";
    xml += "</arguments></app>";
    expect(riftwii::parse_app_meta(xml, meta), "large arguments parse");
    std::size_t total = 0;
    for (const std::string& value : meta.arguments) {
        expect(value.size() <= 96, "each argument is bounded");
        total += value.size() + 1;
    }
    expect(total <= riftwii::kAppCommandLineMax / 2, "metadata argument storage is bounded");
}

void test_command_line() {
    const std::string path = "sd:/apps/example/boot.dol";
    std::string expected = path;
    expected.push_back('\0');
    expected += "two words";
    expected.push_back('\0');
    expected += "--flag";
    expected.append(2, '\0');
    expect(riftwii::app_command_line(path, {"two words", "--flag"}) == expected, "argv block preserves argument boundaries");
    expect(riftwii::app_command_line(path, {}) == path + std::string(2, '\0'), "argv block has two terminal NULs");
    const std::string longest(riftwii::kAppCommandLineMax - 2, 'p');
    expect(riftwii::app_command_line(longest, {}).size() == riftwii::kAppCommandLineMax, "longest exact path fits");
    expect(riftwii::app_command_line(longest + "p", {}).empty(), "overlong path is refused rather than changed");
    expect(riftwii::app_command_line("", {}).empty(), "empty program path is refused");
    expect(riftwii::app_command_line(std::string("sd:/bad\0path", 12), {}).empty(), "embedded NUL in program path is refused");
    expect(riftwii::app_command_line(path, {std::string(1024, 'x')}) == path + std::string(2, '\0'),
           "argument exceeding the block limit is omitted whole");
}

void test_picture_names_and_tile_keys() {
    using riftwii::app_cover_names;
    using riftwii::app_tile_key;
    using riftwii::valid_app_tile_key;
    expect(app_cover_names("ScummVM", "scummvm-wii") ==
           std::vector<std::string>({"ScummVM", "scummvm-wii"}), "metadata picture wins over folder picture");
    expect(app_cover_names("Scumm/VM\\:*?\"<>|", "scummvm") ==
           std::vector<std::string>({"ScummVM", "scummvm"}), "FAT punctuation is removed");
    expect(app_cover_names("../outside", "fallback") ==
           std::vector<std::string>({"outside", "fallback"}), "traversal becomes a single safe component");
    expect(app_cover_names(" . .. ", "fallback") == std::vector<std::string>({"fallback"}), "dot-only stem is omitted");
    // The longest file made from a name, <name>.own.rw2.part, fits FAT's 255.
    expect(app_cover_names(std::string(242, 'a'), "fallback").front().size() == 242, "a 242-byte name is kept");
    expect(app_cover_names(std::string(243, 'a'), "fallback") == std::vector<std::string>({"fallback"}),
           "a 243-byte name leaves no room for .own.rw2.part");
    expect(app_cover_names("", "folder") == std::vector<std::string>({"folder"}), "missing metadata uses folder");
    expect(app_cover_names("same", "same") == std::vector<std::string>({"same"}), "duplicate is checked once");
    expect(app_cover_names(std::string("Bad\0\nName", 9), "folder").front() == "BadName", "control bytes are stripped");
    expect(app_cover_names("日本語", "folder").front() == "日本語", "UTF-8 names are preserved");
    expect(app_cover_names(std::string(248, 'x'), "folder") == std::vector<std::string>({"folder"}),
           "overlong name uses fallback without splitting UTF-8");
    expect(app_cover_names("  Name. ", "folder").front() == "Name", "edge spaces and dots are trimmed");

    expect(app_tile_key("sd:/apps/scummvm") == "app:sd:/apps/scummvm", "normal app key is stable");
    const std::string unusual = app_tile_key("sd:/apps/My app,100%@home");
    expect(unusual == "app:sd:/apps/My%20app%2C100%25%40home" && valid_app_tile_key(unusual),
           "settings and profile separators are escaped canonically");
    expect(valid_app_tile_key(app_tile_key("sd:/apps/日本語")), "Unicode folder key round-trips");
    for (const char* path : {"sd:/apps/", "sd:/apps/..", "sd:/apps/a/b", "sd:/apps/a\\b", "sd:/other/app"})
        expect(app_tile_key(path).empty(), "invalid app path is refused");
    for (const char* key : {"app:sd:/apps/", "app:sd:/apps/..", "app:sd:/apps/a,b", "app:sd:/apps/a b",
                            "app:sd:/apps/%00", "app:sd:/apps/%2F", "app:sd:/apps/%5C",
                            "app:sd:/apps/%", "app:sd:/apps/%gg", "app:sd:/apps/%61"})
        expect(!valid_app_tile_key(key), "unsafe or noncanonical key is refused");
    expect(app_tile_key("sd:/apps/" + std::string(256, 'x')).empty(), "folder key has a size bound");
}

}  // namespace

int main() {
    test_metadata();
    test_command_line();
    test_picture_names_and_tile_keys();
    return failures == 0 ? 0 : 1;
}
