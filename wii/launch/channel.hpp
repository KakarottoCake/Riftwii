// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include "riftwii/channelupdate.hpp"

// The RiftWii channel (docs/CHANNEL.md): a Wii Menu channel that starts
// RiftWii from apps/riftwii/boot.dol on the SD card or USB drive. Its
// installer is an app of its own in the release zip (apps/riftwii_channel,
// channel/installer), so RiftWii carries none of the channel; Settings and
// a one-time offer only start that installer.
namespace riftwii::wii {

// Whether the channel is on this Wii, and its version (0 when it is not).
bool ChannelInstalled(unsigned& version);
// The channel version the installer in this release puts on
// (tools/make_channel.py's TITLE_VERSION): an installed one older than
// that is offered for reinstalling.
// Version 15 keeps the banner/icon's shapes on 16:9 and centres their art.
constexpr unsigned kChannelVersion = 15;
// The oldest channel RiftWii runs with: 11 on a Wii (its L2 clear), 13 on a
// Wii U (11 and 12 never started on a vWii; 13 has its loader as in 10).
unsigned ChannelVersionNeeded();
// Retire stale attempts and distinguish an interrupted installer from a normal return.
riftwii::ChannelUpdateState ChannelInstallerUpdateState();
// Postpone only this start. The failed attempt's marker stays, so later
// starts offer Not now too until the channel no longer needs updating.
void ChannelUpdateNotNow();
// The installed channel's title ID (00010001-UFTW, or the old RFTW), 0
// when it is not installed.
unsigned long long ChannelTitle();

// Whether the Homebrew Channel started RiftWii: its return stub at
// 0x80001800 ("STUBHAXX" at 0x80001804), which std::exit goes back
// through. Started from the RiftWii channel there is none.
bool StartedByHomebrewChannel();
// The Homebrew Channel as an installed title: LULZ (1.1 and later, Wii and
// vWii), OHBC (the Open Homebrew Channel), HAXX or JODI (older ones), the
// first with a ticket; 0 when none is installed.
unsigned long long HomebrewChannelTitle();

// Whether the installer app is on the card (Settings opens it); `why`
// says where to get it when it is not.
bool ChannelInstallerPresent(std::string& why);

// Whether installing can work here: the installer app on the card, a d2x
// cIOS for it (its signature patches take the channel's fakesigned TMD
// and ticket), and not Dolphin, whose ES checks real signatures. `why`
// says what is missing.
bool ChannelCanInstall(std::string& why);

// Shared refusal text identifies a failed marker write across the warm restart.
extern const char* const kChannelMarkerError;
// Starts the installer app. Runs after the menu has closed; returns only
// if it cannot, with `error` saying why. allow_unmarked requires the player's
// consent to lose interruption detection when the card cannot save its note.
bool StartChannelInstaller(std::string& error, bool allow_unmarked = false);

// Starts a Homebrew Channel app (a DOL on the SD card, `path` with its
// sd: prefix) as the Homebrew Channel does: the running IOS kept. Returns
// only if it cannot, with `error` saying why.
bool StartHomebrewApp(const std::string& path, std::string& error);
bool StartHomebrewApp(const std::string& path, const std::string& command_line, std::string& error);

// Where a homebrew app RiftWii starts goes when it exits, as the menu can
// tell before reading the app: back to the Homebrew Channel when that
// started RiftWii (its stub is at 0x80001800), to the Wii Menu when the
// Wii Menu button is set to it, otherwise back to RiftWii (the return
// stub). An app that loads over the stub also goes to the Wii Menu; only
// starting it tells. NotSaid: back to RiftWii is set but the RiftWii
// channel is not installed, so the stub's first title is not known yet.
enum class HomebrewExit { RiftWii, HomebrewChannel, WiiMenu, NotSaid };
HomebrewExit HomebrewExitGoesTo();

}  // namespace riftwii::wii
