/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Wii Remotes for a GameCube game in Wii mode (gc_bt.c): the blob drives
 * the Wii's Bluetooth controller itself, through IOS's
 * /dev/usb/oh1/57e/305, from the game's PADRead, and turns each remote
 * (alone, with a Nunchuk or with a Classic Controller), and each Wii U
 * Pro Controller (gc_pro.h), into the GameCube controller on one port.
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
 *    (registers), input reports 0x20-0x22 and 0x30-0x32/0x35 (the
 *    accelerometer's three bytes after the buttons), the extension's
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

#include "gc_controls.h"
#include "gc_pro.h"

#define GC_BT_EVENT_READS 2u
#define GC_BT_ACL_READS 6u  /* a remote reports up to about 100 times a second; PADRead comes 60 */
#define GC_BT_READ_BYTES 256u
#define GC_BT_REMOTES 4u    /* one per GameCube port */
#define GC_BT_KEYS 8u
#define GC_BT_COMMANDS 6u   /* HCI commands waiting for the controller to take another */
#define GC_BT_FAKE_STEPS 8u /* the test remote's steps (gc_bt_fake) */
#define GC_BT_FAKE_FRAMES 60u  /* PADReads a step */

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
#define GC_BT_EXT_PRO 4u  /* not an extension: the remote is a Wii U Pro Controller (ID 01 20, gc_pro.h) */

struct gc_bt_remote {
    uint32_t used;
    uint32_t handle;
    uint8_t bdaddr[6];
    uint8_t ready;      /* both channels configured, LEDs set */
    uint8_t chans;
    uint8_t led_port;   /* the port its LED shows */
    uint8_t rumble;     /* the motor as the remote has it: bit 0 of every output report it is sent */
    uint8_t spare[2];
    uint32_t ctrl_cid;  /* the remote's end of each channel */
    uint32_t intr_cid;
    uint32_t ext_step;  /* 0 none, 1-3 starting it (gc_bt.c), 4 ready */
    uint32_t ext_kind;
    uint8_t core[2];    /* the buttons, as the remote sends them */
    uint8_t ext[GC_PRO_BYTES];  /* the extension's first six bytes; a Pro Controller's eleven */
    /* The accelerometer, reported only while the remote's kind has a shake
     * in the map (gc_controls.h): its last reading and the shake's state. */
    uint8_t accel[3];
    uint8_t accel_known;  /* accel holds a reading to compare the next with */
    uint8_t jolted;       /* a jolt since the last PADRead */
    uint8_t shake_timer;  /* gc_shake_step's */
    uint8_t ext_wait;     /* PADReads its extension's start has waited (gc_bt.c) */
    uint8_t ext_retries;  /* starts given another go */
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
    uint32_t shakes;          /* shakes that pressed their button, all remotes */
    uint32_t ctx;             /* struct gc_rt_context, for sending (set at each poll) */
    /* Rumble: what the game last asked of each port through PADControlMotor
     * (1 rumbles, 0 and 2 stop), and the trace's counters. A remote rumbles
     * while the port it plays on is asked to. */
    uint8_t rumble_want[4];
    uint32_t motor_calls;     /* PADControlMotor calls seen */
    uint32_t rumble_changes;  /* times a remote's motor was switched on or off */
    /* How far the Bluetooth got, for a tester's trace (GC_BT_SEEN_*), and
     * what came back when it did not go further. */
    uint32_t progress;
    uint8_t conn_status;      /* the last Connection_Complete's status (0: connected) */
    uint8_t auth_status;      /* the last Authentication_Complete's status (0: authenticated) */
    uint8_t disconnect_reason;
    uint8_t ext_error;        /* the last error a remote gave to the extension's start (write ack or read) */
    uint8_t ext_id[2];        /* the last extension ID read: 00 00 Nunchuk, 01 01 Classic Controller, 01 20 Pro Controller */
    uint8_t last_mode;        /* the last report mode sent */
    uint8_t ext_given_up;     /* extensions whose start never finished: played as a remote alone */
    uint32_t data_reports;    /* data reports (0x30-0x3f) from every remote */
    /* The raw 12-bit range each Pro Controller stick axis has reported, from
     * every Pro (report order: left X, right X, left Y, right Y), so a
     * tester's trace gives gc_pro.h's GC_PRO_STICK_REACH a measured value.
     * pro_axes_seen: a bit for each axis that has a first reading. */
    uint16_t pro_min[4];
    uint16_t pro_max[4];
    uint8_t pro_axes_seen;
    /* The test remote's results (gc_bt_fake): two words a step. */
    uint32_t fake_log[GC_BT_FAKE_STEPS * 2];
    /* What each button presses (gc_controls.h), filled by the loader. */
    struct gc_map map;
};

/* gc_bt.progress: a bit for each step a tester's trace should show,
 * set the first time it happens and never cleared. */
#define GC_BT_SEEN_OPEN 0x00000001u          /* /dev/usb/oh1/57e/305 opened */
#define GC_BT_SEEN_RESET 0x00000002u         /* HCI_Reset completed */
#define GC_BT_SEEN_KEYS 0x00000004u          /* the controller returned stored link keys */
#define GC_BT_SEEN_SCAN 0x00000008u          /* Write_Scan_Enable completed: remotes may connect */
#define GC_BT_SEEN_STEP_TIMEOUT 0x00000010u  /* a start-up step went on without its reply */
#define GC_BT_SEEN_COMMAND_ERROR 0x00000020u /* a command completed with an error (last_error) */
#define GC_BT_SEEN_CONN_REQUEST 0x00000040u  /* a remote asked to connect */
#define GC_BT_SEEN_CONNECTED 0x00000080u     /* Connection_Complete, status 0 */
#define GC_BT_SEEN_CONN_FAILED 0x00000100u   /* Connection_Complete with an error (conn_status) */
#define GC_BT_SEEN_KEY_REQUEST 0x00000200u   /* Link_Key_Request */
#define GC_BT_SEEN_NO_KEY 0x00000400u        /* ...for a remote whose key the controller did not return */
#define GC_BT_SEEN_PIN_REQUEST 0x00000800u   /* a PIN asked for: the remote is not synced here */
#define GC_BT_SEEN_AUTH_OK 0x00001000u       /* Authentication_Complete, status 0 */
#define GC_BT_SEEN_AUTH_FAILED 0x00002000u   /* ...with an error (auth_status) */
#define GC_BT_SEEN_DISCONNECT 0x00004000u    /* a remote went away (disconnect_reason) */
#define GC_BT_SEEN_L2CAP 0x00008000u         /* the remote opened an HID channel */
#define GC_BT_SEEN_READY 0x00010000u         /* both channels configured, the LED set */
#define GC_BT_SEEN_STATUS 0x00020000u        /* a status report */
#define GC_BT_SEEN_EXTENSION 0x00040000u     /* ...with an extension plugged in */
#define GC_BT_SEEN_EXT_WRITE 0x00080000u     /* a write to the extension acknowledged */
#define GC_BT_SEEN_EXT_ERROR 0x00100000u     /* the remote gave an error to the extension's start (ext_error) */
#define GC_BT_SEEN_EXT_ID 0x00200000u        /* the extension's ID read (ext_id) */
#define GC_BT_SEEN_MODE 0x00400000u          /* a report mode sent (last_mode) */
#define GC_BT_SEEN_DATA 0x00800000u          /* a data report: the buttons reach the game */
#define GC_BT_SEEN_EXT_RETRY 0x01000000u     /* the extension's start given another go */
#define GC_BT_SEEN_EXT_GIVEN_UP 0x02000000u  /* ...and given up: the remote plays alone */
#define GC_BT_SEEN_PRO 0x04000000u           /* a Wii U Pro Controller identified (its ID read as 01 20) */

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
/* PADControlMotor(chan, command) from the game: the port's rumble is noted
 * (command 1 rumbles, 0 and 2 stop) and goes to the remote on that port
 * with the next PADRead. `allowed`: Settings' Wii Remote rumble (and the
 * game's own) is on; off, every command stops. */
void gc_bt_motor(struct gc_bt* bt, uint32_t chan, uint32_t command, int allowed);
/* Every remote's motor off at once (the game is leaving or has crashed). */
void gc_bt_stop_rumble(struct gc_rt_context* ctx, struct gc_bt* bt);
/* 1 while a ready Wii Remote's HOME, its Classic Controller's or a Pro
 * Controller's, is held. */
int gc_bt_home_held(const struct gc_bt* bt);
/* The test remote (sd:/riftwii/gc/test/fakeremote, gc_rt.h's
 * GC_FLAG_FAKE_REMOTE), for Dolphin, whose emulated remote connects but
 * takes its buttons and motion from keys in its window, which a test run
 * cannot press: instead of the Bluetooth, a remote
 * in slot 1 sends made-up reports through the same parser and map, a step
 * of GC_BT_FAKE_FRAMES PADReads at a time (gc_bt.c lists them), and what
 * port 1 got in each step goes to fake_log for the trace. `n`: the
 * PADRead's count. `pro` (sd:/riftwii/gc/test/fakepro, GC_FLAG_FAKE_PRO):
 * a Wii U Pro Controller's reports instead, steps of their own. */
void gc_bt_fake(struct gc_bt* bt, uint8_t* status, uint32_t n, int pro);
/* A reply from IOS that came while another request was being waited
 * for: marked done if it is one of these (1), else 0. */
int gc_bt_on_reply(struct gc_bt* bt, uint32_t reply);
#endif

#endif
