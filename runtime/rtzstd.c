/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Independent implementation of RFC 8878, sections 3 and 4. The only
 * algorithm description used is the published format, including its
 * predefined probability distributions. No decoder source was consulted.
 * RTZSTD_CHECKPOINT lets the GameCube worker yield without global state. */
#include "rtzstd.h"

#ifndef RTZSTD_CHECKPOINT
#define RTZSTD_CHECKPOINT() ((void)0)
#endif
#ifndef RTZSTD_OBSERVE
#define RTZSTD_OBSERVE(kind, value) ((void)0)
#endif

struct rtzstd_fse_entry {
    uint16_t baseline;
    uint8_t bits;
    uint8_t symbol;
};

struct rtzstd_context {
    struct rtzstd_fse_entry literal_lengths[512];
    struct rtzstd_fse_entry match_lengths[512];
    struct rtzstd_fse_entry offsets[256];
    struct rtzstd_fse_entry weights_table[64];
    uint16_t huffman[2048]; /* symbol in high byte, bit count in low byte */
    int16_t probabilities[256];
    uint16_t next_state[256];
    uint8_t weights[256];
    uint8_t literal_log;
    uint8_t match_log;
    uint8_t offset_log;
    uint8_t huffman_log;
    uint8_t have_tables;
};

typedef char rtzstd_workspace_fits[
    sizeof(struct rtzstd_context) <= RTZSTD_WORKSPACE_BYTES ? 1 : -1];

/* A backward bit reader. The valid bits occupy the low 'count' bits and
 * are consumed from their high end. No speculative read crosses input. */
struct rtzstd_bits {
    const uint8_t* begin;
    const uint8_t* cursor;
    uint32_t value;
    unsigned count;
    int failed;
};

static unsigned rtzstd_log2(uint32_t value) {
    return 31u - (unsigned)__builtin_clz(value);
}

static uint32_t rtzstd_le(const uint8_t* p, unsigned bytes) {
    uint32_t value = 0;
    unsigned i;
    for (i = 0; i < bytes; ++i) value |= (uint32_t)p[i] << (8 * i);
    return value;
}

static int rtzstd_bits_init(struct rtzstd_bits* bits, const uint8_t* input, size_t bytes) {
    uint32_t last;
    if (bytes == 0 || input[bytes - 1] == 0) return 0;
    last = input[bytes - 1];
    bits->begin = input;
    bits->cursor = input + bytes - 1;
    bits->count = rtzstd_log2(last);
    bits->value = last ^ (1u << bits->count);
    bits->failed = 0;
    return 1;
}

static inline void rtzstd_refill(struct rtzstd_bits* bits, unsigned needed) {
    /* Top the reservoir up to 25-32 bits at once rather than to `needed`:
     * most reads then skip this loop. Bits above `count` are never read
     * (reads mask below it), so the shift may push stale ones out. */
    if (bits->count >= needed) return;
    while (bits->count <= 24 && bits->cursor != bits->begin) {
        bits->value = (bits->value << 8) | *--bits->cursor;
        bits->count += 8;
    }
}

static inline uint32_t rtzstd_read(struct rtzstd_bits* bits, unsigned count) {
    uint32_t value;
    if (count == 0) return 0;
    /* Splitting large offsets keeps the reservoir within 32 bits on Wii. */
    if (count > 24) {
        const uint32_t high = rtzstd_read(bits, count - 16);
        return (high << 16) | rtzstd_read(bits, 16);
    }
    rtzstd_refill(bits, count);
    if (bits->count < count) {
        bits->failed = 1;
        return 0;
    }
    bits->count -= count;
    value = (bits->value >> bits->count) & ((1u << count) - 1);
    return value;
}

static int rtzstd_bits_end(const struct rtzstd_bits* bits) {
    return !bits->failed && bits->cursor == bits->begin && bits->count == 0;
}

/* Forward probability headers use a different, low-bits-first packing.
 * Header reads are small and bounded by the alphabet (at most 256). */
static uint32_t rtzstd_forward(const uint8_t* src, size_t bytes, size_t bit,
                               unsigned count) {
    uint32_t value = 0;
    unsigned i;
    for (i = 0; i < count; ++i) {
        const size_t position = bit + i;
        if (position / 8 < bytes) {
            value |= (uint32_t)((src[position / 8] >> (position % 8)) & 1) << i;
        }
    }
    return value;
}

static int rtzstd_distribution(struct rtzstd_context* ctx, const uint8_t** source,
                                const uint8_t* end, unsigned max_symbol,
                                unsigned max_log, unsigned* log, unsigned* symbols) {
    const uint8_t* src = *source;
    const size_t bytes = (size_t)(end - src);
    size_t bit = 4;
    unsigned symbol = 0;
    uint32_t remaining;
    if (bytes == 0) return 0;
    *log = (src[0] & 15) + 5;
    if (*log > max_log) return 0;
    remaining = 1u << *log;
    while (remaining != 0 && symbol <= max_symbol) {
        const unsigned width = rtzstd_log2(remaining + 1) + 1;
        const uint32_t short_limit = (1u << width) - 1 - (remaining + 1);
        uint32_t value = rtzstd_forward(src, bytes, bit, width - 1);
        unsigned used = width - 1;
        int probability;
        if (value >= short_limit) {
            value = rtzstd_forward(src, bytes, bit, width);
            if (value >= (1u << (width - 1))) value -= short_limit;
            used = width;
        }
        bit += used;
        if (bit > bytes * 8) return 0;
        probability = (int)value - 1;
        if (probability < -1 || (uint32_t)(probability < 0 ? 1 : probability) > remaining) return 0;
        ctx->probabilities[symbol++] = (int16_t)probability;
        remaining -= probability < 0 ? 1u : (uint32_t)probability;
        if (probability == 0) {
            uint32_t repeat;
            do {
                repeat = rtzstd_forward(src, bytes, bit, 2);
                bit += 2;
                if (bit > bytes * 8 || repeat > max_symbol + 1 - symbol) return 0;
                while (repeat-- != 0) ctx->probabilities[symbol++] = 0;
                /* The repeat counter was consumed above; reread the flag. */
                repeat = rtzstd_forward(src, bytes, bit - 2, 2);
            } while (repeat == 3);
        }
    }
    if (remaining != 0 || symbol < 2) return 0;
    *symbols = symbol;
    *source += (bit + 7) / 8;
    return 1;
}

static int rtzstd_build_fse(struct rtzstd_context* ctx, struct rtzstd_fse_entry* table,
                            unsigned log, unsigned symbols) {
    const unsigned size = 1u << log;
    const unsigned step = (size >> 1) + (size >> 3) + 3;
    unsigned high = size;
    unsigned position = 0;
    unsigned symbol;
    unsigned i;
    unsigned present = 0;
    for (symbol = 0; symbol < symbols; ++symbol) {
        const int probability = ctx->probabilities[symbol];
        ctx->next_state[symbol] = (uint16_t)(probability < 0 ? 1 : probability);
        if (probability != 0) ++present;
        if (probability == -1) table[--high].symbol = (uint8_t)symbol;
    }
    if (present < 2) return 0;
    for (symbol = 0; symbol < symbols; ++symbol) {
        int copies = ctx->probabilities[symbol];
        while (copies-- > 0) {
            table[position].symbol = (uint8_t)symbol;
            do {
                position = (position + step) & (size - 1);
            } while (position >= high);
        }
    }
    if (position != 0) return 0;
    for (i = 0; i < size; ++i) {
        const unsigned next = ctx->next_state[table[i].symbol]++;
        const unsigned bits = log - rtzstd_log2(next);
        table[i].bits = (uint8_t)bits;
        table[i].baseline = (uint16_t)((next << bits) - size);
    }
    return 1;
}

static int rtzstd_huffman_table(struct rtzstd_context* ctx, const uint8_t** source,
                                const uint8_t* end) {
    const uint8_t* src = *source;
    unsigned count = 0;
    unsigned i;
    uint32_t sum = 0;
    unsigned log;
    unsigned weight;
    unsigned position = 0;
    uint32_t remainder;
    if (src == end) return 0;
    if (*src >= 128) {
        RTZSTD_OBSERVE(4, 0);
        count = *src++ - 127u;
        if ((size_t)(end - src) < (count + 1) / 2) return 0;
        for (i = 0; i < count; ++i) {
            ctx->weights[i] = (uint8_t)((src[i / 2] >> ((i & 1) ? 0 : 4)) & 15);
        }
        src += (count + 1) / 2;
    } else {
        RTZSTD_OBSERVE(4, 1);
        const unsigned bytes = *src++;
        const uint8_t* weights_end;
        unsigned symbols;
        unsigned states[2];
        unsigned turn = 0;
        struct rtzstd_bits bits;
        if ((size_t)(end - src) < bytes) return 0;
        weights_end = src + bytes;
        if (!rtzstd_distribution(ctx, &src, weights_end, 11, 6, &log, &symbols) ||
            !rtzstd_build_fse(ctx, ctx->weights_table, log, symbols) ||
            !rtzstd_bits_init(&bits, src, (size_t)(weights_end - src))) return 0;
        states[0] = rtzstd_read(&bits, log);
        states[1] = rtzstd_read(&bits, log);
        if (bits.failed) return 0;
        for (;;) {
            const struct rtzstd_fse_entry entry = ctx->weights_table[states[turn]];
            if (count >= 254) return 0;
            ctx->weights[count++] = entry.symbol;
            rtzstd_refill(&bits, entry.bits);
            if (bits.count < entry.bits) {
                ctx->weights[count++] = ctx->weights_table[states[turn ^ 1]].symbol;
                break;
            }
            states[turn] = entry.baseline + rtzstd_read(&bits, entry.bits);
            turn ^= 1;
        }
        src = weights_end;
    }
    for (i = 0; i < count; ++i) {
        weight = ctx->weights[i];
        if (weight > 11) return 0;
        if (weight != 0) sum += 1u << (weight - 1);
    }
    if (sum == 0) return 0;
    log = rtzstd_log2(sum) + 1;
    if (log > 11) return 0;
    remainder = (1u << log) - sum;
    if ((remainder & (remainder - 1)) != 0) return 0;
    ctx->weights[count++] = (uint8_t)(rtzstd_log2(remainder) + 1);
    for (weight = 1; weight <= log; ++weight) {
        const unsigned copies = 1u << (weight - 1);
        for (i = 0; i < count; ++i) {
            unsigned j;
            if (ctx->weights[i] != weight) continue;
            for (j = 0; j < copies; ++j) {
                ctx->huffman[position++] = (uint16_t)((i << 8) | (log + 1 - weight));
            }
        }
    }
    ctx->huffman_log = (uint8_t)log;
    *source = src;
    return position == (1u << log);
}

/* The table and its size come in as arguments: read through ctx, they
 * would be loaded again after every output byte (a byte store may alias
 * anything as far as the compiler knows). */
static inline int rtzstd_huffman_symbol(const uint16_t* table, unsigned log,
                                        struct rtzstd_bits* bits, uint8_t* output) {
    unsigned index;
    uint16_t entry;
    rtzstd_refill(bits, log);
    if (bits->count >= log) index = (bits->value >> (bits->count - log)) & ((1u << log) - 1);
    else index = (bits->value << (log - bits->count)) & ((1u << log) - 1);
    entry = table[index];
    if ((entry & 255) > bits->count) return 0;
    bits->count -= entry & 255;
    *output = (uint8_t)(entry >> 8);
    return 1;
}

/* The literals loop is the decoder's hottest: -O3 here makes the whole
 * decoder about 8% faster for about 1 KB of code (the rest stays -O2). */
__attribute__((optimize("O3"))) static int rtzstd_huffman_streams(struct rtzstd_context* ctx, uint8_t* output,
                                  unsigned decoded, const uint8_t* src,
                                  unsigned bytes, unsigned streams) {
    struct rtzstd_bits bits[4];
    unsigned lengths[4];
    unsigned sizes[4];
    unsigned i;
    unsigned at;
    const unsigned segment = streams == 4 ? (decoded + 3) / 4 : decoded;
    RTZSTD_OBSERVE(2, streams);
    if (streams == 4) {
        if (bytes < 6 || decoded < 3 * segment) return 0;
        sizes[3] = bytes - 6;
        for (i = 0; i < 3; ++i) {
            sizes[i] = rtzstd_le(src + 2 * i, 2);
            if (sizes[i] > sizes[3]) return 0;
            sizes[3] -= sizes[i];
        }
        src += 6;
    } else {
        sizes[0] = bytes;
    }
    for (i = 0; i < streams; ++i) {
        lengths[i] = i + 1 == streams ? decoded - i * segment : segment;
        if (!rtzstd_bits_init(&bits[i], src, sizes[i])) return 0;
        src += sizes[i];
    }
    /* Interleave independent streams so their dependency chains overlap.
     * The last stream is the shortest, so every stream runs together up
     * to its length, then the others finish. Yield every 256 rounds. */
    {
        const uint16_t* table = ctx->huffman;
        const unsigned log = ctx->huffman_log;
        const unsigned common = lengths[streams - 1];
        for (at = 0; at < common; ++at) {
            for (i = 0; i < streams; ++i) {
                if (!rtzstd_huffman_symbol(table, log, &bits[i], output + i * segment + at)) return 0;
            }
            if ((at & 255) == 255) RTZSTD_CHECKPOINT();
        }
        for (i = 0; i + 1 < streams; ++i) {
            for (at = common; at < lengths[i]; ++at) {
                if (!rtzstd_huffman_symbol(table, log, &bits[i], output + i * segment + at)) return 0;
            }
        }
    }
    for (i = 0; i < streams; ++i) {
        if (!rtzstd_bits_end(&bits[i])) return 0;
    }
    return 1;
}

static void rtzstd_copy_long(uint8_t* dst, const uint8_t* src, uint32_t bytes) {
    /* Copies run forward. When the source is at least four bytes behind
     * the destination (a match's offset) or the two don't overlap, each
     * four-byte read only sees bytes already final, so words give exactly
     * the byte loop's result in a quarter of the steps; a staged literal
     * sits after its destination, which a forward copy reads first. */
    const uint32_t distance = (uint32_t)(dst > src ? dst - src : src - dst);
    const int words = distance >= 4;
    while (bytes != 0) {
        uint32_t i = 0;
        const uint32_t piece = bytes < 4096 ? bytes : 4096;
        if (words) {
            for (; i + 4 <= piece; i += 4) {
                uint32_t word;
                __builtin_memcpy(&word, src + i, 4);
                __builtin_memcpy(dst + i, &word, 4);
            }
        }
        for (; i < piece; ++i) dst[i] = src[i];
        dst += piece;
        src += piece;
        bytes -= piece;
        RTZSTD_CHECKPOINT();
    }
}

/* Most literal runs and matches are a few bytes: those copy inline, a
 * byte at a time (always correct forward), and skip the long copy's
 * setup and its checkpoint (the sequence loop has its own). */
static inline void rtzstd_copy(uint8_t* dst, const uint8_t* src, uint32_t bytes) {
    if (bytes <= 16) {
        uint32_t i;
        for (i = 0; i < bytes; ++i) dst[i] = src[i];
        return;
    }
    rtzstd_copy_long(dst, src, bytes);
}

static void rtzstd_fill(uint8_t* dst, uint8_t value, uint32_t bytes) {
    while (bytes != 0) {
        uint32_t i;
        const uint32_t piece = bytes < 4096 ? bytes : 4096;
        for (i = 0; i < piece; ++i) dst[i] = value;
        dst += piece;
        bytes -= piece;
        RTZSTD_CHECKPOINT();
    }
}

/* RFC 8878 sections 3.1.1.3.2.2 and 3.1.1.3.2.1.1: format constants,
 * shared by table construction and sequence expansion. */
static const int8_t rtzstd_default_ll[36] = {
    4, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2, 1, 1, 1, 1, 1, -1, -1, -1, -1
};
static const int8_t rtzstd_default_ml[53] = {
    1, 4, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1,
    -1, -1, -1, -1, -1
};
static const int8_t rtzstd_default_of[29] = {
    1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1
};
static const uint32_t rtzstd_ll_base[36] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 18, 20, 22, 24, 28, 32, 40, 48, 64, 128, 256, 512, 1024,
    2048, 4096, 8192, 16384, 32768, 65536
};
static const uint8_t rtzstd_ll_bits[36] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 2, 2, 3, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
};
static const uint32_t rtzstd_ml_base[53] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
    19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34,
    35, 37, 39, 41, 43, 47, 51, 59, 67, 83, 99, 131, 259, 515,
    1027, 2051, 4099, 8195, 16387, 32771, 65539
};
static const uint8_t rtzstd_ml_bits[53] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
};

static int rtzstd_sequence_table(struct rtzstd_context* ctx,
                                  struct rtzstd_fse_entry* table, uint8_t* table_log,
                                  const uint8_t** source, const uint8_t* end,
                                  unsigned mode, unsigned max_symbol, unsigned max_log,
                                  const int8_t* predefined, unsigned predefined_symbols,
                                  unsigned predefined_log, unsigned valid_bit) {
    unsigned log;
    unsigned symbols;
    unsigned i;
    RTZSTD_OBSERVE(3, mode);
    if (mode == 3) return (ctx->have_tables & valid_bit) != 0;
    if (mode == 1) {
        if (*source == end || **source > max_symbol) return 0;
        table[0].symbol = *(*source)++;
        table[0].bits = 0;
        table[0].baseline = 0;
        *table_log = 0;
    } else {
        if (mode == 0) {
            log = predefined_log;
            symbols = predefined_symbols;
            for (i = 0; i < symbols; ++i) ctx->probabilities[i] = predefined[i];
        } else if (!rtzstd_distribution(ctx, source, end, max_symbol, max_log, &log, &symbols)) {
            return 0;
        }
        if (!rtzstd_build_fse(ctx, table, log, symbols)) return 0;
        *table_log = (uint8_t)log;
    }
    ctx->have_tables |= (uint8_t)valid_bit;
    return 1;
}

static int rtzstd_literals(struct rtzstd_context* ctx, const uint8_t** source,
                            const uint8_t* end, uint8_t* output, uint32_t available,
                            uint32_t block_max, const uint8_t** literals,
                            uint32_t* literal_bytes, int* staged) {
    const uint8_t* src = *source;
    unsigned type;
    unsigned format;
    unsigned header;
    uint32_t decoded;
    uint32_t stored;
    uint64_t fields;
    unsigned i;
    if (src == end) return 0;
    type = src[0] & 3;
    RTZSTD_OBSERVE(1, type);
    format = (src[0] >> 2) & 3;
    if (type < 2) {
        header = (format & 1) == 0 ? 1 : (format == 1 ? 2 : 3);
        if ((size_t)(end - src) < header) return 0;
        decoded = rtzstd_le(src, header) >> (header == 1 ? 3 : 4);
        stored = type == 1 ? 1 : decoded;
    } else {
        const unsigned width = format < 2 ? 10 : (format == 2 ? 14 : 18);
        header = format < 2 ? 3 : format + 2;
        if ((size_t)(end - src) < header) return 0;
        fields = 0;
        for (i = 0; i < header; ++i) fields |= (uint64_t)src[i] << (8 * i);
        decoded = (uint32_t)(fields >> 4) & ((1u << width) - 1);
        stored = (uint32_t)(fields >> (4 + width));
    }
    src += header;
    if (decoded > block_max || decoded > available || stored > (size_t)(end - src)) return 0;
    *source = src + stored;
    *literal_bytes = decoded;
    *staged = type != 0;
    if (type == 0) {
        *literals = src;
        return 1;
    }
    output += available - decoded;
    *literals = output;
    if (type == 1) {
        rtzstd_fill(output, *src, decoded);
        return 1;
    }
    end = src + stored;
    if (type == 2) {
        if (!rtzstd_huffman_table(ctx, &src, end)) return 0;
    } else if (ctx->huffman_log == 0) {
        return 0;
    }
    return rtzstd_huffman_streams(ctx, output, decoded, src, (unsigned)(end - src),
                                   format == 0 ? 1 : 4);
}

static int rtzstd_compressed(struct rtzstd_context* ctx, uint8_t* output,
                              uint32_t capacity, uint32_t* written,
                              const uint8_t* src, const uint8_t* end,
                              uint32_t block_max, uint32_t window, uint32_t repeat[3]) {
    const uint32_t block_start = *written;
    uint32_t position = block_start;
    const uint8_t* literals;
    uint32_t literal_bytes;
    uint32_t sequences;
    unsigned modes;
    int staged;
    struct rtzstd_bits bits;
    unsigned ll_state;
    unsigned of_state;
    unsigned ml_state;
    if (!rtzstd_literals(ctx, &src, end, output + position, capacity - position,
                          block_max, &literals, &literal_bytes, &staged) || src == end) return 0;
    sequences = *src++;
    if (sequences == 0) {
        if (src != end) return 0;
    } else {
        if (sequences >= 128) {
            if (src == end) return 0;
            if (sequences == 255) {
                if ((size_t)(end - src) < 2) return 0;
                sequences = rtzstd_le(src, 2) + 0x7F00;
                src += 2;
            } else {
                sequences = ((sequences - 128) << 8) + *src++;
            }
        }
        if (src == end || sequences > block_max / 3) return 0;
        modes = *src++;
        if ((modes & 3) != 0 ||
            !rtzstd_sequence_table(ctx, ctx->literal_lengths, &ctx->literal_log,
                &src, end, modes >> 6, 35, 9, rtzstd_default_ll, 36, 6, 1) ||
            !rtzstd_sequence_table(ctx, ctx->offsets, &ctx->offset_log,
                &src, end, (modes >> 4) & 3, 31, 8, rtzstd_default_of, 29, 5, 2) ||
            !rtzstd_sequence_table(ctx, ctx->match_lengths, &ctx->match_log,
                &src, end, (modes >> 2) & 3, 52, 9, rtzstd_default_ml, 53, 6, 4) ||
            !rtzstd_bits_init(&bits, src, (size_t)(end - src))) return 0;
        ll_state = rtzstd_read(&bits, ctx->literal_log);
        of_state = rtzstd_read(&bits, ctx->offset_log);
        ml_state = rtzstd_read(&bits, ctx->match_log);
        while (sequences-- != 0) {
            const struct rtzstd_fse_entry ll = ctx->literal_lengths[ll_state];
            const struct rtzstd_fse_entry of = ctx->offsets[of_state];
            const struct rtzstd_fse_entry ml = ctx->match_lengths[ml_state];
            const uint32_t offset_value = (1u << of.symbol) + rtzstd_read(&bits, of.symbol);
            const uint32_t match = rtzstd_ml_base[ml.symbol] + rtzstd_read(&bits, rtzstd_ml_bits[ml.symbol]);
            const uint32_t literal = rtzstd_ll_base[ll.symbol] + rtzstd_read(&bits, rtzstd_ll_bits[ll.symbol]);
            uint32_t offset;
            if (bits.failed || literal > literal_bytes || literal > capacity - position) return 0;
            if (offset_value > 3) {
                offset = offset_value - 3;
                repeat[2] = repeat[1];
                repeat[1] = repeat[0];
                repeat[0] = offset;
            } else {
                const unsigned index = offset_value - 1 + (literal == 0);
                offset = index == 3 ? repeat[0] - 1 : repeat[index];
                if (index != 0) {
                    if (index >= 2) repeat[2] = repeat[1];
                    repeat[1] = repeat[0];
                    repeat[0] = offset;
                }
            }
            if (match > capacity - position - literal || offset == 0 ||
                offset > position + literal || offset > window ||
                position + literal + match - block_start > block_max) return 0;
            /* Unconsumed staged literals are live bytes, even inside the
             * destination capacity. Reject a match that would clobber them. */
            if (staged && position + literal + match > (uint32_t)(literals - output) + literal) return 0;
            rtzstd_copy(output + position, literals, literal);
            literals += literal;
            literal_bytes -= literal;
            position += literal;
            rtzstd_copy(output + position, output + position - offset, match);
            position += match;
            if (sequences != 0) {
                ll_state = ll.baseline + rtzstd_read(&bits, ll.bits);
                ml_state = ml.baseline + rtzstd_read(&bits, ml.bits);
                of_state = of.baseline + rtzstd_read(&bits, of.bits);
            }
            RTZSTD_CHECKPOINT();
        }
        if (!rtzstd_bits_end(&bits)) return 0;
    }
    if (literal_bytes > capacity - position || position - block_start + literal_bytes > block_max) return 0;
    rtzstd_copy(output + position, literals, literal_bytes);
    *written = position + literal_bytes;
    return 1;
}

/* XXH64 with seed zero, as specified by RFC 8878's checksum reference:
 * https://github.com/Cyan4973/xxHash/blob/dev/doc/xxhash_spec.md
 * Compute only when the frame requests it; no streaming state is needed. */
#define RTZSTD_P1 UINT64_C(11400714785074694791)
#define RTZSTD_P2 UINT64_C(14029467366897019727)
#define RTZSTD_P3 UINT64_C(1609587929392839161)
#define RTZSTD_P4 UINT64_C(9650029242287828579)
#define RTZSTD_P5 UINT64_C(2870177450012600261)

static uint64_t rtzstd_rotate(uint64_t value, unsigned bits) {
    return (value << bits) | (value >> (64 - bits));
}

static uint64_t rtzstd_le64(const uint8_t* p) {
    return rtzstd_le(p, 4) | ((uint64_t)rtzstd_le(p + 4, 4) << 32);
}

static uint64_t rtzstd_hash_round(uint64_t accumulator, uint64_t lane) {
    return rtzstd_rotate(accumulator + lane * RTZSTD_P2, 31) * RTZSTD_P1;
}

static uint32_t rtzstd_checksum(const uint8_t* src, uint32_t bytes) {
    const uint8_t* end = src + bytes;
    uint64_t hash;
    if (bytes >= 32) {
        uint64_t lanes[4] = {RTZSTD_P1 + RTZSTD_P2, RTZSTD_P2, 0, 0 - RTZSTD_P1};
        unsigned i;
        uint32_t stripes = 0;
        do {
            for (i = 0; i < 4; ++i) lanes[i] = rtzstd_hash_round(lanes[i], rtzstd_le64(src + 8 * i));
            src += 32;
            if ((++stripes & 127) == 0) RTZSTD_CHECKPOINT();
        } while ((size_t)(end - src) >= 32);
        hash = rtzstd_rotate(lanes[0], 1) + rtzstd_rotate(lanes[1], 7) +
               rtzstd_rotate(lanes[2], 12) + rtzstd_rotate(lanes[3], 18);
        for (i = 0; i < 4; ++i) {
            hash ^= rtzstd_hash_round(0, lanes[i]);
            hash = hash * RTZSTD_P1 + RTZSTD_P4;
        }
    } else {
        hash = RTZSTD_P5;
    }
    hash += bytes;
    while ((size_t)(end - src) >= 8) {
        hash ^= rtzstd_hash_round(0, rtzstd_le64(src));
        hash = rtzstd_rotate(hash, 27) * RTZSTD_P1 + RTZSTD_P4;
        src += 8;
    }
    if ((size_t)(end - src) >= 4) {
        hash ^= (uint64_t)rtzstd_le(src, 4) * RTZSTD_P1;
        hash = rtzstd_rotate(hash, 23) * RTZSTD_P2 + RTZSTD_P3;
        src += 4;
    }
    while (src != end) {
        hash ^= *src++ * RTZSTD_P5;
        hash = rtzstd_rotate(hash, 11) * RTZSTD_P1;
    }
    hash ^= hash >> 33;
    hash *= RTZSTD_P2;
    hash ^= hash >> 29;
    hash *= RTZSTD_P3;
    hash ^= hash >> 32;
    return (uint32_t)hash;
}

void* rtzstd_init(void* workspace, size_t bytes) {
    if (workspace == NULL || ((uintptr_t)workspace & 3) != 0 || bytes < RTZSTD_WORKSPACE_BYTES) return NULL;
    return workspace;
}

int32_t rtzstd_decode(void* workspace, uint8_t* output, uint32_t capacity,
                     const uint8_t* input, uint32_t length) {
    struct rtzstd_context* ctx = (struct rtzstd_context*)workspace;
    const uint8_t* src = input;
    const uint8_t* end;
    uint32_t window;
    uint32_t block_max;
    uint32_t written = 0;
    uint32_t repeat[3] = {1, 4, 8};
    uint64_t content_size = 0;
    unsigned descriptor;
    unsigned size_bytes;
    unsigned i;
    unsigned last;
    if (ctx == NULL || output == NULL || input == NULL || length < 6 || capacity > 0x7FFFFFFFu) return -1;
    end = input + length;
    if (rtzstd_le(src, 4) != 0xFD2FB528u) return -1;
    descriptor = src[4];
    src += 5;
    /* A dictionary flag is refused even if the encoded ID happens to be zero. */
    if ((descriptor & 11) != 0) return -1;
    if ((descriptor & 32) != 0) {
        window = 0;
    } else {
        const unsigned exponent = 10 + (*src >> 3);
        const unsigned mantissa = *src++ & 7;
        if (exponent > 30) return -1;
        window = (1u << exponent) + ((1u << exponent) >> 3) * mantissa;
    }
    size_bytes = descriptor >> 6;
    size_bytes = size_bytes == 0 ? ((descriptor & 32) != 0) : (1u << size_bytes);
    if ((size_t)(end - src) < size_bytes) return -1;
    for (i = 0; i < size_bytes; ++i) content_size |= (uint64_t)*src++ << (8 * i);
    if (size_bytes == 2) content_size += 256;
    if (size_bytes != 0 && content_size > capacity) return -1;
    if ((descriptor & 32) != 0) window = (uint32_t)content_size;
    block_max = window < 131072 ? window : 131072;
    ctx->huffman_log = 0;
    ctx->have_tables = 0;
    do {
        uint32_t header;
        uint32_t bytes;
        uint32_t stored;
        unsigned type;
        if ((size_t)(end - src) < 3) return -1;
        header = rtzstd_le(src, 3);
        src += 3;
        last = header & 1;
        type = (header >> 1) & 3;
        RTZSTD_OBSERVE(0, type);
        bytes = header >> 3;
        stored = type == 1 ? 1 : bytes;
        if (type == 3 || bytes > block_max || stored > (size_t)(end - src)) return -1;
        if (type == 2) {
            if (!rtzstd_compressed(ctx, output, capacity, &written, src, src + stored,
                                    block_max, window, repeat)) return -1;
        } else {
            if (bytes > capacity - written) return -1;
            if (type == 1) rtzstd_fill(output + written, *src, bytes);
            else rtzstd_copy(output + written, src, bytes);
            written += bytes;
        }
        src += stored;
        RTZSTD_CHECKPOINT();
    } while (!last);
    if (size_bytes != 0 && written != content_size) return -1;
    if ((descriptor & 4) != 0) {
        if ((size_t)(end - src) != 4 || rtzstd_checksum(output, written) != rtzstd_le(src, 4)) return -1;
        src += 4;
    }
    return src == end ? (int32_t)written : -1;
}
