/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The virtual SD card: a card image (a file such as Dolphin's sd.raw on
 * the SD card or the USB drive) that a game sees in place of the card in
 * its SD slot. Code builds (Project+, REX) read their files through the
 * game's own SD driver; with the image they can live on an SDXC card or
 * a USB drive the game could not read itself.
 *
 * This part is the card: what /dev/sdio/slot0 answers to the game's
 * ioctls and card commands, and where a block of the image lies on the
 * device that holds it. The in-game blob (runtime/vsd) calls it for every
 * request on the handle it gave the game and moves the blocks. Freestanding
 * C99 like rtfat.c: no libc, no static data, no allocation, so it builds
 * unchanged for the host tests and the blob.
 *
 * The card presents itself as an SDHC card when the image is larger than
 * 2 GiB (block addresses), as a standard-capacity one otherwise (byte
 * addresses), with the registers the SD Physical Layer Simplified
 * Specification describes. Sources (no code taken from any of them):
 *  - wiibrew, "/dev/sdio/slot0": the ioctl numbers, the 36-byte command
 *    block, the 16-byte reply, the status bits of GETSTATUS.
 *  - libogc's wiisd.c: the host controller register block (24 bytes: the
 *    register, the size, the value), the command types.
 *  - Dolphin's SDIOSlot0 (behaviour only): the host controller registers
 *    read back as written, the clock turning stable when enabled and the
 *    software reset finishing at once, RESETCARD answering the card's
 *    address, the card event (command 0x40) left pending until it is
 *    taken back (0x41), which answers it with 0x0C210000.
 *  - The SD Physical Layer Simplified Specification 2.00: the CID, CSD
 *    (versions 1.0 and 2.0) and SCR layouts, CRC7, R1 status bits and
 *    card states, the OCR bits. */
#ifndef RIFTWII_RTVSD_H
#define RIFTWII_RTVSD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RTVSD_SECTOR_BYTES 512u
#define RTVSD_MAX_EXTENTS 512u          /* pieces of the image file on its device */
#define RTVSD_SDSC_MAX_SECTORS 0x400000u /* 2 GiB: up to here a standard-capacity card */

/* /dev/sdio/slot0 ioctls (wiibrew). */
#define RTVSD_IOCTL_WRITEHCR 0x01u
#define RTVSD_IOCTL_READHCR 0x02u
#define RTVSD_IOCTL_RESETCARD 0x04u
#define RTVSD_IOCTL_SETCLK 0x06u
#define RTVSD_IOCTL_SENDCMD 0x07u
#define RTVSD_IOCTL_SETBUSWIDTH 0x08u
#define RTVSD_IOCTL_GETSTATUS 0x0Bu
#define RTVSD_IOCTL_GETOCR 0x0Cu

/* GETSTATUS bits (wiibrew, libogc). */
#define RTVSD_STATUS_INSERTED 0x00000001u
#define RTVSD_STATUS_INITIALIZED 0x00010000u
#define RTVSD_STATUS_SDHC 0x00100000u

/* Card commands the game sends with SENDCMD. */
#define RTVSD_CMD_GO_IDLE 0u
#define RTVSD_CMD_ALL_SEND_CID 2u
#define RTVSD_CMD_SEND_RCA 3u
#define RTVSD_CMD_SELECT 7u
#define RTVSD_CMD_SEND_IF_COND 8u
#define RTVSD_CMD_SEND_CSD 9u
#define RTVSD_CMD_SEND_CID 10u
#define RTVSD_CMD_STOP 12u
#define RTVSD_CMD_SEND_STATUS 13u
#define RTVSD_CMD_SET_BLOCKLEN 16u
#define RTVSD_CMD_READ_SINGLE 17u
#define RTVSD_CMD_READ_MULTIPLE 18u
#define RTVSD_CMD_WRITE_SINGLE 24u
#define RTVSD_CMD_WRITE_MULTIPLE 25u
#define RTVSD_CMD_APP 55u
#define RTVSD_ACMD_SET_BUS_WIDTH 6u
#define RTVSD_ACMD_SD_STATUS 13u
#define RTVSD_ACMD_SEND_OP_COND 41u
#define RTVSD_ACMD_SET_CLR_DETECT 42u
#define RTVSD_ACMD_SEND_SCR 51u
/* Not card commands: IOS's card event (wiibrew; Dolphin's behaviour). */
#define RTVSD_CMD_EVENT_REGISTER 0x40u
#define RTVSD_CMD_EVENT_UNREGISTER 0x41u
#define RTVSD_EVENT_INVALID 0x0C210000u  /* what a taken-back event is answered with */

#define RTVSD_RCA 0x5257u                 /* the card's address ("RW") */
#define RTVSD_OCR 0x80FF8000u             /* powered up, 2.7-3.6 V */
#define RTVSD_OCR_CCS 0x40000000u         /* block addresses (SDHC) */

/* R1 card status: the current state in bits 9-12. */
#define RTVSD_R1_READY_FOR_DATA 0x00000100u
#define RTVSD_R1_APP_CMD 0x00000020u
#define RTVSD_R1_ADDRESS_ERROR 0x40000000u
#define RTVSD_R1_OUT_OF_RANGE 0x80000000u
#define RTVSD_STATE_STBY 3u
#define RTVSD_STATE_TRAN 4u

/* IOS results. */
#define RTVSD_OK 0
#define RTVSD_EINVAL (-4)

/* What a command asks of the blob (rtvsd_command's answer). */
#define RTVSD_DONE 0u           /* answered: the reply is ready */
#define RTVSD_READ 1u           /* move `count` blocks from `sector` of the image into the buffer */
#define RTVSD_WRITE 2u          /* move `count` blocks from the buffer to `sector` of the image */
#define RTVSD_DATA 3u           /* copy `data_bytes` of `data` into the buffer, then answer */
#define RTVSD_EVENT_HOLD 4u     /* keep the request unanswered: the card event */
#define RTVSD_EVENT_RELEASE 5u  /* answer the held event with RTVSD_EVENT_INVALID, and this one with 0 */

/* The command block (wiibrew, 36 bytes). */
struct rtvsd_request {
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

/* `count` sectors of the image from `file_sector` lie at `device_sector`
 * of the device holding it (the layout of rt_rvz_extent). */
struct rtvsd_extent {
    uint32_t file_sector;
    uint32_t device_sector;
    uint32_t count;
};

struct rtvsd_answer {
    uint32_t kind;          /* RTVSD_DONE .. RTVSD_EVENT_RELEASE */
    int32_t result;         /* the IOS result when answered */
    uint32_t reply[4];      /* the card's response */
    uint32_t sector;        /* READ, WRITE: the first block of the image */
    uint32_t count;         /* and how many */
    uint32_t data_bytes;    /* DATA */
    uint8_t data[64];
};

/* The card. The loader fills `sectors` and the extents (sorted by
 * file_sector, without gaps from 0) and calls rtvsd_reset; the rest is the
 * card's own. All uint32 so the layout is the same on the host. */
struct rtvsd_card {
    uint32_t sectors;       /* the image's size in sectors */
    uint32_t sdhc;          /* presented as SDHC (block addresses) */
    uint32_t state;         /* RTVSD_STATE_* */
    uint32_t app_cmd;       /* the last command was CMD55 */
    uint32_t bus_width;     /* 0: 1 bit, 2: 4 bits */
    uint32_t read_only;     /* writes fail (the image could not be written) */
    uint32_t extent_count;
    uint32_t hcr[64];       /* host controller registers 0x00-0xFF, one byte each, packed 4 to a word */
    struct rtvsd_extent extents[RTVSD_MAX_EXTENTS];
};

/* After the loader filled `sectors` and the extents: the card as IOS
 * leaves it at power-up. Returns 0 when the extents cover the image from
 * sector 0 without gaps and within 32 bits, -1 otherwise. */
int rtvsd_reset(struct rtvsd_card* card);

/* An ioctl other than SENDCMD: `in`/`out` are the request's buffers as
 * the game passed them (big-endian words), NULL when absent. Returns the
 * IOS result; writes at most `out_len` bytes. */
int32_t rtvsd_ioctl(struct rtvsd_card* card, uint32_t ioctl, const uint32_t* in, uint32_t in_len, uint32_t* out,
                    uint32_t out_len);

/* A SENDCMD: what to do, in `answer`. `data_len` is the size of the
 * data buffer the game supplied (0: none). For READ and WRITE the reply
 * is already the one to give when the transfer succeeds. */
void rtvsd_command(struct rtvsd_card* card, const struct rtvsd_request* req, uint32_t data_len,
                   struct rtvsd_answer* answer);

/* Where `sector` of the image lies: its device sector, and how many
 * sectors from there are contiguous (at most `count`). 0 when outside
 * the image. */
uint32_t rtvsd_map(const struct rtvsd_card* card, uint32_t sector, uint32_t count, uint32_t* device_sector);

/* The card registers, for the tests: CID and CSD as the four words of an
 * R2 response (bit 127 first, the CRC7 and end bit included), the SCR as
 * 8 bytes. */
void rtvsd_cid(uint32_t out[4]);
void rtvsd_csd(const struct rtvsd_card* card, uint32_t out[4]);
/* The capacity a CSD states, in sectors (the inverse, for the tests). */
uint32_t rtvsd_csd_sectors(const uint32_t csd[4]);

#ifdef __cplusplus
}
#endif

#endif
