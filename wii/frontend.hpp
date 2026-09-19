// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "modplan.hpp"
#include "riftwii/launch.hpp"

// The frontend's state and the card side of it, shared by the GUI and the
// autorun's `launch` command so the launch path can be exercised without
// a pad: the disc in the drive, the packages on the card (sd:/riivolution)
// and the choices kept per game (sd:/riftwii/choices/<game id>.txt).
namespace riftwii::wii {

constexpr const char* kPackageDir = "sd:/riivolution";
constexpr const char* kChoicesDir = "sd:/riftwii/choices";

struct FrontendState {
    std::string game_id;          // "RMCE01", or empty
    std::string disc_title;
    std::string disc_status;      // one line for the screen
    riftwii::LaunchModel model;   // packages, enabled flags and choices
    std::string choices_path;     // where the choices are kept, empty without a disc
    // The selection compiled on the preflight screen, booted by main.
    CompiledMod compiled;
    bool has_compiled = false;
};

// Identifies the disc (without waiting for one) and fills the disc fields.
void IdentifyDisc(FrontendState& state);
// Reads every *.xml of the package directory into the model, in name
// order, then the saved choices. Returns a status line for the screen.
std::string ScanPackages(FrontendState& state);
// Writes the model's state to the choices file (nothing without a disc).
void SaveChoices(const FrontendState& state);

}  // namespace riftwii::wii
