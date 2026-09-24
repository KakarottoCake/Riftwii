// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <map>
#include <iosfwd>
#include <string>
#include <vector>

namespace riftwii {

struct DiscIdentity {
    std::string id;
    std::uint8_t revision = 0;
    std::uint8_t number = 0;
    // The console's ID (ES device ID), for {$__ngid}; 0 when unknown.
    std::uint32_t console_id = 0;
};

struct DiscFilter {
    std::string game;
    std::string developer;
    std::vector<std::string> regions;
    int revision = -1;
    int number = -1;
    bool matches(const DiscIdentity& disc) const;
};

// A name/value pair used for {$name} substitution in paths. Params attach to
// options, choices and macro clones; later entries override earlier ones.
struct Param {
    std::string name;
    std::string value;
};

struct FilePatch {
    // Either an absolute disc path ("/dir/file.bin") or, when is_filename is
    // set, a bare file name to look up in the FST (any directory).
    std::string disc;
    std::string external;
    std::uint64_t offset = 0;
    std::uint64_t file_offset = 0;
    std::uint64_t length = 0;
    bool resize = true;
    bool create = false;
    bool is_filename = false;
};

struct FolderPatch {
    // Absolute disc directory, a bare directory name (is_name), or empty to
    // match files anywhere on the disc by name.
    std::string disc;
    std::string external;
    std::uint64_t length = 0;
    bool resize = true;
    bool create = false;
    bool recursive = true;
    bool is_name = false;
};

// Three documented variants share one node: plain write (offset + value),
// ocarina (search for `value`, branch to `offset`), search (find `original`,
// replace with `value`, `align` stride).
struct MemoryPatch {
    std::uint64_t offset = 0;
    bool has_offset = false;
    std::vector<std::uint8_t> value;
    std::string valuefile;  // used instead of `value` when non-empty
    std::vector<std::uint8_t> original;
    bool ocarina = false;
    bool search = false;
    std::uint64_t align = 1;
};

struct SavegamePatch {
    std::string external;
    bool clone = true;
};

// <shift source= destination=>: the destination disc file reads the
// source's data (as patched), as Riivolution does it by pointing the
// destination's FST entry at the source's. Both must be disc files.
struct ShiftPatch {
    std::string source;
    std::string destination;
};

enum class PatchKind { File, Folder, Memory, Savegame, Shift };

// Document order across the per-kind vectors below, so later runtime work
// can honour "first entry wins / last entry wins" rules exactly as written.
struct PatchStep {
    PatchKind kind = PatchKind::File;
    std::size_t index = 0;
};

struct Patch {
    std::string root;
    std::vector<FilePatch> files;
    std::vector<FolderPatch> folders;
    std::vector<MemoryPatch> memory;
    std::vector<SavegamePatch> savegames;
    std::vector<ShiftPatch> shifts;
    std::vector<PatchStep> order;
};

struct Choice {
    std::string name;
    std::vector<std::string> patches;
    std::vector<Param> params;
};

struct Option {
    std::string section;
    std::string id;
    std::string name;
    // The name Riivolution saves its choice under (riivolution/config):
    // the id, else the section's id (or name) followed by the option name.
    std::string config_id;
    std::size_t selected = 0;  // 0 = disabled, otherwise 1-based choice
    std::vector<Choice> choices;
    std::vector<Param> params;
};

// <network protocol="riifs" address="…" port="…"/>: a PC that serves
// packs over RiiFS. An empty address means "find one on the local
// network". Other protocols are recorded as warnings, not here.
struct NetworkServer {
    std::string address;
    std::uint16_t port = 1137;
};

struct Package {
    DiscFilter filter;
    // Where external paths start: the wiidisc root, taken from the XML's
    // folder when relative, or the folder itself when there is none.
    std::string root = "/riivolution";
    // Parsed from wiidisc shiftfiles but intentionally unused: every resized
    // or created file is relocated into the virtual window and the FST is
    // rewritten to point there, so on-disc shifting is never needed and a
    // package that forbids it still applies. Newer SMBW 1.30 sets it.
    bool shift_files = false;
    std::vector<Option> options;
    std::map<std::string, Patch> patches;
    std::vector<NetworkServer> networks;
    // Non-fatal notes: unknown attributes/elements that were ignored, defaults
    // that were clamped. Parsing succeeds; a frontend should surface these.
    std::vector<std::string> warnings;
};

// Which patch kinds the caller's runtime can execute. Anything selected that
// is not allowed makes plan_package() fail with a message naming the option,
// choice, patch and feature - a mod is never launched partially applied.
struct PlanOptions {
    bool allow_folders = false;
    bool allow_memory = false;
    bool allow_savegames = false;
    bool allow_filename_targets = false;
};

struct Plan {
    std::vector<FilePatch> files;
    std::vector<FolderPatch> folders;
    std::vector<MemoryPatch> memory;
    std::vector<SavegamePatch> savegames;
    std::vector<ShiftPatch> shifts;
    std::vector<PatchStep> order;
};

// Parses one XML document into a Package. Tolerates unknown attributes and
// elements (recorded in Package::warnings); rejects malformed values,
// DOCTYPE/entities, non-declaration processing instructions, duplicate
// attributes and the size/depth limits. Output is untouched on failure.
// `folder` is the absolute folder holding the XML ("/riivolution" or
// "/apps/riivolution"): a relative wiidisc root starts there, and it is
// the root when the XML names none.
bool parse_package(const std::string& xml, Package& output, std::string& error,
                   const std::string& folder = "/riivolution");
bool read_package(std::istream& input, Package& output, std::string& error,
                  const std::string& folder = "/riivolution");

// Joins `path` onto `root` (absolute paths replace the root), normalises "."
// and "..", and rejects escapes above "/" and characters that cannot be part
// of an SD/disc path. Placeholders must already be substituted.
bool resolve_path(const std::string& root, const std::string& path, std::string& output);

// Replaces every {$name} with the built-ins __gameid (id[0..3)), __region
// (id[3]), __maker (id[4..6)) and __ngid (the console ID, 8 hex digits),
// else the matching param value; later params override earlier ones. An
// unknown name becomes the empty string, as in Riivolution. Unterminated
// or empty placeholders are errors.
bool substitute_params(const std::string& input, const std::vector<Param>& params,
                       const DiscIdentity& disc, std::string& output, std::string& error);

// Sets an option's selection by name, as a frontend or a config file
// would: `option` is "Option" or "Section/Option" (exact match first,
// then ASCII case-insensitive; an option `id` matches too), `choice` is a
// choice name (same matching), a 1-based number, or empty / "disabled" /
// "0" to turn the option off. Fails, leaving the package untouched, when
// the option or choice does not exist or the name is ambiguous.
bool select_choice(Package& package, const std::string& option, const std::string& choice, std::string& error);

// Resolves the selected choices of `package` for `disc` into a Plan with all
// paths substituted and resolved. Returns an empty plan when the package's
// filter does not match the disc. Output is untouched on failure.
bool plan_package(const Package& package, const DiscIdentity& disc, const PlanOptions& options,
                  Plan& output, std::string& error);

}  // namespace riftwii
