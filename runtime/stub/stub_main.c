/* SPDX-FileCopyrightText: 2026 RiftWii contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The return stub (include/riftwii/returnstub.h): run by libogc's exit()
 * after the app has shut itself down. It talks to IOS directly through
 * Hollywood's IPC registers, by polling, since nothing of the app's own
 * IOS code may be left: /dev/es is opened, and each title the loader
 * listed is launched in turn until IOS takes one. A launch IOS takes
 * never returns (IOS reloads and resets the PPC); a refusal does, and the
 * next title is tried. When none starts, the console restarts to the Wii
 * Menu, as libogc's own exit would have.
 */
#include <stdint.h>

#include "riftwii/returnstub.h"

/* Hollywood's IPC registers, uncached (wiibrew "Hardware/IPC"). */
#define RTSTUB_IPC_PPCMSG 0xCD000000u
#define RTSTUB_IPC_PPCCTRL 0xCD000004u
#define RTSTUB_IPC_ARMMSG 0xCD000008u
#define RTSTUB_IPC_X1 0x01u  /* a request is waiting for IOS */
#define RTSTUB_IPC_Y2 0x02u  /* IOS took it (write 1 to clear) */
#define RTSTUB_IPC_Y1 0x04u  /* IOS replied (write 1 to clear) */
#define RTSTUB_IPC_X2 0x08u  /* the reply is read: IOS may send the next */

#define RTSTUB_IOS_OPEN 1u
#define RTSTUB_IOS_IOCTL 6u
#define RTSTUB_IOS_IOCTLV 7u
#define RTSTUB_ES_LAUNCHTITLE 8u
#define RTSTUB_STM_HOTRESET 0x2001u

/* Several seconds of polling: IOS answers an open or a refusal in far
 * less. A launch IOS takes has no reply to wait for. */
#define RTSTUB_IPC_POLLS 0x04000000u
#define RTSTUB_NO_REPLY (-1000)

struct rtstub_request {
    uint32_t command;
    int32_t result;
    uint32_t fd;
    uint32_t args[5];
};

/* What IOS reads by DMA: 32-byte aligned, and flushed before each send. */
static struct rtstub_request rtstub_request_buf __attribute__((aligned(32)));
static uint32_t rtstub_vectors[4] __attribute__((aligned(32)));
static uint32_t rtstub_stm_buffers[16] __attribute__((aligned(32)));
static const char rtstub_es_path[] __attribute__((aligned(32))) = "/dev/es";
static const char rtstub_stm_path[] __attribute__((aligned(32))) = "/dev/stm/immediate";

extern uint32_t rtstub_count;
extern int32_t rtstub_result;
extern uint8_t rtstub_titles[];

static uint32_t rtstub_read32(uint32_t address) { return *(volatile uint32_t*)(uintptr_t)address; }
static void rtstub_write32(uint32_t address, uint32_t value) { *(volatile uint32_t*)(uintptr_t)address = value; }

static uint32_t rtstub_physical(const volatile void* p) { return (uint32_t)(uintptr_t)p & 0x3FFFFFFFu; }

static void rtstub_flush(const volatile void* p, uint32_t length) {
    uint32_t a = (uint32_t)(uintptr_t)p & ~31u;
    const uint32_t end = (uint32_t)(uintptr_t)p + length;
    for (; a < end; a += 32) __asm__ volatile("dcbf 0,%0" : : "r"(a) : "memory");
    __asm__ volatile("sync" : : : "memory");
}

static void rtstub_invalidate(const volatile void* p, uint32_t length) {
    uint32_t a = (uint32_t)(uintptr_t)p & ~31u;
    const uint32_t end = (uint32_t)(uintptr_t)p + length;
    for (; a < end; a += 32) __asm__ volatile("dcbi 0,%0" : : "r"(a) : "memory");
    __asm__ volatile("sync" : : : "memory");
}

/* A reply IOS has waiting (the address of the request it answers), or 0.
 * Taking it lets IOS send the next. */
static uint32_t rtstub_take_reply(void) {
    uint32_t reply;
    if ((rtstub_read32(RTSTUB_IPC_PPCCTRL) & RTSTUB_IPC_Y1) == 0) return 0;
    reply = rtstub_read32(RTSTUB_IPC_ARMMSG);
    rtstub_write32(RTSTUB_IPC_PPCCTRL, RTSTUB_IPC_Y1);
    rtstub_write32(RTSTUB_IPC_PPCCTRL, RTSTUB_IPC_X2);
    return reply;
}

/* Sends the request and waits for its own reply. Replies to the app's
 * requests that were still in flight when it exited are taken and
 * dropped: IOS takes no new request while one waits to be read. Returns
 * IOS's result, or RTSTUB_NO_REPLY when IOS does not answer in time
 * (`wait_forever` for a launch, whose only reply is a refusal). */
static int32_t rtstub_call(int wait_forever) {
    struct rtstub_request* rq = &rtstub_request_buf;
    const uint32_t address = rtstub_physical(rq);
    uint32_t polls;
    rtstub_flush(rq, sizeof(*rq));
    rtstub_write32(RTSTUB_IPC_PPCMSG, address);
    rtstub_write32(RTSTUB_IPC_PPCCTRL, RTSTUB_IPC_X1);
    for (polls = 0; (rtstub_read32(RTSTUB_IPC_PPCCTRL) & RTSTUB_IPC_Y2) == 0; ++polls) {
        rtstub_take_reply();
        if (polls == RTSTUB_IPC_POLLS) return RTSTUB_NO_REPLY;
    }
    rtstub_write32(RTSTUB_IPC_PPCCTRL, RTSTUB_IPC_Y2);
    for (polls = 0;; ++polls) {
        const uint32_t reply = rtstub_take_reply();
        if (reply == address) break;
        if (polls == RTSTUB_IPC_POLLS && !wait_forever) return RTSTUB_NO_REPLY;
    }
    rtstub_invalidate(rq, sizeof(*rq));
    return rq->result;
}

static void rtstub_clear_request(uint32_t command, uint32_t fd) {
    struct rtstub_request* rq = &rtstub_request_buf;
    uint32_t i;
    rq->command = command;
    rq->result = 0;
    rq->fd = fd;
    for (i = 0; i < 5; ++i) rq->args[i] = 0;
}

static int32_t rtstub_open(const char* path) {
    rtstub_clear_request(RTSTUB_IOS_OPEN, 0);
    rtstub_request_buf.args[0] = rtstub_physical(path);
    rtstub_request_buf.args[1] = 0;  /* no access mode: as libogc opens both devices */
    rtstub_flush(path, 32);
    return rtstub_call(0);
}

/* ES_LaunchTitle: the title ID and its ticket view, as the loader left
 * them in the title's slot. Returns only when IOS refused. */
static int32_t rtstub_launch(uint32_t es, const uint8_t* slot) {
    const uint8_t* title = slot;
    const uint8_t* view = slot + RTSTUB_TITLE_VIEW;
    rtstub_vectors[0] = rtstub_physical(title);
    rtstub_vectors[1] = 8;
    rtstub_vectors[2] = rtstub_physical(view);
    rtstub_vectors[3] = RTSTUB_VIEW_BYTES;
    rtstub_flush(slot, RTSTUB_TITLE_BYTES);
    rtstub_flush(rtstub_vectors, sizeof(rtstub_vectors));
    rtstub_clear_request(RTSTUB_IOS_IOCTLV, es);
    rtstub_request_buf.args[0] = RTSTUB_ES_LAUNCHTITLE;
    rtstub_request_buf.args[1] = 2;  /* in: the title ID and the view */
    rtstub_request_buf.args[2] = 0;
    rtstub_request_buf.args[3] = rtstub_physical(rtstub_vectors);
    return rtstub_call(1);
}

/* The Wii Menu by a restart, as libogc's exit does without a stub. */
static void rtstub_restart_to_menu(void) {
    const int32_t stm = rtstub_open(rtstub_stm_path);
    uint32_t i;
    if (stm < 0) return;
    for (i = 0; i < 16; ++i) rtstub_stm_buffers[i] = 0;
    rtstub_flush(rtstub_stm_buffers, sizeof(rtstub_stm_buffers));
    rtstub_clear_request(RTSTUB_IOS_IOCTL, (uint32_t)stm);
    rtstub_request_buf.args[0] = RTSTUB_STM_HOTRESET;
    rtstub_request_buf.args[1] = rtstub_physical(&rtstub_stm_buffers[0]);
    rtstub_request_buf.args[2] = 0x20;
    rtstub_request_buf.args[3] = rtstub_physical(&rtstub_stm_buffers[8]);
    rtstub_request_buf.args[4] = 0x20;
    rtstub_call(1);
}

void rtstub_main(void) {
    uint32_t i;
    int32_t es;
    /* Replies the app left unread would hold up every request: take them. */
    for (i = 0; i < 64 && rtstub_take_reply() != 0; ++i) {
    }
    es = rtstub_open(rtstub_es_path);
    rtstub_result = es;
    if (es >= 0) {
        const uint32_t count = rtstub_count < RTSTUB_MAX_TITLES ? rtstub_count : RTSTUB_MAX_TITLES;
        for (i = 0; i < count; ++i) {
            rtstub_result = rtstub_launch((uint32_t)es, rtstub_titles + i * RTSTUB_TITLE_BYTES);
        }
    }
    rtstub_flush(&rtstub_result, sizeof(rtstub_result));
    rtstub_restart_to_menu();
}
