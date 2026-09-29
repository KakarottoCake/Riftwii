/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The game crash blob's C side (fault_hook.h). */
#include "fault_hook.h"

typedef char rt_fault_context_size_check[sizeof(struct rt_fault_context) == RT_FAULT_CONTEXT_BYTES ? 1 : -1];

void fault_on_exception(struct rt_fault_context* c, uint32_t exception, uint32_t* context, uint32_t dsisr,
                        uint32_t dar);

/* IOS IPC (wiibrew "Hardware/IPC"): PPC message, control, ARM message;
 * the Broadway interrupt flags (the IPC bit is 30). */
#define IPC_PPCMSG (*(volatile uint32_t*)0xCD000000u)
#define IPC_PPCCTRL (*(volatile uint32_t*)0xCD000004u)
#define IPC_ARMMSG (*(volatile uint32_t*)0xCD000008u)
#define PPC_IRQ_FLAG (*(volatile uint32_t*)0xCD000030u)
#define CTRL_X1 0x01u   /* a request is waiting for IOS */
#define CTRL_Y2 0x02u   /* IOS took it */
#define CTRL_Y1 0x04u   /* IOS answered */
#define CTRL_X2 0x08u   /* the answer was taken */
#define CTRL_KEEP 0x30u /* the interrupt enables */

#define IOS_OPEN 1u
#define IOS_CLOSE 2u
#define IOS_WRITE 4u
#define IOS_IOCTL 6u

#define FS_CREATE_DIR 3u
#define FS_CREATE_FILE 9u
#define FS_EXISTS (-105)
#define FS_OPEN_WRITE 2u

#define FAULT_STEP_FS 1u
#define FAULT_STEP_DIR 2u
#define FAULT_STEP_FILE 3u
#define FAULT_STEP_OPEN 4u
#define FAULT_STEP_WRITE 5u
#define FAULT_STEP_DONE 6u

#define TIMED_OUT (-0x7FFF)

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

static void invalidate(const void* p, uint32_t len) {
    uintptr_t line = (uintptr_t)p & ~(uintptr_t)31u;
    const uintptr_t end = (uintptr_t)p + len;
    for (; line < end; line += 32u) __asm__ volatile("dcbi 0, %0" : : "r"(line) : "memory");
    __asm__ volatile("sync" : : : "memory");
}

static uint32_t physical(const void* p) { return (uint32_t)(uintptr_t)p & 0x3FFFFFFFu; }

static void control(uint32_t bits) { IPC_PPCCTRL = (IPC_PPCCTRL & CTRL_KEEP) | bits; }

/* Takes whatever IOS has signalled: an acknowledgement, or an answer
 * (handed back at once). Returns the answer's request, or 0. */
static uint32_t take_signals(void) {
    uint32_t answered = 0;
    const uint32_t ctrl = IPC_PPCCTRL;
    if (ctrl & CTRL_Y2) {
        control(CTRL_Y2);
        PPC_IRQ_FLAG = 0x40000000u;
    }
    if (ctrl & CTRL_Y1) {
        answered = IPC_ARMMSG;
        control(CTRL_Y1);
        PPC_IRQ_FLAG = 0x40000000u;
        control(CTRL_X2);
    }
    return answered;
}

/* One request, sent and waited for (a second at most for each wait).
 * Its result, or TIMED_OUT. */
static int32_t call(struct rt_fault_context* c, uint32_t* request) {
    const uint32_t limit = c->ticks_per_second;
    const uint32_t phys = physical(request);
    uint32_t start = now();
    flush(request, 64u);
    /* A request of the game's IOS has not taken yet goes first. */
    while (IPC_PPCCTRL & CTRL_X1) {
        take_signals();
        if (now() - start > limit) return TIMED_OUT;
    }
    IPC_PPCMSG = phys;
    control(CTRL_X1);
    start = now();
    for (;;) {
        if (take_signals() == phys) {
            invalidate(request, 64u);
            return (int32_t)request[1];
        }
        if (now() - start > limit) return TIMED_OUT;
    }
}

static int32_t request(struct rt_fault_context* c, struct rt_fault_state* st, uint32_t command, int32_t fd,
                       uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) {
    uint32_t i;
    for (i = 0; i < 16u; ++i) st->request[i] = 0;
    st->request[0] = command;
    st->request[2] = (uint32_t)fd;
    st->request[3] = a0;
    st->request[4] = a1;
    st->request[5] = a2;
    st->request[6] = a3;
    st->request[7] = a4;
    return call(c, st->request);
}

static int32_t create(struct rt_fault_context* c, struct rt_fault_state* st, int32_t fs, uint32_t ioctl,
                      const char* path) {
    rtshot_fs_attr(st->attr, path);
    flush(st->attr, sizeof(st->attr));
    return request(c, st, IOS_IOCTL, fs, ioctl, physical(st->attr), RTSHOT_FS_ATTR_BYTES, 0, 0);
}

/* The record to /shared2/riftwii/crash.bin: the folder and the file made
 * as the game (any of them may already be there), then written. */
static void save(struct rt_fault_context* c, struct rt_fault_state* st) {
    int32_t fs, fd, r;
    st->fs[0] = '/'; st->fs[1] = 'd'; st->fs[2] = 'e'; st->fs[3] = 'v';
    st->fs[4] = '/'; st->fs[5] = 'f'; st->fs[6] = 's'; st->fs[7] = 0;
    flush(st->fs, sizeof(st->fs));
    c->step = FAULT_STEP_FS;
    fs = request(c, st, IOS_OPEN, 0, physical(st->fs), 0, 0, 0, 0);
    c->result = fs;
    if (fs < 0) return;
    c->step = FAULT_STEP_DIR;
    rtshot_dir(st->path);
    r = create(c, st, fs, FS_CREATE_DIR, st->path);
    c->result = r;
    if (r >= 0 || r == FS_EXISTS) {
        c->step = FAULT_STEP_FILE;
        rtfault_path(st->path);
        r = create(c, st, fs, FS_CREATE_FILE, st->path);
        c->result = r;
    }
    request(c, st, IOS_CLOSE, fs, 0, 0, 0, 0, 0);
    if (r < 0 && r != FS_EXISTS) return;
    c->step = FAULT_STEP_OPEN;
    flush(st->path, sizeof(st->path));
    fd = request(c, st, IOS_OPEN, 0, physical(st->path), FS_OPEN_WRITE, 0, 0, 0);
    c->result = fd;
    if (fd < 0) return;
    c->step = FAULT_STEP_WRITE;
    flush(&st->record, sizeof(st->record));
    r = request(c, st, IOS_WRITE, fd, physical(&st->record), sizeof(st->record), 0, 0, 0);
    c->result = r;
    request(c, st, IOS_CLOSE, fd, 0, 0, 0, 0, 0);
    if (r == (int32_t)sizeof(st->record)) c->step = FAULT_STEP_DONE;
}

void fault_on_exception(struct rt_fault_context* c, uint32_t exception, uint32_t* context, uint32_t dsisr,
                        uint32_t dar) {
    struct rt_fault_state* st = (struct rt_fault_state*)(uintptr_t)c->state;
    uint32_t hi, lo, again, i;
    if (c->done || !rtfault_is_crash(exception, context[RTFAULT_CTX_SRR1])) return;
    c->done = 1;
    do {
        __asm__ volatile("mftbu %0" : "=r"(hi));
        __asm__ volatile("mftb %0" : "=r"(lo));
        __asm__ volatile("mftbu %0" : "=r"(again));
    } while (hi != again);
    rtfault_fill(&st->record, exception, context, dsisr, dar, (const char*)(uintptr_t)0x80000000u, hi, lo,
                 c->ticks_per_second, 1);
    for (i = 0; i < RTFAULT_VERSION_BYTES; ++i) st->record.riftwii[i] = c->version[i];
    save(c, st);
}
