// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fullfont.hpp"

#include <gccore.h>
#include <ogc/lwp_heap.h>
#include <ogc/mutex.h>

#include <dirent.h>
#include <sys/stat.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "FreeTypeGX.h"
#include "log.hpp"
#include "riftwii/pluginfile.hpp"
#include "skin.hpp"

#include FT_MODULE_H

// The same four FreeType modules FreeTypeGX.cpp adds for the core font:
// the full font is the same kind of TrueType font without hinting.
extern "C" {
extern const FT_Module_Class sfnt_module_class;
extern const FT_Module_Class tt_driver_class;
extern const FT_Module_Class autofit_module_class;
extern const FT_Module_Class ft_smooth_renderer_class;
}

namespace riftwii::wii::fullfont {
namespace {

// The full font's own memory in MEM2: FreeType's face for it (about 40 KB
// once open, about 105 KB at its peak while glyphs are drawn, measured
// with FreeType on a computer), the file's stdio buffer and the textures
// of the glyphs drawn from it (288 bytes for a kanji at 24 px). What does
// not fit goes to the heap.
constexpr std::size_t kPoolBytes = 384 * 1024;
// Small enough that one read is one or two of the card's sectors runs, as
// FreeType reads a glyph's few hundred bytes at a time.
constexpr std::size_t kFileBufferBytes = 4096;
constexpr const char* kDefaultPath = "sd:/apps/riftwii/font.ttf";

mutex_t g_lock = LWP_MUTEX_NULL;

bool g_enabled = false;
bool g_menu_ended = false;
int g_closed_holds = 0;
// Looked for and not found (or not readable) since the last Close.
bool g_missing = false;
bool g_missing_logged = false;
bool g_note_due = false;
bool g_note_given = false;

u8* g_pool_block = nullptr;
heap_cntrl g_pool;

FT_MemoryRec_ g_memory;
FT_Library g_library = nullptr;
FT_Face g_face = nullptr;
FT_StreamRec g_stream;
FILE* g_file = nullptr;
char* g_file_buffer = nullptr;
unsigned long g_file_position = 0;

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

// Taken the first time the file is there, so a card without it costs no
// MEM2. Kept for the menu phase: Mem2Alloc never gives memory back.
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

// FreeType's allocator for the full font's library: the pool first.
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

// FreeType's stream: reads at `offset`; with `count` 0 only seeks, and
// then answers 0 for success.
unsigned long StreamRead(FT_Stream, unsigned long offset, unsigned char* buffer, unsigned long count) {
    if (g_file == nullptr) return count == 0 ? 1 : 0;
    if (offset != g_file_position) {
        if (std::fseek(g_file, static_cast<long>(offset), SEEK_SET) != 0) return count == 0 ? 1 : 0;
        g_file_position = offset;
    }
    if (count == 0) return 0;
    const std::size_t got = std::fread(buffer, 1, count, g_file);
    g_file_position += got;
    return got;
}

// Called by FT_Done_Face, and by FT_Open_Face when it fails.
void StreamClose(FT_Stream) {
    if (g_file != nullptr) std::fclose(g_file);
    g_file = nullptr;
    PoolFree(g_file_buffer);
    g_file_buffer = nullptr;
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

// Whether the drive `path` is on can be read at all ("sd:/" mounted): a
// file not found on an unmounted card is not missing.
bool DriveReadable(const std::string& path) {
    const std::size_t colon = path.find(":/");
    if (colon == std::string::npos) return false;
    DIR* root = opendir(path.substr(0, colon + 2).c_str());
    if (root == nullptr) return false;
    closedir(root);
    return true;
}

void CloseLocked(const char* why) {
    if (g_face != nullptr) {
        FT_Done_Face(g_face);  // closes the file (StreamClose)
        logf("Menu font: the full font closed (%s); opened again when a character needs it\n", why);
    }
    g_face = nullptr;
    StreamClose(nullptr);
    g_missing = false;
}

// Not to be had until the next Close: logged once a session, and the
// player told once when copying the file would help.
void NoteMissing(const std::string& why, bool copying_helps) {
    g_missing = true;
    if (!g_missing_logged) {
        g_missing_logged = true;
        logf("Menu font: %s, so characters the core font lacks (most kanji) are not shown%s\n", why.c_str(),
             copying_helps ? ". Copy apps/riftwii/font.ttf from the RiftWii zip to the SD card." : "");
    }
    if (copying_helps && !g_note_given) g_note_due = true;
}

bool FileThere(const std::string& path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0;
}

// FreeType's stream on the file at `path`, and the face on it; nullptr,
// with `why`, when it cannot be opened or read as a font.
FT_Face OpenAt(const std::string& path, std::string& why) {
    FILE* file = std::fopen(path.c_str(), "rb");
    struct stat info;
    if (file == nullptr || fstat(fileno(file), &info) != 0 || info.st_size <= 0) {
        if (file != nullptr) std::fclose(file);
        why = path + " could not be opened";
        return nullptr;
    }
    g_file = file;
    g_file_position = 0;
    g_file_buffer = static_cast<char*>(PoolAlloc(kFileBufferBytes));
    if (g_file_buffer != nullptr) setvbuf(g_file, g_file_buffer, _IOFBF, kFileBufferBytes);
    std::memset(&g_stream, 0, sizeof(g_stream));
    g_stream.size = static_cast<unsigned long>(info.st_size);
    g_stream.read = StreamRead;
    g_stream.close = StreamClose;
    FT_Open_Args args;
    std::memset(&args, 0, sizeof(args));
    args.flags = FT_OPEN_STREAM;
    args.stream = &g_stream;
    const FT_Error error = FT_Open_Face(g_library, &args, 0, &g_face);
    if (error != 0) {
        g_face = nullptr;
        StreamClose(nullptr);  // FreeType has closed it already; harmless again
        why = path + " could not be read as a font (FreeType error " + std::to_string(error) + ")";
        return nullptr;
    }
    // Once a session in the usual menu; again after each unmount.
    logf("Menu font: the full font opened from %s (%ld bytes, %ld glyphs) for characters the core font "
         "lacks; read a glyph at a time\n", path.c_str(), static_cast<long>(info.st_size), g_face->num_glyphs);
    return g_face;
}

// The face, opening it if need be; nullptr when it can't be had now.
FT_Face FaceLocked() {
    if (g_face != nullptr) return g_face;
    if (!g_enabled || g_menu_ended || g_closed_holds > 0 || g_missing) return nullptr;
    const std::string beside = FullFontPath();
    if (!DriveReadable(beside) && !DriveReadable(kDefaultPath)) return nullptr;  // tried again later
    // Beside the running boot.dol, then where the release zip puts it (a
    // DOL started by a loader that passes no path, or a broken copy beside
    // it): missing only when neither opens.
    std::vector<std::string> candidates;
    if (FileThere(beside)) candidates.push_back(beside);
    if (beside != kDefaultPath && FileThere(kDefaultPath)) candidates.push_back(kDefaultPath);
    if (candidates.empty()) {
        NoteMissing(beside + " is missing", true);
        return nullptr;
    }
    // The MEM2 pool and the library only once a file is there.
    if (!LibraryReady()) {
        NoteMissing("there was no memory for FreeType", false);
        return nullptr;
    }
    std::string why;
    for (const std::string& path : candidates) {
        std::string failed;
        if (FT_Face face = OpenAt(path, failed)) return face;
        why += (why.empty() ? "" : "; ") + failed;
    }
    NoteMissing(why, true);
    return nullptr;
}

// FreeTypeGX's missing-glyph source: the full font's glyph for
// `charCode` at `pixelSize`, as 8-bit coverage.
bool Glyph(wchar_t charCode, int pixelSize, FtgxBitmapGlyph* out) {
    Lock lock;
    FT_Face face = FaceLocked();
    if (face == nullptr) return false;
    const FT_UInt index = FT_Get_Char_Index(face, static_cast<FT_ULong>(charCode));
    if (index == 0) return false;
    if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixelSize)) != 0) return false;
    if (FT_Load_Glyph(face, index, FT_LOAD_DEFAULT | FT_LOAD_RENDER) != 0) return false;
    const FT_GlyphSlot slot = face->glyph;
    const FT_Bitmap& bitmap = slot->bitmap;
    if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY) return false;
    // Copied out: the slot's bitmap goes with the face, which another
    // thread may close as soon as the lock is let go.
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

// A glyph texture: 32-byte aligned for GX, the block's own start kept in
// the word before it.
void* TextureAlloc(std::size_t bytes) {
    Lock lock;
    if (!PoolReady()) return nullptr;
    // Room for the start's word and up to 31 bytes of alignment.
    const std::size_t slack = sizeof(void*) + 32;
    u8* block = static_cast<u8*>(__lwp_heap_allocate(&g_pool, bytes + slack));
    if (block == nullptr) return nullptr;  // FreeTypeGX takes the heap's
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

void Enable() {
    {
        Lock lock;
        g_enabled = true;
    }
    SetMissingGlyphSource(&kSource);
}

ClosedHold::ClosedHold(const char* why) {
    Lock lock;
    ++g_closed_holds;
    CloseLocked(why);
}

ClosedHold::~ClosedHold() {
    Lock lock;
    --g_closed_holds;
    g_missing = false;  // a remounted card or an update may have brought it
}

void EndMenu() {
    Lock lock;
    CloseLocked("the menu closed");
    g_menu_ended = true;
}

bool TakeMissingNote() {
    Lock lock;
    if (!g_note_due) return false;
    g_note_due = false;
    g_note_given = true;
    return true;
}

std::string FullFontPath() { return menu_font_path_for(RunningDolPath()); }

}  // namespace riftwii::wii::fullfont
