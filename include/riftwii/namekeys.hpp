// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

// The Shift key of the keyboard a Home tile's name is typed on
// (wii/menu/gui_searchkeys.cpp, its name mode), kept apart from the widget
// so its rules can be tested on the host.
//
// Tile names are mod and game names, which use symbols the keys don't
// show: "Ranger Cross (English)", "Project+", "Kamen Rider: Super Climax
// Heroes". A Shift the player presses turns the number row and the last
// three keys of the bottom row into symbols, chosen from the characters
// GameTDB's English titles use (27 Oct 2026, about 10,000 titles: ':' in
// some 2,700 of them, then '!', '(', ')', '&', ',', '+', '?', '[', ']',
// '#', '/', '$'; never '_', '"', ';', '=', '~' or '%').
//
// The Shift that comes on by itself, at the start, after a space and after
// an opening bracket ("(English)", "[Eng]"), only capitalises letters, as a
// phone's does: had it turned the digits into symbols too, "Retro Rewind
// 6.0" or "2 Player" would need a press of Shift before every number that
// starts a word.
namespace riftwii {

enum class NameShift {
    Off,        // lower case, digits
    Automatic,  // capitals, digits: a word's start (the name's, after a space or a bracket)
    On,         // capitals and symbols: pressed by the player
};

// What the key whose own character is `key` (a capital letter, a digit,
// '-', '\'' or '.') types in `shift`: the letter in that case, or the
// symbol it carries when Shift is on. Any other character comes back as it is.
char name_key_char(char key, NameShift shift);

// Whether `key` is one of the keys that turn into a symbol with Shift on.
bool name_key_has_symbol(char key);

// The Shift after a character was typed, deleted or cleared: on by itself
// when the name is empty or ends in a space, '(' or '[', otherwise off. A
// Shift that was on lasts for one character, as today's does.
NameShift name_shift_for_text(const std::string& text);

// The Shift after the Shift key is pressed: off and automatic go to on
// (so a name can start with "(" in one press), on goes to off.
NameShift name_shift_pressed(NameShift shift);

}  // namespace riftwii
