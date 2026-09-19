/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Resident runtime, C part. Rules that keep this position-independent and
 * safe to run inside the game's IOS_IoctlAsync and IPC callbacks (possibly
 * from an interrupt handler): no globals (all state lives in the context
 * the trampoline hands us), no string literals or other address-taken
 * data (they would need absolute relocations), no library calls, no
 * allocation, no blocking waits without a bound. The build links the blob
 * at two different addresses and checks the binaries are identical.
 */
#include "rt_hook.h"
#include "rtable.h"

#define RT_DI_READ 0x71u

/* /dev/sdio/slot0 (wiibrew, libogc wiisd.c) */
#define RT_SDIO_SENDCMD 7u
#define RT_SD_CMD_READMULTIBLOCK 0x12u
#define RT_SD_CMDTYPE_AC 3u
#define RT_SD_RESPONSE_R1 1u

int rt_is_di_read(uint32_t ioctl, const uint32_t* in, uint32_t in_len) {
    /* DVDLowRead: ioctl 0x71 with a 0x20-byte command block whose first
     * word repeats the command number in its top byte (wiibrew /dev/di). */
    return ioctl == RT_DI_READ && in_len == 0x20 && in != 0 && (in[0] >> 24) == RT_DI_READ;
}

uint32_t rt_checksum(const uint8_t* bytes, uint32_t length) {
    uint32_t h = 0;
    uint32_t i;
    for (i = 0; i < length; ++i) h = h * 31u + bytes[i];
    return h;
}

static void rt_zero_bytes(uint8_t* dst, uint32_t bytes) {
    while (bytes--) *dst++ = 0;
}

int rt_build_fs_ipc(uint32_t entry_index, const uintptr_t* args, struct rtfs_ipc* out) {
    const uint32_t command = entry_index % RT_IPC_COMMANDS + 1u;
    const int async = entry_index < RT_IPC_COMMANDS;
    if (out == 0) return 0;
    rt_zero_bytes((uint8_t*)out, (uint32_t)sizeof(*out));
    if (args == 0 || entry_index >= RT_IPC_ENTRIES) return 0;
    out->command = command;
    if (command == RTFS_CMD_OPEN) {
        out->args.open.path = (uint32_t)args[0];
        out->args.open.mode = (uint32_t)args[1];
        if (async) { out->callback = (uint32_t)args[2]; out->user_data = (uint32_t)args[3]; }
        return 1;
    }
    out->fd = (int32_t)(uint32_t)args[0];
    if (command == RTFS_CMD_CLOSE) {
        if (async) { out->callback = (uint32_t)args[1]; out->user_data = (uint32_t)args[2]; }
        return 1;
    }
    if (command == RTFS_CMD_READ || command == RTFS_CMD_WRITE) {
        out->args.readwrite.data = (uint32_t)args[1];
        out->args.readwrite.length = (uint32_t)args[2];
        if (async) { out->callback = (uint32_t)args[3]; out->user_data = (uint32_t)args[4]; }
        return 1;
    }
    if (command == RTFS_CMD_SEEK) {
        out->args.seek.where = (int32_t)(uint32_t)args[1];
        out->args.seek.whence = (uint32_t)args[2];
        if (async) { out->callback = (uint32_t)args[3]; out->user_data = (uint32_t)args[4]; }
        return 1;
    }
    if (command == RTFS_CMD_IOCTL) {
        out->args.ioctl.request = (uint32_t)args[1];
        out->args.ioctl.in = (uint32_t)args[2];
        out->args.ioctl.in_len = (uint32_t)args[3];
        out->args.ioctl.out = (uint32_t)args[4];
        out->args.ioctl.out_len = (uint32_t)args[5];
        if (async) { out->callback = (uint32_t)args[6]; out->user_data = (uint32_t)args[7]; }
        return 1;
    }
    out->args.ioctlv.request = (uint32_t)args[1];
    out->args.ioctlv.in_count = (uint32_t)args[2];
    out->args.ioctlv.out_count = (uint32_t)args[3];
    out->args.ioctlv.vectors = (uint32_t)args[4];
    if (async) { out->callback = (uint32_t)args[5]; out->user_data = (uint32_t)args[6]; }
    return 1;
}

/* Synchronous savegame interception (slice 4B2). Translates the SDK call,
 * runs it against the card image when it targets the save folder, and
 * hijacks the caller with the result. Returns 0 to replay the original.
 *
 * Safety notes: every rtfs begin path classifies before mutating, so a
 * PASS_THROUGH replay changes nothing; the engine's busy rule turns a
 * re-entrant arrival into an immediate error, which is hijacked, never
 * replayed; a transfer-needing request replays only when no transfer has
 * run yet (no backend on the console in 4B2), otherwise an engine anomaly
 * hijacks an I/O error rather than risking a half-applied NAND replay. */
static int rt_fs_transfer(struct rt_fs_state* st) {
    struct rtfat_op* op = &st->req.fat;
#ifdef RT_TARGET_PPC
    (void)st;
    (void)op;
    return 0;
#else
    if (rt_host_fs_transfer == 0) return 0;
    op->io_status = rt_host_fs_transfer(op->io_lba, op->io_count, op->io_buffer, op->io_write);
    return 1;
#endif
}

static int rt_on_sync_fs(struct rt_context* ctx, uint32_t entry_index, uintptr_t* args, uint32_t* result) {
    struct rtfs_ipc ipc;
    struct rt_fs_state* st;
    struct rtfs_request* req;
    uint32_t guard;
    if ((ctx->flags & RT_FLAG_FS) == 0) return 0;
    if (!rt_build_fs_ipc(entry_index, args, &ipc)) return 0;
    if (ctx->fs_state == 0) return 0;
    st = (struct rt_fs_state*)(uintptr_t)ctx->fs_state;
    req = &st->req;
    req->fat.bounce = (uint32_t)(uintptr_t)st->bounce;
    req->fat.bounce_bytes = (uint32_t)sizeof(st->bounce);
    rtfs_begin(&st->fs, req, &ipc);
    if (req->classification == RTFS_PASS_THROUGH) return 0;
    if (req->classification != RTFS_NEEDS_IO) {
        *result = (uint32_t)req->result;
        ctx->fs_hijacked++;
        return 1;
    }
    for (guard = 0; guard < ((uint32_t)1 << 20); ++guard) {
        const int step = rtfs_step(&st->fs, req);
        if (step == RTFAT_DONE) break;
        if (step != RTFAT_IO) {
            /* Engine anomaly after possible partial writes: report I/O
             * error rather than replaying half-applied state to NAND. The
             * request stays active, so later calls fail closed with -102. */
            *result = (uint32_t)RTFAT_EIO;
            ctx->fs_hijacked++;
            return 1;
        }
        if (!rt_fs_transfer(st)) {
            /* No backend (console in 4B2): replay. The begin above only
             * classifies and no transfer has run, but it did take the
             * engine's busy flag, which must be released first. */
            st->fs.busy = 0;
            req->active = 0;
            return 0;
        }
    }
    if (guard >= ((uint32_t)1 << 20)) {
        *result = (uint32_t)RTFAT_EIO;
        ctx->fs_hijacked++;
        return 1;
    }
    *result = (uint32_t)req->result;
    ctx->fs_hijacked++;
    return 1;
}

/* --- console-only pieces: EXI/USB Gecko, caches, interrupts, IPC -------- */
#ifdef RT_TARGET_PPC
/* EXI register block (Wii: 0xCD006800), five words per channel:
 * CSR, MAR, LENGTH, CR, DATA. The sequence is the one libogc's EXI_Select /
 * EXI_Imm / EXI_Sync / EXI_Deselect perform for a 2-byte immediate
 * read-write transfer with device 0 at 32 MHz (see NOTICE.md). */
static volatile uint32_t* rt_exi(uint32_t channel, uint32_t reg) {
    return (volatile uint32_t*)(0xCD006800u + channel * 0x14u + reg * 4u);
}

static int rt_gecko_command(uint32_t channel, uint32_t command, uint32_t* reply) {
    volatile uint32_t* csr = rt_exi(channel, 0);
    volatile uint32_t* cr = rt_exi(channel, 3);
    volatile uint32_t* data = rt_exi(channel, 4);
    uint32_t spins = 0;
    *csr = (*csr & 0x405u) | 0x80u | (5u << 4);
    *data = command << 16;
    *cr = (1u << 4) | (2u << 2) | 1u;
    while (*cr & 1u) {
        if (++spins > 200000u) {
            *csr &= 0x405u;
            return 0;
        }
    }
    *reply = *data >> 16;
    *csr &= 0x405u;
    return 1;
}

/* USB Gecko protocol (wiibrew USB Gecko, libogc usbgecko.c): 0xC000 asks
 * whether a byte can be sent, 0xB0xx sends one; bit 0x0400 of the reply
 * means yes / accepted. */
static int rt_gecko_putc(struct rt_context* ctx, uint32_t ch) {
    uint32_t reply = 0;
    uint32_t tries;
    if (!(ctx->flags & RT_FLAG_GECKO)) return 0;
    for (tries = 0; tries < 100u; ++tries) {
        if (!rt_gecko_command(ctx->gecko_channel, 0xC000u, &reply)) break;
        if (reply & 0x0400u) {
            if (!rt_gecko_command(ctx->gecko_channel, 0xB000u | ((ch & 0xFFu) << 4), &reply)) break;
            if (reply & 0x0400u) return 1;
        }
    }
    /* Nobody is taking the bytes (no adapter, or a stalled host): stop
     * spending interrupt-off time on it. */
    if (++ctx->gecko_failures >= RT_GECKO_MAX_FAILURES) ctx->flags &= ~RT_FLAG_GECKO;
    return 0;
}

/* Writes the CPU's copy of [address, address + length) back to RAM and
 * drops it from the cache, so DMA readers (GX, IOS) see what the game
 * will see, and DMA writers (IOS) are not shadowed by stale lines. */
static void rt_flush_range(uintptr_t address, uint32_t length) {
    uintptr_t line = address & ~(uintptr_t)31u;
    const uintptr_t end = address + length;
    while (line < end) {
        __asm__ volatile("dcbf 0, %0" : : "r"(line) : "memory");
        line += 32u;
    }
    __asm__ volatile("sync" : : : "memory");
}

/* External interrupts off/on around the claim of a pending record: the
 * hook runs on game threads and from the IPC interrupt handler (the DVD
 * driver issues its next command from the completion callback), so the
 * claim must not be interleaved. MSR[EE] is 0x8000; mtmsr is what the
 * SDK's OSDisableInterrupts uses too. */
static uint32_t rt_interrupts_off(void) {
    uint32_t msr;
    __asm__ volatile("mfmsr %0" : "=r"(msr));
    __asm__ volatile("mtmsr %0" : : "r"(msr & ~0x8000u) : "memory");
    return msr;
}
static void rt_interrupts_restore(uint32_t msr) {
    __asm__ volatile("mtmsr %0" : : "r"(msr) : "memory");
}

/* The game's IOS_IoctlvAsync (SDK: fd, ioctl, in count, out count,
 * vectors, callback, user data), reached through the context, so the
 * call is a register-indirect one and the blob stays relocatable. */
typedef int32_t (*rt_ioctlv_async_fn)(uint32_t fd, uint32_t ioctl, uint32_t in_count, uint32_t out_count,
                                       struct rt_ioctlv* vec, uint32_t callback, struct rt_pending* record);
static int32_t rt_ioctlv_async(struct rt_context* ctx, struct rt_pending* record) {
    rt_ioctlv_async_fn fn = (rt_ioctlv_async_fn)(uintptr_t)ctx->ioctlv_async;
    if (fn == 0) return -1;
    return fn(ctx->sdio_fd, RT_SDIO_SENDCMD, 2, 1, record->vec, ctx->complete_entry, record);
}

/* The unhooked IOS_IoctlAsync (the replay slot runs the displaced
 * instructions and continues in the original), for DVDLowReads of our
 * own on the game's /dev/di fd. */
typedef int32_t (*rt_ioctl_async_fn)(uint32_t fd, uint32_t ioctl, uint32_t* in, uint32_t in_len, uint32_t out,
                                     uint32_t out_len, uint32_t callback, struct rt_pending* record);
static int32_t rt_di_read_async(struct rt_context* ctx, struct rt_pending* record, uint32_t length) {
    rt_ioctl_async_fn fn = (rt_ioctl_async_fn)(uintptr_t)ctx->di_read_entry;
    if (fn == 0) return -1;
    return fn(ctx->di_fd, RT_DI_READ, record->di_command, 0x20, record->bounce, length, ctx->complete_entry, record);
}
#else
int32_t (*rt_host_ioctlv_async)(uint32_t fd, uint32_t ioctl, uint32_t in_count, uint32_t out_count,
                                struct rt_ioctlv* vec, uint32_t callback, struct rt_pending* record) = 0;
int32_t (*rt_host_ioctl_async)(uint32_t fd, uint32_t ioctl, uint32_t* in, uint32_t in_len, uint32_t out,
                                uint32_t out_len, uint32_t callback, struct rt_pending* record) = 0;
int32_t (*rt_host_fs_transfer)(uint32_t lba, uint32_t count, uint32_t buffer, uint32_t is_write) = 0;
static int32_t rt_di_read_async(struct rt_context* ctx, struct rt_pending* record, uint32_t length) {
    if (ctx->di_read_entry == 0 || rt_host_ioctl_async == 0) return -1;
    return rt_host_ioctl_async(ctx->di_fd, RT_DI_READ, record->di_command, 0x20, record->bounce, length,
                               ctx->complete_entry, record);
}
static int rt_gecko_putc(struct rt_context* ctx, uint32_t ch) {
    (void)ch;
    return (ctx->flags & RT_FLAG_GECKO) != 0;
}
static void rt_flush_range(uintptr_t address, uint32_t length) {
    (void)address;
    (void)length;
}
static uint32_t rt_interrupts_off(void) {
    return 0;
}
static void rt_interrupts_restore(uint32_t msr) {
    (void)msr;
}
static int32_t rt_ioctlv_async(struct rt_context* ctx, struct rt_pending* record) {
    if (ctx->ioctlv_async == 0 || rt_host_ioctlv_async == 0) return -1;
    return rt_host_ioctlv_async(ctx->sdio_fd, RT_SDIO_SENDCMD, 2, 1, record->vec, ctx->complete_entry, record);
}
#endif

static void rt_gecko_hex(struct rt_context* ctx, uint32_t value) {
    int shift;
    for (shift = 28; shift >= 0; shift -= 4) {
        const uint32_t digit = (value >> shift) & 0xFu;
        rt_gecko_putc(ctx, digit < 10u ? '0' + digit : 'a' + digit - 10u);
    }
}

/* Own copies: the blob links against nothing. */
#ifdef RT_TARGET_PPC
/* The compiler lowers some struct copies in the FAT engine to memcpy even
 * with -fno-builtin, so the blob provides it. PPC-only: host builds use
 * libc, where a second definition would collide at the link. */
void* memcpy(void* dst, const void* src, __SIZE_TYPE__ n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
    return dst;
}
#endif
static void rt_copy(uint8_t* dst, const uint8_t* src, uint32_t n) {
    while (n--) *dst++ = *src++;
}
static void rt_zero(uint8_t* dst, uint32_t n) {
    while (n--) *dst++ = 0;
}

/* Splits a read against the table into `runs`. Returns the number of runs,
 * or -1 when the read must pass through untouched (no table, too many
 * runs, error). */
static int rt_split(const struct rt_context* ctx, uint32_t word_offset, uint32_t length, rt_run* runs,
                    int* touched) {
    uint32_t count = 0;
    uint32_t i;
    *touched = 0;
    if (ctx->table == 0) return -1;
    if (rt_lookup((const rt_header*)(uintptr_t)ctx->table, (uint64_t)word_offset << 2, length, runs, RT_MAX_RUNS,
                  &count) != RT_OK) {
        return -1;
    }
    for (i = 0; i < count; ++i) {
        if (runs[i].kind != RT_KIND_PASSTHROUGH) *touched = 1;
    }
    return (int)count;
}

/* Takes a free pending record, or returns 0. */
static struct rt_pending* rt_claim_pending(struct rt_context* ctx) {
    struct rt_pending* rec = 0;
    const uint32_t msr = rt_interrupts_off();
    uint32_t i;
    for (i = 0; i < RT_MAX_PENDING; ++i) {
        if (!ctx->pending[i].in_use) {
            rec = &ctx->pending[i];
            rec->in_use = 1;
            break;
        }
    }
    rt_interrupts_restore(msr);
    return rec;
}

int rt_on_ioctl_async(struct rt_context* ctx, uintptr_t* args, uint32_t* result) {
    const uint32_t fd = (uint32_t)args[0];
    const uint32_t ioctl = (uint32_t)args[1];
    const uint32_t* in = (const uint32_t*)args[2];
    const uint32_t in_len = (uint32_t)args[3];
    (void)result;
    ctx->ioctl_async_calls++;
    if (rt_is_di_read(ioctl, in, in_len)) {
        const uint32_t length = in[1];
        const uint32_t word_offset = in[2];
        const uint32_t lo = ctx->di_read_bytes_lo + length;
        if (lo < ctx->di_read_bytes_lo) ctx->di_read_bytes_hi++;
        ctx->di_read_bytes_lo = lo;
        ctx->di_reads++;
        ctx->di_fd = fd;
        ctx->last_di_word_offset = word_offset;
        ctx->last_di_length = length;
        if (ctx->flags & RT_FLAG_GECKO) {
            /* "R<word offset>:<length>\n", hex, no literals. */
            rt_gecko_putc(ctx, 'R');
            rt_gecko_hex(ctx, word_offset);
            rt_gecko_putc(ctx, ':');
            rt_gecko_hex(ctx, length);
            rt_gecko_putc(ctx, '\n');
        }
        if (ctx->table != 0) {
            const int in_window = ctx->virtual_start_words != 0 && word_offset >= ctx->virtual_start_words;
            struct rt_pending* rec = rt_claim_pending(ctx);
            if (rec == 0) {
                ctx->pending_overflow++;
            } else {
                int touched = 0;
                const int count = rt_split(ctx, word_offset, length, rec->runs, &touched);
                if (count < 0) {
                    ctx->run_overflow++; /* passes through; a virtual read then fails at the drive, honestly */
                    rec->in_use = 0;
                } else if (!touched && !in_window) {
                    rec->in_use = 0; /* nothing of ours in this read */
                } else {
                    rec->run_count = (uint32_t)count;
                    rec->callback = (uint32_t)args[6];
                    rec->user_data = (uint32_t)args[7];
                    rec->out = (uint32_t)args[4];
                    rec->length = length;
                    rec->word_offset = word_offset;
                    rec->is_virtual = (uint32_t)in_window;
                    rec->phase = RT_PHASE_DISC;
                    rec->run_index = 0;
                    rec->run_done = 0;
                    rec->chunk_bytes = 0;
                    rec->chunk_skip = 0;
                    rec->di_result = 0;
                    if (in_window) {
                        /* The drive must never see the virtual offset: fetch the
                         * same length from the partition start instead (always
                         * readable) and replace every byte on completion. The
                         * command block is the game's; flush our change so the
                         * DMA to IOS carries it whatever the SDK flushed before. */
                        uint32_t* command = (uint32_t*)args[2];
                        command[2] = 0;
                        rt_flush_range((uintptr_t)command, 0x20);
                        ctx->virtual_reads++;
                    }
                    args[6] = (uintptr_t)ctx->complete_entry;
                    args[7] = (uintptr_t)rec;
                    ctx->redirected_reads++;
                }
            }
        }
    }
    return 0;
}

int rt_on_ipc(struct rt_context* ctx, uint32_t entry_index, uintptr_t* args, uint32_t* result) {
    if (entry_index == RT_IPC_ASYNC_IOCTL) return rt_on_ioctl_async(ctx, args, result);
    if (entry_index >= RT_IPC_COMMANDS) return rt_on_sync_fs(ctx, entry_index, args, result);
    return 0;
}

/* Where a run of `record` lands in the game's buffer. */
static uint8_t* rt_run_destination(const struct rt_pending* record, const rt_run* run) {
    return (uint8_t*)(uintptr_t)record->out + (uint32_t)(run->vstart - ((uint64_t)record->word_offset << 2));
}

/* Issues the next chunk of the current DISC run: a DVDLowRead of the
 * 32-byte aligned span around the wanted bytes into the bounce buffer. */
static int rt_issue_disc_chunk(struct rt_context* ctx, struct rt_pending* record) {
    const rt_run* run = &record->runs[record->run_index];
    const uint32_t remaining = (uint32_t)run->length - record->run_done;
    const uint32_t first = (uint32_t)run->source + record->run_done; /* partition byte wanted first */
    const uint32_t start = first & ~31u;
    uint32_t end = first + remaining;
    uint32_t length;
    uint32_t i;
    if (remaining == 0) return 0;
    end = (end + 31u) & ~31u;
    if (end - start > RT_BOUNCE_BYTES) end = start + RT_BOUNCE_BYTES;
    length = end - start;
    record->chunk_skip = first - start;
    record->chunk_bytes = length - record->chunk_skip;
    if (record->chunk_bytes > remaining) record->chunk_bytes = remaining;
    for (i = 0; i < 8; ++i) record->di_command[i] = 0;
    record->di_command[0] = RT_DI_READ << 24;
    record->di_command[1] = length;
    record->di_command[2] = start >> 2;
    rt_flush_range((uintptr_t)record->di_command, sizeof(record->di_command));
    rt_flush_range((uintptr_t)record->bounce, length);
    record->phase = RT_PHASE_DISC_RUN;
    ctx->disc_requests++;
    if (rt_di_read_async(ctx, record, length) < 0) {
        ctx->disc_failures++;
        return -1;
    }
    return 1;
}

/* Issues the next SD chunk of the current run. Returns 1 when a request
 * is in flight, 0 when the run is complete or not an SD run, -1 on an IPC
 * refusal. */
static int rt_issue_sd_chunk(struct rt_context* ctx, struct rt_pending* record) {
    const rt_run* run = &record->runs[record->run_index];
    const uint32_t remaining = (uint32_t)run->length - record->run_done;
    const uint32_t byte_in_sectors = run->skip + record->run_done; /* from the start of sector `source` */
    const uint32_t sector = (uint32_t)run->source + byte_in_sectors / RT_SECTOR_BYTES;
    const uint32_t skip = byte_in_sectors % RT_SECTOR_BYTES;
    uint32_t want = skip + remaining;
    uint32_t sectors;
    struct rt_sdio_request* rq = &record->request;
    if (run->kind == RT_KIND_DISC) return rt_issue_disc_chunk(ctx, record);
    if (run->kind != RT_KIND_SD || remaining == 0) return 0;
    if (want > RT_BOUNCE_BYTES) want = RT_BOUNCE_BYTES;
    sectors = (want + RT_SECTOR_BYTES - 1) / RT_SECTOR_BYTES;
    record->chunk_skip = skip;
    record->chunk_bytes = sectors * RT_SECTOR_BYTES - skip;
    if (record->chunk_bytes > remaining) record->chunk_bytes = remaining;
    rq->cmd = RT_SD_CMD_READMULTIBLOCK;
    rq->cmd_type = RT_SD_CMDTYPE_AC;
    rq->rsp_type = RT_SD_RESPONSE_R1;
    rq->arg = ctx->sdio_sdhc ? sector : sector * RT_SECTOR_BYTES;
    rq->blk_cnt = sectors;
    rq->blk_size = RT_SECTOR_BYTES;
    rq->dma_addr = record->bounce;
    rq->isdma = 1;
    rq->pad0 = 0;
    record->vec[0].data = (uint32_t)(uintptr_t)rq;
    record->vec[0].len = sizeof(*rq);
    record->vec[1].data = record->bounce;
    record->vec[1].len = sectors * RT_SECTOR_BYTES;
    record->vec[2].data = (uint32_t)(uintptr_t)record->response;
    record->vec[2].len = sizeof(record->response);
    rt_flush_range((uintptr_t)rq, sizeof(*rq));
    rt_flush_range((uintptr_t)record->vec, sizeof(record->vec));
    rt_flush_range((uintptr_t)record->bounce, sectors * RT_SECTOR_BYTES); /* no stale lines over the DMA target */
    record->phase = RT_PHASE_SD;
    ctx->sd_requests++;
    if (rt_ioctlv_async(ctx, record) < 0) {
        ctx->sd_failures++;
        return -1;
    }
    return 1;
}

/* The runtime's bytes of a completed read, in buffer order, for the
 * diagnostics (MEM, ZERO, SD runs and virtual gaps). */
static uint32_t rt_checksum_runs(const struct rt_pending* record) {
    uint32_t checksum = 0;
    uint32_t i;
    for (i = 0; i < record->run_count; ++i) {
        const rt_run* run = &record->runs[i];
        const uint8_t* bytes = rt_run_destination(record, run);
        uint32_t k;
        if (run->kind == RT_KIND_PASSTHROUGH && !record->is_virtual) continue;
        for (k = 0; k < (uint32_t)run->length; ++k) checksum = checksum * 31u + bytes[k];
    }
    return checksum;
}

void rt_on_di_complete(struct rt_context* ctx, int32_t* result, struct rt_pending* record, uintptr_t* callback,
                       uintptr_t* user_data) {
    ctx->completions++;
    if (record->phase == RT_PHASE_DISC) {
        record->di_result = (uint32_t)*result;
        if (*result == RT_DI_SUCCESS) {
            /* Everything that comes from memory, now; SD runs follow. */
            uint32_t i;
            for (i = 0; i < record->run_count; ++i) {
                const rt_run* run = &record->runs[i];
                uint8_t* dst = rt_run_destination(record, run);
                const uint32_t n = (uint32_t)run->length;
                if (run->kind == RT_KIND_MEM) {
                    rt_copy(dst, (const uint8_t*)(uintptr_t)run->source, n);
                } else if (run->kind == RT_KIND_ZERO || (run->kind == RT_KIND_PASSTHROUGH && record->is_virtual)) {
                    rt_zero(dst, n); /* nothing on the disc belongs in a virtual gap */
                } else {
                    continue; /* PASSTHROUGH: the disc already filled it; SD and DISC: below */
                }
                rt_flush_range((uintptr_t)dst, n);
            }
            record->run_index = 0;
            record->run_done = 0;
        } else {
            record->run_index = record->run_count; /* nothing to fetch for a failed read */
        }
    } else {
        /* An SD or DISC chunk landed (or failed). IPC results are 0 for a
         * successful SD request and RT_DI_SUCCESS for a DVDLowRead. */
        const rt_run* run = &record->runs[record->run_index];
        const int failed = record->phase == RT_PHASE_SD ? *result < 0 : *result != RT_DI_SUCCESS;
        if (failed) {
            if (record->phase == RT_PHASE_SD) {
                ctx->sd_failures++;
                record->di_result = RT_DI_ERROR;
            } else {
                ctx->disc_failures++;
                record->di_result = *result > 0 ? (uint32_t)*result : RT_DI_ERROR; /* the drive's own verdict */
            }
            record->run_index = record->run_count;
        } else {
            uint8_t* dst = rt_run_destination(record, run) + record->run_done;
            rt_copy(dst, (const uint8_t*)(uintptr_t)record->bounce + record->chunk_skip, record->chunk_bytes);
            rt_flush_range((uintptr_t)dst, record->chunk_bytes);
            record->run_done += record->chunk_bytes;
        }
    }

    /* Next SD work, if any. */
    while (record->run_index < record->run_count) {
        const int issued = rt_issue_sd_chunk(ctx, record);
        if (issued > 0) {
            *callback = 0; /* still busy: the IPC dispatcher just returns */
            return;
        }
        if (issued < 0) {
            record->di_result = RT_DI_ERROR;
            break;
        }
        record->run_index++;
        record->run_done = 0;
    }

    /* Done: hand the game its completion. */
    *result = (int32_t)record->di_result;
    *callback = (uintptr_t)record->callback;
    *user_data = (uintptr_t)record->user_data;
    if (record->di_result == RT_DI_SUCCESS && (ctx->flags & RT_FLAG_GECKO)) {
        /* Diagnostics only (they read the buffer back): "M<word
         * offset>:<length>:<checksum of the redirected bytes>\n" */
        ctx->last_checksum = rt_checksum_runs(record);
        rt_gecko_putc(ctx, 'M');
        rt_gecko_hex(ctx, record->word_offset);
        rt_gecko_putc(ctx, ':');
        rt_gecko_hex(ctx, record->length);
        rt_gecko_putc(ctx, ':');
        rt_gecko_hex(ctx, ctx->last_checksum);
        rt_gecko_putc(ctx, '\n');
    }
    record->in_use = 0;
}
