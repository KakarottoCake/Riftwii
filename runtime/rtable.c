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

size_t rt_table_bytes(uint32_t entry_count) {
    return sizeof(rt_header) + (size_t)entry_count * sizeof(rt_entry);
}

const rt_entry* rt_entries(const rt_header* table) {
    return (const rt_entry*)((const uint8_t*)table + sizeof(rt_header));
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
        return e->skip < RT_SECTOR_BYTES;
    default:
        return 0;
    }
}

int rt_validate(const rt_header* table, size_t available_bytes) {
    const rt_entry* entries;
    uint32_t i;
    if (table == 0 || available_bytes < sizeof(rt_header)) return RT_ERR_SIZE;
    if (table->magic != RT_MAGIC) return RT_ERR_MAGIC;
    if (table->version != RT_VERSION) return RT_ERR_VERSION;
    if (table->reserved != 0) return RT_ERR_ENTRY;
    /* entry_count * sizeof(rt_entry) cannot overflow size_t on any target
       we build for once entry_count is bounded by the bytes available. */
    if (table->entry_count > (available_bytes - sizeof(rt_header)) / sizeof(rt_entry)) return RT_ERR_SIZE;
    entries = rt_entries(table);
    for (i = 0; i < table->entry_count; ++i) {
        if (!rt_entry_ok(&entries[i])) return RT_ERR_ENTRY;
        if (i > 0) {
            const rt_entry* prev = &entries[i - 1];
            if (prev->vstart + prev->length > entries[i].vstart) return RT_ERR_ORDER;
        }
    }
    if (rt_crc32(entries, (size_t)table->entry_count * sizeof(rt_entry)) != table->entries_crc) return RT_ERR_CRC;
    return RT_OK;
}

/* Index of the first entry whose end lies beyond `offset`, or entry_count. */
static uint32_t rt_first_after(const rt_entry* entries, uint32_t count, uint64_t offset) {
    uint32_t lo = 0;
    uint32_t hi = count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
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

int rt_lookup(const rt_header* table, uint64_t offset, uint64_t length,
              rt_run* runs, uint32_t max_runs, uint32_t* run_count) {
    const rt_entry* entries = rt_entries(table);
    const uint32_t count = table->entry_count;
    uint64_t cursor = offset;
    uint64_t end;
    uint32_t idx;
    *run_count = 0;
    if (length == 0) return RT_OK;
    if (offset > UINT64_MAX - length) return RT_ERR_RANGE;
    end = offset + length;
    idx = rt_first_after(entries, count, offset);
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
            case RT_KIND_SD: {
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
