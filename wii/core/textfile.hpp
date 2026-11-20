// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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
// For the callers that never had a cap: a pack's XML, a title list, a GCT,
// a disc file's replacement (megabytes, so read straight into its bytes).
// False only when the file cannot be opened; a read error keeps what came.
bool ReadWholeFile(const std::string& path, std::string& out);
bool ReadWholeFile(const std::string& path, std::vector<std::uint8_t>& out);

// `size` bytes written to `path` in place of what it held. True only when
// the file opened, every byte was written and it closed cleanly. A power cut
// or a full card halfway leaves the file cut short: for caches, logs and
// files RiftWii can make again. A player's own file uses ReplaceWholeFile.
bool WriteWholeFile(const std::string& path, const void* data, std::size_t size);

// `size` bytes saved as `path` without ever leaving it half-written. They go
// to `path.part` first; once that is whole, the old file steps aside as
// `path.bak` and the new one takes its name, and the .bak is removed. Each
// step that fails leaves the old copy readable: a failed write removes the
// .part; a failed last rename puts the old copy back at `path` and keeps the
// whole new one as `path.part`. A power cut between the two renames leaves
// only `path.bak`, which ReadReplacedFile reads. With no `path`, a .bak left
// by an earlier cut-off save stays until this save has succeeded.
bool ReplaceWholeFile(const std::string& path, const void* data, std::size_t size);

// ReadTextFile of `path`, or of `path.bak` when `path` is missing (a save
// cut off between ReplaceWholeFile's two renames).
bool ReadReplacedFile(const std::string& path, std::string& out, std::size_t cap = kTextFileCap);

}  // namespace riftwii::wii
