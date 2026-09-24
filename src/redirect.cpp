// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/redirect.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace riftwii {
namespace {

std::uint64_t linear_source(const rt_entry& e) {
    switch (e.kind) {
    case RT_KIND_SD:
    case RT_KIND_USB: return e.source * RT_SECTOR_BYTES + e.skip;
    case RT_KIND_MEM:
    case RT_KIND_DISC: return e.source;
    default: return 0;
    }
}

bool push_entry(std::vector<rt_entry>& entries, std::uint64_t vstart, std::uint64_t length,
                std::uint32_t kind, std::uint64_t source, std::uint32_t skip, std::string& error) {
    if (length == 0) return true;
    if (vstart > std::numeric_limits<std::uint64_t>::max() - length) {
        error = "redirect entry wraps the address space";
        return false;
    }
    if (length > std::numeric_limits<std::uint32_t>::max()) {
        error = "redirect entry is 4 GiB or longer";
        return false;
    }
    rt_entry e;
    std::memset(&e, 0, sizeof(e));
    e.vstart = vstart;
    e.length = static_cast<std::uint32_t>(length);
    e.kind = static_cast<std::uint8_t>(kind);
    e.source = source;
    e.skip = static_cast<std::uint16_t>(skip);
    entries.push_back(e);
    return true;
}

}  // namespace

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

bool build_redirect_table(const std::vector<VirtualFileLayout>& files, const ExternalPlacer& place,
                          std::uint32_t sdio_fd, std::uint64_t tag,
                          std::vector<std::uint8_t>& out, std::string& error) {
    try {
        std::vector<rt_entry> entries;
        for (std::size_t fi = 0; fi < files.size(); ++fi) {
            const VirtualFileLayout& layout = files[fi];
            if (layout.file == nullptr) {
                error = "redirect layout " + std::to_string(fi) + " has no file";
                return false;
            }
            const std::uint64_t size = layout.file->size();
            if (layout.virtual_offset > std::numeric_limits<std::uint64_t>::max() - size) {
                error = "redirect layout " + std::to_string(fi) + " wraps the address space";
                return false;
            }
            const std::vector<FlatExtent> flat = layout.file->flatten();
            for (const FlatExtent& x : flat) {
                const std::uint64_t vstart = layout.virtual_offset + x.dest;
                switch (x.kind) {
                case FlatExtent::Kind::Original: {
                    const std::uint64_t disc_pos = layout.original_offset + x.source_offset;
                    if (disc_pos == vstart) break;  // untouched and in place: pass through
                    if (!push_entry(entries, vstart, x.length, RT_KIND_DISC, disc_pos, 0, error)) return false;
                    break;
                }
                case FlatExtent::Kind::Zero:
                    if (!push_entry(entries, vstart, x.length, RT_KIND_ZERO, 0, 0, error)) return false;
                    break;
                case FlatExtent::Kind::External: {
                    std::vector<PlacedRun> runs;
                    if (!place(x.source, x.source_offset, x.length, runs, error)) {
                        error = "cannot place external bytes for redirect layout " + std::to_string(fi) + ": " + error;
                        return false;
                    }
                    std::uint64_t placed = 0;
                    for (const PlacedRun& r : runs) {
                        const bool sectors = r.kind == RT_KIND_SD || r.kind == RT_KIND_USB;
                        if (r.kind != RT_KIND_MEM && !sectors) {
                            error = "placement returned an unsupported kind";
                            return false;
                        }
                        if (sectors && r.skip >= RT_SECTOR_BYTES) {
                            error = "placement returned an SD skip past the sector";
                            return false;
                        }
                        if (r.length == 0 || r.length > x.length - placed) {
                            error = "placement length does not match the extent";
                            return false;
                        }
                        if (!push_entry(entries, vstart + placed, r.length, r.kind, r.source,
                                        sectors ? r.skip : 0, error)) return false;
                        placed += r.length;
                    }
                    if (placed != x.length) {
                        error = "placement did not cover the whole extent";
                        return false;
                    }
                    break;
                }
                }
            }
        }
        std::stable_sort(entries.begin(), entries.end(), [](const rt_entry& a, const rt_entry& b) {
            return a.vstart < b.vstart;
        });
        // Overlap check, then coalesce neighbours that continue the same source.
        std::vector<rt_entry> merged;
        for (const rt_entry& e : entries) {
            if (!merged.empty()) {
                rt_entry& p = merged.back();
                const std::uint64_t pend = p.vstart + p.length;
                if (pend > e.vstart) {
                    error = "redirect entries overlap at virtual offset " + std::to_string(e.vstart);
                    return false;
                }
                const bool contiguous = pend == e.vstart && p.kind == e.kind &&
                                        (p.kind == RT_KIND_ZERO || linear_source(p) + p.length == linear_source(e));
                if (contiguous) {
                    p.length += e.length;
                    continue;
                }
            }
            merged.push_back(e);
        }
        if (merged.size() > std::numeric_limits<std::uint32_t>::max()) {
            error = "too many redirect entries";
            return false;
        }
        rt_header header;
        std::memset(&header, 0, sizeof(header));
        header.magic = RT_MAGIC;
        header.version = RT_VERSION;
        header.entry_count = static_cast<std::uint32_t>(merged.size());
        header.sdio_fd = sdio_fd;
        header.tag = tag;
        header.entries_crc = rt_crc32(merged.data(), merged.size() * sizeof(rt_entry));
        std::vector<std::uint8_t> bytes(rt_table_bytes(header.entry_count));
        std::memcpy(bytes.data(), &header, sizeof(header));
        if (!merged.empty()) {
            std::memcpy(bytes.data() + sizeof(header), merged.data(), merged.size() * sizeof(rt_entry));
        }
        const int rc = rt_validate(reinterpret_cast<const rt_header*>(bytes.data()), bytes.size());
        if (rc != RT_OK) {
            error = "built redirect table failed validation (" + std::to_string(rc) + ")";
            return false;
        }
        out = std::move(bytes);
        error.clear();
        return true;
    } catch (const std::bad_alloc&) {
        error = "allocation failure";
        return false;
    }
}

}  // namespace riftwii
