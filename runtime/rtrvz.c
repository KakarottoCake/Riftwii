/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "rtrvz.h"

#define JUNK_BYTES (4u * RTRVZ_JUNK_WORDS)
#define JUNK_BLOCK 0x8000u

static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void junk_advance(rtrvz_junk* j) {
    uint32_t i;
    for (i = 0; i < 32u; ++i) j->buf[i] ^= j->buf[i + RTRVZ_JUNK_WORDS - 32u];
    for (i = 32u; i < RTRVZ_JUNK_WORDS; ++i) j->buf[i] ^= j->buf[i - 32u];
}

void rtrvz_junk_seed(rtrvz_junk* j, const uint8_t seed[RTRVZ_SEED_BYTES]) {
    uint32_t i;
    for (i = 0; i < 17u; ++i) j->buf[i] = be32(seed + 4u * i);
    for (i = 17u; i < RTRVZ_JUNK_WORDS; ++i) {
        j->buf[i] = (j->buf[i - 17u] << 23) ^ (j->buf[i - 16u] >> 9) ^ j->buf[i - 1u];
    }
    for (i = 0; i < 4u; ++i) junk_advance(j);
    j->pos = 0;
}

void rtrvz_junk_skip(rtrvz_junk* j, uint32_t bytes) {
    while (bytes >= JUNK_BYTES - j->pos) {
        bytes -= JUNK_BYTES - j->pos;
        junk_advance(j);
        j->pos = 0;
    }
    j->pos += bytes;
}

void rtrvz_junk_out(rtrvz_junk* j, uint8_t* dst, uint32_t len) {
    /* A word gives bits 31..24, 23..18 (sic), 15..8 and 7..0. */
    static const uint8_t shift[4] = {24u, 18u, 8u, 0u};
    uint32_t i;
    for (i = 0; i < len; ++i) {
        if (j->pos == JUNK_BYTES) {
            junk_advance(j);
            j->pos = 0;
        }
        dst[i] = (uint8_t)(j->buf[j->pos >> 2] >> shift[j->pos & 3u]);
        ++j->pos;
    }
}

int rtrvz_exception_bytes(const uint8_t* in, uint32_t in_len, uint32_t lists, int pad4, uint32_t* out) {
    uint32_t at = 0;
    uint32_t l;
    for (l = 0; l < lists; ++l) {
        uint32_t count;
        if (in_len - at < 2u) return RTRVZ_ERR_TRUNCATED;
        count = ((uint32_t)in[at] << 8) | in[at + 1u];
        at += 2u;
        /* 22 bytes each: a 16-bit offset and a SHA-1. */
        if ((in_len - at) / 22u < count) return RTRVZ_ERR_TRUNCATED;
        at += 22u * count;
    }
    if (pad4 && (at & 3u) != 0) {
        uint32_t pad = 4u - (at & 3u);
        if (in_len - at < pad) return RTRVZ_ERR_TRUNCATED;
        at += pad;
    }
    *out = at;
    return RTRVZ_OK;
}

static void copy_bytes(uint8_t* d, const uint8_t* s, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; ++i) d[i] = s[i];
}

int rtrvz_unpack(const uint8_t* in, uint32_t in_len, uint64_t data_offset, uint32_t skip,
                 uint8_t* dst, uint32_t len, rtrvz_junk* scratch) {
    const uint64_t end = (uint64_t)skip + len;
    uint64_t out = 0; /* unpacked bytes before the current record */
    uint32_t at = 0;
    while (out < end) {
        uint32_t size;
        int junk;
        uint64_t from, to;
        if (in_len - at < 4u) return RTRVZ_ERR_SHORT;
        size = be32(in + at);
        at += 4u;
        junk = (size & 0x80000000u) != 0;
        size &= 0x7FFFFFFFu;
        from = out > skip ? out : skip;
        to = out + size < end ? out + size : end;
        if (junk) {
            if (in_len - at < RTRVZ_SEED_BYTES) return RTRVZ_ERR_TRUNCATED;
            if (from < to) {
                rtrvz_junk_seed(scratch, in + at);
                /* The seed gives the bytes from the 0x8000 boundary at or
                 * before the run's start. */
                rtrvz_junk_skip(scratch, (uint32_t)((data_offset + out) % JUNK_BLOCK) + (uint32_t)(from - out));
                rtrvz_junk_out(scratch, dst + (from - skip), (uint32_t)(to - from));
            }
            at += RTRVZ_SEED_BYTES;
        } else {
            if (in_len - at < size) return RTRVZ_ERR_TRUNCATED;
            if (from < to) copy_bytes(dst + (from - skip), in + at + (uint32_t)(from - out), (uint32_t)(to - from));
            at += size;
        }
        out += size;
    }
    return RTRVZ_OK;
}
