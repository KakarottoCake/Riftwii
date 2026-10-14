// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/returnto.hpp"

// The Wii Remote log (runtime/rtwlog.h): where its blob hooks a game, and
// the saved log as text.
//
// It exists to learn why a Wii U Pro Controller, which RiftWii's own menu
// reads, never works in games: the game's Bluetooth and Wii Remote
// libraries say what they do in debug prints, which retail games compile
// into empty functions. The blob fills those in for one run.
namespace riftwii {

// The empty debug print functions in the game's code: each is exactly the
// compiler's varargs stub, `stwu r1,-0x70(r1)`, `bne cr1,+0x24`, eight
// `stfd f1-f8`, eight `stw r3-r10` and `addi r1,r1,0x70; blr` (19 words).
// The SDK's Bluetooth and Wii Remote libraries print through two of them.
std::vector<std::uint32_t> find_print_stubs(const std::vector<CodeSpan>& spans);

// The instruction __OSLaunchMenu (the HOME Menu's "Wii Menu") runs right
// after loading the System Menu's title for the first time: the `li rZ,0`
// of riftwii/returnto.hpp's first place (`li r5,0`, or `li r7,0` in older
// SDKs). Patching Return to RiftWii leaves it alone, so the log's exit hook
// can go there either way. 0 when the game has no such place.
std::uint32_t find_launch_menu_exit(const std::vector<CodeSpan>& spans);

// The saved log (/shared2/riftwii/wpadlog.bin) as text, one message a
// line with its time; false with `error` when `bytes` is not one. Link keys
// are left out and Bluetooth addresses kept only to their last byte, as the
// text goes into problem reports.
bool describe_wpad_log(const std::uint8_t* bytes, std::size_t size, std::string& text, std::string& error);

}  // namespace riftwii
