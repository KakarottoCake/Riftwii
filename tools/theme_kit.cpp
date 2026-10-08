// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The theme kit's generated files (tools/make_theme_kit.py packs them):
//
//   theme_kit <out_dir> <label>[=<theme.ini>] ...
//
// For each label, <out_dir>/templates/<label>/<name>.png: every picture a
// theme may replace (riftwii/theme.hpp), painted as the menu paints it, in
// the default look or in the given theme.ini's colours. And
// <out_dir>/MyTheme/theme.ini: a starter theme with every key at its
// default value.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "riftwii/pngencode.hpp"
#include "riftwii/skinpaint.hpp"
#include "riftwii/theme.hpp"

namespace {

// Where each colour shows, for the starter theme.ini (docs/THEMES.md has
// the same table).
const char* Where(const std::string& key) {
    static const struct {
        const char* key;
        const char* where;
    } kWhere[] = {
        {"ink", "titles and labels"},
        {"ink_soft", "values, secondary text"},
        {"ink_dim", "hints, footers"},
        {"clock", "Home's clock"},
        {"accent", "highlights, outlines, the bar's curve"},
        {"accent_ink", "text in the accent's colour (switched-on values)"},
        {"text_on_accent", "text drawn on the accent colour"},
        {"warn", "warnings, errors"},
        {"card", "tiles, buttons, panels"},
        {"card_edge", "outline of tiles, buttons and panels"},
        {"card_edge_strong", "round buttons' rims"},
        {"shadow", "shadow under tiles and buttons"},
        {"glow", "glow around the highlighted part"},
        {"glyph", "icons and arrows"},
        {"chip_on", "an option's value box, changed from the default"},
        {"chip_off", "an option's value box at its default"},
        {"chip_off_edge", "its outline"},
        {"switch_off", "an On/Off switch when off"},
        {"bar", "Home's bottom bar, the HOME Menu's top bar"},
        {"backdrop", "behind every screen"},
        {"backdrop_stripe", "the backdrop's thin stripes"},
        {"banner_stripe", "stripes over a game page's banner"},
        {"banner_tint", "mixed into each game's colour (banner, plain spines) by its alpha"},
        {"divider", "between list rows"},
        {"scroll_track", "a list's scroll track"},
        {"scroll_thumb", "a list's scroll thumb"},
        {"badge", "a tile's DISC/USB/SD label"},
        {"shelf", "the top of Home's shelf"},
        {"shelf_edge", "the front edge of Home's shelf"},
        {"pointer1", "outline of player 1's pointer hand"},
        {"pointer2", "outline of player 2's pointer hand"},
        {"pointer3", "outline of player 3's pointer hand"},
        {"pointer4", "outline of player 4's pointer hand"},
    };
    for (const auto& w : kWhere) {
        if (key == w.key) return w.where;
    }
    return "";
}

std::string StarterIni() {
    const riftwii::Theme def = riftwii::default_theme();
    std::ostringstream s;
    s << "# A RiftWii menu theme. Copy this folder to sd:/riftwii/themes/ and pick\n"
         "# it in RiftWii under Settings > Theme. Every value below is RiftWii's\n"
         "# default: change the ones you want and delete the rest if you like.\n"
         "# Colours are #RRGGBB, or #RRGGBBAA where AA is the opacity (00 to FF).\n"
         "\n"
         "[theme]\n"
         "name = My Theme\n"
         "author = Your name\n"
         "\n"
         "[shape]\n"
         "# Corner roundness, 0 (square) to 2 (twice as round).\n"
         "corners = 1\n"
         "# A shine across the top of buttons and tiles: yes or no.\n"
         "gloss = no\n"
         "# Home's bottom bar: dip (sinks under the clock, as the Wii Menu's) or bump.\n"
         "bar = dip\n"
         "# The HOME Menu: wii (the Wii's own) or ios6 (glossy bars and linen).\n"
         "home_menu = wii\n"
         "\n"
         "[backdrop]\n"
         "# Thin horizontal stripes across the backdrop: yes or no.\n"
         "stripes = yes\n"
         "\n"
         "[colors]\n";
    for (const riftwii::ThemeColorKey& k : riftwii::theme_color_keys()) {
        // A comment has a line of its own: after a value it would be read
        // as part of the colour.
        s << "# " << Where(k.key) << "\n"
          << k.key << " = " << riftwii::format_theme_color(def.colors.*(k.field)) << "\n";
    }
    return s.str();
}

bool WriteFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

bool Templates(const std::filesystem::path& dir, const riftwii::Theme& theme) {
    std::filesystem::create_directories(dir);
    for (const riftwii::ThemeImage& im : riftwii::theme_images()) {
        riftwii::Canvas c(0, 0);
        std::vector<std::uint8_t> png;
        if (!riftwii::paint_theme_image(im.name, theme, c) ||
            !riftwii::encode_png_rgba(c.pixels().data(), static_cast<std::uint32_t>(c.width()),
                                      static_cast<std::uint32_t>(c.height()), png) ||
            !WriteFile(dir / (std::string(im.name) + ".png"), png)) {
            std::cerr << "theme_kit: cannot write " << (dir / im.name).string() << ".png\n";
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: theme_kit <out_dir> <label>[=<theme.ini>] ...\n";
        return 2;
    }
    const std::filesystem::path out = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        const std::size_t eq = arg.find('=');
        const std::string label = arg.substr(0, eq);
        riftwii::Theme theme = riftwii::default_theme();
        if (eq != std::string::npos) {
            std::ifstream in(arg.substr(eq + 1), std::ios::binary);
            if (!in) {
                std::cerr << "theme_kit: cannot read " << arg.substr(eq + 1) << "\n";
                return 1;
            }
            std::stringstream text;
            text << in.rdbuf();
            std::vector<std::string> notes;
            theme = riftwii::parse_theme(text.str(), notes);
            for (const std::string& n : notes) std::cerr << label << ": " << n << "\n";
        }
        if (!Templates(out / "templates" / label, theme)) return 1;
    }
    std::filesystem::create_directories(out / "MyTheme");
    const std::string ini = StarterIni();
    if (!WriteFile(out / "MyTheme" / "theme.ini", std::vector<std::uint8_t>(ini.begin(), ini.end()))) return 1;
    std::cout << "theme_kit: wrote " << out.string() << "\n";
    return 0;
}
