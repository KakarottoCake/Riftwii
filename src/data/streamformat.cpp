// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/streamformat.hpp"

#include <cerrno>
#include <cstdlib>

#include "riftwii/checkedalloc.hpp"

namespace riftwii {

int format_to_stream(FILE* stream, const char* format, va_list args) {
    // Crash-screen lines fit here, so printing after a heap failure still
    // works. Each caller owns its buffer, including concurrent log calls.
    char local[512];
    va_list copy;
    va_copy(copy, args);
    int length = std::vsnprintf(local, sizeof local, format, copy);
    va_end(copy);
    if (length < 0) return -1;

    char* text = local;
    if (static_cast<std::size_t>(length) >= sizeof local) {
        const std::size_t capacity = static_cast<std::size_t>(length) + 1;
        if (!can_allocate(capacity)) {
            errno = ENOMEM;
            return -1;
        }
        text = static_cast<char*>(std::malloc(capacity));
        if (text == nullptr) {
            errno = ENOMEM;
            return -1;
        }
        va_copy(copy, args);
        length = std::vsnprintf(text, capacity, format, copy);
        va_end(copy);
        if (length < 0 || static_cast<std::size_t>(length) >= capacity) {
            std::free(text);
            return -1;
        }
    }

    // One fwrite keeps each message together under stdio's stream lock.
    // Use the count, not strlen: printf can produce embedded NUL bytes.
    const std::size_t bytes = static_cast<std::size_t>(length);
    const bool written = bytes == 0 || std::fwrite(text, 1, bytes, stream) == bytes;
    if (text != local) std::free(text);
    return written ? length : -1;
}

}  // namespace riftwii
