/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Wii Remote log blob's C side (wlog_hook.h). */
#include "wlog_hook.h"

#include "rtshot.h"

void wlog_on_print(struct rt_wlog_context* c, const uint32_t* words);
void wlog_on_exit(struct rt_wlog_context* c);
uint32_t wlog_on_ioctlv(struct rt_wlog_context* c, uint32_t* args);
int32_t wlog_after_ioctlv(struct rt_wlog_context* c, uint32_t slot, int32_t result);
int32_t wlog_on_bt(struct rt_wlog_context* c, int32_t result, uint32_t tag);

/* /dev/fs's requests (as in runtime/fault) and an open file's mode. */
#define FS_CREATE_DIR 3u
#define FS_CREATE_FILE 9u
#define FS_EXISTS (-105)
#define FS_OPEN_WRITE 2u

typedef int32_t (*ios_open_fn)(const char* path, uint32_t mode);
typedef int32_t (*ios_close_fn)(int32_t fd);
typedef int32_t (*ios_write_fn)(int32_t fd, const void* buffer, uint32_t length);
typedef int32_t (*ios_ioctl_fn)(int32_t fd, uint32_t ioctl, void* in, uint32_t in_length, void* out,
                                uint32_t out_length);
typedef int32_t (*ipc_callback_fn)(int32_t result, uint32_t user);

/* The Bluetooth dongle, /dev/usb/oh1/57e/305 (wiibrew): USBV0 control,
 * bulk and interrupt messages; HCI commands go to the control endpoint as
 * a class request to the interface, events come from 0x81, ACL data goes
 * out on 0x02 and comes in on 0x82. */
#define USBV0_CTRLMSG 0u
#define USBV0_BLKMSG 1u
#define USBV0_INTRMSG 2u
#define EP_EVENTS 0x81u
#define EP_ACL_IN 0x82u
#define EP_ACL_OUT 0x02u
#define HCI_COMMAND_REQUEST 0x20u
#define EVENT_COMPLETED_PACKETS 0x13u

/* The prints come from game threads and from the Bluetooth stack's
 * callbacks alike: external interrupts are off while one is kept. */
static uint32_t interrupts_off(void) {
    uint32_t msr;
    __asm__ volatile("mfmsr %0" : "=r"(msr));
    __asm__ volatile("mtmsr %0" : : "r"(msr & ~0x8000u) : "memory");
    return msr;
}

static void interrupts_restore(uint32_t msr) { __asm__ volatile("mtmsr %0" : : "r"(msr) : "memory"); }

/* The time base in units of 256 ticks: about 4 us, five hours before it
 * wraps (the low word alone wraps every 70 s). */
static uint32_t now(void) {
    uint32_t hi, lo, again;
    do {
        __asm__ volatile("mftbu %0" : "=r"(hi));
        __asm__ volatile("mftb %0" : "=r"(lo));
        __asm__ volatile("mftbu %0" : "=r"(again));
    } while (hi != again);
    return (hi << 24) | (lo >> 8);
}

/* Whether a pointer argument can be read: MEM1 or MEM2, cached. */
static int readable(uint32_t address) {
    return (address >= 0x80000000u && address < 0x81800000u) || (address >= 0x90000000u && address < 0x94000000u);
}

/* Copies at most `limit - 1` characters and always ends the copy. */
static void copy_text(char* out, const char* in, uint32_t limit) {
    uint32_t i = 0;
    while (i + 1 < limit && in[i] != 0) {
        out[i] = in[i];
        ++i;
    }
    while (i < limit) out[i++] = 0;
}

static int same_numbers(const struct rtwlog_entry* e, const uint32_t* args) {
    uint32_t i;
    for (i = 0; i < RTWLOG_ARGS; ++i) {
        if (e->args[i] != args[i]) return 0;
    }
    return 1;
}

/* The %s arguments, copied now: the strings are the game's and are gone
 * by the time RiftWii reads the log. The format is walked as printf
 * would: each conversion takes the next integer register, except the
 * floating-point ones (%f, %lf, %e, %g), which come in FPRs. */
static void copy_strings(struct rtwlog_entry* e) {
    uint32_t i = 0, gpr = 0, kept = 0;
    while (i < RTWLOG_FMT_BYTES && e->text[i] != 0) {
        char conversion;
        if (e->text[i++] != '%') continue;
        /* Flags, width, precision and length: none of them a conversion. */
        while (i < RTWLOG_FMT_BYTES && e->text[i] != 0) {
            const char ch = e->text[i];
            const int part = ch == '-' || ch == '+' || ch == ' ' || ch == '#' || ch == '.' || ch == 'l' ||
                             ch == 'h' || ch == 'L' || ch == 'z' || (ch >= '0' && ch <= '9');
            if (!part) break;
            ++i;
        }
        if (i >= RTWLOG_FMT_BYTES || e->text[i] == 0) break;
        conversion = e->text[i++];
        if (conversion == '%') continue;
        if (conversion == 'f' || conversion == 'F' || conversion == 'e' || conversion == 'E' || conversion == 'g' ||
            conversion == 'G' || conversion == 'a') {
            continue;
        }
        if (gpr >= RTWLOG_ARGS) break;
        if (conversion == 's' && kept < RTWLOG_STRINGS && readable(e->args[gpr])) {
            copy_text(e->strings[kept], (const char*)(uintptr_t)e->args[gpr], RTWLOG_STRING_BYTES);
            ++kept;
        }
        ++gpr;
    }
}

/* The log's header, the first time the blob runs: until the jump to the
 * game this MEM2 was the loader's, so nothing could be staged there. */
static void start_log(struct rt_wlog_context* c, struct rtwlog_file* f) {
    uint32_t* word = (uint32_t*)f;
    uint32_t i;
    if (f->magic == RTWLOG_MAGIC) return;
    for (i = 0; i < sizeof(*f) / 4u; ++i) word[i] = 0;
    f->magic = RTWLOG_MAGIC;
    f->version = RTWLOG_VERSION;
    for (i = 0; i < sizeof(f->game); ++i) f->game[i] = ((const char*)0x80000000u)[i];
    f->ticks_per_second = c->ticks_per_second;
    f->stubs = c->stubs;
    f->probe = c->probe;
    f->probe_at = c->probe_at;
}

void wlog_on_print(struct rt_wlog_context* c, const uint32_t* words) {
    struct rt_wlog_state* st = (struct rt_wlog_state*)(uintptr_t)c->state;
    struct rtwlog_file* f = &st->file;
    const uint32_t format = words[0];
    const uint32_t* args = words + 1;
    struct rtwlog_entry* e;
    uint32_t msr, i;
    if (!readable(format)) return;
    msr = interrupts_off();
    start_log(c, f);
    if (f->total > 0) {
        /* The same message as the last one: counted, not kept again. */
        struct rtwlog_entry* last = &f->entries[rtwlog_slot(f->total - 1u)];
        if (last->format == format && same_numbers(last, args)) {
            last->repeat++;
            interrupts_restore(msr);
            return;
        }
    }
    e = &f->entries[rtwlog_slot(f->total)];
    e->tick = now();
    e->repeat = 0;
    e->format = format;
    for (i = 0; i < RTWLOG_ARGS; ++i) e->args[i] = args[i];
    copy_text(e->text, (const char*)(uintptr_t)format, RTWLOG_FMT_BYTES);
    for (i = 0; i < RTWLOG_STRINGS; ++i) e->strings[i][0] = 0;
    copy_strings(e);
    f->total++;
    interrupts_restore(msr);
}

/* The Bluetooth side ------------------------------------------------- */

/* The log started and the translator's state set up, at the first use:
 * until the jump this MEM2 was the loader's. Interrupts are off. */
static struct rt_wlog_state* bt_state(struct rt_wlog_context* c) {
    struct rt_wlog_state* st = (struct rt_wlog_state*)(uintptr_t)c->state;
    start_log(c, &st->file);
    if (!c->inited) {
        uint32_t i;
        rtpro_init(&st->pro);
        if (c->tables) rtpro_set_tables(&st->pro, (const uint8_t*)(uintptr_t)c->tables);
        for (i = 0; i < RT_WLOG_WATCHES; ++i) st->watch[i].in_use = 0;
        st->bt_fd = -1;
        st->data_in = 0;
        st->data_out = 0;
        c->inited = 1;
    }
    st->file.pro.on = (c->flags & RT_WLOG_FLAG_TRANSLATE) ? 1u : 0u;
    st->file.pro.hooked = c->hooked;
    st->file.pro.tables = c->tables;
    return st;
}

static uint32_t le16(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }

static void trace(struct rt_wlog_state* st, uint32_t kind, uint32_t flags, uint32_t handle, uint32_t cid,
                  const uint8_t* data, uint32_t size) {
    struct rtwlog_file* f = &st->file;
    struct rtwlog_bt* e = &f->bt[rtwlog_bt_slot(f->pro.bt_total)];
    uint32_t i;
    e->tick = now();
    e->kind = (uint8_t)kind;
    e->flags = (uint8_t)flags;
    e->handle = (uint16_t)handle;
    e->size = (uint16_t)size;
    e->cid = (uint16_t)cid;
    for (i = 0; i < RTWLOG_BT_BYTES; ++i) e->data[i] = i < size ? data[i] : 0;
    f->pro.bt_total++;
}

/* An ACL packet into the trace, from its L2CAP payload. The steady stream
 * (data reports in, speaker data out) is kept only at first, then
 * counted. Returns whether it was kept. */
static int trace_acl(struct rt_wlog_state* st, uint32_t kind, uint32_t flags, const uint8_t* acl, uint32_t size) {
    uint32_t payload;
    const uint8_t* p = acl + 8;
    if (size < 8u) {
        trace(st, kind, flags, 0, 0, acl, size);
        return 1;
    }
    payload = le16(acl + 4);
    if (8u + payload > size) payload = size - 8u;
    if (payload >= 2u) {
        const int data_in = kind != RTWLOG_BT_OUT && p[0] == 0xA1u && p[1] >= 0x30u && p[1] <= 0x3Fu;
        const int speaker_out = kind == RTWLOG_BT_OUT && p[0] == 0xA2u && p[1] == 0x18u;
        if (data_in || speaker_out) {
            uint32_t* kept = data_in ? &st->data_in : &st->data_out;
            /* The rebuilt copy of a report follows its original: kept when
             * that was (it counted already), dropped silently when not. */
            if (kind == RTWLOG_BT_GAME) {
                if (*kept > RTWLOG_BT_DATA_KEPT) return 0;
            } else if (*kept >= RTWLOG_BT_DATA_KEPT) {
                st->file.pro.data_skipped++;
                *kept = RTWLOG_BT_DATA_KEPT + 1u;
                return 0;
            } else {
                ++*kept;
            }
        }
    }
    trace(st, kind, flags, le16(acl) & 0x0FFFu, le16(acl + 6), p, payload);
    return 1;
}

static void copy_counts(struct rt_wlog_state* st) {
    struct rtwlog_pro* f = &st->file.pro;
    uint32_t i;
    f->pros = st->pro.pros;
    f->translated = st->pro.translated;
    f->ids = st->pro.ids;
    f->calibrations = st->pro.calibrations;
    f->acks = st->pro.acks;
    f->modes = st->pro.modes;
    f->keys = st->pro.keys;
    f->bad_keys = st->pro.bad_keys;
    f->memory_reads = st->pro.memory_reads;
    f->replaced = st->pro.replaced;
    for (i = 0; i < RTPRO_LINKS; ++i) {
        const struct rtpro_link* l = &st->pro.links[i];
        if (l->used && l->pro && l->encrypted) f->encrypted = 1;
        if (l->used && l->pro && l->pro_encrypts == RTPRO_SENDS_ENCRYPTED) f->pro_encrypted = 1;
    }
}

/* A bulk-out transfer made shorter: its length, given twice (the
 * endpoint's 16-bit length in, and the data vector's size). */
static void set_out_size(uint32_t* vec, uint32_t size) {
    if (vec[3] == 2u && readable(vec[2])) *(uint16_t*)(uintptr_t)vec[2] = (uint16_t)size;
    vec[5] = size;
}

uint32_t wlog_on_ioctlv(struct rt_wlog_context* c, uint32_t* a) {
    struct rt_wlog_state* st;
    const uint32_t* vec;
    uint32_t msr, i, endpoint;
    /* a: fd, ioctl, in count, io count, vectors, callback, user data. */
    if (a[1] > USBV0_INTRMSG || !readable(a[4])) return 0;
    vec = (const uint32_t*)(uintptr_t)a[4];
    msr = interrupts_off();
    st = bt_state(c);
    if (a[1] == USBV0_CTRLMSG) {
        /* Six inputs (the setup packet's fields), the data last. */
        if (a[2] == 6u && a[3] == 1u && readable(vec[0]) && readable(vec[12]) && vec[13] >= 3u &&
            *(const uint8_t*)(uintptr_t)vec[0] == HCI_COMMAND_REQUEST) {
            st->bt_fd = (int32_t)a[0];
            trace(st, RTWLOG_BT_COMMAND, 0, 0, 0, (const uint8_t*)(uintptr_t)vec[12], vec[13]);
        }
        interrupts_restore(msr);
        return 0;
    }
    /* Bulk and interrupt: the endpoint and the length in, the data. */
    if (a[2] != 2u || a[3] != 1u || !readable(vec[0]) || !readable(vec[4]) || vec[5] < 4u ||
        (st->bt_fd >= 0 && (int32_t)a[0] != st->bt_fd)) {
        interrupts_restore(msr);
        return 0;
    }
    endpoint = *(const uint8_t*)(uintptr_t)vec[0];
    if (endpoint == EP_ACL_OUT && a[1] == USBV0_BLKMSG) {
        uint8_t* acl = (uint8_t*)(uintptr_t)vec[4];
        uint32_t what = 0, size = vec[5];
        /* The game's packet as it was, for the trace if the translator
         * changes it. */
        uint8_t asked[8u + 32u];  /* a whole report: 23 bytes at most */
        const uint32_t asked_size = size < sizeof(asked) ? size : sizeof(asked);
        for (i = 0; i < asked_size; ++i) asked[i] = acl[i];
        /* Changed before IOS_IoctlvAsync flushes the buffers for IOS. */
        if (c->flags & RT_WLOG_FLAG_TRANSLATE) what = rtpro_acl_out(&st->pro, acl, &size);
        if (size != vec[5]) set_out_size((uint32_t*)(uintptr_t)a[4], size);
        if (what & RTPRO_CHANGED) {
            if (trace_acl(st, RTWLOG_BT_OUT, what, asked, asked_size)) trace_acl(st, RTWLOG_BT_SENT, 0, acl, size);
        } else {
            trace_acl(st, RTWLOG_BT_OUT, what, acl, size);
        }
        copy_counts(st);
        interrupts_restore(msr);
        return 0;
    }
    if (!((endpoint == EP_EVENTS && a[1] == USBV0_INTRMSG) || (endpoint == EP_ACL_IN && a[1] == USBV0_BLKMSG))) {
        interrupts_restore(msr);
        return 0;
    }
    for (i = 0; i < RT_WLOG_WATCHES; ++i) {
        struct rt_wlog_watch* w = &st->watch[i];
        if (w->in_use) continue;
        w->in_use = 1;
        w->callback = a[5];
        w->user = a[6];
        w->data = vec[4];
        w->length = vec[5];
        w->endpoint = endpoint;
        a[5] = c->complete_bt;
        a[6] = (uint32_t)(uintptr_t)w;
        interrupts_restore(msr);
        return i + 1u;
    }
    interrupts_restore(msr);
    return 0;  /* all busy: this read goes by unseen */
}

int32_t wlog_after_ioctlv(struct rt_wlog_context* c, uint32_t slot, int32_t result) {
    if (slot != 0 && result < 0) {
        /* IOS refused it: no callback will come. */
        ((struct rt_wlog_state*)(uintptr_t)c->state)->watch[slot - 1u].in_use = 0;
    }
    return result;
}

int32_t wlog_on_bt(struct rt_wlog_context* c, int32_t result, uint32_t tag) {
    struct rt_wlog_state* st = (struct rt_wlog_state*)(uintptr_t)c->state;
    const uint32_t first = (uint32_t)(uintptr_t)&st->watch[0];
    struct rt_wlog_watch* w;
    uint32_t callback, user;
    if (tag < first || tag >= first + sizeof(st->watch) || (tag - first) % sizeof(struct rt_wlog_watch) != 0)
        return 0;  /* not ours: cannot happen */
    w = (struct rt_wlog_watch*)(uintptr_t)tag;
    callback = w->callback;
    user = w->user;
    if (result > 0) {
        const uint32_t msr = interrupts_off();
        uint8_t* data = (uint8_t*)(uintptr_t)w->data;
        const uint32_t size = (uint32_t)result < w->length ? (uint32_t)result : w->length;
        bt_state(c);
        if (w->endpoint == EP_EVENTS) {
            if (size >= 2u && data[0] == EVENT_COMPLETED_PACKETS) st->file.pro.events_skipped++;
            else trace(st, RTWLOG_BT_EVENT, 0, 0, 0, data, size);
            rtpro_event(&st->pro, c->pro_addresses, c->pro_count, data, size);
        } else {
            trace_acl(st, RTWLOG_BT_IN, 0, data, size);
            if (c->flags & RT_WLOG_FLAG_TRANSLATE) {
                uint32_t what = 0;
                const uint32_t rebuilt = rtpro_acl_in(&st->pro, data, size, w->length, &what);
                if (what & RTPRO_CHANGED) {
                    trace_acl(st, RTWLOG_BT_GAME, what, data, rebuilt);
                    result = (int32_t)rebuilt;
                }
            }
        }
        copy_counts(st);
        interrupts_restore(msr);
    }
    w->in_use = 0;
    if (callback == 0) return 0;
    return ((ipc_callback_fn)(uintptr_t)callback)(result, user);
}

/* /shared2/riftwii/wpadlog.bin, written as the game: the folder and the
 * file made (either may be there already), then the whole log. Through
 * the game's own synchronous IOS functions: __OSLaunchMenu makes its
 * own ES calls the same way right after this. */
void wlog_on_exit(struct rt_wlog_context* c) {
    struct rt_wlog_state* st = (struct rt_wlog_state*)(uintptr_t)c->state;
    const ios_open_fn ios_open = (ios_open_fn)(uintptr_t)c->ios_open;
    const ios_close_fn ios_close = (ios_close_fn)(uintptr_t)c->ios_close;
    const ios_write_fn ios_write = (ios_write_fn)(uintptr_t)c->ios_write;
    const ios_ioctl_fn ios_ioctl = (ios_ioctl_fn)(uintptr_t)c->ios_ioctl;
    int32_t fs, fd, r;
    char* p;
    c->saved = 1;
    start_log(c, &st->file);  /* nothing printed: an empty log says so */
    st->file.saves++;
    st->fs[0] = '/'; st->fs[1] = 'd'; st->fs[2] = 'e'; st->fs[3] = 'v';
    st->fs[4] = '/'; st->fs[5] = 'f'; st->fs[6] = 's'; st->fs[7] = 0;
    fs = ios_open(st->fs, 0);
    c->result = fs;
    if (fs < 0) return;
    rtshot_dir(st->path);
    rtshot_fs_attr(st->attr, st->path);
    r = ios_ioctl(fs, FS_CREATE_DIR, st->attr, RTSHOT_FS_ATTR_BYTES, 0, 0);
    /* "/shared2/riftwii" + "/wpadlog.bin" */
    for (p = st->path; *p; ++p) {
    }
    *p++ = '/'; *p++ = 'w'; *p++ = 'p'; *p++ = 'a'; *p++ = 'd'; *p++ = 'l'; *p++ = 'o'; *p++ = 'g';
    *p++ = '.'; *p++ = 'b'; *p++ = 'i'; *p++ = 'n'; *p = 0;
    if (r >= 0 || r == FS_EXISTS) {
        rtshot_fs_attr(st->attr, st->path);
        r = ios_ioctl(fs, FS_CREATE_FILE, st->attr, RTSHOT_FS_ATTR_BYTES, 0, 0);
    }
    ios_close(fs);
    c->result = r;
    if (r < 0 && r != FS_EXISTS) return;
    fd = ios_open(st->path, FS_OPEN_WRITE);
    c->result = fd;
    if (fd < 0) return;
    c->result = ios_write(fd, &st->file, sizeof(st->file));
    ios_close(fd);
}
