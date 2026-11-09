// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "riftwii/padcal.hpp"
#include "rtpadcal.h"

// The GameCube controllers' calibrations on the SD card
// (sd:/riftwii/gccalibration.txt, riftwii/padcal.hpp): read by the
// calibration page and by each launch, written by the page.
namespace riftwii::wii {

// What the card holds; none when the file is missing or unreadable.
PadCalibrations LoadPadCalibrations();
// Writes them all; false when the card could not take the file.
bool SavePadCalibrations(const PadCalibrations& calibrations);
// The table a launch hands its pad hooks; false (and nothing to apply)
// when no controller is calibrated. Says in the log which ones are.
bool LoadPadCalibrationTable(rtpadcal_table& table);

}  // namespace riftwii::wii
