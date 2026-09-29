/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The virtual SD card blob (vsd_hook.h). */
#include "vsd_hook.h"

/* The SD slot's card commands the blob sends itself (wiibrew, libogc
 * wiisd.c: the command types and response kinds). */
#define SD_CMD_READ_MULTIPLE 18u
#define SD_CMD_WRITE_MULTIPLE 25u
#define SD_CMD_SEND_STATUS 13u
#define SD_CMDTYPE_AC 3u
#define SD_RESPONSE_R1 1u
#define SD_STATE_RCV 6u
#define SD_STATE_PRG 7u
/* d2x's /dev/sdio/sdhc and /dev/usb2. */
#define D2X_SD_READ 2u
#define D2X_SD_WRITE 3u
#define D2X_SD_ISINSERTED 4u
#define UMS_BASE 0x554D5300u
#define UMS_GET_CAPACITY (UMS_BASE + 2u)
#define UMS_READ_SECTORS (UMS_BASE + 3u)
#define UMS_WRITE_SECTORS (UMS_BASE + 4u)

#define PIECE_SECTORS_SD 128u  /* one request on the SD card, straight into the game's buffer */
#define IOS_EINVAL (-4)
#define IOS_EBUSY (-8)

typedef int32_t (*open_fn)(uint32_t path, uint32_t mode);
typedef int32_t (*ioctl_fn)(int32_t fd, uint32_t ioctl, uint32_t in, uint32_t in_len, uint32_t out, uint32_t out_len);
typedef int32_t (*ioctlv_fn)(int32_t fd, uint32_t ioctl, uint32_t in_count, uint32_t out_count, uint32_t vec);
typedef int32_t (*ioctl_async_fn)(int32_t fd, uint32_t ioctl, uint32_t in, uint32_t in_len, uint32_t out,
                                  uint32_t out_len, uint32_t callback, uint32_t user_data);
typedef int32_t (*ioctlv_async_fn)(int32_t fd, uint32_t ioctl, uint32_t in_count, uint32_t out_count, uint32_t vec,
                                   uint32_t callback, uint32_t user_data);

/* ---- Memory ------------------------------------------------------------- */

static void flush_range(uint32_t address, uint32_t length) {
    uint32_t line = address & ~31u;
    const uint32_t end = address + length;
    for (; line < end; line += 32u) __asm__ volatile("dcbf 0, %0" : : "r"(line) : "memory");
    __asm__ volatile("sync" : : : "memory");
}

/* Drops the CPU's lines over a buffer IOS has just written. */
static void invalidate_range(uint32_t address, uint32_t length) {
    uint32_t line = address & ~31u;
    const uint32_t end = address + length;
    for (; line < end; line += 32u) __asm__ volatile("dcbi 0, %0" : : "r"(line) : "memory");
    __asm__ volatile("sync" : : : "memory");
}

static void copy(uint8_t* dst, const uint8_t* src, uint32_t n) {
    while (n--) *dst++ = *src++;
}

static uint32_t interrupts_off(void) {
    uint32_t msr;
    __asm__ volatile("mfmsr %0" : "=r"(msr));
    __asm__ volatile("mtmsr %0" : : "r"(msr & ~0x8000u) : "memory");
    return msr;
}

static void interrupts_restore(uint32_t msr) {
    __asm__ volatile("mtmsr %0" : : "r"(msr) : "memory");
}

/* A physical address as the SDK passes it in a command block, to the
 * cached one the CPU uses. */
static uint32_t cached(uint32_t address) {
    return address < 0x80000000u ? (address | 0x80000000u) : address;
}

static struct vsd_state* state_of(const struct vsd_context* ctx) {
    struct vsd_state* st = (struct vsd_state*)(uintptr_t)ctx->state;
    return st != 0 && st->magic == VSD_STATE_MAGIC ? st : 0;
}

/* "/dev/sdio/slot0", compared without a string constant (the blob has no
 * data section). */
static int is_slot0(const char* p) {
    return p[0] == '/' && p[1] == 'd' && p[2] == 'e' && p[3] == 'v' && p[4] == '/' && p[5] == 's' && p[6] == 'd' &&
           p[7] == 'i' && p[8] == 'o' && p[9] == '/' && p[10] == 's' && p[11] == 'l' && p[12] == 'o' && p[13] == 't' &&
           p[14] == '0' && p[15] == 0;
}

/* ---- Requests on the device ------------------------------------------- */

/* Whether a piece can land in / leave from the game's buffer itself. */
static int direct_ok(const struct vsd_state* st, uint32_t buffer) {
    if (buffer & 31u) return 0;
    if (st->backend == VSD_BACKEND_USB) {
        /* d2x's USB driver moves data into and out of MEM2 only. */
        const uint32_t physical = buffer & 0x3FFFFFFFu;
        return physical >= 0x10000000u && physical < 0x14000000u;
    }
    return 1;
}

/* Builds the request for the transfer's next piece in the state's blocks
 * and returns the ioctl, with the vector counts. */
static uint32_t build_piece(struct vsd_state* st, uint32_t device_sector, uint32_t sectors, uint32_t data,
                            uint32_t* in_count, uint32_t* out_count) {
    const uint32_t bytes = sectors * RTVSD_SECTOR_BYTES;
    const uint32_t write = st->transfer.write;
    if (st->backend == VSD_BACKEND_SLOT0) {
        struct rtvsd_request* rq = &st->request;
        rq->cmd = write ? SD_CMD_WRITE_MULTIPLE : SD_CMD_READ_MULTIPLE;
        rq->cmd_type = SD_CMDTYPE_AC;
        rq->rsp_type = SD_RESPONSE_R1;
        rq->arg = st->sdhc ? device_sector : device_sector * RTVSD_SECTOR_BYTES;
        rq->blk_cnt = sectors;
        rq->blk_size = RTVSD_SECTOR_BYTES;
        rq->dma_addr = data;
        rq->isdma = 1;
        rq->pad0 = 0;
        st->vec[0].data = (uint32_t)(uintptr_t)rq;
        st->vec[0].len = sizeof(*rq);
        st->vec[1].data = data;
        st->vec[1].len = bytes;
        st->vec[2].data = (uint32_t)(uintptr_t)st->response;
        st->vec[2].len = 16;
        flush_range((uint32_t)(uintptr_t)rq, sizeof(*rq));
        flush_range((uint32_t)(uintptr_t)st->vec, sizeof(st->vec));
        flush_range((uint32_t)(uintptr_t)st->response, sizeof(st->response));
        flush_range(data, bytes);
        *in_count = 2;
        *out_count = 1;
        return RTVSD_IOCTL_SENDCMD;
    }
    st->words[0] = device_sector;
    st->words[1] = sectors;
    st->vec[0].data = (uint32_t)(uintptr_t)&st->words[0];
    st->vec[0].len = 4;
    st->vec[1].data = (uint32_t)(uintptr_t)&st->words[1];
    st->vec[1].len = 4;
    st->vec[2].data = data;
    st->vec[2].len = bytes;
    flush_range((uint32_t)(uintptr_t)st->words, sizeof(st->words));
    flush_range((uint32_t)(uintptr_t)st->vec, sizeof(st->vec));
    flush_range(data, bytes);
    if (st->backend == VSD_BACKEND_D2X_SD) {
        *in_count = write ? 3u : 2u;
        *out_count = write ? 0u : 1u;
        return write ? D2X_SD_WRITE : D2X_SD_READ;
    }
    *in_count = 2;
    *out_count = 1;
    return write ? UMS_WRITE_SECTORS : UMS_READ_SECTORS;
}

/* CMD13 on the SD slot, its answer in `response`. */
static void build_status(struct vsd_state* st) {
    struct rtvsd_request* rq = &st->request;
    uint32_t i;
    rq->cmd = SD_CMD_SEND_STATUS;
    rq->cmd_type = SD_CMDTYPE_AC;
    rq->rsp_type = SD_RESPONSE_R1;
    rq->arg = st->rca << 16;
    rq->blk_cnt = rq->blk_size = rq->dma_addr = rq->isdma = rq->pad0 = 0;
    for (i = 0; i < 8u; ++i) st->response[i] = 0;
    flush_range((uint32_t)(uintptr_t)rq, sizeof(*rq));
    flush_range((uint32_t)(uintptr_t)st->response, sizeof(st->response));
}

/* After a write to the SD slot: whether the card still programs. */
static int needs_settle(const struct vsd_state* st) {
    return st->backend == VSD_BACKEND_SLOT0 && st->rca != 0 && st->original[VSD_IOCTL_ASYNC] != 0;
}

static int still_busy(struct vsd_state* st) {
    uint32_t state;
    invalidate_range((uint32_t)(uintptr_t)st->response, sizeof(st->response));
    state = (st->response[0] >> 9) & 15u;
    return state == SD_STATE_RCV || state == SD_STATE_PRG;
}

/* The next piece of the transfer: where it goes on the device, and
 * whether it passes through the bounce buffer (a write's bytes are copied
 * into it here). 0 when the image has no such sector (cannot happen with
 * a checked command). */
static uint32_t next_piece(struct vsd_state* st, uint32_t* device_sector, uint32_t* data) {
    struct vsd_transfer* t = &st->transfer;
    const uint32_t buffer = t->buffer + t->done * RTVSD_SECTOR_BYTES;
    uint32_t sectors = rtvsd_map(&st->card, t->sector + t->done, t->count - t->done, device_sector);
    if (sectors == 0) return 0;
    t->bounced = !direct_ok(st, buffer);
    if (t->bounced) {
        if (sectors > VSD_BOUNCE_BYTES / RTVSD_SECTOR_BYTES) sectors = VSD_BOUNCE_BYTES / RTVSD_SECTOR_BYTES;
        if (t->write) copy(st->bounce, (const uint8_t*)(uintptr_t)buffer, sectors * RTVSD_SECTOR_BYTES);
        *data = (uint32_t)(uintptr_t)st->bounce;
    } else {
        const uint32_t most = st->backend == VSD_BACKEND_SLOT0 ? PIECE_SECTORS_SD : VSD_BOUNCE_BYTES / RTVSD_SECTOR_BYTES;
        if (sectors > most) sectors = most;
        *data = buffer;
    }
    return sectors;
}

/* A piece landed: a read's bytes into the game's buffer. */
static void piece_done(struct vsd_state* st) {
    struct vsd_transfer* t = &st->transfer;
    const uint32_t bytes = t->chunk * RTVSD_SECTOR_BYTES;
    const uint32_t buffer = t->buffer + t->done * RTVSD_SECTOR_BYTES;
    if (!t->write) {
        if (t->bounced) {
            invalidate_range((uint32_t)(uintptr_t)st->bounce, bytes);
            copy((uint8_t*)(uintptr_t)buffer, st->bounce, bytes);
            flush_range(buffer, bytes);
        }
        st->sectors_read += t->chunk;
    } else {
        st->sectors_written += t->chunk;
    }
    t->done += t->chunk;
}

/* The game's reply buffer. */
static void write_reply(uint32_t out, const uint32_t reply[4]) {
    uint32_t* p = (uint32_t*)(uintptr_t)out;
    uint32_t i;
    if (out == 0) return;
    for (i = 0; i < 4u; ++i) p[i] = reply[i];
    flush_range(out, 16);
}

/* ---- The synchronous path --------------------------------------------- */

static int32_t transfer_sync(struct vsd_state* st) {
    struct vsd_transfer* t = &st->transfer;
    const ioctlv_fn ioctlv = (ioctlv_fn)(uintptr_t)st->original[VSD_IOCTLV];
    const ioctl_fn ioctl = (ioctl_fn)(uintptr_t)st->original[VSD_IOCTL];
    if (ioctlv == 0) return IOS_EINVAL;
    while (t->done < t->count) {
        uint32_t device_sector = 0, data = 0, in_count, out_count, number;
        int32_t r = -1;
        t->chunk = next_piece(st, &device_sector, &data);
        if (t->chunk == 0) return IOS_EINVAL;
        for (t->tries = 0; t->tries < VSD_TRIES; ++t->tries) {
            number = build_piece(st, device_sector, t->chunk, data, &in_count, &out_count);
            r = ioctlv(st->fd, number, in_count, out_count, (uint32_t)(uintptr_t)st->vec);
            if (r >= 0) break;
            ++st->failures;
            st->last_error = (uint32_t)r;
        }
        if (r < 0) {
            ++st->gave_up;
            return r;
        }
        if (t->write && st->backend == VSD_BACKEND_SLOT0 && st->rca != 0 && ioctl != 0) {
            for (t->polls = 0; t->polls < VSD_SETTLE_POLLS; ++t->polls) {
                build_status(st);
                if (ioctl(st->fd, RTVSD_IOCTL_SENDCMD, (uint32_t)(uintptr_t)&st->request, sizeof(st->request),
                          (uint32_t)(uintptr_t)st->response, 16) < 0 ||
                    !still_busy(st))
                    break;
            }
        }
        piece_done(st);
    }
    return RTVSD_OK;
}

/* ---- The asynchronous path -------------------------------------------- */

/* The null round trip that carries an answer: a status request on the
 * device, its answer in the slot's own line. */
static int32_t deliver(struct vsd_state* st, uint32_t callback, uint32_t user_data, int32_t result) {
    const ioctl_async_fn ioctl_async = (ioctl_async_fn)(uintptr_t)st->original[VSD_IOCTL_ASYNC];
    const ioctlv_async_fn ioctlv_async = (ioctlv_async_fn)(uintptr_t)st->original[VSD_IOCTLV_ASYNC];
    struct vsd_deliver* slot = 0;
    uint32_t i, msr = interrupts_off();
    int32_t r;
    for (i = 0; i < VSD_DELIVERS; ++i) {
        if (!st->deliver[i].in_use) {
            slot = &st->deliver[i];
            slot->in_use = 1;
            break;
        }
    }
    interrupts_restore(msr);
    if (slot == 0) return IOS_EBUSY;
    slot->kind = VSD_TAG_DELIVER;
    slot->callback = callback;
    slot->user_data = user_data;
    slot->result = result;
    flush_range((uint32_t)(uintptr_t)slot->status, sizeof(slot->status));
    if (st->backend == VSD_BACKEND_SLOT0 && ioctl_async != 0) {
        r = ioctl_async(st->fd, RTVSD_IOCTL_GETSTATUS, 0, 0, (uint32_t)(uintptr_t)slot->status, 4, st->complete,
                        (uint32_t)(uintptr_t)slot);
    } else if (ioctlv_async != 0) {
        /* d2x's devices answer ioctlvs only. */
        struct vsd_ioctlv* v = (struct vsd_ioctlv*)(uintptr_t)&slot->status[2];
        const uint32_t out_count = st->backend == VSD_BACKEND_USB ? 1u : 0u;
        v->data = (uint32_t)(uintptr_t)&slot->status[0];
        v->len = 4;
        flush_range((uint32_t)(uintptr_t)slot->status, sizeof(slot->status));
        r = ioctlv_async(st->fd, st->backend == VSD_BACKEND_USB ? UMS_GET_CAPACITY : D2X_SD_ISINSERTED, 0, out_count,
                         (uint32_t)(uintptr_t)v, st->complete, (uint32_t)(uintptr_t)slot);
    } else if (ioctl_async != 0) {
        r = ioctl_async(st->fd, RTVSD_IOCTL_GETSTATUS, 0, 0, (uint32_t)(uintptr_t)slot->status, 4, st->complete,
                        (uint32_t)(uintptr_t)slot);
    } else {
        r = IOS_EINVAL;
    }
    if (r < 0) {
        slot->in_use = 0;
        return r;
    }
    ++st->delivers;
    return RTVSD_OK;
}

/* Sends the transfer's next request (a piece, or a CMD13 while the card
 * settles). Returns what IOS said to the send. */
static int32_t transfer_send(struct vsd_state* st) {
    struct vsd_transfer* t = &st->transfer;
    const uint32_t tag = (uint32_t)(uintptr_t)t;
    if (t->phase == VSD_PHASE_SETTLE) {
        const ioctl_async_fn ioctl_async = (ioctl_async_fn)(uintptr_t)st->original[VSD_IOCTL_ASYNC];
        build_status(st);
        return ioctl_async(st->fd, RTVSD_IOCTL_SENDCMD, (uint32_t)(uintptr_t)&st->request, sizeof(st->request),
                           (uint32_t)(uintptr_t)st->response, 16, st->complete, tag);
    }
    {
        const ioctlv_async_fn ioctlv_async = (ioctlv_async_fn)(uintptr_t)st->original[VSD_IOCTLV_ASYNC];
        uint32_t device_sector = 0, data = 0, in_count, out_count, number;
        if (ioctlv_async == 0) return IOS_EINVAL;
        if (t->tries == 0) {
            t->chunk = next_piece(st, &device_sector, &data);
            if (t->chunk == 0) return IOS_EINVAL;
            t->piece_device = device_sector;
            t->piece_data = data;
        } else {
            device_sector = t->piece_device;
            data = t->piece_data;
        }
        number = build_piece(st, device_sector, t->chunk, data, &in_count, &out_count);
        return ioctlv_async(st->fd, number, in_count, out_count, (uint32_t)(uintptr_t)st->vec, st->complete, tag);
    }
}

/* Ends the transfer: the reply into the game's buffer, the transfer
 * record free, the game's callback and result for the tail call. */
static void transfer_end(struct vsd_state* st, int32_t result, int32_t* out_result, uint32_t* callback,
                         uint32_t* user_data) {
    struct vsd_transfer* t = &st->transfer;
    uint32_t reply[4];
    reply[0] = t->reply[0];
    reply[1] = reply[2] = reply[3] = 0;
    if (result < 0) {
        ++st->gave_up;
        st->last_error = (uint32_t)result;
    }
    write_reply(t->reply_out, reply);
    *out_result = result < 0 ? result : RTVSD_OK;
    *callback = t->callback;
    *user_data = t->user_data;
    t->in_use = 0;
}

void vsd_on_complete(struct vsd_context* ctx, int32_t* result, void* tag, uint32_t* callback, uint32_t* user_data);
void vsd_on_complete(struct vsd_context* ctx, int32_t* result, void* tag, uint32_t* callback, uint32_t* user_data) {
    struct vsd_state* st = state_of(ctx);
    const uint32_t kind = tag != 0 ? *(const uint32_t*)tag : 0;
    *callback = 0;
    *user_data = 0;
    if (st == 0) return;
    if (kind == VSD_TAG_DELIVER) {
        struct vsd_deliver* slot = (struct vsd_deliver*)tag;
        *result = slot->result;
        *callback = slot->callback;
        *user_data = slot->user_data;
        slot->in_use = 0;
        return;
    }
    if (kind != VSD_TAG_TRANSFER) return;
    {
        struct vsd_transfer* t = (struct vsd_transfer*)tag;
        int32_t r = *result;
        if (t->phase == VSD_PHASE_SETTLE) {
            if (r >= 0 && still_busy(st) && ++t->polls < VSD_SETTLE_POLLS) {
                if (transfer_send(st) >= 0) return;
            }
            t->phase = VSD_PHASE_DATA;
            piece_done(st);
        } else if (r < 0) {
            ++st->failures;
            st->last_error = (uint32_t)r;
            if (++t->tries < VSD_TRIES && transfer_send(st) >= 0) return;
            transfer_end(st, r, result, callback, user_data);
            return;
        } else if (t->write && needs_settle(st)) {
            t->phase = VSD_PHASE_SETTLE;
            t->polls = 0;
            if (transfer_send(st) >= 0) return;
            t->phase = VSD_PHASE_DATA;
            piece_done(st);
        } else {
            piece_done(st);
        }
        t->tries = 0;
        if (t->done < t->count) {
            r = transfer_send(st);
            if (r >= 0) return;
            transfer_end(st, r, result, callback, user_data);
            return;
        }
        transfer_end(st, RTVSD_OK, result, callback, user_data);
    }
}

/* ---- The game's calls on the card ---------------------------------------- */

/* A SENDCMD: `data`/`data_len` the game's data buffer (0 when none),
 * `reply_out` its 16-byte reply buffer. Async when `callback` is set (or
 * `async`: a callback may be 0). */
static int32_t send_command(struct vsd_state* st, const struct rtvsd_request* req, uint32_t data, uint32_t data_len,
                            uint32_t reply_out, int async, uint32_t callback, uint32_t user_data) {
    struct rtvsd_answer a;
    uint32_t msr;
    rtvsd_command(&st->card, req, data != 0 ? data_len : 0, &a);
    if (a.kind == RTVSD_EVENT_HOLD) {
        if (!async) return RTVSD_OK;
        st->event_held = 1;
        st->event_callback = callback;
        st->event_user_data = user_data;
        ++st->events;
        return RTVSD_OK;
    }
    if (a.kind == RTVSD_EVENT_RELEASE) {
        if (st->event_held) {
            st->event_held = 0;
            deliver(st, st->event_callback, st->event_user_data, (int32_t)RTVSD_EVENT_INVALID);
        }
        return async ? deliver(st, callback, user_data, RTVSD_OK) : RTVSD_OK;
    }
    if (a.kind == RTVSD_DATA) {
        copy((uint8_t*)(uintptr_t)data, a.data, a.data_bytes);
        flush_range(data, a.data_bytes);
        a.kind = RTVSD_DONE;
    }
    if (a.kind == RTVSD_DONE) {
        write_reply(reply_out, a.reply);
        return async ? deliver(st, callback, user_data, a.result) : a.result;
    }
    /* A read or a write of the image. */
    msr = interrupts_off();
    if (st->transfer.in_use) {
        interrupts_restore(msr);
        ++st->busy;
        return IOS_EBUSY;
    }
    st->transfer.in_use = 1;
    interrupts_restore(msr);
    {
        struct vsd_transfer* t = &st->transfer;
        t->kind = VSD_TAG_TRANSFER;
        t->callback = async ? callback : 0;
        t->user_data = user_data;
        t->write = a.kind == RTVSD_WRITE;
        t->buffer = data;
        t->sector = a.sector;
        t->count = a.count;
        t->done = 0;
        t->chunk = 0;
        t->phase = VSD_PHASE_DATA;
        t->tries = 0;
        t->polls = 0;
        t->reply_out = reply_out;
        t->reply[0] = a.reply[0];
        t->reply[1] = t->reply[2] = t->reply[3] = 0;
        if (t->write) ++st->writes;
        else ++st->reads;
        if (!async) {
            int32_t r = transfer_sync(st);
            write_reply(reply_out, t->reply);
            if (r < 0) {
                ++st->gave_up;
                st->last_error = (uint32_t)r;
            }
            t->in_use = 0;
            return r < 0 ? r : RTVSD_OK;
        }
        {
            const int32_t r = transfer_send(st);
            if (r < 0) {
                /* Not even the first request went out: an error now, no callback. */
                t->in_use = 0;
                ++st->gave_up;
                st->last_error = (uint32_t)r;
                return r;
            }
        }
        return RTVSD_OK;
    }
}

/* IOS_Ioctl(Async) on the card. */
static int32_t on_ioctl(struct vsd_state* st, const uint32_t* a, int async) {
    const uint32_t number = a[1], in = a[2], in_len = a[3], out = a[4], out_len = a[5];
    const uint32_t callback = async ? a[6] : 0, user_data = async ? a[7] : 0;
    if (number == RTVSD_IOCTL_SENDCMD) {
        const struct rtvsd_request* req = (const struct rtvsd_request*)(uintptr_t)in;
        uint32_t data = 0, data_len = 0;
        if (in == 0 || in_len < sizeof(struct rtvsd_request)) return async ? deliver(st, callback, user_data, IOS_EINVAL) : IOS_EINVAL;
        if (req->isdma && req->dma_addr != 0) {
            data = cached(req->dma_addr);
            data_len = req->blk_cnt * req->blk_size;
        }
        return send_command(st, req, data, data_len, out_len >= 16u ? out : 0, async, callback, user_data);
    }
    {
        int32_t r = rtvsd_ioctl(&st->card, number, (const uint32_t*)(uintptr_t)in, in_len, (uint32_t*)(uintptr_t)out,
                                out_len);
        if (out != 0 && out_len != 0) flush_range(out, out_len < 32u ? out_len : 32u);
        return async ? deliver(st, callback, user_data, r) : r;
    }
}

/* IOS_Ioctlv(Async) on the card: SENDCMD with the command block, the data
 * (when the command has any) and the reply. */
static int32_t on_ioctlv(struct vsd_state* st, const uint32_t* a, int async) {
    const uint32_t number = a[1], in_count = a[2], out_count = a[3], vec = a[4];
    const uint32_t callback = async ? a[5] : 0, user_data = async ? a[6] : 0;
    const struct vsd_ioctlv* v = (const struct vsd_ioctlv*)(uintptr_t)vec;
    const struct rtvsd_request* req;
    uint32_t data = 0, data_len = 0, reply_out = 0;
    if (number != RTVSD_IOCTL_SENDCMD || v == 0 || in_count == 0 || v[0].len < sizeof(struct rtvsd_request))
        return async ? deliver(st, callback, user_data, IOS_EINVAL) : IOS_EINVAL;
    req = (const struct rtvsd_request*)(uintptr_t)v[0].data;
    if (in_count >= 2u) {
        data = v[1].data;
        data_len = v[1].len;
        if (data == 0 && req->dma_addr != 0) data = cached(req->dma_addr);
    }
    if (out_count >= 1u && v[in_count].len >= 16u) reply_out = v[in_count].data;
    return send_command(st, req, data, data_len, reply_out, async, callback, user_data);
}

int vsd_on_ipc(struct vsd_context* ctx, uint32_t entry, uint32_t* args, int32_t* result);
int vsd_on_ipc(struct vsd_context* ctx, uint32_t entry, uint32_t* args, int32_t* result) {
    struct vsd_state* st = state_of(ctx);
    if (st == 0) return 0;
    if (entry == VSD_OPEN || entry == VSD_OPEN_ASYNC) {
        if (args[0] == 0 || !is_slot0((const char*)(uintptr_t)args[0])) return 0;
        ++st->opens;
        if (entry == VSD_OPEN) {
            *result = VSD_FAKE_FD;
        } else {
            const int32_t r = deliver(st, args[2], args[3], VSD_FAKE_FD);
            *result = r < 0 ? r : RTVSD_OK;
        }
        return 1;
    }
    if ((int32_t)args[0] != VSD_FAKE_FD) return 0;
    if (entry == VSD_CLOSE) {
        *result = RTVSD_OK;
    } else if (entry == VSD_CLOSE_ASYNC) {
        *result = deliver(st, args[1], args[2], RTVSD_OK);
    } else if (entry == VSD_IOCTL || entry == VSD_IOCTL_ASYNC) {
        *result = on_ioctl(st, args, entry == VSD_IOCTL_ASYNC);
    } else {
        *result = on_ioctlv(st, args, entry == VSD_IOCTLV_ASYNC);
    }
    return 1;
}
