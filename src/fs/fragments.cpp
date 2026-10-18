// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// place_on_fragments (riftwii/redirect.hpp), on its own: the gamecube
// plugin links it without the rest of redirect.cpp, whose table builder
// catches std::bad_alloc and so cannot be built without exceptions
// (docs/PLUGINS.md, "Stage (b)").
#include <algorithm>
#include <limits>

#include "riftwii/redirect.hpp"

namespace riftwii {

bool place_on_fragments(const std::vector<Fragment>& fragments, std::uint64_t file_offset,
                        std::uint64_t length, std::vector<PlacedRun>& out, std::string& error, std::uint32_t kind) {
    std::vector<PlacedRun> runs;
    std::uint64_t frag_start = 0;  // file offset where the current fragment begins
    std::uint64_t remaining = length;
    std::uint64_t cursor = file_offset;
    for (const Fragment& f : fragments) {
        if (f.sector_count == 0) {
            error = "empty fragment";
            return false;
        }
        if (f.sector_count > std::numeric_limits<std::uint64_t>::max() / RT_SECTOR_BYTES) {
            error = "fragment too large";
            return false;
        }
        const std::uint64_t frag_bytes = f.sector_count * RT_SECTOR_BYTES;
        if (frag_start > std::numeric_limits<std::uint64_t>::max() - frag_bytes) {
            error = "fragment list too large";
            return false;
        }
        const std::uint64_t frag_end = frag_start + frag_bytes;
        if (remaining > 0 && cursor < frag_end) {
            const std::uint64_t within = cursor - frag_start;
            const std::uint64_t take = std::min(remaining, frag_end - cursor);
            PlacedRun r;
            r.kind = kind;
            r.length = take;
            r.source = f.sector + within / RT_SECTOR_BYTES;
            r.skip = static_cast<std::uint32_t>(within % RT_SECTOR_BYTES);
            runs.push_back(r);
            cursor += take;
            remaining -= take;
        }
        frag_start = frag_end;
        if (remaining == 0) break;
    }
    if (remaining != 0) {
        error = "range extends past the fragment list";
        return false;
    }
    out = std::move(runs);
    error.clear();
    return true;
}

}  // namespace riftwii
