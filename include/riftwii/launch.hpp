// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "riftwii/patch.hpp"

namespace riftwii {

// What a frontend hands the compiler for one package: its path and the
// choices to apply over the package's defaults (option as "Section/Option",
// choice by name, "" for off; see select_choice).
struct PackageChoices {
    std::string xml_sd_path;
    std::vector<std::pair<std::string, std::string>> choices;
};

// One package as the frontend shows it.
struct LaunchPackage {
    std::string file;      // "mod.xml"
    std::string path;      // where it was read from ("sd:/riivolution/mod.xml")
    bool valid = false;    // parsed
    std::string detail;    // the parse error, or a summary
    bool for_disc = true;  // its filter matches the disc (true when no disc is known)
    bool enabled = false;  // chosen for launch
    Package package;       // its options carry the current choices
};

// Visibility on the mods screen: only packs made for the selected game.
// A pack that fails to parse is judged by the game its <id> names in the
// raw text, and shown (with its error) only when that matches or cannot
// be read; packs for a different game are hidden, never merely greyed.
inline bool show_package(const LaunchPackage& p) { return p.for_disc; }

// Full physical/image identity used for package filters and for binding a
// preflight plan to the disc that was actually compiled.
bool same_disc_identity(const DiscIdentity& left, const DiscIdentity& right);

// True when a selected save mode or package needs compilation/resident setup.
// Both GUI and autorun use this so a plain Separate/Fresh boot cannot bypass
// the save redirect through the direct boot path.
bool needs_launch_pipeline(bool has_selected_packages, const std::string& save_mode);

// The frontend's state: the packages found, which are enabled and what
// each option is set to. Pure data so the host tests cover it; the Wii
// side lists the directory, draws and boots.
// The UI's save handling for one game, persisted with its choices:
// "nand" (the Wii saves as usual), "separate" (an SD folder the NAND save
// is cloned into once), "fresh" (a distinct persistent folder, started empty).
struct SaveOverride {
    std::string dir;   // sd:/ folder, empty when nothing is overridden
    bool clone = false;
    std::string note;  // one log line when dir is set
};

// Folder the title's saves are served from when the UI asks for separation
// and no <savegame external> patch claimed one (XML wins): an empty dir
// means no override. An empty game_id means no override.
SaveOverride resolve_save_override(const std::string& save_mode, const std::string& xml_dir,
                                   const std::string& game_id);

// Which games have packs, from one pass over the XML files: the game
// grid's MODS tag and its "games with mods" filter. A listed image's
// revision and disc number are unknown until it is opened, so only the
// ID is matched here; the mods screen still applies the full filter. A
// pack that fails to parse counts for the game its <id> names, if any.
class PackIndex {
public:
    void add(const std::string& xml);
    bool has_packs(const std::string& game_id) const;
    std::size_t size() const { return filters_.size(); }

private:
    std::vector<DiscFilter> filters_;
};

// Loader settings for one game besides its packs. "global" means the
// Settings screen's default.
struct GameSettings {
    bool cheats = false;                // apply the cheats picked below
    std::set<std::string> cheat_names;  // by name, as in the game's cheat file
    std::string video_width = "global"; // or a riftwii/videopatch.hpp name
    std::string deflicker = "global";
    std::string borders = "global";     // or "keep", "remove"
    std::string video_mode = "global";  // or a VideoMode name
    std::string language = "global";    // or a riftwii/gamelang.hpp name
    std::string cios = "global";        // or "auto", "248" ... "252"
    std::string server = "global";      // or a riftwii/wfcpatch.hpp name
};

class LaunchModel {
public:
    std::vector<LaunchPackage> packages;
    std::string save_mode = "nand";
    GameSettings game;

    // Parses `xml` into a package entry. A package whose filter does not
    // match `disc` (when given) is kept, marked not for this disc, and
    // never enabled. Parse failures leave the entry invalid with the error
    // in `detail`.
    void add(const std::string& file, const std::string& path, const std::string& xml, const DiscIdentity* disc);

    // Enabling a package that is invalid or for another disc is refused.  A
    // package with exactly one off option and one choice selects that choice
    // on enable; packages with more choices or options remain explicit.
    bool set_enabled(std::size_t package, bool enabled);
    // Moves an option to its next (direction > 0) or previous choice,
    // wrapping through "off". Returns false for a bad index.
    //
    // Options that share an id and a section name across packs are one
    // option (as in Riivolution): stepping any of them walks every pack's
    // choices in pack order, and choosing one clears the others. Choosing
    // a choice of a pack that is off turns that pack on.
    bool cycle(std::size_t package, std::size_t option, int direction);
    // The name of an option's current choice ("Off" when disabled); for
    // a merged option, the choice picked in any enabled pack.
    std::string choice_name(std::size_t package, std::size_t option) const;
    // Every pack's copy of a merged option, in pack order (just this one
    // when it is not merged). Only valid packs for this disc take part.
    struct OptionRef {
        std::size_t package;
        std::size_t option;
    };
    std::vector<OptionRef> merge_group(std::size_t package, std::size_t option) const;
    // Whether a list shows this option: false for a merged option's copy
    // when an earlier enabled pack already shows it.
    bool option_shown(std::size_t package, std::size_t option) const;
    // The file of the first enabled pack whose current choices turn on a
    // <savegame> patch, empty when none. Such a pack brings its own save
    // folder and it wins over save_mode (resolve_save_override), so the
    // frontend shows the save setting as the pack's instead of offering it.
    std::string pack_save_owner() const;

    // What to compile: every enabled package with every option stated
    // explicitly, so a package's defaults never leak past the frontend.
    std::vector<PackageChoices> selections() const;

    // Persistence, one line per fact, tab-separated:
    //   *riftwii*\tsaves\t<nand|separate|fresh>
    //   *riftwii*\tcheats\t<on|off>
    //   *riftwii*\tcheat\t<name>          (one per cheat picked)
    //   *riftwii*\tvideo\t<width>, deflicker\t<filter>, borders\t<keep|remove>
    //   *riftwii*\tvideomode\t<mode>, gamelang\t<language>, cios\t<auto|slot>
    //   *riftwii*\tserver\t<off|wiimmfi|wiilink|altwfc|custom>
    //   <file>\t<on|off>
    //   <file>\t<Section/Option>\t<choice name or empty>
    // Restoring ignores files, options and choices it no longer finds, and
    // any save mode it does not know.  A saved enabled package with exactly
    // one option and one choice is normalized to that choice, including old
    // files that stored an empty choice before this convenience existed.
    std::string save() const;
    void restore(const std::string& text);
};

}  // namespace riftwii
