/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Wii Remote log blob's C side (wlog_hook.h). */
#include "wlog_hook.h"

#include "rtshot.h"

void wlog_on_print(struct rt_wlog_context* c, const uint32_t* words);
void wlog_on_exit(struct rt_wlog_context* c);

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
