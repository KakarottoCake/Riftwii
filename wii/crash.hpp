// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// RiftWii's crash screen. libogc sends an unhandled exception (a bad
// pointer, a bad jump, an illegal instruction) to a panic function; ours
// notes the registers, then resumes the crashed thread in a recovery
// function on a spare stack with interrupts on. That function shows the
// report on screen, adds it to the open log and to sd:/riftwii/crash.txt,
// and restarts RiftWii (wii/restart.hpp) when a button is pressed or
// after a minute. Only RiftWii's own crashes land here: a game installs
// its own handlers when it starts.
namespace riftwii::wii {

enum class CrashPhase { Early, Menu, Console };

void CrashInstall();
// Where the screen is: before the menu's video, the menu (libwiigui),
// or the text console of the launch screen.
void CrashSetPhase(CrashPhase phase);

}  // namespace riftwii::wii
