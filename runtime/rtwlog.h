/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Wii Remote log: what the game's own Bluetooth and Wii Remote
 * libraries (WUD, WPAD, BTE) would print in a debug build, kept by the
 * runtime/wlog blob and saved to /shared2/riftwii/wpadlog.bin when the
 * game leaves through the HOME Menu. Retail games compile those prints
 * into empty functions; the blob takes their place. RiftWii turns the
 * file into sd:/riftwii/wpadlog.txt at its next start
 * (riftwii/wpadlog.hpp).
 *
 * The first RTWLOG_FIRST messages are kept, then the last RTWLOG_LAST in
 * a ring, so both the connection and the end are there. A message the
 * same as the one before (format and numbers) only counts a repeat. */
#ifndef RIFTWII_RTWLOG_H
#define RIFTWII_RTWLOG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RTWLOG_MAGIC 0x5257574Cu   /* "RWWL" */
#define RTWLOG_VERSION 2u           /* 2: the Pro Controller probe's state in the header */
/* rtwlog_file.probe: what the loader did to the Wii Remote library's
 * firmware probe (riftwii/wpadlog.hpp, settings.txt "wpad_probe"). */
#define RTWLOG_PROBE_NOT_ASKED 0u   /* the test is off */
#define RTWLOG_PROBE_CHANGED 1u     /* reads 0x0016, at probe_at */
#define RTWLOG_PROBE_NOT_FOUND 2u   /* the game's code did not match */
#define RTWLOG_PROBE_PROTECTED 3u   /* the game checks its own code (MetaFortress) */
#define RTWLOG_FIRST 320u
#define RTWLOG_LAST 128u
#define RTWLOG_ENTRIES (RTWLOG_FIRST + RTWLOG_LAST)
#define RTWLOG_FMT_BYTES 64u       /* the format, cut short (NUL-terminated) */
#define RTWLOG_ARGS 7u             /* r4-r10: the integer and pointer arguments */
#define RTWLOG_STRINGS 2u          /* %s arguments copied while they still exist */
#define RTWLOG_STRING_BYTES 24u

/* One message: big-endian, as the Wii wrote it. 152 bytes. */
struct rtwlog_entry {
    uint32_t tick;                 /* the time base / 256 */
    uint32_t repeat;               /* the same message again this many times */
    uint32_t format;               /* where the format was in the game (to spot repeats) */
    uint32_t args[RTWLOG_ARGS];
    char text[RTWLOG_FMT_BYTES];
    char strings[RTWLOG_STRINGS][RTWLOG_STRING_BYTES];
};

/* The file. */
struct rtwlog_file {
    uint32_t magic;
    uint32_t version;
    char game[8];                  /* 0x80000000: ID, disc number, version */
    uint32_t ticks_per_second;     /* the time base's (divide by 256 for `tick`) */
    uint32_t total;                /* messages seen (repeats not counted) */
    uint32_t stubs;                /* print functions the loader hooked */
    uint32_t saves;                /* times the file was written this run */
    uint32_t probe;                /* RTWLOG_PROBE_* */
    uint32_t probe_at;             /* the probe's instruction, when changed */
    struct rtwlog_entry entries[RTWLOG_ENTRIES];
};

/* Where message number `n` (from 0) is kept. */
static inline uint32_t rtwlog_slot(uint32_t n) {
    return n < RTWLOG_FIRST ? n : RTWLOG_FIRST + (n - RTWLOG_FIRST) % RTWLOG_LAST;
}

#ifdef __cplusplus
}
#endif

#endif
