/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The virtual SD card (rtvsd.h). */
#include "rtvsd.h"

/* Card states (R1 bits 9-12). */
#define STATE_IDLE 0u
#define STATE_READY 1u
#define STATE_IDENT 2u

/* ---- Registers ---------------------------------------------------------- */

/* Bit `bit` (0 = the last bit of the 128) of an R2 register. */
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

static uint32_t get_bits(const uint32_t w[4], uint32_t msb, uint32_t width) {
    uint32_t i, v = 0;
    for (i = 0; i < width; ++i) {
        const uint32_t bit = msb - i;
        v = (v << 1) | ((w[3u - (bit >> 5)] >> (bit & 31u)) & 1u);
    }
    return v;
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

void rtvsd_cid(uint32_t out[4]) {
    out[0] = out[1] = out[2] = out[3] = 0;
    put_bits(out, 127, 8, 0x52u);                 /* MID */
    put_bits(out, 119, 16, 0x5257u);              /* OID "RW" */
    put_bits(out, 103, 8, 'R');                   /* PNM "RIFTW" */
    put_bits(out, 95, 8, 'I');
    put_bits(out, 87, 8, 'F');
    put_bits(out, 79, 8, 'T');
    put_bits(out, 71, 8, 'W');
    put_bits(out, 63, 8, 0x10u);                  /* PRV 1.0 */
    put_bits(out, 55, 32, 0x52575644u);           /* PSN */
    put_bits(out, 19, 12, (26u << 4) | 9u);       /* MDT: 2026-09 */
    seal(out);
}

/* The common fields of both CSD versions. */
static void csd_common(uint32_t out[4], uint32_t block_len_log2) {
    put_bits(out, 119, 8, 0x0Eu);    /* TAAC: 1 ms */
    put_bits(out, 111, 8, 0);        /* NSAC */
    put_bits(out, 103, 8, 0x32u);    /* TRAN_SPEED: 25 MHz */
    put_bits(out, 95, 12, 0x5B5u);   /* CCC: classes 0, 2, 4, 5, 7, 8, 10 */
    put_bits(out, 83, 4, block_len_log2);
    put_bits(out, 46, 1, 1);         /* ERASE_BLK_EN */
    put_bits(out, 45, 7, 0x7Fu);     /* SECTOR_SIZE */
    put_bits(out, 28, 3, 2);         /* R2W_FACTOR */
    put_bits(out, 25, 4, block_len_log2);
}

void rtvsd_csd(const struct rtvsd_card* card, uint32_t out[4]) {
    out[0] = out[1] = out[2] = out[3] = 0;
    if (card->sdhc) {
        /* Version 2.0: C_SIZE counts 512 KiB units. */
        const uint32_t units = card->sectors >> 10;
        put_bits(out, 127, 2, 1);
        csd_common(out, 9);
        put_bits(out, 69, 22, units != 0 ? units - 1u : 0u);
    } else {
        /* Version 1.0: (C_SIZE + 1) << (C_SIZE_MULT + 2) blocks of
         * 2^READ_BL_LEN bytes, the smallest multiplier that fits C_SIZE's
         * 12 bits. Up to 1 GiB the blocks are 512 bytes; above, 2048
         * bytes, as Dolphin's card states them: Brawl's driver reads
         * nothing past 1 GiB of a card with 1024-byte blocks. */
        uint32_t shift = 2; /* sectors per C_SIZE unit, log2: C_SIZE_MULT + 2 + READ_BL_LEN - 9 */
        uint32_t block_len_log2, mult, units;
        while (shift < 11u && (card->sectors >> shift) > 4096u) ++shift;
        units = card->sectors >> shift;
        block_len_log2 = shift > 9u ? 11u : 9u;  /* C_SIZE_MULT tops out at 7 */
        mult = shift - 2u - (block_len_log2 - 9u);
        csd_common(out, block_len_log2);
        put_bits(out, 79, 1, 1);     /* READ_BL_PARTIAL */
        put_bits(out, 73, 12, units != 0 ? units - 1u : 0u);
        put_bits(out, 61, 3, 7);     /* VDD_R_CURR_MIN, _MAX, VDD_W_CURR_MIN, _MAX */
        put_bits(out, 58, 3, 7);
        put_bits(out, 55, 3, 6);
        put_bits(out, 52, 3, 6);
        put_bits(out, 49, 3, mult);
    }
    seal(out);
}

uint32_t rtvsd_csd_sectors(const uint32_t csd[4]) {
    if (get_bits(csd, 127, 2) == 1u) return (get_bits(csd, 69, 22) + 1u) << 10;
    {
        const uint32_t units = get_bits(csd, 73, 12) + 1u;
        const uint32_t shift = get_bits(csd, 49, 3) + 2u + get_bits(csd, 83, 4);
        return shift >= 9u ? units << (shift - 9u) : units >> (9u - shift);
    }
}

/* ---- The card ----------------------------------------------------------- */

int rtvsd_reset(struct rtvsd_card* card) {
    uint32_t i, next = 0;
    card->sdhc = card->sectors > RTVSD_SDSC_MAX_SECTORS ? 1u : 0u;
    card->state = RTVSD_STATE_STBY;
    card->app_cmd = 0;
    card->bus_width = 0;
    for (i = 0; i < 64u; ++i) card->hcr[i] = 0;
    if (card->sectors == 0 || card->extent_count == 0 || card->extent_count > RTVSD_MAX_EXTENTS) return -1;
    for (i = 0; i < card->extent_count; ++i) {
        const struct rtvsd_extent* e = &card->extents[i];
        if (e->file_sector != next || e->count == 0 || e->device_sector + e->count < e->device_sector) return -1;
        next += e->count;
        if (next < e->count) return -1;
    }
    return next >= card->sectors ? 0 : -1;
}

uint32_t rtvsd_map(const struct rtvsd_card* card, uint32_t sector, uint32_t count, uint32_t* device_sector) {
    uint32_t lo = 0, hi = card->extent_count;
    if (count == 0 || sector >= card->sectors) return 0;
    if (count > card->sectors - sector) count = card->sectors - sector;
    while (hi - lo > 1u) {
        const uint32_t mid = lo + ((hi - lo) >> 1);
        if (card->extents[mid].file_sector <= sector) lo = mid;
        else hi = mid;
    }
    {
        const struct rtvsd_extent* e = &card->extents[lo];
        const uint32_t into = sector - e->file_sector;
        uint32_t run;
        if (sector < e->file_sector || into >= e->count) return 0;
        run = e->count - into;
        *device_sector = e->device_sector + into;
        return run < count ? run : count;
    }
}

/* Host controller registers: bytes, little-endian within a register as
 * the controller's are, packed four to a word. */
static uint32_t hcr_get(const struct rtvsd_card* card, uint32_t reg, uint32_t size) {
    uint32_t i, v = 0;
    for (i = 0; i < size && reg + i < 256u; ++i) {
        const uint32_t r = reg + i;
        v |= ((card->hcr[r >> 2] >> ((r & 3u) * 8u)) & 0xFFu) << (i * 8u);
    }
    return v;
}

static void hcr_set(struct rtvsd_card* card, uint32_t reg, uint32_t size, uint32_t value) {
    uint32_t i;
    for (i = 0; i < size && reg + i < 256u; ++i) {
        const uint32_t r = reg + i, shift = (r & 3u) * 8u;
        card->hcr[r >> 2] = (card->hcr[r >> 2] & ~(0xFFu << shift)) | (((value >> (i * 8u)) & 0xFFu) << shift);
    }
}

#define HCR_CLOCK_CONTROL 0x2Cu  /* bit 0: internal clock on, bit 1: stable */
#define HCR_SOFTWARE_RESET 0x2Fu

int32_t rtvsd_ioctl(struct rtvsd_card* card, uint32_t ioctl, const uint32_t* in, uint32_t in_len, uint32_t* out,
                    uint32_t out_len) {
    if (ioctl == RTVSD_IOCTL_WRITEHCR || ioctl == RTVSD_IOCTL_READHCR) {
        /* The block: the register, two zero words, the size, the value. */
        uint32_t reg, size;
        if (in == 0 || in_len < 20u) return RTVSD_EINVAL;
        reg = in[0];
        size = in[3];
        if (reg > 0xFFu || size == 0 || size > 4u) return RTVSD_EINVAL;
        if (ioctl == RTVSD_IOCTL_READHCR) {
            if (out == 0 || out_len < 4u) return RTVSD_EINVAL;
            out[0] = hcr_get(card, reg, size);
            return RTVSD_OK;
        }
        {
            uint32_t value = in[4];
            if (reg == HCR_CLOCK_CONTROL && (value & 1u)) value |= 2u;
            if (reg == HCR_SOFTWARE_RESET) value = 0;  /* done at once */
            hcr_set(card, reg, size, value);
        }
        return RTVSD_OK;
    }
    if (ioctl == RTVSD_IOCTL_RESETCARD) {
        card->state = RTVSD_STATE_STBY;
        card->app_cmd = 0;
        card->bus_width = 0;
        if (out != 0 && out_len >= 4u) out[0] = RTVSD_RCA << 16;
        return RTVSD_OK;
    }
    if (ioctl == RTVSD_IOCTL_GETSTATUS) {
        if (out == 0 || out_len < 4u) return RTVSD_EINVAL;
        out[0] = RTVSD_STATUS_INSERTED | RTVSD_STATUS_INITIALIZED | (card->sdhc ? RTVSD_STATUS_SDHC : 0u);
        return RTVSD_OK;
    }
    if (ioctl == RTVSD_IOCTL_GETOCR) {
        if (out == 0 || out_len < 4u) return RTVSD_EINVAL;
        out[0] = RTVSD_OCR | (card->sdhc ? RTVSD_OCR_CCS : 0u);
        return RTVSD_OK;
    }
    if (ioctl == RTVSD_IOCTL_SETCLK || ioctl == RTVSD_IOCTL_SETBUSWIDTH) return RTVSD_OK;
    return RTVSD_EINVAL;
}

static uint32_t r1(const struct rtvsd_card* card, uint32_t app) {
    return (card->state << 9) | RTVSD_R1_READY_FOR_DATA | (app ? RTVSD_R1_APP_CMD : 0u);
}

static void answer_done(struct rtvsd_answer* a, uint32_t reply0) {
    a->kind = RTVSD_DONE;
    a->result = RTVSD_OK;
    a->reply[0] = reply0;
}

/* A read or a write: the image's sectors, or an error in the reply. */
static void transfer(struct rtvsd_card* card, const struct rtvsd_request* req, uint32_t data_len, uint32_t kind,
                     uint32_t single, struct rtvsd_answer* a) {
    const uint32_t count = single ? 1u : req->blk_cnt;
    uint32_t sector;
    if (card->sdhc) {
        sector = req->arg;
    } else {
        if (req->arg & (RTVSD_SECTOR_BYTES - 1u)) {
            a->kind = RTVSD_DONE;
            a->result = RTVSD_EINVAL;
            a->reply[0] = r1(card, 0) | RTVSD_R1_ADDRESS_ERROR;
            return;
        }
        sector = req->arg >> 9;
    }
    if (req->blk_size != RTVSD_SECTOR_BYTES || count == 0 || count > (data_len >> 9) ||
        (kind == RTVSD_WRITE && card->read_only)) {
        a->kind = RTVSD_DONE;
        a->result = RTVSD_EINVAL;
        a->reply[0] = r1(card, 0);
        return;
    }
    if (sector >= card->sectors || count > card->sectors - sector) {
        a->kind = RTVSD_DONE;
        a->result = RTVSD_EINVAL;
        a->reply[0] = r1(card, 0) | RTVSD_R1_OUT_OF_RANGE;
        return;
    }
    a->kind = kind;
    a->result = RTVSD_OK;
    a->reply[0] = r1(card, 0);
    a->sector = sector;
    a->count = count;
}

void rtvsd_command(struct rtvsd_card* card, const struct rtvsd_request* req, uint32_t data_len,
                   struct rtvsd_answer* a) {
    const uint32_t app = card->app_cmd;
    const uint32_t cmd = req->cmd;
    uint32_t i;
    a->kind = RTVSD_DONE;
    a->result = RTVSD_OK;
    for (i = 0; i < 4u; ++i) a->reply[i] = 0;
    a->sector = a->count = a->data_bytes = 0;
    card->app_cmd = 0;

    if (cmd == RTVSD_CMD_EVENT_REGISTER) {
        a->kind = RTVSD_EVENT_HOLD;
        return;
    }
    if (cmd == RTVSD_CMD_EVENT_UNREGISTER) {
        a->kind = RTVSD_EVENT_RELEASE;
        return;
    }
    if (app) {
        if (cmd == RTVSD_ACMD_SET_BUS_WIDTH) {
            card->bus_width = req->arg & 3u;
            answer_done(a, r1(card, 1));
            return;
        }
        if (cmd == RTVSD_ACMD_SEND_OP_COND) {
            if (card->state == STATE_IDLE) card->state = STATE_READY;
            answer_done(a, RTVSD_OCR | (card->sdhc ? RTVSD_OCR_CCS : 0u));
            return;
        }
        if (cmd == RTVSD_ACMD_SD_STATUS || cmd == RTVSD_ACMD_SEND_SCR) {
            const uint32_t bytes = cmd == RTVSD_ACMD_SEND_SCR ? 8u : 64u;
            for (i = 0; i < 64u; ++i) a->data[i] = 0;
            if (cmd == RTVSD_ACMD_SEND_SCR) {
                a->data[0] = card->sdhc ? 0x02u : 0x01u;              /* SD_SPEC 2.00 / 1.10 */
                a->data[1] = (uint8_t)((card->sdhc ? 0x30u : 0x20u) | 0x05u);  /* security, 1 and 4 bit bus */
            } else {
                a->data[0] = card->bus_width == 2u ? 0x80u : 0x00u;  /* DAT_BUS_WIDTH */
            }
            if (data_len < bytes) {
                a->result = RTVSD_EINVAL;
                a->reply[0] = r1(card, 1);
                return;
            }
            a->kind = RTVSD_DATA;
            a->data_bytes = bytes;
            a->reply[0] = r1(card, 1);
            return;
        }
        if (cmd == RTVSD_ACMD_SET_CLR_DETECT) {
            answer_done(a, r1(card, 1));
            return;
        }
        /* Anything else after CMD55 is the plain command. */
    }
    if (cmd == RTVSD_CMD_GO_IDLE) {
        card->state = STATE_IDLE;
        card->bus_width = 0;
        answer_done(a, 0);
        return;
    }
    if (cmd == RTVSD_CMD_SEND_IF_COND) {
        answer_done(a, req->arg & 0xFFFu);  /* the voltage accepted, the pattern echoed */
        return;
    }
    if (cmd == RTVSD_CMD_ALL_SEND_CID || cmd == RTVSD_CMD_SEND_CID) {
        if (cmd == RTVSD_CMD_ALL_SEND_CID) card->state = STATE_IDENT;
        rtvsd_cid(a->reply);
        return;
    }
    if (cmd == RTVSD_CMD_SEND_CSD) {
        rtvsd_csd(card, a->reply);
        return;
    }
    if (cmd == RTVSD_CMD_SEND_RCA) {
        const uint32_t before = card->state;
        card->state = RTVSD_STATE_STBY;
        answer_done(a, (RTVSD_RCA << 16) | (before << 9) | RTVSD_R1_READY_FOR_DATA);
        return;
    }
    if (cmd == RTVSD_CMD_SELECT) {
        const uint32_t before = card->state;
        card->state = (req->arg >> 16) == RTVSD_RCA ? RTVSD_STATE_TRAN : RTVSD_STATE_STBY;
        answer_done(a, (before << 9) | RTVSD_R1_READY_FOR_DATA);
        return;
    }
    if (cmd == RTVSD_CMD_READ_SINGLE || cmd == RTVSD_CMD_READ_MULTIPLE) {
        transfer(card, req, data_len, RTVSD_READ, cmd == RTVSD_CMD_READ_SINGLE, a);
        return;
    }
    if (cmd == RTVSD_CMD_WRITE_SINGLE || cmd == RTVSD_CMD_WRITE_MULTIPLE) {
        transfer(card, req, data_len, RTVSD_WRITE, cmd == RTVSD_CMD_WRITE_SINGLE, a);
        return;
    }
    if (cmd == RTVSD_CMD_APP) {
        card->app_cmd = 1;
        answer_done(a, r1(card, 1));
        return;
    }
    /* STOP, SEND_STATUS, SET_BLOCKLEN and anything else: the status. */
    answer_done(a, r1(card, 0));
}
