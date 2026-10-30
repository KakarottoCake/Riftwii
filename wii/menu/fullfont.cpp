// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fullfont.hpp"

#include <gccore.h>
#include <ogc/lwp_heap.h>
#include <ogc/mutex.h>
#include <sys/stat.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "FreeTypeGX.h"
#include "log.hpp"
#include "riftwii/checkedalloc.hpp"
#include "riftwii/pluginfile.hpp"
#include "skin.hpp"
#include "usbcatalog.hpp"

#include FT_MODULE_H

extern "C" {
extern const FT_Module_Class sfnt_module_class;
extern const FT_Module_Class tt_driver_class;
extern const FT_Module_Class autofit_module_class;
extern const FT_Module_Class ft_smooth_renderer_class;
}

namespace riftwii::wii::fullfont {
namespace {

constexpr std::size_t kPoolBytes = 384 * 1024;
constexpr std::size_t kMaxFontBytes = 16 * 1024 * 1024;
mutex_t g_lock = LWP_MUTEX_NULL;
bool g_enabled = false;
bool g_menu_ended = false;
int g_closed_holds = 0;
bool g_note_due = false;
bool g_note_given = false;
bool g_korean_missing = false;
u8* g_pool_block = nullptr;
heap_cntrl g_pool;
FT_MemoryRec_ g_memory;
FT_Library g_library = nullptr;
FT_Face g_korean = nullptr;

// The last glyph drawn, width-packed, until FreeTypeGX has copied it.
u8* g_glyph_pixels = nullptr;
std::size_t g_glyph_bytes = 0;

struct Lock {
    Lock() {
        if (g_lock == LWP_MUTEX_NULL) LWP_MutexInit(&g_lock, true);
        LWP_MutexLock(g_lock);
    }
    ~Lock() { LWP_MutexUnlock(g_lock); }
};

bool InPool(const void* block) {
    if (g_pool_block == nullptr || block == nullptr) return false;
    const u8* at = static_cast<const u8*>(block);
    return at >= g_pool_block && at < g_pool_block + kPoolBytes;
}

bool PoolReady() {
    if (g_pool_block != nullptr) return true;
    g_pool_block = skin::Mem2Alloc(kPoolBytes);
    if (g_pool_block == nullptr) return false;
    __lwp_heap_init(&g_pool, g_pool_block, kPoolBytes, 32);
    return true;
}

void* PoolAlloc(std::size_t bytes) {
    void* block = PoolReady() ? __lwp_heap_allocate(&g_pool, bytes) : nullptr;
    return block != nullptr ? block : std::malloc(bytes);
}

void PoolFree(void* block) {
    if (block == nullptr) return;
    if (InPool(block)) {
        __lwp_heap_free(&g_pool, block);
    } else {
        std::free(block);
    }
}

void* FtAlloc(FT_Memory, long bytes) { return PoolAlloc(static_cast<std::size_t>(bytes)); }
void FtFree(FT_Memory, void* block) { PoolFree(block); }

void* FtRealloc(FT_Memory, long current, long wanted, void* block) {
    void* moved = PoolAlloc(static_cast<std::size_t>(wanted));
    if (moved == nullptr) return nullptr;
    if (block != nullptr) {
        std::memcpy(moved, block, static_cast<std::size_t>(current < wanted ? current : wanted));
        PoolFree(block);
    }
    return moved;
}

bool LibraryReady() {
    if (g_library != nullptr) return true;
    if (!PoolReady()) return false;
    g_memory.user = nullptr;
    g_memory.alloc = FtAlloc;
    g_memory.free = FtFree;
    g_memory.realloc = FtRealloc;
    if (FT_New_Library(&g_memory, &g_library) != 0) {
        g_library = nullptr;
        return false;
    }
    FT_Add_Module(g_library, &sfnt_module_class);
    FT_Add_Module(g_library, &tt_driver_class);
    FT_Add_Module(g_library, &autofit_module_class);
    FT_Add_Module(g_library, &ft_smooth_renderer_class);
    return true;
}

std::string RunningDolPath() {
    if (__system_argv != nullptr && __system_argv->argvMagic == ARGV_MAGIC && __system_argv->argc > 0 &&
        __system_argv->argv != nullptr && __system_argv->argv[0] != nullptr) {
        return __system_argv->argv[0];
    }
    return "";
}

std::string FontPath(const char* filename) {
    const std::string full = menu_font_path_for(RunningDolPath());
    return full.substr(0, full.rfind('/') + 1) + filename;
}

// TrueType's whole-file checksum includes the directory and outline data.
// FreeType alone can open a file whose later glyphs were damaged in a copy.
bool FontChecksum(const u8* data, std::size_t size) {
    if (size < 12 || data[0] != 0 || data[1] != 1 || data[2] != 0 || data[3] != 0) return false;
    std::uint32_t sum = 0;
    for (std::size_t at = 0; at < size; at += 4) {
        std::uint32_t word = 0;
        for (std::size_t byte = 0; byte < 4; ++byte) {
            word <<= 8;
            if (at + byte < size) word |= data[at + byte];
        }
        sum += word;
    }
    return sum == 0xB1B0AFBA;
}

u8* ReadAt(const std::string& path, std::size_t& size, std::string& why) {
    // RiftWii's USB view also supports NTFS; it is not a stdio mount.
    if (path.compare(0, 4, "usb:") == 0) {
        std::uint64_t length = 0;
        if (!mount_usb_files(why) || !read_usb_range(path, 0, nullptr, 0, length) ||
            length == 0 || length > kMaxFontBytes) {
            why = path + " could not be read from the USB drive: " + why;
            return nullptr;
        }
        size = static_cast<std::size_t>(length);
        u8* data = can_allocate(size) ? skin::Mem2Alloc(size) : nullptr;
        if (data == nullptr) {
            why = "there is no memory for " + path;
            return nullptr;
        }
        if (!read_usb_range(path, 0, data, size, length) || !FontChecksum(data, size)) {
            skin::Mem2Release(data, size);
            why = path + " is incomplete or has a damaged TrueType checksum";
            return nullptr;
        }
        return data;
    }
    FILE* file = std::fopen(path.c_str(), "rb");
    struct stat info;
    if (file == nullptr || fstat(fileno(file), &info) != 0 || info.st_size <= 0 ||
        info.st_size > static_cast<long>(kMaxFontBytes)) {
        if (file != nullptr) std::fclose(file);
        why = path + " is missing, unreadable or larger than 16 MiB";
        return nullptr;
    }
    size = static_cast<std::size_t>(info.st_size);
    u8* data = can_allocate(size) ? skin::Mem2Alloc(size) : nullptr;
    if (data == nullptr) {
        std::fclose(file);
        why = "there is no memory for " + path;
        return nullptr;
    }
    const bool complete = std::fread(data, 1, size, file) == size;
    const bool closed = std::fclose(file) == 0;
    if (!complete || !closed || !FontChecksum(data, size)) {
        skin::Mem2Release(data, size);
        why = path + " is incomplete or has a damaged TrueType checksum";
        return nullptr;
    }
    return data;
}

FT_Face KoreanLocked() {
    if (g_korean != nullptr) return g_korean;
    if (!g_enabled || g_menu_ended || g_closed_holds > 0 || g_korean_missing) return nullptr;
    if (!LibraryReady()) return nullptr;
    const std::string beside = FontPath("korean.ttf");
    const std::string standard = "sd:/apps/riftwii/korean.ttf";
    std::string why;
    for (const std::string& path : {beside, standard}) {
        if (path == standard && beside == standard && !why.empty()) continue;
        std::size_t size = 0;
        u8* data = ReadAt(path, size, why);
        if (data == nullptr) continue;
        if (FT_New_Memory_Face(g_library, data, size, 0, &g_korean) == 0) {
            logf("Menu font: the Korean font was read from %s (%u bytes) and stays in MEM2 during unmounts.\n",
                 path.c_str(), static_cast<unsigned>(size));
            return g_korean;
        }
        g_korean = nullptr;
        skin::Mem2Release(data, size);
        why = path + " could not be read by FreeType";
    }
    g_korean_missing = true;
    logf("Menu font: the Korean font is unavailable because %s.\n", why.c_str());
    return nullptr;
}

bool Glyph(wchar_t charCode, int pixelSize, FtgxBitmapGlyph* out) {
    Lock lock;
    FT_Face face = KoreanLocked();
    const FT_UInt index = face != nullptr ? FT_Get_Char_Index(face, static_cast<FT_ULong>(charCode)) : 0;
    if (index == 0) return false;
    if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixelSize)) != 0) return false;
    if (FT_Load_Glyph(face, index, FT_LOAD_DEFAULT | FT_LOAD_RENDER) != 0) return false;
    const FT_GlyphSlot slot = face->glyph;
    const FT_Bitmap& bitmap = slot->bitmap;
    if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY) return false;
    const std::size_t bytes = static_cast<std::size_t>(bitmap.width) * bitmap.rows;
    if (bytes > g_glyph_bytes) {
        PoolFree(g_glyph_pixels);
        g_glyph_pixels = static_cast<u8*>(PoolAlloc(bytes));
        g_glyph_bytes = g_glyph_pixels != nullptr ? bytes : 0;
        if (g_glyph_pixels == nullptr) return false;
    }
    for (unsigned row = 0; row < bitmap.rows; ++row) {
        std::memcpy(g_glyph_pixels + row * bitmap.width, bitmap.buffer + row * bitmap.pitch, bitmap.width);
    }
    out->pixels = g_glyph_pixels;
    out->width = static_cast<int>(bitmap.width);
    out->rows = static_cast<int>(bitmap.rows);
    out->left = slot->bitmap_left;
    out->top = slot->bitmap_top;
    out->advance = static_cast<int>(slot->advance.x >> 6);
    return true;
}

// GX needs 32-byte alignment; keep the pool allocation before that address.
void* TextureAlloc(std::size_t bytes) {
    Lock lock;
    if (!PoolReady()) return nullptr;
    const std::size_t slack = sizeof(void*) + 32;
    u8* block = static_cast<u8*>(__lwp_heap_allocate(&g_pool, bytes + slack));
    if (block == nullptr) return nullptr;
    const std::uintptr_t first = reinterpret_cast<std::uintptr_t>(block) + sizeof(void*);
    const std::uintptr_t aligned = (first + 31) & ~std::uintptr_t(31);
    reinterpret_cast<void**>(aligned)[-1] = block;
    return reinterpret_cast<void*>(aligned);
}

bool TextureFree(void* texture) {
    Lock lock;
    if (!InPool(texture)) return false;
    __lwp_heap_free(&g_pool, static_cast<void**>(texture)[-1]);
    return true;
}

const FtgxMissingGlyphSource kSource = {Glyph, TextureAlloc, TextureFree};

}  // namespace

bool Load() {
    Lock lock;
    const std::string beside = FullFontPath();
    const std::string standard = "sd:/apps/riftwii/font.ttf";
    std::string why;
    for (const std::string& path : {beside, standard}) {
        if (path == standard && beside == standard && !why.empty()) continue;
        std::size_t size = 0;
        u8* data = ReadAt(path, size, why);
        if (data == nullptr) continue;
        if (InitFreeType(data, size)) {
            g_enabled = true;
            SetMissingGlyphSource(&kSource);
            logf("Menu font: M PLUS 1 SemiBold was read from %s (%u bytes) and stays in MEM2 during unmounts.\n",
                 path.c_str(), static_cast<unsigned>(size));
            return true;
        }
        DeinitFreeType();
        skin::Mem2Release(data, size);
        why = path + " could not be read by FreeType";
    }
    g_note_due = !g_note_given;
    logf("Menu font: RiftWii's font is unavailable because %s. "
         "Copy apps/riftwii/font.ttf from the RiftWii zip to restore RiftWii's font.\n", why.c_str());
    return false;
}

ClosedHold::ClosedHold(const char* why) {
    Lock lock;
    ++g_closed_holds;
    logf("Menu font: card reads are held while %s; loaded fonts remain readable.\n", why);
}

ClosedHold::~ClosedHold() {
    Lock lock;
    --g_closed_holds;
    g_korean_missing = false;
}

void EndMenu() {
    Lock lock;
    g_menu_ended = true;
}

bool TakeMissingNote() {
    Lock lock;
    if (!g_note_due) return false;
    g_note_due = false;
    g_note_given = true;
    return true;
}

bool HasKoreanCharacters() {
    Lock lock;
    FT_Face face = KoreanLocked();
    return face != nullptr && FT_Get_Char_Index(face, 0xD55C) != 0;  // 한
}

std::string FullFontPath() { return FontPath("font.ttf"); }

}  // namespace riftwii::wii::fullfont
