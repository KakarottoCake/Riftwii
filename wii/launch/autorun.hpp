// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Unattended test driver, built only into test builds (make -f Makefile.wii
// TEST=1; AGENTS.md "Testing in Dolphin"). Source commands select the image
// before probe, xml, boot, or launch: `sd <path-or-id> [cios-slot]`,
// `usb <path-or-id> [cios-slot]`, and `disc`. The launch steps it shares with
// the menu are in launchrun.hpp.
namespace riftwii::wii {

constexpr const char* kAutorunPath = "sd:/riftwii/autorun.txt";
constexpr const char* kAutorunLogPath = "sd:/riftwii/autorun.log";

bool AutorunPresent();
void RunAutorun();

}  // namespace riftwii::wii
