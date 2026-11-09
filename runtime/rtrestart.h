/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The request the game's own Reset leaves on the SD card, written from the
 * pad blob (runtime/pad) in a launch that has no resident runtime (#458):
 * one sector of the card, the same one the resident runtime writes
 * (rt_restart_write in runtime/resident/rt_hook.c), through the IOS call the
 * game uses for its own. Kept free of the hardware so the host tests can run
 * it; the blob does the call and the cache flushes. */
#ifndef RIFTWII_RTRESTART_H
#define RIFTWII_RTRESTART_H

#include <stdint.h>

#define RTRESTART_SECTOR 512u

/* How the card is driven: the loader's `sdio::Card`. */
#define RTRESTART_SD_BYTES 0u  /* SDSC: CMD25's argument is a byte offset */
#define RTRESTART_SD_SDHC 1u   /* SDHC: it is a sector number */
#define RTRESTART_SD_D2X 2u    /* the fd is d2x's /dev/sdio/sdhc: sector and count */

/* /dev/sdio/slot0's SENDCMD request (wiibrew), 36 bytes: the same words as
 * the runtime's struct rt_sdio_request. */
struct rtrestart_request {
    uint32_t cmd;
    uint32_t cmd_type;
    uint32_t rsp_type;
    uint32_t arg;
    uint32_t blk_cnt;
    uint32_t blk_size;
    uint32_t dma_addr;
    uint32_t isdma;
    uint32_t pad0;
};

struct rtrestart_vector {
    uint32_t data;
    uint32_t len;
};

/* The Reset's own words, loader-filled except the counters. */
struct rtrestart_state {
    uint32_t return_to_menu;  /* the game's OSReturnToMenu, which Return to RiftWii points at the channel; loader-filled
                                 last, once the card, the request and that route are all in place: it turns the
                                 Reset on. 0: the game's own Reset is left alone */
    uint32_t lba;             /* the request's sector on the card */
    uint32_t sdio_fd;         /* the card's handle, held for the game since the launch */
    uint32_t sdio_mode;       /* RTRESTART_SD_* */
    uint32_t ioctlv;          /* the game's IOS_Ioctlv (synchronous) */
    uint32_t asked;           /* calls of OSRestart seen */
    uint32_t written;         /* requests written to the card */
    uint32_t refused;         /* requests undone: the write was refused */
    uint32_t result;          /* the last IOS result that was not a success */
    uint32_t pad_state[7];    /* the sectors that follow start on a line of their own */
};

/* In MEM2, 32-byte aligned (IOS flushes and invalidates by line): the
 * state, the two sectors the loader prepared, and everything one write
 * hands to IOS. */
struct rtrestart_block {
    struct rtrestart_state state;
    uint8_t armed[RTRESTART_SECTOR];  /* the request as the Reset leaves it */
    uint8_t idle[RTRESTART_SECTOR];   /* what the sector held; back there when the write may have half landed */
    struct rtrestart_request request __attribute__((aligned(32)));
    uint32_t pad_request[7];
    uint32_t response[8] __attribute__((aligned(32)));
    struct rtrestart_vector vec[3] __attribute__((aligned(32)));
    uint32_t pad_vec[2];
} __attribute__((aligned(32)));

/* The IOS call that writes one sector. */
struct rtrestart_call {
    uint32_t ioctl;
    uint32_t in_count;
    uint32_t out_count;
};

/* Fills blk's request and vectors to write `sector` (line aligned, 512
 * bytes) to `lba`, and says which call to make with blk->vec. */
void rtrestart_prepare_write(struct rtrestart_block* blk, uint32_t sd_mode, uint32_t lba, const uint8_t* sector,
                             struct rtrestart_call* call);

#endif
