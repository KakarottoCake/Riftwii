// SPDX-FileCopyrightText: 2026 RiftWii contributors
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
    // A code build instead of an XML pack: a mod made of Gecko codes that
    // load its files while the game runs (a Project+ build, for one). Its
    // code file; `path` is its folder. No options, never compiled.
    std::string gct_path;
    bool code_build() const { return !gct_path.empty(); }
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
// is cloned into once), "fresh" (a distinct persistent folder, started empty),
// "emunand" (the emulated NAND folder other loaders keep on the SD card,
// riftwii/sharednand.hpp: shared with them, the NAND save cloned in once).
struct SaveOverride {
    std::string dir;   // sd:/ folder, empty when nothing is overridden
    bool clone = false;
    // The clone also fills a data folder that exists but holds no save:
    // the shared emulated NAND's other loader makes the game's folder at
    // every launch, before the game has saved anything.
    bool clone_into_empty = false;
    std::string note;  // one log line when dir is set
};

// The name a pack's author gave it: the section all its options sit in
// (Riivolution's menu shows the same), e.g. "RiiPlay Expansion Pack" for
// WiiPlay.xml. Empty when its options sit in more than one section or in
// none, for code builds and for packs that cannot be read: the menu then
// shows the file's name.
std::string pack_title(const LaunchPackage& p);

// Folder the title's saves are served from when the UI asks for separation
// and no <savegame external> patch claimed one (XML wins): an empty dir
// means no override. An empty game_id means no override. `shared_nand` is
// the folder "emunand" uses (shared_nand_folder); empty, no override.
SaveOverride resolve_save_override(const std::string& save_mode, const std::string& xml_dir,
                                   const std::string& game_id, const std::string& shared_nand = std::string());

// Which games have packs, from one pass over the XML files: the game
// grid's MODS tag and its "games with mods" filter. A listed image's
// revision and disc number are unknown until it is opened, so only the
// ID is matched here; the mods screen still applies the full filter. A
// pack that fails to parse counts for the game its <id> names, if any. A
// pack with no game ID (one for every game, like Riivolution's Ocarina
// XML) counts for none: it still shows on each game's Mods page.
class PackIndex {
public:
    void add(const std::string& xml);
    void add_game(const std::string& game_id);  // a code build's game
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
    std::string borders = "global";     // or "keep", "remove", "remove_all"
    std::string video_mode = "global";  // or a VideoMode name
    std::string language = "global";    // or a riftwii/gamelang.hpp name
    std::string cios = "global";        // or "auto", "248" ... "252"
    std::string server = "global";      // or a riftwii/wfcpatch.hpp name
    std::string region_video = "off";   // "on": the Region video fix (gx_region_video_fix)
    std::string aspect = "global";      // Settings' (LoaderSettings::aspect), or "game", "4:3", "16:9" (gx_force_aspect)
    std::string rumble = "on";          // "off": gx_rumble_off
    std::string speaker = "on";         // "off": gx_speaker_off
    std::string region_strings = "off"; // "on": the Region strings fix (gx_country_strings)
    // A GameCube game's disc reads: "drive" take a GameCube drive's time
    // (some games count on it), "fast" come at the SD card's speed.
    std::string gc_disc_reads = "fast";
    // A GameCube game's memory card: "own" is the game's own file, "shared"
    // one card for all the games of its encoding (gc_card_path).
    std::string gc_card = "own";
    // A GameCube game's picture and language (gcnative.cpp does each).
    // "global", the default, follows Settings and the Wii's own settings
    // as a Wii game's rows do (GcConsoleSettings, resolved below); "game"
    // is the game's own whatever those say.
    // "480p": progressive scan, for a US or Japanese game on a component cable;
    std::string gc_progressive = "global";
    // "16:9": the game's 3D drawn for a widescreen TV;
    std::string gc_widescreen = "global";
    // one of kGcLanguages: the language the game is told the console's
    // GameCube settings say, for this game only.
    std::string gc_language = "global";
};

// The GameCube's languages in the order of their number in its settings
// (the SRAM's language byte, as YAGCD lists it), by GameSettings name.
inline const char* const kGcLanguages[] = {"english", "german", "french", "spanish", "italian", "dutch"};

// The SRAM language number for a GameSettings::gc_language name; -1 for
// "game" or a name that is not one of kGcLanguages.
int gc_sram_language(const std::string& name);

// A GameCube game made for a PAL television (its ID's fourth letter): its
// picture is 50 Hz or 576 lines, never NTSC's 480.
bool gc_pal_game(const std::string& id);

// What a GameCube game's "global" options follow: Settings' choices for
// every game and the Wii's own settings (the menu and the loader read
// them from the console; a test sets them by hand).
struct GcConsoleSettings {
    std::string aspect = "game";            // LoaderSettings::aspect: "game" (the Wii's TV setting), "4:3", "16:9"
    std::string game_language = "console";  // LoaderSettings::game_language (riftwii/gamelang.hpp names)
    bool wii_widescreen = false;            // the Wii's TV is set to 16:9
    bool wii_progressive = false;           // the Wii's 480p setting is on
    int wii_language = -1;                  // the Wii's language, as libogc's CONF_LANG_* number
};

// Whether the game's 3D is drawn for 16:9: only "16:9", picked for the
// game; "game" and "global" (Default) keep the game's own picture, as the
// widening is experimental.
bool gc_widescreen_on(const GameSettings& game, const GcConsoleSettings& console);

// Whether the game gets 480p: "480p" yes, "game" no, "global" when the
// Wii's own 480p setting is on. Never for a PAL game (gc_pal_game).
bool gc_progressive_on(const GameSettings& game, const std::string& id, const GcConsoleSettings& console);

// The SRAM language number the game is told (kGcLanguages' order), or -1
// for the console's GameCube setting as it is. "global": Settings' game
// language when it is one of the GameCube's six, the Wii's own language
// when Settings says the console's, else -1.
int gc_language_number(const GameSettings& game, const GcConsoleSettings& console);

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
    // A code build for `game_id` (its code file is named after the game):
    // `file` names it in the choices file ("rex_/RSBE01.GCT"), `folder` is
    // where it lives, `gct` its code file.
    void add_code_build(const std::string& file, const std::string& folder, const std::string& gct,
                        const std::string& game_id, const DiscIdentity* disc);
    // The code files of the code builds turned on for this disc.
    std::vector<const LaunchPackage*> code_builds() const;

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
    // Whether an enabled pack patches the game: one with a choice picked,
    // or one with no options (it applies as a whole). Code builds do not
    // count. The launch then leaves online play to the packs (wii/launch/boot.cpp
    // patches no server), so the frontend shows the server as the pack's.
    bool packs_patch_game() const;

    // What to compile: every enabled package with every option stated
    // explicitly, so a package's defaults never leak past the frontend.
    // Code builds are not compiled (code_builds() lists them).
    std::vector<PackageChoices> selections() const;
    // XML packs switched on with every option off: they patch nothing and
    // the compile refuses them ("no patches selected"). A pack with no
    // options at all applies as a whole and is never listed.
    std::vector<std::size_t> packs_with_nothing_picked() const;

    // Persistence, one line per fact, tab-separated:
    //   *riftwii*\tsaves\t<nand|separate|fresh>
    //   *riftwii*\tcheats\t<on|off>
    //   *riftwii*\tcheat\t<name>          (one per cheat picked)
    //   *riftwii*\tvideo\t<width>, deflicker\t<filter>, borders\t<keep|remove|remove_all>
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
    // Back to what a game with no choices file has: saves on the Wii and
    // every GameSettings default. restore() only sets what the file names,
    // so a model reused for another game must start here, or the last
    // game's cheats, cIOS and server carry over (a tester's game page said
    // "On, 2 picked" for one cheat).
    void forget_game_choices();
};

}  // namespace riftwii
