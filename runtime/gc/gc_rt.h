/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* GameCube games in Wii mode, without the disc drive: the game's
 * DVDLow* functions branch here and are served from the SD card.
 *
 * A GameCube game drives the disc through the DI registers and waits
 * for the DI interrupt; in Wii mode those belong to IOS. So the SDK's
 * low-level commands are replaced: a read is done at once through IOS's
 * /dev/sdio/slot0 (IPC driven by polling, since the game has no IPC
 * code and its interrupt table knows nothing of IOS), and the command's
 * callback runs from one of the game's own OSAlarms a moment later, as
 * the DI interrupt would have run it.
 *
 * Layout (gc_entry.S): the header below, the trampolines, the C code,
 * then the context. Position independent (linked at two bases and
 * compared); it sits in MEM1 just below the FST, under the game's arena.
 * Every buffer IOS touches is in the context, 32-byte aligned. */
#ifndef RIFTWII_GC_RT_H
#define RIFTWII_GC_RT_H

#include <stdint.h>

#include "gc_crash.h"
#include "gc_layout.h"
#include "gc_load.h"

#define GC_RT_MAGIC 0x52574744u          /* "RWGD" */
#define GC_RT_VERSION 8u                 /* also in gc_entry.S's header */
#define GC_RT_CONTEXT_MAGIC 0x52574743u  /* "RWGC" */

/* The DI interrupt types a DVDLow callback takes. */
#define GC_DI_TC 1u     /* transfer complete */
#define GC_DI_DE 2u     /* device error */
/* A completion whose callback is this raises the DI interrupt in the DI
 * stand-in and runs the game's DI interrupt handler (libogc, below). */
#define GC_CB_DI_RAISE 1u

/* gc_rt_run.kind */
#define GC_RUN_SD 0u    /* `source` is a 512-byte sector of the card, `skip` bytes into it */
#define GC_RUN_ZERO 1u
#define GC_RUN_MEM 2u   /* `source` is an address (bytes the loader placed in memory) */
#define GC_RUN_RVZ 3u   /* `source` is the image's disc offset, read from its RVZ (gc_rvz.h; the RVZ build only) */
#define GC_RUN_USB 4u   /* `source` is a 512-byte sector of the USB drive (d2x's /dev/usb2), `skip` bytes into it */

/* One stretch of the game's view of the disc. Sorted by `disc`, no gaps
 * the game reads (a read past the last run fails). */
struct gc_rt_run {
    uint32_t disc;
    uint32_t length;
    uint32_t source;
    uint32_t skip;
    uint32_t kind;
};

/* Big-endian words at the start of the blob: offsets from its start. */
struct gc_rt_header {
    uint32_t magic;
    uint32_t version;
    uint32_t size;            /* code, then the context */
    uint32_t context_offset;
    uint32_t hook_read;       /* DVDLowRead(addr, length, offset, cb) */
    uint32_t hook_seek;       /* DVDLowSeek(offset, cb) */
    uint32_t hook_disk_id;    /* DVDLowReadDiskID(id, cb) */
    uint32_t hook_inquiry;    /* DVDLowInquiry(info, cb) */
    uint32_t hook_cb_r3;      /* DVDLowStopMotor(cb), DVDLowRequestError(cb) */
    uint32_t hook_audio_status;  /* DVDLowRequestAudioStatus(sub, cb) */
    uint32_t hook_cb_r5;      /* DVDLowAudioBufferConfig(on, size, cb) */
    uint32_t hook_audio_stream;  /* DVDLowAudioStream(sub, length, offset, cb) */
    uint32_t alarm_entry;     /* the OSAlarm handler that runs the callback */
    uint32_t hook_ai_dma;     /* AIInitDMA(start, length): the stream mixed in, then the original */
    uint32_t replay_ai_dma;   /* 16 bytes: AIInitDMA's displaced first word, then padding */
    uint32_t continue_ai_dma; /* 16 bytes: the jump to its second word */
    uint32_t hook_pad_read;   /* PADRead(status): the original, then the test presses (GC_FLAG_PRESS) */
    uint32_t replay_pad_read;
    uint32_t continue_pad_read;
    uint32_t hook_ar_dma;     /* ARStartDMA(type, main, aram, length): copied by the CPU, the original, then gc_after_ar_dma */
    uint32_t replay_ar_dma;
    uint32_t continue_ar_dma;
    uint32_t trace_now;       /* trace(where): a record from the loader, just before the jump */
    uint32_t hook_fatal;      /* __OSUnhandledException(exception, context, dsisr, dar): shown, never returns */
    uint32_t dsi_entry;       /* an exception before the game's OS has its handlers (the loader's vector stubs) */
    uint32_t exi_hooks;       /* GC_EXI_HOOKS entries (gc_card.h) of three words: hook, replay, continue */
    uint32_t hook_vi_configure;     /* VIConfigure(rm): gc_on_vi_configure picks the mode, then the original */
    uint32_t replay_vi_configure;
    uint32_t continue_vi_configure;
    uint32_t hook_projection;       /* GXSetProjection(mtx, type): a perspective one widened, then the original */
    uint32_t replay_projection;
    uint32_t continue_projection;
    uint32_t stray_entry;     /* a jump into the blob's own data (gc_card.h's landing words call it) */
    uint32_t hook_load_queued;      /* Luigi's Mansion's loader queue post(request): gc_on_load_queued */
    uint32_t hook_load_alloc;       /* its loading thread's alloc(length, alignment) for a buffer: gc_on_load_alloc */
    uint32_t hook_load_callbacks;   /* its loading thread's group(id) before a load's callbacks: gc_on_load_callbacks */
    uint32_t hook_load_callbacks_done; /* and group(old) after them: gc_on_load_callbacks_done */
    uint32_t hook_pad_motor;  /* PADControlMotor(chan, command): the original, then gc_on_pad_motor */
    uint32_t replay_pad_motor;
    uint32_t continue_pad_motor;
    uint32_t hook_reset_system;     /* OSResetSystem(reset, code, menu): gc_on_reset_system, then the original */
    uint32_t replay_reset_system;
    uint32_t continue_reset_system;
    uint32_t hook_hot_reset;        /* the SDK's reset routine (PI's reset register): gc_on_hot_reset, never returns */
    uint32_t alarm_entry_ogc;       /* libogc's alarm callback (handle, arg): gc_on_alarm with arg as the alarm */
    uint32_t di_common;             /* what a DI start slot calls: gc_on_di_start */
    uint32_t di_slots;              /* GC_DI_SITES slots of GC_DI_SLOT_WORDS words (gc_layout.h), loader-written */
    uint32_t hook_boot_dol;         /* __OSBootDolSimple(...): gc_on_boot_dol, then the original */
    uint32_t replay_boot_dol;
    uint32_t continue_boot_dol;
};

/* libogc. A game built on libogc (Sonic Riders Tournament Edition, a
 * rebuild compiled with GCC) has no OSSetAlarm and no DVDLow functions to
 * hook: its alarms are SYS_CreateAlarm(&handle) and SYS_SetAlarm(handle,
 * const struct timespec*, cb, arg), and its disc code (libogc's dvd.c)
 * writes the DI's registers itself, many commands of them (the drive's
 * unlock and firmware ones too). So the DI is answered at its registers:
 * the game's DI accesses go to the DI stand-in as for any game, each store
 * that starts a command (DICR, 0xCC00601C, bit 0) jumps to a slot that
 * makes it and calls gc_on_di_start, which carries the command out from
 * the card, and at its end the blob sets the stand-in's DISR as the drive
 * would and runs the game's DI interrupt handler, registered with
 * IRQ_Request(21, handler, arg). The alarm's buffer (GC_ALARM_BYTES) holds
 * the handle in its first word and GC_OGC_ALARM_MADE in its second. */
#define GC_OGC_ALARM_MADE 0x52574F41u  /* "RWOA" */

/* gc_on_fatal's exception numbers (GC_EXCEPTION_EARLY, GC_EXCEPTION_STRAY)
 * and the crash screen are in gc_crash.h. */

/* The trace: a ring of 512-byte records in sd:/riftwii/gc/trace.bin
 * (loader-made, its sectors known), written now and then while the game
 * runs, so a run on a Wii that ends on a black screen says how far it
 * got. Big-endian words. */
#define GC_TRACE_MAGIC 0x52575452u  /* "RWTR" */
enum gc_trace_word {
    GC_TR_MAGIC, GC_TR_SEQ, GC_TR_WHERE, GC_TR_TBU, GC_TR_TBL, GC_TR_COMMANDS, GC_TR_READS, GC_TR_READ_BYTES,
    GC_TR_SD_REQUESTS, GC_TR_FAILURES, GC_TR_LAST_ERROR, GC_TR_LAST_OFFSET, GC_TR_PAD_READS, GC_TR_STREAM_PLAYING,
    GC_TR_STREAM_MIXES, GC_TR_STREAM_REFILLS, GC_TR_AI_DMAS, GC_TR_AR_DMAS, GC_TR_LAST_AR_TYPE, GC_TR_LAST_AR_MAIN,
    GC_TR_LAST_AR_ARAM, GC_TR_LAST_AR_LENGTH, GC_TR_PENDING_CB, GC_TR_ALARMS, GC_TR_BEATS, GC_TR_BEAT_PC,
    GC_TR_BEAT_LR, GC_TR_BEAT_MSR, GC_TR_EXCEPTION, GC_TR_DSISR, GC_TR_DAR,
    GC_TR_PI_CAUSE, GC_TR_PI_MASK, GC_TR_HW_IRQ_FLAG, GC_TR_HW_IRQ_MASK,
    GC_TR_CRASH_R1, GC_TR_CRASH_R3, GC_TR_CRASH_R4, GC_TR_CRASH_R5, GC_TR_CRASH_R31, GC_TR_CRASH_CHAIN, /* 4 words */
    GC_TR_CRASH_WORD = GC_TR_CRASH_CHAIN + 4, /* the word in memory (uncached) at SRR0 */
    GC_TR_GUARD_HITS, GC_TR_GUARD_FIRST_REQUEST, GC_TR_GUARD_FIRST_OFFSET, GC_TR_GUARD_FIRST_VALUE,
    GC_TR_BT_STAGE, GC_TR_BT_ERROR, GC_TR_BT_EVENTS, GC_TR_BT_ACL, GC_TR_BT_LAST_EVENT,
    GC_TR_BT_KEYS, /* stored link keys << 16 | connections */
    GC_TR_BT_REMOTES, /* the first remote: chans << 24 | ready << 16 | ext_step << 8 | ext_kind */
    GC_TR_BT_BUTTONS,
    GC_TR_BT_EVENT_LENGTHS,
    GC_TR_BT_EVENT_LOG, /* 8 words: the first four events' first 8 bytes */
    GC_TR_CARD_READS = GC_TR_BT_EVENT_LOG + 8,
    GC_TR_CARD_WRITES, /* page programs << 16 | sector erases */
    GC_TR_CARD_FLUSHES, GC_TR_CARD_DIRTY, GC_TR_CARD_ERROR,
    GC_TR_CARD_LAST, /* the last command << 24 | accesses */
    GC_TR_VI_CONFIGURES,
    GC_TR_VI_MODE, /* the last VIConfigure: the game's viTVMode << 8 | the one it got; 0x10000: the
                    * picture width asked for was kept, the frame buffer being wider (gc_video.h) */
    GC_TR_PROJECTIONS, /* perspective projections seen with 16:9 on (widened unless GC_FLAG_WIDEN is off) */
    GC_TR_STREAM_STARVED, GC_TR_STREAM_FETCH_BUSY,
    GC_TR_STREAM_BUFFERED, /* ADPCM bytes in the ring ahead of the decoder */
    GC_TR_ORTHOGRAPHICS, /* orthographic projections seen with 16:9 on (left alone) */
    GC_TR_AR_WAITS, /* ARAM transfers whose interrupt was waited for (gc_after_ar_dma) */
    GC_TR_AR_WAIT_LONGEST, /* the longest of those waits, timebase ticks */
    GC_TR_AR_WAIT_TIMEOUTS, /* waits given up: the interrupt did not show in time */
    GC_TR_PROJECTION_CALLERS, /* 16 words: gc_rt_context.projection_callers; with the test remote
                               * (GC_FLAG_FAKE_REMOTE), its results instead (gc_bt.h's fake_log) */
    /* An RVZ game (gc_rvz.h): groups decoded, the time spent serving its
     * reads (card and decoding, timebase ticks, wraps), and its last
     * error << 24 | failures. */
    GC_TR_RVZ_GROUPS = GC_TR_PROJECTION_CALLERS + 16,
    GC_TR_RVZ_TICKS,
    GC_TR_RVZ_ERROR,
    GC_TR_BT_PORTS, /* each remote slot's port (gc_ports.h), a byte each from slot 0 in the top byte; 0xFF none */
    GC_TR_PAD_ERRS, /* each port's err as the game got it after the remotes' fill, port 1 in the top byte; 0xFF none */
    GC_TR_BT_SHAKES, /* shakes that pressed their button (gc_controls.h), all remotes */
    GC_TR_BT_PROGRESS, /* how far the Bluetooth got: gc_bt.h's GC_BT_SEEN_* bits */
    GC_TR_BT_LINK, /* the last Connection_Complete status << 24 | Authentication_Complete status << 16 |
                    * disconnect reason << 8 | the extension's last error (0x80 | n: its ID read failed) */
    GC_TR_BT_EXT, /* the last extension ID << 16 | the last report mode << 8 | extensions given up */
    GC_TR_BT_REPORTS, /* data reports from every remote */
    GC_TR_BT_RUMBLE, /* PADControlMotor calls << 16 | motor switches << 8 | each remote slot's motor (bit 4-7: slot 0-3 on)
                      * | the ports the game wants rumbling (bits 0-3) */
    GC_TR_BT_PRO_RANGE, /* 4 words, one per Wii U Pro Controller stick axis (left X, right X, left Y, right Y):
                         * the lowest raw 12-bit value seen << 16 | the highest; 0 when no Pro was seen */
    GC_TR_RESTARTS = GC_TR_BT_PRO_RANGE + 4, /* restarts done (gc_on_hot_reset) */
    GC_TR_RESETS, /* the game's OSResetSystem calls << 16 | Reset button presses seen */
    GC_TR_RESET_LAST, /* the game's last OSResetSystem: reset << 8 | menu */
    /* The GameCube adapter (gc_gcad.h): its link (rtgcad.h's GCAD_LINK_*)
     * << 24 | HID version << 16 | the game ports its controllers filled
     * << 8 | the device id's low byte; its reports << 16 | the requests
     * that could not be sent; its last error. */
    GC_TR_GCAD,
    GC_TR_GCAD_REPORTS,
    GC_TR_GCAD_ERROR,
    /* A game on the USB drive: d2x's reads (low 16 bits) << 16 | the failed
     * ones (low 8 bits) << 8 | the keep-alive reads that kept the drive from
     * spinning down (low 8 bits). One word, as the record is full; each
     * count only grows, so two records still tell how many came between. */
    GC_TR_USB,
    /* The audio hardware as the record found it, for a restart that hangs
     * on a Wii (Dolphin does not model the DSP's state across it). */
    GC_TR_DSP_CSR, /* the DSP's control (0x500A) << 16 | the audio DMA's control (0x5036, bit 15: on) */
    GC_TR_DSP_MAIL, /* the CPU's mail to the DSP, upper half (0x5000) << 16 | the DSP's to the CPU, upper half (0x5004);
                     * bit 15 of each: mail waiting. The lower halves are not read: reading takes the mail. */
    GC_TR_AI_CR, /* the audio interface's control (0xCD006C00 in Wii mode) */
    GC_TR_AR_CONFIG, /* the ARAM interface's size (0x5012) << 16 | its refresh (0x501A) */
    GC_TR_AUDIO_RESET, /* the last restart's audio reset (gc_audio_reset): the DSP's control before << 16 |
                        * the waits that ran out << 12 (GC_AUDIO_*_SLOW) | the DSP's control after (low 12 bits) */
    /* The VI as the record found it, for picture problems only a Wii shows
     * (colours wrong through an HDMI adapter on 18 Oct; Dolphin ignores
     * the clock and the cable bit). */
    GC_TR_VI_DCR_CLOCK, /* the display control (0x02) << 16 | the clock (0x6C; 1: 54 MHz, progressive) */
    GC_TR_VI_SEL_HSW, /* 0x6E (bit 0: a component cable) << 16 | the frame buffer's width and stride (0x48) */
    GC_TR_VI_TFBL, /* the top field's frame buffer register (0x1C) as written */
    GC_TR_WORDS
};
typedef char gc_trace_fits[(GC_TR_WORDS * 4 <= 512) ? 1 : -1];
/* where: what wrote the record */
#define GC_TRACE_READ 1u
#define GC_TRACE_PAD 2u
#define GC_TRACE_AI 3u
#define GC_TRACE_AR 4u
#define GC_TRACE_JUMP 5u
#define GC_TRACE_BEAT 6u
#define GC_TRACE_FATAL 7u   /* the game crashed: beat_pc/lr/msr are the crash's SRR0, LR, SRR1 */
#define GC_TRACE_COMMAND 8u /* a DVDLow command that is not a read */
#define GC_TRACE_LEAVE 9u   /* the player held HOME: ES_LaunchTitle is next */
#define GC_TRACE_RESTART 10u /* the game is restarted (Reset): MEM1 goes back as it was at the jump */
#define GC_TRACE_EXEC 11u   /* the game switches to another program: exec.txt written, the way back next */
/* The last records of the trace go round; the others stay. */
#define GC_TRACE_RING 16u

/* gc_rt_context.vi_shown: the VI's registers 0x00-0x2b (timing, frame
 * buffers) as eleven words, then 0x48 (the fetch width and scaling) and
 * 0x6c (the clock), each word two 16-bit registers. */
#define GC_VI_SAVED_WORDS 13u
/* gc_rt_context.vi_at_jump: the VI's registers 0x00-0x77. */
#define GC_VI_HANDOVER_WORDS 30u

/* gc_rt_context.flags */
#define GC_FLAG_PRESS 1u  /* tests without a controller: port 1 presses A, B and Start in turn */
#define GC_FLAG_CRASH 4u  /* tests of the crash screen: the 600th pad read takes a program exception */
#define GC_FLAG_LEAVE 2u  /* tests of the way back: port 1 holds L, R, Z and Start from the 1800th pad read;
                           * with GC_FLAG_CRASH, the crash screen takes A after twenty seconds */
#define GC_FLAG_REMOTE_RUMBLE 256u  /* Settings' Wii Remote rumble (and the game's own) is on: PADControlMotor rumbles a remote */
#define GC_FLAG_RUMBLE_TEST 512u  /* tests in Dolphin (with the test remote): the game's own PADControlMotor is called
                                   * for the test remote's port, rumble then stop, every 240 pad reads */
#define GC_FLAG_FAKE_REMOTE 2048u  /* tests in Dolphin: a made-up Wii Remote instead of the Bluetooth (gc_bt_fake) */
#define GC_FLAG_FAKE_PRO 1024u  /* with GC_FLAG_FAKE_REMOTE: the made-up controller is a Wii U Pro Controller */
#define GC_FLAG_CRASH_BLACK 64u  /* with GC_FLAG_CRASH: the screen is blanked first, as VISetBlack does (no active lines) */
#define GC_FLAG_STREAM_ONLY 32u  /* tests: AI buffers that carry disc music carry it alone (the game's own sound cleared) */
#define GC_FLAG_PROGRESSIVE 8u  /* 480p: VIConfigure's NTSC interlaced modes become progressive (gc_on_vi_configure) */
#define GC_FLAG_RESET_TEST 128u  /* tests of the restart: a Reset press at the 1500th pad read and at the 3600th */
#define GC_FLAG_EXEC_TEST 4096u  /* tests of a program switch (gc_on_boot_dol) in a game without one: at the 1500th
                                  * pad read, to the disc's own program with one argument, "rwtest" */
#define GC_FLAG_WIDEN 16u  /* 16:9: perspective projections are drawn 3/4 as wide (gc_on_projection); off with the
                            * hook on, the test of the hook alone (sd:/riftwii/gc/test/nowiden): copied, not widened */

/* The callers of GXSetProjection that gc_on_projection notes for the
 * trace, the first ones seen: four words each, the return address, the
 * perspective calls from there, and that first matrix's [0][0] and
 * [1][1] (float bits, as the game gave them: their ratio is the shape of
 * the picture it draws, 0.75 for one 4:3 wide). */
#define GC_PROJECTION_CALLERS 4u

/* Disc audio streaming (the drive's own ADPCM, 48 kHz stereo): what the
 * drive would have played into the AI is decoded here and mixed into the
 * game's audio as it goes out. In MEM1 beside the blob (loader-placed).
 *
 * The ADPCM comes off the card into a ring ahead of the decoder. The card
 * is read only from the game's own context (its PADRead every frame, its
 * disc reads, the stream's start), never from the AI interrupt: on a Wii
 * a read takes milliseconds, and in the interrupt it held up the game's
 * next audio buffer and was skipped whenever a disc read, a trace write
 * or the Wii Remote driver had the card (the music crackled). The two
 * sides each own one counter: `fetched` (the card side) and `consumed`
 * (the decoder), so neither waits for the other. */
#define GC_STREAM_CHUNK 0x2000u  /* the most one top-up reads: about 0.15 s of music */
struct gc_rt_stream {
    uint32_t playing;         /* a stream is set (AudioStream with a length) */
    uint32_t start;           /* the stream being read ahead, disc bytes */
    uint32_t length;
    uint32_t pos;             /* the next disc byte to read into the ring */
    uint32_t next_start;      /* read after this one ends (the drive's queue); 0 length: stop */
    uint32_t next_length;
    uint32_t buf;             /* the ring, 32-byte aligned (loader-placed) */
    uint32_t buf_bytes;       /* its size, a multiple of GC_STREAM_CHUNK */
    uint32_t fetched;         /* bytes put into the ring so far (the card side only) */
    uint32_t consumed;        /* bytes decoded from it so far (the decoder only) */
    int32_t hist[4];          /* left hist1, hist2, right hist1, hist2 */
    int16_t left[28];
    int16_t right[28];
    uint32_t sample;          /* the next of left/right to use, 28: decode another block */
    uint32_t phase;           /* 48 to 32 kHz: two of every three samples */
    uint32_t mixes;
    uint32_t refills;
    uint32_t starved;         /* AI buffers the ring ran dry in (part of their music missing) */
    uint32_t fetch_busy;      /* top-ups put off because the card was in use */
    uint32_t ended;           /* the last stream is all in the ring: playing stops when it is decoded */
    uint32_t pad[1];
};

/* The restart (Reset): MEM1 as the loader left it at the jump, kept in
 * MEM2 (riftwii/gcreset.hpp's plan_gc_restart), put back region by region
 * in order. `source` 0: the region is zeroed. */
struct gc_restart_region {
    uint32_t address;
    uint32_t length;
    uint32_t source;
};
#define GC_RESTART_REGIONS 24u

/* OSResetSystem's `reset`. */
#define GC_OS_RESET_RESTART 0u
#define GC_OS_RESET_HOTRESET 1u
#define GC_OS_RESET_SHUTDOWN 2u

/* Luigi's Mansion's loads remembered with the heap they were queued from. */
#define GC_LOAD_SLOTS 8u

/* The OSAlarm the callbacks run from: the SDK's is 0x28 bytes. */
#define GC_ALARM_BYTES 0x40u
#define GC_IPC_TIMEOUT 50000000u  /* polls of the IPC registers before giving up */

struct gc_rt_context {
    uint32_t magic;
    /* Loader-filled. */
    uint32_t sdio_fd;         /* /dev/sdio/slot0, the card selected */
    uint32_t sdhc;            /* 1: CMD18 takes a sector, 0: a byte offset */
    uint32_t runs;            /* struct gc_rt_run[run_count] */
    uint32_t run_count;
    uint32_t set_alarm;       /* the game's OSSetAlarm, or libogc's SYS_SetAlarm when ogc_create_alarm is set */
    uint32_t alarm_entry;     /* this blob's alarm handler */
    uint32_t ogc_create_alarm; /* libogc's SYS_CreateAlarm, 0: the SDK's OSSetAlarm is used */
    uint32_t ogc_alarm_entry; /* this blob's libogc alarm callback */
    uint32_t di_handler;      /* libogc: the game's DI interrupt handler (gc_on_di_start), 0: DVDLow hooks */
    uint32_t bounce;          /* 32-byte aligned, bounce_bytes (a multiple of 512) */
    uint32_t bounce_bytes;
    uint32_t delay_ticks;     /* how soon the callback runs */
    uint32_t drive_timing;    /* 1: a read's callback waits as long as a GameCube drive would take (gc_drive_ticks) */
    /* State. */
    uint32_t last_read_end;   /* where the last read ended on the disc, for the seek */
    uint32_t pending_cb;      /* the callback the alarm runs, 0: none */
    uint32_t pending_type;
    /* The read under way (gc_on_read): the alarm copies it from the SD
     * card a piece at a time, after the game's DVDLowRead has returned. */
    uint32_t read_state;      /* GC_READ_* */
    uint32_t read_addr;       /* where the rest goes */
    uint32_t read_length;     /* what is still to copy */
    uint32_t read_offset;     /* the rest's place on the disc */
    uint32_t read_cb;         /* the game's callback once all of it is there */
    uint32_t read_failed;     /* 1: a piece could not be read (the callback gets an error) */
    uint32_t read_started;    /* timebase when the game asked */
    uint32_t read_drive_ticks; /* how long a GameCube drive takes for it; 0: Fast */
    uint32_t alarm_queued;    /* 1: `alarm` is in the OS's queue (gc_arm) */
    /* Luigi's Mansion's file loader (gc_on_load_queued and on), loader-found. */
    uint32_t load_post;       /* post(request): hands the request to the loading thread */
    uint32_t load_group;      /* old = group(id): the heaps' group ID for what is allocated next */
    uint32_t load_alloc;      /* alloc(length, alignment) from the current heap */
    uint32_t loads_steered;   /* buffers allocated from the heap their load was queued from */
    uint32_t load_heap_offset; /* JKRHeap's current heap: its offset from r13 (the game's small data) */
    uint32_t load_saved_heap; /* the temporary heap the callbacks interrupted; 0: no steering */
    uint32_t load_next;       /* the next slot of load_request/load_heap to take */
    uint32_t load_request[GC_LOAD_SLOTS]; /* queued requests (they come from a pool, reused) */
    uint32_t load_heap[GC_LOAD_SLOTS];    /* and the heap current when each was queued */
    /* Counters, for the log and a memory dump. */
    uint32_t commands;
    uint32_t reads;
    uint32_t read_bytes;
    uint32_t sd_requests;
    uint32_t failures;
    int32_t last_error;
    uint32_t last_offset;
    uint32_t di_regs;         /* where the game's DI register reads were sent (its 0xCC006000) */
    uint32_t stream;          /* struct gc_rt_stream */
    uint32_t flags;           /* GC_FLAG_* */
    uint32_t pad_reads;
    uint32_t trace_sector;    /* the trace's first sector, 0: no trace */
    uint32_t trace_count;     /* its records */
    uint32_t trace_buf;       /* 512 bytes, 32-byte aligned */
    uint32_t trace_seq;
    uint32_t card_rca;        /* for CMD13 after a write */
    uint32_t ai_dmas;
    uint32_t ar_dmas;
    uint32_t alarms;
    uint32_t last_ar[4];
    uint32_t busy;            /* the IPC buffers are in use (a read or a trace write) */
    uint32_t xfb;             /* the frame on screen at the jump (YUYV), turned blue by a crash; 0: none */
    uint32_t xfb_bytes;
    /* The heartbeat: an OSAlarm of its own every half second once the
     * game reads, noting where the game was when it went off (the
     * decrementer interrupt's saved context), for a run that hangs. */
    uint32_t beat_ticks;      /* loader-filled, 0: no heartbeat */
    uint32_t beats;
    uint32_t beat_pc;
    uint32_t beat_lr;
    uint32_t beat_msr;
    uint32_t exception;       /* set by gc_on_fatal */
    uint32_t dsisr;
    uint32_t dar;
    uint32_t crash_gpr[5];    /* r1, r3, r4, r5, r31 */
    uint32_t crash_chain[4];  /* the saved LRs up the stack: who called whom */
    uint32_t crash_word;
    /* The guard: IOS keeps live globals at 0x80003100-0x800031ff on a Wii
     * and writes them while it works (a counter at 0x800031a0 at least),
     * but a GameCube game has code there. The loader's copy of the game's
     * bytes is compared after every IOS request and written back. */
    uint32_t guard_addr;      /* loader-filled, 0: none */
    uint32_t guard_hits;      /* words found changed and put back */
    uint32_t guard_first_request; /* sd_requests when the first was found */
    uint32_t guard_first_offset;
    uint32_t guard_first_value;
    uint32_t guard_copy[64];
    uint32_t bt;              /* struct gc_bt (loader-placed), 0: no Wii Remotes */
    uint32_t card;            /* struct gc_card (loader-placed), 0: the real slot A */
    uint32_t gcad;            /* struct gc_gcad (loader-placed, gc_gcad.h), 0: no GameCube adapter */
    /* A game on the USB drive: its GC_RUN_USB runs (and an RVZ's pieces)
     * are read through d2x's /dev/usb2, which the loader opened under the
     * d2x cIOS the game runs on. d2x's USB driver writes only to MEM2, so
     * they go through the bounce buffer. */
    uint32_t usb_fd;          /* 0xFFFFFFFF: the game is not on USB */
    uint32_t usb_keep_sector; /* a sector of the image, read when the drive has been quiet */
    uint32_t usb_keep_ticks;  /* timebase ticks of quiet before that read, 0: never */
    uint32_t usb_last_read;   /* timebase of the last read from the drive */
    uint32_t usb_requests;
    uint32_t usb_failures;
    uint32_t usb_keepalives;
    /* An RVZ game (the blob's RVZ build): the disc's GC_RUN_RVZ runs are
     * read from it. */
    uint32_t rvz;             /* struct gc_rvz (loader-placed), 0: an ISO */
    uint32_t rvz_ticks;       /* timebase ticks spent in its reads (card and decoding), wraps */
    uint32_t test_motor;      /* GC_FLAG_RUMBLE_TEST: the game's PADControlMotor (hooked), 0: not found */
    /* The way back to RiftWii: holding HOME (or L, R, Z and Start) for
     * leave_hold ticks launches the title in leave_buf through /dev/es. */
    uint32_t leave_fd;        /* /dev/es, opened by the loader; 0xFFFFFFFF: no way back */
    uint32_t leave_buf;       /* 32-byte aligned: the title ID at 0, its ticket view (0xD8 bytes) at 0x20 */
    uint32_t leave_hold;      /* timebase ticks */
    uint32_t leave_since;     /* when the hold began (low bit set), 0: not held */
    uint32_t crash_card;      /* the crash screen's card and strip, GC_CRASH_SCREEN_BYTES (loader-placed), 0: none */
    uint32_t leave_card_activity; /* the memory card's activity at the last pad read */
    uint32_t leave_card_quiet;    /* since when the card has had no access (timebase) */
    uint32_t ipc_no_timeout;      /* 1: the request being sent waits for its reply however long (the launch) */
    uint32_t crash_a_was_down;    /* the crash screen: A was down at the last look */
    /* The VI's registers the last time they showed a picture (gc_vi_note):
     * RiftWii's own frame at the jump, then the game's at each heartbeat.
     * A crash puts them back when the screen it finds is blanked, as the
     * SDK keeps it from VIInit until the game's first frame and while it
     * is black (VISetBlack), so the crash screen is seen. */
    uint32_t vi_shown[GC_VI_SAVED_WORDS];
    /* The picture options (the game's Options: 480p, 16:9). */
    uint32_t vi_configures;
    uint32_t vi_mode;         /* GC_TR_VI_MODE */
    uint32_t projections;     /* perspective projections seen (16:9 on) */
    uint32_t orthographics;   /* orthographic ones, left alone */
    /* gc_after_ar_dma: each ARAM interrupt made pending before the game
     * goes on. */
    uint32_t ar_waits;
    uint32_t ar_wait_longest;
    uint32_t ar_wait_timeouts;
    uint32_t vi_width;        /* Picture width (gc_video.h's vi_width): 0 the game's own */
    uint32_t vi_copy[16];     /* the copy of the game's GXRenderModeObj VIConfigure gets (0x3C bytes, gc_video.h) */
    uint32_t projection[16];  /* the widened copy of the game's projection matrix */
    uint32_t projection_callers[GC_PROJECTION_CALLERS * 4];
    /* The restart (Reset, gc_on_hot_reset), loader-filled: the game's
     * entry, the regions to put back (struct gc_restart_region in MEM2,
     * 0: no restart) and the game's OSResetSystem. */
    uint32_t entry;
    uint32_t restart_regions;
    uint32_t restart_count;
    uint32_t restart_stack;       /* the stack the putting back runs on (MEM2): the game's own is in what is put back */
    uint32_t reset_system;
    uint32_t restarts;
    uint32_t resets_asked;        /* the game's OSResetSystem calls */
    uint32_t reset_last;          /* GC_TR_RESET_LAST */
    /* The Reset button as gc_reset_check sees it at each pad read. */
    uint32_t reset_presses;
    uint32_t reset_down_since;    /* when it went down (low bit set), 0: up */
    uint32_t reset_request_at;    /* when a press ended (low bit set), 0: none waiting */
    uint32_t reset_asked_before;  /* resets_asked then: the game's own call answers the press */
    /* The hand-over to the next title (gc_leave): the VI's registers as
     * RiftWii left them at the jump, the game's TV format interlaced as
     * libogc set it up, noted by gc_on_trace_now and put back blanked
     * (gc_vi_hand_over). Two 16-bit registers a word, from 0x00. */
    uint32_t vi_at_jump[GC_VI_HANDOVER_WORDS];
    /* The restart's audio reset (gc_audio_reset): the ARAM interface's
     * size (0x5012) << 16 | refresh (0x501A) and the audio interface's
     * control as they were at the jump, which the first start had. */
    uint32_t ar_config_at_jump;
    uint32_t ai_cr_at_jump;
    uint32_t audio_reset;         /* GC_TR_AUDIO_RESET */
    /* A switch to another program on the disc (gc_on_boot_dol): the one
     * sector of sd:/riftwii/gc/exec.txt, loader-made; 0: switches run as
     * the game makes them. */
    uint32_t exec_sector;
    uint8_t aram_scratch[32] __attribute__((aligned(32)));  /* the hardware's own ARAM DMA goes here */
    uint8_t alarm[GC_ALARM_BYTES] __attribute__((aligned(32)));
    uint8_t beat_alarm[GC_ALARM_BYTES] __attribute__((aligned(32)));
    uint32_t ipc[16] __attribute__((aligned(32)));      /* the IPC request */
    uint32_t vec[8] __attribute__((aligned(32)));       /* three vectors */
    uint32_t request[16] __attribute__((aligned(32)));  /* the SENDCMD block (36 bytes) */
    uint32_t response[8] __attribute__((aligned(32)));
    /* The callback heap policy's four r13 displacements, loader-found:
     * the game's two main heaps, then JKRHeap's system and root heaps.
     * Kept at the end so existing trace/context offsets stay unchanged. */
    uint32_t load_root_heap_offsets[GC_LOAD_ROOT_HEAPS];
};

/* It fits the room gc_entry.S keeps (gc_layout.h); the rest stays zero. */
typedef char gc_rt_context_size[(sizeof(struct gc_rt_context) <= GC_RT_CONTEXT_BYTES) ? 1 : -1];

#ifdef RT_TARGET_PPC
uint32_t gc_on_read(struct gc_rt_context* ctx, uint32_t addr, uint32_t length, uint32_t offset, uint32_t cb);
uint32_t gc_on_load_queued(struct gc_rt_context* ctx, uint32_t request);
uint32_t gc_on_load_alloc(struct gc_rt_context* ctx, uint32_t length, uint32_t alignment, uint32_t request);
uint32_t gc_on_load_callbacks(struct gc_rt_context* ctx, uint32_t group, uint32_t unused, uint32_t request);
uint32_t gc_on_load_callbacks_done(struct gc_rt_context* ctx, uint32_t group);
uint32_t gc_on_disk_id(struct gc_rt_context* ctx, uint32_t id, uint32_t cb);
uint32_t gc_on_inquiry(struct gc_rt_context* ctx, uint32_t info, uint32_t cb);
uint32_t gc_on_done(struct gc_rt_context* ctx, uint32_t cb);
uint32_t gc_on_audio_status(struct gc_rt_context* ctx, uint32_t sub, uint32_t cb);
uint32_t gc_on_audio_stream(struct gc_rt_context* ctx, uint32_t sub, uint32_t length, uint32_t offset, uint32_t cb);
void gc_on_trace_now(struct gc_rt_context* ctx, uint32_t where);
uint32_t gc_on_ar_dma(struct gc_rt_context* ctx, uint32_t type, uint32_t main, uint32_t aram, uint32_t length);
void gc_after_ar_dma(struct gc_rt_context* ctx);
uint32_t gc_on_pad_read(struct gc_rt_context* ctx, uint32_t status, uint32_t result);
void gc_on_pad_motor(struct gc_rt_context* ctx, uint32_t chan, uint32_t command);
uint32_t gc_on_vi_configure(struct gc_rt_context* ctx, uint32_t mode);
void gc_on_reset_system(struct gc_rt_context* ctx, uint32_t* args);
void gc_on_hot_reset(struct gc_rt_context* ctx, uint32_t code);
void gc_on_boot_dol(struct gc_rt_context* ctx, uint32_t* args);
uint32_t gc_on_projection(struct gc_rt_context* ctx, uint32_t mtx, uint32_t type, uint32_t caller);
void gc_on_ai_dma(struct gc_rt_context* ctx, uint32_t start, uint32_t length);
void gc_on_alarm(struct gc_rt_context* ctx, uint32_t alarm, uint32_t os_context);
void gc_on_di_start(struct gc_rt_context* ctx);
/* Sets `alarm` (GC_ALARM_BYTES) to call the blob `ticks` from now, through
 * OSSetAlarm or libogc's alarms. Shared with gc_card.c. */
void gc_set_alarm(struct gc_rt_context* ctx, void* alarm, uint32_t ticks);
void gc_on_fatal(struct gc_rt_context* ctx, uint32_t exception, uint32_t os_context, uint32_t dsisr, uint32_t dar);
void gc_on_stray(struct gc_rt_context* ctx, uint32_t os_context, uint32_t ctr);
/* Shared with gc_bt.c. */
void gc_flush(uint32_t address, uint32_t length);
void gc_invalidate(uint32_t address, uint32_t length);
void gc_guard(struct gc_rt_context* ctx);
uint32_t gc_irq_off(void);
void gc_irq_restore(uint32_t msr);
/* A request to IOS (its physical address), sent and taken; the replies
 * that come meanwhile go to gc_ipc_route. 0, or -1000 when IOS takes
 * nothing. */
int32_t gc_ipc_send(uint32_t phys, struct gc_rt_context* ctx);
/* A reply nobody is waiting for: the Wii Remotes' reads (gc_bt.c) or the
 * GameCube adapter's requests (gc_gcad.c); anything else (left from the
 * menu) is dropped. */
void gc_ipc_route(struct gc_rt_context* ctx, uint32_t reply);
int32_t gc_sd_write(struct gc_rt_context* ctx, uint32_t sector, uint32_t count, uint32_t buf);
int gc_sd_read(struct gc_rt_context* ctx, uint32_t sector, uint32_t count, uint32_t target);
uint32_t gc_ipc_take(void);
#endif

#endif
