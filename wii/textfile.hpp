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

}  // namespace riftwii::wii
