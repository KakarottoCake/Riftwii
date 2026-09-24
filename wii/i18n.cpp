// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n.hpp"

#include <fstream>
#include <sstream>
#include <vector>

#include "es_po.h"
#include "gettext.h"
#include "it_po.h"
#include "ja_po.h"
#include "log.hpp"
#include "pt_po.h"
#include "riftwii/langfile.hpp"

namespace riftwii::wii {
namespace {

Translations g_catalog;

struct BuiltIn {
    const char* lang;
    const unsigned char* data;
    const unsigned size;
};

const BuiltIn kBuiltIn[] = {
    {"es", es_po, es_po_size},
    {"ja", ja_po, ja_po_size},
    {"pt", pt_po, pt_po_size},
    {"it", it_po, it_po_size},
};

}  // namespace

void SetMenuLanguage(const std::string& lang) {
    g_catalog.clear();
    std::size_t built_in = 0;
    for (const BuiltIn& b : kBuiltIn) {
        if (lang == b.lang) built_in = parse_po(std::string(reinterpret_cast<const char*>(b.data), b.size), g_catalog);
    }
    std::size_t from_card = 0;
    const std::string path = "sd:/riftwii/lang/" + lang + ".po";
    std::ifstream in(path, std::ios::binary);
    if (in) {
        std::stringstream text;
        text << in.rdbuf();
        from_card = parse_po(text.str(), g_catalog);
    }
    logf("Language: %s, %u built-in and %u from the card\n", lang.c_str(), static_cast<unsigned>(built_in),
         static_cast<unsigned>(from_card));
}

const char* tr(const char* english) {
    if (!english || g_catalog.empty()) return english;
    const auto it = g_catalog.find(english);
    return it == g_catalog.end() ? english : it->second.c_str();
}

std::string tr(const char* english, std::initializer_list<std::string> args) {
    return fill_placeholders(tr(english), std::vector<std::string>(args));
}

}  // namespace riftwii::wii

// libgui's GuiText translates through this (it replaces libgui's own
// gettext.cpp, which the build leaves out).
const char* gettext(const char* msg) { return riftwii::wii::tr(msg); }
