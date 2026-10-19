/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The GameCube controller adapter in a GameCube game (gc_gcad.h). Same
 * rules as the rest of the blob: no globals, no constant data, no library
 * calls. */
#include "gc_gcad.h"

#include "gc_ports.h"
#include "gc_rt.h"

#define GC_IOS_IOCTL 6u
#define GC_IOS_IOCTLV 7u
#define GC_PAD_STATUS_BYTES 12u
#define GC_PAD_NO_CONTROLLER 0xFFu  /* PADStatus err -1 */

static uint32_t ad_now(void) {
#ifdef RT_TARGET_PPC
    uint32_t tb;
    __asm__ volatile("mftb %0" : "=r"(tb) : : "memory");
    return tb;
#else
    return gc_gcad_host_now();
#endif
}

/* The driver's environment, its replies and the poll talk to IOS: on the
 * Wii only. The ports below are built into the host tests too. */
#ifdef RT_TARGET_PPC
static uint32_t ad_phys(uint32_t p) { return p & 0x3FFFFFFFu; }

/* --- The driver's environment (rtgcad.h) ----------------------------- */

static struct gc_gcad_rq* ad_slot(gcad* g, uint32_t tag) {
    struct gc_gcad* ad = (struct gc_gcad*)g->env_ctx;
    const uint32_t slot = tag & 15u;
    if (slot >= GCAD_TAGS || ad->rq[slot].state == GC_GCAD_RQ_SENT) return 0;
    return &ad->rq[slot];
}

/* The request in `rq` to IOS. Its buffers are flushed: IOS reads the
 * ones going in, and a dirty line left over one it writes would land on
 * top of its answer later. */
static int32_t ad_send(gcad* g, struct gc_gcad_rq* rq, uint32_t tag) {
    struct gc_gcad* ad = (struct gc_gcad*)g->env_ctx;
    struct gc_rt_context* ctx = (struct gc_rt_context*)(uintptr_t)ad->ctx;
    rq->tag = tag;
    rq->state = GC_GCAD_RQ_SENT;
    gc_flush((uint32_t)(uintptr_t)rq, sizeof(rq->ipc) + sizeof(rq->vec));
    ad->sends++;
    if (ctx == 0 || gc_ipc_send(ad_phys((uint32_t)(uintptr_t)rq->ipc), ctx) != 0) {
        rq->state = GC_GCAD_RQ_IDLE;
        ad->send_failures++;
        return -1;
    }
    return 0;
}

int32_t gcad_env_ioctl(gcad* g, uint32_t tag, uint32_t cmd, void* in, uint32_t in_len, void* out, uint32_t out_len) {
    struct gc_gcad* ad = (struct gc_gcad*)g->env_ctx;
    struct gc_gcad_rq* rq = ad_slot(g, tag);
    uint32_t i;
    if (rq == 0) return -1;
    for (i = 0; i < 16; ++i) rq->ipc[i] = 0;
    rq->ipc[0] = GC_IOS_IOCTL;
    rq->ipc[2] = (uint32_t)ad->fd;
    rq->ipc[3] = cmd;
    rq->ipc[4] = in ? ad_phys((uint32_t)(uintptr_t)in) : 0;
    rq->ipc[5] = in_len;
    rq->ipc[6] = out ? ad_phys((uint32_t)(uintptr_t)out) : 0;
    rq->ipc[7] = out_len;
    if (in && in_len) gc_flush((uint32_t)(uintptr_t)in, in_len);
    if (out && out_len) gc_flush((uint32_t)(uintptr_t)out, out_len);
    rq->out = out ? (uint32_t)(uintptr_t)out : 0;
    rq->out_len = out_len;
    return ad_send(g, rq, tag);
}

int32_t gcad_env_ioctlv(gcad* g, uint32_t tag, uint32_t cmd, void* msg, uint32_t msg_len, void* data,
                        uint32_t data_len, int data_in) {
    struct gc_gcad* ad = (struct gc_gcad*)g->env_ctx;
    struct gc_gcad_rq* rq = ad_slot(g, tag);
    uint32_t i;
    if (rq == 0) return -1;
    for (i = 0; i < 16; ++i) rq->ipc[i] = 0;
    rq->vec[0] = ad_phys((uint32_t)(uintptr_t)msg);
    rq->vec[1] = msg_len;
    rq->vec[2] = ad_phys((uint32_t)(uintptr_t)data);
    rq->vec[3] = data_len;
    rq->ipc[0] = GC_IOS_IOCTLV;
    rq->ipc[2] = (uint32_t)ad->fd;
    rq->ipc[3] = cmd;
    rq->ipc[4] = data_in ? 2u : 1u;
    rq->ipc[5] = data_in ? 0u : 1u;
    rq->ipc[6] = ad_phys((uint32_t)(uintptr_t)rq->vec);
    gc_flush((uint32_t)(uintptr_t)msg, msg_len);
    gc_flush((uint32_t)(uintptr_t)data, data_len);
    rq->out = data_in ? 0u : (uint32_t)(uintptr_t)data;
    rq->out_len = data_in ? 0u : data_len;
    return ad_send(g, rq, tag);
}

uint32_t gcad_env_address(gcad* g, void* p) {
    (void)g;
    return (uint32_t)(uintptr_t)p;  /* v4 takes the virtual address (wiibrew) */
}

void gcad_env_flush(gcad* g, void* p, uint32_t len) {
    (void)g;
    gc_flush((uint32_t)(uintptr_t)p, len);
}

void gcad_env_invalidate(gcad* g, void* p, uint32_t len) {
    (void)g;
    gc_invalidate((uint32_t)(uintptr_t)p, len);
}

void gcad_env_event(gcad* g, uint32_t code, uint32_t a, int32_t b) {
    (void)g;
    (void)code;
    (void)a;
    (void)b;
}

/* --- Replies and the poll -------------------------------------------- */

int gc_gcad_on_reply(struct gc_gcad* ad, uint32_t reply) {
    uint32_t i;
    for (i = 0; i < GCAD_TAGS; ++i) {
        struct gc_gcad_rq* rq = &ad->rq[i];
        if (rq->state != GC_GCAD_RQ_SENT || ad_phys((uint32_t)(uintptr_t)rq->ipc) != reply) continue;
        gc_invalidate((uint32_t)(uintptr_t)rq->ipc, sizeof(rq->ipc));
        if (rq->out) gc_invalidate(rq->out, rq->out_len);
        rq->result = (int32_t)rq->ipc[1];
        rq->state = GC_GCAD_RQ_DONE;
        ad->replies++;
        return 1;
    }
    return 0;
}

/* The driver is only ever entered from here, with interrupts off and the
 * IPC free: a reply that comes while one of its requests is being sent is
 * marked done and handed over by the loop below, never from inside the
 * driver. */
void gc_gcad_poll(struct gc_rt_context* ctx, struct gc_gcad* ad) {
    uint32_t msr, reply, i, handed;
    if (ad->fd < 0) return;
    msr = gc_irq_off();
    if (ctx->busy) {  /* a disc read is waiting on IOS: next frame */
        gc_irq_restore(msr);
        return;
    }
    ctx->busy = 1;
    ad->ctx = (uint32_t)(uintptr_t)ctx;
    ad->polls++;
    if (!ad->inited) {
        gcad_init(&ad->g, ad, ad->fd, ad->version, ad->ticks_per_ms);
        if (ad->known_dev >= 0) gcad_expect(&ad->g, ad->known_dev);
        ad->inited = 1;
    }
    while ((reply = gc_ipc_take()) != 0) gc_ipc_route(ctx, reply);
    do {
        handed = 0;
        for (i = 0; i < GCAD_TAGS; ++i) {
            struct gc_gcad_rq* rq = &ad->rq[i];
            if (rq->state != GC_GCAD_RQ_DONE) continue;
            rq->state = GC_GCAD_RQ_IDLE;
            gcad_on_reply(&ad->g, rq->tag, rq->result, ad_now());
            handed = 1;
        }
    } while (handed);
    gcad_tick(&ad->g, ad_now());
    ctx->busy = 0;
    gc_guard(ctx);
    gc_irq_restore(msr);
}

#endif /* RT_TARGET_PPC */

/* --- The game's ports ------------------------------------------------- */

void gc_gcad_fill(struct gc_gcad* ad, uint8_t* status) {
    const uint32_t now = ad_now();
    gcad_pad pads[GCAD_PORTS];
    uint32_t i, present = 0, taken = 0;
    if (!ad->inited) return;
    /* A port whose err is anything but "no controller" (one starting, a
     * transfer error) has a GameCube controller in it, which keeps it. */
    for (i = 0; i < GC_PORTS; ++i)
        if (status[GC_PAD_STATUS_BYTES * i + 10] != GC_PAD_NO_CONTROLLER) taken |= 1u << i;
    for (i = 0; i < GCAD_PORTS; ++i)
        if (gcad_port(&ad->g, i, &pads[i], now)) present |= 1u << i;
    gc_ports_assign(taken, present, ad->port_of);
    ad->filled = 0;
    for (i = 0; i < GCAD_PORTS; ++i) {
        const uint32_t port = ad->port_of[i];
        uint8_t* s;
        if (port >= GC_PORTS) continue;
        s = status + GC_PAD_STATUS_BYTES * port;
        s[0] = (uint8_t)(pads[i].buttons >> 8);
        s[1] = (uint8_t)pads[i].buttons;
        s[2] = (uint8_t)pads[i].stick_x;
        s[3] = (uint8_t)pads[i].stick_y;
        s[4] = (uint8_t)pads[i].substick_x;
        s[5] = (uint8_t)pads[i].substick_y;
        s[6] = pads[i].trigger_l;
        s[7] = pads[i].trigger_r;
        s[8] = 0;
        s[9] = 0;
        s[10] = 0;
        ad->filled |= 1u << port;
    }
}

void gc_gcad_motor(struct gc_gcad* ad, uint32_t chan, uint32_t command) {
    uint32_t i;
    if (!ad->inited || chan >= GC_PORTS) return;
    for (i = 0; i < GCAD_PORTS; ++i) {
        if (ad->port_of[i] != chan) continue;
        gcad_rumble(&ad->g, i, ad->rumble ? command : 0u);
        return;
    }
}
