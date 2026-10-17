/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Wii Remotes for a GameCube game in Wii mode (gc_bt.c): the blob drives
 * the Wii's Bluetooth controller itself, through IOS's
 * /dev/usb/oh1/57e/305, from the game's PADRead, and turns each remote
 * (alone, with a Nunchuk or with a Classic Controller) into the
 * GameCube controller on one port.
 *
 * The menu's libogc reset the controller and closed the device at the
 * hand-over, so the remotes are off: one connects again when a button is
 * pressed, as on the Wii Menu. Only remotes synced to this Wii can (their
 * link keys are in the controller).
 *
 * Every request is asynchronous and polled (gc_rt.c's IPC). Facts used,
 * from public documentation, no code taken:
 *  - The Bluetooth Core specification: HCI commands and events, ACL
 *    packets, L2CAP signalling (connection, configuration).
 *  - wiibrew, "Wiimote": the HID channels (PSM 0x11, 0x13), output
 *    reports 0x11 (LEDs), 0x12 (report mode), 0x15 (status), 0x16/0x17
 *    (registers), input reports 0x20-0x22 and 0x30/0x32, the extension's
 *    unencrypted start (0x55 to 0xA400F0, 0 to 0xA400FB), its ID at
 *    0xA400FA, the Nunchuk and Classic Controller data formats.
 *  - wiibrew, "/dev/usb/oh1": the USB v0 ioctls (control 0, bulk 1,
 *    interrupt 2), their vectors, the Bluetooth endpoints (events 0x81,
 *    ACL in 0x82, out 0x02).
 *  - libogc's lwbt (as the menu's own stack): the order it starts the
 *    controller in and that it answers a link key request with the key
 *    the controller stores. */
#ifndef RIFTWII_GC_BT_H
#define RIFTWII_GC_BT_H

#include <stdint.h>

#define GC_BT_EVENT_READS 2u
#define GC_BT_ACL_READS 6u  /* a remote reports up to about 100 times a second; PADRead comes 60 */
#define GC_BT_READ_BYTES 256u
#define GC_BT_REMOTES 4u    /* one per GameCube port */
#define GC_BT_KEYS 8u
#define GC_BT_COMMANDS 6u   /* HCI commands waiting for the controller to take another */

/* One IOS request with its vectors and small input buffers. */
struct gc_bt_rq {
    uint32_t ipc[16];
    uint32_t vec[16];
    uint8_t setup[32];
    uint32_t state;     /* GC_BT_RQ_* */
    int32_t result;
    uint32_t data;      /* the buffer IOS writes into, 0: none */
    uint32_t length;
    uint32_t pad[4];
};
#define GC_BT_RQ_IDLE 0u
#define GC_BT_RQ_SENT 1u
#define GC_BT_RQ_DONE 2u

/* gc_bt_remote.chans: each HID channel's two configurations. */
#define GC_BT_CTRL_THEIRS 1u
#define GC_BT_CTRL_OURS 2u
#define GC_BT_INTR_THEIRS 4u
#define GC_BT_INTR_OURS 8u
#define GC_BT_CHANS_READY 15u

/* gc_bt_remote.ext_kind */
#define GC_BT_EXT_NONE 0u
#define GC_BT_EXT_NUNCHUK 1u
#define GC_BT_EXT_CLASSIC 2u
#define GC_BT_EXT_OTHER 3u

struct gc_bt_remote {
    uint32_t used;
    uint32_t handle;
    uint8_t bdaddr[6];
    uint8_t ready;      /* both channels configured, LEDs set */
    uint8_t chans;
    uint8_t led_port;   /* the port its LED shows */
    uint8_t spare[3];
    uint32_t ctrl_cid;  /* the remote's end of each channel */
    uint32_t intr_cid;
    uint32_t ext_step;  /* 0 none, 1-3 starting it (gc_bt.c), 4 ready */
    uint32_t ext_kind;
    uint8_t core[2];    /* the buttons, as the remote sends them */
    uint8_t ext[6];     /* the extension's first six bytes */
};

struct gc_bt {
    struct gc_bt_rq events[GC_BT_EVENT_READS];
    struct gc_bt_rq acl[GC_BT_ACL_READS];
    struct gc_bt_rq write;  /* one write (or the open) at a time, waited for */
    uint8_t event_buf[GC_BT_EVENT_READS][GC_BT_READ_BYTES] __attribute__((aligned(32)));
    uint8_t acl_buf[GC_BT_ACL_READS][GC_BT_READ_BYTES] __attribute__((aligned(32)));
    uint8_t out[64] __attribute__((aligned(32)));    /* the packet being written */
    uint8_t path[32] __attribute__((aligned(32)));
    uint8_t commands[GC_BT_COMMANDS][32];
    uint8_t keys[GC_BT_KEYS][22];                     /* address, then link key */
    struct gc_bt_remote remotes[GC_BT_REMOTES];
    /* The ports as gc_bt_fill last left them (gc_ports.h): each remote
     * slot's port, and each port's err byte as the game got it. */
    uint8_t port_of[GC_BT_REMOTES];
    uint8_t port_err[GC_BT_REMOTES];
    int32_t fd;
    uint32_t stage;         /* GC_BT_STAGE_* */
    uint32_t credits;       /* commands the controller takes now */
    uint32_t command_head;
    uint32_t command_count;
    uint32_t key_count;
    uint32_t ident;         /* the next L2CAP signalling identifier */
    uint32_t stage_polls;   /* polls since the start-up's last step */
    /* For the trace. */
    uint32_t polls;
    uint32_t event_count;
    uint32_t acl_count;
    uint32_t connects;
    int32_t last_error;
    uint32_t last_event;    /* code << 16 | the opcode or first bytes */
    uint8_t event_log[4][8];  /* the first four events' first bytes, for the trace */
    uint32_t event_lengths;   /* their lengths, a byte each */
};

#define GC_BT_STAGE_CLOSED 0u
#define GC_BT_STAGE_RESET 1u    /* HCI_Reset sent */
#define GC_BT_STAGE_KEYS 2u     /* Read_Stored_Link_Key sent */
#define GC_BT_STAGE_SCAN 3u     /* Write_Scan_Enable sent */
#define GC_BT_STAGE_READY 4u    /* remotes may connect */
#define GC_BT_STAGE_FAILED 5u   /* the device did not open or a write failed: given up */

#ifdef RT_TARGET_PPC
struct gc_rt_context;
/* Once a frame, from PADRead: IOS's replies taken, the controller and
 * remotes served, then each remote's state written into the port
 * gc_ports.h gives it, one a GameCube controller does not hold (status:
 * the game's PADStatus[4]). */
void gc_bt_poll(struct gc_rt_context* ctx, struct gc_bt* bt);
void gc_bt_fill(struct gc_bt* bt, uint8_t* status);
/* 1 while a ready Wii Remote's HOME, or its Classic Controller's, is held. */
int gc_bt_home_held(const struct gc_bt* bt);
/* A reply from IOS that came while another request was being waited
 * for: marked done if it is one of these. */
void gc_bt_on_reply(struct gc_bt* bt, uint32_t reply);
#endif

#endif
