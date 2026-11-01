// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Parental controls: a 4-digit PIN that locks or hides the games rated for
// players older than an age the parent picks (GameTDB's <rating>, the
// About page's). settings.txt keeps the PIN only as a salted, stretched
// hash ("parental_pin = ..."): a child who reads the card learns nothing
// about a PIN a parent may use elsewhere. Without that line there are no
// parental controls at all, so deleting it is the way back for a parent
// who forgot the PIN.
//
// A 4-digit PIN has 10,000 values. The salt keeps a ready-made table of
// them from working and the stretching makes each guess cost a fraction
// of a second, but someone determined could still try them all on a
// computer: this keeps a PIN private from a casual look, it is not a vault.
namespace riftwii {

// The youngest age a game's rating is for (PEGI 12: 12, ESRB T: 13,
// CERO A: 0), or -1 when the rating is empty, unknown or pending (ESRB
// RP). Case and spaces around either part don't matter.
int rating_minimum_age(const std::string& system, const std::string& value);

// The ages Settings offers as the highest allowed rating, youngest first:
// every age a board's rating can stand for.
constexpr int kParentalAges[] = {3, 6, 7, 10, 12, 13, 15, 16, 17, 18};
constexpr std::size_t kParentalAgeCount = sizeof(kParentalAges) / sizeof(kParentalAges[0]);
bool parental_age_valid(int age);

// Exactly four digits.
bool parental_pin_valid(const std::string& pin);

// PBKDF2-HMAC-SHA256 (RFC 8018) of the PIN with a 16-byte salt, written as
// "pbkdf2-sha256$<rounds>$<salt hex>$<hash hex>". The rounds are kept in
// the line, so a later RiftWii can raise them and still read older ones.
constexpr std::size_t kParentalSaltSize = 16;
constexpr unsigned kParentalRounds = 10000;
std::string parental_pin_record(const std::string& pin, const std::uint8_t (&salt)[kParentalSaltSize],
                                 unsigned rounds = kParentalRounds);
// Whether `record` is one parental_pin_record could have written, with a
// number of rounds a Wii gets through quickly (a record edited to ask for
// billions would freeze the menu).
bool parental_record_valid(const std::string& record);
// Whether `pin` is the one `record` was made from. False for an invalid
// record or PIN. The hashes are compared in full whatever they hold.
bool parental_pin_matches(const std::string& record, const std::string& pin);

// What parental controls do with one game.
enum class ParentalVerdict { Allowed, Locked, Hidden };
struct ParentalRules {
    bool active = false;          // a PIN is set
    bool hide = false;            // hide the games, else lock them (shown, the PIN asked to start one)
    int highest_age = 12;         // games for older players are restricted
    bool restrict_unrated = false;  // a game with no known rating counts as restricted
};
// `minimum_age` is rating_minimum_age's (-1: not known).
ParentalVerdict parental_verdict(const ParentalRules& rules, int minimum_age);

}  // namespace riftwii
