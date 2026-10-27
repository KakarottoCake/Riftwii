// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The age rating marks of the About page (riftwii/ratingmark.hpp).
#include <fstream>
#include <iostream>
#include <string>

#include "riftwii/ratingmark.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) EXPECT_TRUE((a) == (b))

using riftwii::rating_mark_name;

static std::string g_tree;  // the source tree, from the command line

// The mark's picture is in wii/assets, where Makefile.wii embeds it.
static bool HasPicture(const std::string& name) {
    std::ifstream png(g_tree + "/wii/assets/rating_" + name + ".png", std::ios::binary);
    char signature[8] = {};
    png.read(signature, sizeof signature);
    return png && signature[1] == 'P' && signature[2] == 'N' && signature[3] == 'G';
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: ratingmark_tests <source tree>" << std::endl;
        return 1;
    }
    g_tree = argv[1];
    // Every value GameTDB's Wii database has, each to a mark with a picture.
    const char* const seen[][3] = {
        {"ESRB", "EC", "esrb_ec"},  {"ESRB", "E", "esrb_e"},     {"ESRB", "E10+", "esrb_e10"},
        {"ESRB", "T", "esrb_t"},    {"ESRB", "M", "esrb_m"},     {"ESRB", "AO", "esrb_ao"},
        {"ESRB", "RP", "esrb_rp"},  {"PEGI", "3", "pegi_3"},     {"PEGI", "6", "pegi_6"},
        {"PEGI", "7", "pegi_7"},    {"PEGI", "12", "pegi_12"},   {"PEGI", "16", "pegi_16"},
        {"PEGI", "18", "pegi_18"},  {"CERO", "A", "cero_a"},     {"CERO", "B", "cero_b"},
        {"CERO", "C", "cero_c"},    {"CERO", "D", "cero_d"},     {"CERO", "Z", "cero_z"},
        {"GRB", "ALL", "grac_all"}, {"GRB", "12", "grac_12"},    {"GRB", "15", "grac_15"},
        {"GRB", "18", "grac_19"},   {"CSRR", "ALL", "gsrr_g"},   {"GSRR", "PG15", "gsrr_pg15"},
        {"GSRR", "R", "gsrr_r"},    {"GSRR", "P", "gsrr_p"},     {"GSRR", "PG12", "gsrr_pg12"},
    };
    for (const auto& one : seen) {
        EXPECT_EQ(rating_mark_name(one[0], one[1]), one[2]);
        EXPECT_TRUE(HasPicture(one[2]));
    }
    // Case and stray spaces don't matter.
    EXPECT_EQ(rating_mark_name("esrb", "e10+"), "esrb_e10");
    EXPECT_EQ(rating_mark_name(" Pegi ", " 12 "), "pegi_12");
    EXPECT_EQ(rating_mark_name("grac", "all"), "grac_all");
    // No mark: an empty part, a value the board doesn't have (one ESRB "12"
    // and one PEGI "15" are in GameTDB), an unknown board.
    EXPECT_EQ(rating_mark_name("", ""), "");
    EXPECT_EQ(rating_mark_name("ESRB", ""), "");
    EXPECT_EQ(rating_mark_name("", "E"), "");
    EXPECT_EQ(rating_mark_name("ESRB", "12"), "");
    EXPECT_EQ(rating_mark_name("PEGI", "15"), "");
    EXPECT_EQ(rating_mark_name("USK", "12"), "");
    EXPECT_EQ(rating_mark_name("ESRB", "E 10+"), "esrb_e10");

    if (g_failures == 0) std::cout << "ratingmark tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
