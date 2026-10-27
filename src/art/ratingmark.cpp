// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ratingmark.hpp"

#include <cctype>
#include <cstring>

namespace riftwii {
namespace {

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

struct Mark {
    const char* system;  // as GameTDB writes it, upper case
    const char* value;
    const char* name;    // wii/assets/rating_<name>.png
};

// Every value seen in GameTDB's Wii database, plus the boards' other
// current ratings. Korea's and Taiwan's boards are listed under both their
// old names (GameTDB's) and their new ones.
constexpr Mark kMarks[] = {
    {"ESRB", "EC", "esrb_ec"},
    {"ESRB", "E", "esrb_e"},
    {"ESRB", "E10+", "esrb_e10"},
    {"ESRB", "T", "esrb_t"},
    {"ESRB", "M", "esrb_m"},
    {"ESRB", "AO", "esrb_ao"},
    {"ESRB", "RP", "esrb_rp"},
    {"PEGI", "3", "pegi_3"},
    {"PEGI", "6", "pegi_6"},  // Portugal's own age, with PEGI's mark
    {"PEGI", "7", "pegi_7"},
    {"PEGI", "12", "pegi_12"},
    {"PEGI", "16", "pegi_16"},
    {"PEGI", "18", "pegi_18"},
    {"CERO", "A", "cero_a"},
    {"CERO", "B", "cero_b"},
    {"CERO", "C", "cero_c"},
    {"CERO", "D", "cero_d"},
    {"CERO", "Z", "cero_z"},
    {"GRB", "ALL", "grac_all"},
    {"GRB", "12", "grac_12"},
    {"GRB", "15", "grac_15"},
    {"GRB", "18", "grac_19"},  // the adults-only rating, now marked 19
    {"GRB", "19", "grac_19"},
    {"GRAC", "ALL", "grac_all"},
    {"GRAC", "12", "grac_12"},
    {"GRAC", "15", "grac_15"},
    {"GRAC", "18", "grac_19"},
    {"GRAC", "19", "grac_19"},
    {"CSRR", "ALL", "gsrr_g"},  // the general audience rating
    {"CSRR", "G", "gsrr_g"},
    {"CSRR", "P", "gsrr_p"},
    {"CSRR", "PG12", "gsrr_pg12"},
    {"CSRR", "PG15", "gsrr_pg15"},
    {"CSRR", "R", "gsrr_r"},
    {"GSRR", "ALL", "gsrr_g"},
    {"GSRR", "G", "gsrr_g"},
    {"GSRR", "P", "gsrr_p"},
    {"GSRR", "PG12", "gsrr_pg12"},
    {"GSRR", "PG15", "gsrr_pg15"},
    {"GSRR", "R", "gsrr_r"},
};

}  // namespace

std::string rating_mark_name(const std::string& system, const std::string& value) {
    const std::string board = Normalise(system);
    const std::string rating = Normalise(value);
    if (board.empty() || rating.empty()) return "";
    for (const Mark& mark : kMarks) {
        if (board == mark.system && rating == mark.value) return mark.name;
    }
    return "";
}

}  // namespace riftwii
