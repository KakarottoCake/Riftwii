// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

// Problem reports (riftwii/problemreport.hpp) from the Wii: gathered from
// the SD card and the console, saved to sd:/riftwii/report.txt and sent
// to paste.rs, whose link (and a QR code of it) the menu shows. Nothing is
// sent unless the player says so: after a crash the menu asks, and
// Settings has "Send a problem report" for anything else.
namespace riftwii::wii {

constexpr const char* kReportPath = "sd:/riftwii/report.txt";

// At startup, before session.log is opened again: the last run's log
// becomes session-previous.log, so a restart does not lose it.
void RotateSessionLog();

// The controllers connected right now, on one line: Wii Remotes and what
// is plugged into them, GameCube ports, the GameCube adapter, the Wii U
// GamePad, and the pairings fakemote adds.
std::string DescribeControllers();

// At startup, once the menu's IOS is up: a crash the game had last time
// (wii/faulthook.hpp left it on the NAND) becomes sd:/riftwii/gamecrash.txt.
void ImportGameCrash();

// Whether the last run ended in a crash no report was offered for yet:
// the game crashing (ImportGameCrash found one), RiftWii restarting after
// a crash or a failed launch, or a crash.txt newer than the last one
// asked about. `what` says which ("game", "crash", "launch").
bool UnreportedCrash(std::string& what);
// The crash just asked about is not asked about again (sent or not).
void NoteCrashAsked();

struct ReportOutcome {
    bool saved = false;    // sd:/riftwii/report.txt written
    bool sent = false;
    bool partial = false;  // paste.rs kept only the start
    std::string link;
    std::string error;     // why it was not sent
    std::size_t bytes = 0;
};
// Gathers everything, saves it and sends it. `reason` heads the report
// ("RiftWii crashed", "sent from Settings"). Blocks for the upload (up to
// half a minute); call it with the GUI running under a "please wait" box.
ReportOutcome SendProblemReport(const std::string& reason);

}  // namespace riftwii::wii
