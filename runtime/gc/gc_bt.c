/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Wii Remotes as GameCube controllers (gc_bt.h). Same rules as the rest
 * of the blob: no globals, no constant data, no library calls. */
#include "gc_bt.h"
#include "gc_ports.h"
#include "gc_rt.h"

#define GC_IOS_OPEN 1u
#define GC_IOS_IOCTLV 7u
#define GC_USB_CTRLMSG 0u
#define GC_USB_BLKMSG 1u
#define GC_USB_INTRMSG 2u
#define GC_BT_EP_EVENTS 0x81u
#define GC_BT_EP_ACL_IN 0x82u
#define GC_BT_EP_ACL_OUT 0x02u
#define GC_BT_WRITE_POLLS 20000000u  /* polls before a write counts as lost */

/* HCI opcodes (OGF << 10 | OCF). */
#define GC_HCI_ACCEPT_CONNECTION 0x0409u
#define GC_HCI_LINK_KEY_REPLY 0x040Bu
#define GC_HCI_LINK_KEY_NEGATIVE 0x040Cu
#define GC_HCI_PIN_NEGATIVE 0x040Eu
#define GC_HCI_AUTHENTICATE 0x0411u
#define GC_HCI_RESET 0x0C03u
#define GC_HCI_READ_STORED_KEYS 0x0C0Du
#define GC_HCI_WRITE_SCAN_ENABLE 0x0C1Au

/* HCI events. */
#define GC_EV_CONNECTION_COMPLETE 0x03u
#define GC_EV_CONNECTION_REQUEST 0x04u
#define GC_EV_DISCONNECTED 0x05u
#define GC_EV_COMMAND_COMPLETE 0x0Eu
#define GC_EV_COMMAND_STATUS 0x0Fu
#define GC_EV_RETURN_LINK_KEYS 0x15u
#define GC_EV_PIN_REQUEST 0x16u
#define GC_EV_LINK_KEY_REQUEST 0x17u

/* L2CAP signalling. */
#define GC_L2_SIGNALLING 0x0001u
#define GC_L2_CONNECT_REQ 0x02u
#define GC_L2_CONNECT_RSP 0x03u
#define GC_L2_CONFIG_REQ 0x04u
#define GC_L2_CONFIG_RSP 0x05u
#define GC_L2_DISCONNECT_REQ 0x06u
#define GC_L2_DISCONNECT_RSP 0x07u
#define GC_L2_INFO_REQ 0x0Au
#define GC_L2_INFO_RSP 0x0Bu
#define GC_PSM_HID_CONTROL 0x11u
#define GC_PSM_HID_INTERRUPT 0x13u
/* Our end of each remote's channels: control 0x40 + 2 * slot, interrupt one more. */
#define GC_BT_LOCAL_CID 0x40u

static uint32_t bt_phys(const void* p) { return (uint32_t)(uintptr_t)p & 0x3FFFFFFFu; }
static uint32_t bt_le16(const uint8_t* p) { return p[0] | ((uint32_t)p[1] << 8); }
static void bt_put16(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void bt_copy(uint8_t* dst, const uint8_t* src, uint32_t n) {
    while (n--) *dst++ = *src++;
}
static int bt_same(const uint8_t* a, const uint8_t* b, uint32_t n) {
    while (n--)
        if (*a++ != *b++) return 0;
    return 1;
}

/* --- IOS requests --------------------------------------------------- */

static int bt_claim(struct gc_bt_rq* rq, uint32_t reply) {
    if (rq->state != GC_BT_RQ_SENT || bt_phys(rq->ipc) != reply) return 0;
    gc_invalidate((uint32_t)(uintptr_t)rq->ipc, sizeof(rq->ipc));
    if (rq->data) gc_invalidate(rq->data, rq->length);
    rq->result = (int32_t)rq->ipc[1];
    rq->state = GC_BT_RQ_DONE;
    return 1;
}

void gc_bt_on_reply(struct gc_bt* bt, uint32_t reply) {
    uint32_t i;
    if (bt_claim(&bt->write, reply)) return;
    for (i = 0; i < GC_BT_EVENT_READS; ++i)
        if (bt_claim(&bt->events[i], reply)) return;
    for (i = 0; i < GC_BT_ACL_READS; ++i)
        if (bt_claim(&bt->acl[i], reply)) return;
}

/* Takes every reply IOS has ready. */
static void bt_take_replies(struct gc_bt* bt) {
    uint32_t reply;
    while ((reply = gc_ipc_take()) != 0) gc_bt_on_reply(bt, reply);
}

static int bt_send(struct gc_bt* bt, struct gc_bt_rq* rq) {
    gc_flush((uint32_t)(uintptr_t)rq, 160);  /* ipc, vec and setup */
    if (rq->data) gc_invalidate(rq->data, rq->length);  /* IOS writes there, or has the flushed bytes */
    rq->state = GC_BT_RQ_SENT;
    if (gc_ipc_send(bt_phys(rq->ipc), bt) != 0) {
        rq->state = GC_BT_RQ_IDLE;
        bt->last_error = -1000;
        return 0;
    }
    return 1;
}

static void bt_ioctlv(struct gc_bt_rq* rq, int32_t fd, uint32_t ioctl, uint32_t in_count, uint32_t io_count) {
    uint32_t i;
    for (i = 0; i < 16; ++i) rq->ipc[i] = 0;
    rq->ipc[0] = GC_IOS_IOCTLV;
    rq->ipc[2] = (uint32_t)fd;
    rq->ipc[3] = ioctl;
    rq->ipc[4] = in_count;
    rq->ipc[5] = io_count;
    rq->ipc[6] = bt_phys(rq->vec);
}

/* A bulk or interrupt transfer on `endpoint` of `length` bytes at `data`. */
static void bt_transfer(struct gc_bt* bt, struct gc_bt_rq* rq, uint32_t ioctl, uint32_t endpoint, uint8_t* data,
                        uint32_t length) {
    rq->setup[0] = (uint8_t)endpoint;
    rq->setup[4] = (uint8_t)(length >> 8);  /* big-endian here, unlike the control fields */
    rq->setup[5] = (uint8_t)length;
    rq->vec[0] = bt_phys(&rq->setup[0]);
    rq->vec[1] = 1;
    rq->vec[2] = bt_phys(&rq->setup[4]);
    rq->vec[3] = 2;
    rq->vec[4] = bt_phys(data);
    rq->vec[5] = length;
    bt_ioctlv(rq, bt->fd, ioctl, 2, 1);
}

/* Waits for the write request, serving the other replies meanwhile. */
static int bt_wait_write(struct gc_bt* bt) {
    uint32_t polls;
    for (polls = 0; bt->write.state == GC_BT_RQ_SENT; ++polls) {
        if (polls == GC_BT_WRITE_POLLS) {
            bt->stage = GC_BT_STAGE_FAILED;  /* its buffers stay IOS's: nothing more goes out */
            bt->last_error = -1001;
            return 0;
        }
        bt_take_replies(bt);
    }
    bt->write.state = GC_BT_RQ_IDLE;
    if (bt->write.result < 0) bt->last_error = bt->write.result;
    return bt->write.result >= 0;
}

/* --- HCI ------------------------------------------------------------ */

/* Queues a command (opcode, parameters); the controller takes them one
 * by one as it gives credit back. */
static void bt_command(struct gc_bt* bt, uint32_t opcode, const uint8_t* params, uint32_t length) {
    uint8_t* c;
    if (bt->command_count == GC_BT_COMMANDS || length > 29) return;
    c = bt->commands[(bt->command_head + bt->command_count) % GC_BT_COMMANDS];
    bt_put16(c, opcode);
    c[2] = (uint8_t)length;
    bt_copy(c + 3, params, length);
    bt->command_count++;
}

static void bt_send_commands(struct gc_bt* bt) {
    while (bt->credits && bt->command_count && bt->stage != GC_BT_STAGE_FAILED) {
        const uint8_t* c = bt->commands[bt->command_head];
        const uint32_t length = 3u + c[2];
        struct gc_bt_rq* rq = &bt->write;
        bt_copy(bt->out, c, length);
        /* A class request to the device (bmRequestType 0x20), the fields
         * little-endian as USB has them. */
        rq->setup[0] = 0x20;
        rq->setup[4] = 0;
        rq->setup[8] = 0;
        rq->setup[9] = 0;
        rq->setup[12] = 0;
        rq->setup[13] = 0;
        bt_put16(&rq->setup[16], length);
        rq->setup[20] = 0;
        rq->vec[0] = bt_phys(&rq->setup[0]);
        rq->vec[1] = 1;
        rq->vec[2] = bt_phys(&rq->setup[4]);
        rq->vec[3] = 1;
        rq->vec[4] = bt_phys(&rq->setup[8]);
        rq->vec[5] = 2;
        rq->vec[6] = bt_phys(&rq->setup[12]);
        rq->vec[7] = 2;
        rq->vec[8] = bt_phys(&rq->setup[16]);
        rq->vec[9] = 2;
        rq->vec[10] = bt_phys(&rq->setup[20]);
        rq->vec[11] = 1;
        rq->vec[12] = bt_phys(bt->out);
        rq->vec[13] = length;
        bt_ioctlv(rq, bt->fd, GC_USB_CTRLMSG, 6, 1);
        rq->data = 0;
        gc_flush((uint32_t)(uintptr_t)bt->out, sizeof(bt->out));
        bt->command_head = (bt->command_head + 1) % GC_BT_COMMANDS;
        bt->command_count--;
        bt->credits--;
        if (!bt_send(bt, rq) || !bt_wait_write(bt)) return;
    }
}

/* One L2CAP packet to a remote: the ACL header, the L2CAP header, then
 * `payload`. */
static void bt_acl(struct gc_bt* bt, uint32_t handle, uint32_t cid, const uint8_t* payload, uint32_t length) {
    struct gc_bt_rq* rq = &bt->write;
    if (length > sizeof(bt->out) - 8 || bt->stage == GC_BT_STAGE_FAILED) return;
    bt_put16(bt->out, handle | 0x2000u);  /* the first packet of a message */
    bt_put16(bt->out + 2, length + 4);
    bt_put16(bt->out + 4, length);
    bt_put16(bt->out + 6, cid);
    bt_copy(bt->out + 8, payload, length);
    gc_flush((uint32_t)(uintptr_t)bt->out, sizeof(bt->out));
    bt_transfer(bt, rq, GC_USB_BLKMSG, GC_BT_EP_ACL_OUT, bt->out, length + 8);
    rq->data = 0;
    if (bt_send(bt, rq)) bt_wait_write(bt);
}

static struct gc_bt_remote* bt_by_handle(struct gc_bt* bt, uint32_t handle) {
    uint32_t i;
    for (i = 0; i < GC_BT_REMOTES; ++i)
        if (bt->remotes[i].used && bt->remotes[i].handle == handle) return &bt->remotes[i];
    return 0;
}

/* The start-up: Reset, Read_Stored_Link_Key, Write_Scan_Enable, one
 * after the other as each completes (or, should a reply not come or not
 * be understood, after about half a second). */
static uint32_t bt_step_opcode(const struct gc_bt* bt) {
    if (bt->stage == GC_BT_STAGE_RESET) return GC_HCI_RESET;
    if (bt->stage == GC_BT_STAGE_KEYS) return GC_HCI_READ_STORED_KEYS;
    if (bt->stage == GC_BT_STAGE_SCAN) return GC_HCI_WRITE_SCAN_ENABLE;
    return 0xFFFFFFFFu;
}

static void bt_next_step(struct gc_bt* bt) {
    uint8_t params[7];
    uint32_t i;
    bt->stage_polls = 0;
    if (bt->stage == GC_BT_STAGE_RESET) {
        for (i = 0; i < 6; ++i) params[i] = 0;
        params[6] = 1;  /* all of them */
        bt_command(bt, GC_HCI_READ_STORED_KEYS, params, 7);
        bt->stage = GC_BT_STAGE_KEYS;
    } else if (bt->stage == GC_BT_STAGE_KEYS) {
        params[0] = 0x02;  /* page scan: synced remotes may connect; not discoverable */
        bt_command(bt, GC_HCI_WRITE_SCAN_ENABLE, params, 1);
        bt->stage = GC_BT_STAGE_SCAN;
    } else if (bt->stage == GC_BT_STAGE_SCAN) {
        bt->stage = GC_BT_STAGE_READY;
    }
}

static void bt_on_event(struct gc_bt* bt, const uint8_t* e, uint32_t length) {
    const uint32_t code = e[0];
    const uint8_t* p = e + 2;
    uint8_t reply[24];
    uint32_t i;
    if (bt->event_count < 4) {
        bt_copy(bt->event_log[bt->event_count], e, 8);
        bt->event_lengths |= (length > 255 ? 255 : length) << (24 - 8 * bt->event_count);
    }
    bt->event_count++;
    if (length < 2 || length < 2u + e[1]) return;
    bt->last_event = code << 16 | (length > 5 ? bt_le16(e + 3) : 0);
    if (code == GC_EV_COMMAND_COMPLETE || code == GC_EV_COMMAND_STATUS) {
        const uint32_t opcode = code == GC_EV_COMMAND_COMPLETE ? bt_le16(p + 1) : bt_le16(p + 2);
        const uint32_t status = code == GC_EV_COMMAND_COMPLETE ? p[3] : p[0];
        bt->credits = code == GC_EV_COMMAND_COMPLETE ? p[0] : p[1];
        if (code == GC_EV_COMMAND_COMPLETE && status != 0) bt->last_error = (int32_t)(0x10000u | opcode << 8 | status);
        if (code == GC_EV_COMMAND_COMPLETE && opcode == bt_step_opcode(bt)) bt_next_step(bt);
        return;
    }
    if (code == GC_EV_RETURN_LINK_KEYS) {
        for (i = 0; i < p[0] && bt->key_count < GC_BT_KEYS; ++i) bt_copy(bt->keys[bt->key_count++], p + 1 + 22 * i, 22);
        return;
    }
    if (code == GC_EV_CONNECTION_REQUEST) {
        bt_copy(reply, p, 6);
        reply[6] = 0x00;  /* become the master: several remotes share this end */
        bt_command(bt, GC_HCI_ACCEPT_CONNECTION, reply, 7);
        return;
    }
    if (code == GC_EV_CONNECTION_COMPLETE) {
        struct gc_bt_remote* r;
        uint32_t slot;
        if (p[0] != 0) return;
        slot = gc_ports_slot_for(bt->remotes, p + 3);
        if (slot >= GC_BT_REMOTES) return;  /* four already: it waits for one to leave */
        r = &bt->remotes[slot];
        {
            uint8_t* clear = (uint8_t*)r;
            for (i = 0; i < sizeof(*r); ++i) clear[i] = 0;
        }
        r->used = 1;
        r->handle = bt_le16(p + 1) & 0x0FFFu;
        bt_copy(r->bdaddr, p + 3, 6);
        bt->connects++;
        /* As the menu's stack does: authenticate with the stored key. */
        bt_put16(reply, r->handle);
        bt_command(bt, GC_HCI_AUTHENTICATE, reply, 2);
        return;
    }
    if (code == GC_EV_DISCONNECTED) {
        struct gc_bt_remote* r = bt_by_handle(bt, bt_le16(p + 1) & 0x0FFFu);
        if (p[0] == 0 && r) r->used = 0;
        return;
    }
    if (code == GC_EV_LINK_KEY_REQUEST) {
        for (i = 0; i < bt->key_count; ++i)
            if (bt_same(bt->keys[i], p, 6)) {
                bt_copy(reply, bt->keys[i], 22);
                bt_command(bt, GC_HCI_LINK_KEY_REPLY, reply, 22);
                return;
            }
        bt_copy(reply, p, 6);
        bt_command(bt, GC_HCI_LINK_KEY_NEGATIVE, reply, 6);
        return;
    }
    if (code == GC_EV_PIN_REQUEST) {
        /* Syncing is the Wii Menu's job; a remote that is not synced is refused. */
        bt_copy(reply, p, 6);
        bt_command(bt, GC_HCI_PIN_NEGATIVE, reply, 6);
    }
}

/* --- The remote ----------------------------------------------------- */

/* An output report: 0xA2, then `data`, on the interrupt channel. */
static void bt_report(struct gc_bt* bt, const struct gc_bt_remote* r, const uint8_t* data, uint32_t length) {
    uint8_t packet[24];
    packet[0] = 0xA2;
    bt_copy(packet + 1, data, length);
    bt_acl(bt, r->handle, r->intr_cid, packet, length + 1);
}

/* A write of one byte to the extension's registers (0x04: registers,
 * 0xA400xx). */
static void bt_ext_write(struct gc_bt* bt, const struct gc_bt_remote* r, uint32_t reg, uint32_t value) {
    uint8_t w[22];
    uint32_t i;
    for (i = 0; i < sizeof(w); ++i) w[i] = 0;
    w[0] = 0x16;
    w[1] = 0x04;
    w[2] = 0xA4;
    w[3] = 0x00;
    w[4] = (uint8_t)reg;
    w[5] = 1;
    w[6] = (uint8_t)value;
    bt_report(bt, r, w, sizeof(w));
}

/* The report mode: buttons only, or buttons and eight extension bytes;
 * sent only when something changes (the remote then reports at once). */
static void bt_set_mode(struct gc_bt* bt, const struct gc_bt_remote* r) {
    uint8_t m[3];
    m[0] = 0x12;
    m[1] = 0x00;
    m[2] = r->ext_step == 4 ? 0x32 : 0x30;
    bt_report(bt, r, m, 3);
}

static void bt_on_input(struct gc_bt* bt, struct gc_bt_remote* r, const uint8_t* p, uint32_t length) {
    const uint32_t id = p[1];
    uint8_t q[7];
    if (length < 4 || p[0] != 0xA1) return;
    if (id >= 0x20 && id <= 0x3F && id != 0x3D) {
        r->core[0] = p[2];
        r->core[1] = p[3];
    }
    if (id == 0x20 && length >= 8) {
        /* A status report: the extension may have come or gone. The
         * remote stops reporting until the mode is set again. */
        const uint32_t plugged = p[4] & 0x02u;
        if (!plugged) {
            r->ext_step = 0;
            r->ext_kind = GC_BT_EXT_NONE;
        }
        if (plugged && r->ext_step == 0) {
            r->ext_step = 1;
            bt_ext_write(bt, r, 0xF0, 0x55);
            return;
        }
        if (plugged && r->ext_step != 4) return;  /* starting it */
        bt_set_mode(bt, r);
        return;
    }
    if (id == 0x22 && length >= 6 && p[4] == 0x16) {
        if (p[5] != 0) {
            r->ext_step = 0;  /* unplugged meanwhile: the next status starts again */
            return;
        }
        if (r->ext_step == 1) {
            r->ext_step = 2;
            bt_ext_write(bt, r, 0xFB, 0x00);
        } else if (r->ext_step == 2) {
            r->ext_step = 3;
            q[0] = 0x17;
            q[1] = 0x04;
            q[2] = 0xA4;
            q[3] = 0x00;
            q[4] = 0xFA;
            q[5] = 0x00;
            q[6] = 0x06;
            bt_report(bt, r, q, 7);
        }
        return;
    }
    if (id == 0x21 && length >= 13 && r->ext_step == 3) {
        /* The extension's ID: ends 00 00 for a Nunchuk, 01 01 for a
         * Classic Controller (Pro). */
        const uint32_t kind = p[11] << 8 | p[12];
        r->ext_kind = kind == 0x0000 ? GC_BT_EXT_NUNCHUK : kind == 0x0101 ? GC_BT_EXT_CLASSIC : GC_BT_EXT_OTHER;
        r->ext_step = (p[4] & 0x0F) == 0 ? 4 : 0;
        /* Until the first report with the extension's bytes: sticks in the
         * middle and every button up (active low), not the zeros or another
         * extension's bytes, which read as everything pressed. */
        r->ext[0] = r->ext[1] = 0x80;
        r->ext[2] = r->ext[3] = 0x80;
        r->ext[4] = r->ext[5] = 0xFF;
        bt_set_mode(bt, r);
        return;
    }
    if (id == 0x32 && length >= 10) bt_copy(r->ext, p + 4, 6);
}

/* The remote's half of the HID channels: it opens both (control, then
 * interrupt), each configured both ways. */
static void bt_on_signal(struct gc_bt* bt, struct gc_bt_remote* r, uint32_t slot, const uint8_t* s, uint32_t length) {
    while (length >= 4) {
        const uint32_t code = s[0], ident = s[1], size = bt_le16(s + 2);
        const uint8_t* d = s + 4;
        uint8_t out[16];
        if (size + 4 > length) return;
        if (code == GC_L2_CONNECT_REQ && size >= 4) {
            const uint32_t psm = bt_le16(d), theirs = bt_le16(d + 2);
            const uint32_t intr = psm == GC_PSM_HID_INTERRUPT;
            const uint32_t ours = GC_BT_LOCAL_CID + 2 * slot + intr;
            const uint32_t known = psm == GC_PSM_HID_CONTROL || intr;
            out[0] = GC_L2_CONNECT_RSP;
            out[1] = (uint8_t)ident;
            bt_put16(out + 2, 8);
            bt_put16(out + 4, known ? ours : 0);
            bt_put16(out + 6, theirs);
            bt_put16(out + 8, known ? 0 : 2);  /* 2: that PSM is not served */
            bt_put16(out + 10, 0);
            bt_acl(bt, r->handle, GC_L2_SIGNALLING, out, 12);
            if (known) {
                if (intr) r->intr_cid = theirs;
                else r->ctrl_cid = theirs;
                out[0] = GC_L2_CONFIG_REQ;  /* ours: no options (the default MTU) */
                bt->ident = bt->ident % 255 + 1;
                out[1] = (uint8_t)bt->ident;
                bt_put16(out + 2, 4);
                bt_put16(out + 4, theirs);
                bt_put16(out + 6, 0);
                bt_acl(bt, r->handle, GC_L2_SIGNALLING, out, 8);
            }
        } else if (code == GC_L2_CONFIG_REQ && size >= 4) {
            const uint32_t ours = bt_le16(d);
            const uint32_t intr = ours == GC_BT_LOCAL_CID + 2 * slot + 1;
            out[0] = GC_L2_CONFIG_RSP;  /* whatever it asks for, accepted */
            out[1] = (uint8_t)ident;
            bt_put16(out + 2, 6);
            bt_put16(out + 4, intr ? r->intr_cid : r->ctrl_cid);
            bt_put16(out + 6, 0);
            bt_put16(out + 8, 0);
            bt_acl(bt, r->handle, GC_L2_SIGNALLING, out, 10);
            r->chans |= intr ? GC_BT_INTR_THEIRS : GC_BT_CTRL_THEIRS;
        } else if (code == GC_L2_CONFIG_RSP && size >= 6) {
            const uint32_t ours = bt_le16(d);
            if (bt_le16(d + 4) == 0)
                r->chans |= ours == GC_BT_LOCAL_CID + 2 * slot + 1 ? GC_BT_INTR_OURS : GC_BT_CTRL_OURS;
        } else if (code == GC_L2_DISCONNECT_REQ && size >= 4) {
            out[0] = GC_L2_DISCONNECT_RSP;
            out[1] = (uint8_t)ident;
            bt_put16(out + 2, 4);
            bt_copy(out + 4, d, 4);
            bt_acl(bt, r->handle, GC_L2_SIGNALLING, out, 8);
            r->ready = 0;
            r->chans = 0;
        } else if (code == GC_L2_INFO_REQ && size >= 2) {
            out[0] = GC_L2_INFO_RSP;
            out[1] = (uint8_t)ident;
            bt_put16(out + 2, 4);
            bt_copy(out + 4, d, 2);
            bt_put16(out + 6, 1);  /* not supported */
            bt_acl(bt, r->handle, GC_L2_SIGNALLING, out, 8);
        }
        s += 4 + size;
        length -= 4 + size;
    }
    if (!r->ready && r->chans == GC_BT_CHANS_READY) {
        /* Its port's LED, then a status report to learn of an extension. */
        uint8_t led[2];
        r->ready = 1;
        led[0] = 0x11;
        led[1] = (uint8_t)(0x10u << slot);
        r->led_port = (uint8_t)slot;
        bt_report(bt, r, led, 2);
        led[0] = 0x15;
        led[1] = 0x00;
        bt_report(bt, r, led, 2);
    }
}

static void bt_on_acl(struct gc_bt* bt, const uint8_t* a, uint32_t length) {
    const uint32_t handle = bt_le16(a) & 0x0FFFu;
    const uint32_t flags = bt_le16(a) >> 12;
    struct gc_bt_remote* r = bt_by_handle(bt, handle);
    uint32_t l2_length, cid, slot;
    if (length < 8 || r == 0 || (flags & 3u) == 1u) return;  /* continuations: none expected this small */
    bt->acl_count++;
    l2_length = bt_le16(a + 4);
    cid = bt_le16(a + 6);
    if (l2_length + 8 > length) return;
    slot = (uint32_t)(r - bt->remotes);
    if (cid == GC_L2_SIGNALLING) bt_on_signal(bt, r, slot, a + 8, l2_length);
    else if (cid == GC_BT_LOCAL_CID + 2 * slot + 1) bt_on_input(bt, r, a + 8, l2_length);
}

/* --- Driving it ----------------------------------------------------- */

static void bt_read(struct gc_bt* bt, struct gc_bt_rq* rq, uint32_t ioctl, uint32_t endpoint, uint8_t* buf) {
    bt_transfer(bt, rq, ioctl, endpoint, buf, GC_BT_READ_BYTES);
    rq->data = (uint32_t)(uintptr_t)buf;
    rq->length = GC_BT_READ_BYTES;
    bt_send(bt, rq);
}

static void bt_open(struct gc_bt* bt) {
    struct gc_bt_rq* rq = &bt->write;
    uint32_t* path = (uint32_t*)bt->path;
    uint32_t i;
    path[0] = 0x2F646576u;  /* "/dev/usb/oh1/57e/305" */
    path[1] = 0x2F757362u;
    path[2] = 0x2F6F6831u;
    path[3] = 0x2F353765u;
    path[4] = 0x2F333035u;
    path[5] = 0;
    gc_flush((uint32_t)(uintptr_t)bt->path, sizeof(bt->path));
    for (i = 0; i < 16; ++i) rq->ipc[i] = 0;
    rq->ipc[0] = GC_IOS_OPEN;
    rq->ipc[3] = bt_phys(bt->path);
    rq->data = 0;
    if (!bt_send(bt, rq) || !bt_wait_write(bt)) {
        bt->stage = GC_BT_STAGE_FAILED;
        return;
    }
    bt->fd = rq->result;
    if (bt->fd < 0) {
        bt->last_error = bt->fd;
        bt->stage = GC_BT_STAGE_FAILED;
        return;
    }
    for (i = 0; i < GC_BT_EVENT_READS; ++i) bt_read(bt, &bt->events[i], GC_USB_INTRMSG, GC_BT_EP_EVENTS, bt->event_buf[i]);
    for (i = 0; i < GC_BT_ACL_READS; ++i) bt_read(bt, &bt->acl[i], GC_USB_BLKMSG, GC_BT_EP_ACL_IN, bt->acl_buf[i]);
    bt->credits = 1;
    bt_command(bt, GC_HCI_RESET, bt->path, 0);
    bt->stage = GC_BT_STAGE_RESET;
}

/* A remote that plays on another port than its LED shows (a GameCube
 * controller took its own, gc_ports.h) gets that port's LED, so the player
 * sees which port it is. gc_bt_fill chose the ports last frame; a remote
 * that became ready since has none yet and keeps its slot's LED. */
static void bt_update_leds(struct gc_bt* bt) {
    uint32_t i;
    for (i = 0; i < GC_BT_REMOTES && bt->stage != GC_BT_STAGE_FAILED; ++i) {
        struct gc_bt_remote* r = &bt->remotes[i];
        const uint32_t port = bt->port_of[i];
        uint8_t led[2];
        if (!r->used || !r->ready || port >= GC_PORTS || port == r->led_port) continue;
        led[0] = 0x11;
        led[1] = (uint8_t)(0x10u << port);
        r->led_port = (uint8_t)port;
        bt_report(bt, r, led, 2);
    }
}

void gc_bt_poll(struct gc_rt_context* ctx, struct gc_bt* bt) {
    uint32_t i, msr;
    if (bt->stage == GC_BT_STAGE_FAILED) return;
    msr = gc_irq_off();
    if (ctx->busy) {  /* a disc read is waiting on IOS: next frame */
        gc_irq_restore(msr);
        return;
    }
    ctx->busy = 1;
    bt->polls++;
    if (bt->stage == GC_BT_STAGE_CLOSED) bt_open(bt);
    bt_take_replies(bt);
    if (bt->stage == GC_BT_STAGE_READY) bt_update_leds(bt);
    for (i = 0; i < GC_BT_EVENT_READS && bt->stage != GC_BT_STAGE_FAILED; ++i) {
        struct gc_bt_rq* rq = &bt->events[i];
        if (rq->state != GC_BT_RQ_DONE) continue;
        if (rq->result > 0) bt_on_event(bt, bt->event_buf[i], (uint32_t)rq->result);
        else bt->last_error = rq->result;
        bt_read(bt, rq, GC_USB_INTRMSG, GC_BT_EP_EVENTS, bt->event_buf[i]);
    }
    for (i = 0; i < GC_BT_ACL_READS && bt->stage != GC_BT_STAGE_FAILED; ++i) {
        struct gc_bt_rq* rq = &bt->acl[i];
        if (rq->state != GC_BT_RQ_DONE) continue;
        if (rq->result > 0) bt_on_acl(bt, bt->acl_buf[i], (uint32_t)rq->result);
        else bt->last_error = rq->result;
        bt_read(bt, rq, GC_USB_BLKMSG, GC_BT_EP_ACL_IN, bt->acl_buf[i]);
    }
    if (bt->stage >= GC_BT_STAGE_RESET && bt->stage <= GC_BT_STAGE_SCAN && ++bt->stage_polls == 30) {
        bt->credits = 1;
        bt_next_step(bt);
    }
    bt_send_commands(bt);
    ctx->busy = 0;
    gc_guard(ctx);
    gc_irq_restore(msr);
}

/* --- The GameCube side ---------------------------------------------- */

/* PADStatus (12 bytes): buttons, stick x/y, C-stick x/y, L, R, analog A,
 * B, err. */
#define GC_PAD_LEFT 0x0001u
#define GC_PAD_RIGHT 0x0002u
#define GC_PAD_DOWN 0x0004u
#define GC_PAD_UP 0x0008u
#define GC_PAD_Z 0x0010u
#define GC_PAD_R 0x0020u
#define GC_PAD_L 0x0040u
#define GC_PAD_A 0x0100u
#define GC_PAD_B 0x0200u
#define GC_PAD_X 0x0400u
#define GC_PAD_Y 0x0800u
#define GC_PAD_START 0x1000u
#define GC_PAD_NO_CONTROLLER 0xFFu  /* err -1 */
#define GC_STICK_FULL 100

static int32_t bt_clamp(int32_t v) { return v < -127 ? -127 : v > 127 ? 127 : v; }

/* The remote's buttons (first byte: left 0x01, right 0x02, down 0x04,
 * up 0x08, plus 0x10; second: two 0x01, one 0x02, B 0x04, A 0x08, minus
 * 0x10, home 0x80), the D-pad as the remote is held: upright with an
 * extension, sideways (D-pad on the left) without. */
static void bt_fill_one(const struct gc_bt_remote* r, uint8_t* pad) {
    const uint32_t b0 = r->core[0], b1 = r->core[1];
    uint32_t buttons = 0;
    int32_t sx = 0, sy = 0, cx = 0, cy = 0;
    uint32_t l = 0, rt = 0;
    if (b0 & 0x10u) buttons |= GC_PAD_START;
    if (b1 & 0x10u) buttons |= GC_PAD_Z;
    if (r->ext_kind == GC_BT_EXT_CLASSIC && r->ext_step == 4) {
        /* Active low; the sticks and triggers packed into the first four bytes. */
        const uint32_t e4 = ~(uint32_t)r->ext[4] & 0xFFu, e5 = ~(uint32_t)r->ext[5] & 0xFFu;
        const int32_t lx = r->ext[0] & 0x3F, ly = r->ext[1] & 0x3F;
        const int32_t rx = ((r->ext[0] & 0xC0) >> 3) | ((r->ext[1] & 0xC0) >> 5) | ((r->ext[2] & 0x80) >> 7);
        const int32_t ry = r->ext[2] & 0x1F;
        const uint32_t lt = ((r->ext[2] & 0x60u) >> 2) | ((r->ext[3] & 0xE0u) >> 5);
        const uint32_t rtv = r->ext[3] & 0x1Fu;
        buttons = 0;
        if (e5 & 0x10u) buttons |= GC_PAD_A;
        if (e5 & 0x40u) buttons |= GC_PAD_B;
        if (e5 & 0x08u) buttons |= GC_PAD_X;
        if (e5 & 0x20u) buttons |= GC_PAD_Y;
        if (e5 & 0x84u) buttons |= GC_PAD_Z;  /* ZL or ZR */
        if (e4 & 0x04u) buttons |= GC_PAD_START;
        if (e4 & 0x20u) buttons |= GC_PAD_L;
        if (e4 & 0x02u) buttons |= GC_PAD_R;
        if (e5 & 0x01u) buttons |= GC_PAD_UP;
        if (e4 & 0x40u) buttons |= GC_PAD_DOWN;
        if (e5 & 0x02u) buttons |= GC_PAD_LEFT;
        if (e4 & 0x80u) buttons |= GC_PAD_RIGHT;
        sx = (lx - 32) * 3;
        sy = (ly - 32) * 3;
        cx = (rx - 16) * 6;
        cy = (ry - 16) * 6;
        l = lt * 8;
        rt = rtv * 8;
    } else if (r->ext_kind == GC_BT_EXT_NUNCHUK && r->ext_step == 4) {
        if (b1 & 0x08u) buttons |= GC_PAD_A;
        if (b1 & 0x04u) buttons |= GC_PAD_B;
        if (b1 & 0x02u) buttons |= GC_PAD_X;
        if (b1 & 0x01u) buttons |= GC_PAD_Y;
        if (!(r->ext[5] & 0x01u)) buttons |= GC_PAD_R;  /* Z, active low */
        if (!(r->ext[5] & 0x02u)) buttons |= GC_PAD_L;  /* C */
        sx = (int32_t)r->ext[0] - 128;
        sy = (int32_t)r->ext[1] - 128;
        /* The D-pad as the C-stick (the camera in most games). */
        if (b0 & 0x01u) cx = -GC_STICK_FULL;
        if (b0 & 0x02u) cx = GC_STICK_FULL;
        if (b0 & 0x08u) cy = GC_STICK_FULL;
        if (b0 & 0x04u) cy = -GC_STICK_FULL;
    } else {
        /* Sideways: 2 is A, 1 is B, the trigger R, A is X, home Y; the
         * D-pad (its up now points left) is the stick and the D-pad. */
        if (b1 & 0x01u) buttons |= GC_PAD_A;
        if (b1 & 0x02u) buttons |= GC_PAD_B;
        if (b1 & 0x04u) buttons |= GC_PAD_R;
        if (b1 & 0x08u) buttons |= GC_PAD_X;
        if (b1 & 0x80u) buttons |= GC_PAD_Y;
        if (b0 & 0x08u) { buttons |= GC_PAD_LEFT; sx = -GC_STICK_FULL; }
        if (b0 & 0x04u) { buttons |= GC_PAD_RIGHT; sx = GC_STICK_FULL; }
        if (b0 & 0x02u) { buttons |= GC_PAD_UP; sy = GC_STICK_FULL; }
        if (b0 & 0x01u) { buttons |= GC_PAD_DOWN; sy = -GC_STICK_FULL; }
    }
    if (buttons & GC_PAD_L) l = 0xFF;
    if (buttons & GC_PAD_R) rt = 0xFF;
    pad[0] = (uint8_t)(buttons >> 8);
    pad[1] = (uint8_t)buttons;
    pad[2] = (uint8_t)bt_clamp(sx);
    pad[3] = (uint8_t)bt_clamp(sy);
    pad[4] = (uint8_t)bt_clamp(cx);
    pad[5] = (uint8_t)bt_clamp(cy);
    pad[6] = (uint8_t)(l > 0xFF ? 0xFF : l);
    pad[7] = (uint8_t)(rt > 0xFF ? 0xFF : rt);
    pad[8] = 0;
    pad[9] = 0;
    pad[10] = 0;
}

int gc_bt_home_held(const struct gc_bt* bt) {
    uint32_t i;
    for (i = 0; i < GC_BT_REMOTES; ++i) {
        const struct gc_bt_remote* r = &bt->remotes[i];
        if (!r->used || !r->ready) continue;
        /* A Wii Remote alone is held sideways and its HOME is the game's Y,
         * so it leaves with HOME and - (0x10) held together. */
        const int extension = r->ext_step == 4 && (r->ext_kind == GC_BT_EXT_NUNCHUK || r->ext_kind == GC_BT_EXT_CLASSIC);
        if ((r->core[1] & 0x80u) && (extension || (r->core[1] & 0x10u))) return 1;
        /* The Classic Controller's HOME: its fifth byte's 0x08, active low. */
        if (r->ext_kind == GC_BT_EXT_CLASSIC && r->ext_step == 4 && !(r->ext[4] & 0x08u)) return 1;
    }
    return 0;
}

void gc_bt_fill(struct gc_bt* bt, uint8_t* status) {
    uint32_t pads = 0, remotes = 0, i;
    /* The game's PADRead has just read every port: err is -1 where no
     * GameCube controller answered. Any other err (a controller still
     * starting, a transfer error) is a controller there, which keeps it. */
    for (i = 0; i < GC_PORTS; ++i)
        if (status[12 * i + 10] != GC_PAD_NO_CONTROLLER) pads |= 1u << i;
    for (i = 0; i < GC_BT_REMOTES; ++i)
        if (bt->remotes[i].used && bt->remotes[i].ready) remotes |= 1u << i;
    gc_ports_assign(pads, remotes, bt->port_of);
    for (i = 0; i < GC_BT_REMOTES; ++i) {
        const uint32_t port = bt->port_of[i];
        if (port < GC_PORTS) bt_fill_one(&bt->remotes[i], status + 12 * port);
    }
    for (i = 0; i < GC_PORTS; ++i) bt->port_err[i] = status[12 * i + 10];
}
