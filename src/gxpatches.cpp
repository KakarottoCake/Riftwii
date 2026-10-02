// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: USB Loader GX contributors <https://github.com/wiidev/usbloadergx>
// SPDX-FileCopyrightText: crediar
// SPDX-FileCopyrightText: WiiPower
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gxpatches.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace riftwii {
namespace {

#include "gxkirby.inc"

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}
void put32(std::uint8_t* p, std::uint32_t v) {
    p[0] = std::uint8_t(v >> 24);
    p[1] = std::uint8_t(v >> 16);
    p[2] = std::uint8_t(v >> 8);
    p[3] = std::uint8_t(v);
}

// The loaded bytes at `address` when all `length` of them are loaded.
std::uint8_t* at(const std::vector<CodeSpan>& loaded, std::uint32_t address, std::size_t length) {
    for (const CodeSpan& s : loaded) {
        if (address >= s.address && address - s.address <= s.size && length <= s.size - (address - s.address))
            return s.bytes + (address - s.address);
    }
    return nullptr;
}

std::string hex(std::uint32_t v) {
    char buf[12];
    std::snprintf(buf, sizeof(buf), "0x%08x", static_cast<unsigned>(v));
    return buf;
}

bool id_is(const std::string& id, const char* want) {
    const std::size_t n = std::strlen(want);
    return id.size() >= n && id.compare(0, n, want) == 0;
}

// *(u32 *)address = value, as GX writes them.
struct Store {
    std::uint32_t address, value;
};

unsigned store_all(const std::vector<CodeSpan>& loaded, const Store* stores, std::size_t count, unsigned& missing) {
    unsigned done = 0;
    for (std::size_t i = 0; i < count; ++i) {
        std::uint8_t* p = at(loaded, stores[i].address, 4);
        if (!p) {
            ++missing;
            continue;
        }
        put32(p, stores[i].value);
        ++done;
    }
    return done;
}

// patch_kirby: kirbypatch.c, as the table tools/gx_kirby_table.py made.
unsigned kirby(const std::string& id, const std::vector<CodeSpan>& loaded, GxReport& report) {
    const std::uint32_t* table = nullptr;
    std::size_t count = 0;
    if (id_is(id, "SUKE01")) table = kKirbySUKE01, count = sizeof(kKirbySUKE01) / 4;
    else if (id_is(id, "SUKP01")) table = kKirbySUKP01, count = sizeof(kKirbySUKP01) / 4;
    else if (id_is(id, "SUKJ01")) table = kKirbySUKJ01, count = sizeof(kKirbySUKJ01) / 4;
    else if (id_is(id, "SUKK01")) table = kKirbySUKK01, count = sizeof(kKirbySUKK01) / 4;
    if (!table) return 0;
    unsigned done = 0, missing = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint32_t address = 0x80000000u | (table[i] & 0x00FFFFFFu);
        std::uint8_t* p = at(loaded, address, 4);
        if (!p) {
            ++missing;
            continue;
        }
        put32(p, kKirbyValues[table[i] >> 24]);
        ++done;
    }
    report.notes.push_back("MetaFortress (Kirby's Return to Dream Land): " + std::to_string(done) + " checks patched" +
                           (missing ? ", " + std::to_string(missing) + " outside the game" : std::string()));
    return done;
}

// patch_re4: GameCube controllers in Resident Evil 4 Wii Edition.
unsigned re4(const std::string& id, const std::vector<CodeSpan>& loaded, GxReport& report) {
    Store s{0, 0x38600001u};
    if (id_is(id, "RB4E08")) s.address = 0x8016B260u;
    else if (id_is(id, "RB4P08")) s.address = 0x8016B094u;
    else if (id_is(id, "RB4X08")) s.address = 0x8016B0C8u;
    else return 0;
    unsigned missing = 0;
    const unsigned done = store_all(loaded, &s, 1, missing);
    report.notes.push_back(std::string("Resident Evil 4: GameCube controllers ") + (done ? "on" : "not patched (not loaded)"));
    return done;
}

// WIP codes (wip.cpp's do_wip_code): byte offsets into the executable as
// the apploader loads it after its first three reads, that is the DOL
// header then its sections in order; each byte changes only where it holds
// the expected one.
struct Wip {
    std::uint32_t offset, original, replacement;
};

unsigned wip(const DolHeader& dol, const std::vector<CodeSpan>& loaded, const Wip* codes, std::size_t count,
             unsigned& mismatched) {
    unsigned done = 0;
    for (std::size_t c = 0; c < count; ++c) {
        for (std::uint32_t n = 0; n < 4; ++n) {
            const std::uint32_t offset = codes[c].offset + n;
            std::uint32_t position = 0x100;  // the header, GX's fourth load
            std::uint32_t address = 0;
            for (const DolSection& s : dol.sections) {
                if (!s.used()) continue;
                if (offset >= position && offset - position < s.size) {
                    address = s.address + (offset - position);
                    break;
                }
                position += s.size;
            }
            std::uint8_t* p = address ? at(loaded, address, 1) : nullptr;
            const std::uint8_t want = std::uint8_t(codes[c].original >> (24 - 8 * n));
            if (!p || *p != want) {
                ++mismatched;
                continue;
            }
            *p = std::uint8_t(codes[c].replacement >> (24 - 8 * n));
            ++done;
        }
    }
    return done;
}

unsigned nsmb(const std::string& id, const DolHeader& dol, const std::vector<CodeSpan>& loaded, GxReport& report) {
    static const Wip kE[] = {{0x001AB610, 0x9421FFD0, 0x4E800020}, {0x001CED53, 0xDA000000, 0x71000000},
                             {0x001CED6B, 0xDA000000, 0x71000000}};
    static const Wip kP[] = {{0x001AB750, 0x9421FFD0, 0x4E800020}, {0x001CEE90, 0x38A000DA, 0x38A00071},
                             {0x001CEEA8, 0x388000DA, 0x38800071}};
    static const Wip kJ[] = {{0x001AB420, 0x9421FFD0, 0x4E800020}, {0x001CEB63, 0xDA000000, 0x71000000},
                             {0x001CEB7B, 0xDA000000, 0x71000000}};
    const Wip* codes = id_is(id, "SMNE01") ? kE : id_is(id, "SMNP01") ? kP : id_is(id, "SMNJ01") ? kJ : nullptr;
    if (!codes) return 0;
    unsigned mismatched = 0;
    const unsigned done = wip(dol, loaded, codes, 3, mismatched);
    report.notes.push_back("New Super Mario Bros. Wii: BCA check " + std::to_string(done) + " byte(s) patched" +
                           (mismatched ? ", " + std::to_string(mismatched) + " not as expected" : std::string()));
    return done;
}

unsigned prince_of_persia(const std::string& id, const DolHeader& dol, const std::vector<CodeSpan>& loaded,
                          GxReport& report) {
    if (!id_is(id, "SPX") && !id_is(id, "RPW")) return 0;
    static const Wip kCodes[] = {{0x007AAC6A, 0x7A6B6F6A, 0x6F6A7A6B},
                                 {0x007AAC75, 0x7C7A6939, 0x69397C7A},
                                 {0x007AAC82, 0x7376686B, 0x686B7376},
                                 {0x007AAC92, 0x80717570, 0x75708071},
                                 {0x007AAC9D, 0x82806F3F, 0x6F3F8280}};
    unsigned mismatched = 0;
    const unsigned done = wip(dol, loaded, kCodes, 5, mismatched);
    report.notes.push_back("Prince of Persia: " + std::to_string(done) + " byte(s) patched" +
                           (mismatched ? ", " + std::to_string(mismatched) + " not as expected" : std::string()));
    return done;
}

// anti_002_fix (WiiPower): "cmpwi r0,0; b +0x214; lis r3,0x8000" becomes a
// "bne +0x214", once, in each loaded part.
unsigned anti_002(const std::vector<CodeSpan>& loaded, GxReport& report) {
    unsigned done = 0;
    for (const CodeSpan& s : loaded) {
        for (std::size_t i = 0; i + 12 <= s.size; i += 4) {
            if (be32(s.bytes + i) == 0x2C000000u && be32(s.bytes + i + 4) == 0x48000214u &&
                be32(s.bytes + i + 8) == 0x3C608000u) {
                put32(s.bytes + i + 4, 0x40820214u);
                report.notes.push_back("Error #002 check at " + hex(s.address + std::uint32_t(i + 4)) + " patched");
                ++done;
                break;
            }
        }
    }
    return done;
}

}  // namespace

bool gx_protected_game(const std::string& id) {
    return id_is(id, "RPW") || id_is(id, "SPX") || id_is(id, "SDV") || id_is(id, "STN") || id_is(id, "SLVP41");
}

bool gx_kirby_game(const std::string& id) {
    return id_is(id, "SUKE01") || id_is(id, "SUKP01") || id_is(id, "SUKJ01") || id_is(id, "SUKK01");
}

unsigned gx_game_patches(const std::string& game_id, const DolHeader& dol, const std::vector<CodeSpan>& loaded,
                         GxReport& report, bool own_executable) {
    unsigned done = nsmb(game_id, dol, loaded, report);
    done += prince_of_persia(game_id, dol, loaded, report);
    if (own_executable) {
        if (gx_kirby_game(game_id) || game_id.compare(0, 3, "RB4") == 0)
            report.notes.push_back("the pack's own main.dol: the game's fixed-address patches left out");
    } else {
        done += kirby(game_id, loaded, report);
        done += re4(game_id, loaded, report);
    }
    done += anti_002(loaded, report);
    return done;
}

unsigned gx_sd_card_patches(const std::string& id, const std::vector<CodeSpan>& loaded, GxReport& report) {
    Store stores[2];
    std::size_t count = 0;
    const char* name = nullptr;
    if (id_is(id, "REXE01")) stores[count++] = {0x800b9e48u, 0x4800014cu};
    else if (id_is(id, "REXP01")) stores[count++] = {0x800ba358u, 0x4800014cu};
    else if (id_is(id, "REXJ01")) stores[count++] = {0x800ba404u, 0x4800014cu};
    else if (id_is(id, "SUKE01")) stores[count++] = {0x8022da10u, 0x60000000u}, stores[count++] = {0x8022da48u, 0x60000000u};
    else if (id_is(id, "SUKP01")) stores[count++] = {0x8022e800u, 0x60000000u}, stores[count++] = {0x8022e838u, 0x60000000u};
    else if (id_is(id, "SUKJ01")) stores[count++] = {0x8022c66cu, 0x60000000u}, stores[count++] = {0x8022c6a4u, 0x60000000u};
    else if (id_is(id, "SUKK01")) stores[count++] = {0x8022dfc4u, 0x60000000u}, stores[count++] = {0x8022dffcu, 0x60000000u};
    if (count == 0) return 0;
    name = id_is(id, "REX") ? "Excite Truck" : "Kirby's Return to Dream Land";
    unsigned missing = 0;
    const unsigned done = store_all(loaded, stores, count, missing);
    report.notes.push_back(std::string(name) + " from the SD card: " + std::to_string(done) + " word(s) patched");
    return done;
}

unsigned gx_region_video_fix(char region, const std::vector<CodeSpan>& loaded, GxReport& report) {
    if (region != 'E' && region != 'J') {
        report.notes.push_back("Region video fix: only for US and Japanese games, left out");
        return 0;
    }
    static const std::uint32_t kSwitch[3] = {0x4182000C, 0x4180001C, 0x48000018};  // beq +12; blt +28; b +24
    const std::uint32_t kReadBit = 0x5400FFFE;                                      // rlwinm r0, r0, 31, 31, 31
    const std::uint32_t value = region == 'E' ? 0x38000000u : 0x38000001u;          // li r0, 0 / li r0, 1
    unsigned done = 0;
    for (const CodeSpan& s : loaded) {
        for (std::size_t at = 0; at + 12 <= s.size; at += 4) {
            if (be32(s.bytes + at) != kSwitch[0] || be32(s.bytes + at + 4) != kSwitch[1] ||
                be32(s.bytes + at + 8) != kSwitch[2])
                continue;
            for (std::size_t read = at; read + 4 <= s.size; read += 4) {
                if (be32(s.bytes + read) != kReadBit) continue;
                put32(s.bytes + read, value);
                report.notes.push_back("Region video fix: the NTSC-J bit read at " +
                                       hex(s.address + static_cast<std::uint32_t>(read)) + " gives " +
                                       (region == 'E' ? "0" : "1"));
                ++done;
                break;
            }
        }
    }
    if (done == 0) report.notes.push_back("Region video fix: the game does not read the NTSC-J bit that way");
    return done;
}

bool gx_fix_480p(const std::vector<CodeSpan>& loaded, GxReport& report) {
    // Where the stb that stores the wrong value is (the word after it is
    // where the branch back lands), and the two instructions that store 3.
    static const std::uint32_t kMkw[6] = {0x38000065, 0x9b810019, 0x38810018, 0x386000e0, 0x98010018, 0x38a00002};
    static const std::uint32_t kMkwFix[2] = {0x38600003, 0x98610019};
    static const std::uint32_t kNsmb[6] = {0x38000065, 0x9801001c, 0x3881001c, 0x386000e0, 0x9b81001d, 0x38a00002};
    static const std::uint32_t kNsmbFix[2] = {0x38a00003, 0x98a1001d};
    const auto matches = [](const std::uint8_t* p, const std::uint32_t* words) {
        for (int i = 0; i < 6; ++i)
            if (be32(p + 4 * i) != words[i]) return false;
        return true;
    };
    const auto bl_prefix = [](const std::uint8_t* p) { return p[0] == 0x4b && p[1] == 0xff; };
    std::uint32_t site = 0;
    const std::uint32_t* fix = nullptr;
    for (const CodeSpan& s : loaded) {
        if (s.address >= 0x80900000u) continue;  // GX looks in 0x80000000-0x80900000
        for (std::size_t i = 4; i + 9 * 4 <= s.size && !site; i += 4) {
            const std::uint8_t* p = s.bytes + i;
            if (!bl_prefix(p - 4) || !bl_prefix(p + 8 * 4)) continue;
            if (matches(p, kMkw)) site = s.address + std::uint32_t(i) + 4, fix = kMkwFix;
            else if (matches(p, kNsmb)) site = s.address + std::uint32_t(i) + 16, fix = kNsmbFix;
        }
        if (site) break;
    }
    if (!site) return false;
    // find_safe_space: a spot in the SDK's video code, 36 bytes past
    // "lwz r0,0(r30 or r31); lis r3,0x8000; lwz ..." where "li r0,1"
    // stands; the instructions go 32 bytes further ("This TV format").
    std::uint32_t space = 0;
    for (const CodeSpan& s : loaded) {
        if (s.address >= 0x80900000u) continue;
        for (std::size_t i = 0; i + 36 + 4 <= s.size && !space; i += 4) {
            const std::uint8_t* p = s.bytes + i;
            if ((be32(p) == 0x801E0000u || be32(p) == 0x801F0000u) && be32(p + 4) == 0x3C608000u && p[8] == 0x83 &&
                be32(p + 36) == 0x38000001u)
                space = s.address + std::uint32_t(i) + 36;
        }
        if (space) break;
    }
    if (!space) {
        report.notes.push_back("480p fix: the game has no spare room for it");
        return false;
    }
    const std::uint32_t patch = space + 32;
    std::uint8_t* code = at(loaded, patch, 12);
    std::uint8_t* from = at(loaded, site, 4);
    if (!code || !from) return false;
    put32(code, fix[0]);
    put32(code + 4, fix[1]);
    put32(from, 0x48000000u + ((patch - site) & 0x3ffffffu));
    put32(code + 8, 0x48000000u + (((site + 4) - (patch + 8)) & 0x3ffffffu));
    report.notes.push_back("480p fix: " + hex(site) + " branches to " + hex(patch));
    return true;
}

int gx_pick_cios(const std::string& game_id, int requested, std::vector<D2xSlot> slots, bool sd_card,
                 std::string& why) {
    // Duplicates of a base: the first slot keeps it.
    std::vector<D2xSlot> unique;
    for (const D2xSlot& s : slots) {
        if (s.base <= 0) continue;
        bool seen = false;
        for (const D2xSlot& u : unique) seen = seen || u.base == s.base;
        if (!seen) unique.push_back(s);
    }
    std::sort(unique.begin(), unique.end(), [](const D2xSlot& a, const D2xSlot& b) { return a.base < b.base; });
    const auto slot_of = [&](int base) {
        for (const D2xSlot& s : unique)
            if (s.base == base) return s.slot;
        return 0;
    };
    if (requested <= 0 || unique.empty()) return 0;
    bool restricted = false;
    if (id_is(game_id, "SBV")) {
        if (!slot_of(requested)) {
            const int asked = requested;
            requested = slot_of(58) ? 58 : 38;
            why = "SpongeBob's Boating Bash: no base IOS" + std::to_string(asked) + " cIOS, so base " +
                  std::to_string(requested);
        }
    } else if (id_is(game_id, "RSB")) {
        requested = 56;  // "SSBB mods like Infinite won't work with IOS 38"
        why = "Brawl asks for a base IOS56 cIOS";
    } else if (sd_card) {
        const std::size_t before = unique.size();
        unique.erase(std::remove_if(unique.begin(), unique.end(),
                                    [](const D2xSlot& s) { return s.base < 56 || s.base > 60; }),
                     unique.end());
        restricted = unique.size() != before;
        if (unique.empty()) {
            why = "no d2x cIOS with base 56-60, which the SD card needs";
            return 0;
        }
    }
    if (int slot = slot_of(requested)) return slot;
    const auto next = std::lower_bound(unique.begin(), unique.end(), requested,
                                       [](const D2xSlot& s, int r) { return s.base < r; });
    const D2xSlot& pick = next == unique.end() ? unique.back() : *next;
    if (why.empty()) why = "no base IOS" + std::to_string(requested) + " cIOS; the closest is base " + std::to_string(pick.base);
    if (restricted) why += " (bases 56-60 for the SD card)";
    return pick.slot;
}

}  // namespace riftwii
