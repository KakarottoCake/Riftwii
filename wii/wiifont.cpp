// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2012-2025 Dimok
// SPDX-FileCopyrightText: 2012-2025 giantpune
// SPDX-FileCopyrightText: 2012-2025 blackb0x
// SPDX-FileCopyrightText: USB Loader GX contributors <https://github.com/wiidev/usbloadergx>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "wiifont.hpp"

#include <gccore.h>
#include <malloc.h>

#include <cstdio>
#include <cstdlib>

#include "FreeTypeGX.h"
#include "boot.hpp"
#include "log.hpp"
#include "riftwii/brfnt.hpp"
#include "riftwii/sysfont.hpp"
#include "skin.hpp"

namespace riftwii::wii {
namespace {

// A whole NAND file into a 32-byte aligned buffer, as ISFS wants: MEM2 for
// the rest of the menu phase when `keep` (the font content), else the heap
// (the caller frees it).
u8* read_nand(const char* path, bool keep, std::size_t& size, std::string& why) {
    s32 fd = ISFS_Open(path, ISFS_OPEN_READ);
    // The shared contents are the Wii Menu's: the Homebrew Channel's IOS
    // refuses them to an app (-102) until its permission check is opened
    // (boot.hpp; Dolphin never checks, a Wii and a vWii do).
    if (fd == -102 && open_nand_permissions("Menu font")) fd = ISFS_Open(path, ISFS_OPEN_READ);
    if (fd < 0) {
        why = std::string(path) + " could not be opened (" + std::to_string(fd) + ")";
        return nullptr;
    }
    static fstats stats ATTRIBUTE_ALIGN(32);
    size = ISFS_GetFileStats(fd, &stats) < 0 ? 0 : stats.file_length;
    if (size == 0 || size > 16u * 1024 * 1024) {
        ISFS_Close(fd);
        why = std::string(path) + " has no size we can read";
        return nullptr;
    }
    u8* buffer = keep ? skin::Mem2Alloc(size) : static_cast<u8*>(memalign(32, (size + 31) & ~std::size_t(31)));
    if (buffer == nullptr) {
        ISFS_Close(fd);
        why = "no room for " + std::to_string(size) + " bytes";
        return nullptr;
    }
    const s32 got = ISFS_Read(fd, buffer, size);
    ISFS_Close(fd);
    if (got != static_cast<s32>(size)) {
        why = std::string(path) + " read " + std::to_string(got) + " of " + std::to_string(size) + " bytes";
        if (!keep) std::free(buffer);
        return nullptr;
    }
    return buffer;
}

// The Wii Menu's bitmap font, once read: FreeTypeGX takes its glyphs from it.
BitmapFont g_bitmap;
BitmapFont::Glyph g_glyph;  // the last one, until FreeTypeGX has copied it
constexpr unsigned kSheetSlots = 8;  // unpacked sheets kept (64 KiB each in the Wii Menu's)

bool bitmap_glyph(wchar_t ch, int pixel_size, FtgxBitmapGlyph* out) {
    if (!g_bitmap.render(static_cast<std::uint32_t>(ch), pixel_size, g_glyph)) return false;
    *out = FtgxBitmapGlyph{g_glyph.pixels.data(), g_glyph.width, g_glyph.rows, g_glyph.left, g_glyph.top,
                           g_glyph.advance};
    return true;
}

// wbf1.brfna, from the shared content `name` lists: the Wii Menu's text.
void load_bitmap_font(const std::uint8_t* map, std::size_t map_size) {
    const std::string name = shared_content_name(map, map_size, kWiiBitmapFontHash);
    if (name.empty()) {
        logf("Menu font: content.map lists no bitmap font; the TrueType one alone\n");
        return;
    }
    char path[32];
    std::snprintf(path, sizeof path, "/shared1/%s.app", name.c_str());
    std::size_t size = 0, at = 0, length = 0, sheet = 0;
    std::string why;
    u8* app = read_nand(path, true, size, why);
    if (!app) {
        logf("Menu font: the bitmap font was not read (%s)\n", why.c_str());
        return;
    }
    if (!u8_find_file(app, size, "wbf1.brfna", at, length) || !BitmapFont::sheet_size(app + at, length, sheet) ||
        sheet > 1024 * 1024) {
        logf("Menu font: %s holds no wbf1.brfna we can read\n", path);
        return;
    }
    u8* slots = skin::Mem2Alloc(sheet * kSheetSlots);
    if (!slots || !g_bitmap.load(app + at, length, slots, sheet, kSheetSlots, why)) {
        logf("Menu font: the bitmap font was not used (%s)\n", slots ? why.c_str() : "no MEM2 for its sheets");
        return;
    }
    SetBitmapGlyphSource(&bitmap_glyph);
    logf("Menu font: the Wii Menu's bitmap font (wbf1, %u characters), the TrueType one for the rest\n",
         static_cast<unsigned>(g_bitmap.characters()));
}

}  // namespace

u8* LoadWiiMenuFont(std::size_t& size, std::string& why) {
    if (ISFS_Initialize() < 0) {
        why = "the NAND could not be opened";
        return nullptr;
    }
    u8* font = nullptr;
    std::size_t map_size = 0;
    if (u8* map = read_nand("/shared1/content.map", false, map_size, why)) {
        // A Korean Wii has its own font; a Wii set to Korean looks for it first.
        const bool korean = CONF_GetLanguage() == CONF_LANG_KOREAN;
        std::string name = shared_content_name(map, map_size, korean ? kWiiKoreanFontHash : kWiiFontHash);
        if (name.empty()) name = shared_content_name(map, map_size, korean ? kWiiFontHash : kWiiKoreanFontHash);
        // The Wii Menu writes with its bitmap font: Korean is not in it, the
        // TrueType font has it (the bitmap one's missing characters go there).
        if (!name.empty()) load_bitmap_font(map, map_size);
        std::free(map);
        if (name.empty()) {
            why = "content.map lists no Wii Menu font";
        } else {
            char path[32];
            std::snprintf(path, sizeof path, "/shared1/%s.app", name.c_str());
            std::size_t app_size = 0, at = 0;
            if (u8* app = read_nand(path, true, app_size, why)) {
                if (u8_find_font(app, app_size, at, size))
                    font = app + at;
                else
                    why = std::string(path) + " holds no font";
            }
        }
    }
    ISFS_Deinitialize();
    return font;
}

}  // namespace riftwii::wii
