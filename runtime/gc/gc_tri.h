/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Triforce arcade games (Mario Kart Arcade GP 2 first) in RiftWii's
 * GameCube loader: the blob stands in for the arcade boards the game
 * talks to, which a Wii does not have.
 *
 *  - The AM baseboard on SI channel 0. The game sends it 128-byte packets
 *    (0x70, a length, then commands of a byte and a length each); it
 *    answers each one a packet late (the SI swaps 128 bytes both ways at
 *    once), with 0x01, a length, the answers and a checksum that makes
 *    the 128 bytes add up to 0xFF. The JVS I/O board (cabinet buttons,
 *    wheel, pedals, coins) and the steering wheel's serial line ride in
 *    those packets. A one-byte 0x00 asks for the device's type.
 *  - The baseboard's EXI side (channel 0, device 2): its backup memory
 *    (the test menu's settings, the coin counters) and its interrupt
 *    registers. The arcade SDK's own EXI functions are replaced. Its
 *    interrupt reaches the game as channel 2's EXI interrupt, so the
 *    handler kept and called is the one the game sets for channel 2.
 *  - The media board, through the disc interface: its type (0x12), its
 *    status words (reads at 0x80000000 and up), its command mailbox
 *    (writes, 0xAB). The network board under it is answered as idle, so
 *    the game's link-up gives up and the game runs on its own.
 *
 * Behaviour from naari3's Mario Kart Arcade GP2 decompilation (CC0) and
 * Dolphin's Triforce devices (read for behaviour only; NOTICE.md). The
 * code is RiftWii's own.
 *
 * gc_tri.c is built into the blob and, without RT_TARGET_PPC, into the
 * host tests. Blob rules: no globals, no constant data, no library calls:
 * the strings and tables the boards answer with are loader-filled
 * (riftwii/gctriforce.hpp's gc_tri_identity). */
#ifndef RIFTWII_GC_TRI_H
#define RIFTWII_GC_TRI_H

#include <stdint.h>

#define GC_TRI_MAGIC 0x52575446u  /* "RWTF" */
#define GC_TRI_PACKET 128u
/* The baseboard's backup memory: the arcade SDK addresses it with 16-bit
 * offsets and keeps two copies of a 0x200-byte record in its first KiB;
 * its table says 0x2000 bytes. */
#define GC_TRI_BACKUP_BYTES 0x2000u
#define GC_TRI_IO_ID_BYTES 64u
#define GC_TRI_IO_FEATURE_BYTES 32u
#define GC_TRI_ANALOG 8u
#define GC_TRI_COIN_SLOTS 2u
#define GC_TRI_WHEEL_BYTES 32u

/* Baseboard (SI) commands inside a 0x70 packet. */
#define GC_TRI_SI_STATUS 0x00u        /* a packet of its own: the device type */
#define GC_TRI_SI_PACKET 0x70u
#define GC_TRI_BB_SWITCHES 0x10u      /* status switches */
#define GC_TRI_BB_SERIAL 0x11u
#define GC_TRI_BB_FIRM 0x15u
#define GC_TRI_BB_FPGA 0x16u
#define GC_TRI_BB_REGION 0x1Fu
#define GC_TRI_BB_SERIAL_A 0x31u      /* the steering wheel's line */
#define GC_TRI_BB_SERIAL_B 0x32u      /* the card reader's line */
#define GC_TRI_BB_JVS 0x40u           /* 0x40-0x5F: a JVS frame (the low bits: its node) */

/* JVS (the I/O board's protocol): frames start with 0xE0; 0xD0 escapes
 * 0xD0 and 0xE0 (sent as 0xD0 then the byte minus one). */
#define GC_TRI_JVS_SYNC 0xE0u
#define GC_TRI_JVS_MARK 0xD0u
#define GC_TRI_JVS_BROADCAST 0xFFu
#define GC_TRI_JVS_OK 1u              /* status, and a command's report */
#define GC_TRI_JVS_UNKNOWN 2u         /* status: a command it does not know */
#define GC_TRI_JVS_CHECKSUM 3u        /* status: the frame's sum was wrong */

/* The player's inputs, as JVS bits. */
#define GC_TRI_SYSTEM_TEST 0x80u      /* the cabinet's test switch */
#define GC_TRI_P1_START 0x80u         /* player byte 0 */
#define GC_TRI_P1_SERVICE 0x40u       /* player byte 0 */
#define GC_TRI_P1_ITEM 0x20u          /* player byte 1: Mario Kart's item button */
#define GC_TRI_P1_CANCEL 0x02u        /* player byte 1: Mario Kart's cancel button */
#define GC_TRI_ANALOG_CENTRE 0x8000u

/* EXI baseboard commands (the first byte of the arcade SDK's 4-byte
 * command, after its checksum byte). */
#define GC_TRI_EXI_CMD_READ_ISR 0x82u
#define GC_TRI_EXI_CMD_WRITE_ISR 0x83u
#define GC_TRI_EXI_CMD_READ_IMR 0x86u
#define GC_TRI_EXI_CMD_WRITE_IMR 0x87u
#define GC_TRI_EXI_CMD_WRITE_LANCNT 0xFFu

/* The media board. */
#define GC_TRI_MEDIA_INQUIRY 0x29484100u  /* type 0x29 (DIMM board), as Mario Kart Arcade GP 2's expects */
#define GC_TRI_BOARD_STATUS 0x80000000u   /* status words, 0x20 apart */
#define GC_TRI_BOARD_DIMM 0x1F000000u     /* the board's own memory windows, past any game image */

/* The boards' command mailboxes. The arcade SDK writes a 32-byte request
 * (an id, a byte, a big-endian command word, then its data) to a
 * device's request box, rings its doorbell, and waits for the board to
 * put the answer in its answer box (the command with 0x80 set, the data
 * from byte 4) and raise the baseboard's EXI interrupt: status bit 0x04
 * for the media board, 0x10 for the network board. The commands answered:
 * 0x0100 (link up: an IP address), 0x0101 (firmware version) and 0x0301
 * (serial number); any other gets its command word back with no data. */
#define GC_TRI_BOXES 2u
#define GC_TRI_BOX_MEDIA 0u
#define GC_TRI_BOX_NETWORK 1u
#define GC_TRI_MEDIA_ANSWER 0x84000000u
#define GC_TRI_MEDIA_REQUEST 0x84000020u
#define GC_TRI_MEDIA_DOORBELL 0x84000040u  /* a write */
#define GC_TRI_NET_ANSWER 0x89000000u
#define GC_TRI_NET_REQUEST 0x89000200u
#define GC_TRI_NET_DOORBELL 0x88000000u    /* a read */

/* The arcade SDK functions the loader replaces (gc_entry.S's table, in
 * this order; riftwii/gctriforce.hpp finds them). */
enum gc_tri_fn {
    GC_TRI_SI_TRANSFER,     /* SITransfer(chan, out, out_len, in, in_len, cb, delay): channel 0 here */
    GC_TRI_EXI_CONNECT,     /* connect(chan, dev): the baseboard's EXI ID checked */
    GC_TRI_EXI_WRITE_BYTE,  /* (chan, dev, addr, data) */
    GC_TRI_EXI_READ_BYTE,   /* (chan, dev, addr, u8* data) */
    GC_TRI_EXI_WRITE_BLOCK, /* (chan, dev, addr, buf, len, sync) */
    GC_TRI_EXI_READ_BLOCK,  /* (chan, dev, addr, buf, len, sync) */
    GC_TRI_EXI_READ_ISR,    /* (chan, dev, u16* data) */
    GC_TRI_EXI_WRITE_ISR,   /* (chan, dev, data) */
    GC_TRI_EXI_READ_IMR,
    GC_TRI_EXI_WRITE_IMR,
    GC_TRI_EXI_WRITE_LANCNT,
    GC_TRI_DI_WRITE,        /* the 0xAA write: (addr, length, offset, cb), as DVDLowRead */
    GC_TRI_DI_INQUIRY,      /* the 0x12 inquiry: (cb), answered in the DI's immediate register */
    GC_TRI_DI_EXECUTE,      /* the 0xAB command: (arg, cb) */
    GC_TRI_EXI_CALLBACK,    /* (chan, callback): the baseboard's interrupt handler, kept here */
    GC_TRI_HOOKS
};

struct gc_tri {
    uint32_t magic;
    /* Loader-filled identity (gc_tri_identity). */
    uint8_t status_id[4];                      /* the SI type answer, 3 bytes */
    uint8_t serial[16];                        /* the baseboard's serial number (0x11) */
    uint8_t region[20];                        /* the region answer (0x1F) */
    uint8_t firm[2];                           /* 0x15 */
    uint8_t fpga[2];                           /* 0x16 */
    uint8_t io_id[GC_TRI_IO_ID_BYTES];         /* the I/O board's name, with its NUL */
    uint8_t io_features[GC_TRI_IO_FEATURE_BYTES]; /* its feature list, with the end code */
    uint8_t io_id_len;
    uint8_t io_features_len;
    uint8_t io_command_rev;
    uint8_t io_jvs_rev;
    uint8_t io_comm_rev;
    uint8_t pad0[3];
    uint32_t media_inquiry;                    /* what the media board answers to 0x12 */
    uint8_t board_serial[16];                  /* the media and network boards' serial (0x0301) */
    /* The SI side: the answer to the last packet waits for the next one. */
    uint8_t response[2][GC_TRI_PACKET];
    uint32_t response_ready;                   /* response[ready_index] holds an answer */
    uint32_t ready_index;
    uint8_t status_switches[2];                /* 0x10's answer; bit 0 of the second: the JVS sense line */
    uint8_t jvs_address;                       /* the I/O board's node, 0 before the master sets it */
    uint8_t coin_was_down;
    uint16_t coins[GC_TRI_COIN_SLOTS];
    uint8_t wheel_rx[GC_TRI_WHEEL_BYTES];      /* the steering wheel's requests not yet whole */
    uint8_t wheel_tx[GC_TRI_WHEEL_BYTES];      /* its answers not yet sent */
    uint8_t wheel_rx_len;
    uint8_t wheel_tx_len;
    uint8_t wheel_state;                       /* 0: just powered, 1 and 2: alternating answers */
    uint8_t pad1;
    /* The cabinet's inputs, from the controller (gc_tri_set_pad). */
    uint8_t system_in;
    uint8_t player_in[2];
    uint8_t coin_in;                           /* bit per slot: the coin switch is down */
    uint16_t analog_in[GC_TRI_ANALOG];
    /* The EXI side. */
    uint32_t irq_status;
    uint32_t imr;
    uint32_t exi_callback;                     /* the game's handler for the baseboard's interrupt */
    uint32_t irq_raise;                        /* an answer is in a box: the interrupt is due */
    uint8_t box_request[GC_TRI_BOXES][32];
    uint8_t box_answer[GC_TRI_BOXES][32];
    /* Counters, for the trace and for checking a run in Dolphin. */
    uint32_t si_packets;
    uint32_t si_type_reads;
    uint32_t si_unknown;                       /* other one-off SI commands, the last in the top byte */
    uint32_t bb_unknown;                       /* baseboard commands not known, the last in the top byte */
    uint32_t jvs_frames;
    uint32_t jvs_errors;                       /* frames answered with an error status, the last command in the top byte */
    uint32_t exi_calls;
    uint32_t backup_writes;
    uint32_t board_reads;
    uint32_t board_writes;
    uint32_t board_last;                       /* the last board offset read or written */
    uint32_t board_executes;
    uint32_t box_answers;                      /* answers put in a box, the last command in the top half */
    uint32_t pad_polls;
    uint32_t pad_errors;
    uint32_t pad_last[2];                      /* the controller's last 8 bytes */
    uint8_t backup[GC_TRI_BACKUP_BYTES];
};

#ifdef __cplusplus
extern "C" {
#endif

/* SI channel 0: `out` (out_len bytes) from the game; the answer goes to
 * `in` (at most in_len). Returns the bytes answered; 0 means no answer
 * (the hardware's "no response"). */
uint32_t gc_tri_si(struct gc_tri* t, const uint8_t* out, uint32_t out_len, uint8_t* in, uint32_t in_len);

/* The controller in port 1, as its 8 status bytes (0x40 poll, mode 3), or
 * ok 0 when it did not answer: Start the cabinet's start, A a coin, B the
 * item button, X cancel, Z the service button, the stick the wheel, R the
 * accelerator and L the brake. */
void gc_tri_set_pad(struct gc_tri* t, const uint8_t* pad, int ok);

/* A media board offset (not the game's disc image). */
int gc_tri_is_board(uint32_t offset);
/* A disc read of a board offset: `length` bytes into buf. */
void gc_tri_board_read(struct gc_tri* t, uint32_t offset, uint8_t* buf, uint32_t length);
void gc_tri_board_write(struct gc_tri* t, uint32_t offset, const uint8_t* buf, uint32_t length);

/* The baseboard's backup memory and registers over EXI. */
void gc_tri_backup_write(struct gc_tri* t, uint32_t addr, const uint8_t* buf, uint32_t length);
void gc_tri_backup_read(struct gc_tri* t, uint32_t addr, uint8_t* buf, uint32_t length);
/* A register command (GC_TRI_EXI_CMD_*) with its 16-bit value; a read's
 * answer is returned. */
uint32_t gc_tri_exi_register(struct gc_tri* t, uint32_t command, uint32_t value);

#ifdef __cplusplus
}
#endif

#ifdef RT_TARGET_PPC
struct gc_rt_context;
/* A replaced arcade SDK function (enum gc_tri_fn) with the game's r3..r10
 * in args[0..7]; args[8] set when the original should run instead (SI
 * channels 1-3). Returns the game's r3. */
uint32_t gc_on_tri(struct gc_rt_context* ctx, uint32_t fn, uint32_t* args);
/* DVDLowRead of a board offset (gc_on_read): answered here. */
uint32_t gc_tri_on_read(struct gc_rt_context* ctx, struct gc_tri* t, uint32_t addr, uint32_t length, uint32_t offset,
                        uint32_t cb);
#endif

#endif
