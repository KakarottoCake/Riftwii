// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "plugins.hpp"

#include <dirent.h>
#include <fat.h>
#include <gccore.h>
#include <malloc.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/machine/processor.h>
#include <ogc/system.h>
#include <sys/stat.h>

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "d2xsd.hpp"
#include "i18n.hpp"
#include "loadersettings.hpp"
#include "log.hpp"
#include "riftwii/sha256.hpp"
#include "sdfile.hpp"
#include "sdio.hpp"
#include "zstd.h"

// libogc's SRAM copies (libogc/system.c), not in its headers: read_sram.
extern "C" {
syssram* __SYS_LockSram(void);
u32 __SYS_UnlockSram(u32 write);
syssramex* __SYS_LockSramEx(void);
u32 __SYS_UnlockSramEx(u32 write);
}

namespace riftwii::wii::plugin {
namespace {

// Kept in the DOL for tools/make_plugin.py check (CI and the build): the
// plugins next to a DOL must carry exactly this stamp. The stamp itself is
// read from it (HostBuildStamp), so the text can't be left out of the link.
constexpr char kStampMarker[] = "RiftWii plugin build stamp: " RIFTWII_BUILD_STAMP;
constexpr std::size_t kStampMarkerPrefix = sizeof("RiftWii plugin build stamp: ") - 1;
const char* BuildStamp() { return kStampMarker + kStampMarkerPrefix; }

// The header check (src/data/pluginfile.cpp) keeps kPluginTableBytes of
// the table inside the image; this is every byte of it the loader reads.
static_assert(sizeof(rwp_plugin) == kPluginTableBytes, "pluginfile.hpp's table size is not plugin_api.h's");

constexpr const char* kTestFile = "sd:/riftwii/plugin_test.txt";
constexpr const char* kDefaultDirectory = "sd:/apps/riftwii/plugins/";
constexpr std::size_t kMaxPlugins = 8;
constexpr std::size_t kMaxExtraRanges = 4;
constexpr std::size_t kRangeNameBytes = 24;

struct ExtraRange {
    char name[kRangeNameBytes];
    u32 start;
    u32 bytes;
};

// One plugin RiftWii was asked for this run: loaded, or refused (and not
// asked for again, except a missing one, which the update may still bring).
struct Slot {
    bool used;
    bool loaded;
    char name[kPluginNameBytes + 1];
    char stamp[kPluginStampBytes + 1];
    u32 version;
    u32 base;          // in the arena
    u32 memory_bytes;  // image, .bss and state
    u32 image_bytes;
    u32 shutdown;      // address, 0 for none
    const rwp_plugin* table;
    PluginRefusal refusal;
    ExtraRange extra[kMaxExtraRanges];
    std::size_t extra_count;
    // A refusal's two sides (versions or stamps), for its message again.
    std::string theirs;
    std::string ours;
    bool no_file;  // refused because this version's file is not there
};

Slot g_slots[kMaxPlugins];
// The arena: [g_arena_lo, g_arena_hi), taken off the top of MEM2's heap
// at the first load; g_arena_next is where the next block goes.
u32 g_arena_lo = 0;
u32 g_arena_hi = 0;
u32 g_arena_next = 0;
// The plugin whose code is running (init, probe, shutdown): its log lines
// and crash ranges are its own.
Slot* g_current = nullptr;

u32 Align32(u32 value) { return (value + 31u) & ~31u; }

Slot* Find(const char* name) {
    for (Slot& slot : g_slots) {
        if (slot.used && std::strcmp(slot.name, name) == 0) return &slot;
    }
    return nullptr;
}

Slot* NewSlot(const char* name) {
    for (Slot& slot : g_slots) {
        if (slot.used) continue;
        slot = Slot{};
        slot.used = true;
        std::snprintf(slot.name, sizeof(slot.name), "%s", name);
        return &slot;
    }
    return nullptr;
}

bool ArenaInUse() {
    for (const Slot& slot : g_slots) {
        if (slot.loaded) return true;
    }
    return false;
}

// The arena from the top of the loader's MEM2 (lowering arena 2's high
// end, as libogc's IPC heap is taken at start), so it never splits the
// heap and the menu's textures growing up from the low end. Far from the
// start-up stash's copy and the restart snapshot at MEM2's bottom
// (docs/MEMORY.md).
bool ReserveArena() {
    if (g_arena_hi != 0) return true;
    u32 level;
    _CPU_ISR_Disable(level);
    const u32 hi = reinterpret_cast<u32>(SYS_GetArena2Hi()) & ~31u;
    const u32 lo = (hi - RWP_ARENA_BYTES) & ~31u;
    const u32 heap_lo = reinterpret_cast<u32>(SYS_GetArena2Lo());
    const bool fits = hi >= RWP_ARENA_BYTES && lo >= heap_lo;
    if (fits) SYS_SetArena2Hi(reinterpret_cast<void*>(lo));
    _CPU_ISR_Restore(level);
    if (!fits) {
        logf("Plugins: no room for the %u KiB plugin arena at the top of MEM2 (its free part is 0x%08x-0x%08x)\n",
             RWP_ARENA_BYTES >> 10, static_cast<unsigned>(heap_lo), static_cast<unsigned>(hi));
        return false;
    }
    g_arena_lo = lo;
    g_arena_hi = hi;
    g_arena_next = lo;
    logf("Plugins: a %u KiB arena at 0x%08x-0x%08x, the top of RiftWii's MEM2 (%u KiB of MEM2 left for the heap)\n",
         RWP_ARENA_BYTES >> 10, static_cast<unsigned>(lo), static_cast<unsigned>(hi),
         static_cast<unsigned>((lo - heap_lo) >> 10));
    return true;
}

// The arena back to the heap when nothing is loaded and nothing has taken
// MEM2 below it since (the heap only grows from the other end).
void ReleaseArena() {
    if (g_arena_hi == 0 || ArenaInUse()) return;
    u32 level;
    _CPU_ISR_Disable(level);
    const bool on_top = reinterpret_cast<u32>(SYS_GetArena2Hi()) == g_arena_lo;
    if (on_top) SYS_SetArena2Hi(reinterpret_cast<void*>(g_arena_hi));
    _CPU_ISR_Restore(level);
    logf("Plugins: the arena at 0x%08x %s\n", static_cast<unsigned>(g_arena_lo),
         on_top ? "is the heap's again" : "stays taken (MEM2's top moved since)");
    if (on_top) g_arena_lo = g_arena_hi = g_arena_next = 0;
}

void* ArenaTake(u32 bytes) {
    // Checked before rounding up, so a size near 4 GiB cannot wrap to 0.
    if (g_arena_hi == 0 || bytes > g_arena_hi - g_arena_next) return nullptr;
    const u32 size = Align32(bytes);
    if (size > g_arena_hi - g_arena_next) return nullptr;
    void* block = reinterpret_cast<void*>(g_arena_next);
    g_arena_next += size;
    return block;
}

// ---- The table RiftWii gives a plugin (include/riftwii/plugin_api.h) ----

void HostLog(const char* format, ...) __attribute__((format(printf, 1, 2)));
void HostLog(const char* format, ...) {
    char line[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    logf("Plugin %s: %s\n", g_current ? g_current->name : "?", line);
}

void* HostArenaAlloc(uint32_t bytes) { return ArenaTake(bytes); }

void* HostMem1Alloc(uint32_t bytes) {
    if (bytes == 0 || bytes > (64u << 20)) return nullptr;  // more than the Wii has: a plugin's mistake
    return memalign(32, Align32(bytes));
}

void HostMem1Free(void* block) { std::free(block); }

void HostFlushRange(void* start, uint32_t bytes) { DCFlushRange(start, bytes); }

void HostInvalidateRange(void* start, uint32_t bytes) { DCInvalidateRange(start, bytes); }

void HostSyncCode(void* start, uint32_t bytes) {
    DCFlushRange(start, bytes);
    ICInvalidateRange(start, bytes);
}

int32_t HostFileRead(const char* path, uint32_t offset, void* buffer, uint32_t bytes) {
    FILE* file = std::fopen(path, "rb");
    if (file == nullptr) return -1;
    std::size_t got = 0;
    if (std::fseek(file, static_cast<long>(offset), SEEK_SET) == 0) got = std::fread(buffer, 1, bytes, file);
    const bool failed = std::ferror(file) != 0;
    std::fclose(file);
    return failed ? -1 : static_cast<int32_t>(got);
}

int32_t HostFileSize(const char* path) {
    struct stat info;
    if (stat(path, &info) != 0 || info.st_size > 0x7FFFFFFF) return -1;
    return static_cast<int32_t>(info.st_size);
}

const char* HostVersion() { return RIFTWII_VERSION; }

const char* HostBuildStamp() { return BuildStamp(); }

int32_t HostCrashRegister(const char* what, uint32_t start, uint32_t bytes) {
    if (g_current == nullptr || g_current->extra_count >= kMaxExtraRanges || bytes == 0) return 0;
    ExtraRange& range = g_current->extra[g_current->extra_count++];
    std::snprintf(range.name, sizeof(range.name), "%s", what ? what : g_current->name);
    range.start = start;
    range.bytes = bytes;
    return 1;
}

// ---- API 2: what a launcher needs (docs/PLUGINS.md, "Stage (b)") ----

int32_t HostFormat(char* out, uint32_t bytes, const char* format, va_list args) {
    return std::vsnprintf(out, bytes, format, args);
}

void* HostMoveBytes(void* to, const void* from, uint32_t bytes) { return std::memmove(to, from, bytes); }

void* HostFillBytes(void* to, int32_t value, uint32_t bytes) { return std::memset(to, value, bytes); }

// The plugin's own lines, as it wrote them: no name put in front, since
// they already say what they are about ("GameCube: ...").
void HostLogText(const char* text) { logf("%s", text); }

void HostLogClose() { LogClose(); }

// A stream is the C library's FILE, which the plugin never looks into.
FILE* AsFile(void* stream) { return static_cast<FILE*>(stream); }

void* HostStreamOpen(const char* path, const char* mode) { return std::fopen(path, mode); }

int32_t HostStreamClose(void* stream) { return std::fclose(AsFile(stream)) == 0 ? 0 : -1; }

uint32_t HostStreamRead(void* stream, void* buffer, uint32_t bytes) {
    return static_cast<uint32_t>(std::fread(buffer, 1, bytes, AsFile(stream)));
}

uint32_t HostStreamWrite(void* stream, const void* buffer, uint32_t bytes) {
    return static_cast<uint32_t>(std::fwrite(buffer, 1, bytes, AsFile(stream)));
}

int32_t HostStreamSeek(void* stream, int64_t offset, int32_t whence) {
    return fseeko(AsFile(stream), static_cast<off_t>(offset), whence) == 0 ? 0 : -1;
}

int64_t HostStreamTell(void* stream) { return static_cast<int64_t>(ftello(AsFile(stream))); }

int32_t HostPathInfo(const char* path, rwp_path_info* out) {
    struct stat info;
    if (stat(path, &info) != 0) return -1;
    out->is_directory = S_ISDIR(info.st_mode) ? 1 : 0;
    out->size = out->is_directory ? 0 : static_cast<uint64_t>(info.st_size);
    return 0;
}

int32_t HostMakeDirectory(const char* path) {
    if (mkdir(path, 0777) == 0) return 0;
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode) ? 0 : -1;
}

void CopyReason(const std::string& reason, char* error, uint32_t error_bytes) {
    if (error != nullptr && error_bytes != 0) std::snprintf(error, error_bytes, "%s", reason.c_str());
}

int32_t HostSdFileFragments(const char* path, rwp_fragment* out, uint32_t max, uint32_t* count, uint64_t* file_bytes,
                            char* error, uint32_t error_bytes) {
    Fat32File file;
    std::string reason;
    if (!resolve_sd_file(path, file, reason)) {
        CopyReason(reason, error, error_bytes);
        return 0;
    }
    *count = static_cast<uint32_t>(file.fragments.size());
    *file_bytes = file.entry.size;
    for (std::size_t i = 0; i < file.fragments.size() && i < max; ++i) {
        out[i].sector = file.fragments[i].sector;
        out[i].sector_count = file.fragments[i].sector_count;
    }
    return 1;
}

void HostSdForgetLayout() { forget_sd_layout(); }

// As a Wii game's launch does: IOS lets one owner have the slot, so the
// file system lets go of the card before it is opened for the game.
int32_t HostSdHandOver(rwp_sd_card* out, char* error, uint32_t error_bytes) {
    fatUnmount("sd:");
    sd_interface()->shutdown();
    sdio::Card card;
    std::string reason;
    if (!sdio::open_card(card, reason)) {
        CopyReason(reason, error, error_bytes);
        return 0;
    }
    if (card.d2x) {
        CopyReason("the SD card is in use through d2x (not handled yet)", error, error_bytes);
        return 0;
    }
    out->fd = card.fd;
    out->rca = card.rca;
    out->sdhc = card.sdhc ? 1 : 0;
    return 1;
}

int32_t HostIosOpen(const char* path, uint32_t mode) { return IOS_Open(path, mode); }

void HostReadSram(rwp_sram* out) {
    syssram* sram = __SYS_LockSram();
    out->counter_bias = sram->counter_bias;
    out->language = sram->lang;
    __SYS_UnlockSram(0);
    syssramex* sramex = __SYS_LockSramEx();
    std::memcpy(out->flash_id_a, sramex->flash_id[0], sizeof(out->flash_id_a));
    __SYS_UnlockSramEx(0);
}

uint32_t HostVideoFramebuffer() { return reinterpret_cast<uint32_t>(VIDEO_GetCurrentFramebuffer()); }

// The Wii's video encoder in the game's region. It has its own region
// setting (NTSC, PAL...) that libogc writes from its retrace handler when
// the video mode changes; once the game runs that handler is gone, so a US
// game on a PAL Wii ran its NTSC picture through an encoder still set for
// PAL: red came out blue, yellow cyan. The game's own mode on the frame on
// screen, two retraces for the handler to apply it, then libogc shut down.
void HostShutDownForGame(uint32_t tv_mode) {
    void* const frame = VIDEO_GetCurrentFramebuffer();
    VIDEO_Configure(tv_mode == RWP_TV_PAL ? &TVPal528IntDf : &TVNtsc480IntDf);
    VIDEO_SetNextFramebuffer(frame);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    VIDEO_WaitVSync();
    SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);
}

size_t HostZstdDecompress(void* to, size_t to_bytes, const void* from, size_t from_bytes) {
    return ZSTD_decompress(to, to_bytes, from, from_bytes);
}

unsigned long long HostZstdFrameSize(const void* from, size_t from_bytes) {
    return ZSTD_getFrameContentSize(from, from_bytes);
}

uint32_t HostZstdIsError(size_t code) { return ZSTD_isError(code) ? 1 : 0; }

const char* HostZstdErrorName(size_t code) { return ZSTD_getErrorName(code); }

// Every entry in plugin_api.h's order, which a plugin was built against;
// tests/plugin_tests.cpp checks that order and that none is skipped.
const rwp_host kHost = {
    sizeof(rwp_host),
    RWP_API_VERSION,
    HostLog,
    HostArenaAlloc,
    HostMem1Alloc,
    HostMem1Free,
    HostFlushRange,
    HostInvalidateRange,
    HostSyncCode,
    HostFileRead,
    HostFileSize,
    HostVersion,
    HostBuildStamp,
    HostCrashRegister,
    // API 2
    HostFormat,
    HostMoveBytes,
    HostFillBytes,
    HostLogText,
    HostLogClose,
    HostStreamOpen,
    HostStreamClose,
    HostStreamRead,
    HostStreamWrite,
    HostStreamSeek,
    HostStreamTell,
    HostPathInfo,
    HostMakeDirectory,
    HostSdFileFragments,
    HostSdForgetLayout,
    HostSdHandOver,
    HostIosOpen,
    HostReadSram,
    HostVideoFramebuffer,
    HostShutDownForGame,
    HostZstdDecompress,
    HostZstdFrameSize,
    HostZstdIsError,
    HostZstdErrorName,
};
// A missing initializer would leave the last entries NULL without a word:
// the count is checked against the struct's size.
static_assert(sizeof(rwp_host) == 8 + 36 * sizeof(void*), "kHost and plugin_api.h's rwp_host disagree");

// ---- Refusals ----

// The sentence on screen, in the menu's language. Every case ends in what
// to do; "Update RiftWii again" brings the whole matching set.
std::string Message(PluginRefusal refusal, const char* name, const std::string& theirs, const std::string& ours) {
    switch (refusal) {
        case PluginRefusal::Missing:
            return tr("A part of RiftWii is missing (the {1} add-on). Update RiftWii again.", {name});
        case PluginRefusal::Damaged:
        case PluginRefusal::Hash:
            return tr("A part of RiftWii is damaged (the {1} add-on). Update RiftWii again.", {name});
        case PluginRefusal::Api:
        case PluginRefusal::Stamp:
            return tr("The {1} add-on is from a different RiftWii ({2}, this one is {3}). Update RiftWii again.",
                      {name, theirs, ours});
        case PluginRefusal::NoMemory:
            return tr("There was not enough memory for the {1} add-on. Restart RiftWii and try again.", {name});
        case PluginRefusal::InitFailed:
        case PluginRefusal::None:
            break;
    }
    return tr("The {1} add-on did not start. Send a problem report so this can be fixed.", {name});
}

struct Refused {
    PluginRefusal refusal = PluginRefusal::None;
    std::string why;      // for the log
    std::string theirs;   // the plugin's version or stamp, for the message
    std::string ours;
    bool no_file = false;  // this version's file is not there (looked for again)
};

// Formats `why` once with printf: smaller code than strings built from
// pieces, and this is all MEM1.
void Say(std::string& why, const char* format, ...) __attribute__((format(printf, 2, 3)));
void Say(std::string& why, const char* format, ...) {
    char text[200];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    why = text;
}

std::string ReadArgv0() {
    if (__system_argv != nullptr && __system_argv->argvMagic == ARGV_MAGIC && __system_argv->argc > 0 &&
        __system_argv->argv != nullptr && __system_argv->argv[0] != nullptr) {
        return __system_argv->argv[0];
    }
    return "";
}

bool FileExists(const std::string& path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

// Another version's file of `name` in `directory` ("" when none): an
// old plugin from before an update, which is never loaded.
std::string OtherVersionFile(const std::string& directory, const char* name) {
    DIR* dir = opendir(directory.c_str());
    if (dir == nullptr) return "";
    std::string found;
    while (dirent* entry = readdir(dir)) {
        std::string plugin;
        if (plugin_file_of_other_version(entry->d_name, RIFTWII_VERSION, plugin) && plugin == name) {
            found = entry->d_name;
            break;
        }
    }
    closedir(dir);
    return found;
}

// The file for `name`: in Directory(), else, when that is another folder,
// in sd:/apps/riftwii/plugins/ (a DOL started from a USB drive before the
// drive is mounted, or by a loader that passes no path).
std::string FilePath(const char* name) {
    const std::string file = plugin_file_name(name, RIFTWII_VERSION);
    const std::string first = Directory() + file;
    if (FileExists(first) || Directory() == kDefaultDirectory) return first;
    const std::string fallback = std::string(kDefaultDirectory) + file;
    return FileExists(fallback) ? fallback : first;
}

bool ReadExactly(FILE* file, void* buffer, std::size_t bytes) { return std::fread(buffer, 1, bytes, file) == bytes; }

// Every check, then the image in the arena, relocated and made runnable.
// On success `slot` describes the loaded plugin; on failure nothing of it
// stays (the arena's next block is where it was) and `refused` says why.
bool LoadInto(Slot& slot, const std::string& path, Refused& refused) {
    const char* name = slot.name;
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        refused.no_file = true;
        const std::string directory = path.substr(0, path.rfind('/') + 1);
        const std::string other = OtherVersionFile(directory, name);
        if (!other.empty()) {
            // An old plugin left from before an update: named as one.
            refused.refusal = PluginRefusal::Stamp;
            refused.why = "there is no " + path + ", only " + other + " from another RiftWii, which is never loaded";
            refused.theirs = other.substr(other.find('.') + 1, other.size() - other.find('.') - 5);
            refused.ours = RIFTWII_VERSION;
        } else {
            refused.refusal = PluginRefusal::Missing;
            refused.why = "there is no " + path;
        }
        return false;
    }
    std::unique_ptr<FILE, int (*)(FILE*)> closer(file, std::fclose);
    struct stat info;
    std::uint8_t header_bytes[kPluginHeaderBytes];
    const std::uint64_t file_bytes = fstat(fileno(file), &info) == 0 ? static_cast<std::uint64_t>(info.st_size) : 0;
    const bool header_read = ReadExactly(file, header_bytes, sizeof(header_bytes));
    PluginHeader header;
    refused.refusal = plugin_parse_header(header_read ? header_bytes : nullptr, header_read ? sizeof(header_bytes) : 0,
                                          file_bytes, name, RWP_ARENA_BYTES, header, refused.why);
    if (refused.refusal != PluginRefusal::None) return false;

    // Only now, with every size known to fit: the arena and the read.
    if (!ReserveArena()) {
        refused.refusal = PluginRefusal::NoMemory;
        refused.why = "MEM2 has no room for the plugin arena";
        return false;
    }
    const u32 arena_mark = g_arena_next;
    u8* image = static_cast<u8*>(ArenaTake(header.memory_bytes()));
    if (image == nullptr) {
        refused.refusal = PluginRefusal::NoMemory;
        Say(refused.why, "the arena has %u bytes left, it needs %u", static_cast<unsigned>(g_arena_hi - g_arena_next),
            static_cast<unsigned>(header.memory_bytes()));
        return false;
    }
    const auto give_back = [&] { g_arena_next = arena_mark; };
    std::vector<std::uint8_t> relocs(std::size_t(header.reloc_count) * kPluginRelocBytes);
    if (!ReadExactly(file, image, header.image_bytes) ||
        (!relocs.empty() && !ReadExactly(file, relocs.data(), relocs.size()))) {
        give_back();
        refused.refusal = PluginRefusal::Damaged;
        refused.why = "the file could not be read to its end";
        return false;
    }
    std::uint8_t zeroed[kPluginHeaderBytes];
    plugin_hash_header(header_bytes, zeroed);
    Sha256 hasher;
    hasher.update(zeroed, sizeof(zeroed));
    hasher.update(image, header.image_bytes);
    if (!relocs.empty()) hasher.update(relocs.data(), relocs.size());
    const Sha256Digest digest = hasher.finish();
    if (std::memcmp(digest.data(), header.hash, digest.size()) != 0) {
        give_back();
        refused.refusal = PluginRefusal::Hash;
        refused.why = "its SHA-256 does not match its header (damaged on the card or cut short)";
        return false;
    }
    refused.refusal = plugin_check_versions(header, RWP_API_VERSION, RIFTWII_BUILD_STAMP, refused.why);
    if (refused.refusal != PluginRefusal::None) {
        give_back();
        if (refused.refusal == PluginRefusal::Api) {
            Say(refused.theirs, "API %u", static_cast<unsigned>(header.api_version));
            Say(refused.ours, "API %u", static_cast<unsigned>(RWP_API_VERSION));
        } else {
            refused.theirs = header.stamp;
            refused.ours = RIFTWII_BUILD_STAMP;
        }
        return false;
    }

    const u32 base = reinterpret_cast<u32>(image);
    std::memset(image + header.image_bytes, 0, header.memory_bytes() - header.image_bytes);  // .bss and state
    if (!plugin_relocate(image, header, relocs.data(), base, refused.why)) {
        give_back();
        refused.refusal = PluginRefusal::Damaged;
        return false;
    }
    refused.refusal = plugin_check_table(image, header, base, RWP_API_VERSION, refused.why);
    if (refused.refusal != PluginRefusal::None) {
        give_back();
        return false;
    }
    const rwp_plugin* table = reinterpret_cast<const rwp_plugin*>(base + header.table_offset);
    // Code written as data: out of the data cache to memory, then the
    // instruction cache told to fetch it again (as wii/hooks/vsdhook.cpp
    // does for the code it places in MEM2), then the pipeline synced.
    DCFlushRange(image, header.memory_bytes());
    ICInvalidateRange(image, header.memory_bytes());
    asm volatile("sync; isync" ::: "memory");

    slot.loaded = true;
    std::snprintf(slot.stamp, sizeof(slot.stamp), "%s", header.stamp.c_str());
    slot.version = header.plugin_version;
    slot.base = base;
    slot.memory_bytes = header.memory_bytes();
    slot.image_bytes = header.image_bytes;
    slot.shutdown = header.shutdown_offset != 0 ? base + header.shutdown_offset : 0;
    slot.table = table;
    logf("Plugins: %s %u read from %s: %u bytes (image %u, .bss %u, %u relocations), at 0x%08x-0x%08x, %s\n", name,
         static_cast<unsigned>(header.plugin_version), path.c_str(), static_cast<unsigned>(file_bytes),
         static_cast<unsigned>(header.image_bytes), static_cast<unsigned>(header.bss_bytes),
         static_cast<unsigned>(header.reloc_count), static_cast<unsigned>(base),
         static_cast<unsigned>(base + header.memory_bytes()), header.stamp.c_str());

    // Its init, with the slot already in the crash table: a crash in init
    // is named as the plugin's.
    const rwp_init_fn init = reinterpret_cast<rwp_init_fn>(base + header.init_offset);
    Slot* const before = g_current;
    g_current = &slot;
    const int32_t result = init(&kHost);
    g_current = before;
    if (result != 0) {
        slot.loaded = false;
        give_back();
        refused.refusal = PluginRefusal::InitFailed;
        Say(refused.why, "its init returned %d (the lines before say why)", static_cast<int>(result));
        return false;
    }
    return true;
}

}  // namespace

std::string Directory() {
    const auto setting = Settings().other.find("plugins");
    if (setting != Settings().other.end() && !setting->second.empty()) {
        std::string directory = setting->second;
        if (directory.back() != '/') directory += '/';
        return directory;
    }
    return plugin_directory_for(ReadArgv0());
}

const rwp_plugin* Use(const char* name, std::string* message) {
    if (name == nullptr || !plugin_name_valid(name)) return nullptr;
    Slot* slot = Find(name);
    if (slot != nullptr && slot->loaded) return slot->table;
    // A refusal stands for the run, so the card is not read again at every
    // use; only a missing file is looked for again (the update may bring it).
    if (slot != nullptr && !slot->no_file) {
        if (message) *message = Message(slot->refusal, name, slot->theirs, slot->ours);
        return nullptr;
    }
    if (slot == nullptr) slot = NewSlot(name);
    if (slot == nullptr) {
        logf("Plugins: %s not loaded: RiftWii keeps at most %u plugins\n", name, static_cast<unsigned>(kMaxPlugins));
        if (message) *message = Message(PluginRefusal::NoMemory, name, "", "");
        return nullptr;
    }
    const std::string path = FilePath(name);
    const u64 started = gettime();
    Refused refused;
    const bool loaded = LoadInto(*slot, path, refused);
    const unsigned ms = static_cast<unsigned>(diff_msec(started, gettime()));
    if (loaded) {
        slot->refusal = PluginRefusal::None;
        logf("Plugins: %s ready in %u ms (read, checked, relocated and started)\n", name, ms);
        return slot->table;
    }
    slot->refusal = refused.refusal;
    slot->no_file = refused.no_file;
    slot->theirs = refused.theirs;
    slot->ours = refused.ours;
    logf("Plugins: %s not loaded (%s): %s. What needs it stays off%s\n", name, plugin_refusal_name(refused.refusal),
         refused.why.c_str(), refused.refusal == PluginRefusal::InitFailed ? "" : "; none of its code ran");
    ReleaseArena();
    if (message) *message = Message(refused.refusal, name, refused.theirs, refused.ours);
    return nullptr;
}

int32_t Launch(const char* name, const void* request, uint32_t request_bytes, std::string& message) {
    const rwp_plugin* table = Use(name, &message);
    if (table == nullptr) return -1;
    if (table->launch == nullptr) {
        logf("Plugins: %s starts nothing (its table has no launch)\n", name);
        message = Message(PluginRefusal::InitFailed, name, "", "");
        return -1;
    }
    // Its code runs as the plugin's: a crash report names it, and what it
    // registers for the crash screen is its own.
    Slot* const before = g_current;
    g_current = Find(name);
    const int32_t result = table->launch(request, request_bytes);
    g_current = before;
    return result;
}

void ShutdownAll(const char* why) {
    bool any = false;
    for (Slot& slot : g_slots) {
        if (!slot.loaded) continue;
        any = true;
        if (slot.shutdown != 0) {
            Slot* const before = g_current;
            g_current = &slot;
            reinterpret_cast<void (*)()>(slot.shutdown)();
            g_current = before;
        }
        slot.loaded = false;
        slot.used = false;
        logf("Plugins: %s shut down (%s)\n", slot.name, why);
    }
    if (any) {
        g_arena_next = g_arena_lo;
        ReleaseArena();
    }
}

void RemoveOtherVersions() {
    const std::string directory = Directory();
    DIR* dir = opendir(directory.c_str());
    if (dir == nullptr) return;
    std::vector<std::string> old;
    while (dirent* entry = readdir(dir)) {
        std::string name;
        // Only when this version's file of the same plugin is there: a
        // card is never left with no file at all for a plugin.
        if (plugin_file_of_other_version(entry->d_name, RIFTWII_VERSION, name) &&
            FileExists(directory + plugin_file_name(name, RIFTWII_VERSION))) {
            old.push_back(entry->d_name);
        }
    }
    closedir(dir);
    for (const std::string& file : old) {
        const bool removed = std::remove((directory + file).c_str()) == 0;
        logf("Plugins: %s %s (another version's, never loaded by %s)\n", file.c_str(),
             removed ? "removed" : "could not be removed", RIFTWII_VERSION);
    }
}

std::size_t CrashRanges(PluginRange* out, std::size_t max) {
    std::size_t count = 0;
    for (const Slot& slot : g_slots) {
        if (!slot.loaded) continue;
        if (count < max) out[count++] = PluginRange{slot.name, slot.base, slot.memory_bytes};
        for (std::size_t i = 0; i < slot.extra_count && count < max; ++i) {
            out[count++] = PluginRange{slot.extra[i].name, slot.extra[i].start, slot.extra[i].bytes};
        }
    }
    return count;
}

void CrashSummary(char* out, std::size_t bytes) {
    std::size_t at = 0;
    out[0] = '\0';
    for (const Slot& slot : g_slots) {
        if (!slot.loaded || at >= bytes) continue;
        const int n = std::snprintf(out + at, bytes - at, "%s%s %u at 0x%08x-0x%08x (%s)", at ? ", " : "", slot.name,
                                    static_cast<unsigned>(slot.version), static_cast<unsigned>(slot.base),
                                    static_cast<unsigned>(slot.base + slot.memory_bytes), slot.stamp);
        if (n > 0) at += static_cast<std::size_t>(n);
    }
    if (at == 0) std::snprintf(out, bytes, "none");
}

std::string Summary() {
    std::string text;
    for (const Slot& slot : g_slots) {
        if (!slot.used) continue;
        char line[160];
        if (slot.loaded) {
            std::snprintf(line, sizeof(line), "%s %u loaded at 0x%08x (%s)", slot.name,
                          static_cast<unsigned>(slot.version), static_cast<unsigned>(slot.base), slot.stamp);
        } else {
            std::snprintf(line, sizeof(line), "%s not loaded (%s)", slot.name, plugin_refusal_name(slot.refusal));
        }
        if (!text.empty()) text += ", ";
        text += line;
    }
    return text.empty() ? "none used this run" : text + "; this RiftWii's stamp " RIFTWII_BUILD_STAMP;
}

bool RunTestSwitch(std::string& refused) {
    refused.clear();
    FILE* file = std::fopen(kTestFile, "rb");
    if (file == nullptr) return false;
    std::vector<std::string> names;
    char line[96];
    while (std::fgets(line, sizeof(line), file)) {
        char name[sizeof(line)];
        if (std::sscanf(line, " load %95s", name) == 1) names.push_back(name);
    }
    std::fclose(file);
    logf("Plugin test (%s): %u plugin(s) to load from %s\n", kTestFile, static_cast<unsigned>(names.size()),
         Directory().c_str());
    for (const std::string& name : names) {
        std::string message;
        const rwp_plugin* table = Use(name.c_str(), &message);
        if (table == nullptr) {
            logf("Plugin test: %s refused; on screen: %s\n", name.c_str(), message.c_str());
            if (refused.empty()) refused = message;
            continue;
        }
        if (table->probe != nullptr) {
            Slot* slot = Find(name.c_str());
            Slot* const before = g_current;
            g_current = slot;
            const int32_t found = table->probe("sd:/");
            g_current = before;
            logf("Plugin test: %s loaded; its probe of sd:/ answered %d\n", name.c_str(), static_cast<int>(found));
        } else {
            logf("Plugin test: %s loaded\n", name.c_str());
        }
    }
    return true;
}

}  // namespace riftwii::wii::plugin
