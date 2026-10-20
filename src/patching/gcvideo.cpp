// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gcvideo.hpp"

#include <algorithm>
#include <map>

#include "gc_video.h"

namespace riftwii {
namespace {

// The game's code as words by address.
class Words {
public:
    explicit Words(const std::vector<CodeRange>& text) : text_(text) {}
    bool has(std::uint32_t a) const { return find(a) != nullptr; }
    std::uint32_t at(std::uint32_t a) const {
        const std::uint8_t* p = find(a);
        if (p == nullptr) return 0;
        return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
    }
    template <class F>
    void each(F f) const {
        for (const CodeRange& r : text_)
            for (std::uint32_t i = 0; i + 4 <= r.size; i += 4) f(r.address + i, at(r.address + i));
    }

private:
    const std::uint8_t* find(std::uint32_t a) const {
        for (const CodeRange& r : text_)
            if (a >= r.address && a + 4 <= r.address + r.size) return r.bytes + (a - r.address);
        return nullptr;
    }
    const std::vector<CodeRange>& text_;
};

unsigned opcode(std::uint32_t w) { return w >> 26; }
unsigned reg_d(std::uint32_t w) { return (w >> 21) & 31; }
unsigned reg_a(std::uint32_t w) { return (w >> 16) & 31; }
std::uint16_t immediate(std::uint32_t w) { return static_cast<std::uint16_t>(w & 0xFFFF); }

bool is_lis(std::uint32_t w, std::uint16_t value) { return opcode(w) == 15 && reg_a(w) == 0 && immediate(w) == value; }
bool is_addi_of(std::uint32_t w, unsigned source, std::uint16_t value) {
    return opcode(w) == 14 && reg_a(w) == source && immediate(w) == value;
}

// `beq` (bc 12, a CR field's eq bit, no link) and where it goes.
bool is_beq(std::uint32_t at, std::uint32_t w, std::uint32_t& target) {
    if (opcode(w) != 16 || reg_d(w) != 12 || reg_a(w) % 4 != 2 || (w & 3) != 0) return false;
    const std::int32_t offset = static_cast<std::int16_t>(w & 0xFFFC);
    target = at + static_cast<std::uint32_t>(offset);
    return true;
}

// rlwinm's shift, first and last mask bits, as its low 16 bits hold them
// (rA is above them).
std::uint32_t rlwinm_low(unsigned shift, unsigned first, unsigned last) {
    return (shift << 11) | (first << 6) | (last << 1);
}
bool is_rlwinm_of(std::uint32_t w, unsigned source, std::uint32_t low) {
    return opcode(w) == 21 && reg_d(w) == source && immediate(w) == low;
}

// Whether the bit `take` (rlwinm's low half) is taken from the byte the
// load at `a` reads, within a few words; other code reads the same byte
// to change it.
bool takes_bit(const Words& code, std::uint32_t a, std::uint32_t take) {
    const unsigned loaded = reg_d(code.at(a));
    for (std::uint32_t b = a + 4; b <= a + 16; b += 4)
        if (is_rlwinm_of(code.at(b), loaded, take)) return true;
    return false;
}

constexpr std::uint16_t kSramLockOffset = 0x48;
constexpr std::uint16_t kSramSixtyHz = 0x11;
constexpr std::uint16_t kSramLanguage = 0x12;
constexpr std::uint16_t kSramFlags = 0x13;
constexpr std::uint32_t kBlr = 0x4E800020;

// The sharp weights' two BP register writes, high halves and low ones.
constexpr std::uint16_t kSharpHigh0 = 0x5359, kSharpLow0 = 0x5000;
constexpr std::uint16_t kSharpHigh1 = 0x5400, kSharpLow1 = 0x0015;
// How far before the sharp weights the test of vf may be: the function
// writes the sample pattern (four registers) between them.
constexpr std::uint32_t kTestReach = 0x80;

}  // namespace

GcCopyFilterSwitch find_gc_copy_filter_switch(const std::vector<CodeRange>& text) {
    const Words code(text);
    GcCopyFilterSwitch found;
    unsigned count = 0;
    code.each([&](std::uint32_t a, std::uint32_t w) {
        if (!is_lis(w, kSharpHigh0) || !is_lis(code.at(a + 4), kSharpHigh1)) return;
        // Both halves made into the two words within the next two
        // instructions (the compiler orders them as it likes).
        const unsigned high0 = reg_d(w), high1 = reg_d(code.at(a + 4));
        bool low0 = false, low1 = false;
        for (std::uint32_t b = a + 8; b <= a + 12; b += 4) {
            low0 = low0 || is_addi_of(code.at(b), high0, kSharpLow0);
            low1 = low1 || is_addi_of(code.at(b), high1, kSharpLow1);
        }
        if (!low0 || !low1) return;
        for (std::uint32_t b = a - 4; code.has(b) && a - b <= kTestReach; b -= 4) {
            std::uint32_t target = 0;
            if (is_beq(b, code.at(b), target) && target == a) {
                found.branch = b;
                found.sharp = a;
                ++count;
                return;
            }
        }
    });
    if (count != 1) return GcCopyFilterSwitch{};
    return found;
}

GcSramReads find_gc_sram_reads(const std::vector<CodeRange>& text) {
    const Words code(text);
    struct Site {
        std::uint32_t at;  // the addi that makes the copy's address
        unsigned reg;
        std::uint32_t copy;
    };
    std::vector<Site> sites;
    std::map<std::uint32_t, unsigned> seen;  // copy address -> how many locks
    code.each([&](std::uint32_t a, std::uint32_t w) {
        if (opcode(w) != 15 || reg_a(w) != 0) return;  // lis rA,hi
        const unsigned high_reg = reg_d(w);
        for (std::uint32_t b = a + 4; b < a + 24 && code.has(b); b += 4) {
            const std::uint32_t add = code.at(b);
            if (opcode(add) != 14 || reg_a(add) != high_reg) continue;  // addi rB,rA,lo
            const unsigned copy_reg = reg_d(add);
            const std::uint32_t copy = (w << 16) + static_cast<std::uint32_t>(static_cast<std::int16_t>(add & 0xFFFF));
            const std::uint32_t lock_load = (32u << 26) | (copy_reg << 16) | kSramLockOffset;  // lwz r0,0x48(rB)
            const std::uint32_t lock_addr = (14u << 26) | (4u << 21) | (copy_reg << 16) | kSramLockOffset;  // addi r4,rB,0x48
            for (std::uint32_t c = b + 4; c < b + 48 && code.has(c + 4); c += 4)
                if (code.at(c) == lock_load && code.at(c + 4) == lock_addr) {
                    sites.push_back({b, copy_reg, copy});
                    seen[copy]++;
                    break;
                }
            return;
        }
    });
    GcSramReads out;
    unsigned most = 0;
    for (const auto& [copy, n] : seen)
        if (n > most) {
            most = n;
            out.copy = copy;
        }
    const std::uint32_t take_bit7 = rlwinm_low(25, 31, 31);  // rlwinm rC,rX,25,31,31
    const std::uint32_t take_bit6 = rlwinm_low(26, 31, 31);  // rlwinm rC,rX,26,31,31
    for (const Site& site : sites) {
        if (site.copy != out.copy) continue;
        for (std::uint32_t a = site.at + 4; code.has(a) && a < site.at + 0x100; a += 4) {
            const std::uint32_t w = code.at(a);
            if (w == kBlr) break;
            if (opcode(w) != 34 || reg_a(w) != site.reg) continue;  // lbz rX,d(rB)
            const std::uint16_t offset = immediate(w);
            if (offset == kSramLanguage) out.language.push_back(a);
            if (offset == kSramFlags && takes_bit(code, a, take_bit7)) out.progressive.push_back(a);
            if (offset == kSramSixtyHz && takes_bit(code, a, take_bit6)) out.sixty_hz.push_back(a);
        }
    }
    return out;
}

std::uint32_t gc_vi_width(const std::string& name) {
    if (name == "framebuffer") return GC_VI_WIDTH_FRAMEBUFFER;
    if (name == "704") return 704;
    if (name == "720") return GC_VI_LINE;
    return 0;
}

namespace {

// GXInitTlutObj's stale word: `rlwinm rA,rA,0,0,10` keeps the old load
// word's top 11 bits, so its low half is shift 0, first 0, last 10.
constexpr std::uint32_t kTlutRlwinmLow = (0u << 11) | (0u << 6) | (10u << 1);
// The TLUT load word's BP register tag, ORed into the word's top half.
constexpr std::uint16_t kTlutRegisterTag = 0x6400;
// How far past the `lwz` the `oris` and the `sth` may be: Mario Party 4
// keeps them 6 and 8 words on.
constexpr std::uint32_t kTlutReach = 12 * 4;

bool is_tlut_load(std::uint32_t w, unsigned& kept) {
    if (opcode(w) != 32 || reg_a(w) != 3 || immediate(w) != 4) return false;  // lwz rA,4(r3)
    kept = reg_d(w);
    return true;
}

bool is_tlut_keep(std::uint32_t w, unsigned kept) {
    return opcode(w) == 21 && reg_d(w) == kept && reg_a(w) == kept &&
           immediate(w) == kTlutRlwinmLow;  // rlwinm rA,rA,0,0,10
}

bool is_tlut_cleared(std::uint32_t w, unsigned kept) {
    return opcode(w) == 14 && reg_d(w) == kept && reg_a(w) == 0 &&
           immediate(w) == 0;  // li rA,0
}

bool is_tlut_tag(std::uint32_t w) {
    return opcode(w) == 25 && reg_d(w) == reg_a(w) &&
           immediate(w) == kTlutRegisterTag;  // oris rX,rX,0x6400
}

bool is_tlut_store(std::uint32_t w) {
    return opcode(w) == 44 && reg_d(w) == 6 && reg_a(w) == 3 &&
           immediate(w) == 8;  // sth r6,8(r3)
}

void dedupe(std::vector<std::uint32_t>& addresses) {
    std::sort(addresses.begin(), addresses.end());
    addresses.erase(std::unique(addresses.begin(), addresses.end()), addresses.end());
}

}  // namespace

GcTlutSites find_gc_tlut_sites(const std::vector<CodeRange>& text) {
    const Words code(text);
    GcTlutSites sites;
    code.each([&](std::uint32_t load_at, std::uint32_t load) {
        unsigned kept = 32;
        if (!is_tlut_load(load, kept)) return;
        std::uint32_t stale_at = 0;
        std::uint32_t clean_at = 0;
        for (std::uint32_t at = load_at + 4; at <= load_at + 8; at += 4) {
            if (!code.has(at)) continue;
            const std::uint32_t word = code.at(at);
            if (stale_at == 0 && is_tlut_keep(word, kept)) stale_at = at;
            if (clean_at == 0 && is_tlut_cleared(word, kept)) clean_at = at;
        }
        if (stale_at == 0 && clean_at == 0) return;
        bool tagged = false;
        bool stored = false;
        for (std::uint32_t at = load_at + 4; at <= load_at + kTlutReach; at += 4) {
            if (!code.has(at)) continue;
            const std::uint32_t word = code.at(at);
            if (!tagged && is_tlut_tag(word)) tagged = true;
            if (!stored && is_tlut_store(word)) stored = true;
            if (tagged && stored) break;
        }
        if (!tagged || !stored) return;
        if (stale_at != 0)
            sites.stale.push_back(stale_at);
        else
            sites.clean.push_back(clean_at);
    });
    dedupe(sites.stale);
    dedupe(sites.clean);
    // A site already counted as stale is not also clean when two loads
    // share one word (back to back copies of the function).
    for (std::uint32_t at : sites.stale)
        sites.clean.erase(std::remove(sites.clean.begin(), sites.clean.end(), at), sites.clean.end());
    return sites;
}

std::uint32_t gc_tlut_li_for(std::uint32_t rlwinm_word) {
    const unsigned kept = reg_d(rlwinm_word);
    return (14u << 26) | (kept << 21);  // addi rA,0,0
}

std::vector<std::uint32_t> patch_gc_tlut(const std::vector<CodeRange>& text) {
    const GcTlutSites sites = find_gc_tlut_sites(text);
    for (std::uint32_t at : sites.stale) {
        for (const CodeRange& range : text) {
            if (at < range.address || at + 4 > range.address + range.size) continue;
            std::uint8_t* bytes = const_cast<std::uint8_t*>(range.bytes + (at - range.address));
            const std::uint32_t old =
                (std::uint32_t(bytes[0]) << 24) | (std::uint32_t(bytes[1]) << 16) |
                (std::uint32_t(bytes[2]) << 8) | bytes[3];
            const std::uint32_t patched = gc_tlut_li_for(old);
            bytes[0] = static_cast<std::uint8_t>(patched >> 24);
            bytes[1] = static_cast<std::uint8_t>(patched >> 16);
            bytes[2] = static_cast<std::uint8_t>(patched >> 8);
            bytes[3] = static_cast<std::uint8_t>(patched);
            break;
        }
    }
    return sites.stale;
}

}  // namespace riftwii
