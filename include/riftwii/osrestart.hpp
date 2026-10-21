// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

#include "riftwii/symsearch.hpp"

// A Wii game's own restart (the HOME Menu's Reset, the console's Reset
// button): finding the SDK's OSRestart and OSSleepThread in main.dol, and
// the test switch that calls OSRestart on a timer.
//
// Left to itself the game restarts without RiftWii: OSRestart shuts the
// devices down and, for a disc game, reads the disc's apploader again and
// runs it, which loads main.dol and the file table over the running game.
// Every change RiftWii made at launch is gone after that (the runtime's
// hooks that serve a pack's files, the memory patches, the cheats' hook).
// So the runtime takes OSRestart instead (runtime/resident/rt_hook.c,
// rt_reset_on_os_restart): it writes a launch request, the same "--launch"
// text another loader would pass (docs/HEADLESS.md), and leaves for the
// RiftWii channel, which starts RiftWii; RiftWii reads the request and
// starts the same game again, headless, with the same packs and choices.
// Without the channel the hook is not installed and the game restarts as
// it always did.
namespace riftwii {

// The SDK's OSRestart(resetCode): the one function that takes the
// application type into r0 (`clrlwi r0,rN,24`), compares it with 0x81 (a
// channel: its title is launched again) and, a few words later, with
// 0x80 (a disc game: __OSReboot). Its address (the function's `stwu`),
// or 0 when there is not exactly one. Found in Mario Kart Wii.
std::uint32_t find_os_restart(const std::vector<CodeRange>& text);

// The SDK's OSSleepThread(queue): the one function that marks the running
// thread waiting (`li r0,4`, `sth r0,0x2C8(rT)`: OSThread.state past its
// OSContext) and stores the queue in it (`stw rQ,0x2DC(rT)`). Its address
// (the function's `stwu`), or 0. Found in Mario Kart Wii.
std::uint32_t find_os_sleep_thread(const std::vector<CodeRange>& text);

// Test switch (settings.txt `debug_reset = N`): the game restarts itself
// as the HOME Menu's Reset does, N seconds after its main thread first
// sleeps, so Dolphin can show a restart with no controller driven. The
// main thread (the OS's default thread, 0x800000D8, running: 0x800000E4)
// sleeps every frame, waiting for the retrace or its next message. The
// stub at `stub_address`, entered from OSSleepThread's first word, notes
// the time base at the first such call; once `ticks` have passed it turns
// interrupts on and calls OSRestart(0), once; otherwise it runs
// `displaced` (OSSleepThread's first instruction) and goes on at
// `sleep_thread` + 4. Empty when `displaced` cannot run elsewhere.
std::vector<std::uint32_t> restart_trigger_stub(std::uint32_t stub_address, std::uint32_t sleep_thread,
                                                std::uint32_t displaced, std::uint32_t ticks,
                                                std::uint32_t os_restart);
// The stub's words from its start: the first sleep's time base, the ticks
// to wait, OSRestart, and whether it has fired.
constexpr std::uint32_t kRestartTriggerDataOffset = 0x80;
constexpr std::uint32_t kRestartTriggerBytes = 0x90;
// The Wii's time base: the bus clock (243 MHz) over 4.
constexpr std::uint32_t kTimeBaseTicksPerSecond = 60750000;

}  // namespace riftwii
