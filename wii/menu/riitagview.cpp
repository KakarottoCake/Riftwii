// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riitagview.hpp"

#include <ogc/cache.h>
#include <ogc/gx.h>
#include <zlib.h>

#include <cstddef>
#include <cstdint>

#include "httpmem.hpp"
#include "log.hpp"
#include "netsock.hpp"
#include "skin.hpp"
#include "tls.hpp"
#include "riftwii/canvas.hpp"
#include "riftwii/pngdecode.hpp"

namespace riftwii::wii::riitag {
namespace {

// The download's buffer holds the HTTP headers and the PNG.
constexpr std::size_t kHeaderRoom = 8192;
constexpr std::size_t kRawBytes = kRiiTagImageMaxBytes + kHeaderRoom;
// The PNG decoder's buffers for a 1200 x 450 picture: the joined image data
// (at most the file), the unfiltered rows and the RGBA result (w * h * 8).
constexpr std::size_t kScratchBytes = kRiiTagImageMaxBytes + std::size_t(1200) * 450 * 8 + 4096;
// The picture shrunk to the menu's size, as rows and then as GX's tiles.
constexpr std::size_t kShownBytes = std::size_t(kRiiTagShownWidth) * kRiiTagShownHeight * 4;

// A block of MEM2 that is kept between visits when it cannot be given back
// (the arena only moves one way: see skin::Mem2Release).
struct Block {
    u8* data = nullptr;
    std::size_t bytes = 0;
};
Block g_texture, g_work;

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

// The work block's parts.
struct WorkParts {
    u8* tls = nullptr;
    u8* raw = nullptr;
    u8* scratch = nullptr;
    u8* rows = nullptr;
};
WorkParts g_parts;

bool g_open = false;
// The download's thread writes g_jobOk and g_jobError while g_running is
// true; the menu thread reads them only once it is false.
volatile bool g_running = false;
bool g_jobOk = false;
std::string g_jobError;
std::string g_url;
TagState g_state = TagState::Idle;
std::string g_error;

// The PNG's zlib stream (pngdecode.hpp). zlib's own state (about 45 KB)
// comes from the heap, as the covers' does.
bool Inflate(const std::uint8_t* data, std::size_t size, std::uint8_t* out, std::size_t out_size) {
    z_stream z = {};
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

// On the network's thread: download, decode, shrink, into g_texture.
void RunDownload() {
    g_jobOk = false;
    g_jobError.clear();
    MemoryGet get;
    get.buffer = g_parts.raw;
    get.capacity = kRawBytes;
    get.tls_block = g_parts.tls;
    get.tls_bytes = TlsStream::memory_needed();
    if (!HttpGetInMemory(g_url, get, g_jobError, 20000)) {
        g_running = false;
        return;
    }
    if (get.body_length > kRiiTagImageMaxBytes) {
        g_jobError = "the tag is bigger than RiftWii takes (1.5 MB)";
        g_running = false;
        return;
    }
    const std::uint8_t* rgba = nullptr;
    std::uint32_t width = 0, height = 0;
    if (!decode_png_in(get.buffer + get.body_offset, get.body_length, &Inflate, kRiiTagImageMaxSide, g_parts.scratch,
                       kScratchBytes, rgba, width, height, g_jobError)) {
        g_running = false;
        return;
    }
    fit_rgba_into(rgba, static_cast<int>(width), static_cast<int>(height), g_parts.rows, kRiiTagShownWidth,
                  kRiiTagShownHeight);
    to_gx_rgba8(g_parts.rows, kRiiTagShownWidth, kRiiTagShownHeight, g_texture.data);
    DCFlushRange(g_texture.data, kShownBytes);
    logf("RiiTag: the tag's picture is %ux%u, %u bytes\n", static_cast<unsigned>(width),
         static_cast<unsigned>(height), static_cast<unsigned>(get.body_length));
    g_jobOk = true;
    g_running = false;
}

}  // namespace

bool Open(std::string& error) {
    if (g_open) return true;
    const std::size_t tls = (TlsStream::memory_needed() + 31) & ~std::size_t(31);
    const std::size_t work = tls + kRawBytes + kScratchBytes + kShownBytes;
    // The texture first: the work block, on top of it, is the one most
    // likely to be given back.
    if (!Ensure(g_texture, kShownBytes) || !Ensure(g_work, work)) {
        error = "there is not enough free memory to show the tag";
        Giveback(g_work);
        Giveback(g_texture);
        return false;
    }
    u8* at = g_work.data;
    g_parts.tls = at;
    at += tls;
    g_parts.raw = at;
    at += kRawBytes;
    g_parts.scratch = at;
    at += kScratchBytes;
    g_parts.rows = at;
    g_state = TagState::Idle;
    g_error.clear();
    g_open = true;
    logf("RiiTag: the tag page's MEM2: %u KB\n", static_cast<unsigned>((g_texture.bytes + g_work.bytes) >> 10));
    return true;
}

void Close() {
    if (!g_open) return;
    if (g_running) {
        NetCancelBackground();
        NetWaitForBackground();
        g_running = false;
    }
    g_open = false;
    g_state = TagState::Idle;
    // Last taken, first given back.
    Giveback(g_work);
    Giveback(g_texture);
}

bool Start(const std::string& id) {
    if (!g_open || g_running) return false;
    const std::string url = riitag_image_url(id);
    if (url.empty()) {
        g_state = TagState::Failed;
        g_error = "riitag_id in settings.txt is not a Discord ID";
        return true;
    }
    // Something else on the network's thread (a cover download): the page
    // asks again on its next turn.
    if (NetBackgroundBusy()) return false;
    g_url = url;
    g_running = true;
    // Never here and now: the page would hang for the download's 20 s
    // before it could draw Loading or take B.
    if (!NetRunInBackground(RunDownload, true, false)) {
        g_running = false;
        g_state = TagState::Failed;
        g_error = "the download could not start (no thread for it)";
        logf("RiiTag: %s\n", g_error.c_str());
        return true;
    }
    g_state = TagState::Loading;
    logf("RiiTag: downloading the tag's picture from riitag.t0g3pii.de\n");
    return true;
}

TagState Status(std::string& error) {
    if (g_state == TagState::Loading && !g_running) {
        g_state = g_jobOk ? TagState::Ready : TagState::Failed;
        // A refresh rewrote the texture where GX may still cache the last one.
        if (g_jobOk) GX_InvalidateTexAll();
        g_error = g_jobError;
        if (!g_jobOk) logf("RiiTag: the tag's picture could not be shown: %s\n", g_error.c_str());
    }
    error = g_error;
    return g_state;
}

const u8* Texture() {
    return g_open && g_state == TagState::Ready ? g_texture.data : nullptr;
}

}  // namespace riftwii::wii::riitag
