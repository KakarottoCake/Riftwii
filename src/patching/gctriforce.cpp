// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gctriforce.hpp"

#include <cstring>
#include <map>
#include <set>

#include "gc_tri.h"

namespace riftwii {
namespace {

constexpr std::uint32_t kBlr = 0x4E800020;

struct Text {
    const std::vector<CodeRange>& ranges;

    bool has(std::uint32_t a) const {
        for (const CodeRange& r : ranges)
            if (a >= r.address && a - r.address + 4 <= r.size && (a & 3) == 0) return true;
        return false;
    }
    std::uint32_t at(std::uint32_t a) const {
        for (const CodeRange& r : ranges) {
            if (a >= r.address && a - r.address + 4 <= r.size) {
                const std::uint8_t* p = r.bytes + (a - r.address);
                return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
            }
        }
        return 0;
    }
    // The word after the previous blr, within 4 KiB.
    std::uint32_t function_start(std::uint32_t a) const {
        for (std::uint32_t b = a - 4; has(b) && a - b < 0x1000; b -= 4)
            if (at(b) == kBlr) return b + 4;
        return 0;
    }
};

std::uint32_t bl_target(std::uint32_t at, std::uint32_t w) {
    if ((w & 0xFC000003u) != 0x48000001u) return 0;
    std::uint32_t d = w & 0x03FFFFFCu;
    if (d & 0x02000000u) d |= 0xFC000000u;
    return at + d;
}

// Every bl in the text: its target, then the places calling it.
std::map<std::uint32_t, std::vector<std::uint32_t>> all_calls(const Text& text) {
    std::map<std::uint32_t, std::vector<std::uint32_t>> calls;
    for (const CodeRange& r : text.ranges)
        for (std::uint32_t off = 0; off + 4 <= r.size; off += 4) {
            const std::uint32_t a = r.address + off;
            if (const std::uint32_t t = bl_target(a, text.at(a))) calls[t].push_back(a);
        }
    return calls;
}

// Whether `word` is among the `back` words before `site`, in its function.
bool word_before(const Text& text, std::uint32_t site, std::uint32_t mask, std::uint32_t word, unsigned back) {
    for (unsigned k = 1; k <= back; ++k) {
        const std::uint32_t a = site - 4 * k;
        if (!text.has(a) || text.at(a) == kBlr) return false;
        if ((text.at(a) & mask) == word) return true;
    }
    return false;
}

// The command byte the checksum helper is given at `site`: the last
// "lis r3,imm" or "oris r3,rX,imm" before it, imm's top byte; -1 if none.
int exi_command_at(const Text& text, std::uint32_t site) {
    for (unsigned k = 1; k <= 12; ++k) {
        const std::uint32_t a = site - 4 * k;
        if (!text.has(a)) return -1;
        const std::uint32_t w = text.at(a);
        if ((w & 0xFFFF0000u) == 0x3C600000u) return int((w >> 8) & 0xFF);       // lis r3,imm
        if ((w & 0xFC1F0000u) == 0x64030000u) return int((w >> 8) & 0xFF);       // oris r3,rX,imm
    }
    return -1;
}

// Whether the function at `start` copies r8 (a sixth argument) early on.
bool takes_sixth_argument(const Text& text, std::uint32_t start) {
    for (unsigned k = 0; k < 24 && text.has(start + 4 * k); ++k) {
        const std::uint32_t w = text.at(start + 4 * k);
        if ((w & 0xFFE0FFFFu) == 0x7D004378u) return true;  // mr rX,r8
        if ((w & 0xFC1FFFFFu) == 0x38080000u) return true;  // addi rX,r8,0
        if (w == kBlr) break;
    }
    return false;
}

}  // namespace

const char* gc_triforce_function_name(unsigned index) {
    switch (index) {
    case GC_TRI_SI_TRANSFER: return "SITransfer";
    case GC_TRI_EXI_CONNECT: return "baseboard connect";
    case GC_TRI_EXI_WRITE_BYTE: return "backup write byte";
    case GC_TRI_EXI_READ_BYTE: return "backup read byte";
    case GC_TRI_EXI_WRITE_BLOCK: return "backup write block";
    case GC_TRI_EXI_READ_BLOCK: return "backup read block";
    case GC_TRI_EXI_READ_ISR: return "baseboard read ISR";
    case GC_TRI_EXI_WRITE_ISR: return "baseboard write ISR";
    case GC_TRI_EXI_READ_IMR: return "baseboard read IMR";
    case GC_TRI_EXI_WRITE_IMR: return "baseboard write IMR";
    case GC_TRI_EXI_WRITE_LANCNT: return "baseboard network reset";
    case GC_TRI_DI_WRITE: return "media board write (0xAA)";
    case GC_TRI_DI_INQUIRY: return "media board inquiry (0x12)";
    case GC_TRI_DI_EXECUTE: return "media board command (0xAB)";
    case GC_TRI_EXI_CALLBACK: return "baseboard interrupt handler";
    default: return "?";
    }
}

GcTriforceFunctions find_gc_triforce_functions(const std::vector<CodeRange>& ranges) {
    const Text text{ranges};
    const auto calls = all_calls(text);
    GcTriforceFunctions out;
    auto& h = out.hooks;

    // SITransfer: called with channel 0, 128 bytes each way.
    for (const auto& callee : calls) {
        for (std::uint32_t site : callee.second) {
            if (word_before(text, site, 0xFFFFFFFFu, 0x38600000u, 10) &&  // li r3,0
                word_before(text, site, 0xFFFFFFFFu, 0x38A00080u, 10) &&  // li r5,0x80
                word_before(text, site, 0xFFFFFFFFu, 0x38E00080u, 10)) {  // li r7,0x80
                h[GC_TRI_SI_TRANSFER] = callee.first;
                break;
            }
        }
        if (h[GC_TRI_SI_TRANSFER] != 0) break;
    }

    // The EXI checksum helper: the function holding "lis rX,0x8D80".
    std::uint32_t check = 0;
    for (const CodeRange& r : ranges) {
        for (std::uint32_t off = 0; off + 4 <= r.size && check == 0; off += 4) {
            const std::uint32_t a = r.address + off;
            if ((text.at(a) & 0xFC1FFFFFu) == 0x3C008D80u) check = text.function_start(a);
        }
    }
    const auto check_calls = check != 0 ? calls.find(check) : calls.end();
    if (check_calls != calls.end()) {
        std::map<std::uint32_t, std::set<int>> commands;  // function -> its command bytes
        for (std::uint32_t site : check_calls->second) {
            const std::uint32_t f = text.function_start(site);
            const int cmd = exi_command_at(text, site);
            if (f != 0 && cmd >= 0) commands[f].insert(cmd);
        }
        for (const auto& fc : commands) {
            const std::set<int>& c = fc.second;
            unsigned index = kGcTriHooks;
            if (c == std::set<int>{0x01, 0x02}) index = GC_TRI_EXI_WRITE_BYTE;
            else if (c == std::set<int>{0x01, 0x05}) index = GC_TRI_EXI_WRITE_BLOCK;
            else if (c == std::set<int>{0x01, 0x03})
                index = takes_sixth_argument(text, fc.first) ? GC_TRI_EXI_READ_BLOCK : GC_TRI_EXI_READ_BYTE;
            else if (c == std::set<int>{0x82}) index = GC_TRI_EXI_READ_ISR;
            else if (c == std::set<int>{0x83}) index = GC_TRI_EXI_WRITE_ISR;
            else if (c == std::set<int>{0x86}) index = GC_TRI_EXI_READ_IMR;
            else if (c == std::set<int>{0x87}) index = GC_TRI_EXI_WRITE_IMR;
            else if (c == std::set<int>{0xFF}) index = GC_TRI_EXI_WRITE_LANCNT;
            if (index < kGcTriHooks && h[index] == 0) h[index] = fc.first;
        }
    }

    // The baseboard's interrupt handler setter: the short function right
    // after the network controller's, which only calls EXISetExiCallback.
    if (const std::uint32_t lan = h[GC_TRI_EXI_WRITE_LANCNT]) {
        std::uint32_t a = lan;
        while (text.has(a) && text.at(a) != kBlr && a - lan < 0x400) a += 4;
        const std::uint32_t next = a + 4;
        unsigned calls_in = 0, words = 0;
        for (std::uint32_t b = next; text.has(b) && words < 16; b += 4, ++words) {
            if (bl_target(b, text.at(b)) != 0) ++calls_in;
            if (text.at(b) == kBlr) break;
        }
        if (calls_in == 1 && words < 16) h[GC_TRI_EXI_CALLBACK] = next;
    }

    // Connect: the EXI ID compared with 0x06041000.
    for (const CodeRange& r : ranges) {
        for (std::uint32_t off = 0; off + 8 <= r.size; off += 4) {
            const std::uint32_t a = r.address + off;
            if ((text.at(a) & 0xFC00FFFFu) == 0x3C00F9FCu && text.at(a + 4) == 0x28001000u) {
                h[GC_TRI_EXI_CONNECT] = text.function_start(a);
                break;
            }
        }
        if (h[GC_TRI_EXI_CONNECT] != 0) break;
    }

    // The media board's commands, through the scrambling helper.
    for (const auto& callee : calls) {
        bool reads = false, writes = false;
        for (std::uint32_t site : callee.second) {
            if (word_before(text, site, 0xFFFFFFFFu, 0x386000A8u, 8)) reads = true;
            if (word_before(text, site, 0xFFFFFFFFu, 0x386000AAu, 8)) writes = true;
        }
        if (!reads || !writes) continue;
        for (std::uint32_t site : callee.second) {
            const std::uint32_t f = text.function_start(site);
            if (word_before(text, site, 0xFFFFFFFFu, 0x386000AAu, 8)) h[GC_TRI_DI_WRITE] = f;
            if (word_before(text, site, 0xFFFFFFFFu, 0x38600012u, 8)) h[GC_TRI_DI_INQUIRY] = f;
            if (word_before(text, site, 0xFC1FFFFFu, 0x600300ABu, 8)) h[GC_TRI_DI_EXECUTE] = f;
        }
        break;
    }

    for (unsigned i = 0; i < kGcTriHooks; ++i) {
        if (h[i] != 0) continue;
        if (!out.missing.empty()) out.missing += ", ";
        out.missing += gc_triforce_function_name(i);
    }
    return out;
}

void gc_tri_identity(gc_tri& t, char region) {
    t.magic = GC_TRI_MAGIC;
    // The SI type: the AM baseboard (0x10110800), its switches 0x100.
    t.status_id[0] = 0x10;
    t.status_id[1] = 0x11;
    t.status_id[2] = 0x09;
    // A made-up serial number in the board's own format.
    std::memcpy(t.serial, "AADE-01R26110001", 16);
    // The region answer: four bytes, the region and its complement, then 0xFF.
    std::memset(t.region, 0xFF, sizeof(t.region));
    const std::uint8_t code = region == 'J' ? 1 : region == 'P' ? 3 : 2;
    t.region[0] = 0x00;
    t.region[1] = 0x00;
    t.region[2] = 0x30;
    t.region[3] = 0x00;
    t.region[4] = code;
    t.region[5] = std::uint8_t(~code);
    t.region[6] = 0x00;
    t.region[7] = 0x00;
    t.firm[0] = 0x00;
    t.firm[1] = 0x26;
    t.fpga[0] = 0x07;
    t.fpga[1] = 0x06;
    // The baseboard's switches, low when on: test 0x80 and service 0x40
    // up, DIP switch 1 (bit 0) on. Mario Kart Arcade GP 2 then skips
    // its start-up check of the card reader and the camera ("no device
    // check"), as a cabinet without them; otherwise it stops on "call
    // staff" for the camera, which a Wii does not have.
    t.status_switches[0] = 0xFE;
    t.status_switches[1] = 0xFF;
    // Namco's FCA-1 I/O board, as Mario Kart Arcade GP 2's cabinet has.
    const char* id = "namco ltd.;FCA-1;Ver1.01;JPN,Multipurpose + Rotary Encoder";
    const std::size_t id_len = std::strlen(id) + 1;
    std::memcpy(t.io_id, id, id_len);
    t.io_id_len = std::uint8_t(id_len);
    const std::uint8_t features[] = {
        0x01, 1, 16, 0,  // switches: a player, 16 each
        0x02, 2, 0, 0,   // coin slots
        0x03, 7, 0, 0,   // analog channels
        0x12, 6, 0, 0,   // general outputs
        0x13, 4, 0, 0,   // analog outputs
        0x00,            // end
    };
    std::memcpy(t.io_features, features, sizeof(features));
    t.io_features_len = std::uint8_t(sizeof(features));
    t.io_command_rev = 0x11;
    t.io_jvs_rev = 0x20;
    t.io_comm_rev = 0x10;
    t.media_inquiry = GC_TRI_MEDIA_INQUIRY;
    std::memcpy(t.board_serial, "AAFE-01R26110001", 16);
    for (unsigned ch = 0; ch < GC_TRI_ANALOG; ++ch) t.analog_in[ch] = GC_TRI_ANALOG_CENTRE;
    t.analog_in[1] = 0;
    t.analog_in[2] = 0;
}

}  // namespace riftwii
