// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n.hpp"

#include <fstream>
#include <sstream>
#include <vector>

#include "FreeTypeGX.h"
#include "es_po_zst.h"
#include "gettext.h"
#include "it_po_zst.h"
#include "ja_po_zst.h"
#include "ko_po_zst.h"
#include "log.hpp"
#include "pt_po_zst.h"
#include "riftwii/langfile.hpp"
#include "zstd.h"

namespace riftwii::wii {
namespace {

Translations g_catalog;

// wii/lang/*.po, zstd-compressed (Makefile.wii); only the menu's language
// is unpacked.
struct BuiltIn {
    const char* lang;
    const unsigned char* data;
    const unsigned size;
};

const BuiltIn kBuiltIn[] = {
    {"es", es_po_zst, es_po_zst_size},
    {"ja", ja_po_zst, ja_po_zst_size},
    {"pt", pt_po_zst, pt_po_zst_size},
    {"it", it_po_zst, it_po_zst_size},
    {"ko", ko_po_zst, ko_po_zst_size},
};

std::string Unpack(const BuiltIn& b) {
    const unsigned long long size = ZSTD_getFrameContentSize(b.data, b.size);
    if (size == ZSTD_CONTENTSIZE_UNKNOWN || size == ZSTD_CONTENTSIZE_ERROR || size == 0 || size > (1u << 20)) return {};
    std::string text(static_cast<std::size_t>(size), '\0');
    if (ZSTD_decompress(&text[0], text.size(), b.data, b.size) != text.size()) return {};
    return text;
}

}  // namespace

void SetMenuLanguage(const std::string& lang) {
    g_catalog.clear();
    std::size_t built_in = 0;
    for (const BuiltIn& b : kBuiltIn) {
        if (lang == b.lang) built_in = parse_po(Unpack(b), g_catalog);
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

bool MenuLanguageDrawable(const std::string& lang) {
    return lang != "ko" || FontHasChar(0xD55C);  // 한
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
