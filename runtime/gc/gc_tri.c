/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Triforce arcade boards for a GameCube game in Wii mode (gc_tri.h).
 * Same rules as the rest of the blob: no globals, no constant data, no
 * library calls. Everything above the RT_TARGET_PPC part is built into
 * the host tests too. */
#include "gc_tri.h"

static void tri_copy(uint8_t* dst, const uint8_t* src, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; ++i) dst[i] = src[i];
}

static void tri_fill(uint8_t* dst, uint8_t value, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; ++i) dst[i] = value;
}

/* ---- The JVS I/O board ------------------------------------------------ */

/* An answer being built: the unescaped frame (node 0 = the master, a
 * length, a status, the commands' reports), its checksum added at the end. */
struct tri_jvs_reply {
    uint8_t bytes[GC_TRI_PACKET];
    uint32_t length;
    int overflow;
};

static void jvs_put(struct tri_jvs_reply* r, uint8_t value) {
    if (r->length < sizeof(r->bytes)) r->bytes[r->length++] = value;
    else r->overflow = 1;
}

/* The frame in `escaped` (from its 0xE0 on) unescaped into `out`: returns
 * the bytes, 0 when it is cut short. */
static uint32_t jvs_unescape(const uint8_t* escaped, uint32_t length, uint8_t* out, uint32_t room) {
    uint32_t i = 1, n = 0;
    if (length == 0 || escaped[0] != GC_TRI_JVS_SYNC) return 0;
    while (i < length && n < room) {
        uint8_t b = escaped[i++];
        if (b == GC_TRI_JVS_MARK) {
            if (i >= length) return 0;
            b = (uint8_t)(escaped[i++] + 1u);
        }
        out[n++] = b;
    }
    return n;
}

/* The analog channel `ch` big-endian, the centre for a channel the
 * cabinet does not have. */
static void jvs_put_analog(struct tri_jvs_reply* r, const struct gc_tri* t, uint32_t ch) {
    const uint16_t value = ch < GC_TRI_ANALOG ? t->analog_in[ch] : (uint16_t)GC_TRI_ANALOG_CENTRE;
    jvs_put(r, (uint8_t)(value >> 8));
    jvs_put(r, (uint8_t)value);
}

static void wheel_reset(struct gc_tri* t) {
    t->wheel_state = 0;
    t->wheel_rx_len = 0;
}

/* One command of a frame for this board: its report and data into `r`.
 * Returns the bytes of the request it used (the command byte included),
 * 0 for a command the board does not know (the frame ends with status 2)
 * and -1 for one whose parameters are cut short (report 2, frame ends). */
static int32_t jvs_command(struct gc_tri* t, const uint8_t* p, uint32_t left, struct tri_jvs_reply* r) {
    const uint8_t cmd = p[0];
    uint32_t i;
    if (cmd == 0x10) {  /* identify: the board's name */
        jvs_put(r, GC_TRI_JVS_OK);
        for (i = 0; i < t->io_id_len; ++i) jvs_put(r, t->io_id[i]);
        return 1;
    }
    if (cmd == 0x11 || cmd == 0x12 || cmd == 0x13) {  /* command, JVS and communication revisions */
        jvs_put(r, GC_TRI_JVS_OK);
        jvs_put(r, cmd == 0x11 ? t->io_command_rev : cmd == 0x12 ? t->io_jvs_rev : t->io_comm_rev);
        return 1;
    }
    if (cmd == 0x14) {  /* features */
        jvs_put(r, GC_TRI_JVS_OK);
        for (i = 0; i < t->io_features_len; ++i) jvs_put(r, t->io_features[i]);
        return 1;
    }
    if (cmd == 0x15) {  /* the main board's name, given to the I/O board */
        for (i = 1; i < left; ++i)
            if (p[i] == 0) {
                jvs_put(r, GC_TRI_JVS_OK);
                return (int32_t)(i + 1);
            }
        return -1;
    }
    if (cmd == 0x20) {  /* switches: players, bytes each */
        uint32_t player, b;
        if (left < 3) return -1;
        jvs_put(r, GC_TRI_JVS_OK);
        jvs_put(r, t->system_in);
        for (player = 0; player < p[1]; ++player)
            for (b = 0; b < p[2]; ++b) jvs_put(r, player == 0 && b < 2 ? t->player_in[b] : 0);
        return 3;
    }
    if (cmd == 0x21) {  /* coins: slots, two bytes each (condition << 14 | count) */
        uint32_t slot;
        if (left < 2) return -1;
        jvs_put(r, GC_TRI_JVS_OK);
        for (slot = 0; slot < p[1]; ++slot) {
            if (slot < GC_TRI_COIN_SLOTS) {
                jvs_put(r, (uint8_t)((t->coins[slot] >> 8) & 0x3Fu));
                jvs_put(r, (uint8_t)t->coins[slot]);
            } else {
                jvs_put(r, 0x80);  /* condition 2: no counter there */
                jvs_put(r, 0);
            }
        }
        return 2;
    }
    if (cmd == 0x22) {  /* analog inputs: channels */
        uint32_t ch;
        if (left < 2) return -1;
        jvs_put(r, GC_TRI_JVS_OK);
        for (ch = 0; ch < p[1]; ++ch) jvs_put_analog(r, t, ch);
        return 2;
    }
    if (cmd == 0x23 || cmd == 0x26) {  /* rotary (2 bytes a channel), misc switches (bytes): none moving */
        if (left < 2) return -1;
        jvs_put(r, GC_TRI_JVS_OK);
        for (i = 0; i < (cmd == 0x23 ? 2u * p[1] : p[1]); ++i) jvs_put(r, 0);
        return 2;
    }
    if (cmd == 0x24) {  /* keypad: nothing pressed */
        jvs_put(r, GC_TRI_JVS_OK);
        jvs_put(r, 0);
        return 1;
    }
    if (cmd == 0x25 || cmd == 0x2E) {  /* screen position, remaining payout: zeros */
        if (left < 2) return -1;
        jvs_put(r, GC_TRI_JVS_OK);
        for (i = 0; i < 4; ++i) jvs_put(r, 0);
        return 2;
    }
    if (cmd == 0x30 || cmd == 0x35) {  /* coin counter down / up: slot (from 1), amount */
        uint32_t slot, amount;
        if (left < 4) return -1;
        slot = p[1] != 0 ? p[1] - 1u : 0;
        amount = ((uint32_t)p[2] << 8) | p[3];
        if (slot < GC_TRI_COIN_SLOTS) {
            uint32_t coins = t->coins[slot];
            if (cmd == 0x30) coins = coins > amount ? coins - amount : 0;
            else coins = coins + amount > 0x3FFFu ? 0x3FFFu : coins + amount;
            t->coins[slot] = (uint16_t)coins;
        }
        jvs_put(r, GC_TRI_JVS_OK);
        return 4;
    }
    if (cmd == 0x31 || cmd == 0x36) {  /* payout counter up / down: no hopper */
        if (left < 4) return -1;
        jvs_put(r, GC_TRI_JVS_OK);
        return 4;
    }
    if (cmd == 0x32 || cmd == 0x34) {  /* general outputs (lamps), character display: bytes */
        if (left < 2 || left < 2u + p[1]) return -1;
        /* Mario Kart: output bit 0x80 of the first byte resets the wheel. */
        if (cmd == 0x32 && p[1] != 0 && (p[2] & 0x80u)) wheel_reset(t);
        jvs_put(r, GC_TRI_JVS_OK);
        return (int32_t)(2u + p[1]);
    }
    if (cmd == 0x33) {  /* analog outputs: channels, two bytes each */
        if (left < 2 || left < 2u + 2u * p[1]) return -1;
        jvs_put(r, GC_TRI_JVS_OK);
        return (int32_t)(2u + 2u * p[1]);
    }
    if (cmd == 0x37 || cmd == 0x38) {  /* one output byte, one output bit */
        if (left < 3) return -1;
        jvs_put(r, GC_TRI_JVS_OK);
        return 3;
    }
    if (cmd == 0x70) {  /* Namco's own: 0x18 and four bytes, answered 0xFF */
        if (left < 6 || p[1] != 0x18) return -1;
        jvs_put(r, GC_TRI_JVS_OK);
        jvs_put(r, 0xFF);
        return 6;
    }
    return 0;
}

/* The frame `f` (unescaped: node, length, data, checksum). Returns 1 with
 * the answer in `r`, 0 when it gets none (a broadcast reset, another
 * node's frame, a broken one). */
static int jvs_frame(struct gc_tri* t, const uint8_t* f, uint32_t n, struct tri_jvs_reply* r) {
    uint32_t length, i, sum = 0;
    const uint8_t node = n > 0 ? f[0] : 0;
    r->length = 0;
    r->overflow = 0;
    if (n < 3) return 0;
    length = f[1];
    if (length < 1 || 2u + length > n) return 0;
    for (i = 0; i < 1u + length; ++i) sum += f[i];
    t->jvs_frames++;
    jvs_put(r, 0);  /* to the master */
    jvs_put(r, 0);  /* length, below */
    jvs_put(r, GC_TRI_JVS_OK);
    if ((uint8_t)sum != f[1u + length]) {
        r->bytes[2] = GC_TRI_JVS_CHECKSUM;
        t->jvs_errors = (t->jvs_errors + 1u) & 0x00FFFFFFu;
    } else if (node == GC_TRI_JVS_BROADCAST) {
        const uint8_t cmd = f[2];
        if (cmd == 0xF0 && length >= 3 && f[3] == 0xD9) {  /* reset: no answer */
            t->jvs_address = 0;
            t->status_switches[1] |= 1u;  /* the sense line: an address is wanted */
            wheel_reset(t);
            return 0;
        }
        if (cmd == 0xF1 && length >= 3) {  /* set address */
            t->jvs_address = f[3];
            t->status_switches[1] &= (uint8_t)~1u;
            jvs_put(r, GC_TRI_JVS_OK);
        } else {
            return 0;
        }
    } else if (node == t->jvs_address && node != 0) {
        uint32_t at = 2;
        const uint32_t end = 1u + length;  /* the checksum's place */
        while (at < end) {
            const int32_t used = jvs_command(t, f + at, end - at, r);
            if (used == 0) {
                r->length = 3;
                r->bytes[2] = GC_TRI_JVS_UNKNOWN;
                t->jvs_errors = (t->jvs_errors + 1u) | ((uint32_t)f[at] << 24);
                break;
            }
            if (used < 0) {
                jvs_put(r, 2);  /* report: parameter size */
                t->jvs_errors = (t->jvs_errors + 1u) | ((uint32_t)f[at] << 24);
                break;
            }
            at += (uint32_t)used;
        }
    } else {
        return 0;
    }
    if (r->overflow) return 0;
    r->bytes[1] = (uint8_t)(r->length - 1u);  /* status, reports and the checksum to come */
    sum = 0;
    for (i = 0; i < r->length; ++i) sum += r->bytes[i];
    jvs_put(r, (uint8_t)sum);
    return !r->overflow;
}

/* ---- The steering wheel (serial line A) ------------------------------- */

static void wheel_answer(struct gc_tri* t, uint8_t a, uint8_t b, uint8_t c) {
    if (t->wheel_tx_len + 3u > GC_TRI_WHEEL_BYTES) return;
    t->wheel_tx[t->wheel_tx_len++] = a;
    t->wheel_tx[t->wheel_tx_len++] = b;
    t->wheel_tx[t->wheel_tx_len++] = c;
}

/* Mario Kart's force-feedback wheel takes 10-byte requests (FF FF FF 01,
 * then three forces) and answers each with three letters: "E00" after it
 * powers up, then "C06" and "C01" in turn, which the game takes as the
 * motor off and on. */
static void wheel_receive(struct gc_tri* t, const uint8_t* data, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; ++i) {
        if (t->wheel_rx_len < GC_TRI_WHEEL_BYTES) t->wheel_rx[t->wheel_rx_len++] = data[i];
        if (t->wheel_rx_len < 10) continue;
        if (t->wheel_rx[0] == 0xFF && t->wheel_rx[1] == 0xFF && t->wheel_rx[2] == 0xFF && t->wheel_rx[3] == 0x01) {
            if (t->wheel_state == 0) {
                wheel_answer(t, 'E', '0', '0');
                t->wheel_state = 1;
            } else if (t->wheel_state == 1) {
                wheel_answer(t, 'C', '0', '6');
                t->wheel_state = 2;
            } else {
                wheel_answer(t, 'C', '0', '1');
                t->wheel_state = 1;
            }
        }
        t->wheel_rx_len = 0;
    }
}

/* ---- The baseboard's SI packets --------------------------------------- */

/* Room left in the answer for `n` more bytes (its last byte is the checksum). */
static int bb_room(uint32_t at, uint32_t n) { return at + n <= GC_TRI_PACKET - 1u; }

/* One 0x70 packet, its answer into `out` (128 bytes). */
static void bb_packet(struct gc_tri* t, const uint8_t* in, uint32_t in_len, uint8_t* out) {
    uint32_t at = 2, i = 2, end, k, sum = 0;
    tri_fill(out, 0, GC_TRI_PACKET);
    end = in_len >= 2 ? 2u + in[1] : 0;
    if (end > in_len) end = in_len;
    while (i < end) {
        const uint8_t cmd = in[i++];
        uint32_t data_len;
        const uint8_t* data;
        if (i >= end) break;
        /* A length byte, then that many bytes; 0x1F and 0x21 send more
         * than their length byte says (four and three). */
        data_len = cmd == GC_TRI_BB_REGION ? 4u : cmd == 0x21 ? 3u : in[i];
        data = in + i + 1;
        if (i + 1u + data_len > end) break;
        i += 1u + data_len;
        if (cmd == GC_TRI_BB_SWITCHES) {
            if (!bb_room(at, 4)) break;
            out[at++] = cmd;
            out[at++] = 2;
            out[at++] = t->status_switches[0];
            out[at++] = t->status_switches[1];
        } else if (cmd == GC_TRI_BB_SERIAL) {
            if (!bb_room(at, 18)) break;
            out[at++] = cmd;
            out[at++] = 16;
            tri_copy(out + at, t->serial, 16);
            at += 16;
        } else if (cmd == GC_TRI_BB_FIRM || cmd == GC_TRI_BB_FPGA) {
            if (!bb_room(at, 4)) break;
            out[at++] = cmd;
            out[at++] = 2;
            out[at++] = cmd == GC_TRI_BB_FIRM ? t->firm[0] : t->fpga[0];
            out[at++] = cmd == GC_TRI_BB_FIRM ? t->firm[1] : t->fpga[1];
        } else if (cmd == GC_TRI_BB_REGION) {
            if (!bb_room(at, 22)) break;
            out[at++] = cmd;
            out[at++] = 20;
            tri_copy(out + at, t->region, 20);
            at += 20;
        } else if (cmd == 0x12 || cmd == 0x14 || cmd == 0x23 || cmd == 0x24) {
            /* Acknowledged with no data. */
            if (!bb_room(at, 2)) break;
            out[at++] = cmd;
            out[at++] = 0;
        } else if (cmd == 0x21 || cmd == 0x22 || cmd == 0x60 || cmd == GC_TRI_BB_SERIAL_B) {
            /* No answer: settings, the card reader's line (no reader). */
        } else if (cmd == GC_TRI_BB_SERIAL_A) {
            wheel_receive(t, data, data_len);
        } else if ((cmd & 0xE0u) == GC_TRI_BB_JVS) {
            uint8_t frame[GC_TRI_PACKET];
            struct tri_jvs_reply reply;
            const uint32_t n = jvs_unescape(data, data_len, frame, sizeof(frame));
            if (n != 0 && jvs_frame(t, frame, n, &reply)) {
                /* Escaped behind a 0xE0, as the game sent its own. */
                uint32_t escaped = 1;
                for (k = 0; k < reply.length; ++k)
                    escaped += reply.bytes[k] == GC_TRI_JVS_SYNC || reply.bytes[k] == GC_TRI_JVS_MARK ? 2u : 1u;
                if (!bb_room(at, 2u + escaped)) break;
                out[at++] = cmd;
                out[at++] = (uint8_t)escaped;
                out[at++] = GC_TRI_JVS_SYNC;
                for (k = 0; k < reply.length; ++k) {
                    const uint8_t b = reply.bytes[k];
                    if (b == GC_TRI_JVS_SYNC || b == GC_TRI_JVS_MARK) {
                        out[at++] = GC_TRI_JVS_MARK;
                        out[at++] = (uint8_t)(b - 1u);
                    } else {
                        out[at++] = b;
                    }
                }
            }
        } else {
            t->bb_unknown = ((t->bb_unknown + 1u) & 0x00FFFFFFu) | ((uint32_t)cmd << 24);
        }
    }
    /* What the wheel has to say, after the commands. */
    if (t->wheel_tx_len != 0 && bb_room(at, 2u + t->wheel_tx_len)) {
        out[at++] = GC_TRI_BB_SERIAL_A;
        out[at++] = t->wheel_tx_len;
        tri_copy(out + at, t->wheel_tx, t->wheel_tx_len);
        at += t->wheel_tx_len;
        t->wheel_tx_len = 0;
    }
    out[0] = 0x01;
    out[1] = (uint8_t)(at - 2u);
    for (k = 0; k < at; ++k) sum += out[k];
    out[GC_TRI_PACKET - 1u] = (uint8_t)~sum;
}

uint32_t gc_tri_si(struct gc_tri* t, const uint8_t* out, uint32_t out_len, uint8_t* in, uint32_t in_len) {
    uint32_t next, n;
    if (out_len == 0) return 0;
    if (out[0] == GC_TRI_SI_STATUS) {
        t->si_type_reads++;
        n = in_len < 3 ? in_len : 3;
        tri_copy(in, t->status_id, n);
        return n;
    }
    if (out[0] != GC_TRI_SI_PACKET) {
        t->si_unknown = ((t->si_unknown + 1u) & 0x00FFFFFFu) | ((uint32_t)out[0] << 24);
        return 0;
    }
    t->si_packets++;
    /* The answer to this packet comes with the next one; the one before's
     * goes back now. */
    next = t->ready_index ^ 1u;
    bb_packet(t, out, out_len, t->response[next]);
    n = 0;
    if (t->response_ready) {
        n = in_len < GC_TRI_PACKET ? in_len : GC_TRI_PACKET;
        tri_copy(in, t->response[t->ready_index], n);
    }
    t->ready_index = next;
    t->response_ready = 1;
    return n;
}

void gc_tri_set_pad(struct gc_tri* t, const uint8_t* pad, int ok) {
    uint32_t ch;
    uint8_t coin;
    t->system_in = 0;
    t->player_in[0] = 0;
    t->player_in[1] = 0;
    for (ch = 0; ch < GC_TRI_ANALOG; ++ch) t->analog_in[ch] = GC_TRI_ANALOG_CENTRE;
    t->analog_in[1] = 0;  /* the pedals let go */
    t->analog_in[2] = 0;
    coin = 0;
    if (ok) {
        if (pad[0] & 0x10u) t->player_in[0] |= GC_TRI_P1_START;
        if (pad[1] & 0x10u) t->player_in[0] |= GC_TRI_P1_SERVICE;  /* Z */
        if (pad[0] & 0x02u) t->player_in[1] |= GC_TRI_P1_ITEM;     /* B */
        if (pad[0] & 0x04u) t->player_in[1] |= GC_TRI_P1_CANCEL;   /* X */
        if (pad[0] & 0x01u) coin = 1;                                /* A */
        t->analog_in[0] = (uint16_t)(((uint32_t)pad[2] << 8) | pad[2]);  /* the stick: the wheel */
        t->analog_in[1] = (uint16_t)(((uint32_t)pad[7] << 8) | pad[7]);  /* R: the accelerator */
        t->analog_in[2] = (uint16_t)(((uint32_t)pad[6] << 8) | pad[6]);  /* L: the brake */
    }
    /* A coin goes in when the switch closes, as the slot counts it. */
    if (coin && !(t->coin_was_down & 1u) && t->coins[0] < 0x3FFFu) t->coins[0]++;
    t->coin_was_down = coin;
    t->coin_in = coin;
}

/* ---- The media board -------------------------------------------------- */

/* The doorbell of box `box`: its request answered (gc_tri.h). */
static void box_answer(struct gc_tri* t, uint32_t box) {
    uint8_t* a = t->box_answer[box];
    const uint8_t* q = t->box_request[box];
    const uint32_t command = ((uint32_t)q[2] << 8) | q[3];
    uint32_t i;
    if (command == 0) return;
    tri_fill(a, 0, 32);
    a[0] = q[0];
    a[1] = q[1];
    a[2] = q[2];
    a[3] = (uint8_t)(q[3] | 0x80u);
    if (command == 0x0100) {
        /* Linked: its address, 192.168.11.2 (the SDK swaps it). */
        a[4] = 2;
        a[5] = 11;
        a[6] = 168;
        a[7] = 192;
    } else if (command == 0x0101) {
        a[4] = 0x01;  /* version 1.01 */
        a[5] = 0x01;
    } else if (command == 0x0301) {
        for (i = 0; i < 16; ++i) a[4 + i] = t->board_serial[i];
    }
    tri_fill(t->box_request[box], 0, 32);
    t->irq_status |= box == GC_TRI_BOX_MEDIA ? 0x04u : 0x10u;
    t->irq_raise = 1;
    t->box_answers = ((t->box_answers + 1u) & 0xFFFFu) | (command << 16);
}

int gc_tri_is_board(uint32_t offset) {
    return offset >= GC_TRI_BOARD_STATUS || (offset >= 0x1EFF8000u && offset < 0x20000000u);
}

void gc_tri_board_read(struct gc_tri* t, uint32_t offset, uint8_t* buf, uint32_t length) {
    t->board_reads++;
    t->board_last = offset;
    tri_fill(buf, 0, length);
    if (offset == GC_TRI_MEDIA_ANSWER || offset == GC_TRI_NET_ANSWER) {
        tri_copy(buf, t->box_answer[offset == GC_TRI_NET_ANSWER], length < 32 ? length : 32);
        return;
    }
    if (offset == GC_TRI_NET_DOORBELL) {
        box_answer(t, GC_TRI_BOX_NETWORK);
        return;
    }
    if (length < 4) return;
    /* The status words: what a DIMM board that has loaded its game says
     * (each read 0x20 bytes, the words little-endian where the SDK swaps
     * them). The command mailboxes at 0x84000000 and 0x89000000 read
     * empty: no answer ever comes, so the network board is not there. */
    if (offset == GC_TRI_BOARD_STATUS) {
        buf[1] = 1;  /* the board is up */
    } else if (offset == GC_TRI_BOARD_STATUS + 0x40u) {
        tri_fill(buf, 0xFF, length);
        buf[0] = 0;  /* DIMM size 512 MB, swapped */
        buf[1] = 0;
        buf[2] = 0;
        buf[3] = 0x20;
        if (length >= 8) {
            buf[4] = 'G';
            buf[5] = 'C';
            buf[6] = 'A';
            buf[7] = 'M';
        }
    } else if (offset == GC_TRI_BOARD_STATUS + 0x100u) {
        buf[0] = 0x1F;
        buf[1] = 0x1F;
        buf[2] = 0x1F;
    } else if (offset == GC_TRI_BOARD_STATUS + 0x120u) {
        buf[0] = 0xFA;  /* the board's firmware, 0x01FA swapped */
        buf[1] = 0x01;
    } else if (offset == GC_TRI_BOARD_STATUS + 0x140u) {
        buf[0] = 1;     /* its firmware is running */
    } else if (offset == GC_TRI_BOARD_STATUS + 0x160u) {
        buf[2] = 0x1E;
    } else if (offset == GC_TRI_BOARD_STATUS + 0x1A0u) {
        tri_fill(buf, 0xFF, 4);
    }
}

void gc_tri_board_write(struct gc_tri* t, uint32_t offset, const uint8_t* buf, uint32_t length) {
    const uint32_t n = length < 32 ? length : 32;
    t->board_writes++;
    t->board_last = offset;
    if (offset == GC_TRI_MEDIA_REQUEST || offset == GC_TRI_NET_REQUEST) {
        /* A request (the SDK also writes zeros here to clear it). */
        const uint32_t box = offset == GC_TRI_NET_REQUEST ? GC_TRI_BOX_NETWORK : GC_TRI_BOX_MEDIA;
        if (n >= 4 && (buf[2] | buf[3]) != 0) tri_copy(t->box_request[box], buf, n);
    } else if (offset == GC_TRI_MEDIA_ANSWER || offset == GC_TRI_NET_ANSWER) {
        tri_fill(t->box_answer[offset == GC_TRI_NET_ANSWER], 0, 32);  /* the SDK took the answer */
    } else if (offset == GC_TRI_MEDIA_DOORBELL) {
        box_answer(t, GC_TRI_BOX_MEDIA);
    }
}

/* ---- The baseboard over EXI ------------------------------------------- */

void gc_tri_backup_write(struct gc_tri* t, uint32_t addr, const uint8_t* buf, uint32_t length) {
    uint32_t i;
    t->backup_writes++;
    for (i = 0; i < length; ++i) t->backup[(addr + i) & (GC_TRI_BACKUP_BYTES - 1u)] = buf[i];
}

void gc_tri_backup_read(struct gc_tri* t, uint32_t addr, uint8_t* buf, uint32_t length) {
    uint32_t i;
    for (i = 0; i < length; ++i) buf[i] = t->backup[(addr + i) & (GC_TRI_BACKUP_BYTES - 1u)];
}

uint32_t gc_tri_exi_register(struct gc_tri* t, uint32_t command, uint32_t value) {
    t->exi_calls++;
    if (command == GC_TRI_EXI_CMD_READ_ISR) return 0x0400u | (t->irq_status & 0xFFu);
    if (command == GC_TRI_EXI_CMD_READ_IMR) return 0xFF81u;
    if (command == GC_TRI_EXI_CMD_WRITE_ISR) t->irq_status &= ~(value & 0xFFu);
    if (command == GC_TRI_EXI_CMD_WRITE_IMR) t->imr = value;
    if (command == GC_TRI_EXI_CMD_WRITE_LANCNT) {
        /* The network controller's reset: it raises its interrupt (status
         * 2), which RiftWii never delivers (no network board). */
        if (value == 0) t->irq_status = 0x02u;
        if (value == 0x201u) t->irq_status = 0;
    }
    return 0;
}

/* ---- In the game ------------------------------------------------------ */

#ifdef RT_TARGET_PPC
#include "gc_rt.h"

#define TRI_SI_COMCSR 0xCD006434u
#define TRI_SI_SR 0xCD006438u
#define TRI_SI_BUFFER 0xCD006480u
#define TRI_SI_TSTART 0x00000001u
#define TRI_SI_TCINT 0x80000000u
#define TRI_SI_RDSTINTMSK 0x08000000u
#define TRI_SI_CHAN0_ERRORS 0x0F000000u  /* under-run, over-run, collision, no response */
#define TRI_SI_NOREP 0x08u               /* a callback's "no response" */
#define TRI_OS_CURRENT_CONTEXT 0x800000D4u
#define TRI_POLL_TICKS 60750u            /* a millisecond */

static uint32_t tri_read32(uint32_t a) { return *(volatile uint32_t*)(uintptr_t)a; }
static void tri_write32(uint32_t a, uint32_t v) { *(volatile uint32_t*)(uintptr_t)a = v; }

/* The controller in port 1, read with the SI's own transfer (0x40 0x03
 * 0x00, eight bytes back): the game never reads port 1 itself (the
 * baseboard is there on a cabinet), and its SI transfers on that channel
 * all come here, so the channel is free. No interrupt: the transfer is
 * waited for with interrupts off, about a tenth of a millisecond. */
static void tri_poll_pad(struct gc_tri* t) {
    uint8_t pad[8];
    uint32_t start, now, csr, sr, w0, w1, i;
    int ok = 0;
    const uint32_t msr = gc_irq_off();
    csr = tri_read32(TRI_SI_COMCSR);
    if (!(csr & TRI_SI_TSTART)) {
        tri_write32(TRI_SI_SR, TRI_SI_CHAN0_ERRORS);
        tri_write32(TRI_SI_BUFFER, 0x40030000u);
        tri_write32(TRI_SI_COMCSR, TRI_SI_TCINT | (csr & TRI_SI_RDSTINTMSK) | (3u << 16) | (8u << 8) | TRI_SI_TSTART);
        __asm__ volatile("mftb %0" : "=r"(start));
        do {
            __asm__ volatile("mftb %0" : "=r"(now));
            csr = tri_read32(TRI_SI_COMCSR);
        } while ((csr & TRI_SI_TSTART) && now - start < TRI_POLL_TICKS);
        sr = tri_read32(TRI_SI_SR);
        ok = !(csr & TRI_SI_TSTART) && !(sr & TRI_SI_CHAN0_ERRORS);
        tri_write32(TRI_SI_SR, TRI_SI_CHAN0_ERRORS);
        tri_write32(TRI_SI_COMCSR, TRI_SI_TCINT | (csr & TRI_SI_RDSTINTMSK));
        if (ok) {
            w0 = tri_read32(TRI_SI_BUFFER);
            w1 = tri_read32(TRI_SI_BUFFER + 4u);
            for (i = 0; i < 4; ++i) {
                pad[i] = (uint8_t)(w0 >> (24u - 8u * i));
                pad[4u + i] = (uint8_t)(w1 >> (24u - 8u * i));
            }
            t->pad_last[0] = w0;
            t->pad_last[1] = w1;
            /* A controller sets bit 7 of its first byte only on an error. */
            if (pad[0] & 0x80u) ok = 0;
        }
    }
    gc_irq_restore(msr);
    t->pad_polls++;
    if (!ok) t->pad_errors++;
    gc_tri_set_pad(t, pad, ok);
}

typedef void (*tri_si_callback_fn)(uint32_t chan, uint32_t error, uint32_t os_context);
typedef void (*tri_exi_callback_fn)(uint32_t chan, uint32_t os_context);

/* An answer went into a box: the baseboard's interrupt, as the game's
 * EXI handler for it (channel 2's) would get it. */
static void tri_raise(struct gc_tri* t) {
    uint32_t msr;
    if (!t->irq_raise || t->exi_callback == 0) return;
    t->irq_raise = 0;
    msr = gc_irq_off();
    ((tri_exi_callback_fn)(uintptr_t)t->exi_callback)(2, *(const uint32_t*)(uintptr_t)TRI_OS_CURRENT_CONTEXT);
    gc_irq_restore(msr);
}

uint32_t gc_on_tri(struct gc_rt_context* ctx, uint32_t fn, uint32_t* args) {
    struct gc_tri* t = (struct gc_tri*)(uintptr_t)ctx->tri;
    if (fn == GC_TRI_SI_TRANSFER) {
        const uint8_t* out = (const uint8_t*)(uintptr_t)args[1];
        uint32_t answered, msr;
        if (args[0] != 0) {
            args[8] = 1;  /* another port: the hardware's */
            return 0;
        }
        if (args[2] != 0 && out[0] == GC_TRI_SI_PACKET) tri_poll_pad(t);
        answered = gc_tri_si(t, out, args[2], (uint8_t*)(uintptr_t)args[3], args[4]);
        /* Its callback at once, with interrupts off as the SI interrupt
         * would run it: the arcade SDK waits for the channel only by
         * asking whether it is busy, which it never is here. */
        if (args[5] != 0) {
            msr = gc_irq_off();
            ((tri_si_callback_fn)(uintptr_t)args[5])(0, answered != 0 ? 0 : TRI_SI_NOREP,
                                                     *(const uint32_t*)(uintptr_t)TRI_OS_CURRENT_CONTEXT);
            gc_irq_restore(msr);
        }
        return 1;
    }
    if (fn == GC_TRI_EXI_CONNECT) {
        t->exi_calls++;
        return 1;
    }
    if (fn == GC_TRI_EXI_WRITE_BYTE) {
        const uint8_t value = (uint8_t)args[3];
        gc_tri_backup_write(t, args[2], &value, 1);
        return 1;
    }
    if (fn == GC_TRI_EXI_READ_BYTE) {
        gc_tri_backup_read(t, args[2], (uint8_t*)(uintptr_t)args[3], 1);
        return 1;
    }
    if (fn == GC_TRI_EXI_WRITE_BLOCK) {
        gc_tri_backup_write(t, args[2], (const uint8_t*)(uintptr_t)args[3], args[4]);
        return 1;
    }
    if (fn == GC_TRI_EXI_READ_BLOCK) {
        gc_tri_backup_read(t, args[2], (uint8_t*)(uintptr_t)args[3], args[4]);
        gc_flush(args[3], args[4]);
        return 1;
    }
    if (fn == GC_TRI_EXI_READ_ISR || fn == GC_TRI_EXI_READ_IMR) {
        *(uint16_t*)(uintptr_t)args[2] =
            (uint16_t)gc_tri_exi_register(t, fn == GC_TRI_EXI_READ_ISR ? GC_TRI_EXI_CMD_READ_ISR : GC_TRI_EXI_CMD_READ_IMR, 0);
        return 1;
    }
    if (fn == GC_TRI_EXI_WRITE_ISR || fn == GC_TRI_EXI_WRITE_IMR || fn == GC_TRI_EXI_WRITE_LANCNT) {
        const uint32_t command = fn == GC_TRI_EXI_WRITE_ISR   ? GC_TRI_EXI_CMD_WRITE_ISR
                                 : fn == GC_TRI_EXI_WRITE_IMR ? GC_TRI_EXI_CMD_WRITE_IMR
                                                              : GC_TRI_EXI_CMD_WRITE_LANCNT;
        gc_tri_exi_register(t, command, args[2] & 0xFFFFu);
        return 1;
    }
    if (fn == GC_TRI_DI_WRITE) {
        gc_tri_board_write(t, args[2], (const uint8_t*)(uintptr_t)args[0], args[1]);
        gc_di_finish(ctx, args[3], 0, 0);
        tri_raise(t);
        return 1;
    }
    if (fn == GC_TRI_EXI_CALLBACK) {
        if (args[0] == 2) t->exi_callback = args[1];
        return 0;
    }
    if (fn == GC_TRI_DI_INQUIRY) {
        gc_di_finish(ctx, args[0], 1, t->media_inquiry);
        return 1;
    }
    if (fn == GC_TRI_DI_EXECUTE) {
        t->board_executes++;
        gc_di_finish(ctx, args[1], 0, 0);
        return 1;
    }
    return 0;
}

uint32_t gc_tri_on_read(struct gc_rt_context* ctx, struct gc_tri* t, uint32_t addr, uint32_t length, uint32_t offset,
                        uint32_t cb) {
    gc_tri_board_read(t, offset, (uint8_t*)(uintptr_t)addr, length);
    gc_flush(addr, length);
    gc_di_finish(ctx, cb, 0, 0);
    tri_raise(t);
    return 1;
}
#endif
