// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// A C++ plugin's runtime (rwpruntime.hpp): every name the C library and
// libstdc++ would give its link, on RiftWii's table. Linked into each C++
// plugin (Makefile.plugin), so the plugin has no undefined symbol and calls
// RiftWii only through rwp_host (docs/PLUGINS.md, "Stage (b)").
#include "rwpruntime.hpp"

#include <sys/stat.h>

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

#include "zstd.h"

// std::string's members in the plugin itself, built here without
// exceptions: libstdc++ declares them extern (its own copy was built with
// exceptions and would bring its unwinding tables, which a plugin may not
// have).
template class std::basic_string<char>;

namespace riftwii::plugin {
namespace {

const rwp_host* g_host = nullptr;
// escape()'s way back: __builtin_setjmp's five words, and whether a launch
// is inside the window where it may be used.
void* g_escape_buffer[5];
bool g_escape_armed = false;
Escape g_escape;

// A trap: a program exception, which RiftWii's crash screen reports with
// the plugin's name and offset (wii/core/crash.cpp).
[[noreturn]] void stop() {
    for (;;) __builtin_trap();
}

}  // namespace

bool start_runtime(const rwp_host* table) {
    if (table == nullptr || table->api_version != RWP_API_VERSION || !RWP_HOST_HAS(table, zstd_error_name)) return false;
    g_host = table;
    return true;
}

const rwp_host& host() { return *g_host; }

void** escape_buffer() { return g_escape_buffer; }

void arm_escape() {
    g_escape = Escape{};
    g_escape_armed = true;
}

void disarm_escape() { g_escape_armed = false; }

Escape escape_taken() { return g_escape; }

void escape(std::int32_t result, const char* why) {
    if (g_escape_armed) {
        g_escape_armed = false;
        g_escape.result = result;
        g_escape.why = why;
        __builtin_longjmp(g_escape_buffer, 1);
    }
    if (g_host != nullptr) g_host->log("stopping: %s (outside a launch, nothing can be given back)", why);
    stop();
}

}  // namespace riftwii::plugin

using riftwii::plugin::escape;
using riftwii::plugin::host;

// ---- Memory: RiftWii's heap (MEM1, then MEM2 when it is full) ----

namespace {

void* allocate(std::size_t bytes) {
    // The heap refuses 0 bytes; C++ wants a distinct block for them.
    void* block = host().mem1_alloc(bytes == 0 ? 1 : static_cast<std::uint32_t>(bytes));
    if (block == nullptr) escape(RWP_LAUNCH_NO_MEMORY, "RiftWii's memory is full");
    return block;
}

}  // namespace

void* operator new(std::size_t bytes) { return allocate(bytes); }
void* operator new[](std::size_t bytes) { return allocate(bytes); }
void operator delete(void* block) noexcept { host().mem1_free(block); }
void operator delete[](void* block) noexcept { host().mem1_free(block); }
void operator delete(void* block, std::size_t) noexcept { host().mem1_free(block); }
void operator delete[](void* block, std::size_t) noexcept { host().mem1_free(block); }

// ---- What libstdc++ would throw: nothing can be thrown here ----

namespace std {

void __throw_bad_alloc() { escape(RWP_LAUNCH_NO_MEMORY, "RiftWii's memory is full"); }
void __throw_bad_array_new_length() { escape(RWP_LAUNCH_NO_MEMORY, "an array larger than memory was asked for"); }
void __throw_length_error(const char* what) { escape(RWP_LAUNCH_FAILED, what); }
void __throw_logic_error(const char* what) { escape(RWP_LAUNCH_FAILED, what); }
void __throw_out_of_range(const char* what) { escape(RWP_LAUNCH_FAILED, what); }
void __throw_out_of_range_fmt(const char* what, ...) { escape(RWP_LAUNCH_FAILED, what); }
void __throw_invalid_argument(const char* what) { escape(RWP_LAUNCH_FAILED, what); }
void __throw_bad_function_call() { escape(RWP_LAUNCH_FAILED, "an empty std::function was called"); }

}  // namespace std

extern "C" {

void __cxa_pure_virtual() { escape(RWP_LAUNCH_FAILED, "a pure virtual function was called"); }

void abort() { escape(RWP_LAUNCH_FAILED, "abort() was called"); }

// ---- The C library's memory and string functions ----

void* memcpy(void* to, const void* from, size_t bytes) {
    return host().move_bytes(to, from, static_cast<std::uint32_t>(bytes));
}

void* memmove(void* to, const void* from, size_t bytes) {
    return host().move_bytes(to, from, static_cast<std::uint32_t>(bytes));
}

void* memset(void* to, int value, size_t bytes) {
    return host().fill_bytes(to, value, static_cast<std::uint32_t>(bytes));
}

int memcmp(const void* a, const void* b, size_t bytes) {
    const auto* left = static_cast<const unsigned char*>(a);
    const auto* right = static_cast<const unsigned char*>(b);
    for (size_t i = 0; i < bytes; ++i) {
        if (left[i] != right[i]) return left[i] < right[i] ? -1 : 1;
    }
    return 0;
}

void* memchr(const void* where, int value, size_t bytes) {
    const auto* bytes_at = static_cast<const unsigned char*>(where);
    for (size_t i = 0; i < bytes; ++i) {
        if (bytes_at[i] == static_cast<unsigned char>(value)) return const_cast<unsigned char*>(bytes_at + i);
    }
    return nullptr;
}

size_t strlen(const char* text) {
    size_t length = 0;
    while (text[length] != '\0') ++length;
    return length;
}

int vsnprintf(char* out, size_t bytes, const char* format, va_list args) {
    return host().format(out, static_cast<std::uint32_t>(bytes), format, args);
}

int snprintf(char* out, size_t bytes, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(out, bytes, format, args);
    va_end(args);
    return written;
}

// ---- Files: the C library's calls on rwp_host's streams ----
// A FILE* here is the loader's own FILE, which the plugin only passes back.

FILE* fopen(const char* path, const char* mode) { return static_cast<FILE*>(host().stream_open(path, mode)); }

int fclose(FILE* file) { return host().stream_close(file) == 0 ? 0 : EOF; }

size_t fread(void* buffer, size_t size, size_t count, FILE* file) {
    if (size == 0 || count == 0) return 0;
    return host().stream_read(file, buffer, static_cast<std::uint32_t>(size * count)) / size;
}

size_t fwrite(const void* buffer, size_t size, size_t count, FILE* file) {
    if (size == 0 || count == 0) return 0;
    return host().stream_write(file, buffer, static_cast<std::uint32_t>(size * count)) / size;
}

int fseek(FILE* file, long offset, int whence) { return host().stream_seek(file, offset, whence); }

long ftell(FILE* file) { return static_cast<long>(host().stream_tell(file)); }

int fseeko(FILE* file, off_t offset, int whence) { return host().stream_seek(file, offset, whence); }

off_t ftello(FILE* file) { return static_cast<off_t>(host().stream_tell(file)); }

int stat(const char* path, struct stat* out) {
    rwp_path_info info;
    if (host().path_info(path, &info) != 0) return -1;
    std::memset(out, 0, sizeof(*out));
    out->st_mode = info.is_directory ? (S_IFDIR | 0777) : (S_IFREG | 0666);
    out->st_size = static_cast<off_t>(info.size);
    return 0;
}

int mkdir(const char* path, mode_t) { return host().make_directory(path); }

// ---- Zstandard: RiftWii's own decoder ----

size_t ZSTD_decompress(void* to, size_t to_bytes, const void* from, size_t from_bytes) {
    return host().zstd_decompress(to, to_bytes, from, from_bytes);
}

unsigned long long ZSTD_getFrameContentSize(const void* from, size_t from_bytes) {
    return host().zstd_frame_size(from, from_bytes);
}

unsigned ZSTD_isError(size_t code) { return host().zstd_is_error(code); }

const char* ZSTD_getErrorName(size_t code) { return host().zstd_error_name(code); }

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
