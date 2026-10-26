// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <cstring>

namespace riftwii {

// Fixed-size, pointer-free state survives a soft return through another loader.
// The layout tag and checksum reject leftovers written by a game or old build.
struct StartupNote {
    std::uint32_t magic = 0;
    std::uint32_t layout = 1;
    std::uint32_t step = 0;
    std::uint32_t previous_step = 0;
    char version[48] = {};
    char previous_version[48] = {};
    std::uint32_t checksum = 0;
};
static_assert(sizeof(StartupNote) <= 0x100);

inline std::uint32_t startup_note_checksum(const StartupNote& note) {
    std::uint32_t sum = note.layout ^ note.step ^ (note.previous_step << 16);
    for (unsigned i = 0; i < sizeof(note.version); ++i) {
        sum = (sum << 5) | (sum >> 27);
        sum ^= static_cast<unsigned char>(note.version[i]);
        sum ^= static_cast<unsigned char>(note.previous_version[i]) << 8;
    }
    return sum;
}

inline bool startup_note_valid(const StartupNote& note, unsigned step_count) {
    return note.magic == 0x52575342 && note.layout == 1 && note.step < step_count &&
           note.previous_step < step_count && note.version[47] == '\0' &&
           note.previous_version[47] == '\0' && note.checksum == startup_note_checksum(note);
}

inline void startup_note_begin(StartupNote& note, const char* version, unsigned step, unsigned step_count) {
    StartupNote next;
    next.magic = 0x52575342;  // RWSB
    next.step = step;
    if (startup_note_valid(note, step_count)) {
        next.previous_step = note.step != 0 ? note.step : note.previous_step;
        const char* previous = note.step != 0 ? note.version : note.previous_version;
        std::memcpy(next.previous_version, previous, sizeof(next.previous_version));
    }
    std::strncpy(next.version, version, sizeof(next.version) - 1);
    next.checksum = startup_note_checksum(next);
    note = next;
}

inline void startup_note_mark(StartupNote& note, unsigned step) {
    // LogReady cleared the magic: later logging and mounts need no new record.
    if (note.magic != 0x52575342) return;
    note.step = step;
    note.checksum = startup_note_checksum(note);
}

}  // namespace riftwii
