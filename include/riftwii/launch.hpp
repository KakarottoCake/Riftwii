// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
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

// The frontend's state: the packages found, which are enabled and what
// each option is set to. Pure data so the host tests cover it; the Wii
// side lists the directory, draws and boots.
class LaunchModel {
public:
    std::vector<LaunchPackage> packages;

    // Parses `xml` into a package entry. A package whose filter does not
    // match `disc` (when given) is kept, marked not for this disc, and
    // never enabled. Parse failures leave the entry invalid with the error
    // in `detail`.
    void add(const std::string& file, const std::string& path, const std::string& xml, const DiscIdentity* disc);

    // Enabling a package that is invalid or for another disc is refused.
    bool set_enabled(std::size_t package, bool enabled);
    // Moves an option to its next (direction > 0) or previous choice,
    // wrapping through "off". Returns false for a bad index.
    bool cycle(std::size_t package, std::size_t option, int direction);
    // The name of an option's current choice ("Off" when disabled).
    std::string choice_name(std::size_t package, std::size_t option) const;

    // What to compile: every enabled package with every option stated
    // explicitly, so a package's defaults never leak past the frontend.
    std::vector<PackageChoices> selections() const;

    // Persistence, one line per fact, tab-separated:
    //   <file>\t<on|off>
    //   <file>\t<Section/Option>\t<choice name or empty>
    // Restoring ignores files, options and choices it no longer finds.
    std::string save() const;
    void restore(const std::string& text);
};

}  // namespace riftwii
