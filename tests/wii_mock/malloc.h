// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdlib>

// libogc's memalign, freed with free as on the Wii. This header stands in
// for the host's <malloc.h> wherever tests/wii_mock is on the include path
// (macOS has none; glibc declares memalign only there, so the two never
// meet in one file).
inline void* memalign(std::size_t alignment, std::size_t size) {
    void* memory = nullptr;
    if (posix_memalign(&memory, alignment, size) != 0) return nullptr;
    return memory;
}
