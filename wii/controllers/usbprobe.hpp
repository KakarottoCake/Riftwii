// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace riftwii::wii {

// Settings > Check the GameCube adapter > Check each cIOS: which IOS can
// reach the adapter where it is plugged in. A Wii U's front ports work in
// the menu (IOS 58) but not in games, which run on a d2x cIOS: this asks
// each one. The player says which port first (`port`, "front" or
// "back"), kept in sd:/riftwii/usbcheck_port.txt over the restart.
void SaveUsbCheckPort(const char* port);

// At the start after that restart, before the pads and the drives come
// up: IOS 58, then every installed d2x slot (248 to 252), each loaded in
// turn and asked for its USB device list and /dev/usb/hid (every call
// with a time limit), then IOS 58 again. The details go to the session
// log and sd:/riftwii/usbcheck.txt (problem reports carry it); the
// answer is a line for Home.
std::string RunUsbCheck(bool sd_mounted);

}  // namespace riftwii::wii
