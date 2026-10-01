// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "riftwii/symsearch.hpp"

// Where the Gecko code handler (vendor-gecko/) is called from in a game.
// Each hook is found by four instructions every SDK build of the function
// contains (the same patterns other loaders use); the first `blr` after
// them becomes a branch to the handler's entry, whose own `blr` then
// returns to the function's caller.
//   Retrace     the end of the video retrace handler, once per frame
//               (RiftWii's choice; Gecko OS hook type 1)
//   AudioFrame  the end of AXNextFrame, once per audio frame (hook type
//               7, which Project+ builds ask for in gameconfig.txt)
// A second executable the game starts is hooked the same way, by
// wii/dolswitch_stub.S (find_dol_jumps).
namespace riftwii {

constexpr std::uint32_t kCodeHandlerAddress = 0x80001800;
constexpr std::uint32_t kCodeHandlerEntry = 0x800018A8;
constexpr std::uint32_t kCodeListAddress = 0x800022A8;
constexpr std::uint32_t kCodeListEnd = 0x80003000;

enum class CodeHook { Retrace, AudioFrame };

// The address of the `blr` to replace, or 0 when no text has the pattern.
std::uint32_t find_code_hook(const std::vector<CodeRange>& text, CodeHook hook);
inline std::uint32_t find_cheat_hook(const std::vector<CodeRange>& text) {
    return find_code_hook(text, CodeHook::Retrace);
}

// The `b` from `from` to `to`, or 0 when out of reach.
std::uint32_t encode_b(std::uint32_t from, std::uint32_t to);

// The four instructions a hook is found by.
const std::uint32_t* code_hook_pattern(CodeHook hook);

// A game that starts another executable: its DOL loader ends with
// "sync; isync; mtctr r31; bctr". The addresses of those `bctr`s, which
// install_cheats points at wii/dolswitch_stub.S (at kDolSwitchStub) so the
// next executable is hooked too (USB Loader GX's multidol hook does the
// same).
std::vector<std::uint32_t> find_dol_jumps(const std::vector<CodeRange>& text);
constexpr std::uint32_t kDolSwitchStub = 0x80001000;
constexpr std::uint32_t kDolSwitchStubEnd = 0x80001300;  // the next exception vector the OS uses

// Points a copy of the handler at a code list elsewhere than
// kCodeListAddress (a big list, where gameconfig.txt says): the handler
// loads the list's address with `lis r15, hi` / `ori r15, r15, lo`.
// False, and the copy untouched, when those two instructions are not
// where they are expected.
bool relocate_code_list(std::uint8_t* handler, std::size_t size, std::uint32_t address);

}  // namespace riftwii
