// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "padcalstore.hpp"

#include <sys/stat.h>

#include <string>

#include "log.hpp"
#include "textfile.hpp"

namespace riftwii::wii {
namespace {

constexpr const char* kPadCalibrationPath = "sd:/riftwii/gccalibration.txt";
// A few lines a controller: anything larger is not this file.
constexpr std::size_t kPadCalibrationCap = 16u << 10;

}  // namespace

PadCalibrations LoadPadCalibrations() {
    PadCalibrations calibrations;
    std::string text;
    if (ReadTextFile(kPadCalibrationPath, text, kPadCalibrationCap)) calibrations.parse(text);
    return calibrations;
}

bool SavePadCalibrations(const PadCalibrations& calibrations) {
    mkdir("sd:/riftwii", 0777);
    const std::string text = calibrations.serialize();
    const bool saved = WriteWholeFile(kPadCalibrationPath, text.data(), text.size());
    logf("GameCube controller calibration: %u controller(s) %s %s\n",
         static_cast<unsigned>(calibrations.entries().size()), saved ? "saved to" : "could not be saved to",
         kPadCalibrationPath);
    return saved;
}

bool LoadPadCalibrationTable(rtpadcal_table& table) {
    const PadCalibrations calibrations = LoadPadCalibrations();
    const bool any = pad_calibration_table(calibrations, table);
    if (!any) return false;
    std::string names;
    for (const PadCalibrations::Entry& e : calibrations.entries()) {
        if (!names.empty()) names += ", ";
        names += pad_slot_key(e.slot);
    }
    logf("GameCube controller calibration: applied in games to %s\n", names.c_str());
    return true;
}

}  // namespace riftwii::wii
