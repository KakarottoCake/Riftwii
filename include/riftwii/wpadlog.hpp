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

// The Wii Remote library's firmware probe, for a Wii U Pro Controller test.
// Right after a controller connects the SDK reads one byte at address
// 0x1770, past the end of a Wii Remote's memory: a retail remote answers
// with an error, which the SDK takes as "firmware 2.1 or later". A Wii U Pro
// Controller hangs up on that read instead (testers' Wii Remote logs on
// 2610-371 and 372, Mario Kart Wii), before the game looks at anything else.
//
// The patch makes the probe read 0x0016 (calibration, the first thing
// libogc reads, which the Pro Controller answers in RiftWii's menu) and makes
// the answer's handler take the error path whatever comes back, so a Wii
// Remote goes on exactly as before. Found as `li rA,0x1770` with `li rB,0x17`
// (the read report) close by; the handler is the callback loaded just
// before it with `lis rC`/`addi rC`, whose `mr rD,r4` (keeping the result)
// becomes `li rD,1`. Both or nothing.
struct WpadProbePatch {
    std::uint32_t probe = 0;    // the `li rA,0x1770`
    std::uint32_t handler = 0;  // the callback
    std::uint32_t result = 0;   // its `mr rD,r4`
};
bool find_wpad_probe(const std::vector<CodeSpan>& spans, WpadProbePatch& out, std::string& why);
// Writes the two instructions (into `spans`' bytes).
void apply_wpad_probe(const std::vector<CodeSpan>& spans, const WpadProbePatch& patch);

// The saved log (/shared2/riftwii/wpadlog.bin) as text, one message a
// line with its time; false with `error` when `bytes` is not one. Link keys
// are left out and Bluetooth addresses kept only to their last byte, as the
// text goes into problem reports.
bool describe_wpad_log(const std::uint8_t* bytes, std::size_t size, std::string& text, std::string& error);

}  // namespace riftwii
