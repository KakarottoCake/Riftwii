// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/parental.hpp"

#include <cctype>
#include <cstring>
#include <vector>

#include "riftwii/sha256.hpp"

namespace riftwii {
namespace {

constexpr const char* kScheme = "pbkdf2-sha256";
// A Wii gets through 10,000 rounds in well under a second; a record asking
// for far more than that was not written by RiftWii.
constexpr unsigned kMaxRounds = 200000;

// Upper case, without the spaces GameTDB sometimes leaves around a value.
std::string Normalise(const std::string& text) {
    std::string out;
    for (char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (std::isspace(c)) continue;
        out += static_cast<char>(std::toupper(c));
    }
    return out;
}

struct Age {
    const char* system;  // as GameTDB writes it, upper case
    const char* value;
    int age;
};

// Each board's ratings as the youngest age they are for. The ESRB's
// Everyone is placed at 6, as the boards' comparison tables put it beside
// PEGI 7 and USK 6. Korea's and Taiwan's boards under both their names, as
// in ratingmark.cpp.
constexpr Age kAges[] = {
    {"ESRB", "EC", 3},    {"ESRB", "E", 6},       {"ESRB", "E10+", 10},  {"ESRB", "T", 13},
    {"ESRB", "M", 17},    {"ESRB", "AO", 18},
    {"CERO", "A", 0},     {"CERO", "B", 12},      {"CERO", "C", 15},     {"CERO", "D", 17},
    {"CERO", "Z", 18},
    {"GRB", "ALL", 0},    {"GRB", "12", 12},      {"GRB", "15", 15},     {"GRB", "18", 18},
    {"GRB", "19", 18},    {"GRAC", "ALL", 0},     {"GRAC", "12", 12},    {"GRAC", "15", 15},
    {"GRAC", "18", 18},   {"GRAC", "19", 18},
    {"CSRR", "ALL", 0},   {"CSRR", "G", 0},       {"CSRR", "P", 6},      {"CSRR", "PG12", 12},
    {"CSRR", "PG15", 15}, {"CSRR", "R", 18},      {"GSRR", "ALL", 0},    {"GSRR", "G", 0},
    {"GSRR", "P", 6},     {"GSRR", "PG12", 12},   {"GSRR", "PG15", 15},  {"GSRR", "R", 18},
};

// A rating that is an age on its own (PEGI's, the USK's, Australia's
// numbers): 0 to 21, at most two digits.
int PlainAge(const std::string& value) {
    if (value.empty() || value.size() > 2) return -1;
    int age = 0;
    for (char c : value) {
        if (c < '0' || c > '9') return -1;
        age = age * 10 + (c - '0');
    }
    return age <= 21 ? age : -1;
}

std::string ToHex(const std::uint8_t* data, std::size_t size) {
    static const char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        out += kDigits[data[i] >> 4];
        out += kDigits[data[i] & 15];
    }
    return out;
}

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Lower-case hex of exactly `size` bytes.
bool FromHex(const std::string& text, std::uint8_t* out, std::size_t size) {
    if (text.size() != size * 2) return false;
    for (std::size_t i = 0; i < size; ++i) {
        const int high = HexDigit(text[2 * i]);
        const int low = HexDigit(text[2 * i + 1]);
        if (high < 0 || low < 0) return false;
        out[i] = static_cast<std::uint8_t>(high * 16 + low);
    }
    return true;
}

// HMAC-SHA256 (RFC 2104) with a key shorter than a block, its two padded
// key blocks hashed once and copied for every message.
class HmacSha256 {
public:
    HmacSha256(const std::uint8_t* key, std::size_t size) {
        std::uint8_t inner[64], outer[64];
        for (std::size_t i = 0; i < 64; ++i) {
            const std::uint8_t k = i < size ? key[i] : 0;
            inner[i] = static_cast<std::uint8_t>(k ^ 0x36);
            outer[i] = static_cast<std::uint8_t>(k ^ 0x5c);
        }
        inner_.update(inner, sizeof inner);
        outer_.update(outer, sizeof outer);
    }
    Sha256Digest mac(const std::uint8_t* message, std::size_t size) const {
        Sha256 inner = inner_;
        inner.update(message, size);
        const Sha256Digest innerDigest = inner.finish();
        Sha256 outer = outer_;
        outer.update(innerDigest.data(), innerDigest.size());
        return outer.finish();
    }

private:
    Sha256 inner_;
    Sha256 outer_;
};

// PBKDF2's first (and only, for 32 bytes) block: RFC 8018, section 5.2.
Sha256Digest Pbkdf2(const std::string& pin, const std::uint8_t* salt, std::size_t saltSize, unsigned rounds) {
    const HmacSha256 hmac(reinterpret_cast<const std::uint8_t*>(pin.data()), pin.size());
    std::vector<std::uint8_t> first(salt, salt + saltSize);
    const std::uint8_t blockIndex[4] = {0, 0, 0, 1};
    first.insert(first.end(), blockIndex, blockIndex + 4);
    Sha256Digest u = hmac.mac(first.data(), first.size());
    Sha256Digest result = u;
    for (unsigned round = 1; round < rounds; ++round) {
        u = hmac.mac(u.data(), u.size());
        for (std::size_t i = 0; i < result.size(); ++i) result[i] ^= u[i];
    }
    return result;
}

struct Record {
    unsigned rounds = 0;
    std::uint8_t salt[kParentalSaltSize];
    Sha256Digest hash;
};

bool ParseRecord(const std::string& text, Record& out) {
    // scheme$rounds$salt$hash
    std::vector<std::string> parts;
    std::size_t at = 0;
    while (parts.size() < 5) {
        const std::size_t dollar = text.find('$', at);
        parts.push_back(text.substr(at, dollar == std::string::npos ? std::string::npos : dollar - at));
        if (dollar == std::string::npos) break;
        at = dollar + 1;
    }
    if (parts.size() != 4 || parts[0] != kScheme) return false;
    const std::string& rounds = parts[1];
    if (rounds.empty() || rounds.size() > 6) return false;
    unsigned value = 0;
    for (char c : rounds) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<unsigned>(c - '0');
    }
    if (value == 0 || value > kMaxRounds) return false;
    out.rounds = value;
    return FromHex(parts[2], out.salt, kParentalSaltSize) && FromHex(parts[3], out.hash.data(), out.hash.size());
}

}  // namespace

int rating_minimum_age(const std::string& system, const std::string& value) {
    const std::string board = Normalise(system);
    const std::string rating = Normalise(value);
    if (rating.empty()) return -1;
    for (const Age& entry : kAges) {
        if (board == entry.system && rating == entry.value) return entry.age;
    }
    // PEGI, the USK and any other board that rates by age alone.
    return PlainAge(rating);
}

bool parental_age_valid(int age) {
    for (int allowed : kParentalAges) {
        if (age == allowed) return true;
    }
    return false;
}

bool parental_pin_valid(const std::string& pin) {
    if (pin.size() != 4) return false;
    for (char c : pin) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

std::string parental_pin_record(const std::string& pin, const std::uint8_t (&salt)[kParentalSaltSize],
                                 unsigned rounds) {
    if (!parental_pin_valid(pin) || rounds == 0 || rounds > kMaxRounds) return "";
    const Sha256Digest hash = Pbkdf2(pin, salt, kParentalSaltSize, rounds);
    return std::string(kScheme) + "$" + std::to_string(rounds) + "$" + ToHex(salt, kParentalSaltSize) + "$" +
           ToHex(hash.data(), hash.size());
}

bool parental_record_valid(const std::string& record) {
    Record parsed;
    return ParseRecord(record, parsed);
}

bool parental_pin_matches(const std::string& record, const std::string& pin) {
    Record parsed;
    if (!ParseRecord(record, parsed) || !parental_pin_valid(pin)) return false;
    const Sha256Digest hash = Pbkdf2(pin, parsed.salt, kParentalSaltSize, parsed.rounds);
    // Every byte compared, so the time taken says nothing about how close a guess was.
    std::uint8_t difference = 0;
    for (std::size_t i = 0; i < hash.size(); ++i) difference |= static_cast<std::uint8_t>(hash[i] ^ parsed.hash[i]);
    return difference == 0;
}

ParentalVerdict parental_verdict(const ParentalRules& rules, int minimum_age) {
    if (!rules.active) return ParentalVerdict::Allowed;
    const bool known = minimum_age >= 0;
    const bool restricted = known ? minimum_age > rules.highest_age : rules.restrict_unrated;
    if (!restricted) return ParentalVerdict::Allowed;
    return rules.hide ? ParentalVerdict::Hidden : ParentalVerdict::Locked;
}

}  // namespace riftwii
