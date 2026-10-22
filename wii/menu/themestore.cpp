// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "themestore.hpp"

#include <dirent.h>
#include <ogc/cache.h>
#include <ogc/gx.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>

#include "httpmem.hpp"
#include "log.hpp"
#include "netsock.hpp"
#include "skin.hpp"
#include "textfile.hpp"
#include "tls.hpp"
#include "riftwii/pngdecode.hpp"
#include "riftwii/sha256.hpp"
#include "riftwii/theme.hpp"
#include "riftwii/themelist.hpp"
#include "riftwii/zipread.hpp"

namespace riftwii::wii::themes {
namespace {

constexpr const char* kThemesDir = "sd:/riftwii/themes";
constexpr const char* kRecordsPath = "sd:/riftwii/themes/.browser.txt";
constexpr std::size_t kRecordsCap = 64u << 10;

// How many previews are kept as textures at once, and how far from the
// focused theme the loader fetches ahead.
constexpr int kSlots = 12;
constexpr int kReadAhead = 4;
constexpr std::size_t kThumbBytes = std::size_t(kThumbWidth) * kThumbHeight * 2;

// The work block's parts. The answer's buffer holds headers and body, so it
// is a little over what the list (256 KB) and a preview (256 KB) may be.
constexpr std::size_t kRawBytes = kThemePreviewMaxBytes + 8192;
// The PNG decoder's three buffers for a 640x480 picture: the joined image
// data, the unfiltered rows and the RGBA result (2.7 MB at 8 bits a channel).
constexpr std::size_t kScratchBytes = 3u << 20;
// zlib's state and window for one inflate (about 40 KB), and the chunk
// each inflate hands on.
constexpr std::size_t kZlibBytes = 64u << 10;
constexpr std::size_t kChunkBytes = 16u << 10;
// What a zip's download may be over its size: the HTTP headers.
constexpr std::size_t kHeaderRoom = 8192;

// A block of MEM2 that is kept between visits when it cannot be given back
// (the arena only moves one way: see skin::Mem2Release).
struct Block {
    u8* data = nullptr;
    std::size_t bytes = 0;
};

Block g_thumbs, g_listBlock, g_work, g_zip;

// Takes `need` bytes into `block`: the block already held when it is big
// enough, else a new one (the old one given back when it can be).
bool Ensure(Block& block, std::size_t need) {
    if (block.data && block.bytes >= need) return true;
    if (block.data && skin::Mem2Release(block.data, block.bytes)) block = Block{};
    block.data = skin::Mem2Alloc(need);
    block.bytes = block.data ? need : 0;
    return block.data != nullptr;
}

void Giveback(Block& block) {
    if (block.data && skin::Mem2Release(block.data, block.bytes)) block = Block{};
}

// ---- State of the page ----

enum class Job { None, List, Preview, Install };

struct Slot {
    int index = -1;       // the theme whose preview it holds (or is being filled with)
    bool ready = false;
    unsigned used = 0;    // the last time it was drawn
};

bool g_open = false;
Slot g_slots[kSlots];
unsigned g_clock = 0;
bool g_texDirty = false;  // a slot was refilled: GX's texture cache must forget
std::size_t g_focus = 0;

ThemeListing* g_listing = nullptr;
std::size_t g_count = 0;
unsigned char* g_failed = nullptr;     // per theme: its preview could not be fetched
unsigned char* g_installed = nullptr;  // per theme: an Installed value
ListState g_listState = ListState::Loading;
std::string g_listError;

// Which job runs, and its parts. The job's thread writes what is marked as
// its own while `g_job` is not None; Pump reads it only once the thread ended.
volatile Job g_job = Job::None;
bool g_listAsked = false;
std::size_t g_jobIndex = 0;   // the theme of a preview or an install
int g_jobSlot = -1;
bool g_jobOk = false;
std::string g_jobError;
volatile bool g_cancel = false;

bool g_installAsked = false;
std::size_t g_installIndex = 0;
bool g_installFinished = false;
std::string g_installFolder, g_installError;
volatile Stage g_stage = Stage::Download;
volatile std::uint32_t g_done = 0, g_total = 0;

// The page's own record of the card: what the browser installed, and the
// folders there (lower case, FAT does not tell a from A).
std::string g_records;
std::set<std::string> g_folders;

std::string Lower(const std::string& text) {
    std::string out = text;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

u8* SlotData(int slot) { return g_thumbs.data + static_cast<std::size_t>(slot) * kThumbBytes; }

// ---- zlib in MEM2 ----
// zlib asks for its state and window from the heap; here it is lent a block
// of the work area instead, handed out once per inflate.
struct ZlibHeap {
    u8* base = nullptr;
    std::size_t size = 0, used = 0;
};
ZlibHeap g_zheap;

voidpf ZlibAlloc(voidpf, uInt items, uInt size) {
    const std::size_t bytes = (static_cast<std::size_t>(items) * size + 7) & ~std::size_t(7);
    if (bytes > g_zheap.size - g_zheap.used) return Z_NULL;
    voidpf at = g_zheap.base + g_zheap.used;
    g_zheap.used += bytes;
    return at;
}
void ZlibFree(voidpf, voidpf) {}

struct WorkParts {
    u8* tls = nullptr;
    u8* raw = nullptr;
    u8* scratch = nullptr;
    u8* zlib = nullptr;
    u8* chunk = nullptr;
};
WorkParts g_parts;

bool ArrangeWork() {
    const std::size_t tls = (TlsStream::memory_needed() + 31) & ~std::size_t(31);
    const std::size_t total = tls + kRawBytes + kScratchBytes + kZlibBytes + kChunkBytes;
    if (!Ensure(g_work, total)) return false;
    u8* at = g_work.data;
    g_parts.tls = at;
    at += tls;
    g_parts.raw = at;
    at += kRawBytes;
    g_parts.scratch = at;
    at += kScratchBytes;
    g_parts.zlib = at;
    at += kZlibBytes;
    g_parts.chunk = at;
    return true;
}

void ResetZlibHeap() {
    g_zheap.base = g_parts.zlib;
    g_zheap.size = kZlibBytes;
    g_zheap.used = 0;
}

// A PNG's zlib stream into exactly `out_size` bytes (pngdecode.hpp).
bool PngInflate(const std::uint8_t* data, std::size_t size, std::uint8_t* out, std::size_t out_size) {
    ResetZlibHeap();
    z_stream z = {};
    z.zalloc = ZlibAlloc;
    z.zfree = ZlibFree;
    if (inflateInit(&z) != Z_OK) return false;
    z.next_in = const_cast<Bytef*>(data);
    z.avail_in = static_cast<uInt>(size);
    z.next_out = out;
    z.avail_out = static_cast<uInt>(out_size);
    int result = Z_OK;
    while (z.avail_out > 0 && result == Z_OK) result = inflate(&z, Z_SYNC_FLUSH);
    inflateEnd(&z);
    return z.avail_out == 0 && (result == Z_OK || result == Z_STREAM_END || result == Z_BUF_ERROR);
}

// A zip entry's raw deflate data, in chunks to `sink`, no more than
// `expected` bytes in all (zipread.hpp).
bool ZipInflate(const std::uint8_t* in, std::size_t in_size, std::uint64_t expected, const ZipSink& sink) {
    ResetZlibHeap();
    z_stream z = {};
    z.zalloc = ZlibAlloc;
    z.zfree = ZlibFree;
    if (inflateInit2(&z, -15) != Z_OK) return false;
    z.next_in = const_cast<Bytef*>(in);
    z.avail_in = static_cast<uInt>(in_size);
    std::uint64_t total = 0;
    bool ok = false;
    for (;;) {
        z.next_out = g_parts.chunk;
        z.avail_out = static_cast<uInt>(kChunkBytes);
        const int result = inflate(&z, Z_NO_FLUSH);
        const std::size_t made = kChunkBytes - z.avail_out;
        if (total + made > expected) break;  // more than the zip said
        if (made > 0 && !sink(g_parts.chunk, made)) break;
        total += made;
        if (result == Z_STREAM_END) {
            ok = total == expected;
            break;
        }
        if (result != Z_OK) break;  // damaged, or no progress (Z_BUF_ERROR)
    }
    inflateEnd(&z);
    return ok;
}

// ---- Thumbnails ----

// `rgba` (w x h) averaged down into the slot's 160x120, as RGB5A3 in 4x4
// tiles, opaque: a picture's see-through parts show over white.
void MakeThumbnail(const u8* rgba, int w, int h, u8* out) {
    constexpr int kTilesAcross = kThumbWidth / 4;
    for (int py = 0; py < kThumbHeight; ++py) {
        const int y0 = py * h / kThumbHeight;
        const int y1 = std::max(y0 + 1, (py + 1) * h / kThumbHeight);
        for (int px = 0; px < kThumbWidth; ++px) {
            const int x0 = px * w / kThumbWidth;
            const int x1 = std::max(x0 + 1, (px + 1) * w / kThumbWidth);
            unsigned r = 0, g = 0, b = 0, count = 0;
            for (int y = y0; y < y1 && y < h; ++y) {
                for (int x = x0; x < x1 && x < w; ++x) {
                    const u8* p = rgba + (static_cast<std::size_t>(y) * w + x) * 4;
                    const unsigned a = p[3];
                    r += (p[0] * a + 255 * (255 - a)) / 255;
                    g += (p[1] * a + 255 * (255 - a)) / 255;
                    b += (p[2] * a + 255 * (255 - a)) / 255;
                    ++count;
                }
            }
            if (count == 0) count = 1;
            const unsigned value = 0x8000u | ((r / count >> 3) << 10) | ((g / count >> 3) << 5) | (b / count >> 3);
            const std::size_t tile = static_cast<std::size_t>(py / 4) * kTilesAcross + static_cast<std::size_t>(px / 4);
            u8* texel = out + tile * 32 + static_cast<std::size_t>((py % 4) * 4 + (px % 4)) * 2;
            texel[0] = static_cast<u8>(value >> 8);
            texel[1] = static_cast<u8>(value);
        }
    }
}

// ---- The card ----

void MakeDirs() {
    mkdir("sd:/riftwii", 0777);
    mkdir(kThemesDir, 0777);
}

bool IsDirectory(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// A folder and everything in it, at most four levels deep (a theme is one
// level; a hand-made one may hold a sounds folder).
bool RemoveTree(const std::string& path, int depth = 0) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return true;
    if (!S_ISDIR(st.st_mode)) return unlink(path.c_str()) == 0;
    if (depth > 4) return false;
    std::vector<std::string> names;
    if (DIR* d = opendir(path.c_str())) {
        while (const dirent* e = readdir(d)) {
            if (std::strcmp(e->d_name, ".") != 0 && std::strcmp(e->d_name, "..") != 0) names.push_back(e->d_name);
        }
        closedir(d);
    }
    for (const std::string& name : names) {
        if (!RemoveTree(path + "/" + name, depth + 1)) return false;
    }
    return rmdir(path.c_str()) == 0;
}

bool WriteRecords(const std::string& text) {
    const std::string temp = std::string(kRecordsPath) + ".part";
    if (!WriteWholeFile(temp, text.data(), text.size())) {
        std::remove(temp.c_str());
        return false;
    }
    std::remove(kRecordsPath);  // FAT's rename does not replace
    if (std::rename(temp.c_str(), kRecordsPath) != 0) {
        std::remove(temp.c_str());
        return false;
    }
    return true;
}

std::string HexOf(const Sha256Digest& digest) {
    static const char* const kDigits = "0123456789abcdef";
    std::string out;
    for (std::uint8_t byte : digest) {
        out += kDigits[byte >> 4];
        out += kDigits[byte & 15];
    }
    return out;
}

// ---- Jobs (on the network's background thread) ----

// The only way the browser reaches the network: https://riftwii.github.io/
// and nothing else, however the address came to be in the list (HttpGetInMemory
// then follows redirects on that same server only).
bool Fetch(const std::string& url, MemoryGet& get, std::string& error, int timeout_ms = 15000,
           const HttpProgress& progress = nullptr) {
    if (url != kThemeListUrl && !theme_url_ok(url)) {
        error = "RiftWii only fetches themes from " + std::string(kThemeSiteUrl);
        return false;
    }
    return HttpGetInMemory(url, get, error, timeout_ms, progress);
}

std::string Short(const std::string& text) { return text.size() <= 120 ? text : text.substr(0, 117) + "..."; }

void RunList() {
    MemoryGet get;
    get.buffer = g_parts.raw;
    get.capacity = kRawBytes;
    get.tls_block = g_parts.tls;
    get.tls_bytes = TlsStream::memory_needed();
    g_jobOk = false;
    if (!Fetch(kThemeListUrl, get, g_jobError)) return;
    if (get.body_length > kThemeListMaxBytes) {
        g_jobError = "the theme list is bigger than RiftWii takes (256 KB)";
        return;
    }
    std::size_t skipped = 0;
    // Read where it lies in the download: no copy of it in the heap.
    if (!parse_theme_list(reinterpret_cast<const char*>(get.buffer + get.body_offset), get.body_length, g_listing,
                          kThemeListMaxThemes, g_count, skipped, g_jobError))
        return;
    logf("Themes: the list has %u themes (%u entries left out for breaking a rule)\n", static_cast<unsigned>(g_count),
         static_cast<unsigned>(skipped));
    g_jobOk = true;
}

void RunPreview() {
    g_jobOk = false;
    const ThemeListing& theme = g_listing[g_jobIndex];
    MemoryGet get;
    get.buffer = g_parts.raw;
    get.capacity = kRawBytes;
    get.tls_block = g_parts.tls;
    get.tls_bytes = TlsStream::memory_needed();
    if (!Fetch(theme.preview, get, g_jobError)) return;
    if (get.body_length > kThemePreviewMaxBytes) {
        g_jobError = "the preview is bigger than RiftWii takes (256 KB)";
        return;
    }
    const std::uint8_t* rgba = nullptr;
    std::uint32_t w = 0, h = 0;
    if (!decode_png_in(get.buffer + get.body_offset, get.body_length, &PngInflate, 1024, g_parts.scratch, kScratchBytes,
                       rgba, w, h, g_jobError))
        return;
    u8* out = SlotData(g_jobSlot);
    MakeThumbnail(rgba, static_cast<int>(w), static_cast<int>(h), out);
    DCFlushRange(out, kThumbBytes);
    g_jobOk = true;
}

bool Cancelled(std::string& error) {
    if (!g_cancel) return false;
    error = "cancelled";
    return true;
}

// The whole install of `theme`, from the download to the move into place.
// `zip` is the buffer the download is read into (headers and body).
bool InstallTheme(const ThemeListing& theme, MemoryGet& get, std::string& error) {
    const std::string folder = theme.folder;
    const std::string temp = std::string(kThemesDir) + "/." + folder + ".tmp";
    const std::string old = std::string(kThemesDir) + "/." + folder + ".old";
    const std::string final_path = std::string(kThemesDir) + "/" + folder;

    g_stage = Stage::Download;
    g_done = 0;
    g_total = theme.size;
    logf("Themes: downloading %s (%u bytes)\n", folder.c_str(), static_cast<unsigned>(theme.size));
    const HttpProgress progress = [](std::size_t received) { g_done = static_cast<std::uint32_t>(received); };
    if (!Fetch(theme.zip, get, error, 20000, progress)) return false;
    if (Cancelled(error)) return false;

    g_stage = Stage::Check;
    // Exactly the size the list said, then its SHA-256, before anything is
    // looked at: a download that is not the zip the list named is thrown away.
    if (get.body_length != theme.size) {
        error = "the download is " + std::to_string(get.body_length) + " bytes, not the " + std::to_string(theme.size) +
                " the theme list says";
        return false;
    }
    const std::uint8_t* zip = get.buffer + get.body_offset;
    if (HexOf(sha256(zip, get.body_length)) != theme.sha256) {
        error = "the download is not the file the theme list names (its SHA-256 differs)";
        return false;
    }
    std::vector<ZipEntry> entries;
    if (!zip_read_directory(zip, get.body_length, entries, error)) return false;
    ZipThemeLimits limits;
    limits.files = theme.files;
    limits.unpacked_size = theme.unpacked_size;
    std::vector<ZipThemeFile> plan;
    if (!zip_plan_theme(entries, folder, limits, plan, error)) return false;

    struct statvfs vfs;
    if (statvfs("sd:/", &vfs) == 0) {
        const std::uint64_t free_bytes = static_cast<std::uint64_t>(vfs.f_bavail) * vfs.f_frsize;
        if (free_bytes < std::uint64_t(theme.unpacked_size) + (1u << 20)) {
            error = "the SD card has no room for this theme";
            return false;
        }
    }

    g_stage = Stage::Unpack;
    g_done = 0;
    g_total = static_cast<std::uint32_t>(plan.size());
    MakeDirs();
    RemoveTree(temp);
    if (mkdir(temp.c_str(), 0777) != 0) {
        error = "cannot make the folder " + temp;
        return false;
    }
    for (const ZipThemeFile& file : plan) {
        if (Cancelled(error)) return false;
        const std::string path = temp + "/" + file.file;
        FILE* out = std::fopen(path.c_str(), "wb");
        if (!out) {
            error = "cannot write " + path;
            return false;
        }
        const ZipSink sink = [&](const std::uint8_t* data, std::size_t size) {
            return std::fwrite(data, 1, size, out) == size;
        };
        const bool unpacked = zip_extract(zip, get.body_length, entries[file.entry], &ZipInflate, sink, error);
        const bool closed = std::fclose(out) == 0;
        if (!unpacked) return false;
        if (!closed) {
            error = "cannot write " + path + " (card full?)";
            return false;
        }
        g_done = g_done + 1;
    }

    g_stage = Stage::Verify;
    // RiftWii's own theme loader's checks: theme.ini read as the menu reads
    // it, and every picture the theme has that the menu would use decoded
    // (a damaged one is refused here; one of the wrong size the menu skips,
    // with a note, as it does for any theme).
    std::string ini;
    if (!ReadTextFile(temp + "/theme.ini", ini, 64u << 10)) {
        error = "theme.ini is missing or over 64 KB";
        return false;
    }
    std::vector<std::string> notes;
    const Theme parsed = parse_theme(ini, notes);
    for (const std::string& note : notes) logf("Themes: %s's theme.ini: %s\n", folder.c_str(), note.c_str());
    for (const ThemeImage& image : theme_images()) {
        if (Cancelled(error)) return false;
        const std::string path = temp + "/" + image.name + ".png";
        struct stat st;
        if (stat(path.c_str(), &st) != 0) continue;
        // The download buffer is free now: the picture is read into it.
        if (st.st_size <= 0 || static_cast<std::size_t>(st.st_size) > get.capacity) {
            error = std::string(image.name) + ".png is too big";
            return false;
        }
        FILE* in = std::fopen(path.c_str(), "rb");
        const bool read = in && std::fread(get.buffer, 1, static_cast<std::size_t>(st.st_size), in) ==
                                    static_cast<std::size_t>(st.st_size);
        if (in) std::fclose(in);
        if (!read) {
            error = "cannot read " + path;
            return false;
        }
        const std::uint8_t* rgba = nullptr;
        std::uint32_t w = 0, h = 0;
        std::string why;
        if (!decode_png_in(get.buffer, static_cast<std::size_t>(st.st_size), &PngInflate, 1024, g_parts.scratch,
                           kScratchBytes, rgba, w, h, why)) {
            error = std::string(image.name) + ".png is damaged (" + why + ")";
            return false;
        }
        if (static_cast<int>(w) != image.w || static_cast<int>(h) != image.h)
            logf("Themes: %s's %s.png is %ux%u, not %dx%d; the menu will paint its own\n", folder.c_str(), image.name,
                 static_cast<unsigned>(w), static_cast<unsigned>(h), image.w, image.h);
    }
    if (Cancelled(error)) return false;

    g_stage = Stage::Finish;
    // The old copy is moved aside, the new one into its place, and only then
    // is the old one removed, so a failure leaves a theme that works.
    RemoveTree(old);
    const bool had = IsDirectory(final_path);
    if (had && std::rename(final_path.c_str(), old.c_str()) != 0) {
        error = "cannot move the old copy of the theme aside";
        return false;
    }
    if (std::rename(temp.c_str(), final_path.c_str()) != 0) {
        if (had) std::rename(old.c_str(), final_path.c_str());
        error = "cannot move the theme into place";
        return false;
    }
    if (had) RemoveTree(old);
    std::string records;
    ReadTextFile(kRecordsPath, records, kRecordsCap);
    if (!WriteRecords(theme_record_text(records, folder, theme.sha256)))
        logf("Themes: %s is installed, but sd:/riftwii/themes/.browser.txt could not be written; the browser will not know it as its own\n",
             folder.c_str());
    return true;
}

void RunInstall() {
    g_jobOk = false;
    g_jobError.clear();
    const ThemeListing theme = g_listing[g_jobIndex];
    const std::size_t need = std::size_t(theme.size) + kHeaderRoom;
    if (!Ensure(g_zip, need)) {
        g_jobError = "not enough free memory for this download";
        return;
    }
    MemoryGet get;
    get.buffer = g_zip.data;
    get.capacity = need;
    get.tls_block = g_parts.tls;
    get.tls_bytes = TlsStream::memory_needed();
    std::string error;
    const bool ok = InstallTheme(theme, get, error);
    const std::string temp = std::string(kThemesDir) + "/." + theme.folder + ".tmp";
    // Whatever went wrong, no half-unpacked folder is left.
    if (!ok) RemoveTree(temp);
    g_jobOk = ok;
    g_jobError = error;
    Giveback(g_zip);
}

void JobEntry() {
    switch (g_job) {
    case Job::List: RunList(); break;
    case Job::Preview: RunPreview(); break;
    case Job::Install: RunInstall(); break;
    case Job::None: break;
    }
}

// ---- Scheduling (the menu thread) ----

bool Within(std::size_t index, std::size_t centre, int radius) {
    const long distance = static_cast<long>(index) - static_cast<long>(centre);
    return distance >= -radius && distance <= radius;
}

int SlotOf(std::size_t index) {
    for (int i = 0; i < kSlots; ++i) {
        if (g_slots[i].index == static_cast<int>(index)) return i;
    }
    return -1;
}

// The slot a new preview goes into: an empty one, else the least recently
// drawn that is not near the focus. -1 when every slot is wanted.
int FreeSlot() {
    int pick = -1;
    for (int i = 0; i < kSlots; ++i) {
        if (g_slots[i].index < 0) return i;
        if (Within(static_cast<std::size_t>(g_slots[i].index), g_focus, kReadAhead)) continue;
        if (pick < 0 || g_slots[i].used < g_slots[pick].used) pick = i;
    }
    return pick;
}

// The next preview to fetch: the focused theme, then its neighbours.
bool NextPreview(std::size_t& index, int& slot) {
    if (g_count == 0) return false;
    for (int step = 0; step <= 2 * kReadAhead; ++step) {
        const long offset = step == 0 ? 0 : (step % 2 == 1 ? (step + 1) / 2 : -(step / 2));
        const long candidate = static_cast<long>(g_focus) + offset;
        if (candidate < 0 || candidate >= static_cast<long>(g_count)) continue;
        const std::size_t c = static_cast<std::size_t>(candidate);
        if (g_failed[c] || SlotOf(c) >= 0) continue;
        const int free_slot = FreeSlot();
        if (free_slot < 0) return false;
        index = c;
        slot = free_slot;
        return true;
    }
    return false;
}

void StartJob(Job job) {
    g_job = job;
    if (!NetRunInBackground(JobEntry, true)) {
        // The network's thread is busy with another job (a cover): later.
        g_job = Job::None;
        return;
    }
    if (job == Job::Install) g_installAsked = false;
    if (job == Job::List) g_listAsked = false;
}

void FinishJob() {
    NetWaitForBackground();
    const Job done = g_job;
    g_job = Job::None;
    switch (done) {
    case Job::List:
        if (g_jobOk) {
            g_listState = ListState::Ready;
            std::memset(g_failed, 0, kThemeListMaxThemes);
            RefreshInstalled();
        } else {
            g_listState = ListState::Failed;
            g_listError = Short(g_jobError);
            logf("Themes: the list could not be fetched: %s\n", g_jobError.c_str());
        }
        break;
    case Job::Preview:
        if (g_jobOk) {
            g_slots[g_jobSlot].ready = true;
            g_slots[g_jobSlot].used = ++g_clock;
            g_texDirty = true;  // GX is the GUI thread's: it invalidates
        } else {
            g_slots[g_jobSlot] = Slot{};
            g_failed[g_jobIndex] = 1;
            logf("Themes: the preview of %s could not be fetched: %s\n", g_listing[g_jobIndex].folder,
                 g_jobError.c_str());
        }
        break;
    case Job::Install:
        g_installFinished = true;
        g_installFolder = g_listing[g_jobIndex].folder;
        g_installError = g_jobOk ? std::string() : (g_jobError.empty() ? std::string("it did not work") : g_jobError);
        if (g_jobOk) logf("Themes: %s is installed\n", g_installFolder.c_str());
        else logf("Themes: %s was not installed: %s\n", g_installFolder.c_str(), g_installError.c_str());
        g_cancel = false;
        RefreshInstalled();
        break;
    case Job::None: break;
    }
}

}  // namespace

bool Open(std::string& error) {
    if (g_open) return true;
    const std::size_t list_bytes = kThemeListMaxThemes * (sizeof(ThemeListing) + 2) + 64;
    if (!Ensure(g_thumbs, kSlots * kThumbBytes) || !Ensure(g_listBlock, list_bytes) || !ArrangeWork()) {
        error = "there is not enough free memory for the theme browser";
        Giveback(g_work);
        Giveback(g_listBlock);
        Giveback(g_thumbs);
        return false;
    }
    std::memset(g_listBlock.data, 0, list_bytes);
    g_listing = reinterpret_cast<ThemeListing*>(g_listBlock.data);
    g_failed = g_listBlock.data + kThemeListMaxThemes * sizeof(ThemeListing);
    g_installed = g_failed + kThemeListMaxThemes;
    g_count = 0;
    for (Slot& s : g_slots) s = Slot{};
    g_focus = 0;
    g_listState = ListState::Loading;
    g_listError.clear();
    g_listAsked = true;
    g_installAsked = false;
    g_installFinished = false;
    g_cancel = false;
    g_job = Job::None;
    g_open = true;
    logf("Themes: the browser's MEM2: %u KB (previews %u, list %u, work %u)\n",
         static_cast<unsigned>((g_thumbs.bytes + g_listBlock.bytes + g_work.bytes) >> 10),
         static_cast<unsigned>(g_thumbs.bytes >> 10), static_cast<unsigned>(g_listBlock.bytes >> 10),
         static_cast<unsigned>(g_work.bytes >> 10));
    return true;
}

void Close() {
    if (!g_open) return;
    if (g_job != Job::None) {
        g_cancel = true;
        NetCancelBackground();
        NetWaitForBackground();
        g_job = Job::None;
    }
    g_open = false;
    g_listing = nullptr;
    g_failed = g_installed = nullptr;
    g_count = 0;
    g_records.clear();
    g_folders.clear();
    for (Slot& s : g_slots) s = Slot{};
    // Last taken, first given back: only a block that is on top of the arena
    // can be. The rest stays held for the next visit.
    Giveback(g_zip);
    Giveback(g_work);
    Giveback(g_listBlock);
    Giveback(g_thumbs);
    logf("Themes: the browser's page is closed; MEM2 held on: %u KB\n",
         static_cast<unsigned>((g_thumbs.bytes + g_listBlock.bytes + g_work.bytes + g_zip.bytes) >> 10));
}

void Pump() {
    if (!g_open) return;
    if (g_job != Job::None) {
        if (NetBackgroundBusy()) return;
        FinishJob();
    }
    if (g_job != Job::None) return;
    if (g_installAsked) {
        g_jobIndex = g_installIndex;
        StartJob(Job::Install);
        return;
    }
    if (g_listAsked) {
        StartJob(Job::List);
        return;
    }
    if (g_listState != ListState::Ready) return;
    std::size_t index = 0;
    int slot = -1;
    if (!NextPreview(index, slot)) return;
    g_slots[slot] = Slot{static_cast<int>(index), false, 0};
    g_jobIndex = index;
    g_jobSlot = slot;
    StartJob(Job::Preview);
    // A job that could not start leaves the slot reserved for nothing.
    if (g_job == Job::None) g_slots[slot] = Slot{};
}

ListState ListStatus(std::string& error) {
    error = g_listError;
    return g_listState;
}

const ThemeListing* Listing(std::size_t& count) {
    count = g_listState == ListState::Ready ? g_count : 0;
    return g_listing;
}

const u8* Thumbnail(std::size_t index) {
    if (!g_open) return nullptr;
    if (g_texDirty) {
        g_texDirty = false;
        GX_InvalidateTexAll();
    }
    const int slot = SlotOf(index);
    if (slot < 0 || !g_slots[slot].ready) return nullptr;
    g_slots[slot].used = ++g_clock;
    return SlotData(slot);
}

bool ThumbnailReady(std::size_t index) {
    const int slot = g_open ? SlotOf(index) : -1;
    return slot >= 0 && g_slots[slot].ready;
}

void RetryList() {
    if (!g_open || g_listState != ListState::Failed) return;
    g_listState = ListState::Loading;
    g_listError.clear();
    g_listAsked = true;
}

void Focus(std::size_t index) { g_focus = index; }

bool PreviewFailed(std::size_t index) { return g_open && g_failed && index < g_count && g_failed[index] != 0; }

void RefreshInstalled() {
    g_records.clear();
    g_folders.clear();
    ReadTextFile(kRecordsPath, g_records, kRecordsCap);
    if (DIR* d = opendir(kThemesDir)) {
        while (const dirent* e = readdir(d)) {
            if (e->d_name[0] != '.') g_folders.insert(Lower(e->d_name));
        }
        closedir(d);
    }
    if (!g_installed) return;
    for (std::size_t i = 0; i < g_count; ++i) {
        std::string recorded;
        Installed state = Installed::No;
        if (g_folders.count(Lower(g_listing[i].folder))) {
            if (!theme_record_find(g_records, g_listing[i].folder, recorded)) state = Installed::Other;
            else state = recorded == g_listing[i].sha256 ? Installed::Current : Installed::Update;
        }
        g_installed[i] = static_cast<unsigned char>(state);
    }
}

Installed InstalledState(const ThemeListing& theme) {
    if (!g_installed || !g_listing) return Installed::No;
    const std::size_t index = static_cast<std::size_t>(&theme - g_listing);
    return index < g_count ? static_cast<Installed>(g_installed[index]) : Installed::No;
}

bool StartInstall(std::size_t index) {
    if (!g_open || g_listState != ListState::Ready || index >= g_count) return false;
    if (g_job == Job::Install || g_installAsked) return false;
    g_installIndex = index;
    g_installAsked = true;
    g_installFinished = false;
    g_cancel = false;
    g_stage = Stage::Download;
    g_done = 0;
    g_total = g_listing[index].size;
    return true;
}

bool InstallRunning() { return g_installAsked || g_job == Job::Install; }

InstallProgress CurrentInstallProgress() {
    InstallProgress p;
    p.stage = g_stage;
    p.done = g_done;
    p.total = g_total;
    return p;
}

void CancelInstall() {
    g_cancel = true;
    NetCancelBackground();
}

bool TakeInstall(std::string& folder, std::string& error) {
    if (!g_installFinished) return false;
    g_installFinished = false;
    folder = g_installFolder;
    error = g_installError;
    return true;
}

bool DeleteTheme(const std::string& folder, std::string& error) {
    if (!theme_folder_ok(folder)) {
        error = "that is not a theme folder";
        return false;
    }
    std::string records;
    ReadTextFile(kRecordsPath, records, kRecordsCap);
    std::string recorded;
    // Only what the browser installed: a theme the player made or copied
    // there is theirs to remove.
    if (!theme_record_find(records, folder, recorded)) {
        error = "this theme was not installed from here, so RiftWii leaves it alone";
        return false;
    }
    if (!RemoveTree(std::string(kThemesDir) + "/" + folder)) {
        error = "the theme's folder could not be removed";
        logf("Themes: %s could not be removed from the card\n", folder.c_str());
        return false;
    }
    if (!WriteRecords(theme_record_without(records, folder)))
        logf("Themes: %s was removed, but sd:/riftwii/themes/.browser.txt could not be written\n", folder.c_str());
    logf("Themes: %s is deleted\n", folder.c_str());
    RefreshInstalled();
    return true;
}

}  // namespace riftwii::wii::themes
