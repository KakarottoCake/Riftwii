// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// RiftWii's GameCube loader, the gamecube plugin's work (docs/PLUGINS.md,
// "Stage (b)"): moved out of the DOL as it was, with what it used to ask
// the menu's C++ for (the settings, the launch's extras, the way back, the
// crash screen) handed in by gcentry.cpp, and libogc and the card reached
// through RiftWii's table (gchost.hpp).
#include "gchost.hpp"

#include <sys/stat.h>
#include <ogc/machine/processor.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include "gc_bt.h"
#include "gc_gcad.h"
#include "gc_card.h"
#include "gc_rt.h"
#include "gc_rvz.h"
#include "gc_video.h"
#include "gcblobs.h"
#include "riftwii/codehook.hpp"
#include "riftwii/fat32.hpp"
#include "riftwii/gccard.hpp"
#include "riftwii/gcclock.hpp"
#include "riftwii/gccontrols.hpp"
#include "riftwii/gcdsp.hpp"
#include "riftwii/gclayout.hpp"
#include "riftwii/gcreset.hpp"
#include "riftwii/gcvideo.hpp"
#include "riftwii/gcoverlay.hpp"
#include "riftwii/gxpatches.hpp"
#include "riftwii/hook.hpp"
#include "riftwii/launch.hpp"
#include "riftwii/redirect.hpp"
#include "riftwii/rvz.hpp"
#include "riftwii/sha1.hpp"
#include "riftwii/symsearch.hpp"
#include "zstd.h"

namespace riftwii::wii {

namespace {

constexpr std::uint32_t kMem1Top = 0x81800000;
constexpr std::uint32_t kLoaderStart = 0x80A00000;  // RiftWii's own image (Makefile.wii)
constexpr std::uint32_t kLoaderCeiling = 0x81200000;  // its heap's top (memlimits.hpp)
constexpr std::uint32_t kGameFloor = 0x80003100;
constexpr std::uint32_t kBounceBytes = 0x8000;
constexpr std::uint32_t kBi2Bytes = 0x2000;
// The blob's data buffers in MEM2 (the layout in LaunchGcNative): after
// the memory card's image, which follows the game's 16 MB of ARAM, and far
// below IOS (0x933E0000).
constexpr std::uint32_t kGcMem2Data = GC_CARD_IMAGE + GC_CARD_BYTES;
constexpr std::uint32_t kGcMem2DataMax = 0x00100000;
// An RVZ game's group buffers and Zstandard's workspace (gc_rvz.h), right
// after the data buffers: 2 MB, which holds 512 KiB chunks (two decoded
// groups, one as stored and the decoder's 48 KB), still far below IOS.
// Nothing needs to be in them at the jump, so they are not staged.
constexpr std::uint32_t kGcRvzCache = kGcMem2Data + kGcMem2DataMax;
constexpr std::uint32_t kGcRvzCacheMax = 0x00200000;
// The restart's copy of MEM1 as the game starts (Reset, riftwii/gcreset.hpp),
// after the RVZ's buffers: 24 MB at most, ending at 0x92D00000, still below
// IOS. Its last 4 KB are the stack the blob puts MEM1 back on.
constexpr std::uint32_t kGcRestartWindow = kGcRvzCache + kGcRvzCacheMax;
constexpr std::uint32_t kGcRestartWindowBytes = 0x01800000;
constexpr std::uint32_t kGcRestartStackBytes = 0x1000;
// The decoder's workspace: its tables and a 4 KiB literal buffer, as for
// Wii games (runtime/resident/rt_hook.h's RT_RVZ_DCTX_BYTES).
constexpr std::uint32_t kGcRvzDecoderBytes = 0xC000;
// Disc audio read ahead (the ring of runtime/gc's gc_rt_stream): about 0.6 s,
// four top-ups of GC_STREAM_CHUNK, so several frames without one (the card
// busy with the game's own reads) do not run it dry.
constexpr std::uint32_t kStreamBytes = 4 * GC_STREAM_CHUNK;
// Hollywood's registers through the uncached mirror: AHBPROT, whose all
// ones means the PPC may read the rest, and HW_CLOCKS (riftwii/gcclock.hpp).
constexpr std::uint32_t kAhbprot = 0xCD800064;
constexpr std::uint32_t kHwClocks = 0xC0000000 | kHwClocksAddress;

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

struct Section {
    std::uint32_t address = 0;
    std::vector<std::uint8_t> bytes;
};

struct Dol {
    std::vector<Section> text;
    std::vector<Section> data;
    std::uint32_t bss = 0;
    std::uint32_t bss_size = 0;
    std::uint32_t entry = 0;
};

bool read_at(const ByteSource& disc, std::uint64_t offset, void* out, std::size_t length) {
    return disc.read(offset, static_cast<std::uint8_t*>(out), length);
}

bool read_dol(const ByteSource& f, std::uint32_t at, Dol& out, std::string& error) {
    std::uint8_t h[0x100];
    if (!read_at(f, at, h, sizeof(h))) {
        error = "cannot read the game's executable";
        return false;
    }
    for (int i = 0; i < 18; ++i) {
        const std::uint32_t offset = be32(h + 4 * i);
        const std::uint32_t address = be32(h + 0x48 + 4 * i);
        const std::uint32_t size = be32(h + 0x90 + 4 * i);
        if (size == 0) continue;
        if (address < kGameFloor || address + size > kLoaderStart) {
            error = "the game's executable reaches memory RiftWii is still in (not handled yet)";
            return false;
        }
        Section s;
        s.address = address;
        s.bytes.resize(size);
        if (!read_at(f, std::uint64_t(at) + offset, s.bytes.data(), size)) {
            error = "cannot read the game's executable";
            return false;
        }
        (i < 7 ? out.text : out.data).push_back(std::move(s));
    }
    out.bss = be32(h + 0xD8);
    out.bss_size = be32(h + 0xDC);
    out.entry = be32(h + 0xE0);
    if (out.bss_size != 0 && out.bss + out.bss_size > kLoaderStart) {
        error = "the game's memory reaches memory RiftWii is still in (not handled yet)";
        return false;
    }
    return !out.text.empty();
}

// The game's code as words by address, for the search below.
class Code {
public:
    explicit Code(std::vector<Section>& text) : text_(text) {}
    bool has(std::uint32_t a) const { return find(a) != nullptr; }
    std::uint32_t at(std::uint32_t a) const {
        const std::uint8_t* p = find(a);
        return p ? be32(p) : 0;
    }
    void put(std::uint32_t a, std::uint32_t word) {
        std::uint8_t* p = const_cast<std::uint8_t*>(find(a));
        p[0] = word >> 24;
        p[1] = word >> 16;
        p[2] = word >> 8;
        p[3] = word;
    }
    template <class F>
    void each(F f) const {
        for (const Section& s : text_)
            for (std::uint32_t i = 0; i + 4 <= s.bytes.size(); i += 4) f(s.address + i, be32(&s.bytes[i]));
    }
    // The same code for the host-tested finders (riftwii/gcvideo.hpp).
    std::vector<CodeRange> ranges() const {
        std::vector<CodeRange> out;
        for (const Section& s : text_) out.push_back(CodeRange{s.address, s.bytes.data(), s.bytes.size()});
        return out;
    }

private:
    const std::uint8_t* find(std::uint32_t a) const {
        for (const Section& s : text_)
            if (a >= s.address && a + 4 <= s.address + s.bytes.size()) return &s.bytes[a - s.address];
        return nullptr;
    }
    std::vector<Section>& text_;
};

bool is_lis(std::uint32_t w, std::uint16_t value) { return (w & 0xFC1F0000) == 0x3C000000 && (w & 0xFFFF) == value; }
bool is_oris(std::uint32_t w, std::uint16_t value) { return (w >> 26) == 25 && (w & 0xFFFF) == value; }
std::uint32_t bl_target(std::uint32_t at, std::uint32_t w) {
    if ((w & 0xFC000003) != 0x48000001) return 0;
    std::int32_t d = static_cast<std::int32_t>(w & 0x03FFFFFC);
    if (d & 0x02000000) d -= 0x04000000;
    return at + d;
}

// The "lis rX,H" whose register later gets "ori rX,rX,L" (L < 0x8000), with
// nothing between them that branches or gives rX another value: the
// address of the ori, or 0. Then rX's low half is still zero, so "addi
// rX,rX,L" gives the same value.
std::uint32_t later_ori(const Code& code, std::uint32_t at, unsigned reg) {
    if (reg == 0) return 0;  // "addi r0,r0,L" would read r0 as 0: li r0,L
    for (std::uint32_t a = at + 4; a <= at + 64 && code.has(a); a += 4) {
        const std::uint32_t w = code.at(a);
        const unsigned op = w >> 26, d = (w >> 21) & 31, ra = (w >> 16) & 31;
        if (op == 24 && d == reg && ra == reg) return (w & 0xFFFF) < 0x8000 ? a : 0;
        if (op == 16 || op == 18 || op == 19) return 0;  // a branch: another path may join here
        if (op == 31 || (op >= 20 && op <= 30)) {
            if (ra == reg) return 0;  // logical, rotate and X-form results go to rA
            if (op == 31 && d == reg) return 0;  // and arithmetic X-forms to rD
        } else if (d == reg && op != 36 && op != 37 && op != 38 && op != 39 && op != 44 && op != 45 &&
                   op != 47 && op != 52 && op != 53 && op != 54 && op != 55) {
            return 0;  // a load or an immediate into rX (stores only read rD)
        }
    }
    return 0;
}

// Most GameCube games build a hardware register's address as "lis
// rX,0xCC00" and then use it with an offset (lwz rY,0x6008(rX)) or an
// addi, which the finders below follow. Sonic Riders Tournament Edition
// (GXSRTE), built with another compiler, loads 0xCC00 into each register
// first and ORs the register's offset in later (674 of its 687 such
// places; a few of a retail game's ~170). For such a game, those ori
// become the addi that give the same value, so the finders read its code
// as they read the others'; a retail game's code is left as it is.
// Returns how many were changed.
unsigned ori_to_addi(Code& code) {
    unsigned bases = 0, with_ori = 0;
    code.each([&](std::uint32_t a, std::uint32_t w) {
        if (!is_lis(w, 0xCC00)) return;
        ++bases;
        if (later_ori(code, a, (w >> 21) & 31)) ++with_ori;
    });
    if (bases < 20 || with_ori * 2 < bases) return 0;
    std::vector<std::uint32_t> changes;
    code.each([&](std::uint32_t a, std::uint32_t w) {
        if ((w & 0xFC1F0000) != 0x3C000000) return;  // lis rX,H (addis rX,0,H)
        if (std::uint32_t o = later_ori(code, a, (w >> 21) & 31)) changes.push_back(o);
    });
    for (std::uint32_t o : changes) code.put(o, (code.at(o) & 0x03FFFFFF) | (14u << 26));  // ori rA,rS -> addi rD,rA: same fields
    return static_cast<unsigned>(changes.size());
}

// libogc (gc_rt.h, "libogc"). Its SYS_SetAlarm(handle, timespec, cb, arg)
// starts by refusing the null handle (cmpwi r3,-1) and checking the
// handle's type in its top half (srwi rX,r3,16), where OSSetAlarm takes an
// OSAlarm's address. find_dvd_low's OSSetAlarm (the timeout alarm the
// disc code sets) is this in a libogc game.
bool is_ogc_set_alarm(const Code& code, std::uint32_t fn) {
    if (fn == 0 || code.at(fn) != 0x2C03FFFF) return false;
    for (std::uint32_t a = fn + 4; a <= fn + 16; a += 4)
        if ((code.at(a) & 0xFFE0FFFF) == 0x5460843E) return true;  // rlwinm rX,r3,16,16,31
    return false;
}

// libogc's DVD_Init: IRQ_Request(21, __DVDInterruptHandler, NULL), the DI
// interrupt's handler built into r4 just before, then __UnmaskIrq and
// SYS_CreateAlarm(&the disc's timeout alarm), its address r13-relative
// ("addi r3,r13,X"). Both, or false.
bool find_ogc_di_irq(const Code& code, std::uint32_t& handler, std::uint32_t& create) {
    bool found = false;
    code.each([&](std::uint32_t a, std::uint32_t w) {
        if (found || w != 0x38600015) return;  // li r3,21
        std::uint32_t r4 = 0;
        bool r4_known = false;
        for (std::uint32_t b = a - 24; b < a + 24; b += 4) {
            const std::uint32_t x = code.at(b);
            const unsigned op = x >> 26, d = (x >> 21) & 31, ra = (x >> 16) & 31;
            if (op == 15 && d == 4 && ra == 0) {
                r4 = (x & 0xFFFF) << 16;
                r4_known = true;
            } else if (op == 14 && d == 4 && ra == 4 && r4_known) {
                r4 += static_cast<std::int16_t>(x & 0xFFFF);
            } else if (op == 24 && d == 4 && ra == 4 && r4_known) {
                r4 |= x & 0xFFFF;
            }
            if (b <= a || !bl_target(b, x)) continue;
            // The call: then SYS_CreateAlarm within the next few words.
            for (std::uint32_t c = b + 4; c < b + 32; c += 4) {
                if ((code.at(c) & 0xFFFF0000) != 0x386D0000) continue;  // addi r3,r13,X
                if (std::uint32_t t = bl_target(c + 4, code.at(c + 4))) {
                    if (r4_known && code.has(r4)) {
                        handler = r4;
                        create = t;
                        found = true;
                    }
                }
                break;
            }
            break;
        }
    });
    return found;
}

// The stores that start a DI command: a word stored at 0xCC00601C (DICR),
// its address followed through lis, addi, addis and ori within each
// function. Run before redirect_di_registers moves the bases.
std::vector<std::uint32_t> find_di_starts(const Code& code) {
    std::vector<std::uint32_t> sites;
    std::uint32_t value[32] = {};
    bool known[32] = {};
    code.each([&](std::uint32_t a, std::uint32_t w) {
        const unsigned op = w >> 26, d = (w >> 21) & 31, ra = (w >> 16) & 31;
        const std::int32_t imm = static_cast<std::int16_t>(w & 0xFFFF);
        if (op == 15) {  // lis, addis
            const bool from = ra != 0 && known[ra];
            value[d] = (ra == 0 ? 0 : value[ra]) + (static_cast<std::uint32_t>(imm) << 16);
            known[d] = ra == 0 || from;
        } else if (op == 14) {  // li, addi
            value[d] = (ra == 0 ? 0 : value[ra]) + imm;
            known[d] = ra == 0 || known[ra];
        } else if (op == 24) {  // ori rA,rS
            value[ra] = value[d] | (w & 0xFFFF);
            known[ra] = known[d];
        } else if ((op == 36 || op == 37) && ra != 0 && known[ra] && value[ra] + imm == 0xCC00601C) {  // stw, stwu
            sites.push_back(a);
        } else if ((op >= 32 && op <= 35) || (op >= 40 && op <= 43) || op == 46 || op == 7 || op == 8 ||
                   op == 12 || op == 13) {
            known[d] = false;  // a load or an arithmetic immediate into rD
        } else if (op >= 20 && op <= 29) {
            known[ra] = false;  // the other logical and rotate forms write rA
        } else if (op == 31) {
            known[d] = known[ra] = false;  // X-forms: one of the two
        }
        if (op == 37) known[ra] = false;  // stwu moved its base
        if (bl_target(a, w)) {  // a call: the volatile registers are the callee's
            known[0] = false;
            for (unsigned r = 3; r <= 12; ++r) known[r] = false;
        }
        if (w == 0x4E800020) std::fill(std::begin(known), std::end(known), false);  // blr
    });
    return sites;
}

// The DVDLow* functions, found by the DI command each one writes (the
// command words are the drive's, documented on wiibrew and in yagcd).
struct DvdLow {
    std::uint32_t read = 0, seek = 0, disk_id = 0, inquiry = 0, stop_motor = 0, request_error = 0;
    std::uint32_t audio_stream = 0, audio_status = 0, audio_config = 0, reset = 0;
    std::uint32_t set_alarm = 0;
};

// The start of the function holding `at`: the word after the previous blr.
std::uint32_t branch(std::uint32_t from, std::uint32_t to) { return 0x48000000 | ((to - from) & 0x03FFFFFC); }

std::uint32_t function_start(const Code& code, std::uint32_t at) {
    for (std::uint32_t a = at - 4; code.has(a) && at - a < 0x400; a -= 4)
        if (code.at(a) == 0x4E800020) return a + 4;
    return 0;
}

// Whether one of the sixteen instructions after `at` is "addi rD,`reg`,`value`"
// or "ori rA,`reg`,`value`", before anything else loads `reg`. Most SDK
// builds add right after the lis; Sonic Riders Tournament Edition (GXSRTE)
// loads every register's 0xCC00 first and ORs the 0x40 in 9 to 13
// instructions later.
bool adds_to_register(const Code& code, std::uint32_t at, unsigned reg, std::uint16_t value) {
    for (std::uint32_t a = at + 4; a <= at + 64 && code.has(a); a += 4) {
        const std::uint32_t w = code.at(a);
        const unsigned op = w >> 26, d = (w >> 21) & 31, ra = (w >> 16) & 31;
        if (op == 14 && ra == reg && (w & 0xFFFF) == value) return true;
        if (op == 24 && d == reg && (w & 0xFFFF) == value) return true;  // ori: the source is in the D field
        const bool loads = (op >= 32 && op <= 35) || (op >= 40 && op <= 43) || op == 46;
        const bool immediate = op == 7 || (op >= 12 && op <= 15);  // mulli, addic, addi, addis
        const bool logical = op >= 24 && op <= 29;                  // ori, oris, xori, xoris, andi., andis.
        if (d == reg && (loads || immediate)) return false;  // reg gets another value
        if (ra == reg && logical) return false;
    }
    return false;
}

bool find_dvd_low(const Code& code, DvdLow& out, std::string& error) {
    std::uint32_t just_read = 0;
    std::map<std::uint32_t, std::vector<std::uint32_t>> calls;  // target -> call sites
    // Each command's candidates; the same words turn up elsewhere in a
    // game, so only those beside the read, seek and disc ID are taken.
    std::map<std::uint32_t*, std::vector<std::uint32_t>> found;
    code.each([&](std::uint32_t a, std::uint32_t w) {
        if (std::uint32_t t = bl_target(a, w)) calls[t].push_back(a);
        auto take = [&](std::uint32_t& slot) {
            const std::uint32_t f = function_start(code, a);
            if (f != 0) found[&slot].push_back(f);
        };
        if (is_lis(w, 0xA800)) {
            // ReadDiskID adds 0x40 to the read command, within a few
            // instructions: Luigi's Mansion (USA), an early SDK, stores the
            // link register in between (lis r5; stw r0,4(r1); addi r0,r5,0x40).
            if (adds_to_register(code, a, (w >> 21) & 31, 0x40)) take(out.disk_id);
            else take(just_read);
        } else if (is_lis(w, 0xAB00)) take(out.seek);
        else if (is_lis(w, 0x1200)) take(out.inquiry);
        else if (is_lis(w, 0xE300)) take(out.stop_motor);
        else if (is_lis(w, 0xE000)) take(out.request_error);
        else if (is_oris(w, 0xE100)) take(out.audio_stream);
        else if (is_oris(w, 0xE200)) take(out.audio_status);
        else if (is_oris(w, 0xE400)) take(out.audio_config);
    });
    // The read, seek and disc ID functions sit together: the read that has
    // a seek within 4 KB.
    for (std::uint32_t r : found[&just_read])
        for (std::uint32_t k : found[&out.seek])
            for (std::uint32_t d : found[&out.disk_id])
                if (just_read == 0 && std::max({r, k, d}) - std::min({r, k, d}) < 0x1000) {
                    just_read = r;
                    out.seek = k;
                    out.disk_id = d;
                }
    if (just_read == 0) {
        error = "the game's disc functions were not found";
        return false;
    }
    // DVDLowRead may be a wrapper round the function that writes the
    // command (older SDKs seek first on some drives): of the two, the one
    // the rest of the game calls.
    const std::uint32_t lo = std::min({just_read, out.seek, out.disk_id}) - 0x1000;
    const std::uint32_t hi = std::max({just_read, out.seek, out.disk_id}) + 0x1000;
    for (std::uint32_t* slot : {&out.inquiry, &out.stop_motor, &out.request_error, &out.audio_stream,
                                &out.audio_status, &out.audio_config})
        for (std::uint32_t f : found[slot])
            if (*slot == 0 && f >= lo && f <= hi) *slot = f;
    auto outside = [&](std::uint32_t target) {
        std::size_t n = 0;
        for (std::uint32_t site : calls[target])
            if (site < lo || site > hi) ++n;
        return n;
    };
    out.read = just_read;
    std::size_t best = outside(just_read);
    for (std::uint32_t site : calls[just_read]) {
        const std::uint32_t f = function_start(code, site);
        if (f != 0 && f >= lo && f <= hi && outside(f) > best) {
            best = outside(f);
            out.read = f;
        }
    }
    // OSSetAlarm: Seek's last call (its timeout alarm).
    for (std::uint32_t a = out.seek; code.has(a) && code.at(a) != 0x4E800020; a += 4)
        if (std::uint32_t t = bl_target(a, code.at(a))) out.set_alarm = t;
    // DVDLowReset: near the others, toggles the DI reset bit in PI's reset
    // register (0xCC003024) with "ori 1" then "ori 5".
    code.each([&](std::uint32_t a, std::uint32_t w) {
        if (a < lo || a > hi || out.reset != 0) return;
        if ((w >> 26) == 24 && (w & 0xFFFF) == 5 && (code.at(a + 4) & 0xFC00FFFF) == 0x90000000) {
            for (std::uint32_t b = a; b > a - 0x80; b -= 4)
                if ((code.at(b) >> 26) == 24 && (code.at(b) & 0xFFFF) == 1) {
                    out.reset = function_start(code, b);
                    break;
                }
        }
    });
    if (out.set_alarm == 0) {
        error = "the game's OSSetAlarm was not found";
        return false;
    }
    return true;
}

// __OSUnhandledException(exception, context, dsisr, dar), the GameCube
// OS's last stop for a crash: the function that prints "Unhandled
// Exception %d". The SDK loads the address of its block of strings once
// (lis/addi) and adds each string's offset, so a function whose base is
// at most 0x200 bytes before the string and that adds the difference.
std::uint32_t find_unhandled_exception(const Code& code, const Dol& dol) {
    static const char kText[] = "Unhandled Exception %d";
    std::uint32_t text = 0;
    for (const Section& s : dol.data) {
        const auto at = std::search(s.bytes.begin(), s.bytes.end(), kText, kText + sizeof(kText));
        if (at != s.bytes.end()) {
            text = s.address + static_cast<std::uint32_t>(at - s.bytes.begin());
            break;
        }
    }
    if (text == 0) return 0;
    std::uint32_t found = 0;
    code.each([&](std::uint32_t a, std::uint32_t w) {
        if (found != 0 || (w & 0xFC1F0000) != 0x3C000000) return;  // lis rX,hi
        const std::uint32_t reg = (w >> 21) & 31;
        for (std::uint32_t b = a + 4; b <= a + 40; b += 4) {
            const std::uint32_t add = code.at(b);
            if ((add >> 26) != 14 || ((add >> 16) & 31) != reg) continue;  // addi rY,rX,lo
            const std::uint32_t base = (w << 16) + static_cast<std::uint32_t>(static_cast<std::int16_t>(add & 0xFFFF));
            if (base > text || text - base >= 0x200) return;
            const std::uint32_t held = (add >> 21) & 31;
            const std::uint32_t wanted = (14u << 26) | (3u << 21) | (held << 16) | (text - base);  // addi r3,rY,off
            if (base == text && held == 3) {
                found = function_start(code, a);
                return;
            }
            for (std::uint32_t c = b + 4; code.has(c) && c < b + 0x300; c += 4)
                if (code.at(c) == wanted) {
                    found = function_start(code, a);
                    return;
                }
            return;
        }
    });
    return found;
}

// ARAM DMA moves nothing in Wii mode (the blob's gc_aram_copy does the
// game's transfers instead), so __ARChecksize (the function that writes
// 0xDEADBEEF test patterns into ARAM to look for an expansion) would read
// back nonsense. It is sent from where it records "no expansion" (`li r0,0`, then `stw
// r0,x(r13)`) to its last block, which writes the ARAM mode register and
// the size: the 16 MB every console has, none of the test DMAs.
std::uint32_t find_aram_check(const Code& code) {
    std::uint32_t check = 0;
    // __ARChecksize, found by 0xDEADBEEF itself: `lis rX,0xDEAE`, then `addi rY,rX,0xBEEF` within
    // sixteen words. The OS's thread code builds 0xDEADBABE the same way, and
    // matching only the `lis` picked that function in Super Mario Eclipse,
    // so the real check ran (in Dolphin its test DMAs reach main memory).
    // Kirby Air Ride's compiler put twelve other instructions between the
    // two (the `addi` 0x34 bytes on), so with a window of four words its
    // ARAM addresses were never raised and its sound played from MEM1.
    code.each([&](std::uint32_t a, std::uint32_t w) {
        if (check != 0 || (w & 0xFC1FFFFF) != 0x3C00DEAE) return;  // lis rX,0xDEAE
        const std::uint32_t high = (w >> 21) & 31;
        for (std::uint32_t b = a + 4; b <= a + 64 && code.has(b); b += 4) {
            const std::uint32_t x = code.at(b);
            if (x == 0x4E800020) break;  // blr: the function ended without it
            if ((x >> 26) == 14 && ((x >> 16) & 31) == high && (x & 0xFFFF) == 0xBEEF) {  // addi rY,rX,0xBEEF
                check = function_start(code, a);
                return;
            }
        }
    });
    return check;
}

// The 2001 SDK's __ARChecksize (Luigi's Mansion USA) records no "no
// expansion" in r13's data: it keeps the mode bits and the size in two
// registers to the end, and its first test already looks for 32 MB of
// ARAM, writing past 16 MB (in Dolphin that is MEM2 at 0x91000000, the
// memory card's image, whose header it overwrote). So right after the mode
// register's address is set (`sthu rX,0x12(rM)`), the registers get the
// function's own 16 MB answer (`li rMode,N` then `lis rSize,0x100`) and it
// goes to its last block. `to` is that block, `end` the function's blr.
bool skip_aram_size_check_2001(Code& code, std::uint32_t check, std::uint32_t to, std::uint32_t end) {
    // The last block's registers: `or rA,rA,rMode` after the clrrwi, and
    // the size stored to 0x800000D0 (`stw rSize,0xD0(rX)`).
    unsigned mode_reg = 32, size_reg = 32;
    const std::uint32_t or_word = code.at(to + 8);
    if ((or_word >> 26) == 31 && ((or_word >> 1) & 0x3FF) == 444) mode_reg = (or_word >> 11) & 31;  // or rA,rS,rB
    for (std::uint32_t a = to; a < end; a += 4) {
        const std::uint32_t w = code.at(a);
        if ((w >> 26) == 36 && (w & 0xFFFF) == 0xD0) size_reg = (w >> 21) & 31;  // stw rS,0xD0(rX)
    }
    if (mode_reg == 32 || size_reg == 32) return false;
    // The 16 MB answer: `li rMode,N` right before `lis rSize,0x100`.
    std::uint32_t mode = 0x10000;
    for (std::uint32_t a = check + 4; a < to && mode == 0x10000; a += 4)
        if (code.at(a) == ((15u << 26) | (size_reg << 21) | 0x100u)) {
            const std::uint32_t prev = code.at(a - 4);
            if ((prev & 0xFFFF0000u) == ((14u << 26) | (mode_reg << 21))) mode = prev & 0xFFFF;
        }
    // Where the mode register's address is set.
    std::uint32_t after = 0;
    for (std::uint32_t a = check; a < to && after == 0; a += 4) {
        const std::uint32_t w = code.at(a);
        if ((w >> 26) == 45 && (w & 0xFFFF) == 0x12) after = a + 4;  // sthu rX,0x12(rM)
    }
    if (mode == 0x10000 || after == 0 || after + 8 >= to) return false;
    code.put(after, (14u << 26) | (mode_reg << 21) | mode);       // li rMode,N
    code.put(after + 4, (15u << 26) | (size_reg << 21) | 0x100u);  // lis rSize,0x100 (16 MB)
    code.put(after + 8, 0x48000000 | ((to - (after + 8)) & 0x03FFFFFC));  // b to
    return true;
}

bool skip_aram_size_check(Code& code) {
    const std::uint32_t check = find_aram_check(code);
    if (check == 0) return false;
    std::uint32_t from = 0, to = 0, end = 0;
    for (std::uint32_t a = check; code.has(a) && a < check + 0x1000; a += 4)
        if (code.at(a) == 0x4E800020) {
            end = a;
            break;
        }
    if (end == 0) return false;
    for (std::uint32_t a = check + 4; a < end && from == 0; a += 4) {
        const std::uint32_t w = code.at(a);
        const std::uint32_t prev = code.at(a - 4);
        // stw rX,d(r13) right after li rX,0
        if ((w >> 26) == 36 && ((w >> 16) & 31) == 13 && prev == ((14u << 26) | (w & 0x03E00000)))
            from = a + 4;
    }
    // The last block: `lhz rA,0(rM)` then `clrrwi rA,rA,6` (the mode register).
    for (std::uint32_t a = end; a > check && to == 0; a -= 4) {
        const std::uint32_t w = code.at(a);
        const std::uint32_t reg = (w >> 21) & 31;
        if (w == ((21u << 26) | (reg << 21) | (reg << 16) | (25u << 1)) && (code.at(a - 4) >> 26) == 40) to = a - 4;
    }
    if (to != 0 && from == 0) return skip_aram_size_check_2001(code, check, to, end);
    if (from == 0 || to == 0 || to <= from) return false;
    code.put(from, 0x48000000 | ((to - from) & 0x03FFFFFC));  // b to
    return true;
}

// The game's ARAM addresses with bit 28 set (0x10004000 up instead of
// 0x4000 up), so its sound program's reads of ARAM samples go to MEM2,
// where the blob keeps ARAM (gc_aram_copy masks the bit off). On a Wii
// in Wii mode the DSP's reads with bit 28 go to MEM2 and those without
// it to MEM1; Dolphin's DSP does the same. This leaves the game's sound
// program as it is, which Dolphin's fast HLE audio needs: it recognises
// a program by its checksum and stopped on the patched ones.
// ARInit (the function that calls __ARChecksize) starts the allocator at
// `li rX,0x4000`, and returns that base again when called twice; each
// such `li` becomes a branch to a stub of `lis rX,0x1000`, `ori
// rX,rX,0x4000` and a branch back. ARGetBaseAddress (`li r3,0x4000`,
// `blr`, beside ARInit) is changed the same way. Returns how many.
constexpr std::uint32_t kStubBytes = 0x40;  // five stubs of three words
unsigned raise_aram_base(Code& code, std::uint32_t check, std::uint32_t stubs, std::vector<std::uint32_t>& words) {
    if (check == 0) return 0;
    std::uint32_t init = 0;
    code.each([&](std::uint32_t a, std::uint32_t w) {
        if (init == 0 && bl_target(a, w) == check) init = function_start(code, a);
    });
    if (init == 0) return 0;
    std::vector<std::uint32_t> sites;
    auto is_li_4000 = [](std::uint32_t w) { return (w & 0xFC1FFFFF) == 0x38004000; };  // li rX,0x4000
    for (std::uint32_t a = init; code.has(a) && a < init + 0x400; a += 4) {
        const std::uint32_t w = code.at(a);
        if (w == 0x4E800020) break;
        if (is_li_4000(w)) sites.push_back(a);
    }
    if (sites.empty()) return 0;
    for (std::uint32_t a = init - 0x800; a < init + 0x800; a += 4)
        if (code.has(a) && code.has(a + 4) && code.has(a - 4) && code.at(a) == 0x38604000 &&
            code.at(a + 4) == 0x4E800020 && code.at(a - 4) == 0x4E800020)
            sites.push_back(a);
    words.clear();
    for (std::uint32_t a : sites) {
        if (words.size() + 3 > kStubBytes / 4) break;
        const std::uint32_t reg = (code.at(a) >> 21) & 31;
        const std::uint32_t stub = stubs + 4 * static_cast<std::uint32_t>(words.size());
        words.push_back(0x3C001000 | (reg << 21));                // lis rX,0x1000
        words.push_back(0x60004000 | (reg << 21) | (reg << 16));  // ori rX,rX,0x4000
        words.push_back(branch(stub + 8, a + 4));
        code.put(a, branch(a, stub));
    }
    return static_cast<unsigned>(words.size() / 3);
}

// With the ARAM addresses raised (raise_aram_base), a sound engine that
// keeps its own heap in ARAM from what ARAlloc gives it up to ARGetSize(),
// at most 16 MB, would end its heap below where it starts: HAL's engine
// in Kirby Air Ride does `bl ARGetSize`, `lis rX,0x100`, `cmplw r3,rX`
// and keeps the smaller, found no room for its sounds and waited for good
// after its save screen (Melee, by the same studio, does not clamp). Such
// a limit is raised with the addresses: the `lis` gives 0x11000000 and
// the comparison's operands swap, so the raised limit is always taken
// (ARAM is 16 MB on every GameCube). ARGetSize is `lwz r3,d(r13)`, `blr`.
// Returns how many limits were raised.
unsigned raise_aram_limits(Code& code) {
    unsigned raised = 0;
    code.each([&](std::uint32_t a, std::uint32_t w) {
        const std::uint32_t target = bl_target(a, w);
        if (target == 0 || !code.has(target + 4)) return;
        const bool returns_r13_word = (code.at(target) & 0xFFFF0000) == 0x806D0000 && code.at(target + 4) == 0x4E800020;
        if (!returns_r13_word || !code.has(a + 8)) return;
        const std::uint32_t lis = code.at(a + 4);
        if (!is_lis(lis, 0x0100)) return;
        const std::uint32_t reg = (lis >> 21) & 31;
        const std::uint32_t cmplw_r3 = 0x7C030040 | (reg << 11);  // cmplw r3,rX
        if (code.at(a + 8) != cmplw_r3) return;
        code.put(a + 4, (lis & 0xFFFF0000) | 0x1100);              // lis rX,0x1100
        code.put(a + 8, 0x7C000040 | (reg << 16) | (3 << 11));   // cmplw rX,r3
        ++raised;
    });
    return raised;
}

// __AI_SRC_INIT times the audio interface by switching its disc-stream
// playback on and waiting for the stream's sample counter (0xCC006C08)
// to move: `lwz r0,0(rC)`, `cmplw r3,r0`, `beq` back, twice, with
// interrupts off. On a GameCube the drive's streaming ticks it; on a Wii
// in Wii mode nothing does, and Super Mario Eclipse hung there for good
// (Dolphin ticks it anyway). The waits become nops: the calibration sees
// no time pass and keeps its first, normal answer.
unsigned skip_ai_stream_waits(Code& code) {
    unsigned patched = 0;
    code.each([&](std::uint32_t a, std::uint32_t w) {
        if ((w & 0xFFE0FFFF) != 0x80000000) return;  // lwz r0,0(rC)
        if (code.at(a + 4) != 0x7C030040 || code.at(a + 8) != 0x4182FFF8) return;  // cmplw r3,r0; beq- -8
        // Only in a function that drives the AI's control register (0x6C00).
        const std::uint32_t f = function_start(code, a);
        bool ai = false;
        for (std::uint32_t b = f; f != 0 && b < a; b += 4)
            if ((code.at(b) & 0xFFFF) == 0x6C00 && (code.at(b) >> 26) == 32) ai = true;  // lwz rX,0x6C00(rY)
        if (!ai) return;
        code.put(a + 8, 0x60000000);  // nop
        ++patched;
    });
    return patched;
}

// The function that stores to each of `addresses` (DSP registers, built
// from "lis rX,0xCC00" and adds, which a walk through each function
// follows):
// - AIInitDMA: the AI DMA start (0xCC005030, 0xCC005032) and length (0xCC005036);
// - ARStartDMA: the ARAM DMA's main memory address (0xCC005020/22), ARAM
//   address (0xCC005024/26) and length with direction (0xCC005028/2A).
std::uint32_t find_dsp_stores(const Code& code, const std::vector<std::uint32_t>& addresses) {
    std::uint32_t found = 0;
    std::uint32_t fn = 0;
    std::uint32_t value[32] = {};
    bool known[32] = {};
    std::uint32_t seen = 0;
    const std::uint32_t all = (1u << addresses.size()) - 1;
    code.each([&](std::uint32_t a, std::uint32_t w) {
        if (found) return;
        if (fn == 0) {
            fn = a;
            std::fill(std::begin(known), std::end(known), false);
            seen = 0;
        }
        const unsigned op = w >> 26, d = (w >> 21) & 31, ra = (w >> 16) & 31;
        const std::int32_t imm = static_cast<std::int16_t>(w & 0xFFFF);
        if (op == 15 && ra == 0) {  // lis
            value[d] = static_cast<std::uint32_t>(imm) << 16;
            known[d] = true;
        } else if (op == 14 && ra != 0 && known[ra]) {  // addi
            value[d] = value[ra] + imm;
            known[d] = true;
        } else if (op == 24 && known[d]) {  // ori rA,rS: GCC's way (gc_rt.h, "libogc")
            value[ra] = value[d] | (w & 0xFFFF);
            known[ra] = true;
        } else if ((op == 44 || op == 45) && known[ra]) {  // sth, sthu
            const std::uint32_t at = value[ra] + imm;
            for (std::size_t i = 0; i < addresses.size(); ++i)
                if (at == addresses[i]) seen |= 1u << i;
            if (seen == all) found = fn;
        } else if (op >= 32 && op <= 43) {  // a load replaces its target
            known[d] = false;
        }
        if (w == 0x4E800020) fn = 0;  // blr: the next word starts a function
    });
    return found;
}

// PADRead(PADStatus status[4]): for each port it cannot read, it stores
// the error into the status's `err` byte (offset 10) and clears the 10
// bytes before it (`stb rE,10(rS)`, then `li r5,10` and a call): the
// function with the most such places, at least three.
std::uint32_t find_pad_read(const Code& code) {
    std::map<std::uint32_t, unsigned> sites;
    code.each([&](std::uint32_t a, std::uint32_t w) {
        if ((w >> 26) != 38 || (w & 0xFFFF) != 10) return;  // stb rX,10(rY)
        for (std::uint32_t b = a + 4; b <= a + 16; b += 4)
            if (code.at(b) == 0x38A0000A) {  // li r5,10
                if (std::uint32_t f = function_start(code, a)) sites[f]++;
                break;
            }
    });
    std::uint32_t best = 0;
    unsigned most = 2;
    for (auto& [f, n] : sites)
        if (n > most) {
            most = n;
            best = f;
        }
    return best;
}

// The start of the function holding `at` in GCC's code (gc_rt.h,
// "libogc"), which puts a function's epilogue and its blr in the middle:
// the nearest "stwu r1,-N(r1)" before `at` that follows a blr or a b. 0
// for none within 4 KB (a leaf function without a frame).
std::uint32_t gcc_function_start(const Code& code, std::uint32_t at) {
    for (std::uint32_t a = at; code.has(a) && at - a < 0x1000; a -= 4) {
        if ((code.at(a) & 0xFFFF8000) != 0x94218000) continue;  // stwu r1,-N(r1)
        const std::uint32_t before = code.at(a - 4);
        if (before == 0x4E800020 || (before & 0xFC000003) == 0x48000000) return a;
    }
    return 0;
}

// PADRead as GCC builds it (Sonic Riders Tournament Edition): the
// PADStatus of a port it cannot read is cleared by three word stores (0,
// 4, 8) and its err byte (10) written, where the SDK's calls memset with
// 10; the function with the most such places, at least three.
std::uint32_t find_pad_read_gcc(const Code& code) {
    std::map<std::uint32_t, unsigned> sites;
    code.each([&](std::uint32_t a, std::uint32_t w) {
        if ((w >> 26) != 38 || (w & 0xFFFF) != 10) return;  // stb rE,10(rS)
        const unsigned status = (w >> 16) & 31;
        unsigned cleared = 0;
        for (std::uint32_t b = a - 12; b < a; b += 4) {
            const std::uint32_t x = code.at(b);
            const std::uint32_t disp = x & 0xFFFF;
            if ((x >> 26) == 36 && ((x >> 16) & 31) == status && (disp == 0 || disp == 4 || disp == 8)) ++cleared;
        }
        if (cleared == 3)
            if (std::uint32_t f = gcc_function_start(code, a)) sites[f]++;
    });
    std::uint32_t best = 0;
    unsigned most = 2;
    for (auto& [f, n] : sites)
        if (n > most) {
            most = n;
            best = f;
        }
    return best;
}

// Sends the game's DI register accesses to `base` (where 0xCC006000 was):
// each "lis rX,0xCC00" whose register is then only used at 0x6000-0x603F
// gets `base`'s upper half. Returns how many.
unsigned redirect_di_registers(Code& code, std::uint32_t base) {
    std::vector<std::uint32_t> patch;
    code.each([&](std::uint32_t a, std::uint32_t w) {
        if (!is_lis(w, 0xCC00)) return;
        const unsigned r = (w >> 21) & 31;
        bool di = false, other = false;
        for (std::uint32_t b = a + 4; b < a + 4 * 40 && code.has(b); b += 4) {
            const std::uint32_t x = code.at(b);
            const unsigned op = x >> 26, d = (x >> 21) & 31, ra = (x >> 16) & 31;
            const std::uint32_t disp = x & 0xFFFF;
            // A D-form with rA = 0 takes 0, not r0 (li, absolute loads):
            // GCC builds bases in r0 too (gc_rt.h, "libogc").
            if (((op == 14 || (op >= 32 && op <= 47)) && ra == r && ra != 0) || (op == 24 && d == r)) {
                if (disp >= 0x6000 && disp < 0x6040) di = true;
                else other = true;
            }
            if (x == 0x4E800020) break;
            if (op >= 20 && op <= 29 && ra == r) break;  // an ori or rotate into it (GCC's code, gc_rt.h "libogc")
            if ((op == 14 || op == 15 || (op >= 32 && op <= 43)) && d == r && !(op >= 32 && op <= 43 && ra == r && false))
                break;  // the register gets another value
        }
        if (di && !other) patch.push_back(a);
    });
    for (std::uint32_t a : patch) code.put(a, (code.at(a) & 0xFFFF0000) | ((base - 0x6000) >> 16));
    return static_cast<unsigned>(patch.size());
}

// The serial interface (GameCube controllers), EXI (memory cards) and the
// audio interface sit at 0xCD006400-0xCD006FFF on a Wii in Wii mode; a
// GameCube game reaches for them at 0xCC006400-0xCC006FFF, which goes
// nowhere there (so its sound played at the Wii's 48 kHz, 1.5 times too
// fast, and it found no memory card; Dolphin answers at both). Each
// "lis rX,0xCC00" whose register is then only used in that range gets
// 0xCD00. Returns how many; `mixed` counts those also used elsewhere,
// left alone.
unsigned redirect_wii_blocks(Code& code, unsigned& mixed) {
    std::vector<std::uint32_t> patch;
    mixed = 0;
    code.each([&](std::uint32_t a, std::uint32_t w) {
        // "lis rX,0xCC00", or GCC's "addis rX,rY,0xCC00" that adds the base
        // to a channel's index (libogc's SI and EXI, gc_rt.h "libogc").
        if ((w >> 26) != 15 || (w & 0xFFFF) != 0xCC00) return;
        const unsigned r = (w >> 21) & 31;
        bool moved = false, other = false;
        for (std::uint32_t b = a + 4; b < a + 4 * 40 && code.has(b); b += 4) {
            const std::uint32_t x = code.at(b);
            const unsigned op = x >> 26, d = (x >> 21) & 31, ra = (x >> 16) & 31;
            const std::uint32_t disp = x & 0xFFFF;
            // A D-form with rA = 0 takes 0, not r0 (li, absolute loads):
            // GCC builds bases in r0 too (gc_rt.h, "libogc").
            if (((op == 14 || (op >= 32 && op <= 47)) && ra == r && ra != 0) || (op == 24 && d == r)) {
                const bool wii_block = disp >= 0x6400 && disp < 0x7000;
                if (wii_block) moved = true;
                else other = true;
                // An update form (lwzu, sthu...) leaves the register on that
                // block's register: what follows is relative to it.
                if (op >= 32 && (op & 1) && disp >= 0x6400 && disp < 0x7000) break;
            }
            if (x == 0x4E800020) break;
            if ((op == 14 || op == 15 || (op >= 32 && op <= 43)) && d == r) break;  // the register gets another value
            if (op >= 20 && op <= 29 && ra == r) break;  // an ori or rotate into it (GCC's code, gc_rt.h "libogc")
            const unsigned xo = (x >> 1) & 0x1FF;
            if (op == 31 && d == r && (xo == 266 || xo == 40 || xo == 10)) break;  // add, subf, addc into it
        }
        if (moved && !other) patch.push_back(a);
        if (moved && other) ++mixed;
    });
    for (std::uint32_t a : patch) code.put(a, (code.at(a) & 0xFFFF0000) | 0xCD00);
    return static_cast<unsigned>(patch.size());
}

// The sound chip's ARAM in Wii mode. A GameCube game copies its samples
// to ARAM addresses below 16 MB and has the DSP read them back from
// there; in Wii mode the blob keeps ARAM at the start of MEM2 (its ARAM
// DMA moves nothing there), but the DSP's own reads below 0x10000000 go
// to MEM1, so it plays the game's code and data as sound. Each known
// sound program (DSP microcode) the game carries is patched to look in
// MEM2 (riftwii/gcdsp.hpp): the simple kind gets a call to free words
// doing "ori $AC0.M, #0x1000", the AX kind a rewrite that sets the MEM2
// bit for the sample format each sound uses. Used on a Wii (see the
// choice in LaunchGcNative): in Dolphin the game's ARAM addresses are
// raised instead (raise_aram_base).

// Patches every known sound program in the executable; returns how many.
// Unknown ones are left alone (their sound plays from the wrong memory).
unsigned patch_dsp_programs(Dol& dol) {
    unsigned patched = 0;
    auto patch_in = [&](Section& sec) {
        std::vector<std::uint8_t>& b = sec.bytes;
        for (std::size_t i = 0; i + 16 <= b.size();) {
            const GcDspFix* fix = gcdsp_find(&b[i], b.size() - i);
            if (fix == nullptr) {
                i += 2;
                continue;
            }
            if (!gcdsp_apply(*fix, &b[i], b.size() - i)) {
                i += 2;
                continue;
            }
            logf("GameCube: the %s sound program at 0x%08x reads its samples from MEM2 now\n", fix->name,
                 sec.address + static_cast<std::uint32_t>(i));
            ++patched;
            i += fix->length - 2;
        }
    };
    for (Section& sec : dol.text) patch_in(sec);
    for (Section& sec : dol.data) patch_in(sec);
    return patched;
}


// IOS keeps live globals in 0x80003100-0x800031ff on a Wii and rewrites
// a counter at 0x800031a0 several times a second, but a GameCube game has
// code there (Eclipse: memset's fill loop). The function holding that
// word runs from a copy instead: up to 0x100 bytes from its start, with
// the relative branches that leave the copy aimed back where they went;
// the original's first word becomes a branch to the copy. The blob's
// guard still puts IOS's writes back, for anything else in the area.
constexpr std::uint32_t kIosCounter = 0x800031A0;
constexpr std::uint32_t kMovedBytes = 0x100;

bool move_out_of_ios(Code& code, std::uint32_t moved_addr, std::vector<std::uint32_t>& moved) {
    if (!code.has(kIosCounter)) return false;
    const std::uint32_t start = function_start(code, kIosCounter);
    if (start == 0 || start == kIosCounter) return false;
    // Up to the function's blr. A longer function is copied as far as the
    // copy has room for, and its last word jumps back to the rest: without
    // that, it ran on past the copy into whatever followed.
    std::uint32_t length = 0;
    bool whole = false;
    while (length + 4 < kMovedBytes && code.has(start + length)) {
        const bool last = code.at(start + length) == 0x4E800020;
        length += 4;
        if (last) {
            whole = true;
            break;
        }
    }
    if (start + length <= kIosCounter) return false;  // the counter's word would stay where it is
    moved.clear();
    for (std::uint32_t at = 0; at < length; at += 4) {
        std::uint32_t w = code.at(start + at);
        const std::uint32_t op = w >> 26;
        const bool absolute = (w & 2) != 0;
        if (op == 18 && !absolute) {  // b / bl
            std::int32_t d = static_cast<std::int32_t>(w & 0x03FFFFFC);
            if (d & 0x02000000) d -= 0x04000000;
            const std::uint32_t target = start + at + d;
            if (target < start || target >= start + length)
                w = (w & 0xFC000003) | ((target - (moved_addr + at)) & 0x03FFFFFC);
        } else if (op == 16 && !absolute) {  // bc
            const std::int32_t d = static_cast<std::int16_t>(w & 0xFFFC);
            const std::uint32_t target = start + at + d;
            if (target < start || target >= start + length) return false;  // out of a bc's reach from the copy
        }
        moved.push_back(w);
    }
    if (!whole) moved.push_back(branch(moved_addr + length, start + length));  // on with the rest, where it is
    code.put(start, branch(start, moved_addr));
    return true;
}


void store32(std::uint32_t a, std::uint32_t v) { *reinterpret_cast<volatile std::uint32_t*>(a) = v; }
std::uint32_t load32(std::uint32_t a) { return *reinterpret_cast<volatile std::uint32_t*>(a); }

// The clocks the game is told at 0x800000F8 and 0x800000FC, with the
// GameCube clocks built into its code moved to them (riftwii/gcclock.hpp).
// They are the Wii's: only IOS's own processor can switch Hollywood and
// Broadway to a GameCube's 162 and 486 MHz, with Broadway held stopped
// meanwhile (docs/ARCHITECTURE.md, "Clock speed"), and nothing does in
// Wii mode. HW_CLOCKS is read only for the log: the blob's own waits
// (gc_rt.c's drive timing) count the Wii's time base too, so a register
// that read otherwise would be a misreading to report, not a reason to
// tell the game a speed nothing else here runs at.
GcClocks match_game_to_clocks(Code& code, const Dol& dol) {
    const GcClocks clocks = kWiiClocks;
    const std::uint32_t time_base_khz = gc_time_base_hz(clocks) / 1000;
    char register_text[96] = "HW_CLOCKS not read: no hardware access";
    if (load32(kAhbprot) == 0xFFFFFFFF) {
        const std::uint32_t hw_clocks = load32(kHwClocks);
        const bool agrees = gc_clocks_from_hw_clocks(hw_clocks).cpu == clocks.cpu;
        std::snprintf(register_text, sizeof(register_text), "HW_CLOCKS 0x%08x: %s", static_cast<unsigned>(hw_clocks),
                      agrees ? "Wii speed, as expected" : "says GameCube speed, which Wii mode never sets");
    }
    logf("GameCube: clock %u MHz CPU and %u MHz bus, time base %u.%03u MHz, as the game is told (%s)\n",
         clocks.cpu / 1000000, clocks.bus / 1000000, time_base_khz / 1000, time_base_khz % 1000, register_text);

    const GcCodeReader read = [&code](std::uint32_t address, std::uint32_t& word) {
        if (!code.has(address)) return false;
        word = code.at(address);
        return true;
    };
    std::vector<GcTextRange> text_ranges;
    for (const Section& s : dol.text)
        text_ranges.push_back({s.address, s.address + static_cast<std::uint32_t>(s.bytes.size())});
    const std::vector<GcClockFix> fixes = find_gc_clock_values(read, text_ranges, clocks);
    for (const GcClockFix& fix : fixes) {
        code.put(fix.high_at, fix.high_word);
        code.put(fix.low_at, fix.low_word);
        logf("GameCube: clock value %u built into the game's code at 0x%08x changed to %u, the speed it runs at\n",
             fix.old_value, fix.high_at, fix.new_value);
    }
    if (fixes.empty())
        logf("GameCube: clock values: none found built in place with immediates; a folded or float clock would not be found, so this does not prove the game's timing is right\n");
    else
        logf("GameCube: clock values: %u built into the game's code moved to the Wii's\n",
             static_cast<unsigned>(fixes.size()));

    // Floats and doubles holding a clock are only reported: a float in
    // data may be used for more than the clock, so it is not rewritten.
    const GcCodeReader read_data = [&dol](std::uint32_t address, std::uint32_t& word) {
        for (const Section& s : dol.data) {
            const bool inside = address >= s.address && address - s.address + 4 <= s.bytes.size();
            if (!inside) continue;
            const std::uint8_t* b = s.bytes.data() + (address - s.address);
            word = (static_cast<std::uint32_t>(b[0]) << 24) | (static_cast<std::uint32_t>(b[1]) << 16) |
                   (static_cast<std::uint32_t>(b[2]) << 8) | b[3];
            return true;
        }
        return false;
    };
    std::vector<GcTextRange> data_ranges;
    for (const Section& s : dol.data)
        data_ranges.push_back({s.address, s.address + static_cast<std::uint32_t>(s.bytes.size())});
    const std::vector<GcClockConstant> constants = find_gc_clock_constants(read_data, data_ranges);
    for (const GcClockConstant& c : constants)
        logf("GameCube: the game's data holds the GameCube clock %u as a %s at 0x%08x; it is only reported, not changed\n",
             c.hz, c.is_double ? "double" : "float", c.address);
    if (constants.empty())
        logf("GameCube: no float or double holding a GameCube clock found in the game's data sections\n");
    return clocks;
}

// A formatted, empty 251-block card (gc_card.h), its header made from
// this console's SRAM (the card's flash ID for slot A, the clock bias,
// the language), the time and the DTV register; the layout is the
// library's (src/fs/gccard.cpp). Some games (Eclipse) never offer to
// format a blank card and use it as if it were.
void format_card(std::vector<std::uint8_t>& card, bool shift_jis) {
    GcCardFormat format;
    rwp_sram sram;
    plugin::host().read_sram(&sram);
    format.counter_bias = sram.counter_bias;
    format.language = sram.language;
    format.format_time = gettime();
    static_assert(sizeof(sram.flash_id_a) == std::tuple_size<decltype(format.flash_id)>::value,
                  "rwp_sram's flash ID is not a card's");
    std::memcpy(format.flash_id.data(), sram.flash_id_a, format.flash_id.size());
    format.dtv = *reinterpret_cast<volatile std::uint16_t*>(0xCC00206E);
    format.size_mbits = GC_CARD_ID & 0xFC;
    format.shift_jis = shift_jis;
    gc_card_format(card, format);
}

// The SDK's EXI functions (gc_card.h's order), found through the EXI
// control blocks they all index (an array of 0x40-byte blocks, one per
// channel, its state word at 12) and what each does to that state word:
// select sets 4 and deselect clears it, lock sets 16 and unlock clears
// it, attach sets 8 (and probes). EXIImm keeps its buffer and length at 16 and 20;
// EXIDma calls the same interrupt clear as EXIImm without storing them;
// EXIGetID reads the probe times at 0x800030C0 and calls lock, select,
// EXIImm, then EXISync; EXIProbe and EXIProbeEx call the probe select
// uses and EXIGetID, EXIProbeEx returning -1 too. Returns the control
// blocks' address, 0 when anything is missing or doubtful.
//
// The 2001 SDK (Luigi's Mansion USA) differs: its blocks are 0x38 bytes,
// indexed with mulli by 0x38; its EXIGetID does not read the probe times
// (it is the one function calling lock, select and EXIImm); and it has no
// EXIProbeEx, its EXIProbe being the probe select calls. EXIProbeEx is then
// left 0, and the other ten are hooked.
//
// One block size: `index_word` is the instruction that turns a channel
// into its block's offset.
std::uint32_t find_exi_functions_by(const Code& code, std::uint32_t index_word, std::uint32_t (&out)[GC_EXI_HOOKS]) {
    std::fill(std::begin(out), std::end(out), 0);
    // The control blocks: lis/addi pairs near the index instruction at a
    // function's start.
    std::map<std::uint32_t, unsigned> bases;
    std::map<std::uint32_t, std::uint32_t> base_of;  // function -> its base
    code.each([&](std::uint32_t a, std::uint32_t w) {
        if ((w & 0xFC00FFFF) != index_word) return;
        const std::uint32_t f = function_start(code, a);
        if (f == 0 || a - f > 0x40) return;
        std::uint32_t hi[32] = {};
        bool known[32] = {};
        for (std::uint32_t b = f; b < f + 0x50; b += 4) {
            const std::uint32_t x = code.at(b);
            const unsigned op = x >> 26, d = (x >> 21) & 31, ra = (x >> 16) & 31;
            if (op == 15 && ra == 0) {
                hi[d] = (x & 0xFFFF) << 16;
                known[d] = true;
            } else if (op == 14 && ra != 0 && known[ra]) {
                const std::uint32_t value = hi[ra] + static_cast<std::uint32_t>(static_cast<std::int16_t>(x & 0xFFFF));
                if (value >= 0x80000000 && value < 0x81800000) {
                    bases[value]++;
                    base_of[f] = value;
                    return;
                }
            }
        }
    });
    std::uint32_t ecb = 0;
    unsigned most = 7;
    for (auto& [b, n] : bases)
        if (n > most) {
            most = n;
            ecb = b;
        }
    if (ecb == 0) return 0;
    struct Fn {
        std::uint32_t start = 0, size = 0;
        bool sets[32] = {}, clears[32] = {}, stores_16_20 = false, minus_one = false, probe_times = false;
        bool dma_start = false;  // ori rX,rX,3: a DMA transfer's control word
        std::vector<std::uint32_t> calls;
    };
    std::vector<Fn> fns;
    for (auto& [f, b] : base_of) {
        if (b != ecb) continue;
        Fn fn;
        fn.start = f;
        bool st16 = false, st20 = false;
        for (std::uint32_t a = f; code.has(a) && a < f + 0x800; a += 4) {
            const std::uint32_t x = code.at(a);
            const unsigned op = x >> 26;
            if (op == 24 && ((x >> 21) & 31) == ((x >> 16) & 31) && (x & 0xFFFF) < 32 && (x & 0xFFFF) != 0)
                for (unsigned bit = 0; bit < 5; ++bit)
                    if ((x & 0xFFFF) == (1u << bit)) fn.sets[bit] = true;  // ori rX,rX,1<<bit
            if (op == 24 && ((x >> 21) & 31) == ((x >> 16) & 31) && (x & 0xFFFF) == 3) fn.dma_start = true;
            if (op == 21 && ((x >> 11) & 31) == 0) {  // rlwinm rA,rS,0,MB,ME clearing one bit
                const unsigned mb = (x >> 6) & 31, me = (x >> 1) & 31;
                if (mb == (me + 2) % 32) fn.clears[31 - ((me + 1) % 32)] = true;
            }
            if (op == 36 && (x & 0xFFFF) == 16) st16 = true;
            if (op == 36 && (x & 0xFFFF) == 20) st20 = true;
            if (x == 0x3860FFFF) fn.minus_one = true;  // li r3,-1
            if (op == 14 && (x & 0xFFFF) == 0x30C0) fn.probe_times = true;
            if (std::uint32_t t = bl_target(a, x)) fn.calls.push_back(t);
            if (x == 0x4E800020) {
                fn.size = a + 4 - f;
                break;
            }
        }
        fn.stores_16_20 = st16 && st20;
        fns.push_back(fn);
    }
    auto only = [&](auto test) -> std::uint32_t {
        std::uint32_t found = 0;
        for (const Fn& fn : fns)
            if (test(fn)) {
                if (found) return 0;  // two: doubtful
                found = fn.start;
            }
        return found;
    };
    auto calls = [](const Fn& fn, std::uint32_t target) {
        return target != 0 && std::find(fn.calls.begin(), fn.calls.end(), target) != fn.calls.end();
    };
    out[GC_EXI_SELECT] = only([](const Fn& fn) { return fn.sets[2] && !fn.sets[4]; });
    out[GC_EXI_DESELECT] = only([](const Fn& fn) { return fn.clears[2]; });
    out[GC_EXI_LOCK] = only([](const Fn& fn) { return fn.sets[4]; });
    out[GC_EXI_IMM] = only([](const Fn& fn) { return fn.stores_16_20 && fn.sets[1]; });
    out[GC_EXI_GET_ID] = only([&](const Fn& fn) {
        return fn.probe_times && calls(fn, out[GC_EXI_LOCK]) && calls(fn, out[GC_EXI_SELECT]);
    });
    if (out[GC_EXI_GET_ID] == 0)  // the 2001 SDK's, which does not read the probe times
        out[GC_EXI_GET_ID] = only([&](const Fn& fn) {
            return calls(fn, out[GC_EXI_LOCK]) && calls(fn, out[GC_EXI_SELECT]) && calls(fn, out[GC_EXI_IMM]);
        });
    const std::uint32_t get_id = out[GC_EXI_GET_ID];
    // EXIGetID unlocks (and attaches) too: never taken for those.
    out[GC_EXI_UNLOCK] = only([&](const Fn& fn) { return fn.clears[4] && !fn.probe_times && fn.start != get_id; });
    // The probe select calls (in the control blocks' functions).
    std::uint32_t probe = 0;
    for (const Fn& fn : fns)
        if (fn.start == out[GC_EXI_SELECT])
            for (std::uint32_t t : fn.calls)
                for (const Fn& g : fns)
                    if (g.start == t && probe == 0) probe = t;
    out[GC_EXI_ATTACH] = only(
        [&](const Fn& fn) { return fn.sets[3] && calls(fn, probe) && !fn.probe_times && fn.start != get_id; });
    out[GC_EXI_PROBE] = only([&](const Fn& fn) {
        return calls(fn, probe) && calls(fn, out[GC_EXI_GET_ID]) && !fn.minus_one && !fn.sets[3] && fn.size < 0x100;
    });
    out[GC_EXI_PROBE_EX] = only([&](const Fn& fn) {
        return calls(fn, probe) && calls(fn, out[GC_EXI_GET_ID]) && fn.minus_one && !fn.sets[3];
    });
    // The 2001 SDK: no EXIProbe around the probe, and no EXIProbeEx.
    const bool old_sdk = index_word == 0x1C000038;  // the 0x38-byte blocks
    const bool old_probe = old_sdk && out[GC_EXI_PROBE] == 0 && out[GC_EXI_PROBE_EX] == 0 && probe != 0;
    if (old_probe) out[GC_EXI_PROBE] = probe;
    // EXIDma: the other one that starts a transfer, with DMA and start
    // set in the control word.
    out[GC_EXI_DMA] = only([&](const Fn& fn) { return fn.dma_start && fn.start != out[GC_EXI_IMM]; });
    // EXISync: what EXIGetID calls right after its first EXIImm.
    for (const Fn& fn : fns)
        if (fn.start == out[GC_EXI_GET_ID])
            for (std::size_t i = 0; i + 1 < fn.calls.size(); ++i)
                if (fn.calls[i] == out[GC_EXI_IMM]) {
                    out[GC_EXI_SYNC] = fn.calls[i + 1];
                    break;
                }
    for (unsigned i = 0; i < GC_EXI_HOOKS; ++i)
        if (out[i] == 0 && !(i == GC_EXI_PROBE_EX && old_probe)) return 0;
    return ecb;
}

// The 0x40-byte blocks first ("slwi rX,rY,6"), then the 2001 SDK's 0x38-byte
// ones ("mulli rX,rY,0x38"), each kept only if all the functions are found
// on it: in Luigi's Mansion (USA) a table of the game's own has more
// slwi-by-6 users than anything else, and in Twilight Princess another has
// more mulli-by-0x38 users than its EXI blocks.
std::uint32_t find_exi_functions(const Code& code, std::uint32_t (&out)[GC_EXI_HOOKS]) {
    if (const std::uint32_t ecb = find_exi_functions_by(code, 0x54003032, out)) return ecb;
    return find_exi_functions_by(code, 0x1C000038, out);
}

// Instruction fields, for the searches below.
unsigned opcode(std::uint32_t w) { return w >> 26; }
unsigned reg_d(std::uint32_t w) { return (w >> 21) & 31; }
unsigned reg_a(std::uint32_t w) { return (w >> 16) & 31; }
std::uint32_t li(unsigned reg, std::uint16_t value) { return (14u << 26) | (reg << 21) | value; }  // addi reg,0,value
// rlwinm's shift, first and last mask bits, as its low 16 bits hold them
// (rA is above them).
std::uint32_t rlwinm_low(unsigned shift, unsigned first, unsigned last) {
    return (shift << 11) | (first << 6) | (last << 1);
}
bool is_rlwinm_of(std::uint32_t w, unsigned source, std::uint32_t low) {
    return opcode(w) == 21 && reg_d(w) == source && (w & 0xFFFF) == low;
}

// VIGetDTVStatus: reads the VI's DTV register (0xCC00206E, bit 0 set
// while a component cable is plugged in, YAGCD's VI_DTV) with `lhz
// rX,0x206E(rY)` and keeps its low two bits (`clrlwi rZ,rX,30`). Other
// OS code reads the register too, for its other bits.
std::vector<std::uint32_t> find_dtv_reads(const Code& code) {
    std::vector<std::uint32_t> found;
    const std::uint32_t low_two_bits = rlwinm_low(0, 30, 31);
    code.each([&](std::uint32_t a, std::uint32_t w) {
        if (opcode(w) != 40 || (w & 0xFFFF) != 0x206E) return;  // lhz rX,0x206E(rY)
        if (is_rlwinm_of(code.at(a + 4), reg_d(w), low_two_bits)) found.push_back(a);
    });
    return found;
}

// VIConfigure(const GXRenderModeObj* rm): near its start it keeps rm in a
// register, loads rm->viTVMode (`lwz rX,0(rR)`) and takes its low two
// bits (interlaced, double-strike or progressive), reads the start-up TV
// mode at 0x800000CC, and shifts viTVMode right by 2 (its TV format). The
// one function doing all three in its first 0x80 bytes; 0 if not exactly one.
std::uint32_t find_vi_configure(const Code& code) {
    std::uint32_t found = 0;
    unsigned count = 0;
    const std::uint32_t low_two_bits = rlwinm_low(0, 30, 31);
    const std::uint32_t shift_right_2 = rlwinm_low(30, 2, 31);
    code.each([&](std::uint32_t start, std::uint32_t) {
        if (!code.has(start - 4) || code.at(start - 4) != 0x4E800020) return;  // a function's first word
        bool takes_interlace = false, reads_boot_mode = false, takes_format = false;
        bool high_8000[32] = {};
        for (std::uint32_t a = start; code.has(a) && a < start + 0x80; a += 4) {
            const std::uint32_t w = code.at(a);
            if (w == 0x4E800020) break;
            if (opcode(w) == 15 && reg_a(w) == 0) high_8000[reg_d(w)] = (w & 0xFFFF) == 0x8000;  // lis rA,0x8000
            if (opcode(w) == 32 && high_8000[reg_a(w)] && (w & 0xFFFF) == 0xCC) reads_boot_mode = true;
            if (opcode(w) == 32 && (w & 0xFFFF) == 0 && reg_a(w) > 3) {  // lwz rX,0(rR), rR kept from r3
                for (std::uint32_t b = a + 4; b < a + 24; b += 4)
                    if (is_rlwinm_of(code.at(b), reg_d(w), low_two_bits)) takes_interlace = true;
            }
            if (opcode(w) == 21 && (w & 0xFFFF) == shift_right_2) takes_format = true;
        }
        if (takes_interlace && reads_boot_mode && takes_format) {
            found = start;
            ++count;
        }
    });
    return count == 1 ? found : 0;
}

// GXSetProjection(f32 mtx[4][4], GXProjectionType type): it compares the
// type with 1 (orthographic) at once and loads mtx's terms GX keeps: [0][0],
// [1][1], [2][2], [2][3] (0x00, 0x14, 0x28, 0x2C) and, by the type, [0][2]
// and [1][2] or [0][3] and [1][3] (0x08, 0x18 or 0x0C, 0x1C). The one
// function that does it all in its first 0x80 bytes; 0 if not exactly one.
std::uint32_t find_gx_set_projection(const Code& code) {
    std::uint32_t found = 0;
    unsigned count = 0;
    code.each([&](std::uint32_t start, std::uint32_t) {
        if (!code.has(start - 4) || code.at(start - 4) != 0x4E800020) return;
        bool compares_type = false;
        std::uint32_t loaded = 0;  // bit n: the word at 4n of mtx
        for (std::uint32_t a = start; code.has(a) && a < start + 0x80; a += 4) {
            const std::uint32_t w = code.at(a);
            if (w == 0x4E800020) break;
            if (w == 0x2C040001 && a < start + 0x20) compares_type = true;  // cmpwi r4,1
            if (opcode(w) == 48 && reg_a(w) == 3 && (w & 0xFFFF) < 0x40 && (w & 3) == 0)  // lfs fX,d(r3)
                loaded |= 1u << ((w & 0xFFFF) / 4);
        }
        const std::uint32_t wanted = (1u << 0) | (1u << 2) | (1u << 3) | (1u << 5) | (1u << 6) | (1u << 7) |
                                     (1u << 10) | (1u << 11);
        if (compares_type && (loaded & wanted) == wanted) {
            found = start;
            ++count;
        }
    });
    return count == 1 ? found : 0;
}

}  // namespace

// The way back from the game (gc_rt.c's gc_leave): the title holding
// HOME launches, with its ticket view, and /dev/es opened here; IOS keeps
// it open across the jump, as it keeps the SD card's. The RiftWii channel
// when it is installed and Settings > Return to says RiftWii, else the
// Wii Menu: the DOL picks the title and reads its ticket view
// (wii/launch/gcplugin.cpp's way_back_title), so the plugin makes no ES
// calls of its own.
constexpr std::uint32_t kLeaveBytes = 0x100;
constexpr u64 kWiiMenuTitle = 0x0000000100000002ull;
// The ticket view's place in the block (gc_rt.c reads it from there).
constexpr std::uint32_t kLeaveViewOffset = 0x20;

static void prepare_leave(gc_rt_context* ctx, std::uint32_t buf, bool test, const GcLaunchExtras& extras) {
    ctx->leave_fd = 0xFFFFFFFFu;
    ctx->leave_buf = 0;
    ctx->leave_hold = 2 * 60750000u;  // two seconds of the timebase (the bus clock / 4)
    ctx->leave_since = 0;
    auto* bytes = reinterpret_cast<std::uint8_t*>(buf);
    std::memset(bytes, 0, kLeaveBytes);
    const bool view_fits = extras.way_back_view_bytes <= kLeaveBytes - kLeaveViewOffset;
    if (const u64 title = view_fits ? extras.way_back_title : 0) {
        const s32 fd = plugin::host().ios_open("/dev/es", 0);
        if (fd < 0) {
            logf("GameCube: no way back to RiftWii: /dev/es did not open (%d)\n", fd);
            return;
        }
        for (unsigned i = 0; i < 8; ++i) bytes[i] = static_cast<std::uint8_t>(title >> (56 - 8 * i));
        std::memcpy(bytes + kLeaveViewOffset, extras.way_back_view, extras.way_back_view_bytes);
        DCFlushRange(bytes, kLeaveBytes);
        ctx->leave_fd = static_cast<std::uint32_t>(fd);
        ctx->leave_buf = buf;
        logf("GameCube: holding HOME for two seconds goes to %08x-%08x\n", static_cast<u32>(title >> 32),
             static_cast<u32>(title));
        return;
    }
    logf("GameCube: no way back to RiftWii: none of the titles to go back to is installed\n");
    // The test of the way back (sd:/riftwii/gc/test/leave) goes on regardless,
    // to the Wii Menu with no ticket view: IOS refuses, and the game must
    // carry on.
    if (!test) return;
    const s32 fd = plugin::host().ios_open("/dev/es", 0);
    if (fd < 0) return;
    for (unsigned i = 0; i < 8; ++i) bytes[i] = static_cast<std::uint8_t>(kWiiMenuTitle >> (56 - 8 * i));
    DCFlushRange(bytes, kLeaveBytes);
    ctx->leave_fd = static_cast<std::uint32_t>(fd);
    ctx->leave_buf = buf;
    logf("GameCube: the test of the way back goes on without a ticket view, to be refused\n");
}

// Luigi's Mansion's file loader (gc_on_load_queued and on, gc_rt.c): its
// loading thread allocates a load's buffer, and its callbacks what they
// make, from JSystem's current heap, a global, at whatever moment the
// thread runs; with reads at the SD card's speed that was another
// thread's temporary heap. Found by the game's own code:
//   the thread: lbz r3,0x80(r30); bl group; mr; bl align; slwi r4,r3,5;
//               lwz r3,0x60(r30); bl alloc (replaced: gc_on_load_alloc);
//               stw r3,0x64(r30)
//   the queue:  addi r4,r4,31; clrrwi r4,r4,5; stw r4,0x60(r3), then
//               stw rX,0x64(r3) and, a few words on, bl post
// The queue's post is the call replaced (two queue functions, one post).
// Then, further on in the thread, its callbacks: lbz r3,0x80(r30); bl
// group (replaced: gc_on_load_callbacks), and after them mr r3,r29; bl
// group (replaced: gc_on_load_callbacks_done). The current heap's place
// is alloc's first lwz rX,d(r13). Only the games gxpatches.hpp's
// gc_loads_use_queued_heap names are looked at.
struct LmLoader {
    std::uint32_t group = 0, align = 0, alloc = 0, alloc_site = 0, post = 0;
    std::vector<std::uint32_t> post_sites;
    std::uint32_t heap_offset = 0, callbacks_site = 0, callbacks_done_site = 0;
};

LmLoader find_lm_loader(const std::string& id, const Code& code) {
    LmLoader out;
    if (!gc_loads_use_queued_heap(id)) return out;
    unsigned threads = 0;
    bool one_post = true;
    code.each([&](std::uint32_t a, std::uint32_t w) {
        if (w != 0x54642834u) return;  // slwi r4,r3,5
        if (!code.has(a - 16) || !code.has(a + 12)) return;
        const std::uint32_t load_group = code.at(a - 16);
        if ((load_group & 0xFFE0FFFFu) != 0x88600080u) return;  // lbz r3,0x80(rR)
        const std::uint32_t r = (load_group >> 16) & 31;
        if (code.at(a + 4) != (0x80600060u | (r << 16)) || code.at(a + 12) != (0x90600064u | (r << 16))) return;
        const std::uint32_t group = bl_target(a - 12, code.at(a - 12));
        const std::uint32_t align = bl_target(a - 4, code.at(a - 4));
        const std::uint32_t alloc = bl_target(a + 8, code.at(a + 8));
        if (group == 0 || align == 0 || alloc == 0 || r != 30) return;
        out.group = group;
        out.align = align;
        out.alloc = alloc;
        out.alloc_site = a + 8;
        ++threads;
        // The callbacks, further on in the same loop.
        for (std::uint32_t b = a + 16; b < a + 0x100 && code.has(b + 4); b += 4) {
            if (out.callbacks_site == 0 && code.at(b) == 0x887E0080u && bl_target(b + 4, code.at(b + 4)) == group)
                out.callbacks_site = b + 4;
            else if (out.callbacks_site != 0 && code.at(b) == 0x7FA3EB78u && bl_target(b + 4, code.at(b + 4)) == group) {
                out.callbacks_done_site = b + 4;
                break;
            }
        }
    });
    code.each([&](std::uint32_t a, std::uint32_t w) {
        if (w != 0x90830060u) return;  // stw r4,0x60(r3)
        if (!code.has(a - 8) || code.at(a - 8) != 0x3884001Fu || code.at(a - 4) != 0x54840034u) return;
        for (std::uint32_t b = a + 4; b < a + 32 && code.has(b); b += 4) {
            if ((code.at(b) & 0xFC1FFFFFu) != 0x90030064u) continue;  // stw rX,0x64(r3)
            for (std::uint32_t c = b + 4; c < b + 16 && code.has(c); c += 4) {
                const std::uint32_t post = bl_target(c, code.at(c));
                if (post == 0) continue;
                if (out.post != 0 && post != out.post) one_post = false;
                out.post = post;
                out.post_sites.push_back(c);
                break;
            }
            break;
        }
    });
    if (threads != 1 || !one_post || out.post_sites.empty() || out.post_sites.size() > 4) return LmLoader{};
    if (out.callbacks_site == 0 || out.callbacks_done_site == 0) return LmLoader{};
    // alloc, or the function it calls first (a wrapper).
    std::uint32_t fn = out.alloc;
    for (int depth = 0; depth < 2 && out.heap_offset == 0 && fn != 0; ++depth) {
        std::uint32_t first_call = 0;
        for (std::uint32_t a = fn; a < fn + 0x40 && code.has(a); a += 4) {
            const std::uint32_t w = code.at(a);
            if ((w >> 26) == 32 && ((w >> 16) & 31) == 13) {  // lwz rX,d(r13)
                out.heap_offset =
                    static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(w & 0xFFFF)));
                break;
            }
            if (first_call == 0) first_call = bl_target(a, w);
            if (w == 0x4E800020u) break;  // blr
        }
        fn = first_call;
    }
    return out;
}

// The game's picture and language options (its Options page), patched
// into the staged code. `replay_hook(fn, entry, replay, back)` hooks a
// function that then runs on (false: it cannot be hooked).
template <class ReplayHook>
static void apply_picture_and_language(const GcImage& image, Code& code, ReplayHook replay_hook, const gc_rt_header& h,
                                       gc_rt_context* ctx, bool test_no_widen, bool test_no_vi_hook) {
    const GcSramReads sram = find_gc_sram_reads(code.ranges());
    // VIConfigure, hooked once for what changes the mode it is given
    // (gc_on_vi_configure): 480p and the picture width.
    const bool pal_480p = image.progressive && gc_pal_game(image.id);
    const bool progressive = image.progressive && !pal_480p;
    std::uint32_t vi_configure = 0;
    bool vi_hooked = false;
    // A 60 Hz game's mode is the game's own: hooked only for the trace's
    // record of the mode it picked.
    // The test of the picture without it (sd:/riftwii/gc/test/novihook):
    // 480p is still offered, and the game switches to its own 480p mode,
    // if it has one, as on a GameCube with a component cable.
    if (test_no_vi_hook) {
        logf("GameCube: VIConfigure left as made, as sd:/riftwii/gc/test/novihook asks: the game picks its own modes\n");
    } else if (progressive || image.vi_width != 0 || image.sixty_hz) {
        vi_configure = find_vi_configure(code);
        vi_hooked = vi_configure != 0 &&
                    replay_hook(vi_configure, h.hook_vi_configure, h.replay_vi_configure, h.continue_vi_configure);
    }
    // 480p. A game offers it when VIGetDTVStatus says a component cable is
    // in and OSGetProgressiveMode says the console's 480p setting is on
    // (or the player holds B at start): both are told yes. A game that has
    // no 480p is switched to it by VIConfigure (gc_on_vi_configure).
    if (pal_480p) {
        logf("GameCube: 480p left off: %s is a PAL game, its picture is not NTSC's 480 lines\n", image.id.c_str());
    } else if (progressive) {
        const std::vector<std::uint32_t> dtv = find_dtv_reads(code);
        for (std::uint32_t a : dtv) code.put(a, li(reg_d(code.at(a)), 1));  // a component cable is in
        for (std::uint32_t a : sram.progressive) code.put(a, li(reg_d(code.at(a)), 0x80));  // the 480p setting is on
        if (vi_hooked) ctx->flags |= GC_FLAG_PROGRESSIVE;
        logf("GameCube: 480p: %u cable check(s) and %u 480p setting read(s) answer yes; VIConfigure 0x%08x %s\n",
             static_cast<unsigned>(dtv.size()), static_cast<unsigned>(sram.progressive.size()), vi_configure,
             vi_hooked ? "turns interlaced modes into 480p" : "not hooked: a game without its own 480p stays interlaced");
    }
    // Game speed: a European game is told its TV takes 60 Hz. Each read of
    // either 60 Hz flag (riftwii/gcvideo.hpp) answers yes, and a game with
    // a 60 Hz mode of its own picks it itself, as on a European console
    // set that way. Games made for 50 Hz alone are not converted: their
    // logic runs once a frame, so 60 Hz would play them a fifth faster.
    if (image.sixty_hz) {
        for (std::uint32_t a : sram.sixty_hz) code.put(a, li(reg_d(code.at(a)), 0x40));     // bit 6 at 0x11
        for (std::uint32_t a : sram.progressive) code.put(a, li(reg_d(code.at(a)), 0x80));  // bit 7 at 0x13
        logf("GameCube: 60 Hz: %u read(s) of the console's 60 Hz setting and %u of the older SDK's flag answer yes; "
             "the game switches only if it has a 60 Hz mode of its own (VIConfigure %s)\n",
             static_cast<unsigned>(sram.sixty_hz.size()), static_cast<unsigned>(sram.progressive.size()),
             vi_hooked ? "hooked, its mode in the trace" : "not hooked");
    }
    // Picture width: VIConfigure draws the picture that wide, centred.
    if (image.vi_width != 0) {
        if (vi_hooked) ctx->vi_width = image.vi_width;
        const std::string wanted = image.vi_width == GC_VI_WIDTH_FRAMEBUFFER ? std::string("the frame buffer's width")
                                                                             : std::to_string(image.vi_width) + " pixels";
        if (vi_hooked) {
            logf("GameCube: picture width %s, centred, through VIConfigure 0x%08x\n", wanted.c_str(), vi_configure);
        } else {
            logf("GameCube: picture width %s not applied: VIConfigure was not found, so the game keeps its own\n",
                 wanted.c_str());
        }
    }
    // 16:9: GXSetProjection widens what the game draws in perspective
    // (gc_hook_projection); its 2D, drawn with orthographic projections,
    // stays as it is and is stretched by the TV.
    if (image.widescreen) {
        const std::uint32_t projection = find_gx_set_projection(code);
        const bool hooked = projection != 0 &&
                            replay_hook(projection, h.hook_projection, h.replay_projection, h.continue_projection);
        // The test of the hook alone (sd:/riftwii/gc/test/nowiden): every
        // projection goes through it and is copied, none is widened.
        if (hooked && !test_no_widen) ctx->flags |= GC_FLAG_WIDEN;
        const char* what = !hooked ? "not hooked: 4:3 as before"
                           : test_no_widen ? "hooked but not widened (the test of the hook alone)"
                                           : "widens 3D";
        logf("GameCube: 16:9: GXSetProjection 0x%08x %s\n", projection, what);
    }
    // Sharper picture: GXSetCopyFilter always writes the weights that copy
    // each line as it is (riftwii/gcvideo.hpp).
    if (image.sharp) {
        const GcCopyFilterSwitch filter = find_gc_copy_filter_switch(code.ranges());
        if (filter.branch != 0) {
            code.put(filter.branch, branch(filter.branch, filter.sharp));
            logf("GameCube: sharper picture: GXSetCopyFilter's blur test at 0x%08x now always takes the sharp "
                 "weights at 0x%08x\n", filter.branch, filter.sharp);
        } else {
            logf("GameCube: sharper picture not applied: GXSetCopyFilter was not found, so the game keeps its own "
                 "blur\n");
        }
    }
    // The language: OSGetLanguage's read of the settings' copy becomes the
    // chosen number. The console's SRAM is left as it is (sram above).
    if (image.language >= 0) {
        for (std::uint32_t a : sram.language)
            code.put(a, li(reg_d(code.at(a)), static_cast<std::uint16_t>(image.language)));
        logf("GameCube: the game is told the language is %s (%u read(s) of the settings at 0x%08x)\n",
             kGcLanguages[image.language], static_cast<unsigned>(sram.language.size()), sram.copy);
    }
}

// An RVZ game's reader as the loader sets it up (gc_rvz.h): the RVZ's
// group table for the disc, written to the card, and where the RVZ and that
// file lie on the card.
struct GcRvzSetup {
    RvzRuntimeTable table;
    RvzBufferSizes sizes;
    std::vector<gc_rvz_extent> extents;        // the RVZ
    std::vector<gc_rvz_extent> table_extents;  // sd:/riftwii/rvz/<ID>.groups
    std::uint32_t disc_bytes = 0;
    std::uint32_t chunk_bytes = 0;
    std::uint8_t header[GC_RVZ_HEADER_BYTES] = {};
};

// A file's pieces on the card as the reader takes them: which of its
// sectors lie where.
std::vector<gc_rvz_extent> extents_of(const Fat32File& file) {
    std::vector<gc_rvz_extent> out;
    std::uint32_t file_sector = 0;
    for (const auto& fragment : file.fragments) {
        gc_rvz_extent e{};
        e.file_sector = file_sector;
        e.device_sector = static_cast<std::uint32_t>(fragment.sector);
        e.count = static_cast<std::uint32_t>(fragment.sector_count);
        out.push_back(e);
        file_sector += e.count;
    }
    return out;
}

// `source` is the RVZ's disc as open_gc_disc opened it (the plugin has no
// RTTI to ask a ByteSource what it is).
bool prepare_gc_rvz(const GcImage& image, const RvzDiscSource* source, const Fat32File& file, GcRvzSetup& out,
                    std::string& error) {
    if (source == nullptr) {
        error = "the RVZ was not opened as one";
        return false;
    }
    const RvzImage& rvz = source->image();
    if (rvz.head().iso_size > 0xFFFFFFFFull) {
        error = "the RVZ's disc is larger than a GameCube disc";
        return false;
    }
    if (!rvz.disc_runtime_table(out.table, error)) {
        error = "the RVZ's groups cannot be read during play: " + error;
        return false;
    }
    out.sizes = rvz_buffer_sizes(out.table);
    out.disc_bytes = static_cast<std::uint32_t>(rvz.head().iso_size);
    out.chunk_bytes = rvz.head().chunk_size;
    std::memcpy(out.header, rvz.head().disc_header.data(), sizeof(out.header));
    out.extents = extents_of(file);
    if (out.extents.size() > GC_RVZ_MAX_EXTENTS) {
        const char* drive = !image.usb_fragments.empty() ? "USB drive" : "SD card";
        error = "the RVZ is in " + std::to_string(out.extents.size()) + " pieces on the " + drive +
                ", more than the " + std::to_string(GC_RVZ_MAX_EXTENTS) +
                " RiftWii can follow during play: copy it to the " + drive + " again";
        return false;
    }
    // The group table, 8 bytes a group (gc_rvz.h), read from the card a
    // sector at a time during play.
    mkdir("sd:/riftwii", 0777);
    mkdir("sd:/riftwii/rvz", 0777);
    const std::string path = "sd:/riftwii/rvz/" + image.id + ".groups";
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        const bool written =
            f && std::fwrite(out.table.entries.data(), 1, out.table.entries.size(), f) == out.table.entries.size();
        if (f) std::fclose(f);
        if (!written) {
            error = "cannot write " + path;
            return false;
        }
    }
    forget_sd_layout();  // the raw lookups' folder listings predate it
    Fat32File groups;
    if (!resolve_sd_file(path, groups, error)) {
        error = path + ": " + error;
        return false;
    }
    out.table_extents = extents_of(groups);
    if (out.table_extents.size() > GC_RVZ_TABLE_EXTENTS) {
        error = path + " is in too many pieces on the SD card";
        return false;
    }
    logf("GameCube: %s, %u groups of %u KiB; group table %s (%u bytes, %u piece(s)), the RVZ in %u piece(s)\n",
         describe_rvz(rvz.head()).c_str(), static_cast<unsigned>(out.table.group_count),
         static_cast<unsigned>(out.chunk_bytes / 1024), path.c_str(),
         static_cast<unsigned>(out.table.entries.size()), static_cast<unsigned>(out.table_extents.size()),
         static_cast<unsigned>(out.extents.size()));
    return true;
}

// The RVZ build of the blob out of its zstd frame (Makefile.runtime).
bool unpack_gc_rvz_blob(std::vector<std::uint8_t>& out, std::string& error) {
    const unsigned long long bytes = ZSTD_getFrameContentSize(riftwii_gc_rvz_zst, riftwii_gc_rvz_zst_size);
    if (bytes == ZSTD_CONTENTSIZE_UNKNOWN || bytes == ZSTD_CONTENTSIZE_ERROR || bytes > (1u << 20)) {
        error = "the GameCube RVZ blob in this build is damaged";
        return false;
    }
    out.resize(static_cast<std::size_t>(bytes));
    const std::size_t got = ZSTD_decompress(out.data(), out.size(), riftwii_gc_rvz_zst, riftwii_gc_rvz_zst_size);
    if (ZSTD_isError(got) || got != out.size()) {
        error = "the GameCube RVZ blob in this build is damaged";
        return false;
    }
    return true;
}

// Cheats (riftwii/cheats.hpp), picked on the game page as for a Wii game:
// the Gecko code handler at kCodeHandlerAddress and the code list after it
// (to kCodeListEnd), in the GameCube's debugger area. Nothing of RiftWii's
// is there in a GameCube launch (the blob, the DI registers' stand-in and
// BI2 are at MEM1's top, the buffers in MEM2, the early exception stubs
// below 0x80000E00), no game section loads under kGameFloor, and the
// game's OS leaves the area alone. The handler runs at the end of the
// game's __VIRetraceHandler, once per frame, as in a Wii game.
struct GcCheats {
    std::vector<std::uint8_t> gct;      // empty: no cheats
    std::uint32_t hook = 0;             // the retrace handler's blr
    std::vector<std::uint8_t> handler;  // the code handler as placed (gc_handler_without_fpu)
};

// The code handler for a GameCube game: the Gecko handler with its FPU use
// taken out. As shipped it turns MSR[FP] on at its start, saves f2 and f3,
// and leaves FP on when it returns. Hooked at the end of the GameCube SDK's
// __VIRetraceHandler, in the OS's interrupt context, that crashed Luigi's
// Mansion on a real Wii at its first retraces with any code on, even one
// plain 04 write (an ISI at 0; with the MSR put back at its end, a hang);
// without the FP enable and the f2/f3 save and restore it boots and the
// codes apply (Jack's Wii, 18 Oct, sd:/riftwii/gc/cheatnofp of a test
// build). Dolphin showed neither. The handler's only other FPU use is the
// float add and multiply of the register operation codes (86/88 with
// operation 9 or A): those now skip to the end of the operation, so such a
// code does nothing in a GameCube game instead of raising an FP-unavailable
// exception in the interrupt. False, with `why`, when the handler is not
// the one these words were found in.
static bool gc_handler_without_fpu(std::vector<std::uint8_t>& image, std::string& why) {
    const auto word_at = [&](std::size_t at) {
        return static_cast<std::uint32_t>(image[at]) << 24 | static_cast<std::uint32_t>(image[at + 1]) << 16 |
               static_cast<std::uint32_t>(image[at + 2]) << 8 | image[at + 3];
    };
    const auto put = [&](std::size_t at, std::uint32_t w) {
        image[at] = static_cast<std::uint8_t>(w >> 24);
        image[at + 1] = static_cast<std::uint8_t>(w >> 16);
        image[at + 2] = static_cast<std::uint8_t>(w >> 8);
        image[at + 3] = static_cast<std::uint8_t>(w);
    };
    // Each instruction's place; it must be there exactly once.
    const auto find_once = [&](std::uint32_t w, std::size_t& at) {
        std::size_t found = 0;
        for (std::size_t i = 0; i + 4 <= image.size(); i += 4) {
            if (word_at(i) != w) continue;
            at = i;
            ++found;
        }
        return found == 1;
    };
    constexpr std::uint32_t kNop = 0x60000000;
    struct Taken {
        std::uint32_t word;
        const char* what;
    };
    const Taken taken[] = {
        {0x7F400124, "mtmsr r26 (FP on)"},
        {0xD8410098, "stfd f2,152(r1)"},
        {0xD86100A0, "stfd f3,160(r1)"},
        {0xC8410098, "lfd f2,152(r1)"},
        {0xC86100A0, "lfd f3,160(r1)"},
    };
    std::size_t places[5];
    for (std::size_t i = 0; i < 5; ++i) {
        if (!find_once(taken[i].word, places[i])) {
            why = std::string("the code handler is not the one RiftWii knows (") + taken[i].what + ")";
            return false;
        }
    }
    std::size_t float_load = 0, float_store = 0;
    if (!find_once(0xC05A0000, float_load) || !find_once(0xD05A0000, float_store) || float_store <= float_load) {
        why = "the code handler is not the one RiftWii knows (its float operations)";
        return false;
    }
    for (std::size_t place : places) put(place, kNop);
    // lfs f2,0(r26) becomes a branch past stfs f2,0(r26).
    put(float_load, 0x48000000 | static_cast<std::uint32_t>(float_store + 4 - float_load));
    return true;
}

// IOS's globals area and the most of the function there that may run from
// a copy (move_out_of_ios): a hook in it would be undone or left behind.
constexpr std::uint32_t kIosAreaStart = 0x80003100;
constexpr std::uint32_t kIosAreaEnd = kIosCounter + kMovedBytes;

// The launch's cheats and where their hook goes, looked for in the game's
// code before any patch of ours. False, with `why`, when they stay off.
static bool plan_gc_cheats(const Dol& dol, const GcLaunchExtras& extras, GcCheats& out, std::string& why) {
    out = GcCheats{};
    if (extras.cheat_gct.empty()) return true;
    // A code build's gameconfig.txt (Project+) is for Wii games: its list
    // place and hook type mean nothing here.
    if (extras.code_build_codes) {
        why = "a code build's own codes are not supported for GameCube games";
        return false;
    }
    // The handler's file ends with the first 8 bytes of its own (empty)
    // code list, at kCodeListAddress: the list written there replaces them.
    if (extras.code_handler_bytes > kCodeListEnd - kCodeHandlerAddress) {
        why = "the code handler is larger than the debugger area";
        return false;
    }
    const std::uint32_t room = kCodeListEnd - kCodeListAddress;
    if (extras.cheat_gct.size() > room) {
        why = "the codes need " + std::to_string(extras.cheat_gct.size()) + " bytes and the code list holds " +
              std::to_string(room) + " (up to 0x80003000, where IOS's globals start); pick fewer cheats";
        return false;
    }
    std::vector<CodeRange> text;
    for (const Section& s : dol.text) text.push_back(CodeRange{s.address, s.bytes.data(), s.bytes.size()});
    const std::uint32_t hook = find_gc_cheat_hook(text);
    if (hook == 0) {
        why = "the game's video retrace handler was not found, so there is nowhere to run them from";
        return false;
    }
    if (hook >= kIosAreaStart && hook < kIosAreaEnd) {
        why = "the end of the game's video retrace handler is in IOS's globals area, where RiftWii moves code";
        return false;
    }
    std::vector<std::uint8_t> handler(extras.code_handler, extras.code_handler + extras.code_handler_bytes);
    if (!gc_handler_without_fpu(handler, why)) return false;
    out.gct = extras.cheat_gct;
    out.hook = hook;
    out.handler = std::move(handler);
    return true;
}

// The branch to the handler, into the staged code, once every other patch
// is in: false, with `why`, when one of them took the hook's word.
static bool hook_gc_cheats(Code& code, const GcCheats& cheats, std::string& why) {
    constexpr std::uint32_t kBlr = 0x4E800020;
    if (code.at(cheats.hook) != kBlr) {
        why = "another of RiftWii's patches changed the end of the game's video retrace handler";
        return false;
    }
    const std::uint32_t jump = encode_b(cheats.hook, kCodeHandlerEntry);
    if (jump == 0) {
        why = "the code handler is out of a branch's reach from the game's video retrace handler";
        return false;
    }
    code.put(cheats.hook, jump);
    return true;
}

// The handler and the list into the debugger area: after the jump's clear
// of low memory, before the game runs.
static void place_gc_cheats(const GcCheats& cheats, const std::string& game_id) {
    std::uint8_t* handler = reinterpret_cast<std::uint8_t*>(kCodeHandlerAddress);
    std::memcpy(handler, cheats.handler.data(), cheats.handler.size());
    // Its first bytes hold the game's ID, where cheat tools look for it.
    std::memcpy(handler, game_id.data(), std::min<std::size_t>(6, game_id.size()));
    void* list = reinterpret_cast<void*>(kCodeListAddress);
    std::memset(list, 0, kCodeListEnd - kCodeListAddress);
    std::memcpy(list, cheats.gct.data(), cheats.gct.size());
    DCFlushRange(handler, kCodeListEnd - kCodeHandlerAddress);
    ICInvalidateRange(handler, kCodeListEnd - kCodeHandlerAddress);
}

// The Wii Remotes' map for this game (riftwii/gccontrols.hpp): the
// built-in one, then sd:/riftwii/gc/controls/default.txt and the game's
// own file over it, each line said in the log when it could not be used.
static void load_gc_controls(const std::string& game_id, gc_map& map) {
    gc_controls_default(map);
    for (const std::string& path : gc_controls_paths(game_id)) {
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) continue;
        // One byte more than a file may have, so a larger one is seen as such.
        std::string text(kMaxGcControlsFile + 1, '\0');
        text.resize(std::fread(&text[0], 1, text.size(), f));
        std::fclose(f);
        const std::string name = path.substr(path.rfind('/') + 1);
        std::vector<std::string> notes;
        const std::size_t changed = gc_controls_apply(text, name, map, notes);
        for (const std::string& note : notes) logf("GameCube controls: %s\n", note.c_str());
        logf("GameCube controls: %s read, %u button(s) changed\n", path.c_str(), static_cast<unsigned>(changed));
    }
    static const char* const kKindNames[GC_MAP_KINDS] = {"held sideways", "with a Nunchuk", "a Classic Controller"};
    for (std::uint32_t kind = 0; kind < GC_MAP_KINDS; ++kind)
        if (gc_map_wants_shake(&map, kind))
            logf("GameCube controls: shaking the Wii Remote (%s) presses %s\n", kKindNames[kind],
                 gc_controls_describe(map.to[kind][GC_IN_SHAKE]).c_str());
}

bool LaunchGcNative(const GcImage& image, const GcOverlayPlan* pack, const GcLaunchExtras& extras, std::string& error) {
    // The plugin runs to the jump from its arena at the top of MEM2: never
    // inside what the launch writes there (the card's image, the blob's
    // buffers, the RVZ's and the Reset restart's copy of MEM1).
    if (!plugin_clear_of(GC_CARD_IMAGE, kGcRestartWindow + kGcRestartWindowBytes, error)) return false;
    // Test presses for Dolphin runs with no controller to press with
    // (sd:/riftwii/gc/test/press), read while the card is still the file
    // system's (before it is opened raw). The test switches are files in
    // sd:/riftwii/gc/test/, a folder of their own, away from the player's
    // cards, saves and controls, so a copied folder cannot set one.
    struct stat st_press;
    const bool test_presses = stat("sd:/riftwii/gc/test/press", &st_press) == 0;
    // A test of the way back (gc_rt.h's GC_FLAG_LEAVE).
    const bool test_leave = stat("sd:/riftwii/gc/test/leave", &st_press) == 0;
    // And of the crash screen (GC_FLAG_CRASH).
    const bool test_crash = stat("sd:/riftwii/gc/test/crash", &st_press) == 0;
    // And of it on a blanked screen (GC_FLAG_CRASH_BLACK).
    const bool test_crash_black = stat("sd:/riftwii/gc/test/crashblack", &st_press) == 0;
    // And of disc audio streaming: the music alone, to compare it with an
    // offline decode (GC_FLAG_STREAM_ONLY).
    const bool test_stream_only = stat("sd:/riftwii/gc/test/streamonly", &st_press) == 0;
    // And of the restart: two Reset presses (GC_FLAG_RESET_TEST).
    const bool test_reset = stat("sd:/riftwii/gc/test/reset", &st_press) == 0;
    // And of 16:9's hook without the widening (GC_FLAG_WIDEN).
    const bool test_no_widen = stat("sd:/riftwii/gc/test/nowiden", &st_press) == 0;
    // And of the ARAM addresses left as the SDK gives them in any game
    // (sd:/riftwii/gc/test/keeparam), as gc_keeps_aram_addresses does for its
    // list: tells whether a game's crash comes from the raised addresses
    // without a build for it.
    const bool test_keep_aram = stat("sd:/riftwii/gc/test/keeparam", &st_press) == 0;
    // And of the Wii Remotes' map with no remote to press (a test run
    // cannot press Dolphin's emulated one): a made-up remote instead
    // (gc_bt.h's gc_bt_fake).
    const bool test_fake_remote = stat("sd:/riftwii/gc/test/fakeremote", &st_press) == 0;
    // The same with a made-up Wii U Pro Controller (Dolphin has none), in
    // its own report layout (gc_pro.h); it takes the test remote's place.
    const bool test_fake_pro = stat("sd:/riftwii/gc/test/fakepro", &st_press) == 0;
    const bool test_rumble = stat("sd:/riftwii/gc/test/rumbletest", &st_press) == 0;
    // And of the picture with fewer of RiftWii's hooks, for colours that
    // came out wrong on a Wii through HDMI (18 Oct): VIConfigure as the
    // game made it, and PADControlMotor not hooked (the only hook the
    // build that showed it added since the one that did not).
    const bool test_no_vi_hook = stat("sd:/riftwii/gc/test/novihook", &st_press) == 0;
    const bool test_no_motor = stat("sd:/riftwii/gc/test/nomotor", &st_press) == 0;
    // And of the Wii's video encoder at the hand-over, for the same colours
    // (red and blue swapped, in 480p only so far): set in NTSC 480p, as the
    // VI will be once the game's 480p starts, or left as the menu set it.
    const bool test_handover_480p = stat("sd:/riftwii/gc/test/handover480p", &st_press) == 0;
    const bool test_ave_keep = stat("sd:/riftwii/gc/test/avekeep", &st_press) == 0;
    // The first GameCube line of boot.log names every test switch that is
    // on, so a log from a run with one set (say a copied sd:/riftwii/gc/test/
    // folder) cannot be mistaken for a player's.
    std::string switches;
    const struct {
        bool on;
        const char* name;
    } test_switches[] = {{test_presses, "press"},
                         {test_leave, "leave"},
                         {test_crash, "crash"},
                         {test_crash_black, "crashblack"},
                         {test_stream_only, "streamonly"},
                         {test_no_widen, "nowiden"},
                         {test_keep_aram, "keeparam"},
                         {test_fake_remote, "fakeremote"},
                         {test_fake_pro, "fakepro"},
                         {test_rumble, "rumbletest"},
                         {test_no_vi_hook, "novihook"},
                         {test_no_motor, "nomotor"},
                         {test_handover_480p, "handover480p"},
                         {test_ave_keep, "avekeep"},
                         {test_reset, "reset"}};
    for (const auto& test_switch : test_switches) {
        if (!test_switch.on) continue;
        switches += switches.empty() ? " [test switches: " : ", ";
        switches += test_switch.name;
    }
    if (!switches.empty()) switches += "]";
    logf("GameCube (native): %s (%s)%s\n", image.id.c_str(), image.path.c_str(), switches.c_str());
    // What the Wii Remotes' buttons press, from the player's files.
    gc_map controls;
    load_gc_controls(image.id, controls);
    const bool rvz = image.rvz;
    const bool on_usb = !image.usb_fragments.empty();  // the image itself (packs may be on the drive too)
    Fat32File file;
    if (on_usb) {
        // On the USB drive: its pieces as the menu found them, before the
        // reload into the d2x cIOS whose /dev/usb2 reads them now.
        file.entry.size = static_cast<std::uint32_t>(image.usb_bytes);
        file.fragments = image.usb_fragments;
        logf("GameCube: the image is on the USB drive (d2x's /dev/usb2 fd %d), in %u piece(s)\n",
             static_cast<int>(image.usb_fd), static_cast<unsigned>(file.fragments.size()));
    } else if (!resolve_sd_file(image.path, file, error)) {
        return false;
    }
    // The disc: the image file itself, or the disc an RVZ holds.
    std::unique_ptr<ByteSource> disc;
    const RvzDiscSource* rvz_disc = nullptr;
    if (!open_gc_disc(image, disc, rvz_disc, error)) return false;
    const ByteSource& f = *disc;
    // The game's view of the disc: the image, then the packs' window
    // (gcoverlay.hpp) when there are packs. An RVZ's bytes are read from it
    // by their offset in the image (the blob's RVZ build).
    std::vector<GcRun> placed;
    if (rvz) {
        if (!gc_disc_runs_rvz(disc->size(), pack, placed, error)) return false;
    } else if (!gc_disc_runs(file.fragments, file.entry.size, pack, placed, error, on_usb)) {
        return false;
    }
    GcRvzSetup rvz_setup;
    if (rvz && !prepare_gc_rvz(image, rvz_disc, file, rvz_setup, error)) return false;

    std::vector<std::uint8_t> boot(0x440), bi2(kBi2Bytes);
    Dol dol;
    bool ok = read_at(f, 0, boot.data(), boot.size()) && read_at(f, 0x440, bi2.data(), bi2.size());
    if (!ok) error = "cannot read " + image.path;
    ok = ok && read_dol(f, be32(&boot[0x420]), dol, error);
    // With packs, the header and FST are the plan's (the files moved to
    // its window), and the game reads that FST from memory.
    if (pack) std::copy(pack->header.begin(), pack->header.end(), boot.begin() + 0x420);
    const std::uint32_t fst_size = be32(&boot[0x428]);
    const std::uint32_t fst_max = std::max(be32(&boot[0x42C]), fst_size);
    std::vector<std::uint8_t> fst(fst_size);
    if (pack) {
        fst = pack->fst;
    } else if (ok && (fst.empty() || !read_at(f, be32(&boot[0x424]), fst.data(), fst.size()))) {
        error = "cannot read the game's FST";
        ok = false;
    }
    if (!ok) return false;


    Code code(dol.text);
    DvdLow low;
    if (!find_dvd_low(code, low, error)) return false;
    // The cheats' hook, found while the code is still the game's own.
    GcCheats cheats;
    std::string cheats_off;
    if (!plan_gc_cheats(dol, extras, cheats, cheats_off)) logf("Codes are off: %s\n", cheats_off.c_str());
    if (const unsigned changed = ori_to_addi(code))
        logf("GameCube: the game ORs its hardware registers' offsets in (another compiler's code): %u ori "
             "made the addi that give the same value, so the searches below find their places\n",
             changed);
    // A libogc game (gc_rt.h, "libogc"): its disc is answered at the DI's
    // registers instead of through DVDLow hooks.
    std::uint32_t ogc_di_handler = 0, ogc_create_alarm = 0;
    std::vector<std::uint32_t> di_starts;
    const bool ogc = is_ogc_set_alarm(code, low.set_alarm);
    if (ogc) {
        if (!find_ogc_di_irq(code, ogc_di_handler, ogc_create_alarm)) {
            error = "the game is built on libogc, and its DI interrupt handler was not found";
            return false;
        }
        di_starts = find_di_starts(code);
        if (di_starts.empty() || di_starts.size() > GC_DI_SITES) {
            error = "the game is built on libogc, and its DI command starts were not found (" +
                    std::to_string(di_starts.size()) + ")";
            return false;
        }
        logf("GameCube: the game is built on libogc: SYS_SetAlarm 0x%08x, SYS_CreateAlarm 0x%08x, DI interrupt "
             "handler 0x%08x, %u DI command start(s) answered at the DI's registers instead of DVDLow hooks\n",
             low.set_alarm, ogc_create_alarm, ogc_di_handler, static_cast<unsigned>(di_starts.size()));
    }
    logf("GameCube: DVDLowRead 0x%08x, Seek 0x%08x, ReadDiskID 0x%08x, Inquiry 0x%08x, StopMotor 0x%08x, "
         "RequestError 0x%08x, AudioStream 0x%08x, AudioStatus 0x%08x, AudioConfig 0x%08x, Reset 0x%08x, "
         "OSSetAlarm 0x%08x\n",
         low.read, low.seek, low.disk_id, low.inquiry, low.stop_motor, low.request_error, low.audio_stream,
         low.audio_status, low.audio_config, low.reset, low.set_alarm);

    // Where RiftWii's part goes. MEM1's top, as the apploader leaves it:
    // the FST, then (ours) the blob with its context, the code moved from
    // IOS's counter, the ARAM stubs, the memory card's struct and the way
    // back's block, packed around the DI registers' stand-in
    // (riftwii/gclayout.hpp), and BI2's copy under them all, right on the
    // arena's top. BI2 sits lowest: a disc whose bi2.bin still has
    // developer arguments (Soulcalibur II's "Final_US.elf -O ...",
    // argument offset 0x1FCC) has the SDK's __start set ArenaHi to BI2
    // plus that offset, and OSInit then clears the arena up to there.
    // Above everything of ours, that wiped the blob; below it, the new
    // ArenaHi stays inside BI2's own copy.
    //
    // The blob's data buffers live in MEM2 after the memory card's image
    // (kGcMem2Data): IOS reads and writes MEM2 as well as MEM1, the game
    // never sees it, and it keeps the top of MEM1 small. Some games link
    // their stack and ArenaLo up there: Mario Golf: Toadstool Tour and
    // Mario Power Tennis start the stack at 0x817dc000, 0x24000 bytes under
    // MEM1's end. The 0x2c000 bytes RiftWii's part took there before put
    // ArenaHi under their ArenaLo, and OSInit's arena clear ran past MEM1's
    // end. Code and the blob's context stay in MEM1, within reach of the
    // game's own branches.
    // The blob: the RVZ build for an RVZ (Zstandard and RVZ packing, 50 KB
    // more of MEM1), the plain one for an ISO.
    std::vector<std::uint8_t> rvz_blob;
    const std::uint8_t* blob_bytes = riftwii_gc_bin;
    std::size_t blob_size = riftwii_gc_bin_size;
    if (rvz) {
        if (!unpack_gc_rvz_blob(rvz_blob, error)) return false;
        blob_bytes = rvz_blob.data();
        blob_size = rvz_blob.size();
    }
    const gc_rt_header& h = *reinterpret_cast<const gc_rt_header*>(blob_bytes);
    if (blob_size < sizeof(h) || h.magic != GC_RT_MAGIC || h.version != GC_RT_VERSION || h.size > blob_size) {
        error = "the GameCube blob is damaged";
        return false;
    }
    // The context the blob reserves (gc_entry.S) must hold the whole
    // struct: one that ends past the blob writes into what lies above it.
    if (h.context_offset + sizeof(gc_rt_context) > h.size) {
        error = "the GameCube blob's context is smaller than gc_rt_context";
        return false;
    }
    std::uint32_t fst_addr = (kMem1Top - fst_max) & ~31u;
    enum Mem1Piece { kPieceBlob, kPieceCard, kPieceMoved, kPieceLeave, kPieceStubs, kMem1Pieces };
    std::vector<GcMem1Piece> mem1_pieces(kMem1Pieces);
    mem1_pieces[kPieceBlob].bytes = h.size;  // the biggest first: it packs best
    mem1_pieces[kPieceCard].bytes = sizeof(gc_card);
    mem1_pieces[kPieceMoved].bytes = kMovedBytes;
    mem1_pieces[kPieceLeave].bytes = kLeaveBytes;
    mem1_pieces[kPieceStubs].bytes = kStubBytes;
    const GcMem1Plan mem1 = plan_gc_mem1(fst_addr, mem1_pieces, kBi2Bytes);
    const std::uint32_t blob_addr = mem1_pieces[kPieceBlob].address;
    const std::uint32_t card_addr = mem1_pieces[kPieceCard].address;
    const std::uint32_t moved_addr = mem1_pieces[kPieceMoved].address;
    const std::uint32_t leave_addr = mem1_pieces[kPieceLeave].address;
    const std::uint32_t stubs_addr = mem1_pieces[kPieceStubs].address;
    const std::uint32_t di_regs = mem1.di_regs;
    const std::uint32_t bi2_addr = mem1.bi2;
    std::uint32_t arena_hi = mem1.arena_hi;
    // libogc (gc_rt.h, "libogc") does not take ArenaHi from 0x80000034 in
    // every version: Sonic Riders Tournament Edition's sets its arena's top
    // to the FST's address less 0x800, so with the FST at MEM1's top its heap
    // covered the blob, and a 1.4 MB read wrote over it. For such a game the
    // FST goes below everything else, and the arena ends 0x800 under it:
    // either way of reading the top leaves the blob, the card and the DI
    // stand-in alone.
    if (ogc) {
        fst_addr = (arena_hi - fst_max) & ~31u;
        arena_hi = (fst_addr - 0x800) & ~31u;
    }
    if (arena_hi < kLoaderCeiling) {
        error = "the game's FST is too large";
        return false;
    }
    // MEM2, one after another from kGcMem2Data, each 32-byte aligned.
    const std::uint32_t runs_bytes = static_cast<std::uint32_t>(placed.size() * sizeof(gc_rt_run));
    std::uint32_t mem2_next = kGcMem2Data;
    auto mem2_take = [&mem2_next](std::uint32_t bytes) {
        const std::uint32_t at = mem2_next;
        mem2_next = (at + bytes + 31u) & ~31u;
        return at;
    };
    const std::uint32_t bounce_addr = mem2_take(kBounceBytes);
    const std::uint32_t adp_addr = mem2_take(kStreamBytes);
    const std::uint32_t stream_addr = mem2_take(sizeof(gc_rt_stream));
    const std::uint32_t trace_addr = mem2_take(512);
    const std::uint32_t bt_addr = mem2_take(sizeof(gc_bt));
    // The GameCube adapter's state (gc_gcad.h), when the menu opened it.
    const std::uint32_t gcad_addr = extras.adapter_fd >= 0 ? mem2_take(sizeof(gc_gcad)) : 0;
    const std::uint32_t crash_addr = mem2_take(GC_CRASH_SCREEN_BYTES);  // the card, then the strip
    const std::uint32_t runs_addr = mem2_take(runs_bytes);
    // An RVZ's reader: its state and the pieces of the RVZ on the card.
    const std::uint32_t rvz_state_bytes =
        rvz ? gc_rvz_state_bytes(static_cast<std::uint32_t>(rvz_setup.extents.size())) : 0;
    const std::uint32_t rvz_addr = rvz ? mem2_take(rvz_state_bytes) : 0;
    const std::uint32_t mem2_bytes = mem2_next - kGcMem2Data;
    if (mem2_bytes > kGcMem2DataMax) {
        error = "the packs' disc layout is too large";
        return false;
    }
    // An RVZ's buffers, in their own window after those (kGcRvzCache): the
    // decoder's workspace, a group as stored, and the two slots' decoded
    // groups (the game's reads and the disc audio stream's).
    std::uint32_t rvz_next = kGcRvzCache;
    auto rvz_take = [&rvz_next](std::uint32_t bytes) {
        const std::uint32_t at = rvz_next;
        rvz_next = (at + bytes + 31u) & ~31u;
        return at;
    };
    const std::uint32_t rvz_dctx_addr = rvz && rvz_setup.sizes.decoder ? rvz_take(kGcRvzDecoderBytes) : 0;
    const std::uint32_t rvz_stored_addr = rvz && rvz_setup.sizes.stored ? rvz_take(rvz_setup.sizes.stored) : 0;
    const std::uint32_t rvz_slot_addr[GC_RVZ_SLOTS] = {rvz ? rvz_take(rvz_setup.sizes.group) : 0,
                                                       rvz ? rvz_take(rvz_setup.sizes.group) : 0};
    if (rvz_next - kGcRvzCache > kGcRvzCacheMax) {
        logf("GameCube: the RVZ's %u KiB chunks need %u KB of buffers in game, more than the %u KB set aside\n",
             static_cast<unsigned>(rvz_setup.chunk_bytes / 1024), static_cast<unsigned>((rvz_next - kGcRvzCache) / 1024),
             static_cast<unsigned>(kGcRvzCacheMax / 1024));
        error = "This RVZ's chunks are too large to read during play: convert it again in Dolphin with 128 KiB chunks.";
        return false;
    }
    // Built here, zeroed, and moved into MEM2 just before the jump with
    // the card's image: the loader's own heap may be where it goes.
    std::vector<std::uint8_t> mem2_stage(mem2_bytes, 0);
    auto staged = [&mem2_stage](std::uint32_t mem2_address) {
        return static_cast<void*>(mem2_stage.data() + (mem2_address - kGcMem2Data));
    };

    // The game's own layout at the top of MEM1: its stack's top (the r1
    // that __init_registers sets) and ArenaLo (OSInit's __ArenaLo). The
    // arena's top has to stay at or above both, or OSInit's clear wraps
    // and the stack runs down into RiftWii's part.
    {
        const GcCodeReader read = [&code](std::uint32_t address, std::uint32_t& word) {
            if (!code.has(address)) return false;
            word = code.at(address);
            return true;
        };
        std::vector<GcTextRange> text_ranges;
        for (const Section& s : dol.text)
            text_ranges.push_back({s.address, s.address + static_cast<std::uint32_t>(s.bytes.size())});
        const GcStackTop stack = find_gc_stack_top(read, dol.entry);
        const std::uint32_t arena_lo = find_gc_arena_lo(read, text_ranges);
        char stack_text[48] = "not found";
        char arena_text[16] = "not found";
        if (stack.address != 0)
            std::snprintf(stack_text, sizeof(stack_text), "0x%08x (set at 0x%08x)", static_cast<unsigned>(stack.address),
                          static_cast<unsigned>(stack.set_at));
        if (arena_lo != 0) std::snprintf(arena_text, sizeof(arena_text), "0x%08x", static_cast<unsigned>(arena_lo));
        logf("GameCube: the game's stack top %s, its ArenaLo %s\n", stack_text, arena_text);
        logf("GameCube: MEM1 from the arena's top 0x%08x: BI2 0x%08x, DI stand-in 0x%08x, blob 0x%08x (%u bytes, "
             "context 0x%08x), memory card 0x%08x, way back 0x%08x, code at IOS's counter 0x%08x, ARAM stubs "
             "0x%08x, FST 0x%08x (%u bytes)\n",
             arena_hi, bi2_addr, di_regs, blob_addr, static_cast<unsigned>(h.size), blob_addr + h.context_offset,
             card_addr, leave_addr, moved_addr, stubs_addr, fst_addr, static_cast<unsigned>(fst_max));
        logf("GameCube: MEM2 0x%08x (%u bytes): bounce 0x%08x, stream 0x%08x (ring 0x%08x), trace 0x%08x, Wii "
             "Remotes 0x%08x, crash screen 0x%08x (strip 0x%08x), %u run(s) 0x%08x\n",
             kGcMem2Data, static_cast<unsigned>(mem2_bytes), bounce_addr, stream_addr, adp_addr, trace_addr, bt_addr,
             crash_addr, crash_addr + GC_CRASH_CARD_BYTES, static_cast<unsigned>(placed.size()), runs_addr);
        if (rvz)
            logf("GameCube: RVZ reader 0x%08x (%u bytes); buffers 0x%08x (%u bytes): decoder 0x%08x, stored group "
                 "0x%08x (%u bytes), the game's group 0x%08x and the stream's 0x%08x (%u bytes each)\n",
                 rvz_addr, static_cast<unsigned>(rvz_state_bytes), kGcRvzCache,
                 static_cast<unsigned>(rvz_next - kGcRvzCache), rvz_dctx_addr, rvz_stored_addr,
                 static_cast<unsigned>(rvz_setup.sizes.stored), rvz_slot_addr[0], rvz_slot_addr[1],
                 static_cast<unsigned>(rvz_setup.sizes.group));
        const std::uint32_t game_floor = std::max(stack.address, arena_lo);
        if (game_floor > arena_hi) {
            logf("GameCube: the game's stack or arena starts at 0x%08x, above the arena's top 0x%08x: the FST and "
                 "RiftWii's part leave it no room\n",
                 game_floor, arena_hi);
            error = rvz ? "the game keeps its stack where RiftWii's part has to be (top of MEM1), which is larger for "
                          "an RVZ: use an ISO of it"
                        : "the game keeps its stack where RiftWii's part has to be (top of MEM1)";
            return false;
        }
    }

    // The blob straight into place (above RiftWii's heap), its table to
    // MEM2's stage.
    std::memcpy(reinterpret_cast<void*>(blob_addr), blob_bytes, h.size);
    // The RVZ build's references to Zstandard's tables, for where it now is.
    if (rvz && !apply_resident_relocs(reinterpret_cast<std::uint8_t*>(blob_addr), h.size, riftwii_gc_rvz_rel,
                                      riftwii_gc_rvz_rel_size, blob_addr, error)) {
        error = "the GameCube RVZ blob: " + error;
        return false;
    }
    auto* ctx = reinterpret_cast<gc_rt_context*>(blob_addr + h.context_offset);
    auto* runs = static_cast<gc_rt_run*>(staged(runs_addr));
    for (std::size_t i = 0; i < placed.size(); ++i) {
        const GcRun& r = placed[i];
        runs[i].disc = static_cast<std::uint32_t>(r.disc);
        runs[i].length = static_cast<std::uint32_t>(r.length);
        runs[i].skip = r.skip;
        if (r.kind == GcRun::Kind::Sd) {
            runs[i].kind = GC_RUN_SD;
            runs[i].source = static_cast<std::uint32_t>(r.sector);
        } else if (r.kind == GcRun::Kind::Usb) {
            runs[i].kind = GC_RUN_USB;
            runs[i].source = static_cast<std::uint32_t>(r.sector);
        } else if (r.kind == GcRun::Kind::Image) {
            runs[i].kind = GC_RUN_RVZ;
            runs[i].source = static_cast<std::uint32_t>(r.image_offset);
        } else if (r.kind == GcRun::Kind::Fst) {
            runs[i].kind = GC_RUN_MEM;
            runs[i].source = fst_addr + static_cast<std::uint32_t>(r.fst_offset);
        } else {
            runs[i].kind = GC_RUN_ZERO;
            runs[i].source = 0;
        }
    }
    std::memcpy(reinterpret_cast<void*>(fst_addr), fst.data(), fst.size());
    std::memcpy(reinterpret_cast<void*>(bi2_addr), bi2.data(), bi2.size());
    // Developer arguments left in bi2.bin move the game's ArenaHi (see the
    // layout above): worth a line, as a disc with them clears up to there.
    const std::uint32_t bi2_arg_offset = be32(&bi2[8]);
    // What else of bi2.bin the game's start-up reads: the debug flag (2 or
    // 3 has __start start the MetroTRK debugger), the simulated memory
    // size and the controllers' spec, which a disc left with developer
    // settings may hold too.
    logf("GameCube: bi2 simulated memory 0x%08x, argument offset 0x%x, debug flag %u, pad spec %u\n",
         static_cast<unsigned>(be32(&bi2[4])), static_cast<unsigned>(bi2_arg_offset),
         static_cast<unsigned>(be32(&bi2[0xC])), static_cast<unsigned>(be32(&bi2[0x24])));
    if (bi2_arg_offset != 0)
        logf("GameCube: bi2 argument offset 0x%x: the game's __start sets ArenaHi from it (0x%08x), below the blob\n",
             static_cast<unsigned>(bi2_arg_offset), static_cast<unsigned>((bi2_addr + bi2_arg_offset + 4) & ~31u));
    // A simulated memory size above a retail console's 24 MB (a development
    // kit's 48 MB left in bi2.bin) is brought down to 24 MB, as Swiss does
    // for a disc that has one: a game that sizes its memory from it writes
    // past MEM1's end.
    if (be32(&bi2[4]) > 0x01800000) {
        store32(bi2_addr + 4, 0x01800000);
        logf("GameCube: bi2's simulated memory size lowered to a GameCube's 24 MB\n");
    }

    // The trace and the memory cards live in sd:/riftwii/gc. mkdir makes one
    // level at a time, and a launch that did not come through the menu
    // (autorun, headless) finds no gc folder on a fresh card: without these
    // the game ran with no memory card and no trace.
    mkdir("sd:/riftwii", 0777);
    mkdir("sd:/riftwii/gc", 0777);
    mkdir("sd:/riftwii/gc/cards", 0777);

    // The trace (gc_rt.h): one cluster's worth of zeroed records, its
    // sectors given to the blob, read back after a run that went wrong.
    std::uint32_t trace_sector = 0, trace_count = 0;
    {
        static const char kTrace[] = "sd:/riftwii/gc/trace.bin";
        std::vector<std::uint8_t> zeros(128 << 10);
        std::FILE* t = std::fopen(kTrace, "wb");
        const bool written = t && std::fwrite(zeros.data(), 1, zeros.size(), t) == zeros.size();
        if (t) std::fclose(t);
        forget_sd_layout();  // the raw lookups' folder listings predate it
        Fat32File tf;
        std::string why;
        if (written && resolve_sd_file(kTrace, tf, why) && !tf.fragments.empty()) {
            trace_sector = static_cast<std::uint32_t>(tf.fragments[0].sector);
            trace_count = static_cast<std::uint32_t>(std::min<std::uint64_t>(256, tf.fragments[0].sector_count));
        }
        logf("GameCube: trace %s (sector %u, %u records)%s%s\n", trace_count ? "on" : "off", trace_sector, trace_count,
             why.empty() ? "" : ": ", why.c_str());
    }

    // The memory card in slot A (gc_card.h): one file per game, or the shared one
    // (gc_card_path), made and formatted the first time. Its whole image goes to MEM2 at the
    // jump, and the blob writes what the game saves back to its sectors.
    std::vector<std::uint8_t> card_image;
    std::vector<gc_card_run> card_runs;
    // (The memory card's backup, sd:/riftwii/gc/backups/, is made by the
    // DOL before the plugin starts: wii/launch/gcplugin.cpp.)
    {
        const std::string path = gc_card_path(image.id, image.shared_card);
        card_image.assign(GC_CARD_BYTES, 0xFF);
        bool ready = false;
        bool exists = false;
        long size = -1;
        if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
            exists = true;
            std::fseek(f, 0, SEEK_END);
            size = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            ready = size == static_cast<long>(GC_CARD_BYTES) &&
                    std::fread(card_image.data(), 1, card_image.size(), f) == card_image.size();
            std::fclose(f);
        }
        // A card of another size (a 59-block one from Dolphin or a real
        // card, say) is never overwritten: its saves stay, and the game runs
        // without a card. The menu still reads it, to copy its saves out.
        const bool other_size = exists && size != static_cast<long>(GC_CARD_BYTES) && size != 0;
        // New, empty, or never formatted (its header still erased): formatted here.
        const bool blank = !other_size && (!ready || std::all_of(card_image.begin(), card_image.begin() + 0x200,
                                                                   [](std::uint8_t b) { return b == 0xFF; }));
        if (other_size) {
            logf("GameCube: %s is %ld bytes, not a 251-block card's %u: left as it is, and the game runs without a card\n",
                 path.c_str(), size, GC_CARD_BYTES);
            ready = false;
        }
        if (blank) {
            const bool shift_jis = gc_card_shift_jis(image.id);
            format_card(card_image, shift_jis);
            logf("GameCube: a newly formatted memory card, %s (%u KB)\n", path.c_str(), GC_CARD_BYTES / 1024);
            std::FILE* f = std::fopen(path.c_str(), "wb");
            ready = f && std::fwrite(card_image.data(), 1, card_image.size(), f) == card_image.size();
            if (f) std::fclose(f);
            forget_sd_layout();
        }
        Fat32File cf;
        std::string why;
        if (ready && resolve_sd_file(path.c_str(), cf, why)) {
            std::uint32_t at = 0;
            for (const auto& fr : cf.fragments) {
                if (at >= GC_CARD_FILE_SECTORS) break;
                gc_card_run run{};
                run.card_sector = at;
                run.sd_sector = static_cast<std::uint32_t>(fr.sector);
                run.count = static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(fr.sector_count, GC_CARD_FILE_SECTORS - at));
                at += run.count;
                card_runs.push_back(run);
            }
            if (at < GC_CARD_FILE_SECTORS || card_runs.size() > GC_CARD_RUNS) {
                why = "the file is in too many pieces or too short";
                card_runs.clear();
            }
        }
        if (card_runs.empty()) {
            logf("GameCube: no memory card (%s): %s\n", path.c_str(), why.empty() ? "it could not be made" : why.c_str());
            card_image.clear();
        } else {
            logf("GameCube: memory card %s, %u piece(s)\n", path.c_str(), static_cast<unsigned>(card_runs.size()));
        }
    }

    // Hooks into the blob, in the staged code.
    auto hook = [&](std::uint32_t fn, std::uint32_t entry) {
        if (fn != 0) code.put(fn, branch(fn, blob_addr + entry));
    };
    // A hook that runs the original afterwards: its first word to the
    // replay slot, then the jump to its second.
    auto replay_hook = [&](std::uint32_t fn, std::uint32_t entry, std::uint32_t replay, std::uint32_t back) {
        store32(blob_addr + replay, code.at(fn));
        store32(blob_addr + back, 0x3D800000 | ((fn + 4) >> 16));
        store32(blob_addr + back + 4, 0x618C0000 | ((fn + 4) & 0xFFFF));
        hook(fn, entry);
    };
    // The game's picture and language options, while the log still
    // reaches the card (testers send it).
    apply_picture_and_language(image, code, [&](std::uint32_t fn, std::uint32_t entry, std::uint32_t replay,
                                                std::uint32_t back) {
        // The replay slot runs the first word elsewhere: not a relative branch.
        const unsigned first = opcode(code.at(fn));
        if (first == 16 || first == 18) return false;
        replay_hook(fn, entry, replay, back);
        return true;
    }, h, ctx, test_no_widen, test_no_vi_hook);

    // Luigi's Mansion: its loads allocate from the heap they were queued from.
    const LmLoader lm = find_lm_loader(image.id, code);
    if (lm.heap_offset != 0) {
        ctx->load_post = lm.post;
        ctx->load_group = lm.group;
        ctx->load_alloc = lm.alloc;
        ctx->load_heap_offset = lm.heap_offset;
        for (std::uint32_t site : lm.post_sites) code.put(site, branch(site, blob_addr + h.hook_load_queued) | 1);  // bl
        code.put(lm.alloc_site, branch(lm.alloc_site, blob_addr + h.hook_load_alloc) | 1);
        code.put(lm.callbacks_site, branch(lm.callbacks_site, blob_addr + h.hook_load_callbacks) | 1);
        code.put(lm.callbacks_done_site, branch(lm.callbacks_done_site, blob_addr + h.hook_load_callbacks_done) | 1);
        logf("GameCube: the file loader's buffers and callbacks use the heap each load was queued from (post 0x%08x "
             "from %u place(s), alloc 0x%08x, current heap at r13%+d)\n",
             static_cast<unsigned>(lm.post), static_cast<unsigned>(lm.post_sites.size()),
             static_cast<unsigned>(lm.alloc), static_cast<int>(static_cast<std::int32_t>(lm.heap_offset)));
    } else if (gc_loads_use_queued_heap(image.id)) {
        logf("GameCube: Luigi's Mansion's file loader was not found; it allocates from whichever heap is current\n");
    }

    // What holding HOME goes to.
    prepare_leave(ctx, leave_addr, test_leave, extras);
    ctx->crash_card = 0;
    // A hint promising that HOME or A goes back is left out (with the
    // dump) when there is no way back after all (/dev/es did not open): a
    // plain blue screen beats a promise the buttons cannot keep.
    const bool broken_promise = extras.crash_screen_way_back && ctx->leave_buf == 0;
    if (broken_promise && extras.crash_screen != nullptr)
        logf("GameCube: the crash screen's dump and hint are left out, as the hint promises a way back there is not\n");
    if (extras.crash_screen != nullptr && extras.crash_screen_bytes == GC_CRASH_SCREEN_BYTES && !broken_promise) {
        std::memcpy(staged(crash_addr), extras.crash_screen, GC_CRASH_SCREEN_BYTES);
        ctx->crash_card = crash_addr;
    }
    ctx->runs = runs_addr;
    ctx->run_count = static_cast<std::uint32_t>(placed.size());
    if (rvz) {
        // The RVZ's reader (gc_rvz.h), staged with the rest of MEM2's data;
        // its buffers need nothing in them.
        auto* reader = static_cast<gc_rvz*>(staged(rvz_addr));
        reader->magic = GC_RVZ_MAGIC;
        reader->disc_bytes = rvz_setup.disc_bytes;
        reader->chunk_bytes = rvz_setup.chunk_bytes;
        reader->group_count = rvz_setup.table.group_count;
        reader->stored_buffer = rvz_stored_addr;
        reader->stored_bytes = rvz_stored_addr ? rvz_setup.sizes.stored : 0;
        reader->dctx_buffer = rvz_dctx_addr;
        reader->dctx_bytes = rvz_dctx_addr ? kGcRvzDecoderBytes : 0;
        for (std::uint32_t s = 0; s < GC_RVZ_SLOTS; ++s) {
            reader->slots[s].buffer = rvz_slot_addr[s];
            reader->slots[s].buffer_bytes = rvz_setup.sizes.group;
            reader->slots[s].group = GC_RVZ_NO_GROUP;
        }
        std::memcpy(reader->header, rvz_setup.header, sizeof(reader->header));
        reader->entry_sector = GC_RVZ_NO_GROUP;
        reader->table_extent_count = static_cast<std::uint32_t>(rvz_setup.table_extents.size());
        std::copy(rvz_setup.table_extents.begin(), rvz_setup.table_extents.end(), reader->table_extents);
        reader->extent_count = static_cast<std::uint32_t>(rvz_setup.extents.size());
        reader->extents_on_usb = on_usb ? 1 : 0;  // the group table stays on the card
        std::copy(rvz_setup.extents.begin(), rvz_setup.extents.end(), reader->extents);
        ctx->rvz = rvz_addr;
    }
    ctx->set_alarm = low.set_alarm;
    ctx->alarm_entry = blob_addr + h.alarm_entry;
    ctx->bounce = bounce_addr;
    ctx->bounce_bytes = kBounceBytes;
    ctx->delay_ticks = 2000;  // about 30 microseconds
    // Reads take a GameCube drive's time (gc_drive_ticks) unless the game's
    // Loading speed option says Fast.
    ctx->drive_timing = image.drive_timing ? 1 : 0;
    ctx->beat_ticks = 30375000;  // half a second of the bus clock / 4
    // A game or a pack on the USB drive: d2x's /dev/usb2, kept open across
    // the hand-over like the card's handle, and a keep-alive read of the
    // first sector the game reads there after 30 s without one, so a hard
    // drive that spins down when idle does not make the game's next read
    // take seconds (some games call a slow read a disc error).
    ctx->usb_fd = image.usb_fd >= 0 ? static_cast<std::uint32_t>(image.usb_fd) : 0xFFFFFFFFu;
    if (image.usb_fd >= 0) {
        for (std::size_t i = 0; i < placed.size(); ++i) {
            if (placed[i].kind != GcRun::Kind::Usb) continue;
            ctx->usb_keep_sector = static_cast<std::uint32_t>(placed[i].sector);
            ctx->usb_keep_ticks = 60 * 30375000u;  // 30 s
            break;
        }
    }
    ctx->di_regs = di_regs;
    std::memset(reinterpret_cast<void*>(di_regs), 0, 0x40);
    auto* stream = static_cast<gc_rt_stream*>(staged(stream_addr));
    stream->buf = adp_addr;
    stream->buf_bytes = kStreamBytes;
    stream->sample = 28;
    ctx->stream = stream_addr;
    ctx->trace_sector = trace_sector;
    ctx->trace_count = trace_count;
    ctx->trace_buf = trace_addr;
    // The Wii Remotes' state (gc_bt.h), zeroed in MEM2's stage: the blob
    // opens the Bluetooth controller at the game's first PADRead.
    ctx->bt = bt_addr;
    static_cast<gc_bt*>(staged(bt_addr))->map = controls;
    // The GameCube adapter for Wii U: its driver runs from the game's
    // PADRead on the handle the menu opened (gc_gcad.h); its controllers
    // take their ports before the Wii Remotes.
    if (gcad_addr != 0) {
        auto* ad = static_cast<gc_gcad*>(staged(gcad_addr));
        ad->fd = extras.adapter_fd;
        ad->version = extras.adapter_version;
        ad->known_dev = extras.adapter_dev;
        ad->ticks_per_ms = 60750;  // the timebase, the bus clock / 4 in Wii mode
        ad->rumble = extras.adapter_rumble ? 1 : 0;
        for (std::uint8_t& port : ad->port_of) port = GC_PORT_NONE;
        ctx->gcad = gcad_addr;
        logf("GameCube: GameCube adapter on (/dev/usb/hid v%u fd %d, adapter device %d, rumble %s), state 0x%08x\n",
             static_cast<unsigned>(extras.adapter_version), static_cast<int>(extras.adapter_fd),
             static_cast<int>(extras.adapter_dev), extras.adapter_rumble ? "on" : "off", gcad_addr);
    }
    auto* memcard = reinterpret_cast<gc_card*>(card_addr);
    std::memset(memcard, 0, sizeof(*memcard));
    // Its landing words (gc_card.h), with the card on or off: a jump to the
    // struct's start shows the crash screen with the jumper's LR and CTR.
    memcard->landing[0] = 0x7C0802A6;  // mflr r0
    memcard->landing[1] = branch(card_addr + 4, blob_addr + h.stray_entry) | 1;  // bl gc_stray_entry

    // The hooks, in the staged code. A libogc game's DI command starts jump
    // to the slots instead (gc_rt.h, "libogc"): the store, a frame for r0
    // and LR, gc_di_common, and back.
    if (ogc) {
        ctx->ogc_create_alarm = ogc_create_alarm;
        ctx->ogc_alarm_entry = blob_addr + h.alarm_entry_ogc;
        ctx->di_handler = ogc_di_handler;
        for (std::size_t i = 0; i < di_starts.size(); ++i) {
            const std::uint32_t site = di_starts[i];
            const std::uint32_t slot = blob_addr + h.di_slots + static_cast<std::uint32_t>(i) * GC_DI_SLOT_WORDS * 4;
            const std::uint32_t words[GC_DI_SLOT_WORDS] = {
                code.at(site),
                0x9421FFF0,  // stwu r1,-16(r1)
                0x90010008,  // stw r0,8(r1)
                0x7C0802A6,  // mflr r0
                0x9001000C,  // stw r0,12(r1)
                branch(slot + 20, blob_addr + h.di_common) | 1,  // bl gc_di_common
                0x8001000C,  // lwz r0,12(r1)
                0x7C0803A6,  // mtlr r0
                0x80010008,  // lwz r0,8(r1)
                0x38210010,  // addi r1,r1,16
                branch(slot + 40, site + 4),  // b back
            };
            for (unsigned k = 0; k < GC_DI_SLOT_WORDS; ++k) store32(slot + 4 * k, words[k]);
            code.put(site, branch(site, slot));
        }
    } else {
        hook(low.read, h.hook_read);
        hook(low.seek, h.hook_seek);
        hook(low.disk_id, h.hook_disk_id);
        hook(low.inquiry, h.hook_inquiry);
        hook(low.stop_motor, h.hook_cb_r3);
        hook(low.request_error, h.hook_cb_r3);
        hook(low.audio_status, h.hook_audio_status);
        hook(low.audio_config, h.hook_cb_r5);
        hook(low.audio_stream, h.hook_audio_stream);
    }
    const unsigned di_patches = redirect_di_registers(code, di_regs);
    unsigned wii_mixed = 0;
    const unsigned wii_blocks = redirect_wii_blocks(code, wii_mixed);
    logf("GameCube: %u SI/EXI/AI register base(s) moved to 0xCD00 (%u also used elsewhere, left)\n", wii_blocks,
         wii_mixed);
    const std::uint32_t ai_dma = find_dsp_stores(code, {0xCC005030, 0xCC005032, 0xCC005036});
    if (ai_dma != 0) {
        // Its first word to the replay slot, then the jump to its second.
        const std::uint32_t replay = blob_addr + h.replay_ai_dma;
        const std::uint32_t back = blob_addr + h.continue_ai_dma;
        const std::uint32_t first = code.at(ai_dma);
        if ((first >> 26) == 18 || (first >> 26) == 16) {
            error = "AIInitDMA starts with a branch (not handled yet)";
            return false;
        }
        store32(replay, first);
        store32(back, 0x3D800000 | ((ai_dma + 4) >> 16));        // lis r12, hi
        store32(back + 4, 0x618C0000 | ((ai_dma + 4) & 0xFFFF));  // ori r12, r12, lo
        hook(ai_dma, h.hook_ai_dma);
    }
    // PADRead: the Wii Remotes are polled from it, and the test presses
    // go in there.
    std::uint32_t pad_read = find_pad_read(code);
    if (pad_read == 0) pad_read = find_pad_read_gcc(code);
    std::uint32_t motor = 0;
    if (pad_read != 0) {
        replay_hook(pad_read, h.hook_pad_read, h.replay_pad_read, h.continue_pad_read);
        if (test_presses) ctx->flags |= GC_FLAG_PRESS;
        if (test_fake_remote || test_fake_pro) ctx->flags |= GC_FLAG_FAKE_REMOTE;
        if (test_fake_pro) ctx->flags |= GC_FLAG_FAKE_PRO;
        if (test_leave) ctx->flags |= GC_FLAG_LEAVE;
        if (test_crash || test_crash_black) ctx->flags |= GC_FLAG_CRASH;
        if (test_crash_black) ctx->flags |= GC_FLAG_CRASH_BLACK;
        // PADControlMotor: a Wii Remote on the port the game rumbles
        // rumbles too, unless Settings' Wii Remote rumble (or this game's
        // own Rumble) is off, as for Wii games. The game's own function
        // runs first, for a GameCube controller on the port.
        std::vector<CodeRange> ranges;
        for (const Section& s : dol.text) ranges.push_back(CodeRange{s.address, s.bytes.data(), s.bytes.size()});
        std::string motor_error;
        if (test_no_motor)
            logf("GameCube: PADControlMotor not hooked, as sd:/riftwii/gc/test/nomotor asks: no Wii Remote rumble\n");
        else if (!find_gc_pad_motor(ranges, motor, motor_error))
            logf("GameCube: Wii Remote rumble off: %s\n", motor_error.c_str());
        if (motor != 0) {
            replay_hook(motor, h.hook_pad_motor, h.replay_pad_motor, h.continue_pad_motor);
            if (!extras.rumble_off) ctx->flags |= GC_FLAG_REMOTE_RUMBLE;
            if (test_rumble) {
                ctx->flags |= GC_FLAG_RUMBLE_TEST;
                ctx->test_motor = motor;
            }
        }
    }
    if (test_stream_only) ctx->flags |= GC_FLAG_STREAM_ONLY;
    // Reset restarts the game, as on a GameCube (gc_on_hot_reset): the
    // game's OSResetSystem and its reset routine go to the blob, and MEM1's
    // pieces as they are at the jump are kept in MEM2 to be put back, in
    // this order: the zeroed BSS and DI stand-in, then the game's sections,
    // BI2, the FST and low memory (copied there just before the jump).
    const GcResetFunctions reset = find_gc_reset_functions(code.ranges());
    const bool reset_hookable = reset.reset_system != 0 && opcode(code.at(reset.reset_system)) != 16 &&
                                opcode(code.at(reset.reset_system)) != 18;
    GcRestartPlan restart;
    if (reset_hookable && gc_restart_known_to_work(image.id)) {
        std::vector<GcRestartPiece> pieces;
        if (dol.bss_size != 0) pieces.push_back({dol.bss, dol.bss_size, true});
        pieces.push_back({di_regs, kGcDiRegsBytes, true});
        for (const Section& s : dol.text)
            pieces.push_back({s.address, static_cast<std::uint32_t>(s.bytes.size()), false});
        for (const Section& s : dol.data)
            pieces.push_back({s.address, static_cast<std::uint32_t>(s.bytes.size()), false});
        pieces.push_back({bi2_addr, static_cast<std::uint32_t>(bi2.size()), false});
        if (!fst.empty()) pieces.push_back({fst_addr, static_cast<std::uint32_t>(fst.size()), false});
        pieces.push_back({0x80000000, kGameFloor - 0x80000000, false});
        restart = plan_gc_restart(pieces, kGcRestartWindow, kGcRestartWindowBytes - kGcRestartStackBytes,
                                  GC_RESTART_REGIONS);
    }
    // The table, kept here (the plan's own vector may be in MEM2's window).
    gc_restart_region restart_table[GC_RESTART_REGIONS] = {};
    const std::uint32_t restart_count = static_cast<std::uint32_t>(restart.regions.size());
    // Hooked for every game: the ones without a restart go back to RiftWii
    // on Reset (gc_on_reset_system), rather than stop the console.
    if (reset_hookable) {
        replay_hook(reset.reset_system, h.hook_reset_system, h.replay_reset_system, h.continue_reset_system);
        hook(reset.hardware_reset, h.hook_hot_reset);
        ctx->reset_system = reset.reset_system;
        if (test_reset) ctx->flags |= GC_FLAG_RESET_TEST;
    }
    if (restart_count != 0) {
        for (std::uint32_t i = 0; i < restart_count; ++i)
            restart_table[i] = {restart.regions[i].address, restart.regions[i].length, restart.regions[i].source};
        ctx->entry = dol.entry;
        ctx->restart_regions = restart.table;
        ctx->restart_count = restart_count;
        ctx->restart_stack = kGcRestartWindow + kGcRestartWindowBytes - 16;  // a frame's back chain and LR above it
        logf("GameCube: Reset restarts the game: OSResetSystem 0x%08x, reset routine 0x%08x, MEM1 kept in %u "
             "piece(s), 0x%08x-0x%08x%s\n",
             reset.reset_system, reset.hardware_reset, static_cast<unsigned>(restart_count), restart.table,
             restart.end, pad_read != 0 ? "" : " (PADRead not found: only the game's own Reset)");
    } else if (reset_hookable && gc_restart_known_to_work(image.id)) {
        logf("GameCube: Reset goes back to RiftWii: the game's memory does not fit in the restart's %u MB\n",
             static_cast<unsigned>(kGcRestartWindowBytes >> 20));
    } else if (reset_hookable) {
        logf("GameCube: Reset goes back to RiftWii, as holding HOME (OSResetSystem 0x%08x): this game's restart is "
             "not known to work yet\n",
             reset.reset_system);
    } else {
        logf("GameCube: Reset goes back to RiftWii, as holding HOME: OSResetSystem and the reset routine were not "
             "found\n");
    }
    // The memory card: the EXI functions to the blob (gc_card.h).
    if (!card_image.empty()) {
        std::uint32_t exi[GC_EXI_HOOKS];
        const std::uint32_t ecb = find_exi_functions(code, exi);
        bool branches = false;
        for (std::uint32_t f : exi)
            if (f != 0 && ((code.at(f) >> 26) == 18 || (code.at(f) >> 26) == 16)) branches = true;
        if (ecb != 0 && !branches) {
            for (unsigned i = 0; i < GC_EXI_HOOKS; ++i) {
                if (exi[i] == 0) continue;  // EXIProbeEx, which the 2001 SDK does not have
                const std::uint32_t entry = h.exi_hooks + 12 * i;
                replay_hook(exi[i], load32(blob_addr + entry), load32(blob_addr + entry + 4),
                            load32(blob_addr + entry + 8));
            }
            memcard->image = GC_CARD_IMAGE;
            memcard->ecb = ecb;
            memcard->run_count = static_cast<std::uint32_t>(card_runs.size());
            std::copy(card_runs.begin(), card_runs.end(), memcard->runs);
            ctx->card = card_addr;
        }
        logf("GameCube: memory card %s (EXI blocks 0x%08x, EXIImm 0x%08x, EXIGetID 0x%08x)\n",
             ctx->card ? "in slot A" : "off: the EXI functions were not all found", ecb, exi[GC_EXI_IMM],
             exi[GC_EXI_GET_ID]);
    }
    const std::uint32_t ar_dma = find_dsp_stores(code, {0xCC005020, 0xCC005022, 0xCC005024, 0xCC005026,
                                                         0xCC005028, 0xCC00502A});
    if (ar_dma != 0) replay_hook(ar_dma, h.hook_ar_dma, h.replay_ar_dma, h.continue_ar_dma);
    // The sound program reads ARAM from MEM2 (where the blob keeps it) in
    // one of two ways, and a Wii and Dolphin each take only one of them.
    // The DSP's sample reader keeps its addresses in the samples' own units
    // (nibbles for 4-bit samples, words for 16-bit). A Wii picks MEM2 by
    // bit 28 of that register: Nintendont's patched programs set it there,
    // and they play right on a Wii. Dolphin turns the register into a byte
    // address first and looks at bit 28 of that, and its fast HLE audio
    // reads byte addresses itself and stops on a patched program. So:
    //  - on a Wii, the program is patched (patch_dsp_programs). Raising
    //    the game's byte addresses instead (raise_aram_base) put bit 29,
    //    not 28, into a 4-bit sample's register, and Super Mario Eclipse's
    //    music came out garbled on a Wii (clean in Dolphin);
    //  - in Dolphin, and for a program not in Nintendont's list, the game's
    //    ARAM addresses are raised.
    const bool on_wii = !extras.in_dolphin;
    const unsigned dsp_patched = on_wii ? patch_dsp_programs(dol) : 0;
    std::vector<std::uint32_t> stub_words;
    // A game whose own checks need the SDK's ARAM addresses keeps them
    // (gxpatches.hpp's gc_keeps_aram_addresses).
    const bool keep_aram = gc_keeps_aram_addresses(image.id) || test_keep_aram;
    const unsigned aram_raised =
        dsp_patched || keep_aram ? 0 : raise_aram_base(code, find_aram_check(code), stubs_addr, stub_words);
    const unsigned aram_limits = aram_raised ? raise_aram_limits(code) : 0;
    for (std::size_t i = 0; i < stub_words.size(); ++i) store32(stubs_addr + 4 * i, stub_words[i]);
    DCFlushRange(reinterpret_cast<void*>(stubs_addr), kStubBytes);
    ICInvalidateRange(reinterpret_cast<void*>(stubs_addr), kStubBytes);
    if (dsp_patched)
        logf("GameCube: %u sound program(s) patched to read ARAM from MEM2 (a Wii)\n", dsp_patched);
    else if (keep_aram && !gc_keeps_aram_addresses(image.id))
        logf("GameCube: ARAM addresses left as the SDK gives them, as sd:/riftwii/gc/test/keeparam asks (the sound "
             "may play wrong)\n");
    else if (keep_aram)
        logf("GameCube: ARAM addresses left as the SDK gives them: %s checks them against its own limit "
             "(its sound may play wrong)\n", image.id.c_str());
    else
        logf("GameCube: ARAM addresses from 0x10004000 (%u place(s), %u sound heap limit(s) raised with them) for the "
             "sound program to read MEM2 (%s)\n",
             aram_raised, aram_limits, on_wii ? "a Wii, the program not one Nintendont's list knows" : "Dolphin");
    const bool aram_check_skipped = skip_aram_size_check(code);
    const unsigned ai_waits = skip_ai_stream_waits(code);
    logf("GameCube: %u wait(s) for the AI stream counter skipped\n", ai_waits);
    // Colour-indexed textures (C4/C8/C14X2) get their palettes through
    // GXInitTlutObj, whose old SDK builds the load word from the old
    // word's top bits (riftwii/gcvideo.hpp): harmless on a GameCube,
    // which ignores addresses past its 24 MB, but in Wii mode the GPU is
    // sent to MEM2 or nowhere and Mario Party 4 comes out in the wrong
    // colours.
    const GcTlutSites tlut = find_gc_tlut_sites(code.ranges());
    for (std::uint32_t at : tlut.stale) code.put(at, gc_tlut_li_for(code.at(at)));
    if (!tlut.stale.empty()) {
        const std::uint32_t first_fn = function_start(code, tlut.stale.front());
        const std::uint32_t where = first_fn != 0 ? first_fn : tlut.stale.front();
        if (tlut.stale.size() == 1) {
            logf("GameCube: GXInitTlutObj 0x%08x builds its palette address from the new pointer alone (it kept "
                 "stale bits that, in Wii mode, point the GPU at the wrong memory: wrong colours on paletted "
                 "textures)\n",
                 static_cast<unsigned>(where));
        } else {
            logf("GameCube: %u GXInitTlutObj site(s), first at 0x%08x, build their palette addresses from the new "
                 "pointer alone (they kept stale bits that, in Wii mode, point the GPU at the wrong memory: wrong "
                 "colours on paletted textures)\n",
                 static_cast<unsigned>(tlut.stale.size()), static_cast<unsigned>(where));
        }
    } else if (!tlut.clean.empty()) {
        logf("GameCube: GXInitTlutObj already builds its palette address from the new pointer alone (%u site(s) "
             "already clean)\n",
             static_cast<unsigned>(tlut.clean.size()));
    } else {
        logf("GameCube: no GXInitTlutObj keeping stale palette address bits found (none in this game, or another "
             "SDK's that the search does not know): its palettes load as the game builds them\n");
    }
    const GcClocks clocks = match_game_to_clocks(code, dol);
    logf("GameCube: ARAM in MEM2 through ARStartDMA 0x%08x, size check %s\n", ar_dma,
         aram_check_skipped ? "skipped" : "not found");
    // A crash shows itself on the screen and in the trace instead of a
    // blank halt.
    const std::uint32_t unhandled = find_unhandled_exception(code, dol);
    hook(unhandled, h.hook_fatal);
    std::vector<std::uint32_t> moved;
    const bool moved_out = move_out_of_ios(code, moved_addr, moved);
    for (std::size_t i = 0; i < moved.size(); ++i) store32(moved_addr + 4 * i, moved[i]);
    DCFlushRange(reinterpret_cast<void*>(moved_addr), kMovedBytes);
    ICInvalidateRange(reinterpret_cast<void*>(moved_addr), kMovedBytes);
    logf("GameCube: the code at IOS's counter (0x%08x) %s\n", kIosCounter,
         moved_out ? "runs from a copy" : "left where it is");
    // The guard (gc_rt.h): the game's bytes for IOS's globals area, taken
    // after every patch above (the branch to the moved code is one), or the
    // guard would put the unpatched words back.
    for (const Section& s : dol.text)
        if (s.address <= 0x80003100 && s.address + s.bytes.size() >= 0x80003200) {
            ctx->guard_addr = 0x80003100;
            for (std::uint32_t i = 0; i < 64; ++i) ctx->guard_copy[i] = be32(&s.bytes[0x80003100 - s.address + 4 * i]);
        }
    logf("GameCube: __OSUnhandledException 0x%08x\n", unhandled);
    logf("GameCube: PADRead 0x%08x%s%s, ARStartDMA 0x%08x\n", pad_read, test_presses ? " (test presses)" : "",
         test_fake_remote ? " (test remote)" : "", ar_dma);
    logf("GameCube: Wii Remotes %s\n", pad_read == 0 ? "off (no PADRead found)" : "on");
    logf("GameCube: PADControlMotor 0x%08x%s\n", motor,
         motor == 0 ? " (not found: a Wii Remote does not rumble)"
         : (ctx->flags & GC_FLAG_REMOTE_RUMBLE) ? ": a Wii Remote rumbles with its port"
                                                : ": Wii Remote rumble is off in the settings");
    logf("GameCube: %u DI register access(es) sent to 0x%08x; AIInitDMA 0x%08x%s\n", di_patches, di_regs, ai_dma,
         ai_dma ? "" : " (not found: no disc audio streaming)");
    if (low.reset != 0) code.put(low.reset, 0x4E800020);  // blr: the drive is not there to reset
    // The cheats' hook, last: every patch above is in, so none can have
    // taken its word unnoticed.
    if (!cheats.gct.empty()) {
        if (hook_gc_cheats(code, cheats, cheats_off)) {
            logf("Codes: %u cheat(s), %u bytes at 0x%08x, handler at 0x%08x called from the retrace handler 0x%08x "
                 "(the handler leaves the FPU alone: float operation codes do nothing)\n",
                 static_cast<unsigned>(extras.cheat_count), static_cast<unsigned>(cheats.gct.size()),
                 kCodeListAddress, kCodeHandlerAddress, cheats.hook);
        } else {
            logf("Codes are off: %s\n", cheats_off.c_str());
            cheats = GcCheats{};
        }
    }

    // The SD card goes to the blob only now, after every patch above, so
    // their log lines still reach boot.log (testers send it). The card,
    // opened and selected: IOS lets one owner have the slot, so the file
    // system lets go first, as a Wii game's launch does.
    // What goes to MEM2 at the jump, as one block: the card's image (when
    // the game has a card) and the blob's buffers right after it.
    const std::uint32_t mem2_card_bytes = ctx->card ? GC_CARD_BYTES : 0;
    const std::uint32_t mem2_from = kGcMem2Data - mem2_card_bytes;
    std::vector<std::uint8_t> mem2_image;
    if (ctx->card) {
        card_image.resize(GC_CARD_BYTES + mem2_bytes);
        std::memcpy(card_image.data() + GC_CARD_BYTES, mem2_stage.data(), mem2_bytes);
        mem2_image.swap(card_image);
    } else {
        mem2_image.swap(mem2_stage);
    }
    logf("Releasing the SD card (the log ends here; the rest is on screen)\n");
    LogClose();
    rwp_sd_card card{};
    if (!hand_over_sd_card(card, error)) return false;
    ctx->sdio_fd = static_cast<std::uint32_t>(card.fd);
    ctx->sdhc = card.sdhc ? 1 : 0;
    ctx->card_rca = card.rca;

    const std::uint32_t tv_mode = image.id.size() > 3 && (image.id[3] == 'P' || image.id[3] == 'D' ||
                                                          image.id[3] == 'F' || image.id[3] == 'S' ||
                                                          image.id[3] == 'I' || image.id[3] == 'X')
                                      ? 1  // PAL
                                      : 0;
    logf("GameCube: entry 0x%08x, FST 0x%08x, blob 0x%08x (%u bytes), %u run(s), arena top 0x%08x, card fd %d %s\n",
         dol.entry, fst_addr, blob_addr, static_cast<unsigned>(h.size), static_cast<unsigned>(placed.size()),
         arena_hi, card.fd, card.sdhc ? "SDHC" : "SDSC");
    DCFlushRange(reinterpret_cast<void*>(bi2_addr), kMem1Top - bi2_addr);
    ICInvalidateRange(reinterpret_cast<void*>(blob_addr), h.size);
    ICInvalidateRange(memcard->landing, sizeof(memcard->landing));

    LogClose();
    // The frame stays on screen until the game sets up its own video; a
    // crash turns it blue (gc_on_fatal) when the VI's registers give none.
    const std::uint32_t xfb = plugin::host().video_framebuffer();
    const std::uint32_t xfb_bytes = 640 * 480 * 2;
    ctx->xfb = xfb;
    ctx->xfb_bytes = xfb_bytes;
    DCFlushRange(ctx, sizeof(*ctx));
    // The Wii's video encoder in the game's region (its own region setting
    // must match the game's picture, or a US game on a PAL Wii shows red as
    // blue), then libogc shut down: RiftWii's shut_down_for_game.
    std::uint32_t handover = tv_mode == 1 ? RWP_TV_PAL : RWP_TV_NTSC;
    if (test_ave_keep) handover = RWP_TV_KEEP;
    else if (test_handover_480p && handover == RWP_TV_NTSC) handover = RWP_TV_NTSC_480P;
    plugin::host().shut_down_for_game(handover);
    u32 level;
    _CPU_ISR_Disable(level);
    (void)level;

    // Low memory as a GameCube's start-up leaves it for the game.
    std::memset(reinterpret_cast<void*>(0x80000000), 0, kGameFloor - 0x80000000);
    for (const Section& s : dol.text) std::memcpy(reinterpret_cast<void*>(s.address), s.bytes.data(), s.bytes.size());
    for (const Section& s : dol.data) std::memcpy(reinterpret_cast<void*>(s.address), s.bytes.data(), s.bytes.size());
    if (dol.bss_size != 0) {
        // Sections may sit inside the BSS range; zero only around them.
        std::vector<std::pair<std::uint32_t, std::uint32_t>> used;
        for (const Section& s : dol.data) used.emplace_back(s.address, s.address + s.bytes.size());
        for (std::uint32_t a = dol.bss; a < dol.bss + dol.bss_size; ++a) {
            bool inside = false;
            for (auto& u : used)
                if (a >= u.first && a < u.second) inside = true;
            if (!inside) *reinterpret_cast<volatile std::uint8_t*>(a) = 0;
        }
    }
    std::memcpy(reinterpret_cast<void*>(0x80000000), boot.data(), 0x20);
    store32(0x80000020, 0x0D15EA5E);
    store32(0x80000024, 1);
    store32(0x80000028, 0x01800000);
    store32(0x8000002C, 0x00000003);  // a retail console
    store32(0x80000034, arena_hi);
    store32(0x80000038, fst_addr);
    store32(0x8000003C, fst_max);
    store32(0x800000CC, tv_mode);
    store32(0x800000D0, 0x01000000);  // ARAM
    store32(0x800000EC, kMem1Top);
    store32(0x800000F0, 0x01800000);
    store32(0x800000F4, bi2_addr);
    store32(0x800000F8, clocks.bus);
    store32(0x800000FC, clocks.cpu);
    // After the clear above, which would have taken them.
    if (!cheats.gct.empty()) place_gc_cheats(cheats, image.id);
    // IOS's replies stay polled: no IPC interrupt reaches the game.
    store32(0xCD000004, 0);
    // Nor any other of Hollywood's interrupts: the game's OS knows none of
    // them and could not clear one (HW_PPCIRQMASK, then the flags acked).
    store32(0xCD000034, 0);
    store32(0xCD000030, 0xFFFFFFFF);
    // A first trace record through the blob's own IPC, as the game will
    // use it. The guarded bytes in memory before the checks' IOS requests compare them.
    DCFlushRange(reinterpret_cast<void*>(0x80003100), 0x100);
    // MEM2 now, before the blob's first trace record (its buffer is
    // there): one move of what was put together above. The heap it comes
    // from may be where it goes; memmove copes with one overlapping copy,
    // and nothing reads the heap after.
    std::memmove(reinterpret_cast<void*>(mem2_from), mem2_image.data(), mem2_image.size());
    DCFlushRange(reinterpret_cast<void*>(mem2_from), mem2_image.size());
    reinterpret_cast<void (*)(std::uint32_t)>(blob_addr + h.trace_now)(GC_TRACE_JUMP);
    // Until the game's OS installs its own exception handlers, every
    // vector goes to the blob's crash screen (gc_on_fatal), so a crash in
    // the first moments shows instead of hanging on an empty vector. Each
    // stub saves r3, SRR0, SRR1 and its number in SPRG0-3 and rfi's to
    // dsi_entry with translation on.
    {
        const std::uint32_t entry = blob_addr + h.dsi_entry;
        for (std::uint32_t vector = 0x2; vector <= 0xD; ++vector) {
            const std::uint32_t stub[] = {
                0x7C7043A6,                    // mtsprg 0,r3
                0x38600000 | vector,           // li r3,vector
                0x7C7343A6,                    // mtsprg 3,r3
                0x7C7A02A6,                    // mfsrr0 r3
                0x7C7143A6,                    // mtsprg 1,r3
                0x7C7B02A6,                    // mfsrr1 r3
                0x7C7243A6,                    // mtsprg 2,r3
                0x3C600000 | (entry >> 16),    // lis r3,entry@h
                0x60630000 | (entry & 0xFFFF), // ori r3,r3,entry@l
                0x7C7A03A6,                    // mtsrr0 r3
                0x7C6000A6,                    // mfmsr r3
                0x60630030,                    // ori r3,r3,0x30 (IR, DR)
                0x7C7B03A6,                    // mtsrr1 r3
                0x4C000064,                    // rfi
            };
            for (std::size_t i = 0; i < sizeof(stub) / sizeof(stub[0]); ++i)
                store32(0x80000000 + vector * 0x100 + 4 * i, stub[i]);
        }
    }
    // The restart's copy of MEM1 as the game starts, last, once nothing
    // else is written there; what it overwrites in MEM2 (the loader's heap)
    // is no longer needed.
    for (std::uint32_t i = 0; i < restart_count; ++i) {
        const gc_restart_region& r = restart_table[i];
        if (r.source == 0) continue;
        std::memcpy(reinterpret_cast<void*>(r.source), reinterpret_cast<const void*>(r.address), r.length);
        DCFlushRange(reinterpret_cast<void*>(r.source), r.length);
    }
    if (restart_count != 0) {
        std::memcpy(reinterpret_cast<void*>(kGcRestartWindow), restart_table, sizeof(restart_table));
        DCFlushRange(reinterpret_cast<void*>(kGcRestartWindow), sizeof(restart_table));
    }
    DCFlushRange(reinterpret_cast<void*>(0x80000000), kLoaderStart - 0x80000000);
    ICInvalidateRange(reinterpret_cast<void*>(0x80000000), kLoaderStart - 0x80000000);
    reinterpret_cast<void (*)()>(dol.entry)();
    return false;
}

}  // namespace riftwii::wii
