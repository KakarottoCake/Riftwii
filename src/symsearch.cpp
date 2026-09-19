// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/symsearch.hpp"

#include <map>
#include <set>

namespace riftwii {
namespace {

constexpr std::size_t kCallWindow = 24;      // instructions between the command load and its bl
constexpr std::size_t kArgumentLookback = 8; // the other arguments may be loaded before the command
constexpr std::size_t kBodyWindow = 64;      // instructions of a candidate searched for the IPC command
constexpr std::uint32_t kIpcIoctl = 6;       // IPC request commands (wiibrew IOS IPC)
constexpr std::uint32_t kIpcIoctlv = 7;

// DI commands the SDK's DVD driver sends through IOS_IoctlAsync.
constexpr std::uint32_t kDiCommands[] = {0x71, 0x70, 0x8A, 0x8D, 0x12, 0xE3, 0xE4, 0x79, 0x88, 0xD9, 0xA8, 0xE0};
constexpr std::uint32_t kOpenPartition = 0x8B;

std::uint32_t be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}

std::uint32_t li(unsigned reg, std::uint32_t value) { return 0x38000000u | (reg << 21) | (value & 0xFFFF); }

bool is_bl(std::uint32_t insn) { return (insn & 0xFC000003u) == 0x48000001u; }

std::uint32_t bl_target(std::uint32_t address, std::uint32_t insn) {
    std::int32_t displacement = static_cast<std::int32_t>(insn & 0x03FFFFFCu);
    if (displacement & 0x02000000) displacement -= 0x04000000;
    return address + static_cast<std::uint32_t>(displacement);
}

bool inside(const std::vector<CodeRange>& text, std::uint32_t address) {
    for (const CodeRange& r : text) {
        if (address >= r.address && address < r.address + r.size) return true;
    }
    return false;
}

std::uint32_t word_at(const std::vector<CodeRange>& text, std::uint32_t address) {
    for (const CodeRange& r : text) {
        if (address >= r.address && address + 4 <= r.address + r.size) return be32(r.bytes + (address - r.address));
    }
    return 0;
}

// Calls `visit(target)` for the first bl within kCallWindow instructions
// after every occurrence of `insn` (at word index `site`), provided
// `accept(range, site, end)` holds.
template <typename Accept, typename Visit>
void for_each_call_after(const std::vector<CodeRange>& text, std::uint32_t insn, Accept accept, Visit visit) {
    for (const CodeRange& r : text) {
        const std::size_t words = r.size / 4;
        for (std::size_t i = 0; i < words; ++i) {
            if (be32(r.bytes + i * 4) != insn) continue;
            const std::size_t end = i + 1 + kCallWindow < words ? i + 1 + kCallWindow : words;
            if (!accept(r, i, end)) continue;
            for (std::size_t j = i + 1; j < end; ++j) {
                const std::uint32_t w = be32(r.bytes + j * 4);
                if (is_bl(w)) {
                    visit(bl_target(r.address + static_cast<std::uint32_t>(j * 4), w));
                    break;
                }
            }
        }
    }
}

bool window_contains(const CodeRange& r, std::size_t from, std::size_t to, std::uint32_t insn) {
    for (std::size_t k = from; k < to; ++k) {
        if (be32(r.bytes + k * 4) == insn) return true;
    }
    return false;
}

// A function that starts with a stack frame push (every SDK build) and,
// before its first blr, loads the IPC command number it puts in the
// request block: IOS_IoctlAsync stores 6, IOS_IoctlvAsync 7. The helpers
// the DVD driver calls around them do neither.
bool looks_like_ipc_function(const std::vector<CodeRange>& text, std::uint32_t address, std::uint32_t ipc_command) {
    if (!inside(text, address) || (word_at(text, address) & 0xFFFF0000u) != 0x94210000u) return false;
    for (std::size_t k = 1; k < kBodyWindow; ++k) {
        const std::uint32_t w = word_at(text, address + static_cast<std::uint32_t>(k * 4));
        if (w == 0x4E800020u) break;                                            // blr
        if ((w & 0xFC1FFFFFu) == (0x38000000u | ipc_command)) return true;     // li rX, command
    }
    return false;
}

}  // namespace

bool find_ipc_symbols(const std::vector<CodeRange>& text, IpcSymbols& out, std::string& error,
                      unsigned min_commands) {
    for (const CodeRange& r : text) {
        if (r.bytes == nullptr || (r.address & 3) != 0 || (r.size & 3) != 0) {
            error = "code ranges must be word aligned";
            return false;
        }
    }
    // target -> set of DI commands whose call sites branch to it
    std::map<std::uint32_t, std::set<std::uint32_t>> votes;
    for (const std::uint32_t cmd : kDiCommands) {
        for_each_call_after(
            text, li(4, cmd), [](const CodeRange&, std::size_t, std::size_t) { return true; },
            [&](std::uint32_t target) { votes[target].insert(cmd); });
    }
    std::uint32_t best = 0;
    std::size_t best_votes = 0;
    bool tie = false;
    for (const auto& [target, commands] : votes) {
        if (!commands.count(0x71) || !looks_like_ipc_function(text, target, kIpcIoctl)) continue;
        if (commands.size() > best_votes) {
            best = target;
            best_votes = commands.size();
            tie = false;
        } else if (commands.size() == best_votes) {
            tie = true;
        }
    }
    if (best_votes < min_commands || tie) {
        error = "IOS_IoctlAsync not found: no IPC-shaped function (stwu prologue, li 6) shared by " +
                std::to_string(min_commands) + " DI command call sites" + (tie ? " (tie)" : "");
        return false;
    }
    out.ioctl_async = best;
    out.ioctl_async_commands = static_cast<unsigned>(best_votes);

    out.ioctlv_async = 0;
    std::map<std::uint32_t, unsigned> vvotes;
    for_each_call_after(
        text, li(4, kOpenPartition),
        [](const CodeRange& r, std::size_t site, std::size_t end) {
            // The vector counts (3 in, 2 out) are loaded in whatever order
            // the compiler chose, before or after the command.
            const std::size_t from = site > kArgumentLookback ? site - kArgumentLookback : 0;
            return window_contains(r, from, end, li(5, 3)) && window_contains(r, from, end, li(6, 2));
        },
        [&](std::uint32_t target) { vvotes[target]++; });
    for (const auto& [target, n] : vvotes) {
        (void)n;
        if (target != best && looks_like_ipc_function(text, target, kIpcIoctlv)) {
            if (out.ioctlv_async != 0 && out.ioctlv_async != target) {
                out.ioctlv_async = 0;  // ambiguous: leave it unknown
                break;
            }
            out.ioctlv_async = target;
        }
    }
    error.clear();
    return true;
}

}  // namespace riftwii
