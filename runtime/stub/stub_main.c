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
#ifndef RTSTUB_IPC_POLLS
#define RTSTUB_IPC_POLLS 0x04000000u
#endif
#define RTSTUB_NO_REPLY (-1000)

struct rtstub_request {
    uint32_t command;
    int32_t result;
    uint32_t fd;
    uint32_t args[5];
};

/* What IOS reads by DMA: 32-byte aligned, and flushed before each send. */
static struct rtstub_request rtstub_request_buf __attribute__((aligned(32)));
/* Launch vectors and reset buffers have disjoint lifetimes. IOS has
 * answered every launch refusal before the reset reuses these lines. */
static uint32_t rtstub_payload[16] __attribute__((aligned(32)));
static const char rtstub_es_path[] __attribute__((aligned(32))) = "/dev/es";
static const char rtstub_stm_path[] __attribute__((aligned(32))) = "/dev/stm/immediate";

extern uint32_t rtstub_count;
extern int32_t rtstub_result;
extern uint8_t rtstub_titles[];

#ifdef RTSTUB_HOST_TEST
#include "stub_host.h"
#else
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

#endif

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

/* Build the three request shapes here. Keeping the zeroing and IOS
 * call together avoids a separate copy of them in every caller. */
static int32_t rtstub_request(uint32_t command, uint32_t fd, const void* data) {
    struct rtstub_request* request = &rtstub_request_buf;
    uint32_t i;
    uint32_t* words = (uint32_t*)(void*)request;
    for (i = 0; i < sizeof(*request) / 4u; ++i) words[i] = 0;
    request->command = command;
    request->fd = fd;
    if (command == RTSTUB_IOS_OPEN) {
        request->args[0] = rtstub_physical(data);
        rtstub_flush(data, 32u);
        return rtstub_call(0);
    } else if (command == RTSTUB_IOS_IOCTLV) {
        const uint8_t* slot = data;
        rtstub_payload[0] = rtstub_physical(slot);
        rtstub_payload[1] = 8u;
        rtstub_payload[2] = rtstub_physical(slot + RTSTUB_TITLE_VIEW);
        rtstub_payload[3] = RTSTUB_VIEW_BYTES;
        rtstub_flush(slot, RTSTUB_TITLE_BYTES);
        request->args[0] = RTSTUB_ES_LAUNCHTITLE;
        request->args[1] = 2u;
        request->args[3] = rtstub_physical(rtstub_payload);
    } else {
        for (i = 0; i < 16u; ++i) rtstub_payload[i] = 0;
        request->args[0] = RTSTUB_STM_HOTRESET;
        request->args[1] = rtstub_physical(rtstub_payload);
        request->args[2] = 32u;
        request->args[3] = rtstub_physical(rtstub_payload + 8);
        request->args[4] = 32u;
    }
    const uint32_t payload_bytes = command == RTSTUB_IOS_IOCTLV ? 16u : sizeof(rtstub_payload);
    rtstub_flush(rtstub_payload, payload_bytes);
    return rtstub_call(1);
}

void rtstub_main(void) {
    uint32_t i;
    int32_t es;
    /* Replies the app left unread would hold up every request: take them. */
    for (i = 0; i < 64 && rtstub_take_reply() != 0; ++i) {
    }
    es = rtstub_request(RTSTUB_IOS_OPEN, 0, rtstub_es_path);
    rtstub_result = es;
    if (es >= 0) {
        const uint32_t count = rtstub_count < RTSTUB_MAX_TITLES ? rtstub_count : RTSTUB_MAX_TITLES;
        for (i = 0; i < count; ++i) {
            rtstub_result = rtstub_request(RTSTUB_IOS_IOCTLV, (uint32_t)es, rtstub_titles + i * RTSTUB_TITLE_BYTES);
        }
    }
    rtstub_flush(&rtstub_result, sizeof(rtstub_result));
    {
        const int32_t stm = rtstub_request(RTSTUB_IOS_OPEN, 0, rtstub_stm_path);
        if (stm >= 0) rtstub_request(RTSTUB_IOS_IOCTL, (uint32_t)stm, 0);
    }
}
