// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

// Puts the RiftWii channel (docs/CHANNEL.md) on the Wii Menu, or takes it
// off. The caller runs under a d2x cIOS: its signature patches take the
// channel's fakesigned TMD and ticket.
namespace installer {

// Whether the channel is on this Wii, and its version (0 when it is not).
bool Installed(unsigned& version);
// The version this installer puts on.
unsigned PackageVersion();
// Whether this is a Wii U's vWii, which gets the channel's vWii package.
bool OnVWii();

bool Install(std::string& error);
bool Remove(std::string& error);

// main.cpp: a line for the screen and for sd:/riftwii/channel.log.
void Log(const char* format, ...) __attribute__((format(printf, 1, 2)));

}  // namespace installer
