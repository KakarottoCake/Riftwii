/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The way back from a Wii game by a GameCube controller (#224): L, R, Z
 * and Start held, as the GameCube games' way back (runtime/gc), leave the
 * game once no save has been written for a moment. The decisions, kept
 * free of the hardware so the host tests can run them; the pad blob
 * (runtime/pad) calls them from its PADRead and IPC submit hooks. */
#ifndef RIFTWII_RTLEAVE_H
#define RIFTWII_RTLEAVE_H

#include <stdint.h>

#define RTLEAVE_BUTTONS 0x1070u  /* Start, L, R, Z (PADStatus button bits) */
#define RTLEAVE_PADS 4u
#define RTLEAVE_STATUS_BYTES 12u /* PADStatus: buttons, sticks, triggers, analog A/B, err, pad */

/* All times are the time base's low word. */
struct rtleave {
    uint32_t hold;      /* ticks the combo is held (loader-filled) */
    uint32_t quiet;     /* ticks a save must have been quiet (loader-filled) */
    uint32_t since;     /* when the hold began (low bit set), 0: not held */
    uint32_t activity;  /* the last save activity (low bit set), 0: none yet */
    /* The game's save requests while they are with IOS: each its block's
     * address with the command in the low bits (the SDK's blocks are 32-byte
     * aligned), 0 for a free slot; and when it was handed over. */
    uint32_t pending[4];
    uint32_t pending_since[4];
    /* Save requests that found no free slot (low bit set), 0: none. A save
     * can keep dozens in flight (Brawl's first, in Dolphin); they are over
     * once the newest save request has its reply: IOS's file system answers
     * in the order it is asked. Else after the cap. */
    uint32_t overflow;
    uint32_t newest;        /* the newest save request, as a slot holds it; 1: one that can't be read back; 0: none or answered */
};
#define RTLEAVE_PENDING_SLOTS 4u

/* A save request counts as with IOS for at most this many quiet windows
 * (10 s with the loader's second): request blocks are reused, so a block
 * that never shows its reply only delays the way back, never blocks it. */
#define RTLEAVE_PENDING_QUIETS 10u

/* Whether a pad in any of the four ports (no error) holds the combo. */
int rtleave_held(const uint8_t* status);

/* Whether the SDK's IPC request `block` (as its submit routine gets it:
 * the command, the fd, then the arguments, buffers as physical addresses)
 * writes a save: a write, a NAND file opened for writing, or a /dev/fs
 * change. `arg0` and `arg1` are the block's first two arguments as
 * memory the caller can read (the path or buffer, then the ioctl's
 * input), 0 where they are no address. Writes other than a save's only
 * make the hold wait longer. */
int rtleave_save_request(const uint32_t* block, const uint8_t* arg0, const uint8_t* arg1);

/* A save was written at `now`. */
void rtleave_note_save(struct rtleave* l, uint32_t now);

/* The save request whose block (its first word, the command) is at
 * `block` went to IOS at `now` with `command` there. IOS's reply replaces
 * that word in memory (with 8, the original command moving into the fd's
 * place); the pad hook reads it back uncached. A block not
 * 32-byte aligned, a command above 7 or no free slot: counted as
 * `overflow` instead, so it still keeps the way back waiting. */
void rtleave_note_pending(struct rtleave* l, uint32_t now, uint32_t block, uint32_t command);

/* The block address a pending slot's `entry` waits on (0: a free slot). */
#define RTLEAVE_PENDING_BLOCK(entry) ((entry) & ~31u)

/* Whether any of the game's save requests is still with IOS, given each
 * slot's command word as it is in memory now (`words`, one per slot,
 * anything for a free one), then the newest request's (`words` has
 * RTLEAVE_PENDING_SLOTS + 1). A slot whose word has changed (the reply, or
 * the block reused) or whose RTLEAVE_PENDING_QUIETS windows have passed is
 * over: freed, and noted as save activity at `now`, so the quiet second
 * counts from its end. `overflow` ends the same way when the newest
 * request's word has changed, or at its cap. */
int rtleave_pending(struct rtleave* l, uint32_t now, const uint32_t* words);

/* The resident runtime's `wrote` while a save write's command is with IOS
 * (its RT_FS_WRITE_WITH_IOS): never quiet then. 0: no write yet; 1: one
 * has gone out, and `resident_last_write` is its last step. */
#define RTLEAVE_RESIDENT_WITH_IOS 2

/* 1 when no save was written within `quiet` ticks of `now`, neither
 * through the IPC calls nor by the resident runtime (`resident_wrote`,
 * then its `resident_last_write`), and no save is with IOS right now:
 * neither the resident's nor the game's own pending request (checked
 * with rtleave_pending just before). */
int rtleave_quiet(const struct rtleave* l, uint32_t now, int resident_wrote, uint32_t resident_last_write);

/* At each pad read: 1 when the combo has been held `hold` ticks and no
 * save was written within `quiet` ticks, neither through the IPC calls
 * nor, with saves on the SD card, by the resident runtime
 * (`resident_wrote`, then its `resident_last_write`). While a save holds
 * it back the hold keeps counting; letting go starts it over. */
int rtleave_due(struct rtleave* l, int held, uint32_t now, int resident_wrote, uint32_t resident_last_write);

#endif
