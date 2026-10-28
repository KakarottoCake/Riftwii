// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/streamformat.hpp"

#include <cstdarg>
#include <cstdio>

// The Wii link redirects FILE formatting here, including calls from the
// prebuilt libraries. This leaves only snprintf's formatter in the DOL;
// flushing and log batching stay with their existing callers.
extern "C" int __wrap_vfprintf(FILE* stream, const char* format, va_list args) {
    return riftwii::format_to_stream(stream, format, args);
}

extern "C" int __wrap_vprintf(const char* format, va_list args) {
    return riftwii::format_to_stream(stdout, format, args);
}

extern "C" int __wrap_fprintf(FILE* stream, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int length = riftwii::format_to_stream(stream, format, args);
    va_end(args);
    return length;
}

extern "C" int __wrap_printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int length = riftwii::format_to_stream(stdout, format, args);
    va_end(args);
    return length;
}
