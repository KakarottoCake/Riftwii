// SPDX-License-Identifier: GPL-3.0-or-later
// Credit: USB Loader GX (https://github.com/wiidev/usbloadergx),
// source/patches/gamepatches.c: PatchReturnTo (giantpune's "magic super
// patch to return to channels"), GPL-3.0. Reimplemented here; see NOTICE.md.
#include "riftwii/returnto.hpp"

#include <cstring>

namespace riftwii {
namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

void put32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

// li rD,value
constexpr std::uint32_t li(unsigned d, std::uint16_t value) { return 0x38000000u | (d << 21) | value; }

struct Site {
    std::uint8_t* at;
    std::uint32_t address;
};

// The three places, in order: the first is followed by "li rZ,0" as well.
std::vector<Site> find_sites(const std::vector<CodeSpan>& spans, unsigned rl, unsigned rh, unsigned rz) {
    std::vector<Site> sites;
    for (const CodeSpan& s : spans) {
        for (std::size_t o = 0; o + 8 <= s.size && sites.size() < 3; o += 4) {
            const std::uint8_t* p = s.bytes + o;
            if (be32(p) != li(rl, 2) || be32(p + 4) != li(rh, 1)) continue;
            if (sites.empty() && (o + 12 > s.size || be32(p + 8) != li(rz, 0))) continue;
            sites.push_back(Site{s.bytes + o, s.address + static_cast<std::uint32_t>(o)});
        }
    }
    return sites;
}

}  // namespace

std::string ReturnToReport::describe() const {
    if (patched) return std::string("patched") + (old_sdk ? " (older SDK)" : "");
    if (!stub_place) return "not patched: no place for the stub";
    return "not patched: " + std::to_string(sites) + " of the 3 places found";
}

ReturnToReport patch_return_to(const std::vector<CodeSpan>& spans, std::uint32_t title_low) {
    ReturnToReport report;
    // The stub's place: 0x30 past "Metrowerks T", word-aligned.
    static const char kMark[] = "Metrowerks T";
    std::uint8_t* stub = nullptr;
    std::uint32_t stub_address = 0;
    for (const CodeSpan& s : spans) {
        for (std::size_t o = 0; !stub && o + 12 <= s.size; ++o) {
            if (std::memcmp(s.bytes + o, kMark, 12) != 0) continue;
            const std::size_t at = (o + 0x30 + 3) & ~std::size_t(3);
            if (at + 20 <= s.size) {
                stub = s.bytes + at;
                stub_address = s.address + static_cast<std::uint32_t>(at);
            }
        }
    }
    report.stub_place = stub != nullptr;

    // Newer SDKs load the title into r3/r4 (r5 = 0), older ones into r5/r6.
    unsigned rh = 3, rl = 4;
    std::vector<Site> sites = find_sites(spans, 4, 3, 5);
    if (sites.size() < 3) {
        std::vector<Site> old = find_sites(spans, 6, 5, 7);
        if (old.size() == 3) {
            sites = old;
            rh = 5;
            rl = 6;
            report.old_sdk = true;
        }
    }
    report.sites = static_cast<unsigned>(sites.size());
    if (!stub || sites.size() != 3) return report;

    // lis rH,1; ori rH,rH,1; lis rL,title>>16; ori rL,rL,title; blr
    put32(stub, 0x3C000000u | (rh << 21) | 1u);
    put32(stub + 4, 0x60000000u | (rh << 21) | (rh << 16) | 1u);
    put32(stub + 8, 0x3C000000u | (rl << 21) | (title_low >> 16));
    put32(stub + 12, 0x60000000u | (rl << 21) | (rl << 16) | (title_low & 0xFFFF));
    put32(stub + 16, 0x4E800020u);
    for (const Site& site : sites) {
        put32(site.at, 0x48000001u | ((stub_address - site.address) & 0x03FFFFFCu));  // bl stub
        put32(site.at + 4, 0x60000000u);                                              // nop
    }
    report.patched = true;
    return report;
}

}  // namespace riftwii
