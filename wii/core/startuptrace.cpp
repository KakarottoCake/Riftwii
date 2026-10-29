// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "startuptrace.hpp"

#include <ogc/cache.h>

#include "log.hpp"
#include "memlimits.hpp"
#include "riftwii/startupnote.hpp"

namespace riftwii::wii::startup {
namespace {

StartupNote* note() { return reinterpret_cast<StartupNote*>(mem::kStartupTrace); }

void Flush() { DCFlushRange(note(), sizeof(StartupNote)); }

const char* StepName(std::uint32_t step) {
    constexpr const char* names[] = {
        "none", "copying start-up data", "restoring arguments", "starting libogc", "restoring the NAND permission check",
        "setting memory limits", "reserving memory card work space", "reading the restart note",
        "installing the crash handler", "starting the SD card", "mounting the SD card",
        "stopping the SD card for a retry", "reading test limits", "reading launch arguments",
        "checking the test script", "creating the log folder", "archiving the previous log",
        "opening the log"
    };
    static_assert(sizeof(names) / sizeof(names[0]) == static_cast<unsigned>(Step::Count));
    return step < static_cast<unsigned>(Step::Count) ? names[step] : "an unknown step";
}

}  // namespace

void Begin() {
    startup_note_begin(*note(), RIFTWII_VERSION, static_cast<unsigned>(Step::Stash),
                       static_cast<unsigned>(Step::Count));
    Flush();
}

void Mark(Step step) {
    startup_note_mark(*note(), static_cast<unsigned>(step));
    Flush();
}

void LogReady() {
    StartupNote* saved = note();
    if (!startup_note_valid(*saved, static_cast<unsigned>(Step::Count))) return;
    if (saved->previous_step != 0) {
        logf("The last start stopped at: %s (RiftWii %s), before its log opened.\n",
             StepName(saved->previous_step), saved->previous_version);
        LogSync();
    }
    // Clearing follows the synced report: another interrupted start must not
    // erase the evidence before any log has reached the card.
    *saved = StartupNote{};
    Flush();
}

}  // namespace riftwii::wii::startup
