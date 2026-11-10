// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgpimage.hpp"

namespace riftwii {
namespace {

std::uint32_t get32(const CtgpBytes& b, std::size_t at) {
    return (std::uint32_t(b[at]) << 24) | (std::uint32_t(b[at + 1]) << 16) | (std::uint32_t(b[at + 2]) << 8) | b[at + 3];
}
void put32(CtgpBytes& b, std::size_t at, std::uint32_t v) {
    b[at] = static_cast<std::uint8_t>(v >> 24);
    b[at + 1] = static_cast<std::uint8_t>(v >> 16);
    b[at + 2] = static_cast<std::uint8_t>(v >> 8);
    b[at + 3] = static_cast<std::uint8_t>(v);
}
void put16(CtgpBytes& b, std::size_t at, std::uint32_t v) {
    b[at] = static_cast<std::uint8_t>(v >> 8);
    b[at + 1] = static_cast<std::uint8_t>(v);
}

}  // namespace

bool ctgp_parse_row_table(const std::uint8_t* data, std::size_t size, CtgpRowTable& out, std::string& error) {
    if (size < 8 || data[0] != 'C' || data[1] != 'R' || data[2] != 'T' || data[3] != '0') {
        error = "not a CTGP-R row table";
        return false;
    }
    const std::uint32_t count = (std::uint32_t(data[4]) << 24) | (std::uint32_t(data[5]) << 16) |
                                (std::uint32_t(data[6]) << 8) | data[7];
    const std::size_t rows_end = 8 + std::size_t(count) * 12;
    if (count < kCtgpRowCount || count > 0x10000 || (size != rows_end && size != rows_end + 54 * 4)) {
        error = "row table has the wrong size";
        return false;
    }
    CtgpRowTable t;
    t.words.resize(std::size_t(count) * 3);
    for (std::size_t i = 0; i < t.words.size(); ++i) {
        const std::uint8_t* p = data + 8 + i * 4;
        t.words[i] = (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
    }
    if (size == rows_end + 54 * 4) {
        t.low.resize(54);
        for (std::size_t i = 0; i < 54; ++i) {
            const std::uint8_t* p = data + rows_end + i * 4;
            t.low[i] = (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
        }
    }
    out = std::move(t);
    return true;
}

bool ctgp_link(CtgpBytes& boot, CtgpBytes& boat, const std::uint8_t* beat,
               std::size_t beat_size, std::uint32_t bias, std::string& error) {
    CtgpBytes* bufs[2] = {&boot, &boat};
    const std::uint32_t base[2] = {kCtgpBootBase, kCtgpBoatBase + bias};
    int cur = -1;
    std::uint32_t off = 0;
    for (std::size_t i = 0; i + 8 <= beat_size; i += 8) {
        const std::uint32_t skip = (std::uint32_t(beat[i]) << 8) | beat[i + 1];
        const unsigned type = beat[i + 2];
        const unsigned section = beat[i + 3];
        const std::uint32_t addend = (std::uint32_t(beat[i + 4]) << 24) | (std::uint32_t(beat[i + 5]) << 16) |
                                     (std::uint32_t(beat[i + 6]) << 8) | beat[i + 7];
        if (type == 0xCA) {
            if (section > 1) { error = "relocation names a section that does not exist"; return false; }
            cur = static_cast<int>(section);
            off = 0;
            continue;
        }
        if (type == 0xCB) return true;
        if (cur < 0) { error = "relocation before a section was selected"; return false; }
        CtgpBytes& buf = *bufs[cur];
        // Bound even skip-only records before adding: off must not wrap on
        // the Wii's 32-bit size_t or escape the selected section.
        if (off > buf.size() || skip > buf.size() - off) {
            error = "relocation skip outside its section";
            return false;
        }
        off += skip;
        if (type == 0xC9) continue;
        if (section > 1) { error = "relocation names a section that does not exist"; return false; }
        if (off > buf.size() || buf.size() - off < 4) { error = "relocation outside its section"; return false; }
        std::uint32_t val = base[section] + addend;
        if (type > 9) val -= base[cur] + off;
        const std::uint32_t w = get32(buf, off);
        switch (type) {
        case 1:
        case 14: put32(buf, off, val); break;
        case 4: put16(buf, off, val & 0xFFFF); break;
        case 5: put16(buf, off, val >> 16); break;
        case 6: put16(buf, off, (val + 0x8000) >> 16); break;
        case 2:
        case 10:
            if ((w >> 26) != 18) { error = "a branch relocation does not sit on a branch"; return false; }
            put32(buf, off, (val & 0x3FFFFFC) | (w & 0xFC000003));
            break;
        case 7:
        case 8:
        case 9:
        case 11:
            if ((w >> 26) != 16) { error = "a conditional-branch relocation does not sit on a branch"; return false; }
            put32(buf, off, (val & 0xFFFC) | (w & 0xFFFF0003));
            break;
        default:
            error = "unknown relocation type";
            return false;
        }
    }
    return true;
}

bool ctgp_build_image(const CtgpBytes& boot_bin, const CtgpBytes& boat_bin, const CtgpBytes& beat_bin,
                      const CtgpRowTable& rows, CtgpRegion region,
                      CtgpImage& out, std::string& error) {
    if (rows.rows() < kCtgpRowCount) {
        error = "row table is too short";
        return false;
    }
    CtgpImage img;
    img.boot = boot_bin;
    img.boat = boat_bin;
    // the slot tables and hook destinations must fit in BOOT
    const std::size_t need = 0x7C + 2534 * 4;
    if (img.boot.size() < need || img.boot.size() < 0x2814 + 4 * 31 || img.boot.size() < 0x28B4) {
        error = "BOOT.BIN is too small";
        return false;
    }
    if (!ctgp_link(img.boot, img.boat, beat_bin.data(), beat_bin.size(), 0, error)) return false;

    auto slot = [&](std::uint32_t address) { return get32(img.boot, address - kCtgpBootBase); };
    bool ok = true;
    auto add = [&](std::vector<CtgpHook>& list, std::uint32_t row, std::uint32_t dest_slot, std::uint32_t delta,
                   bool link) {
        CtgpHook h;
        h.row = row;
        h.site = rows.row(row, region) + delta;
        h.dest = slot(dest_slot);
        h.link = link;
        const std::int64_t rel = static_cast<std::int64_t>(h.dest) - static_cast<std::int64_t>(h.site);
        if ((h.site & 3) != 0 || (h.dest & 3) != 0 || rel < -0x2000000 || rel > 0x1FFFFFC) ok = false;
        h.word = 0x48000000u | (static_cast<std::uint32_t>(rel) & 0x3FFFFFC) | (link ? 1u : 0u);
        list.push_back(h);
    };
    for (std::uint32_t i = 0; i < 31; ++i) {
        if (i == 28) add(img.hooks, i, 0x81622814 + 4 * i, 0x2A0, true);
        else add(img.hooks, i, 0x81622814 + 4 * i, 0, false);
    }
    add(img.hooks, 2567, 0x816228A4, 0, false);
    add(img.hooks, 2567, 0x816228A8, static_cast<std::uint32_t>(-0x208), true);
    add(img.hooks, 2567, 0x816228AC, static_cast<std::uint32_t>(-0x210), true);
    for (std::uint32_t d : {0x194u, 0x1BCu, 0x1E4u}) add(img.hooks, 2567, 0x816228B0, d, true);
    add(img.optional_hooks, 2568, 0x81622890, 0, false);
    add(img.optional_hooks, 2569, 0x81622894, 0, false);
    if (!ok) {
        error = "a hook cannot reach its destination";
        return false;
    }

    for (std::uint32_t i = 0; i < 31; ++i) put32(img.boot, 4 * i, rows.row(i, region) + 4);
    for (std::uint32_t k = 0; k < 2534; ++k) put32(img.boot, 0x7C + 4 * k, rows.row(31 + k, region));
    put32(img.boot, 0x70, rows.row(28, region) + 0x2A0 + 4);
    put32(img.boot, 0x50, rows.row(2565, region));
    put32(img.boot, 0x68, rows.row(2566, region));
    out = std::move(img);
    return true;
}

}  // namespace riftwii
