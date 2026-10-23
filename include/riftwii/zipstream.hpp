// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "riftwii/zipread.hpp"

namespace riftwii {

// Streams the first entry of a file, for GameTDB's single-entry database.
// Unlike zip_extract, this never holds the packed archive in memory. Only
// stored/raw-deflate entries with sizes and CRC in the local header are
// supported; encryption, data descriptors and ZIP64 are refused.
// Both the archive and the unpacked entry have caller-supplied limits.
// Checks the exact packed/unpacked lengths and CRC before returning true.
// The sink may already have seen bytes on failure: discard its results.
// Progress is in [0, 1]; either callback may return false to cancel.
bool zip_stream_first(const std::string& path, std::uint64_t max_archive_size,
                      std::uint64_t max_unpacked_size, const ZipSink& sink, std::string& error,
                      const std::function<bool(double)>& progress = {});

}  // namespace riftwii
