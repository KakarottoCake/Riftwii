/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The parts of reading an RVZ group that follow decompression: skipping
 * the hash exception lists at the start of Wii partition data, and
 * decoding RVZ packing, where runs of the disc's padding are stored as the
 * 68-byte seed of the generator that made them. Format: Dolphin's
 * docs/WiaAndRvz.md. Freestanding C99 like rtable.c, so the host reader
 * (src/rvz.cpp) and the resident runtime share it.
 */
#ifndef RIFTWII_RTRVZ_H
#define RIFTWII_RTRVZ_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RTRVZ_OK 0
#define RTRVZ_ERR_TRUNCATED (-1) /* a record or list runs past the input */
#define RTRVZ_ERR_SHORT (-2)     /* the input ends before the bytes asked for */

#define RTRVZ_JUNK_WORDS 521u
#define RTRVZ_SEED_BYTES 68u

/* The padding generator: lagged Fibonacci, xor, j = 32, k = 521. */
typedef struct rtrvz_junk {
    uint32_t buf[RTRVZ_JUNK_WORDS];
    uint32_t pos; /* next byte of buf to output, 0 .. 4 * 521 */
} rtrvz_junk;

/* Seeds from 17 big-endian words, ready to output the byte at a 0x8000
 * boundary. */
void rtrvz_junk_seed(rtrvz_junk* j, const uint8_t seed[RTRVZ_SEED_BYTES]);
void rtrvz_junk_skip(rtrvz_junk* j, uint32_t bytes);
void rtrvz_junk_out(rtrvz_junk* j, uint8_t* dst, uint32_t len);

/* Bytes taken by `lists` exception lists at the start of `in`, plus the
 * padding to a multiple of 4 that follows them when the group is stored
 * uncompressed (`pad4`). RTRVZ_OK, or RTRVZ_ERR_TRUNCATED. */
int rtrvz_exception_bytes(const uint8_t* in, uint32_t in_len, uint32_t lists, int pad4, uint32_t* out);

/* Decodes the packed stream `in` (exception lists already skipped) and
 * copies its unpacked bytes [skip, skip + len) to `dst`. `data_offset` is
 * where the stream's first byte sits for the generator: its disc offset
 * for raw data, its offset in the partition's data without hashes for
 * partition data. `scratch` holds the generator. RTRVZ_OK, or an error. */
int rtrvz_unpack(const uint8_t* in, uint32_t in_len, uint64_t data_offset, uint32_t skip,
                 uint8_t* dst, uint32_t len, rtrvz_junk* scratch);

#ifdef __cplusplus
}
#endif

#endif
