/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The screenshot blob's C side (shot_hook.h). */
#include "shot_hook.h"

uint32_t shot_on_ioctlv(struct rt_shot_context* c, uint32_t* args);
int32_t shot_after_ioctlv(struct rt_shot_context* c, uint32_t slot, int32_t result);
void shot_on_pad(struct rt_shot_context* c, uint8_t* status);
int32_t shot_on_bt(struct rt_shot_context* c, int32_t result, uint32_t tag);
void shot_on_nand(struct rt_shot_context* c, int32_t result);

/* /dev/fs (wiibrew). */
#define FS_CREATE_DIR 3u
#define FS_CREATE_FILE 9u
#define FS_EXISTS (-105)
#define FS_OPEN_WRITE 2u

typedef int32_t (*shot_open_fn)(const char* path, uint32_t mode, uint32_t callback, uint32_t user);
typedef int32_t (*shot_close_fn)(int32_t fd, uint32_t callback, uint32_t user);
typedef int32_t (*shot_write_fn)(int32_t fd, const void* data, uint32_t length, uint32_t callback, uint32_t user);
typedef int32_t (*shot_ioctl_fn)(int32_t fd, uint32_t ioctl, void* in, uint32_t in_len, void* out, uint32_t out_len,
                                 uint32_t callback, uint32_t user);
typedef int32_t (*shot_callback_fn)(int32_t result, uint32_t user);

static uint32_t interrupts_off(void) {
    uint32_t msr;
    __asm__ volatile("mfmsr %0" : "=r"(msr));
    __asm__ volatile("mtmsr %0" : : "r"(msr & ~0x8000u) : "memory");
    return msr;
}

static void interrupts_restore(uint32_t msr) { __asm__ volatile("mtmsr %0" : : "r"(msr) : "memory"); }

static uint32_t now(void) {
    uint32_t tb;
    __asm__ volatile("mftb %0" : "=r"(tb) : : "memory");
    return tb;
}

static void flush(const void* p, uint32_t len) {
    uintptr_t line = (uintptr_t)p & ~(uintptr_t)31u;
    const uintptr_t end = (uintptr_t)p + len;
    for (; line < end; line += 32u) __asm__ volatile("dcbf 0, %0" : : "r"(line) : "memory");
    __asm__ volatile("sync" : : : "memory");
}

/* Cached RAM the game may pass: MEM1 and MEM2. */
static int in_ram(uint32_t address, uint32_t length) {
    const uint32_t end = address + length;
    if (end < address) return 0;
    return (address >= 0x80000000u && end <= 0x81800000u) || (address >= 0x90000000u && end <= 0x94000000u);
}

/* The state, zeroed at the first call: until the game started, its
 * memory was the loader's. */
static struct rt_shot_state* state_of(struct rt_shot_context* c) {
    struct rt_shot_state* st = (struct rt_shot_state*)(uintptr_t)c->state;
    if (!c->inited) {
        const uint32_t msr = interrupts_off();
        if (!c->inited) {
            uint32_t* w = (uint32_t*)(void*)st;
            uint32_t i;
            for (i = 0; i < sizeof(*st) / 4u; ++i) w[i] = 0;
            st->fs_fd = -1;
            st->file_fd = -1;
            c->inited = 1;
        }
        interrupts_restore(msr);
    }
    return st;
}

/* The NAND job ------------------------------------------------------- */

static int32_t send_close(struct rt_shot_context* c, int32_t fd) {
    return ((shot_close_fn)(uintptr_t)c->close_async)(fd, c->complete_nand, 0);
}

static int32_t send_create(struct rt_shot_context* c, struct rt_shot_state* st, uint32_t ioctl) {
    rtshot_fs_attr(st->attr, st->path);
    flush(st->attr, sizeof(st->attr));
    flush(st->path, sizeof(st->path));
    return ((shot_ioctl_fn)(uintptr_t)c->ioctl_async)(st->fs_fd, ioctl, st->attr, RTSHOT_FS_ATTR_BYTES, 0, 0,
                                                      c->complete_nand, 0);
}

#define NOTHING_OPEN 0x7FFFFFFF

/* Closes what is open, one request at a time; NOTHING_OPEN when nothing
 * was left. */
static int32_t send_next_close(struct rt_shot_context* c, struct rt_shot_state* st) {
    int32_t fd;
    if (st->file_fd >= 0) {
        fd = st->file_fd;
        st->file_fd = -1;
        st->phase = RT_SHOT_CLOSE_FILE;
        return send_close(c, fd);
    }
    if (st->fs_fd >= 0) {
        fd = st->fs_fd;
        st->fs_fd = -1;
        st->phase = RT_SHOT_CLOSE_FS;
        return send_close(c, fd);
    }
    return NOTHING_OPEN;
}

static void finish(struct rt_shot_context* c, struct rt_shot_state* st) {
    if (st->error == 0) {
        ++c->shots;
    } else {
        ++c->failures;
        c->last_error = st->error;
    }
    ++c->number;
    st->phase = RT_SHOT_IDLE;
}

/* `result` answers the request `st->phase` names; sends the next. A
 * request IOS refuses at once counts as answered with its error (a
 * refused close as closed). After a failure what is open is closed and
 * the job ends. */
static void job_step(struct rt_shot_context* c, struct rt_shot_state* st, int32_t result) {
    for (;;) {
        int32_t sent;
        switch (st->phase) {
        case RT_SHOT_OPEN_FS:
            if (result < 0) {
                st->error = result;
                sent = NOTHING_OPEN;
                break;
            }
            st->fs_fd = result;
            rtshot_dir(st->path);
            st->phase = RT_SHOT_MKDIR;
            sent = send_create(c, st, FS_CREATE_DIR);
            break;
        case RT_SHOT_MKDIR:
            if (result < 0 && result != FS_EXISTS) {
                st->error = result;
                sent = send_next_close(c, st);
                break;
            }
            st->tries = 0;
            rtshot_path(st->path, c->number);
            st->phase = RT_SHOT_CREATE;
            sent = send_create(c, st, FS_CREATE_FILE);
            break;
        case RT_SHOT_CREATE:
            if (result == FS_EXISTS && ++st->tries < 100u) {
                /* The menu has not taken that one yet. */
                ++c->number;
                rtshot_path(st->path, c->number);
                sent = send_create(c, st, FS_CREATE_FILE);
                break;
            }
            if (result < 0) {
                st->error = result;
                sent = send_next_close(c, st);
                break;
            }
            st->phase = RT_SHOT_OPEN_FILE;
            sent = ((shot_open_fn)(uintptr_t)c->open_async)(st->path, FS_OPEN_WRITE, c->complete_nand, 0);
            break;
        case RT_SHOT_OPEN_FILE:
            if (result < 0) {
                st->error = result;
                sent = send_next_close(c, st);
                break;
            }
            st->file_fd = result;
            st->phase = RT_SHOT_WRITE;
            {
                const uint32_t from = (c->flags & RT_SHOT_FLAG_DIRECT) ? (uint32_t)(uintptr_t)st->header : c->frame;
                sent = ((shot_write_fn)(uintptr_t)c->write_async)(st->file_fd, (const void*)(uintptr_t)from,
                                                                  st->bytes, c->complete_nand, 0);
            }
            break;
        case RT_SHOT_WRITE:
            if (result != (int32_t)st->bytes) st->error = result < 0 ? result : -1;
            if (st->error == 0 && (c->flags & RT_SHOT_FLAG_DIRECT)) {
                st->phase = RT_SHOT_WRITE_PIXELS;
                sent = ((shot_write_fn)(uintptr_t)c->write_async)(st->file_fd, (const void*)(uintptr_t)st->pixels,
                                                                  st->pixel_bytes, c->complete_nand, 0);
                break;
            }
            sent = send_next_close(c, st);
            break;
        case RT_SHOT_WRITE_PIXELS:
            if (result != (int32_t)st->pixel_bytes) st->error = result < 0 ? result : -1;
            sent = send_next_close(c, st);
            break;
        case RT_SHOT_CLOSE_FILE:
        case RT_SHOT_CLOSE_FS:
            sent = send_next_close(c, st);
            break;
        default:
            return;
        }
        if (sent == NOTHING_OPEN) {
            finish(c, st);
            return;
        }
        if (sent >= 0) return;  /* in flight: its answer comes back here */
        result = sent;
    }
}

/* The picture on screen into the frame buffer, then the job starts.
 * Interrupts are off. */
static void capture(struct rt_shot_context* c, struct rt_shot_state* st) {
    struct rtshot_frame f;
    const uint16_t vtr = *(volatile uint16_t*)0xCC002000u;
    const uint16_t picture = *(volatile uint16_t*)0xCC002048u;
    const uint32_t top = ((uint32_t) * (volatile uint16_t*)0xCC00201Cu << 16) | *(volatile uint16_t*)0xCC00201Eu;
    uint32_t y, x;
    uint8_t* out = (uint8_t*)(uintptr_t)c->frame;
    if (st->phase != RT_SHOT_IDLE || c->shots + c->failures >= RT_SHOT_MAX) return;
    if (!rtshot_frame(vtr, picture, top, &f)) {
        ++c->failures;
        c->last_error = -1000;
        return;
    }
    if (c->flags & RT_SHOT_FLAG_DIRECT) {
        /* No copy: the header here, the picture read by IOS from the
         * frame buffer itself. Lines the CPU may hold are written back
         * first (dcbst: the game's view does not change). */
        const uint32_t start = 0x80000000u | f.address;
        st->pixels = start;
        st->pixel_bytes = f.stride * f.lines;
        for (x = start & ~31u; x < start + st->pixel_bytes; x += 32u)
            __asm__ volatile("dcbst 0, %0" : : "r"(x) : "memory");
        __asm__ volatile("sync" : : : "memory");
        rtshot_header(st->header, &f, (const uint8_t*)(uintptr_t)0x80000000u, c->shots + c->failures + 1u, now());
        st->bytes = RTSHOT_HEADER_BYTES;
        flush(st->header, sizeof(st->header));
        goto start_job;
    }
    /* Through the cache, each line flushed first (the CPU may hold an
     * older copy) and dropped after, so the game's view of it does not
     * change. */
    for (y = 0; y < f.lines; ++y) {
        const uint32_t base = 0x80000000u | (f.address + y * f.stride);
        uint32_t* dst = (uint32_t*)(void*)(out + RTSHOT_HEADER_BYTES + y * f.stride);
        for (x = 0; x < f.stride; x += 32u) {
            const uint32_t line = base + x;
            const uint32_t* src = (const uint32_t*)(uintptr_t)line;
            __asm__ volatile("dcbf 0, %0" : : "r"(line) : "memory");
            __asm__ volatile("sync" : : : "memory");
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = src[3];
            dst[4] = src[4];
            dst[5] = src[5];
            dst[6] = src[6];
            dst[7] = src[7];
            __asm__ volatile("dcbi 0, %0" : : "r"(line) : "memory");
            dst += 8;
        }
    }
    rtshot_header(out, &f, (const uint8_t*)(uintptr_t)0x80000000u, c->shots + c->failures + 1u, now());
    st->bytes = RTSHOT_HEADER_BYTES + f.stride * f.lines;
    flush(out, st->bytes);
start_job:
    st->error = 0;
    st->fs_fd = -1;
    st->file_fd = -1;
    /* "/dev/fs", one character at a time (no constant data). */
    st->device[0] = '/'; st->device[1] = 'd'; st->device[2] = 'e'; st->device[3] = 'v';
    st->device[4] = '/'; st->device[5] = 'f'; st->device[6] = 's'; st->device[7] = 0;
    flush(st->device, sizeof(st->device));
    st->phase = RT_SHOT_OPEN_FS;
    {
        const int32_t sent = ((shot_open_fn)(uintptr_t)c->open_async)(st->device, 0, c->complete_nand, 0);
        if (sent < 0) job_step(c, st, sent);
    }
}

/* The hooks ---------------------------------------------------------- */

uint32_t shot_on_ioctlv(struct rt_shot_context* c, uint32_t* a) {
    struct rt_shot_state* st = state_of(c);
    const uint32_t* vec;
    uint32_t i, msr;
    /* a: fd, ioctl, in count, io count, vectors, callback, user data. */
    if (a[1] != RTSHOT_USBV0_BLKMSG || a[2] != 2u || a[3] != 1u || !in_ram(a[4], 24u)) return 0;
    vec = (const uint32_t*)(uintptr_t)a[4];
    if (!in_ram(vec[0], 1u) || vec[1] < 1u || !in_ram(vec[4], vec[5]) || vec[5] < 12u) return 0;
    if (*(const uint8_t*)(uintptr_t)vec[0] != RTSHOT_ACL_IN) return 0;
    msr = interrupts_off();
    for (i = 0; i < RT_SHOT_WATCHES; ++i) {
        struct rt_shot_watch* w = &st->watch[i];
        if (w->in_use) continue;
        w->in_use = 1;
        w->callback = a[5];
        w->user = a[6];
        w->data = vec[4];
        w->length = vec[5];
        a[5] = c->complete_bt;
        a[6] = (uint32_t)(uintptr_t)w;
        interrupts_restore(msr);
        return i + 1u;
    }
    interrupts_restore(msr);
    return 0;  /* all busy: this read goes by unseen */
}

int32_t shot_after_ioctlv(struct rt_shot_context* c, uint32_t slot, int32_t result) {
    if (slot != 0 && result < 0) {
        /* IOS refused it: no callback will come. */
        state_of(c)->watch[slot - 1u].in_use = 0;
    }
    return result;
}

void shot_on_pad(struct rt_shot_context* c, uint8_t* status) {
    struct rt_shot_state* st = state_of(c);
    const uint32_t msr = interrupts_off();
    if (rtshot_pads(&st->input, status)) capture(c, st);
    interrupts_restore(msr);
}

int32_t shot_on_bt(struct rt_shot_context* c, int32_t result, uint32_t tag) {
    struct rt_shot_state* st = state_of(c);
    const uint32_t first = (uint32_t)(uintptr_t)&st->watch[0];
    struct rt_shot_watch* w;
    uint32_t callback, user;
    if (tag < first || tag >= first + sizeof(st->watch) || (tag - first) % sizeof(struct rt_shot_watch) != 0)
        return 0;  /* not ours: cannot happen */
    w = (struct rt_shot_watch*)(uintptr_t)tag;
    callback = w->callback;
    user = w->user;
    if (result > 0) {
        const uint32_t size = (uint32_t)result < w->length ? (uint32_t)result : w->length;
        const uint32_t reports = st->input.reports;
        if (rtshot_acl(&st->input, (uint8_t*)(uintptr_t)w->data, size)) capture(c, st);
        if ((c->flags & RT_SHOT_FLAG_DEMO) && st->input.reports != reports) {
            const uint32_t t = now();
            if (c->demo_start == 0) c->demo_start = t | 1u;
            else if (c->shots + c->failures < 2u &&
                     t - c->demo_start >= (c->shots + c->failures + 1u) * 20u * c->ticks_per_second)
                capture(c, st);
        }
    }
    w->in_use = 0;
    if (callback == 0) return 0;
    return ((shot_callback_fn)(uintptr_t)callback)(result, user);
}

void shot_on_nand(struct rt_shot_context* c, int32_t result) { job_step(c, state_of(c), result); }
