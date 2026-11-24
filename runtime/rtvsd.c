/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The virtual SD card (rtvsd.h). */
#include "rtvsd.h"

/* Card states (R1 bits 9-12). */
#define STATE_IDLE 0u
#define STATE_READY 1u
#define STATE_IDENT 2u

/* ---- The card ----------------------------------------------------------- */

/* A paged card's index: page 0 starts at sector 0, the others rise, each
 * inside the image. */
static int index_ok(const struct rtvsd_card* card) {
    const uint32_t pages = rtvsd_page_count(card->extent_count);
    uint32_t i;
    if (rtvsd_pages(card)[0].file_sector != 0) return 0;
    for (i = 1; i < pages; ++i) {
        const uint32_t first = rtvsd_pages(card)[i].file_sector;
        if (first <= rtvsd_pages(card)[i - 1u].file_sector || first >= card->sectors) return 0;
    }
    return 1;
}

int rtvsd_reset(struct rtvsd_card* card) {
    uint32_t i, next = 0;
    card->sdhc = card->sectors > RTVSD_SDSC_MAX_SECTORS ? 1u : 0u;
    card->state = RTVSD_STATE_STBY;
    card->app_cmd = 0;
    card->bus_width = 0;
    for (i = 0; i < 64u; ++i) card->hcr[i] = 0;
    for (i = 0; i < RTVSD_PAGE_SLOTS; ++i) card->page_held[i] = RTVSD_NO_PAGE;
    card->page_last = 0;
    card->page_coming = RTVSD_NO_PAGE;
    card->page_coming_slot = 0;
    if (card->sectors == 0 || card->extent_count == 0 || card->extent_count > RTVSD_MAX_EXTENTS) return -1;
    if (card->paged) return index_ok(card) ? 0 : -1;
    for (i = 0; i < card->extent_count; ++i) {
        const struct rtvsd_extent* e = &rtvsd_extents(card)[i];
        if (e->file_sector != next || e->count == 0 || e->device_sector + e->count < e->device_sector) return -1;
        next += e->count;
        if (next < e->count) return -1;
    }
    return next >= card->sectors ? 0 : -1;
}

/* ---- A paged card's piece list ------------------------------------------- */

/* The page whose pieces hold `sector` (inside the image), by binary search
 * of the index. */
static uint32_t page_of(const struct rtvsd_card* card, uint32_t sector) {
    uint32_t lo = 0, hi = rtvsd_page_count(card->extent_count);
    while (hi - lo > 1u) {
        const uint32_t mid = lo + ((hi - lo) >> 1);
        if (rtvsd_pages(card)[mid].file_sector <= sector) lo = mid;
        else hi = mid;
    }
    return lo;
}

/* The page's bytes are the device's, big-endian whatever the CPU. */
static uint32_t load_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void page_extent(const uint8_t* page, uint32_t i, struct rtvsd_extent* out) {
    const uint8_t* p = page + i * 12u;
    out->file_sector = load_be32(p);
    out->device_sector = load_be32(p + 4);
    out->count = load_be32(p + 8);
}

uint32_t rtvsd_page_wanted(struct rtvsd_card* card, uint32_t sector, uint32_t* device_sector, uint8_t** slot_address) {
    uint32_t page, slot, spare;
    if (!card->paged || sector >= card->sectors) return 0;
    page = page_of(card, sector);
    for (slot = 0; slot < RTVSD_PAGE_SLOTS; ++slot) {
        if (card->page_held[slot] == page) {
            card->page_last = slot;
            return 0;
        }
    }
    /* Not the slot used last: a request's other lookup may need its page. */
    spare = (card->page_last + 1u) % RTVSD_PAGE_SLOTS;
    card->page_held[spare] = RTVSD_NO_PAGE;
    card->page_coming = page;
    card->page_coming_slot = spare;
    card->page_last = spare;
    *device_sector = rtvsd_pages(card)[page].device_sector;
    *slot_address = rtvsd_page_slot(card, spare);
    return 1;
}

void rtvsd_page_arrived(struct rtvsd_card* card) {
    if (card->page_coming_slot < RTVSD_PAGE_SLOTS) card->page_held[card->page_coming_slot] = card->page_coming;
}

/* The extent holding `sector` (inside the image), by binary search, in
 * `*out`: from the table, or from the slot holding its page. 0 when there
 * is none (a paged card whose page is not held). */
static int extent_of(const struct rtvsd_card* card, uint32_t sector, struct rtvsd_extent* out) {
    if (!card->paged) {
        uint32_t lo = 0, hi = card->extent_count;
        while (hi - lo > 1u) {
            const uint32_t mid = lo + ((hi - lo) >> 1);
            if (rtvsd_extents(card)[mid].file_sector <= sector) lo = mid;
            else hi = mid;
        }
        /* Field by field: a struct copy could become a memcpy call, which the blob has none of. */
        out->file_sector = rtvsd_extents(card)[lo].file_sector;
        out->device_sector = rtvsd_extents(card)[lo].device_sector;
        out->count = rtvsd_extents(card)[lo].count;
    } else {
        const uint32_t page = page_of(card, sector);
        const uint32_t before = page * RTVSD_PAGE_EXTENTS;
        const uint32_t in_page = card->extent_count - before < RTVSD_PAGE_EXTENTS ? card->extent_count - before
                                                                                  : RTVSD_PAGE_EXTENTS;
        const uint8_t* bytes = 0;
        uint32_t slot, lo = 0, hi = in_page;
        for (slot = 0; slot < RTVSD_PAGE_SLOTS; ++slot)
            if (card->page_held[slot] == page) bytes = rtvsd_page_slot(card, slot);
        if (bytes == 0) return 0;
        while (hi - lo > 1u) {
            const uint32_t mid = lo + ((hi - lo) >> 1);
            if (load_be32(bytes + mid * 12u) <= sector) lo = mid;
            else hi = mid;
        }
        page_extent(bytes, lo, out);
    }
    return sector >= out->file_sector && sector - out->file_sector < out->count;
}

uint32_t rtvsd_map(const struct rtvsd_card* card, uint32_t sector, uint32_t count, uint32_t* device_sector) {
    struct rtvsd_extent e;
    uint32_t into, run;
    if (count == 0 || sector >= card->sectors) return 0;
    if (count > card->sectors - sector) count = card->sectors - sector;
    if (!extent_of(card, sector, &e)) return 0;
    into = sector - e.file_sector;
    run = e.count - into;
    *device_sector = e.device_sector + into;
    return run < count ? run : count;
}

uint32_t rtvsd_piece(const struct rtvsd_card* card, uint32_t sector, uint32_t* first) {
    struct rtvsd_extent e;
    if (sector >= card->sectors) return 0;
    if (!extent_of(card, sector, &e)) return 0;
    *first = e.file_sector;
    return e.count;
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
    if (cmd == RTVSD_CMD_ALL_SEND_CID || cmd == RTVSD_CMD_SEND_CID || cmd == RTVSD_CMD_SEND_CSD) {
        /* The registers the loader built (rtvsd_card.cid, .csd). */
        const uint32_t* reg = cmd == RTVSD_CMD_SEND_CSD ? card->csd : card->cid;
        if (cmd == RTVSD_CMD_ALL_SEND_CID) card->state = STATE_IDENT;
        for (i = 0; i < 4u; ++i) a->reply[i] = reg[i];
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
