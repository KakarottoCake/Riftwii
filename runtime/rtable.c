/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "rtable.h"

uint32_t rt_crc32(const void* data, size_t length) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;
    for (i = 0; i < length; ++i) {
        uint32_t byte = p[i];
        int bit;
        crc ^= byte;
        for (bit = 0; bit < 8; ++bit) {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

size_t rt_table_bytes(uint32_t entry_count, uint32_t entry_bytes) {
    const size_t index_bytes = entry_count >= RT_INDEX_MIN_ENTRIES ?
                               (RT_INDEX_BUCKETS + 1) * sizeof(uint32_t) : 0;
    if (entry_bytes != RT_ENTRY_COMPACT && entry_bytes != RT_ENTRY_WIDE) return 0;
    if (entry_count > (SIZE_MAX - sizeof(rt_header) - index_bytes) / entry_bytes) return 0;
    return sizeof(rt_header) + (size_t)entry_count * entry_bytes + index_bytes;
}

static int rt_fits_compact(const rt_entry* entry) {
    return entry->vstart <= UINT64_MAX - entry->length &&
           ((entry->vstart + entry->length) >> 2) <= UINT32_MAX && entry->source <= UINT32_MAX;
}

uint32_t rt_entry_format(const rt_entry* entries, uint32_t entry_count) {
    uint32_t i;
    for (i = 0; i < entry_count; ++i) {
        if (!rt_fits_compact(&entries[i])) return RT_ENTRY_WIDE;
    }
    return RT_ENTRY_COMPACT;
}

/* Derive the bucket width from the covered span rather than absolute disc
 * addresses: a pack's redirects often occupy a small relocated window. */
static uint32_t rt_index_shift(const rt_compact_entry* entries, uint32_t count) {
    const uint32_t span = entries[count - 1].vend_words - entries[0].vend_words;
    if (span < RT_INDEX_BUCKETS) return 0;
    /* Both supported compilers use 32-bit unsigned int; PowerPC implements
     * this with cntlzw, avoiding a variable-length loop on every lookup. */
    return 26u - (uint32_t)__builtin_clz(span);
}

static uint32_t rt_wide_index_shift(const rt_entry* entries, uint32_t count) {
    const uint64_t first_end = entries[0].vstart + entries[0].length;
    const uint64_t last_end = entries[count - 1].vstart + entries[count - 1].length;
    const uint64_t span = last_end - first_end;
    if (span < RT_INDEX_BUCKETS) return 0;
    return 58u - (uint32_t)__builtin_clzll(span);
}

static uint32_t* rt_index(rt_header* table) {
    return (uint32_t*)((uint8_t*)table + sizeof(rt_header) + (size_t)table->entry_count * table->entry_bytes);
}

/* Loader-only index construction/checking uses the same keys as each hot
 * search, without converting compact entries into wide temporary structs. */
static uint64_t rt_index_key(const rt_header* table, uint32_t index) {
    const void* data = (const uint8_t*)table + sizeof(rt_header);
    if (table->entry_bytes == RT_ENTRY_COMPACT) return ((const rt_compact_entry*)data)[index].vend_words;
    {
        const rt_entry* entry = &((const rt_entry*)data)[index];
        return entry->vstart + entry->length;
    }
}

static uint32_t rt_table_index_shift(const rt_header* table) {
    const void* data = (const uint8_t*)table + sizeof(rt_header);
    if (table->entry_bytes == RT_ENTRY_COMPACT) {
        return rt_index_shift((const rt_compact_entry*)data, table->entry_count);
    }
    return rt_wide_index_shift((const rt_entry*)data, table->entry_count);
}

int rt_encode_entries(rt_header* table, const rt_entry* entries) {
    uint8_t* data = (uint8_t*)table + sizeof(rt_header);
    const size_t bytes = rt_table_bytes(table->entry_count, table->entry_bytes);
    uint32_t i;
    if (bytes == 0) return RT_ERR_SIZE;
    for (i = 0; i < table->entry_count; ++i) {
        const rt_entry* entry = &entries[i];
        if (table->entry_bytes == RT_ENTRY_COMPACT) {
            rt_compact_entry* compact = &((rt_compact_entry*)data)[i];
            if (!rt_fits_compact(entry) || entry->reserved != 0) return RT_ERR_ENTRY;
            compact->vend_words = (uint32_t)((entry->vstart + entry->length) >> 2);
            compact->vend_low = (uint8_t)((entry->vstart + entry->length) & 3);
            compact->source = (uint32_t)entry->source;
            compact->length = entry->length;
            compact->skip = entry->skip;
            compact->kind = entry->kind;
        } else {
            ((rt_entry*)data)[i] = *entry;
        }
    }
    if (table->entry_count >= RT_INDEX_MIN_ENTRIES) {
        const uint32_t shift = rt_table_index_shift(table);
        const uint64_t base = rt_index_key(table, 0);
        uint32_t* index = rt_index(table);
        uint32_t entry_index = 0;
        for (i = 0; i <= RT_INDEX_BUCKETS; ++i) {
            /* Subtract before shifting: adding a bucket boundary could wrap
             * near UINT64_MAX in a valid wide table. */
            while (entry_index < table->entry_count) {
                const uint64_t relative_end = rt_index_key(table, entry_index) - base;
                if ((relative_end >> shift) >= i) break;
                ++entry_index;
            }
            index[i] = entry_index;
        }
    }
    table->entries_crc = rt_crc32(data, bytes - sizeof(rt_header));
    return RT_OK;
}

rt_entry rt_read_entry(const rt_header* table, uint32_t index) {
    const void* data = (const uint8_t*)table + sizeof(rt_header);
    rt_entry entry;
    if (table->entry_bytes == RT_ENTRY_WIDE) return ((const rt_entry*)data)[index];
    {
        const rt_compact_entry* compact = &((const rt_compact_entry*)data)[index];
        const uint64_t end = ((uint64_t)compact->vend_words << 2) | compact->vend_low;
        entry.vstart = end - compact->length;
        entry.source = compact->source;
        entry.length = compact->length;
        entry.skip = compact->skip;
        entry.kind = compact->kind;
        /* Reuse the wide validator's reserved check for invalid low bits. */
        entry.reserved = compact->vend_low > 3;
    }
    return entry;
}

static int rt_entry_ok(const rt_entry* e) {
    if (e->length == 0) return 0;
    if (e->vstart > UINT64_MAX - e->length) return 0; /* range wraps */
    if (e->reserved != 0) return 0;
    switch (e->kind) {
    case RT_KIND_ZERO:
        return e->source == 0 && e->skip == 0;
    case RT_KIND_MEM:
    case RT_KIND_DISC:
        return e->skip == 0;
    case RT_KIND_SD:
    case RT_KIND_USB:
        return e->skip < RT_SECTOR_BYTES;
    default:
        return 0;
    }
}

int rt_validate(const rt_header* table, size_t available_bytes) {
    uint64_t previous_end = 0;
    size_t bytes;
    uint32_t i;
    if (table == 0 || available_bytes < sizeof(rt_header)) return RT_ERR_SIZE;
    if (table->magic != RT_MAGIC) return RT_ERR_MAGIC;
    if (table->version != RT_VERSION) return RT_ERR_VERSION;
    bytes = rt_table_bytes(table->entry_count, table->entry_bytes);
    if (bytes == 0 || bytes > available_bytes) return RT_ERR_SIZE;
    for (i = 0; i < table->entry_count; ++i) {
        const rt_entry entry = rt_read_entry(table, i);
        if (!rt_entry_ok(&entry)) return RT_ERR_ENTRY;
        if (i > 0 && previous_end > entry.vstart) return RT_ERR_ORDER;
        previous_end = entry.vstart + entry.length;
    }
    if (table->entry_count >= RT_INDEX_MIN_ENTRIES) {
        const void* data = (const uint8_t*)table + sizeof(rt_header);
        const size_t entry_bytes = (size_t)table->entry_count * table->entry_bytes;
        const uint32_t* index = (const uint32_t*)((const uint8_t*)data + entry_bytes);
        const uint32_t shift = rt_table_index_shift(table);
        const uint64_t base = rt_index_key(table, 0);
        uint32_t entry_index = 0;
        for (i = 0; i <= RT_INDEX_BUCKETS; ++i) {
            while (entry_index < table->entry_count) {
                const uint64_t relative_end = rt_index_key(table, entry_index) - base;
                if ((relative_end >> shift) >= i) break;
                ++entry_index;
            }
            if (index[i] != entry_index) return RT_ERR_ENTRY;
        }
    }
    if (rt_crc32((const uint8_t*)table + sizeof(rt_header), bytes - sizeof(rt_header)) != table->entries_crc) {
        return RT_ERR_CRC;
    }
    return RT_OK;
}

/* Search exclusive ends, as in the wide walker. Encoding the end rather
 * than the start removes the 64-bit add from every compact probe. Only an
 * equal word needs its two remaining bits, keeping the usual probe 32-bit. */
static uint32_t rt_compact_first_after(const rt_compact_entry* entries, uint32_t count, uint64_t offset) {
    const uint64_t words = offset >> 2;
    const uint32_t low = (uint32_t)offset & 3;
    uint32_t lo = 0;
    uint32_t hi = count;
    if (words > UINT32_MAX) return count;
    if (count >= RT_INDEX_MIN_ENTRIES) {
        const uint32_t base = entries[0].vend_words;
        if (words < base) return 0;
        {
            const uint32_t* index = (const uint32_t*)(entries + count);
            const uint32_t shift = rt_index_shift(entries, count);
            const uint32_t bucket = ((uint32_t)words - base) >> shift;
            if (bucket >= RT_INDEX_BUCKETS) return count;
            lo = index[bucket];
            hi = index[bucket + 1];
            /* The first end in the next bucket can cover this request. */
            if (hi < count) ++hi;
        }
    }
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        const rt_compact_entry* entry = &entries[mid];
        if (entry->vend_words < (uint32_t)words ||
            (entry->vend_words == (uint32_t)words && entry->vend_low <= low)) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

static uint32_t rt_wide_first_after(const rt_entry* entries, uint32_t count, uint64_t offset) {
    uint32_t lo = 0;
    uint32_t hi = count;
    if (count >= RT_INDEX_MIN_ENTRIES) {
        const uint64_t base = entries[0].vstart + entries[0].length;
        if (offset < base) return 0;
        {
            const uint32_t* index = (const uint32_t*)(entries + count);
            const uint32_t shift = rt_wide_index_shift(entries, count);
            const uint64_t bucket = (offset - base) >> shift;
            if (bucket >= RT_INDEX_BUCKETS) return count;
            lo = index[bucket];
            hi = index[bucket + 1];
            if (hi < count) ++hi;
        }
    }
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        if (entries[mid].vstart + entries[mid].length <= offset) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

static int rt_emit(rt_run* runs, uint32_t max_runs, uint32_t* run_count,
                   uint64_t vstart, uint64_t length, uint32_t kind, uint64_t source, uint32_t skip) {
    rt_run* r;
    if (*run_count >= max_runs) return RT_ERR_RUNS;
    r = &runs[*run_count];
    r->vstart = vstart;
    r->length = length;
    r->source = source;
    r->skip = skip;
    r->kind = kind;
    *run_count += 1;
    return RT_OK;
}

/* Wide entries retain direct field access. Decoding them through the compact
 * loop adds copies on every split, which costs more than the saved code. */
static int rt_lookup_wide(const rt_header* table, uint64_t offset, uint64_t length,
                          rt_run* runs, uint32_t max_runs, uint32_t* run_count) {
    const rt_entry* entries = (const rt_entry*)((const uint8_t*)table + sizeof(rt_header));
    const uint32_t count = table->entry_count;
    uint64_t cursor = offset;
    uint64_t end;
    uint32_t idx;
    *run_count = 0;
    if (length == 0) return RT_OK;
    if (offset > UINT64_MAX - length) return RT_ERR_RANGE;
    end = offset + length;
    idx = rt_wide_first_after(entries, count, offset);
    while (cursor < end) {
        int rc;
        if (idx < count && entries[idx].vstart <= cursor) {
            const rt_entry* e = &entries[idx];
            uint64_t entry_end = e->vstart + e->length;
            uint64_t run_end = entry_end < end ? entry_end : end;
            uint64_t delta = cursor - e->vstart;
            uint64_t source = 0;
            uint32_t skip = 0;
            switch (e->kind) {
            case RT_KIND_MEM:
            case RT_KIND_DISC:
                source = e->source + delta;
                break;
            case RT_KIND_SD:
            case RT_KIND_USB: {
                uint64_t byte_in_sectors = e->skip + delta;
                source = e->source + byte_in_sectors / RT_SECTOR_BYTES;
                skip = (uint32_t)(byte_in_sectors % RT_SECTOR_BYTES);
                break;
            }
            default:
                break;
            }
            rc = rt_emit(runs, max_runs, run_count, cursor, run_end - cursor, e->kind, source, skip);
            if (rc != RT_OK) return rc;
            cursor = run_end;
            idx += 1;
        } else {
            uint64_t gap_end = end;
            if (idx < count && entries[idx].vstart < end) gap_end = entries[idx].vstart;
            rc = rt_emit(runs, max_runs, run_count, cursor, gap_end - cursor, RT_KIND_PASSTHROUGH, 0, 0);
            if (rc != RT_OK) return rc;
            cursor = gap_end;
        }
    }
    return RT_OK;
}

int rt_lookup(const rt_header* table, uint64_t offset, uint64_t length,
              rt_run* runs, uint32_t max_runs, uint32_t* run_count) {
    const rt_compact_entry* entries = (const rt_compact_entry*)((const uint8_t*)table + sizeof(rt_header));
    const uint32_t count = table->entry_count;
    uint64_t cursor = offset;
    uint64_t end;
    uint32_t idx;
    if (table->entry_bytes == RT_ENTRY_WIDE) {
        return rt_lookup_wide(table, offset, length, runs, max_runs, run_count);
    }
    *run_count = 0;
    if (length == 0) return RT_OK;
    if (offset > UINT64_MAX - length) return RT_ERR_RANGE;
    end = offset + length;
    idx = rt_compact_first_after(entries, count, offset);
    while (cursor < end) {
        uint64_t start = end;
        uint64_t source = 0;
        uint32_t entry_length = 0;
        uint32_t kind = RT_KIND_PASSTHROUGH;
        uint32_t skip = 0;
        int rc;
        if (idx < count) {
            const rt_compact_entry* entry = &entries[idx];
            start = (((uint64_t)entry->vend_words << 2) | entry->vend_low) - entry->length;
            entry_length = entry->length;
            source = entry->source;
            kind = entry->kind;
            skip = entry->skip;
        }
        if (idx < count && start <= cursor) {
            const uint64_t entry_end = start + entry_length;
            const uint64_t run_end = entry_end < end ? entry_end : end;
            const uint64_t delta = cursor - start;
            switch (kind) {
            case RT_KIND_MEM:
            case RT_KIND_DISC:
                source += delta;
                break;
            case RT_KIND_SD:
            case RT_KIND_USB: {
                const uint64_t byte_in_sectors = skip + delta;
                source += byte_in_sectors / RT_SECTOR_BYTES;
                skip = (uint32_t)(byte_in_sectors % RT_SECTOR_BYTES);
                break;
            }
            default:
                source = 0;
                break;
            }
            rc = rt_emit(runs, max_runs, run_count, cursor, run_end - cursor, kind, source, skip);
            if (rc != RT_OK) return rc;
            cursor = run_end;
            idx += 1;
        } else {
            const uint64_t gap_end = start < end ? start : end;
            rc = rt_emit(runs, max_runs, run_count, cursor, gap_end - cursor, RT_KIND_PASSTHROUGH, 0, 0);
            if (rc != RT_OK) return rc;
            cursor = gap_end;
        }
    }
    return RT_OK;
}
