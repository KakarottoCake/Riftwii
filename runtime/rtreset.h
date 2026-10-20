/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef RIFTWII_RTRESET_H
#define RIFTWII_RTRESET_H

/*
 * A Wii game's own restart (the HOME Menu's Reset) with the launch's
 * changes kept (include/riftwii/resetkeep.hpp has the whole story).
 *
 * The restore list the loader leaves in MEM2, big-endian words like
 * everything on the console:
 *   RTRST_MAGIC, record count,
 *   then per record: address, byte length (a multiple of 4), the bytes.
 * The same format holds the IPC hooks the runtime puts back while the
 * game's restart loads main.dol over them.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RTRST_MAGIC 0x52575253u /* 'RWRS' */
/* A list the loader never makes: a bad one is left alone. */
#define RTRST_MAX_RECORDS 4096u

/* Kept in rt_context (runtime/resident/rt_hook.h); the loader fills the
 * addresses, the runtime the rest. */
struct rtrst_state {
    uint32_t list;          /* the restore list, 0: the restart is left alone */
    uint32_t hooks;         /* the IPC hooks' list, put back after each load of the restart */
    uint32_t app_entry;     /* the disc apploader's own entry */
    uint32_t game_entry;    /* the game's entry point at the launch */
    uint32_t arena1_hi;     /* 0x80000034 at the launch: the restart's may not be higher */
    uint32_t mem1_end;      /* 0x80003110 at the launch, the same */
    uint32_t main_wrapper;  /* the blob's stand-ins for the apploader's main and close, */
    uint32_t close_wrapper; /* and the entry point the restart jumps to */
    uint32_t start_wrapper;
    uint32_t app_main;      /* the apploader's own main and close, learned at its entry */
    uint32_t app_close;
    uint32_t restarts;      /* apploader entries seen (the game reading its apploader again) */
    uint32_t restored;      /* restarts into the game itself, the list written back */
    uint32_t other;         /* restarts into another executable, left alone */
};

/* Writes every record of `list` to memory: a record's address plus
 * `bias` (0 on the console; host tests point it at a buffer), with the
 * caches made to agree for code. Returns the records written, or -1 when
 * the list is not one (its magic, or more than RTRST_MAX_RECORDS), with
 * nothing written. */
int32_t rtrst_apply(const uint8_t* list, uintptr_t bias);

#ifdef __cplusplus
}
#endif

#endif
