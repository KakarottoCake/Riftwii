// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgpstage2.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <zlib.h>

#include "riftwii/ctgparena.hpp"

namespace riftwii {
namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

std::int32_t s32(std::uint32_t v) { return static_cast<std::int32_t>(v); }
std::uint32_t sext16(std::uint32_t v) { return static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(v & 0xFFFF))); }

std::uint32_t rotl(std::uint32_t v, std::uint32_t n) {
    n &= 31;
    return n == 0 ? v : (v << n) | (v >> (32 - n));
}

// The rlwinm mask from bit mb to bit me (bit 0 is the most significant).
std::uint32_t rlw_mask(std::uint32_t mb, std::uint32_t me) {
    const std::uint32_t from_mb = 0xFFFFFFFFu >> mb;
    const std::uint32_t to_me = 0xFFFFFFFFu << (31 - me);
    return mb <= me ? (from_mb & to_me) : (from_mb | to_me);
}

// CTGP-R's loader's system-call handler (it saves HID0 in r3, sets bit 28 for
// one sync and puts HID0 back): the builder checks the first word, and sc
// runs it.
constexpr std::uint32_t kHandlerAddress = 0x80000c00;
constexpr std::uint32_t kHandler[7] = {0x7c70faa6, 0x60640008, 0x7c90fba6, 0x4c00012c,
                                       0x7c0004ac, 0x7c70fba6, 0x4c000064};

constexpr std::uint32_t kXerCa = 0x20000000;
constexpr std::uint32_t kSprXer = 1, kSprLr = 8, kSprCtr = 9, kSprHid0 = 1008, kSprIabr = 1010, kSprDabr = 1013;

}  // namespace

bool ctgp_parse_dol(const std::uint8_t* dol, std::size_t size, CtgpDolLayout& out, std::string& error) {
    if (dol == nullptr || size < 0x100) {
        error = "MAIN.DOL is too short to be a program";
        return false;
    }
    CtgpDolLayout layout;
    for (int i = 0; i < 18; ++i) {
        CtgpDolSection s;
        s.offset = be32(dol + 4 * i);
        s.address = be32(dol + 0x48 + 4 * i);
        s.size = be32(dol + 0x90 + 4 * i);
        s.text = i < 7;
        if (s.size == 0) continue;
        const bool in_file = s.offset >= 0x100 && s.offset <= size && size - s.offset >= s.size;
        const bool in_mem1 = s.address >= 0x80000000u && s.address < 0x81800000u && 0x81800000u - s.address >= s.size;
        if (!in_file || !in_mem1) {
            error = "MAIN.DOL's section table points outside the file or the Wii's memory";
            return false;
        }
        layout.sections.push_back(s);
    }
    layout.bss_address = be32(dol + 0xd8);
    layout.bss_size = be32(dol + 0xdc);
    if (layout.bss_size != 0 && (layout.bss_address < 0x80000000u || layout.bss_address >= 0x81800000u ||
                                 0x81800000u - layout.bss_address < layout.bss_size)) {
        error = "MAIN.DOL's BSS lies outside the Wii's memory";
        return false;
    }
    if (layout.sections.empty()) {
        error = "MAIN.DOL has no sections";
        return false;
    }
    out = std::move(layout);
    return true;
}

// ---------------------------------------------------------------------------
// The interpreter

CtgpPpc::CtgpPpc(const std::uint8_t* dol, const CtgpDolLayout& layout) : dol_(dol), layout_(layout) {}

bool CtgpPpc::backing(std::uint32_t line, Line& out) const {
    out.fill(0);
    bool mapped = false;
    const std::uint64_t line_end = std::uint64_t(line) + 32;
    for (const CtgpDolSection& s : layout_.sections) {
        const std::uint64_t end = std::uint64_t(s.address) + s.size;
        if (end <= line || s.address >= line_end) continue;
        mapped = true;
        for (std::uint32_t i = 0; i < 32; ++i) {
            const std::uint32_t a = line + i;
            if (a >= s.address && a < end) out[i] = dol_[s.offset + (a - s.address)];
        }
    }
    if (layout_.bss_size != 0 && line < std::uint64_t(layout_.bss_address) + layout_.bss_size &&
        layout_.bss_address < line_end)
        mapped = true;
    if (line >= kCtgpPpcStackTop - kCtgpPpcStackBytes && line < kCtgpPpcStackTop) mapped = true;
    if (line == kHandlerAddress) {
        for (std::uint32_t i = 0; i < 7; ++i) {
            out[4 * i] = static_cast<std::uint8_t>(kHandler[i] >> 24);
            out[4 * i + 1] = static_cast<std::uint8_t>(kHandler[i] >> 16);
            out[4 * i + 2] = static_cast<std::uint8_t>(kHandler[i] >> 8);
            out[4 * i + 3] = static_cast<std::uint8_t>(kHandler[i]);
        }
        mapped = true;
    }
    return mapped;
}

bool CtgpPpc::mapped(std::uint32_t address) const {
    for (const CtgpDolSection& s : layout_.sections)
        if (address >= s.address && address - s.address < s.size) return true;
    if (layout_.bss_size != 0 && address >= layout_.bss_address && address - layout_.bss_address < layout_.bss_size)
        return true;
    if (address >= kCtgpPpcStackTop - kCtgpPpcStackBytes && address < kCtgpPpcStackTop) return true;
    return address >= kHandlerAddress && address < kHandlerAddress + sizeof kHandler;
}

bool CtgpPpc::view(std::uint32_t line, const std::map<std::uint32_t, Line>& layer, Line& out) const {
    const auto it = layer.find(line);
    if (it != layer.end()) {
        out = it->second;
        return true;
    }
    return backing(line, out);
}

bool CtgpPpc::read(std::uint32_t address, std::uint8_t* out, std::uint32_t length) const {
    for (std::uint32_t i = 0; i < length; ++i) {
        const std::uint32_t a = address + i;
        Line l;
        if (a < address || !mapped(a) || !view(a & ~31u, data_, l)) return false;
        out[i] = l[a & 31];
    }
    return true;
}

bool CtgpPpc::wrote_into(std::uint32_t address, std::uint32_t length) const {
    const std::uint64_t end = std::uint64_t(address) + length;
    for (const auto& line : data_) {
        if (line.first + 32ull > address && line.first < end) return true;
    }
    return false;
}

bool CtgpPpc::load(std::uint32_t address, std::uint32_t length, std::uint32_t& value) {
    std::uint8_t bytes[4];
    if ((address & (length - 1)) != 0 || !read(address, bytes, length)) {
        fault_ = "a load outside the decoder's memory";
        return false;
    }
    value = 0;
    for (std::uint32_t i = 0; i < length; ++i) value = (value << 8) | bytes[i];
    return true;
}

bool CtgpPpc::store(std::uint32_t address, std::uint32_t length, std::uint32_t value) {
    const std::uint32_t line = address & ~31u;
    Line l;
    // Aligned stores only (a line holds them whole); the handler is read-only.
    if ((address & (length - 1)) != 0 || line == kHandlerAddress || !mapped(address) ||
        !mapped(address + length - 1) || !view(line, data_, l)) {
        fault_ = "a store outside the decoder's memory";
        return false;
    }
    if (data_.size() >= kCtgpPpcMaxLines && data_.find(line) == data_.end()) {
        fault_ = "the decoder's memory overlay is full";
        return false;
    }
    for (std::uint32_t i = 0; i < length; ++i)
        l[(address & 31) + i] = static_cast<std::uint8_t>(value >> (8 * (length - 1 - i)));
    data_[line] = l;
    return true;
}

bool CtgpPpc::flush(std::uint32_t address) {
    const std::uint32_t line = address & ~31u;
    Line l;
    if (!view(line, data_, l)) {
        fault_ = "a cache flush outside the decoder's memory";
        return false;
    }
    if (memory_.size() >= kCtgpPpcMaxLines && memory_.find(line) == memory_.end()) {
        fault_ = "the decoder's memory overlay is full";
        return false;
    }
    memory_[line] = l;
    return true;
}

bool CtgpPpc::fetch(std::uint32_t address, std::uint32_t& word) {
    const std::uint32_t line = address & ~31u;
    if (!mapped(address)) {
        fault_ = "an instruction fetch outside the decoder's memory";
        return false;
    }
    auto it = icache_.find(line);
    if (it == icache_.end()) {
        Line l;
        if ((address & 3) != 0 || !mapped(address) || !view(line, memory_, l) || icache_.size() >= kCtgpPpcMaxLines) {
            fault_ = "an instruction fetch outside the decoder's memory";
            return false;
        }
        it = icache_.emplace(line, l).first;
    }
    word = be32(it->second.data() + (address & 31));
    return true;
}

bool CtgpPpc::run_until(std::uint32_t stop, std::uint32_t max_steps, std::string& error) {
    while (pc != stop) {
        if (steps_ >= max_steps) {
            error = "the decoder stopped after its step limit";
            return false;
        }
        if (!step(error)) return false;
        ++steps_;
    }
    return true;
}

bool CtgpPpc::step(std::string& error) {
    char where[64];
    std::uint32_t inst = 0;
    if (!fetch(pc, inst)) {
        std::snprintf(where, sizeof where, " at 0x%08x", static_cast<unsigned>(pc));
        error = fault_ + where;
        return false;
    }
    const std::uint32_t op = inst >> 26;
    const std::uint32_t rd = (inst >> 21) & 31, ra = (inst >> 16) & 31, rb = (inst >> 11) & 31;
    const std::uint32_t uimm = inst & 0xFFFF;
    const std::uint32_t simm = sext16(inst);
    const std::uint32_t ra0 = ra == 0 ? 0 : r[ra];
    std::uint32_t next = pc + 4;
    bool ok = true;

    auto set_cr = [&](std::uint32_t field, std::uint32_t bits) {
        const std::uint32_t shift = 28 - 4 * field;
        cr = (cr & ~(0xFu << shift)) | ((bits & 0xF) << shift);
    };
    auto compare_signed = [&](std::uint32_t field, std::uint32_t a, std::uint32_t b) {
        const std::uint32_t lt = s32(a) < s32(b) ? 8 : s32(a) > s32(b) ? 4 : 2;
        set_cr(field, lt | ((xer >> 31) & 1));
    };
    auto compare_unsigned = [&](std::uint32_t field, std::uint32_t a, std::uint32_t b) {
        const std::uint32_t lt = a < b ? 8 : a > b ? 4 : 2;
        set_cr(field, lt | ((xer >> 31) & 1));
    };
    auto record = [&](std::uint32_t value) { compare_signed(0, value, 0); };
    auto set_carry = [&](bool carry) { xer = carry ? (xer | kXerCa) : (xer & ~kXerCa); };
    auto branch_taken = [&](std::uint32_t bo, std::uint32_t bi) {
        bool ctr_ok = true;
        if ((bo & 4) == 0) {
            --ctr;
            ctr_ok = (ctr != 0) != ((bo & 2) != 0);
        }
        const bool cond_ok = (bo & 16) != 0 || (((cr >> (31 - bi)) & 1) == ((bo >> 3) & 1));
        return ctr_ok && cond_ok;
    };
    auto unknown = [&]() {
        std::snprintf(where, sizeof where, "an instruction it does not run (0x%08x at 0x%08x)",
                      static_cast<unsigned>(inst), static_cast<unsigned>(pc));
        error = std::string("the decoder met ") + where;
        return false;
    };

    switch (op) {
    case 7: r[rd] = static_cast<std::uint32_t>(s32(r[ra]) * s32(simm)); break;  // mulli
    case 8: {                                                                       // subfic
        const std::uint64_t sum = std::uint64_t(~r[ra]) + simm + 1;
        r[rd] = static_cast<std::uint32_t>(sum);
        set_carry(sum >> 32);
        break;
    }
    case 10: compare_unsigned(rd >> 2, r[ra], uimm); break;                      // cmpli
    case 11: compare_signed(rd >> 2, r[ra], simm); break;                         // cmpi
    case 12:                                                                      // addic
    case 13: {                                                                    // addic.
        const std::uint64_t sum = std::uint64_t(r[ra]) + simm;
        r[rd] = static_cast<std::uint32_t>(sum);
        set_carry(sum >> 32);
        if (op == 13) record(r[rd]);
        break;
    }
    case 14: r[rd] = ra0 + simm; break;            // addi
    case 15: r[rd] = ra0 + (uimm << 16); break;    // addis
    case 16: {                                     // bc
        const std::uint32_t target = sext16(inst & 0xFFFC) + ((inst & 2) ? 0 : pc);
        const bool taken = branch_taken(rd, ra);
        if (inst & 1) lr = pc + 4;
        if (taken) next = target;
        break;
    }
    case 17:  // sc: the loader's handler
        r[3] = kCtgpPpcHid0;
        r[4] = kCtgpPpcHid0 | 8;
        break;
    case 18: {  // b
        std::uint32_t li = inst & 0x03FFFFFC;
        if (li & 0x02000000) li |= 0xFC000000;
        if (inst & 1) lr = pc + 4;
        next = (inst & 2) ? li : pc + li;
        break;
    }
    case 19: {
        const std::uint32_t xo = (inst >> 1) & 0x3FF;
        if (xo == 16 || xo == 528) {  // bclr, bcctr
            const std::uint32_t target = (xo == 16 ? lr : ctr) & ~3u;
            const bool taken = xo == 16 ? branch_taken(rd, ra) : branch_taken(rd | 4, ra);
            if (inst & 1) lr = pc + 4;
            if (taken) next = target;
        } else if (xo == 150) {  // isync
        } else {
            return unknown();
        }
        break;
    }
    case 20:    // rlwimi
    case 21: {  // rlwinm
        const std::uint32_t mask = rlw_mask((inst >> 6) & 31, (inst >> 1) & 31);
        const std::uint32_t rotated = rotl(r[rd], rb);
        r[ra] = op == 21 ? (rotated & mask) : ((rotated & mask) | (r[ra] & ~mask));
        if (inst & 1) record(r[ra]);
        break;
    }
    case 24: r[ra] = r[rd] | uimm; break;           // ori
    case 25: r[ra] = r[rd] | (uimm << 16); break;   // oris
    case 26: r[ra] = r[rd] ^ uimm; break;           // xori
    case 27: r[ra] = r[rd] ^ (uimm << 16); break;   // xoris
    case 28: r[ra] = r[rd] & uimm; record(r[ra]); break;  // andi.
    case 31: {
        const std::uint32_t xo = (inst >> 1) & 0x3FF;
        const std::uint32_t xo9 = xo & 0x1FF;
        const bool rc = (inst & 1) != 0;
        const std::uint32_t a = r[ra], b = r[rb];
        auto set_d = [&](std::uint32_t v) {
            r[rd] = v;
            if (rc) record(v);
        };
        auto set_a = [&](std::uint32_t v) {
            r[ra] = v;
            if (rc) record(v);
        };
        std::uint32_t value = 0;
        if (xo9 == 266) set_d(a + b);                                        // add
        else if (xo9 == 40) set_d(b - a);                                    // subf
        else if (xo9 == 104) set_d(0u - a);                                  // neg
        else if (xo9 == 235) set_d(static_cast<std::uint32_t>(s32(a) * s32(b)));  // mullw
        else if (xo9 == 11) set_d(static_cast<std::uint32_t>((std::uint64_t(a) * b) >> 32));  // mulhwu
        else if (xo9 == 459) set_d(b == 0 ? 0 : a / b);                      // divwu
        else if (xo9 == 491) {                                               // divw
            const bool undefined = b == 0 || (a == 0x80000000u && b == 0xFFFFFFFFu);
            set_d(undefined ? 0 : static_cast<std::uint32_t>(s32(a) / s32(b)));
        } else if (xo9 == 202) {                                             // addze
            const std::uint64_t sum = std::uint64_t(a) + ((xer & kXerCa) ? 1 : 0);
            set_carry(sum >> 32);
            set_d(static_cast<std::uint32_t>(sum));
        } else if (xo9 == 10 || xo9 == 8) {                                  // addc, subfc
            const std::uint64_t sum = xo9 == 10 ? std::uint64_t(a) + b : std::uint64_t(~a) + b + 1;
            set_carry(sum >> 32);
            set_d(static_cast<std::uint32_t>(sum));
        } else if (xo == 0) compare_signed(rd >> 2, a, b);                   // cmp
        else if (xo == 32) compare_unsigned(rd >> 2, a, b);                  // cmpl
        else if (xo == 28) set_a(r[rd] & b);                                 // and
        else if (xo == 60) set_a(r[rd] & ~b);                                // andc
        else if (xo == 444) set_a(r[rd] | b);                                // or (mr)
        else if (xo == 316) set_a(r[rd] ^ b);                                // xor
        else if (xo == 124) set_a(~(r[rd] | b));                             // nor
        else if (xo == 24) set_a((b & 32) ? 0 : r[rd] << (b & 31));          // slw
        else if (xo == 536) set_a((b & 32) ? 0 : r[rd] >> (b & 31));         // srw
        else if (xo == 824 || xo == 792) {                                   // srawi, sraw
            const std::uint32_t n = xo == 824 ? rb : (b & 63);
            const std::int32_t v = s32(r[rd]);
            const std::uint32_t shift = n > 31 ? 31 : n;
            const bool lost = n > 31 ? v != 0 : (shift != 0 && (static_cast<std::uint32_t>(v) & ((1u << shift) - 1)) != 0);
            set_carry(v < 0 && lost);
            set_a(static_cast<std::uint32_t>(v >> shift));
        } else if (xo == 954) set_a(static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(r[rd]))));  // extsb
        else if (xo == 922) set_a(sext16(r[rd]));                            // extsh
        else if (xo == 26) {                                                 // cntlzw
            std::uint32_t n = 0;
            while (n < 32 && (r[rd] & (0x80000000u >> n)) == 0) ++n;
            set_a(n);
        } else if (xo == 23 || xo == 87 || xo == 279) {                      // lwzx, lbzx, lhzx
            const std::uint32_t length = xo == 23 ? 4 : xo == 87 ? 1 : 2;
            if (!load(ra0 + b, length, value)) ok = false;
            else r[rd] = value;
        } else if (xo == 151 || xo == 215 || xo == 407) {                    // stwx, stbx, sthx
            const std::uint32_t length = xo == 151 ? 4 : xo == 215 ? 1 : 2;
            if (!store(ra0 + b, length, r[rd])) ok = false;
        } else if (xo == 339 || xo == 467) {                                 // mfspr, mtspr
            const std::uint32_t spr = ((inst >> 16) & 31) | (((inst >> 11) & 31) << 5);
            if (xo == 339) {
                if (spr == kSprXer) r[rd] = xer;
                else if (spr == kSprLr) r[rd] = lr;
                else if (spr == kSprCtr) r[rd] = ctr;
                else if (spr == kSprHid0) r[rd] = kCtgpPpcHid0;
                else if (spr == kSprIabr) r[rd] = iabr_;
                else if (spr == kSprDabr) r[rd] = dabr_;
                else return unknown();
            } else {
                if (spr == kSprXer) xer = r[rd];
                else if (spr == kSprLr) lr = r[rd];
                else if (spr == kSprCtr) ctr = r[rd];
                else if (spr == kSprIabr) iabr_ = r[rd] & ~1u;  // bit 31 reads back as 0
                else if (spr == kSprDabr) dabr_ = r[rd];
                else return unknown();
            }
        } else if (xo == 19) r[rd] = cr;                                     // mfcr
        else if (xo == 144) {                                                // mtcrf
            const std::uint32_t fxm = (inst >> 12) & 0xFF;
            for (std::uint32_t f = 0; f < 8; ++f)
                if (fxm & (0x80u >> f)) set_cr(f, r[rd] >> (28 - 4 * f));
        } else if (xo == 86 || xo == 54) {                                   // dcbf, dcbst
            if (!flush(ra0 + b)) ok = false;
        } else if (xo == 982) {                                              // icbi
            icache_.erase((ra0 + b) & ~31u);
        } else if (xo == 598 || xo == 854) {                                 // sync, eieio
        } else {
            return unknown();
        }
        break;
    }
    case 32: case 33: case 34: case 35: case 40: case 41: {  // lwz(u), lbz(u), lhz(u)
        const std::uint32_t length = op <= 33 ? 4 : op <= 35 ? 1 : 2;
        const bool update = (op & 1) != 0;
        if (update && (ra == 0 || ra == rd)) return unknown();
        const std::uint32_t address = (update ? r[ra] : ra0) + simm;
        std::uint32_t value = 0;
        if (!load(address, length, value)) {
            ok = false;
            break;
        }
        r[rd] = value;
        if (update) r[ra] = address;
        break;
    }
    case 36: case 37: case 38: case 39: case 44: case 45: {  // stw(u), stb(u), sth(u)
        const std::uint32_t length = op <= 37 ? 4 : op <= 39 ? 1 : 2;
        const bool update = (op & 1) != 0;
        if (update && ra == 0) return unknown();
        const std::uint32_t address = (update ? r[ra] : ra0) + simm;
        if (!store(address, length, r[rd])) {
            ok = false;
            break;
        }
        if (update) r[ra] = address;
        break;
    }
    case 46: case 47: {  // lmw, stmw
        std::uint32_t address = ra0 + simm;
        for (std::uint32_t reg = rd; reg < 32 && ok; ++reg, address += 4) {
            if (op == 46) {
                std::uint32_t value = 0;
                ok = load(address, 4, value);
                if (ok) r[reg] = value;
            } else {
                ok = store(address, 4, r[reg]);
            }
        }
        break;
    }
    default:
        return unknown();
    }
    if (!ok) {
        std::snprintf(where, sizeof where, " (0x%08x at 0x%08x)", static_cast<unsigned>(inst), static_cast<unsigned>(pc));
        error = fault_ + where;
        return false;
    }
    pc = next;
    return true;
}

// ---------------------------------------------------------------------------
// The table builder

namespace {

// Where in the file an address of a DOL section is, if the whole range
// [address, address + length) lies in one section.
bool file_offset(const CtgpDolLayout& layout, std::uint32_t address, std::uint32_t length, std::uint32_t& offset) {
    for (const CtgpDolSection& s : layout.sections) {
        if (address >= s.address && address - s.address <= s.size && s.size - (address - s.address) >= length) {
            offset = s.offset + (address - s.address);
            return true;
        }
    }
    return false;
}

// The substitution loop: for each byte whose address is not a multiple of 16,
// byte = table[byte]. Seven instructions; returns the registers it uses.
struct SubstitutionLoop {
    std::uint32_t address = 0;
    std::uint32_t pointer = 0;  // register walking the payload
    std::uint32_t table = 0;    // register holding the table's address
};

bool match_loop(const std::uint8_t* code, SubstitutionLoop& loop) {
    std::uint32_t w[7];
    for (int i = 0; i < 7; ++i) w[i] = be32(code + 4 * i);
    const std::uint32_t p = (w[0] >> 21) & 31, t = (w[0] >> 16) & 31;
    if ((w[0] >> 26) != 28 || (w[0] & 0xFFFF) != 15) return false;               // andi. t,p,15
    if (w[1] != 0x41820010) return false;                                         // beq +16
    if (w[2] != ((34u << 26) | (t << 21) | (p << 16))) return false;              // lbz t,0(p)
    const std::uint32_t base = (w[3] >> 16) & 31;
    if (w[3] != ((31u << 26) | (t << 21) | (base << 16) | (t << 11) | (87u << 1))) return false;  // lbzx t,base,t
    if (w[4] != ((38u << 26) | (t << 21) | (p << 16))) return false;              // stb t,0(p)
    if (w[5] != ((14u << 26) | (p << 21) | (p << 16) | 1)) return false;           // addi p,p,1
    // bdnz back to the first instruction (any prediction hint).
    if ((w[6] & 0xFC00FFFF) != 0x4000FFE8 || (((w[6] >> 21) & 0x16) != 0x10)) return false;
    if (p == 0 || t == p || base == t || base == p) return false;
    loop.pointer = p;
    loop.table = base;
    return true;
}

}  // namespace

CtgpStage2Result ctgp_stage2_table(const std::uint8_t* dol, std::size_t size, CtgpStage2Table& out,
                                   std::string& error) {
    CtgpDolLayout layout;
    if (!ctgp_parse_dol(dol, size, layout, error)) return CtgpStage2Result::Damaged;

    // Exactly one substitution loop in the program's text.
    SubstitutionLoop loop;
    int found = 0;
    const CtgpDolSection* loop_section = nullptr;
    for (const CtgpDolSection& s : layout.sections) {
        if (!s.text) continue;
        for (std::uint32_t at = 0; at + 28 <= s.size; at += 4) {
            SubstitutionLoop candidate;
            if (!match_loop(dol + s.offset + at, candidate)) continue;
            candidate.address = s.address + at;
            loop = candidate;
            loop_section = &s;
            ++found;
        }
    }
    if (found != 1) {
        error = found == 0 ? "MAIN.DOL has no unpacking loop the decoder knows"
                           : "MAIN.DOL has more than one unpacking loop";
        return CtgpStage2Result::Unsupported;
    }
    // The function that holds it starts at the nearest stwu r1,-n(r1) before it.
    std::uint32_t start = 0;
    for (std::uint32_t at = loop.address - loop_section->address; at >= 4;) {
        at -= 4;
        const std::uint32_t w = be32(dol + loop_section->offset + at);
        if ((w & 0xFFFF8000) == 0x94218000) {
            start = loop_section->address + at;
            break;
        }
    }
    if (start == 0) {
        error = "MAIN.DOL's unpacking function has no start the decoder can find";
        return CtgpStage2Result::Unsupported;
    }

    CtgpPpc cpu(dol, layout);
    cpu.r[1] = kCtgpPpcStackTop - 0x400;
    cpu.lr = 0;  // a return before the loop fetches from 0, outside its memory
    cpu.pc = start;
    std::string run_error;
    if (!cpu.run_until(loop.address, kCtgpStage2MaxSteps, run_error)) {
        error = "MAIN.DOL's table builder did not run to its end: " + run_error;
        return CtgpStage2Result::Unsupported;
    }

    const std::uint32_t table_address = cpu.r[loop.table];
    const std::uint32_t payload = cpu.r[loop.pointer];
    const std::uint32_t length = cpu.ctr;
    std::uint32_t payload_offset = 0;
    if (length < 16 || !file_offset(layout, payload, length, payload_offset)) {
        error = "MAIN.DOL's packed program is not inside the file";
        return CtgpStage2Result::Unsupported;
    }
    const std::uint64_t table_end = std::uint64_t(table_address) + 256;
    if (table_end > payload && table_address < std::uint64_t(payload) + length) {
        error = "MAIN.DOL's table overlaps its packed program";
        return CtgpStage2Result::Unsupported;
    }
    CtgpStage2Table result;
    if (!cpu.read(table_address, result.table.data(), 256)) {
        error = "MAIN.DOL's table is outside the decoder's memory";
        return CtgpStage2Result::Unsupported;
    }
    // The builder must not have changed the packed bytes themselves: the
    // mapping below starts from the file's.
    if (cpu.wrote_into(payload, length)) {
        error = "MAIN.DOL's table builder changed its packed program";
        return CtgpStage2Result::Unsupported;
    }
    bool seen[256] = {};
    for (std::uint8_t b : result.table) seen[b] = true;
    for (bool s : seen) {
        if (!s) {
            error = "MAIN.DOL's table builder made a table that is not a one-to-one mapping";
            return CtgpStage2Result::Unsupported;
        }
    }
    result.payload_address = payload;
    result.payload_length = length;
    result.payload_offset = payload_offset;
    result.steps = cpu.steps();
    out = result;
    return CtgpStage2Result::Ok;
}

// ---------------------------------------------------------------------------
// The payload

namespace {

// zlib's own memory (its state and 32 KB window) comes from the launch's MEM2
// arena when there is one.
voidpf arena_zalloc(voidpf, uInt items, uInt size) {
    const std::size_t bytes = std::size_t(items) * size;
    if (g_ctgp_arena != nullptr) {
        if (void* p = g_ctgp_arena->allocate(bytes)) return p;
    }
    return std::malloc(bytes);
}
void arena_zfree(voidpf, voidpf p) {
    if (g_ctgp_arena != nullptr && g_ctgp_arena->owns(p)) {
        g_ctgp_arena->release(p);
        return;
    }
    std::free(p);
}

constexpr std::uint32_t kLayoutCountAddress = 0x816ffed8;  // the game's layout count (ctgpapply.hpp)
constexpr std::uint32_t kLookBack = 256;                    // instructions searched before the store
constexpr std::uint32_t kMaxCandidates = 4;
// The deflate data starts 10 bytes into the payload (the unpacker writes a
// two-byte zlib header at +8).
constexpr std::uint32_t kDeflateStart = 10;

// Watches the inflated program, a word at a time, for the settings step:
//   lis b,hi / addi b,b,lo ... mulli i,x,0xdc ... lwzx v,b,i ... stw v,0xfed8(any)
// and keeps the three entries at b once the stream passes them.
class CupFinder {
public:
    void feed(const std::uint8_t* bytes, std::size_t length) {
        for (std::size_t i = 0; i < length; ++i) {
            const std::uint64_t position = offset_ + i;
            capture(position, bytes[i]);
            partial_ = (partial_ << 8) | bytes[i];
            if ((position & 3) == 3) word(partial_);
        }
        offset_ += length;
    }
    std::uint64_t size() const { return offset_; }
    bool too_many() const { return too_many_; }
    struct Candidate {
        std::uint32_t address = 0;
        std::array<std::uint8_t, 3 * kCtgpCupEntryBytes> bytes{};
        std::uint32_t filled = 0;
        bool missed = false;  // the table was before the code that names it
    };
    std::vector<Candidate> candidates;

private:
    void capture(std::uint64_t position, std::uint8_t b) {
        for (Candidate& c : candidates) {
            const std::uint64_t start = c.address - kCtgpChannelBase;
            if (position >= start && position < start + c.bytes.size()) {
                c.bytes[position - start] = b;
                ++c.filled;
            }
        }
    }
    std::uint32_t back(std::uint32_t n) const { return ring_[(count_ - 1 - n) % kLookBack]; }
    void word(std::uint32_t w) {
        ring_[count_ % kLookBack] = w;
        ++count_;
        if ((w >> 26) != 36 || (w & 0xFFFF) != (kLayoutCountAddress & 0xFFFF)) return;
        const std::uint32_t value = (w >> 21) & 31;
        const std::uint32_t limit = count_ < kLookBack ? count_ : kLookBack;
        for (std::uint32_t n = 1; n < limit; ++n) {
            const std::uint32_t x = back(n);
            if ((x >> 26) != 31 || ((x >> 1) & 0x3FF) != 23 || ((x >> 21) & 31) != value) continue;
            found_load(n, (x >> 16) & 31, (x >> 11) & 31, limit);
            return;
        }
    }
    void found_load(std::uint32_t at, std::uint32_t ra, std::uint32_t rb, std::uint32_t limit) {
        std::uint32_t index_reg = 32;
        for (std::uint32_t n = at + 1; n < limit; ++n) {
            const std::uint32_t y = back(n);
            const std::uint32_t dest = (y >> 21) & 31;
            if ((y >> 26) == 7 && (y & 0xFFFF) == kCtgpCupEntryBytes && (dest == ra || dest == rb)) {
                index_reg = dest;
                break;
            }
        }
        if (index_reg == 32) return;
        const std::uint32_t base = index_reg == rb ? ra : rb;
        bool have_low = false;
        std::uint32_t low = 0;
        for (std::uint32_t n = at + 1; n < limit; ++n) {
            const std::uint32_t y = back(n);
            if (!have_low && (y >> 26) == 14 && ((y >> 21) & 31) == base && ((y >> 16) & 31) == base) {
                low = sext16(y);
                have_low = true;
                continue;
            }
            if (have_low && (y >> 26) == 15 && ((y >> 21) & 31) == base && ((y >> 16) & 31) == 0) {
                add(((y & 0xFFFF) << 16) + low);
                return;
            }
        }
    }
    void add(std::uint32_t address) {
        for (const Candidate& c : candidates)
            if (c.address == address) return;
        if (candidates.size() >= kMaxCandidates) {
            too_many_ = true;
            return;
        }
        Candidate c;
        c.address = address;
        c.missed = address < kCtgpChannelBase || address - kCtgpChannelBase < offset_;
        candidates.push_back(c);
    }

    std::uint64_t offset_ = 0;
    std::uint32_t partial_ = 0;
    std::uint32_t ring_[kLookBack] = {};
    std::uint32_t count_ = 0;
    bool too_many_ = false;
};

struct InflateGuard {
    z_stream stream{};
    bool started = false;
    ~InflateGuard() {
        if (started) inflateEnd(&stream);
    }
};

}  // namespace

CtgpStage2Result ctgp_stage2_cups(std::uint8_t* dol, std::size_t size, const CtgpStage2Table& table,
                                  CtgpChannelCups& out, std::string& error) {
    if (table.payload_length <= kDeflateStart || table.payload_offset > size ||
        size - table.payload_offset < table.payload_length) {
        error = "MAIN.DOL's packed program is not inside the file";
        return CtgpStage2Result::Damaged;
    }
    std::uint8_t* payload = dol + table.payload_offset;
    for (std::uint32_t i = 0; i < table.payload_length; ++i) {
        if (((table.payload_address + i) & 15) != 0) payload[i] = table.table[payload[i]];
    }

    InflateGuard z;
    z.stream.zalloc = arena_zalloc;
    z.stream.zfree = arena_zfree;
    if (inflateInit2(&z.stream, -MAX_WBITS) != Z_OK) {
        error = "the unpacker did not start";
        return CtgpStage2Result::Damaged;
    }
    z.started = true;
    z.stream.next_in = payload + kDeflateStart;
    z.stream.avail_in = table.payload_length - kDeflateStart;
    CtgpBytes chunk(32 * 1024);
    CupFinder finder;
    int status = Z_OK;
    while (status == Z_OK) {
        z.stream.next_out = chunk.data();
        z.stream.avail_out = static_cast<uInt>(chunk.size());
        status = inflate(&z.stream, Z_NO_FLUSH);
        const std::size_t produced = chunk.size() - z.stream.avail_out;
        finder.feed(chunk.data(), produced);
        if (status == Z_BUF_ERROR && produced == 0) break;
        if (status == Z_BUF_ERROR) status = Z_OK;
        // The channel program fits in MEM1: a stream that keeps going is not it.
        if (finder.size() > 24u * 1024 * 1024) break;
    }
    if (status != Z_STREAM_END) {
        error = "MAIN.DOL's packed channel program does not unpack (the data is damaged)";
        return CtgpStage2Result::Damaged;
    }
    if (finder.too_many() || finder.candidates.size() != 1) {
        error = finder.candidates.empty() ? "the channel program has no cup-layout table the decoder can find"
                                          : "the channel program has more than one place that looks like its "
                                            "cup-layout table";
        return CtgpStage2Result::Unsupported;
    }
    const CupFinder::Candidate& c = finder.candidates.front();
    if (c.missed || c.filled != c.bytes.size()) {
        error = "the channel program's cup-layout table is not inside what was unpacked";
        return CtgpStage2Result::Unsupported;
    }
    out.address = c.address;
    out.entries = c.bytes;
    out.inflated = static_cast<std::uint32_t>(finder.size());
    return CtgpStage2Result::Ok;
}

}  // namespace riftwii
