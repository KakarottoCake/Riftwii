// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The plugin's Zstandard calls use the loader's decoder through rwp_host.
// Kept apart from the libc replacements so host tests can use this shim.
#include "rwpruntime.hpp"

#include "zstd.h"

using riftwii::plugin::host;

extern "C" {

size_t ZSTD_decompress(void* to, size_t to_bytes, const void* from, size_t from_bytes) {
    return host().zstd_decompress(to, to_bytes, from, from_bytes);
}

unsigned long long ZSTD_getFrameContentSize(const void* from, size_t from_bytes) {
    return host().zstd_frame_size(from, from_bytes);
}

unsigned ZSTD_isError(size_t code) { return host().zstd_is_error(code); }

const char* ZSTD_getErrorName(size_t code) { return host().zstd_error_name(code); }

ZSTD_ErrorCode ZSTD_getErrorCode(size_t result) {
    // The host returns the vendored decoder's size_t unchanged. Its errors
    // are negated enum values, so no plugin API extension is needed.
    if (!host().zstd_is_error(result)) return ZSTD_error_no_error;
    return static_cast<ZSTD_ErrorCode>(size_t(0) - result);
}

// A decoding context of its own is only a speed-up: each call of the
// loader's one-shot decoder makes its own. The pointer stands for "a
// context" so a caller's null check holds; it is never looked into.
namespace {
char g_dctx_token;
}

ZSTD_DCtx* ZSTD_createDCtx() { return reinterpret_cast<ZSTD_DCtx*>(&g_dctx_token); }

size_t ZSTD_freeDCtx(ZSTD_DCtx*) { return 0; }

size_t ZSTD_decompressDCtx(ZSTD_DCtx*, void* to, size_t to_bytes, const void* from, size_t from_bytes) {
    return host().zstd_decompress(to, to_bytes, from, from_bytes);
}

}  // extern "C"
