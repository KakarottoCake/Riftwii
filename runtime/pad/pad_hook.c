/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-FileCopyrightText: 2017 Alex Chadwick (wup-028-bslug) <https://github.com/Chadderz121/wup-028-bslug> */
/* SPDX-FileCopyrightText: FIX94 and the Nintendont contributors, used with permission <https://github.com/FIX94/Nintendont> */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The GameCube adapter blob's C side (pad_hook.h): the game's PAD calls
 * in, the driver's IPC through the game's own IOS functions out. */
#include "pad_hook.h"

#include "rtgcad.h"

#define PAD_STATUS_BYTES 12u  /* PADStatus: button, stick x/y, substick x/y, L, R, analog A/B, err, pad */

uint32_t pad_on_read(struct rt_pad_context* c, uint8_t* status, uint32_t motor);
void pad_on_motor(struct rt_pad_context* c, int32_t chan, uint32_t command);
void pad_on_complete(struct rt_pad_context* c, int32_t result, uint32_t tag);
void pad_on_submit(struct rt_pad_context* c, const uint32_t* block);
uint32_t pad_on_sleep(struct rt_pad_context* c);

/* External interrupts off around the driver: PADRead runs on game
 * threads (some from the SI sampling callback), the replies in the IPC
 * interrupt. */
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

/* The game's IOS_IoctlAsync (fd, ioctl, in, in length, out, out length,
 * callback, user data) and IOS_IoctlvAsync (fd, ioctl, in count, io
 * count, vectors, callback, user data). */
typedef int32_t (*pad_ioctl_fn)(int32_t fd, uint32_t ioctl, void* in, uint32_t in_len, void* out,
                                uint32_t out_len, uint32_t callback, uint32_t tag);
typedef int32_t (*pad_ioctlv_fn)(int32_t fd, uint32_t ioctl, uint32_t in_count, uint32_t io_count, uint32_t* vec,
                                 uint32_t callback, uint32_t tag);
/* The game's screen-dimming reset for its controller ports: clears a
 * flag the video code checks once a frame, returns 1. */
typedef int32_t (*pad_idle_reset_fn)(void);

/* The game's PADRead only sees pads in the console's own ports, so it
 * never tells the video code that an adapter pad is in use: with the
 * Wii's screen burn-in reduction on, adapter-only play dimmed the screen
 * after five minutes (Project+ on a Wii U, 2610-541). Each change an
 * adapter pad makes is passed on the way PADRead passes its own. */
static void keep_screen_lit(const struct rt_pad_context* c) {
    pad_idle_reset_fn reset = (pad_idle_reset_fn)(uintptr_t)c->idle_reset;
    if (reset != 0) reset();
}

int32_t gcad_env_ioctl(gcad* g, uint32_t tag, uint32_t cmd, void* in, uint32_t in_len, void* out, uint32_t out_len) {
    struct rt_pad_context* c = (struct rt_pad_context*)g->env_ctx;
    pad_ioctl_fn fn = (pad_ioctl_fn)(uintptr_t)c->ioctl_async;
    if (fn == 0) return -1;
    return fn(c->fd, cmd, in, in_len, out, out_len, c->complete_entry, tag);
}

int32_t gcad_env_ioctlv(gcad* g, uint32_t tag, uint32_t cmd, void* msg, uint32_t msg_len, void* data,
                        uint32_t data_len, int data_in) {
    struct rt_pad_context* c = (struct rt_pad_context*)g->env_ctx;
    pad_ioctlv_fn fn = (pad_ioctlv_fn)(uintptr_t)c->ioctlv_async;
    /* Each slot its own two vectors, in MEM2 with the rest: the SDK
     * turns them into physical addresses in place, so two requests in
     * flight never share them. The slots using ioctlv are 2, 3 and 4. */
    const uint32_t slot = (tag & 15u) - GCAD_TAG_SETUP;
    uint32_t* vec;
    if (fn == 0 || slot > 2) return -1;
    vec = (uint32_t*)(void*)(g->env + slot * 16u);
    vec[0] = (uint32_t)(uintptr_t)msg;
    vec[1] = msg_len;
    vec[2] = (uint32_t)(uintptr_t)data;
    vec[3] = data_len;
    return fn(c->fd, cmd, data_in ? 2u : 1u, data_in ? 0u : 1u, vec, c->complete_entry, tag);
}

uint32_t gcad_env_address(gcad* g, void* p) {
    (void)g;
    return (uint32_t)(uintptr_t)p;  /* v4 takes the virtual address (wiibrew) */
}

void gcad_env_flush(gcad* g, void* p, uint32_t len) {
    uintptr_t line = (uintptr_t)p & ~(uintptr_t)31u;
    const uintptr_t end = (uintptr_t)p + len;
    (void)g;
    for (; line < end; line += 32u) __asm__ volatile("dcbf 0, %0" : : "r"(line) : "memory");
    __asm__ volatile("sync" : : : "memory");
}

void gcad_env_invalidate(gcad* g, void* p, uint32_t len) {
    /* Only ever on whole buffers of whole lines that the CPU did not
     * write since they went to IOS. */
    uintptr_t line = (uintptr_t)p & ~(uintptr_t)31u;
    const uintptr_t end = (uintptr_t)p + len;
    (void)g;
    for (; line < end; line += 32u) __asm__ volatile("dcbi 0, %0" : : "r"(line) : "memory");
    __asm__ volatile("sync" : : : "memory");
}

void gcad_env_event(gcad* g, uint32_t code, uint32_t a, int32_t b) {
    (void)g;
    (void)code;
    (void)a;
    (void)b;
}

static gcad* state(struct rt_pad_context* c) {
    gcad* g = (gcad*)(uintptr_t)c->state;
    if (!c->inited) {
        gcad_init(g, c, c->fd, c->version, c->ticks_per_ms);
        if (c->known_dev >= 0) gcad_expect(g, c->known_dev);
        c->inited = 1;
    }
    return g;
}

/* ---- The way back (#224): L, R, Z and Start held on a GameCube pad ---- */

/* A physical address the SDK stored in the block, as the CPU reads it
 * (cached), or 0 for none. The SDK stores only the game's own buffers
 * there (MEM1 or MEM2), and rtleave_save_request reads one only for an
 * open or a /dev/fs change, whose argument is always a buffer. */
static const uint8_t* pad_cached(uint32_t physical) {
    return physical != 0 ? (const uint8_t*)(uintptr_t)(physical | 0x80000000u) : 0;
}

/* Every IOS request the game makes passes here (the SDK's submit
 * routine): a save's is noted. The request's first two arguments as
 * addresses, for rtleave_save_request to read where they are paths. */
void pad_on_submit(struct rt_pad_context* c, const uint32_t* block) {
    if (rtleave_save_request(block, pad_cached(block[3]), pad_cached(block[4]))) rtleave_note_save(&c->leave, now());
}

/* The resident runtime's last SD save write (rt_fs_state's last_write,
 * then wrote, as it is: RTLEAVE_RESIDENT_WITH_IOS while a write's command
 * is with IOS), with saves on the SD card; nothing otherwise. */
static void pad_resident_write(const struct rt_pad_context* c, int* wrote, uint32_t* last_write) {
    *wrote = 0;
    *last_write = 0;
    if (c->resident_last_write != 0) {
        const volatile uint32_t* w = (const volatile uint32_t*)(uintptr_t)c->resident_last_write;
        *last_write = w[0];
        *wrote = (int)w[1];
    }
}

/* At each PADRead, the pads as the game will see them (the adapter's
 * filled in): the combo held long enough, with no save being written,
 * asks for the way back. Letting go before the main thread's sleep has
 * left takes the ask back, and the hold starts over. */
static void pad_leave_check(struct rt_pad_context* c, const uint8_t* status, uint32_t t) {
    int resident_wrote;
    uint32_t resident_last_write;
    const int held = rtleave_held(status);
    if (c->return_to_menu == 0) return;
    if (c->leave_asked == RT_PAD_LEAVE_ASKED && !held) {
        c->leave_asked = 0;
        rtleave_due(&c->leave, 0, t, 0, 0);  /* resets the hold */
        return;
    }
    if (c->leave_asked) return;
    pad_resident_write(c, &resident_wrote, &resident_last_write);
    if (rtleave_due(&c->leave, held, t, resident_wrote, resident_last_write)) c->leave_asked = RT_PAD_LEAVE_ASKED;
}

/* OSSleepThread: the game's main thread (the OS's default thread, at
 * 0x800000D8, running: 0x800000E4) sleeps every frame. Once the way
 * back is asked, the first such sleep leaves through OSReturnToMenu, as
 * the HOME Menu's Wii Menu button does from the same thread. Once only:
 * OSReturnToMenu writes the System Menu's state files with synchronous
 * calls, whose waits sleep the main thread through here again (in
 * Dolphin, Mario Kart Wii opened state.dat twice and stopped). A save
 * begun since the ask (between the PADRead and this sleep) takes the ask
 * back: the combo, still held, asks again once the save has been quiet.
 * The time and the save times are read with interrupts off, as at
 * PADRead, so a save noted in between can't look newer than the time
 * (the unsigned difference would wrap and read as long quiet). */
uint32_t pad_on_sleep(struct rt_pad_context* c) {
    const uint32_t running = *(volatile uint32_t*)(uintptr_t)0x800000E4u;
    const uint32_t main_thread = *(volatile uint32_t*)(uintptr_t)0x800000D8u;
    int resident_wrote, quiet;
    uint32_t resident_last_write, msr;
    if (c->leave_asked != RT_PAD_LEAVE_ASKED || running != main_thread) return 0;
    msr = interrupts_off();
    pad_resident_write(c, &resident_wrote, &resident_last_write);
    quiet = rtleave_quiet(&c->leave, now(), resident_wrote, resident_last_write);
    c->leave_asked = quiet ? RT_PAD_LEAVE_GONE : 0;
    interrupts_restore(msr);
    return quiet ? c->return_to_menu : 0;
}

uint32_t pad_on_read(struct rt_pad_context* c, uint8_t* status, uint32_t motor) {
    const uint32_t msr = interrupts_off();
    const uint32_t t = now();
    gcad* g = state(c);
    uint32_t i, mask = 0;
    gcad_tick(g, t);
    for (i = 0; i < GCAD_PORTS; ++i) {
        uint8_t* s = status + i * PAD_STATUS_BYTES;
        gcad_pad pad;
        /* A pad in the console's own port wins; only ports reporting an
         * error (none plugged, or not ready) take the adapter's. */
        if ((int8_t)s[10] == 0) continue;
        if (c->flags & RT_PAD_FLAG_DEMO) {
            /* A for 6 of every 64 reads (PADRead runs once a frame or so). */
            if (mask != 0) continue;
            pad.buttons = (c->reads & 63u) < 6u ? GCAD_PAD_A : 0u;
            pad.stick_x = pad.stick_y = pad.substick_x = pad.substick_y = 0;
            pad.trigger_l = pad.trigger_r = 0;
            pad.wireless = 1;
        } else if (!gcad_port(g, i, &pad, t)) {
            continue;
        }
        s[0] = (uint8_t)(pad.buttons >> 8);
        s[1] = (uint8_t)pad.buttons;
        s[2] = (uint8_t)pad.stick_x;
        s[3] = (uint8_t)pad.stick_y;
        s[4] = (uint8_t)pad.substick_x;
        s[5] = (uint8_t)pad.substick_y;
        s[6] = pad.trigger_l;
        s[7] = pad.trigger_r;
        s[8] = 0;
        s[9] = 0;
        s[10] = 0;
        mask |= 1u << i;
        if (!pad.wireless) motor |= 0x80000000u >> i;  /* PADRead's "has a motor" bits */
        /* Against the pad as it was when it last counted, not the last
         * read: a stick moved slowly, a few steps a read, still counts
         * once it has gone GCAD_ACTIVE_STEP, and a resting stick's wobble
         * never adds up to that. */
        if (gcad_pad_moved(&c->last[i], &pad)) {
            keep_screen_lit(c);
            c->last[i] = pad;
        }
    }
    c->overlay_mask = mask;
    ++c->reads;
    pad_leave_check(c, status, t);
    interrupts_restore(msr);
    return motor;
}

void pad_on_motor(struct rt_pad_context* c, int32_t chan, uint32_t command) {
    uint32_t msr;
    if (chan < 0 || chan >= (int32_t)GCAD_PORTS || !c->inited) return;
    msr = interrupts_off();
    gcad_rumble((gcad*)(uintptr_t)c->state, (uint32_t)chan, (c->overlay_mask >> chan) & 1u ? command : 0u);
    interrupts_restore(msr);
}

void pad_on_complete(struct rt_pad_context* c, int32_t result, uint32_t tag) {
    const uint32_t msr = interrupts_off();
    ++c->replies;
    if (c->inited) gcad_on_reply((gcad*)(uintptr_t)c->state, tag, result, now());
    interrupts_restore(msr);
}
