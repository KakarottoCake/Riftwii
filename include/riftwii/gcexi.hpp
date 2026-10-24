// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/symsearch.hpp"

namespace riftwii {

// Same order as runtime/gc/gc_card.h's gc_exi_fn and gc_entry.S's table.
constexpr unsigned kGcExiHooks = 11;
struct GcExiFunctions {
    std::uint32_t blocks = 0;
    std::array<std::uint32_t, kGcExiHooks> hooks{};
    // Only for the log, never to hook: on a rejection, what one table gave
    // (0 for each function it did not), so a problem report says which
    // function kept the memory card off. When tables resolved, it is the
    // first of them; otherwise the table that placed the most functions.
    std::array<std::uint32_t, kGcExiHooks> partial{};
    // The control-block table `partial` belongs to (0: no table was tried).
    std::uint32_t partial_blocks = 0;
    // How many tables had enough users to be tried (more than seven).
    unsigned tables_tried = 0;
    // How many control-block tables resolved every function: 1 when the
    // result was taken, more when the finder refused to choose between them.
    unsigned resolving_tables = 0;
    // The result leaves EXIProbeEx 0 because none was found and the SDK is
    // one that may lack it: the 2001 SDK (`old_sdk`), or the 0x40-byte SDK
    // of a game on the list (not `old_sdk`).
    bool probe_ex_absent = false;
    bool old_sdk = false;
    // For a listed game (see find_gc_exi_functions) whose other ten functions
    // were all placed but whose missing EXIProbeEx was not accepted: which
    // check refused, and where (0 when no single address explains it). Only
    // for the log; set with the table `partial` belongs to.
    enum class ListedRefusal {
        None,
        ProbeLike,           // not exactly one function calls the probe and EXIGetID: EXIProbe is not alone
        AttachDoesNotFollow, // the code after EXIProbe is not EXIAttach (`listed_refusal_at`)
        ProbeCaller,         // a branch to the probe (`listed_refusal_at`) an EXIProbeEx could hide in
    };
    ListedRefusal listed_refusal = ListedRefusal::None;
    std::uint32_t listed_refusal_at = 0;
};

// How far apart two hooked functions must start: a hook overwrites the
// function's first word with a branch, and its replay jumps back to the second
// word, so a second hook within these 8 bytes would be overwritten or entered
// half way.
constexpr std::uint32_t kGcExiHookSpan = 8;

// The SDK's EXI control blocks and all the functions needed for slot A,
// or an empty result when any required function is missing or ambiguous.
// Recognises 0x40-byte blocks (slwi by 6) and the 2001 SDK's 0x38-byte
// blocks (mulli by 0x38). Exactly one table must resolve every function.
// Only the 2001 SDK, and the 0x40-byte SDK of the few games listed by ID
// (`game_id`, the disc's six-character ID) whose code shows no room for
// one, may omit EXIProbeEx.
GcExiFunctions find_gc_exi_functions(const std::vector<CodeRange>& text, const std::string& game_id = {});

// The SDK name of hook `index` (gc_card.h's gc_exi_fn order), for the log:
// a game whose card is off then says which function the finder missed.
const char* gc_exi_function_name(unsigned index);

// Whether the loader may hook slot A with `found`: all or nothing, so a game
// never reaches the Wii's own EXI half way through a card command.
enum class GcExiVerdict {
    Hook,              // every function placed (EXIProbeEx only when absent by design)
    NoTables,          // no control-block table had more than seven users
    NotAllFound,       // tables were tried, none resolved every function
    ListedRefused,     // a listed game's missing EXIProbeEx was refused (found.listed_refusal says why)
    SeveralTables,     // more than one table resolved: the finder will not guess
    Incomplete,        // a result with a function missing (never from the finder)
    StartsWithBranch,  // a function starts with a branch, which a hook cannot replay
    Overlapping,       // two hooks within kGcExiHookSpan of each other (the same address, say)
};
struct GcExiDecision {
    GcExiVerdict verdict = GcExiVerdict::NotAllFound;
    // For Overlapping: the first pair of hooks that collide (gc_card.h's order).
    unsigned overlap_first = 0;
    unsigned overlap_second = 0;
    // For StartsWithBranch: which hooks start with a relative branch.
    std::array<bool, kGcExiHooks> branching{};
};
GcExiDecision decide_gc_exi_hooks(const GcExiFunctions& found, const std::vector<CodeRange>& text);

}  // namespace riftwii
