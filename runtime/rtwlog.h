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
 * same as the one before (format and numbers) only counts a repeat.
 *
 * Since version 3 the blob also sits in the game's IOS_IoctlvAsync, in
 * its traffic with the Bluetooth dongle: it runs the Wii U Pro Controller
 * translator (runtime/rtpro.h) and keeps a trace of the HCI commands and
 * events and of the HID reports both ways (the first RTWLOG_BT_FIRST, then
 * the last RTWLOG_BT_LAST), which says who ends a connection and why. Data
 * reports past the first few are only counted. */
#ifndef RIFTWII_RTWLOG_H
#define RIFTWII_RTWLOG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RTWLOG_MAGIC 0x5257574Cu   /* "RWWL" */
#define RTWLOG_VERSION 3u           /* 2: the Pro Controller probe's state; 3: the Bluetooth trace */
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

/* The Bluetooth trace. */
#define RTWLOG_BT_FIRST 384u
#define RTWLOG_BT_LAST 128u
#define RTWLOG_BT_ENTRIES (RTWLOG_BT_FIRST + RTWLOG_BT_LAST)
#define RTWLOG_BT_BYTES 16u        /* of each packet, from its start (HCI) or its L2CAP payload (ACL) */
#define RTWLOG_BT_DATA_KEPT 24u    /* data reports kept each way before they are only counted */
/* rtwlog_bt.kind */
#define RTWLOG_BT_COMMAND 1u       /* HCI command to the dongle: opcode, length, parameters */
#define RTWLOG_BT_EVENT 2u         /* HCI event from it: code, length, parameters */
#define RTWLOG_BT_OUT 3u           /* ACL to a controller, as sent (after the translator) */
#define RTWLOG_BT_IN 4u            /* ACL from a controller, as it came */
#define RTWLOG_BT_GAME 5u          /* the same, as the translator handed it to the game */

/* One packet: 28 bytes. `handle` is the ACL connection's (HCI: 0);
 * `size` the packet's (ACL: its L2CAP payload's); `cid` the L2CAP
 * channel; `flags` the translator's RTPRO_* bits. */
struct rtwlog_bt {
    uint32_t tick;
    uint8_t kind;
    uint8_t flags;
    uint16_t handle;
    uint16_t size;
    uint16_t cid;
    uint8_t data[RTWLOG_BT_BYTES];
};

/* The translator's counts (rtpro_state's), copied as they change. */
struct rtwlog_pro {
    uint32_t on;                   /* 1: translating; 0: the trace only */
    uint32_t hooked;               /* IOS_IoctlvAsync hooked */
    uint32_t pros;
    uint32_t translated;
    uint32_t ids;
    uint32_t calibrations;
    uint32_t acks;
    uint32_t modes;
    uint32_t encrypted;            /* the game turned on encryption on a Pro link */
    uint32_t bt_total;             /* packets traced */
    uint32_t data_skipped;         /* data reports counted, not kept */
    uint32_t events_skipped;       /* Number Of Completed Packets events, counted */
    uint32_t keys;                 /* encryption keys turned into tables */
    uint32_t bad_keys;             /* keys that did not check out against the game's tables */
    uint32_t tables;               /* where the game's lookup tables are, 0: not found */
    uint32_t pro_encrypted;        /* a Pro sent its bytes encrypted although asked not to */
};

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
    struct rtwlog_pro pro;         /* version 3 */
    struct rtwlog_bt bt[RTWLOG_BT_ENTRIES];
};

/* Where message number `n` (from 0) is kept. */
static inline uint32_t rtwlog_slot(uint32_t n) {
    return n < RTWLOG_FIRST ? n : RTWLOG_FIRST + (n - RTWLOG_FIRST) % RTWLOG_LAST;
}

/* Where traced packet number `n` is kept. */
static inline uint32_t rtwlog_bt_slot(uint32_t n) {
    return n < RTWLOG_BT_FIRST ? n : RTWLOG_BT_FIRST + (n - RTWLOG_BT_FIRST) % RTWLOG_BT_LAST;
}

#ifdef __cplusplus
}
#endif

#endif
