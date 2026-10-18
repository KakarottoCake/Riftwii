// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n.hpp"

#include <vector>

#include "FreeTypeGX.h"
#include "gettext.h"
#include "log.hpp"
#include "riftwii/langfile.hpp"
#include "stash.hpp"
#include "textfile.hpp"
#include "zstd.h"

namespace riftwii::wii {
namespace {

Translations g_catalog;

// wii/lang/*.po, zstd-compressed in the start-up stash as "<lang>.po.zst"
// (Makefile.wii, wii/core/stash.hpp); only the menu's language is unpacked.
const char* const kBuiltIn[] = {"es", "ja", "pt", "it", "fr", "ko"};

std::string Unpack(const char* lang) {
    const std::string name = std::string(lang) + ".po.zst";
    const u8* packed = nullptr;
    u32 packed_size = 0;
    if (!stash::Find(name.c_str(), packed, packed_size)) {
        logf("Language: %s is not in the start-up data\n", name.c_str());
        return {};
    }
    const unsigned long long size = ZSTD_getFrameContentSize(packed, packed_size);
    if (size == ZSTD_CONTENTSIZE_UNKNOWN || size == ZSTD_CONTENTSIZE_ERROR || size == 0 || size > (1u << 20)) return {};
    std::string text(static_cast<std::size_t>(size), '\0');
    if (ZSTD_decompress(&text[0], text.size(), packed, packed_size) != text.size()) return {};
    return text;
}

}  // namespace

void SetMenuLanguage(const std::string& lang) {
    g_catalog.clear();
    std::size_t built_in = 0;
    for (const char* b : kBuiltIn) {
        if (lang == b) built_in = parse_po(Unpack(b), g_catalog);
    }
    std::size_t from_card = 0;
    const std::string path = "sd:/riftwii/lang/" + lang + ".po";
    std::string text;
    if (ReadTextFile(path, text)) from_card = parse_po(text, g_catalog);
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
