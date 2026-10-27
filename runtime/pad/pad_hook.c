/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-FileCopyrightText: 2017 Alex Chadwick (wup-028-bslug) <https://github.com/Chadderz121/wup-028-bslug> */
/* SPDX-FileCopyrightText: FIX94 and the Nintendont contributors, used with permission <https://github.com/FIX94/Nintendont> */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The GameCube adapter blob's C side (pad_hook.h): the game's PAD calls
 * in, the driver's IPC through the game's own IOS functions out. */
#include "pad_hook.h"

#include "rtgcad.h"
#include "rthome.h"

#define PAD_STATUS_BYTES 12u  /* PADStatus: button, stick x/y, substick x/y, L, R, analog A/B, err, pad */

uint32_t pad_on_read(struct rt_pad_context* c, uint8_t* status, uint32_t motor);
void pad_on_motor(struct rt_pad_context* c, int32_t chan, uint32_t command);
void pad_on_complete(struct rt_pad_context* c, int32_t result, uint32_t tag);
void pad_on_submit(struct rt_pad_context* c, const uint32_t* block);
uint32_t pad_on_sleep(struct rt_pad_context* c);
uint32_t pad_on_suspend(struct rt_pad_context* c, uint32_t thread);
int32_t pad_on_kpad(struct rt_pad_context* c, int32_t chan, uint8_t* status, int32_t result, int32_t* error);
int32_t pad_on_probe(struct rt_pad_context* c, int32_t chan, uint32_t* type, int32_t result);
void pad_on_menu(struct rt_pad_context* c);
int32_t pad_on_select(struct rt_pad_context* c, int32_t result);

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
    const uint32_t address = (uint32_t)(uintptr_t)block;
    /* The game's own memory (MEM1 or MEM2, cached): the block's command
     * word is read back uncached (pad_check_pending), where IOS writes its
     * reply. The SDK may never drop its cached copy of the block, so a
     * cached read could show the command long after the reply (in Dolphin,
     * Brawl's saves stayed pending until the cap). */
    const int in_game_memory = (address >= 0x80000000u && address < 0x81800000u) ||
                               (address >= 0x90000000u && address < 0x94000000u);
    uint32_t msr;
    uint32_t t;
    if (!rtleave_save_request(block, pad_cached(block[3]), pad_cached(block[4]))) return;
    msr = interrupts_off();
    /* Memory holds the command from now on, before the submit routine's
     * own flush: an uncached read can't see what the block held before. */
    if (in_game_memory) {
        __asm__ volatile("dcbst 0, %0" : : "r"(block) : "memory");
        __asm__ volatile("sync" : : : "memory");
    }
    t = now();
    rtleave_note_save(&c->leave, t);
    /* Anywhere else it can't be read back: counted as overflow instead. */
    rtleave_note_pending(&c->leave, t, in_game_memory ? address : 0, block[0]);
    interrupts_restore(msr);
}

/* The pending save requests: each still with IOS, or now over (its
 * command word in memory, read uncached through 0xC0000000/0xD0000000, no
 * longer the one submitted). Interrupts off, as at its callers. */
static void pad_check_pending(struct rt_pad_context* c, uint32_t t) {
    uint32_t words[RTLEAVE_PENDING_SLOTS + 1];
    uint32_t i;
    for (i = 0; i <= RTLEAVE_PENDING_SLOTS; ++i) {
        const uint32_t entry = i < RTLEAVE_PENDING_SLOTS ? c->leave.pending[i] : c->leave.newest;
        const uint32_t block = RTLEAVE_PENDING_BLOCK(entry);
        words[i] = block != 0 ? *(const volatile uint32_t*)(uintptr_t)(block | 0x40000000u) : 0;
    }
    rtleave_pending(&c->leave, t, words);
}

/* The resident runtime's last SD save write (rt_fs_state's last_write,
 * then wrote: RTLEAVE_RESIDENT_WITH_IOS while a write's command is with
 * IOS, or while its savegame engine has a request in hand or queued), with
 * saves on the SD card; nothing otherwise. */
static void pad_resident_write(const struct rt_pad_context* c, int* wrote, uint32_t* last_write) {
    *wrote = 0;
    *last_write = 0;
    if (c->resident_last_write != 0) {
        const volatile uint32_t* w = (const volatile uint32_t*)(uintptr_t)c->resident_last_write;
        *last_write = w[0];
        *wrote = rtleave_resident(w[1], *(const volatile uint32_t*)(uintptr_t)c->resident_busy,
                                  *(const volatile uint32_t*)(uintptr_t)c->resident_queued);
    }
}

/* At each PADRead, the pads as the game will see them (the adapter's
 * filled in): the combo held long enough, with no save being written,
 * asks for the way back. Letting go before a sleep has left takes the
 * ask back, and the hold starts over. The ask notes the thread this
 * PADRead runs on, the one a game whose main thread never sleeps leaves
 * from (rtleave_sleep_leaves). */
static void pad_home_check(struct rt_pad_context* c, const uint8_t* status, uint32_t t);

static void pad_leave_check(struct rt_pad_context* c, const uint8_t* status, uint32_t t) {
    int resident_wrote;
    uint32_t resident_last_write;
    const int held = rtleave_held(status);
    if (c->home_on) {
        pad_home_check(c, status, t);
        return;
    }
    if (c->return_to_menu == 0) return;
    if (c->leave_asked == RT_PAD_LEAVE_ASKED && !held) {
        c->leave_asked = 0;
        c->leave_thread = 0;
        c->leave_asked_at = 0;
        rtleave_due(&c->leave, 0, t, 0, 0);  /* resets the hold */
        return;
    }
    if (c->leave_asked) return;
    pad_check_pending(c, t);
    pad_resident_write(c, &resident_wrote, &resident_last_write);
    if (rtleave_due(&c->leave, held, t, resident_wrote, resident_last_write)) {
        c->leave_asked = RT_PAD_LEAVE_ASKED;
        c->leave_thread = *(volatile uint32_t*)(uintptr_t)0x800000E4u;
        c->leave_asked_at = t | 1u;
    }
}

/* OSSleepThread: the game's main thread (the OS's default thread, the
 * oldest live one: the head of the OS's thread list at 0x800000DC; running:
 * 0x800000E4) sleeps every frame. Not 0x800000D8: that is the thread whose
 * floating-point registers were last saved, any thread, or 0. Once the way
 * back is asked, the first such sleep leaves through OSReturnToMenu, as
 * the HOME Menu's Wii Menu button does from the same thread. A game
 * whose main thread never sleeps (N64 Virtual Console) leaves from the
 * pad-reading thread's sleep instead, after RT_PAD_LEAVE_FALLBACK_MS
 * with the ask open (rtleave_sleep_leaves). Once only:
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
    const uint32_t main_thread = *(volatile uint32_t*)(uintptr_t)0x800000DCu;
    int resident_wrote, quiet;
    uint32_t resident_last_write, msr, t;
    if (c->leave_asked != RT_PAD_LEAVE_ASKED) return 0;  /* every sleep: the quick way out */
    msr = interrupts_off();
    /* Again with interrupts off: a PADRead in between (another thread's, or
     * the SI callback's) may have taken the ask back, the combo let go. */
    if (c->leave_asked != RT_PAD_LEAVE_ASKED) {
        interrupts_restore(msr);
        return 0;
    }
    t = now();
    if (!rtleave_sleep_leaves(running, main_thread, c->leave_thread, c->leave_asked_at, t,
                              RT_PAD_LEAVE_FALLBACK_MS * c->ticks_per_ms)) {
        interrupts_restore(msr);
        return 0;
    }
    /* A save this very thread is waiting for sleeps it here: its request
     * is still pending, so this sleep doesn't leave. */
    pad_check_pending(c, t);
    pad_resident_write(c, &resident_wrote, &resident_last_write);
    quiet = rtleave_quiet(&c->leave, t, resident_wrote, resident_last_write);
    c->leave_asked = quiet ? RT_PAD_LEAVE_GONE : 0;
    if (!quiet) {
        c->leave_thread = 0;
        c->leave_asked_at = 0;
    }
    interrupts_restore(msr);
    return quiet ? c->return_to_menu : 0;
}

/* OSSuspendThread(thread): the thread that read the pads at the ask,
 * suspending itself, waits there as in a sleep (Cruis'n USA's pad-reading
 * thread, every frame), so it may leave by pad_on_sleep's rules; any other
 * suspend never does. */
uint32_t pad_on_suspend(struct rt_pad_context* c, uint32_t thread) {
    if (c->leave_asked != RT_PAD_LEAVE_ASKED) return 0;
    if (!rtleave_self_suspend(thread, *(volatile uint32_t*)(uintptr_t)0x800000E4u, c->leave_thread)) return 0;
    return pad_on_sleep(c);
}

/* ---- The HOME Menu (#224 part 1): the same combo, the game's own menu ---- */

/* The game's own memory, where it keeps its KPAD buffers and its answers. */
static int pad_in_ram(const void* p) {
    const uint32_t a = (uint32_t)(uintptr_t)p;
    return (a >= 0x80000000u && a < 0x81800000u) || (a >= 0x90000000u && a < 0x94000000u);
}

static void pad_put32(uint8_t* p, uint32_t v) { *(volatile uint32_t*)(void*)p = v; }

/* KPADStatus's fields, laid out alike from the 2007 SDK (132 bytes) to the
 * 2010 one (240): the Wii Remote's buttons, the device type, the error, the
 * pointer's validity, the data format, then the Classic Controller's
 * buttons, sticks and triggers. */
#define PAD_KPAD_HOLD 0x00u
#define PAD_KPAD_TRIG 0x04u
#define PAD_KPAD_RELEASE 0x08u
#define PAD_KPAD_ACC_Y 0x10u       /* acc: x, y, z from 0x0C */
#define PAD_KPAD_ACC_VALUE 0x18u
#define PAD_KPAD_POS_X 0x20u       /* the pointer: x, y */
#define PAD_KPAD_POS_Y 0x24u
#define PAD_KPAD_HORIZON_X 0x34u
#define PAD_KPAD_VERTICAL_Y 0x54u
#define PAD_FLOAT_ONE 0x3F800000u
#define PAD_FLOAT_MINUS_ONE 0xBF800000u
#define PAD_KPAD_DEV_TYPE 0x5Cu
#define PAD_KPAD_WPAD_ERR 0x5Du
#define PAD_KPAD_DPD_VALID 0x5Eu
#define PAD_KPAD_DATA_FORMAT 0x5Fu
#define PAD_KPAD_CL_HOLD 0x60u
#define PAD_KPAD_CL_AXES 0x6Cu  /* left stick x/y, right stick x/y, L, R: six floats */
#define PAD_KPAD_END 0x84u
#define PAD_DEV_CLASSIC 2u
/* The format KPAD reports for a Classic Controller: with the pointer (8)
 * while the HOME Menu is open, without (7) otherwise (seen in Naruto, Mario
 * Kart Wii and Brawl, 23 Oct). A game may check it: Ranger Cross waited
 * forever on a stand-in that reported another format than it asked for. */
#define PAD_FORMAT_CLASSIC 7u
#define PAD_FORMAT_CLASSIC_POINTER 8u

/* KPAD's "both of the pointer's points seen". */
#define PAD_DPD_SEEN 2u

/* The stand-in's status into the game's buffer: a Classic Controller on a
 * Wii Remote, no error. The Remote's own buttons carry the same presses
 * (rthome_remote), and while the HOME Menu is open its pointer is on the
 * screen, for games that look only at the Remote (Bomberman Blast).
 * Interrupts off, at its callers. */
static void pad_home_status(struct rt_pad_context* c, uint8_t* status, uint32_t t) {
    uint32_t hold, trig, release, remote_hold, remote_trig, remote_release, i;
    int32_t axes[6], pointer[2];
    const int menu_open = rthome_menu_open(&c->home);
    rthome_classic(&c->home, t, &hold, &trig, &release, axes);
    rthome_remote(&c->home, &remote_hold, &remote_trig, &remote_release, pointer);
    pad_put32(status + PAD_KPAD_HOLD, remote_hold);
    pad_put32(status + PAD_KPAD_TRIG, remote_trig);
    pad_put32(status + PAD_KPAD_RELEASE, remote_release);
    /* The pointer as KPAD gives it, -1 to 1 across the screen (float bits
     * take numerators over RTHOME_AXIS_FULL). */
    pad_put32(status + PAD_KPAD_POS_X, rthome_float_bits(pointer[0] * RTHOME_AXIS_FULL / RTHOME_POINTER_FULL));
    pad_put32(status + PAD_KPAD_POS_Y, rthome_float_bits(pointer[1] * RTHOME_AXIS_FULL / RTHOME_POINTER_FULL));
    /* The Wii Remote lying still, as KPAD reports a real one with a Classic
     * Controller (Mario Kart Wii, Dolphin): gravity along -Y, acceleration
     * 1.0, the horizon and the vertical level. */
    pad_put32(status + PAD_KPAD_ACC_Y, PAD_FLOAT_MINUS_ONE);
    pad_put32(status + PAD_KPAD_ACC_VALUE, PAD_FLOAT_ONE);
    pad_put32(status + PAD_KPAD_HORIZON_X, PAD_FLOAT_ONE);
    pad_put32(status + PAD_KPAD_VERTICAL_Y, PAD_FLOAT_ONE);
    status[PAD_KPAD_DEV_TYPE] = PAD_DEV_CLASSIC;
    status[PAD_KPAD_WPAD_ERR] = 0;
    status[PAD_KPAD_DPD_VALID] = (uint8_t)(menu_open ? PAD_DPD_SEEN : 0u);
    status[PAD_KPAD_DATA_FORMAT] = (uint8_t)(menu_open ? PAD_FORMAT_CLASSIC_POINTER : PAD_FORMAT_CLASSIC);
    pad_put32(status + PAD_KPAD_CL_HOLD, hold);
    pad_put32(status + PAD_KPAD_CL_HOLD + 4, trig);
    pad_put32(status + PAD_KPAD_CL_HOLD + 8, release);
    for (i = 0; i < 6; ++i) pad_put32(status + PAD_KPAD_CL_AXES + 4 * i, rthome_float_bits(axes[i]));
    ++c->home_writes;
}

/* At each PADRead, in place of the way back: the combo starts the stand-in
 * (rthome_pads). Each stand-in's end is judged (rthome_keep): in a game that
 * can't take one (it never read it, or read it and opened no menu twice in
 * a row) the combo is the way back from then on, where the way back is in,
 * so no game loses the way back it has without part 1. */
static void pad_home_check(struct rt_pad_context* c, const uint8_t* status, uint32_t t) {
    const uint32_t before = c->home.phase;
    (void)rthome_stand_in(&c->home, t);
    /* Judged once, whichever hook saw the stand-in end. */
    if (c->home.phase == RTHOME_DONE && !c->home_judged) {
        c->home_judged = 1;
        if (!rthome_keep(&c->home, c->home_writes != 0) && c->return_to_menu != 0) {
            c->home_on = 0;
            return;
        }
    }
    rthome_pads(&c->home, status, t);
    if (c->home.phase == RTHOME_PRESSING && before == RTHOME_IDLE) {
        c->home_writes = 0;
        c->home_buffer = 0;
        c->home_fresh = 0;
        c->home_judged = 0;
    }
}

/* The KPAD read: on the stand-in's channel the newest status is the
 * stand-in's, and there is one. */
int32_t pad_on_kpad(struct rt_pad_context* c, int32_t chan, uint8_t* status, int32_t result, int32_t* error) {
    uint32_t msr, t, stand_in;
    if (!c->home_on) return result;
    msr = interrupts_off();
    t = now();
    ++c->kpad_reads;
    if (chan >= 0 && chan < (int32_t)RTHOME_CHANNELS) c->kpad_channels |= 1u << chan;
    stand_in = rthome_stand_in(&c->home, t);
    if (stand_in == RTHOME_NO_CHANNEL || (uint32_t)chan != stand_in || !pad_in_ram(status) ||
        c->kpad_status_bytes < PAD_KPAD_END) {
        interrupts_restore(msr);
        return result;
    }
    pad_home_status(c, status, t);
    c->home_buffer = (uint32_t)(uintptr_t)status;
    c->home_fresh = 1;
    if (c->kpad_takes_error && pad_in_ram(error)) *error = 0;
    interrupts_restore(msr);
    return 1;
}

/* WPADProbe: the stand-in's channel has a Classic Controller. The HOME
 * Menu probes every channel each of its frames, and some games stop their
 * own KPAD reads (and their PADRead) while it is up (Kamen Rider: Super
 * Climax Heroes): then the pads are read here, and the stand-in's status
 * goes into the buffer the KPAD read last filled. Only when the KPAD read
 * hasn't written it since the last probe: a second write in one frame
 * would take its newly pressed buttons away before the menu saw them.
 * Other channels' answers say which channels are free. */
int32_t pad_on_probe(struct rt_pad_context* c, int32_t chan, uint32_t* type, int32_t result) {
    uint32_t msr, t, stand_in;
    if (!c->home_on) return result;
    t = now();
    /* Interrupts off for the state, as everywhere (the SI callback's
     * PADRead changes it too), then on again for the game's PADRead. */
    msr = interrupts_off();
    stand_in = rthome_stand_in(&c->home, t);
    interrupts_restore(msr);
    /* Only while a stand-in is up or a HOME Menu runs (rthome_reads_pads),
     * in a game that reads its pads but has stopped: a Virtual Console
     * game's emulator stops while its HOME Menu is open, and the combo, let
     * go there, would never arm again. Never in a game that has not called
     * PADRead at all (it never set the pads up). */
    /* Once a round of probes: on the stand-in's channel, else on channel 0. */
    const uint32_t read_on = stand_in != RTHOME_NO_CHANNEL ? stand_in : 0u;
    if ((uint32_t)chan == read_on && c->pad_read != 0 && c->last_pad_read != 0 &&
        t - c->last_pad_read > 50u * c->ticks_per_ms &&
        rthome_reads_pads(&c->home, t, 100u * c->ticks_per_ms)) {
        /* The game's PADRead, through the hook, so the adapter fills it and
         * the stand-in keeps up with the pad. Interrupts on, as a game
         * thread calls it. */
        ((uint32_t(*)(uint8_t*))(uintptr_t)c->pad_read)(c->pad_scratch);
    }
    msr = interrupts_off();
    t = now();  /* again: that PADRead may have noted a later time */
    ++c->probes;
    stand_in = rthome_stand_in(&c->home, t);
    if (stand_in == RTHOME_NO_CHANNEL || (uint32_t)chan != stand_in) {
        if (chan >= 0) rthome_probed(&c->home, (uint32_t)chan, result);
        interrupts_restore(msr);
        return result;
    }
    if (pad_in_ram(type)) *type = PAD_DEV_CLASSIC;
    if (!c->home_fresh && pad_in_ram((const void*)(uintptr_t)c->home_buffer))
        pad_home_status(c, (uint8_t*)(uintptr_t)c->home_buffer, t);
    c->home_fresh = 0;
    interrupts_restore(msr);
    return 0;
}

/* HBMCalc: the HOME Menu runs this frame. */
void pad_on_menu(struct rt_pad_context* c) {
    const uint32_t msr = interrupts_off();
    ++c->menu_frames;
    if (c->home_on) rthome_menu_frame(&c->home, now());
    interrupts_restore(msr);
}

/* HBMGetSelectBtnNum: the game asks which button its menu ended on (-1:
 * none yet), every frame the menu runs. */
int32_t pad_on_select(struct rt_pad_context* c, int32_t result) {
    uint32_t msr;
    if (result == -1) return result;
    msr = interrupts_off();
    ++c->selects;
    c->last_select = (uint32_t)result;
    if (c->home_on) rthome_selected(&c->home, result);
    interrupts_restore(msr);
    return result;
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
    c->last_pad_read = t | 1u;
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
