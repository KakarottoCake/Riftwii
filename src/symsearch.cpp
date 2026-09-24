// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/symsearch.hpp"

#include <algorithm>
#include <cstdio>
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

// ---- the whole API ----------------------------------------------------------

namespace riftwii {
namespace {

constexpr std::uint32_t kApiWindowBytes = 0x2000;  // either side of IOS_IoctlAsync
constexpr std::uint32_t kApiMaxFunctionBytes = 0x400;
constexpr std::size_t kStoreWindow = 8;            // instructions from the li to the stw
constexpr std::uint32_t kBlockRelaunch = 0x28;     // request block field the reboot forms set

bool is_stwu_r1(std::uint32_t w) { return (w & 0xFFFF0000u) == 0x94210000u; }
bool is_li(std::uint32_t w) { return (w & 0xFC1F0000u) == 0x38000000u; }  // addi rX, 0, imm
unsigned rd_of(std::uint32_t w) { return (w >> 21) & 31u; }

// The immediates a function stores into a request block at `offset`:
// `li rX, imm` followed within a few instructions by `stw rX, offset(rY)`.
std::set<std::uint32_t> stored_immediates(const std::vector<CodeRange>& text, std::uint32_t start, std::uint32_t end,
                                          std::uint32_t offset) {
    std::set<std::uint32_t> found;
    for (std::uint32_t a = start; a + 4 <= end; a += 4) {
        const std::uint32_t w = word_at(text, a);
        if (!is_li(w)) continue;
        for (std::size_t k = 1; k <= kStoreWindow && a + k * 4 < end; ++k) {
            const std::uint32_t s = word_at(text, a + static_cast<std::uint32_t>(k * 4));
            if ((s & 0xFC000000u) == 0x90000000u && rd_of(s) == rd_of(w) && (s & 0xFFFFu) == offset) {
                found.insert(w & 0xFFFFu);
                break;
            }
            if ((s & 0xFC000000u) == 0x38000000u && rd_of(s) == rd_of(w)) break;  // the register moved on
        }
    }
    return found;
}

// Targets of the bl instructions in [start, end).
std::set<std::uint32_t> call_targets(const std::vector<CodeRange>& text, std::uint32_t start, std::uint32_t end) {
    std::set<std::uint32_t> targets;
    for (std::uint32_t a = start; a + 4 <= end; a += 4) {
        const std::uint32_t w = word_at(text, a);
        if (is_bl(w)) targets.insert(bl_target(a, w));
    }
    return targets;
}

// How the function hands its block to `submit`: 1 when r4 (the callback)
// is a register copy (asynchronous), 0 when it is `li r4, 0`
// (synchronous), -1 when there is no such call.
int submit_style(const std::vector<CodeRange>& text, std::uint32_t start, std::uint32_t end, std::uint32_t submit) {
    for (std::uint32_t a = start + 8; a + 4 <= end; a += 4) {
        const std::uint32_t w = word_at(text, a);
        if (!is_bl(w) || bl_target(a, w) != submit) continue;
        for (std::uint32_t back = 4; back <= 8; back += 4) {
            const std::uint32_t p = word_at(text, a - back);
            if (rd_of(p) != 4) continue;
            if (is_li(p)) return (p & 0xFFFFu) == 0 ? 0 : 1;
            return 1;  // mr r4, rX or another move
        }
        return 1;
    }
    return -1;
}

}  // namespace

bool find_ipc_api(const std::vector<CodeRange>& text, const IpcSymbols& known, IpcApi& out, std::string& error) {
    if (known.ioctl_async == 0 || !inside(text, known.ioctl_async)) {
        error = "IOS_IoctlAsync must be known to find the rest of the IPC API";
        return false;
    }
    // The stretch of text around it, clipped to the range it lives in.
    const CodeRange* range = nullptr;
    for (const CodeRange& r : text) {
        if (known.ioctl_async >= r.address && known.ioctl_async < r.address + r.size) range = &r;
    }
    const std::uint32_t lo = known.ioctl_async - std::min(known.ioctl_async - range->address, kApiWindowBytes);
    const std::uint32_t hi = std::min(static_cast<std::uint32_t>(range->address + range->size), known.ioctl_async + kApiWindowBytes);

    // Function starts: stack-frame prologues.
    std::vector<std::uint32_t> starts;
    for (std::uint32_t a = lo; a + 4 <= hi; a += 4) {
        if (is_stwu_r1(word_at(text, a))) starts.push_back(a);
    }
    std::size_t known_index = starts.size();
    for (std::size_t i = 0; i < starts.size(); ++i) {
        if (starts[i] == known.ioctl_async) known_index = i;
    }
    if (known_index == starts.size()) {
        error = "IOS_IoctlAsync does not start with a stack frame push";
        return false;
    }
    // The two helpers every API function calls: the block allocator
    // (called with 64, 32: size and alignment) and the submit routine
    // (called with the block and the callback register, or 0).
    const std::uint32_t known_end = known_index + 1 < starts.size() ? starts[known_index + 1] : hi;
    std::uint32_t alloc = 0, submit = 0;
    for (std::uint32_t a = known.ioctl_async + 8; a + 4 <= known_end; a += 4) {
        const std::uint32_t w = word_at(text, a);
        if (!is_bl(w)) continue;
        const std::uint32_t p1 = word_at(text, a - 4), p2 = word_at(text, a - 8);
        if ((p1 == li(5, 32) && p2 == li(4, 64)) || (p1 == li(4, 64) && p2 == li(5, 32))) {
            alloc = bl_target(a, w);
        } else if ((p1 & 0xFC0007FFu) == 0x7C000378u && ((p1 >> 16) & 31u) == 4 && rd_of(p1) == ((p1 >> 11) & 31u)) {
            submit = bl_target(a, w);  // mr r4, rX (or rX, rX, rX) before the call
        }
    }
    if (alloc == 0 || submit == 0) {
        error = "IOS_IoctlAsync does not call a block allocator (64, 32) and a submit routine the expected way";
        return false;
    }
    // Every function in the stretch that uses both helpers and stores
    // one IPC command is an API function; the reboot forms of ioctlv
    // (relaunch set in the block) are left alone.
    for (std::size_t i = 0; i < starts.size(); ++i) {
        const std::uint32_t start = starts[i];
        const std::uint32_t next = i + 1 < starts.size() ? starts[i + 1] : hi;
        if (next - start > kApiMaxFunctionBytes) continue;
        const std::set<std::uint32_t> calls = call_targets(text, start, next);
        if (!calls.count(alloc) || !calls.count(submit)) continue;
        const std::set<std::uint32_t> commands = stored_immediates(text, start, next, 0);
        if (commands.size() != 1) continue;
        const std::uint32_t cmd = *commands.begin();
        if (cmd < 1 || cmd > 7) continue;
        const std::set<std::uint32_t> relaunch = stored_immediates(text, start, next, kBlockRelaunch);
        bool reboots = false;
        for (const std::uint32_t v : relaunch) reboots = reboots || v != 0;
        if (reboots) continue;
        const int style = submit_style(text, start, next, submit);
        if (style < 0) continue;
        std::uint32_t& slot = style == 1 ? out.async[cmd] : out.sync[cmd];
        if (slot != 0) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "two %s functions store IPC command %u near IOS_IoctlAsync",
                          style == 1 ? "asynchronous" : "synchronous", static_cast<unsigned>(cmd));
            error = buf;
            return false;
        }
        slot = start;
    }
    if (out.async[kIpcIoctlCmd] != known.ioctl_async) {
        error = "the IPC API search does not classify IOS_IoctlAsync as the asynchronous ioctl";
        return false;
    }
    if (known.ioctlv_async != 0 && out.async[kIpcIoctlvCmd] != known.ioctlv_async) {
        error = "the IOS_IoctlvAsync found through the partition open disagrees with the API search";
        return false;
    }
    error.clear();
    return true;
}

}  // namespace riftwii

// --- The PAD functions ------------------------------------------------------

namespace riftwii {
namespace {

constexpr std::uint32_t kMflrR0 = 0x7C0802A6u;
constexpr std::uint32_t kBlr = 0x4E800020u;
constexpr std::uint32_t kFunctionLookback = 0x1000;  // bytes walked back to a function's start
constexpr unsigned kMinReadSites = 3;
constexpr std::size_t kMotorWindow = 40;             // instructions from the flag read to the command

// The start of the function containing `address`: the nearest `stwu r1`
// followed by `mflr r0` before it, 0 when a `blr` comes first (the site
// is not inside a framed function) or none is near.
std::uint32_t function_start(const std::vector<CodeRange>& text, std::uint32_t address) {
    for (std::uint32_t back = 0; back <= kFunctionLookback && address >= back; back += 4) {
        const std::uint32_t at = address - back;
        if (!inside(text, at)) return 0;
        const std::uint32_t w = word_at(text, at);
        if (back != 0 && w == kBlr) return 0;
        if ((w & 0xFFFF8000u) == 0x94218000u && word_at(text, at + 4) == kMflrR0) return at;
    }
    return 0;
}

}  // namespace

bool find_pad_symbols(const std::vector<CodeRange>& text, PadSymbols& out, std::string& error) {
    out = PadSymbols{};
    std::map<std::uint32_t, std::set<std::uint32_t>> read_memsets;  // function -> memset targets
    std::map<std::uint32_t, unsigned> read_sites;
    std::set<std::uint32_t> motors;
    for (const CodeRange& r : text) {
        const std::size_t words = r.size / 4;
        for (std::size_t i = 0; i + 4 < words; ++i) {
            const std::uint32_t w = be32(r.bytes + i * 4);
            const std::uint32_t at = r.address + static_cast<std::uint32_t>(i * 4);
            if ((w & 0xFC00FFFFu) == 0x9800000Au) {  // stb rE, 10(rS)
                const std::uint32_t s = (w >> 16) & 31u;
                const std::uint32_t mr = 0x7C000378u | (s << 21) | (3u << 16) | (s << 11);
                const std::uint32_t call = be32(r.bytes + (i + 4) * 4);
                if (be32(r.bytes + (i + 1) * 4) == mr && be32(r.bytes + (i + 2) * 4) == li(4, 0) &&
                    be32(r.bytes + (i + 3) * 4) == li(5, 10) && is_bl(call)) {
                    const std::uint32_t f = function_start(text, at);
                    if (f != 0) {
                        read_memsets[f].insert(bl_target(at + 16, call));
                        ++read_sites[f];
                    }
                }
            }
            if ((w & 0xFC1FFFFFu) == 0x3C008000u) {  // lis rX, 0x8000
                const std::uint32_t x = (w >> 21) & 31u;
                for (std::size_t k = 1; k <= 3 && i + k < words; ++k) {
                    if ((be32(r.bytes + (i + k) * 4) & 0xFC1FFFFFu) != (0x88000000u | (x << 16) | 0x30E3u)) continue;
                    bool command = false;
                    for (std::size_t j = i + k + 1; j < words && j <= i + k + kMotorWindow && !command; ++j) {
                        command = (be32(r.bytes + j * 4) & 0xFC00FFFFu) == 0x64000040u;  // oris rA, rB, 0x40
                    }
                    const std::uint32_t f = command ? function_start(text, at) : 0;
                    if (f != 0) motors.insert(f);
                    break;
                }
            }
        }
    }
    for (const auto& [f, sites] : read_sites) {
        if (sites < kMinReadSites || read_memsets[f].size() != 1) continue;
        if (out.read != 0) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "two PADRead candidates, 0x%08x and 0x%08x", out.read, f);
            error = buf;
            return false;
        }
        out.read = f;
        out.read_sites = sites;
    }
    if (motors.size() > 1) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%u PADControlMotor candidates, the first 0x%08x",
                      static_cast<unsigned>(motors.size()), *motors.begin());
        error = buf;
        return false;
    }
    if (!motors.empty()) out.control_motor = *motors.begin();
    error.clear();
    return true;
}

}  // namespace riftwii
