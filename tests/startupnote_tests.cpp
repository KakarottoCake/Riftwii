// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/startupnote.hpp"

#include <cstring>
#include <iostream>

int main() {
    using namespace riftwii;
    unsigned failures = 0;
    const auto check = [&](bool passed) {
        if (!passed) ++failures;
    };
    constexpr unsigned count = 20;
    StartupNote note;
    startup_note_begin(note, "2610-test", 1, count);
    check(startup_note_valid(note, count));
    check(note.previous_step == 0);
    startup_note_mark(note, 8);
    startup_note_begin(note, "2610-next", 1, count);
    check(note.previous_step == 8);
    check(std::strcmp(note.previous_version, "2610-test") == 0);
    // Each failed attempt reports its own last step on the next good start.
    startup_note_mark(note, 9);
    startup_note_begin(note, "2610-next", 1, count);
    check(note.previous_step == 9);
    check(std::strcmp(note.previous_version, "2610-next") == 0);
    // Damaged game memory and unknown layouts never supply a log string.
    note.version[47] = 'x';
    check(!startup_note_valid(note, count));
    startup_note_begin(note, "new", 1, count);
    check(note.previous_step == 0);
    note.step = count;
    note.checksum = startup_note_checksum(note);
    check(!startup_note_valid(note, count));
    startup_note_begin(note, "new", 1, count);
    note.layout = 2;
    note.checksum = startup_note_checksum(note);
    check(!startup_note_valid(note, count));
    // LogReady clears the note; subsequent file opens cannot rearm it.
    note = StartupNote{};
    startup_note_mark(note, 7);
    check(note.magic == 0);
    startup_note_begin(note, "new", 1, count);
    check(note.previous_step == 0);
    std::cout << failures << " start-up note failures\n";
    return failures == 0 ? 0 : 1;
}
