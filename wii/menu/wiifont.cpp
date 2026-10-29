// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2012-2025 Dimok
// SPDX-FileCopyrightText: 2012-2025 giantpune
// SPDX-FileCopyrightText: 2012-2025 blackb0x
// SPDX-FileCopyrightText: USB Loader GX contributors <https://github.com/wiidev/usbloadergx>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "wiifont.hpp"

#include <gccore.h>
#include <malloc.h>

#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>

#include "FreeTypeGX.h"
#include "loadersettings.hpp"
#include "log.hpp"
#include "riftwii/brfnt.hpp"
#include "riftwii/checkedalloc.hpp"
#include "riftwii/sysfont.hpp"
#include "skin.hpp"
#include "wiimenucontent.hpp"

namespace riftwii::wii {
namespace {

// Font archives stay in MEM2 while FreeTypeGX refers to their bytes.
u8* read_content(const WiiMenuContentReader& reader, const TmdContent& content,
                 std::size_t& size, std::string& why) {
    constexpr std::size_t kMaxContent = 16u * 1024 * 1024;
    if (content.size == 0 || content.size > kMaxContent) {
        why = "the System Menu font content has no size we can read";
        return nullptr;
    }
    size = static_cast<std::size_t>(content.size);
    u8* buffer = can_allocate(size) ? skin::Mem2Alloc(size) : nullptr;
    if (buffer == nullptr) {
        why = "no room for " + std::to_string(size) + " bytes";
        return nullptr;
    }
    if (!reader.Read(content, buffer, kMaxContent, why)) {
        skin::Mem2Release(buffer, size);
        return nullptr;
    }
    return buffer;
}

// The SD card's copy of the shared contents the fonts are in.
constexpr const char* kCacheDir = "sd:/riftwii/fontcache";
constexpr const char* kCacheFont = "sd:/riftwii/fontcache/font.app";
constexpr const char* kCacheKorean = "sd:/riftwii/fontcache/font-korean.app";
constexpr const char* kCacheBitmap = "sd:/riftwii/fontcache/bitmap.app";

// A file of the copy into MEM2 for the rest of the menu phase.
u8* read_cache(const char* path, std::size_t& size) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return nullptr;
    u8* buffer = nullptr;
    if (std::fseek(f, 0, SEEK_END) == 0) {
        const long length = std::ftell(f);
        if (length > 0 && length <= 16L * 1024 * 1024 && std::fseek(f, 0, SEEK_SET) == 0) {
            size = static_cast<std::size_t>(length);
            buffer = skin::Mem2Alloc(size);
            if (buffer && std::fread(buffer, 1, size, f) != size) buffer = nullptr;
        }
    }
    std::fclose(f);
    return buffer;
}

// A content read from the NAND into the copy: written whole beside it,
// then renamed, so a start that stops halfway leaves no broken copy.
void save_cache(const char* path, const u8* data, std::size_t size) {
    mkdir(kCacheDir, 0777);
    const std::string temp = std::string(path) + ".part";
    FILE* f = std::fopen(temp.c_str(), "wb");
    if (!f) return;
    const bool ok = std::fwrite(data, 1, size, f) == size;
    if (std::fclose(f) != 0 || !ok) {
        std::remove(temp.c_str());
        logf("Menu font: %s could not be written\n", path);
        return;
    }
    std::remove(path);
    if (std::rename(temp.c_str(), path) == 0) logf("Menu font: copied to %s\n", path);
}

void drop_cache() {
    std::remove(kCacheFont);
    std::remove(kCacheKorean);
    std::remove(kCacheBitmap);
}

// The Wii Menu's bitmap font, once read: FreeTypeGX takes its glyphs from it.
BitmapFont g_bitmap;
BitmapFont::Glyph g_glyph;  // the last one, until FreeTypeGX has copied it
constexpr unsigned kSheetSlots = 8;  // unpacked sheets kept (64 KiB each in the Wii Menu's)

// At the screen's pixels (`pixels_x` by `pixels_y` to a menu unit), so a
// 16:9 menu draws it as sharp as a 4:3 one.
bool bitmap_glyph(wchar_t ch, int pixel_size, float pixels_x, float pixels_y, FtgxBitmapGlyph* out) {
    if (!g_bitmap.render(static_cast<std::uint32_t>(ch), pixel_size, g_glyph, pixels_x, pixels_y)) return false;
    *out = FtgxBitmapGlyph{g_glyph.pixels.data(), g_glyph.width, g_glyph.rows, g_glyph.left, g_glyph.top,
                           g_glyph.advance};
    return true;
}

// wbf1.brfna out of the shared content holding it (`path`, for the log).
bool use_bitmap_app(const u8* app, std::size_t size, const char* path) {
    std::size_t at = 0, length = 0, sheet = 0;
    std::string why;
    if (!u8_find_file(app, size, "wbf1.brfna", at, length) || !BitmapFont::sheet_size(app + at, length, sheet) ||
        sheet > 1024 * 1024) {
        logf("Menu font: %s holds no wbf1.brfna we can read\n", path);
        return false;
    }
    u8* slots = skin::Mem2Alloc(sheet * kSheetSlots);
    if (!slots || !g_bitmap.load(app + at, length, slots, sheet, kSheetSlots, why)) {
        logf("Menu font: the bitmap font was not used (%s)\n", slots ? why.c_str() : "no MEM2 for its sheets");
        return false;
    }
    SetBitmapGlyphSource(&bitmap_glyph);
    logf("Menu font: the Wii Menu's bitmap font (wbf1, %u characters), the TrueType one for the rest\n",
         static_cast<unsigned>(g_bitmap.characters()));
    return true;
}

// Find the shared font by hash in the System Menu's own content records.
void load_bitmap_font(const WiiMenuContentReader& reader) {
    const TmdContent* content = reader.FindHash(kWiiBitmapFontHash);
    if (!content) {
        logf("Menu font: the System Menu lists no bitmap font; the TrueType one alone\n");
        return;
    }
    const std::string path = "ES content index " + std::to_string(content->index);
    std::size_t size = 0;
    std::string why;
    u8* app = read_content(reader, *content, size, why);
    if (!app) {
        logf("Menu font: the bitmap font was not read (%s)\n", why.c_str());
        return;
    }
    if (use_bitmap_app(app, size, path.c_str())) {
        logf("Menu font: read the Wii Menu's bitmap font through ES\n");
        save_cache(kCacheBitmap, app, size);
    }
}

// The copy on the SD card, when there is one that reads.
u8* load_cached(bool korean, std::size_t& size) {
    std::size_t app_size = 0, at = 0;
    const char* path = korean ? kCacheKorean : kCacheFont;
    u8* app = read_cache(path, app_size);
    if (!app) {
        path = korean ? kCacheFont : kCacheKorean;
        app = read_cache(path, app_size);
    }
    if (!app) return nullptr;
    if (!u8_find_font(app, app_size, at, size)) {
        logf("Menu font: %s holds no font; read from the NAND again\n", path);
        drop_cache();
        return nullptr;
    }
    logf("Menu font: the SD card's copy (%s)\n", path);
    std::size_t bitmap_size = 0;
    if (u8* bitmap = read_cache(kCacheBitmap, bitmap_size)) use_bitmap_app(bitmap, bitmap_size, kCacheBitmap);
    return app + at;
}

}  // namespace

bool WiiMenuFontCached() {
    struct stat st;
    return stat(kCacheFont, &st) == 0 || stat(kCacheKorean, &st) == 0;
}

u8* LoadWiiMenuFont(std::size_t& size, std::string& why) {
    // Korean, the console's or the menu's own: the font with Hangul first
    // (RiftWii's own font has none).
    const bool korean = CONF_GetLanguage() == CONF_LANG_KOREAN || MenuLanguage() == "ko";
    if (u8* cached = load_cached(korean, size)) return cached;
    WiiMenuContentReader reader("Menu font");
    if (!reader.Open(why)) return nullptr;
    bool korean_font = korean;
    const TmdContent* content = reader.FindHash(korean ? kWiiKoreanFontHash : kWiiFontHash);
    if (!content) {
        korean_font = !korean;
        content = reader.FindHash(korean ? kWiiFontHash : kWiiKoreanFontHash);
    }
    if (!content) {
        why = "the System Menu lists no Wii Menu font";
        return nullptr;
    }
    // Missing bitmap characters (including Hangul) use the TrueType font.
    load_bitmap_font(reader);
    std::size_t app_size = 0, at = 0;
    u8* app = read_content(reader, *content, app_size, why);
    if (!app) return nullptr;
    if (!u8_find_font(app, app_size, at, size)) {
        why = "ES content index " + std::to_string(content->index) + " holds no font";
        skin::Mem2Release(app, app_size);
        return nullptr;
    }
    logf("Menu font: read the Wii Menu's TrueType font through ES\n");
    save_cache(korean_font ? kCacheKorean : kCacheFont, app, app_size);
    return app + at;
}

}  // namespace riftwii::wii
