// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "riftwii/launch.hpp"

// Riivolution's own record of a game's choices,
// sd:/riivolution/config/<first four characters of the game id>.xml:
//
//   <riivolution version="2">
//     <option id="<config id>" default="<n>"/>
//   </riivolution>
//
// n is 0 for off, else the choice counted from 1. Options that several
// packs share (the same section and id) count their choices across the
// packs, in pack order. RiftWii reads it for a game it has no choices of
// its own for, and writes it on every change, so switching between the two
// loaders keeps the same mods on.
namespace riftwii {

struct RiivolutionConfig {
    std::vector<std::pair<std::string, unsigned>> options;  // id, default
};

// False when `xml` is not a version 2 config.
bool parse_riivolution_config(const std::string& xml, RiivolutionConfig& out);
std::string write_riivolution_config(const RiivolutionConfig& config);

// Sets the model's options from `config` (an out-of-range value leaves
// its option alone, as Riivolution does), then turns each pack it touched
// on when any of its options is on and off otherwise. Returns how many
// options it set.
unsigned apply_riivolution_config(LaunchModel& model, const RiivolutionConfig& config);

// `existing` with the model's options for this game: options it knows are
// replaced in place, new ones added at the end, the rest kept.
RiivolutionConfig merge_riivolution_config(const LaunchModel& model, const RiivolutionConfig& existing);

// "SB4E01" -> "SB4E".
std::string riivolution_config_name(const std::string& game_id);

}  // namespace riftwii
