// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "wiifont.hpp"

#include <gccore.h>
#include <malloc.h>

#include <cstdio>
#include <cstdlib>

#include "riftwii/sysfont.hpp"
#include "skin.hpp"

namespace riftwii::wii {
namespace {

// A whole NAND file into a 32-byte aligned buffer, as ISFS wants: MEM2 for
// the rest of the menu phase when `keep` (the font content), else the heap
// (the caller frees it).
u8* read_nand(const char* path, bool keep, std::size_t& size, std::string& why) {
    const s32 fd = ISFS_Open(path, ISFS_OPEN_READ);
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
