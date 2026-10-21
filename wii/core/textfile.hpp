// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>

namespace riftwii::wii {

// A text file of RiftWii's own (settings, history, choices, a language
// override, meta.xml) read whole. One larger than `cap` is not read: the
// menu's heap is about a megabyte, and a stray huge file must not take it
// down. False when the file is missing, unreadable or too large (the last
// is logged).
constexpr std::size_t kTextFileCap = 256u << 10;
bool ReadTextFile(const std::string& path, std::string& out, std::size_t cap = kTextFileCap);

// A file read whole, however large, as std::ifstream and rdbuf() read it
// before (the file streams brought libstdc++'s locale code into the DOL).
// For the callers that never had a cap: a pack's XML, a title list, a GCT.
// False only when the file cannot be opened; a read error keeps what came.
bool ReadWholeFile(const std::string& path, std::string& out);

// `size` bytes written to `path` in place of what it held. True only when
// the file opened, every byte was written and it closed cleanly.
bool WriteWholeFile(const std::string& path, const void* data, std::size_t size);

}  // namespace riftwii::wii
