/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The virtual SD card's CID and CSD builder as runtime/rtvsd.c had it
 * until the loader took the job over (src/patching/vsdbuild.cpp,
 * vsd_card_registers). Kept here, renamed, only so vsd_tests can check
 * that the loader's registers are the ones the blob used to build, word
 * for word: a game's SD driver reads the card's size from the CSD. */
#include <stdint.h>

#include "rtvsd.h"

static void put_bits(uint32_t w[4], uint32_t msb, uint32_t width, uint32_t value) {
    uint32_t i;
    for (i = 0; i < width; ++i) {
        const uint32_t bit = msb - i;
        const uint32_t word = 3u - (bit >> 5);
        const uint32_t mask = 1u << (bit & 31u);
        if ((value >> (width - 1u - i)) & 1u) w[word] |= mask;
        else w[word] &= ~mask;
    }
}

/* CRC7 (x^7 + x^3 + 1) of bits 127..8, into bits 7..1 with the end bit. */
static void seal(uint32_t w[4]) {
    uint32_t crc = 0, bit;
    for (bit = 127; bit >= 8; --bit) {
        const uint32_t in = (w[3u - (bit >> 5)] >> (bit & 31u)) & 1u;
        const uint32_t top = (crc >> 6) & 1u;
        crc = (crc << 1) & 0x7Fu;
        if (in ^ top) crc ^= 0x09u;
    }
    put_bits(w, 7, 7, crc);
    put_bits(w, 0, 1, 1);
}

void legacy_rtvsd_cid(uint32_t out[4]);
void legacy_rtvsd_csd(const struct rtvsd_card* card, uint32_t out[4]);

void legacy_rtvsd_cid(uint32_t out[4]) {
    out[0] = out[1] = out[2] = out[3] = 0;
    put_bits(out, 127, 8, 0x52u);
    put_bits(out, 119, 16, 0x5257u);
    put_bits(out, 103, 8, 'R');
    put_bits(out, 95, 8, 'I');
    put_bits(out, 87, 8, 'F');
    put_bits(out, 79, 8, 'T');
    put_bits(out, 71, 8, 'W');
    put_bits(out, 63, 8, 0x10u);
    put_bits(out, 55, 32, 0x52575644u);
    put_bits(out, 19, 12, (26u << 4) | 9u);
    seal(out);
}

static void csd_common(uint32_t out[4], uint32_t block_len_log2) {
    put_bits(out, 119, 8, 0x0Eu);
    put_bits(out, 111, 8, 0);
    put_bits(out, 103, 8, 0x32u);
    put_bits(out, 95, 12, 0x5B5u);
    put_bits(out, 83, 4, block_len_log2);
    put_bits(out, 46, 1, 1);
    put_bits(out, 45, 7, 0x7Fu);
    put_bits(out, 28, 3, 2);
    put_bits(out, 25, 4, block_len_log2);
}

void legacy_rtvsd_csd(const struct rtvsd_card* card, uint32_t out[4]) {
    out[0] = out[1] = out[2] = out[3] = 0;
    if (card->sdhc) {
        const uint32_t units = card->sectors >> 10;
        put_bits(out, 127, 2, 1);
        csd_common(out, 9);
        put_bits(out, 69, 22, units != 0 ? units - 1u : 0u);
    } else {
        uint32_t shift = 2;
        uint32_t block_len_log2, mult, units;
        while (shift < 11u && (card->sectors >> shift) > 4096u) ++shift;
        units = card->sectors >> shift;
        block_len_log2 = shift > 9u ? 11u : 9u;
        mult = shift - 2u - (block_len_log2 - 9u);
        csd_common(out, block_len_log2);
        put_bits(out, 79, 1, 1);
        put_bits(out, 73, 12, units != 0 ? units - 1u : 0u);
        put_bits(out, 61, 3, 7);
        put_bits(out, 58, 3, 7);
        put_bits(out, 55, 3, 6);
        put_bits(out, 52, 3, 6);
        put_bits(out, 49, 3, mult);
    }
    seal(out);
}
