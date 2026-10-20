// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

#include "riftwii/symsearch.hpp"

// A Wii game's own restart (the HOME Menu's Reset, the console's Reset
// button) keeping the packs, cheats and RiftWii's changes.
//
// The game restarts without RiftWii: its OSRestart shuts the devices
// down and, for a disc game, reads the disc's apploader again and runs
// it, which loads main.dol and the file table over the running game,
// then jumps to the entry point (Mario Kart Wii's SDK: OSRestart,
// __OSReboot setting the arena to 0x81280000-0x812F0000, the apploader's
// header read at word 0x910, then "sync; isync; mtctr r31; bctr"). Every
// change RiftWii made to the game's code at launch is gone after that:
// the runtime's hooks (the pack's files), the memory patches, the
// cheats' hook.
//
// RiftWii keeps a restore list in MEM2 (runtime/rtreset.h): what the
// launch changed in main.dol's sections, as differences from the disc's
// bytes, and the low memory and memory patches outside them, whole. The
// runtime serves the apploader header with its own entry in place of the
// apploader's (a MEM run at kApploaderEntryOffset), so the restart runs
// through it: the hooks are put back after each load (so the file table
// is read through the runtime too), and the entry point the restart jumps
// to is the runtime's, which writes the list back and starts the game.
namespace riftwii {

// The apploader header's entry field, in the partition's data.
constexpr std::uint64_t kApploaderEntryOffset = 0x2450;

// One stretch of memory as the game must have it after a restart.
struct ResetRecord {
    std::uint32_t address = 0;          // word-aligned
    std::vector<std::uint8_t> bytes;    // a multiple of 4 bytes
};

// Every word of `after` that differs from `before` (the same `length`
// bytes at `address`, word-aligned), as records. Runs of differences a
// few words apart become one record: a record's header costs two words.
void add_reset_differences(std::uint32_t address, const std::uint8_t* before, const std::uint8_t* after,
                           std::uint32_t length, std::vector<ResetRecord>& out);

// `length` bytes at `address`, whole; widened to whole words.
void add_reset_range(std::uint32_t address, const std::uint8_t* bytes, std::uint32_t length,
                     std::vector<ResetRecord>& out);

// The list as the runtime reads it (big-endian words): RTRST_MAGIC, the
// record count, then each record's address, byte length and bytes.
std::vector<std::uint8_t> serialize_reset_list(const std::vector<ResetRecord>& records);

// The bytes serialize_reset_list makes of these records.
std::uint32_t reset_list_bytes(const std::vector<ResetRecord>& records);

// The SDK's OSRestart(resetCode): the one function that takes the
// application type into r0 (`clrlwi r0,rN,24`), compares it with 0x81 (a
// channel: its title is launched again) and, a few words later, with
// 0x80 (a disc game: __OSReboot). Its address (the function's `stwu`),
// or 0 when there is not exactly one. Found in Mario Kart Wii.
std::uint32_t find_os_restart(const std::vector<CodeRange>& text);

// The SDK's VIWaitForRetrace, for a game without PADRead (New Super Mario
// Bros. Wii): the one function whose loop reads the retrace count from
// the small data area (`lwz rA,C(r13)`), sleeps on the retrace queue
// (`addi r3,r13,Q; bl OSSleepThread`), reads the count again
// (`lwz r0,C(r13)`) and goes round while it is the same
// (`cmplw rA,r0; beq -0x10`). Its address (the function's `stwu`), or 0.
// Found in Mario Kart Wii.
std::uint32_t find_vi_wait_for_retrace(const std::vector<CodeRange>& text);

// Test switch (settings.txt `debug_reset = N`): the game restarts itself
// as the HOME Menu's Reset does, at the Nth call of a function its main
// thread calls every frame (PADRead, else VIWaitForRetrace), so Dolphin
// can show a restart with no controller driven. The stub at
// `stub_address` counts the calls, calls OSRestart(0) at the Nth and
// otherwise runs `displaced` (the function's first instruction) and goes
// on at `pad_read` + 4. Empty when `displaced` cannot run elsewhere.
std::vector<std::uint32_t> restart_trigger_stub(std::uint32_t stub_address, std::uint32_t pad_read,
                                                std::uint32_t displaced, std::uint32_t calls,
                                                std::uint32_t os_restart);
// Where the stub keeps its count, from its start.
constexpr std::uint32_t kRestartTriggerCountOffset = 0x40;
constexpr std::uint32_t kRestartTriggerBytes = 0x4C;

}  // namespace riftwii
