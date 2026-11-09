// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "rtpadcal.h"

// GameCube controller calibration (Settings > Controllers > Calibrate a
// GameCube controller): what the page measured on each controller, the
// file it is kept in (sd:/riftwii/gccalibration.txt), and the stretch a
// game applies (runtime/rtpadcal.h), worked out here so the game never
// divides.
//
// A worn stick stops short of its gate: each of its four directions is
// stretched so the reach the page measured becomes a healthy stick's, and
// never further (a value past it is held there). A stick that already
// reaches that far is left as it is. A dead zone, set on the page, makes
// small values near the centre read as the centre, for a stick that
// drifts; what lies past it is stretched so full tilt is still full tilt.
// Each trigger is stretched the same way to a healthy trigger's travel.
namespace riftwii {

// A healthy GameCube controller's reach from the centre, which a worn one
// is stretched to: the main stick at its gate (the figure the Xbox 360
// pad's mapping already uses, runtime/rtgcad.c), the C stick a little
// less (its gate is smaller), and a trigger at the end of its travel
// before the click.
constexpr std::uint8_t kPadMainStickReach = 104;
constexpr std::uint8_t kPadCStickReach = 96;
constexpr std::uint8_t kPadTriggerReach = 180;
// A direction the stick reached less than this during the calibration
// was not really pushed there: the page asks again rather than store it.
constexpr std::uint8_t kPadMinReach = 40;
// The dead zone slider's range and step.
constexpr std::uint8_t kPadDeadZoneMax = 30;
constexpr std::uint8_t kPadDeadZoneStep = 2;
// A trigger pressed less than this was not pressed: left as it is.
constexpr std::uint8_t kPadMinTrigger = 40;

// What the page measured on one controller. A reach of 0 was not
// measured: that direction (or trigger) is left as it is.
struct PadCalibration {
    // Right, left, up, down, as rtpadcal_pad's directions.
    std::array<std::uint8_t, 4> main{};
    std::array<std::uint8_t, 4> c_stick{};
    std::uint8_t trigger_l = 0;
    std::uint8_t trigger_r = 0;
    std::uint8_t dead_zone = 0;  // both sticks, 0 to kPadDeadZoneMax

    // Nothing measured and no dead zone: the controller as it is.
    bool is_default() const;
};

// Where a controller plays from, as far as it can be told apart: one of
// the Wii's own GameCube ports, one of the adapter's ports, or a USB pad
// by its IDs (any port).
struct PadSlot {
    enum class Kind { Wii, Adapter, Usb };
    Kind kind = Kind::Wii;
    std::uint32_t id = 0;  // port 0-3, or VID << 16 | PID

    bool operator==(const PadSlot& other) const { return kind == other.kind && id == other.id; }
};

// The slot's name in the file: "wii1" to "wii4", "adapter1" to "adapter4",
// "usb-057e2009". False for anything else, so a hand-edited line can't
// name a fifth port.
std::string pad_slot_key(const PadSlot& slot);
bool parse_pad_slot(const std::string& key, PadSlot& slot);

// Every stored calibration. The Wii's four ports and the adapter's four,
// and up to RTPADCAL_USB_PADS USB pads.
class PadCalibrations {
public:
    struct Entry {
        PadSlot slot;
        PadCalibration calibration;
    };
    // The file's text: one "<slot> = <main right left up down> <C right
    // left up down> <L> <R> <dead zone>" line each, "#" comments. Lenient:
    // a line it cannot read, a value out of range or a slot past the
    // limits is dropped.
    void parse(const std::string& text);
    std::string serialize() const;

    const PadCalibration* find(const PadSlot& slot) const;
    // Stores it, or forgets the slot when it is the default. False when it
    // is a USB pad and RTPADCAL_USB_PADS are stored already.
    bool set(const PadSlot& slot, const PadCalibration& calibration);
    void forget(const PadSlot& slot);
    const std::vector<Entry>& entries() const { return entries_; }

private:
    std::vector<Entry> entries_;
};

// The stretch for one controller, as a game applies it. A default
// calibration gives a pad that changes nothing.
rtpadcal_pad pad_calibration_stretch(const PadCalibration& calibration);
// The whole table for a launch. False (and a zeroed table) when there is
// nothing to apply, so the launch can leave it out.
bool pad_calibration_table(const PadCalibrations& calibrations, rtpadcal_table& table);

// A measurement under way: the furthest each direction and trigger has
// gone. `sticks` is the stick's x and y, then the C stick's, from the
// centre as a game reads them; the triggers from their rest.
void pad_calibration_note(PadCalibration& measured, const std::int8_t sticks[4], std::uint8_t trigger_l,
                          std::uint8_t trigger_r);
// Whether a stick's four directions all reached kPadMinReach.
bool pad_stick_measured(const std::array<std::uint8_t, 4>& reach);

}  // namespace riftwii
