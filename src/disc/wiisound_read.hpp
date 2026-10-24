// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstring>

#include "riftwii/wiisound.hpp"

namespace riftwii::sound_detail {

inline std::uint32_t word(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | p[3];
}
inline unsigned half(const std::uint8_t* p) { return (unsigned(p[0]) << 8) | p[1]; }
inline bool reject(std::string& error, const char* why) {
    error = why;
    return false;
}

inline bool header(SoundBytes& file, const char* magic, std::string& error) {
    if (!file.contains(0, 32) || std::memcmp(file.data, magic, 4) != 0 || half(file.data + 4) != 0xfeff ||
        half(file.data + 12) < 32 || !file.contains(0, word(file.data + 8)) || word(file.data + 8) < 32)
        return reject(error, "the sound file header is damaged or unsupported");
    file.size = word(file.data + 8);
    return true;
}

inline bool section(SoundBytes file, unsigned slot, const char* magic, SoundBytes& out, std::string& error) {
    if (!file.contains(slot, 8)) return reject(error, "a sound section descriptor is missing");
    out = file.slice(word(file.data + slot), word(file.data + slot + 4));
    if (!out.contains(0, 8) || std::memcmp(out.data, magic, 4) != 0 || word(out.data + 4) != out.size)
        return reject(error, "a sound section is outside its file or has a damaged header");
    return true;
}

// References on disk must be offsets, never console addresses. All callers
// pass a view whose beginning is the documented reference base.
inline bool reference(SoundBytes base, std::size_t at, std::size_t minimum, SoundBytes& out) {
    if (!base.contains(at, 8) || base.data[at] != 1) return false;
    const std::size_t offset = word(base.data + at + 4);
    if (!base.contains(offset, minimum)) return false;
    out = base.slice(offset, base.size - offset);
    return true;
}

inline bool table(SoundBytes base, std::size_t ref, SoundBytes& out) {
    if (!reference(base, ref, 4, out)) return false;
    const std::size_t count = word(out.data);
    return count <= 4096 && count <= (out.size - 4) / 8;
}

inline bool item(SoundBytes base, SoundBytes table, std::uint32_t index,
                 std::size_t minimum, SoundBytes& out) {
    return table.contains(0, 4) && index < word(table.data) &&
           table.contains(4 + std::size_t(index) * 8, 8) &&
           reference(base, static_cast<std::size_t>(table.data - base.data) + 4 + std::size_t(index) * 8,
                     minimum, out);
}

}  // namespace riftwii::sound_detail
