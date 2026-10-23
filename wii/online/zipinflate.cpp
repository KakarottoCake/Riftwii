// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "zipinflate.hpp"

#include <zlib.h>

namespace riftwii::wii {
namespace {

// zlib asks for its state and window from the heap; here it is lent the
// block instead, handed out once per inflate.
struct ZlibHeap {
    std::uint8_t* base = nullptr;
    std::size_t used = 0;
};
ZlibHeap g_heap;
std::uint8_t* g_chunk = nullptr;

voidpf Alloc(voidpf, uInt items, uInt size) {
    const std::size_t bytes = (static_cast<std::size_t>(items) * size + 7) & ~std::size_t(7);
    if (!g_heap.base || bytes > kZipZlibBytes - g_heap.used) return Z_NULL;
    voidpf at = g_heap.base + g_heap.used;
    g_heap.used += bytes;
    return at;
}
void Free(voidpf, voidpf) {}

}  // namespace

void ZipInflateUse(std::uint8_t* zlib_block, std::uint8_t* chunk_block) {
    g_heap.base = zlib_block;
    g_chunk = chunk_block;
}

bool ZipInflate(const std::uint8_t* in, std::size_t in_size, std::uint64_t expected, const ZipSink& sink) {
    if (!g_heap.base || !g_chunk) return false;
    g_heap.used = 0;
    z_stream z = {};
    z.zalloc = Alloc;
    z.zfree = Free;
    if (inflateInit2(&z, -15) != Z_OK) return false;
    z.next_in = const_cast<Bytef*>(in);
    z.avail_in = static_cast<uInt>(in_size);
    std::uint64_t total = 0;
    bool ok = false;
    for (;;) {
        z.next_out = g_chunk;
        z.avail_out = static_cast<uInt>(kZipChunkBytes);
        const int result = inflate(&z, Z_NO_FLUSH);
        const std::size_t made = kZipChunkBytes - z.avail_out;
        if (total + made > expected) break;  // more than the zip said
        if (made > 0 && !sink(g_chunk, made)) break;
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

}  // namespace riftwii::wii
