/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A memory card in slot A for a GameCube game in Wii mode (gc_card.c),
 * kept in a file on the SD card.
 *
 * The game's EXI functions (the bus the card hangs on) are hooked: for
 * channel 0, device 0 the blob answers as a card would, from an image
 * of the whole card in MEM2; the other devices (the clock and SRAM on
 * channel 0, slot B, the serial port) still go to the hardware. What the
 * game programs or erases goes back to the card file once the card has
 * been quiet for a moment, from the heartbeat.
 *
 * The card's side of the bus, from public documentation (no code taken):
 * the commands 0x52 (read), 0xF1 (erase sector), 0xF2 (program page),
 * 0xF4 (erase all), 0x83 (status), 0x89 (clear status), 0x81 (interrupt
 * on or off), the four address bytes, the ID's size and sector bits, a
 * status of 0x41 (ready, unlocked) so the game skips the unlock (Yet
 * Another GameCube Documentation; Dolphin's memory card device; libogc's
 * card.c, as a reader of the same card). The EXI control block's layout
 * and the SDK's function order come from reading the game's own code. */
#ifndef RIFTWII_GC_CARD_H
#define RIFTWII_GC_CARD_H

#include <stdint.h>

/* The hooked EXI functions, in the blob header's table. */
enum gc_exi_fn {
    GC_EXI_IMM,       /* EXIImm(chan, buf, len, type, callback) */
    GC_EXI_DMA,       /* EXIDma(chan, buf, len, type, callback) */
    GC_EXI_SYNC,      /* EXISync(chan) */
    GC_EXI_SELECT,    /* EXISelect(chan, dev, freq) */
    GC_EXI_DESELECT,  /* EXIDeselect(chan) */
    GC_EXI_LOCK,      /* EXILock(chan, dev, unlocked) */
    GC_EXI_UNLOCK,    /* EXIUnlock(chan) */
    GC_EXI_PROBE,     /* EXIProbe(chan) */
    GC_EXI_PROBE_EX,  /* EXIProbeEx(chan) */
    GC_EXI_ATTACH,    /* EXIAttach(chan, ext) */
    GC_EXI_GET_ID,    /* EXIGetID(chan, dev, id) */
    GC_EXI_HOOKS
};

/* 251 blocks (16 Mbit), 8 KB sectors, the shortest read latency. */
#define GC_CARD_ID 0x00000010u
#define GC_CARD_BYTES 0x00200000u
#define GC_CARD_SECTOR 0x2000u
#define GC_CARD_FILE_SECTORS (GC_CARD_BYTES / 512u)
#define GC_CARD_RUNS 32u
#define GC_CARD_IMAGE 0x91000000u  /* MEM2, cached, after the ARAM (gc_rt.c) */

/* A stretch of the card file: card bytes [card, card + count * 512) are
 * SD sectors [sd, sd + count). */
struct gc_card_run {
    uint32_t card_sector;
    uint32_t sd_sector;
    uint32_t count;
};

struct gc_card {
    /* Loader-filled. */
    /* Code, not data: "mflr r0", then "bl" to the blob's gc_stray_entry.
     * Wind Waker and Metroid Prime (USA) were seen executing this struct
     * from its first word in Dolphin, which ran its fields as instructions
     * and ended in a recursion of the game's own exception handler; landing
     * here instead shows the crash screen with the jumper's LR and CTR. */
    uint32_t landing[2];
    uint32_t image;         /* the card, GC_CARD_BYTES */
    uint32_t ecb;           /* the game's EXI control blocks: channel 0's EXI callback first */
    uint32_t run_count;
    struct gc_card_run runs[GC_CARD_RUNS];
    /* The card's state. */
    uint32_t selected;
    uint32_t locked;
    uint32_t irq_on;
    uint8_t cmd[8];         /* the command's first bytes */
    uint32_t count;         /* bytes the game sent since the select */
    uint32_t address;       /* the next byte read or programmed */
    /* Callbacks, run from an OSAlarm as the EXI interrupt would. */
    uint32_t tc_callback;   /* the DMA's */
    uint32_t exi_pending;   /* a program or erase finished: the game's EXI callback */
    uint32_t alarm_set;
    uint8_t alarm[0x40] __attribute__((aligned(32)));
    /* Bytes not yet in the card file, a bit per 512. */
    uint8_t dirty[GC_CARD_FILE_SECTORS / 8];
    uint32_t dirty_count;
    uint32_t activity;      /* bumped on every access */
    uint32_t seen_activity; /* activity at the last heartbeat */
    /* For the trace. */
    uint32_t reads;
    uint32_t programs;
    uint32_t erases;
    uint32_t flushes;
    int32_t flush_error;
    uint32_t last_cmd;
};

#ifdef RT_TARGET_PPC
struct gc_rt_context;
/* A hooked EXI call: *pass set when the original should run instead. */
uint32_t gc_on_exi(struct gc_rt_context* ctx, uint32_t fn, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                   uint32_t a4, uint32_t* pass);
void gc_card_on_alarm(struct gc_rt_context* ctx, struct gc_card* card, uint32_t os_context);
/* From the heartbeat: what was written, back to the card file once quiet. */
/* Writes the image's changed sectors back to the card file. Without
 * `force` only once a heartbeat has seen no card access (the game is not
 * in the middle of a save); with it at once (leaving the game). */
void gc_card_flush(struct gc_rt_context* ctx, struct gc_card* card, int force);
#endif

#endif
