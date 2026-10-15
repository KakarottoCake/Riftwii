/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Wii U Pro Controller translator (rtpro.h). No switch statements and
 * no initialised arrays: either would put tables in .rodata, which the
 * blobs must not have. */
#include "rtpro.h"

/* HID over L2CAP: the first byte of a report on the interrupt channel. */
#define HID_INPUT 0xA1u
#define HID_OUTPUT 0xA2u

/* Output reports. */
#define OUT_MODE 0x12u
#define OUT_WRITE 0x16u
#define OUT_READ 0x17u
/* Input reports. */
#define IN_READ 0x21u
#define IN_ACK 0x22u

/* HCI events. */
#define EVENT_CONNECTION_COMPLETE 0x03u
#define EVENT_DISCONNECTION_COMPLETE 0x05u

/* A Pro Controller's sticks are 12 bits, about 0x800 at rest; this far
 * from it counts as a full push (they reach a little further). */
#define STICK_CENTRE 2048
#define STICK_REACH 896
/* The Classic Controller's calibration (below) puts a full push 24 from
 * the left stick's centre (6 bits) and 12 from the right's (5 bits). */
#define LEFT_REACH 24
#define RIGHT_REACH 12

/* The accelerometer at rest, flat: no tilt, 1 g up (the calibration's
 * zero and 1 g points, below). */
#define ACCEL_ZERO 0x80u
#define ACCEL_ONE_G 0x9Au

static uint32_t le16(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }

static void put_le16(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

void rtpro_init(struct rtpro_state* st) {
    uint8_t* b = (uint8_t*)st;
    uint32_t i;
    for (i = 0; i < sizeof(*st); ++i) b[i] = 0;
}

void rtpro_set_tables(struct rtpro_state* st, const uint8_t* tables) { st->tables = tables; }

/* Encryption ---------------------------------------------------------- */

static uint8_t rotate_right(uint32_t v, uint32_t n) {
    n &= 7u;
    return (uint8_t)((v >> n) | (v << ((8u - n) & 7u)));
}

/* The key's 16 bytes (as written from 0xA40040) into the two tables,
 * through the game's lookup tables: the first ten bytes, reversed, are a
 * random part; the last six, reversed, are the key proper, which the
 * random part and one row of the answer table must give back (that row's
 * number picks the pair of table boxes). 0 when no row does. */
static int make_tables(const uint8_t* tables, const uint8_t* written, uint8_t* xor_table, uint8_t* add_table) {
    const uint8_t* keybox = tables + 48;
    uint8_t rnd[10], key[6], t[10], k[6];
    uint32_t i, row;
    for (i = 0; i < 10u; ++i) rnd[i] = written[9u - i];
    for (i = 0; i < 6u; ++i) key[i] = written[15u - i];
    for (i = 0; i < 10u; ++i) t[i] = keybox[rnd[i]];
    for (row = 0; row < 7u; ++row) {
        const uint8_t* ans = tables + 6u * row;
        k[0] = (uint8_t)((uint8_t)(rotate_right(ans[0] ^ t[5], t[2]) - t[9]) ^ t[4]);
        k[1] = (uint8_t)((uint8_t)(rotate_right(ans[1] ^ t[1], t[0]) - t[5]) ^ t[7]);
        k[2] = (uint8_t)((uint8_t)(rotate_right(ans[2] ^ t[6], t[8]) - t[2]) ^ t[0]);
        k[3] = (uint8_t)((uint8_t)(rotate_right(ans[3] ^ t[4], t[7]) - t[3]) ^ t[2]);
        k[4] = (uint8_t)((uint8_t)(rotate_right(ans[4] ^ t[1], t[6]) - t[3]) ^ t[4]);
        k[5] = (uint8_t)((uint8_t)(rotate_right(ans[5] ^ t[7], t[8]) - t[5]) ^ t[9]);
        if (k[0] != key[0] || k[1] != key[1] || k[2] != key[2] || k[3] != key[3] || k[4] != key[4] ||
            k[5] != key[5])
            continue;
        {
            const uint8_t* a = tables + 48u + 256u * (1u + row);
            const uint8_t* b = a + 256u;
            add_table[0] = a[key[4]] ^ b[rnd[3]];
            add_table[1] = a[key[2]] ^ b[rnd[5]];
            add_table[2] = a[key[5]] ^ b[rnd[7]];
            add_table[3] = a[key[0]] ^ b[rnd[2]];
            add_table[4] = a[key[1]] ^ b[rnd[4]];
            add_table[5] = a[key[3]] ^ b[rnd[9]];
            add_table[6] = a[rnd[0]] ^ b[rnd[6]];
            add_table[7] = a[rnd[1]] ^ b[rnd[8]];
            xor_table[0] = a[key[0]] ^ b[rnd[1]];
            xor_table[1] = a[key[5]] ^ b[rnd[4]];
            xor_table[2] = a[key[3]] ^ b[rnd[0]];
            xor_table[3] = a[key[2]] ^ b[rnd[9]];
            xor_table[4] = a[key[4]] ^ b[rnd[7]];
            xor_table[5] = a[key[1]] ^ b[rnd[8]];
            xor_table[6] = a[rnd[3]] ^ b[rnd[5]];
            xor_table[7] = a[rnd[2]] ^ b[rnd[6]];
        }
        return 1;
    }
    return 0;
}

static uint8_t encrypt_byte(const struct rtpro_link* l, uint8_t v, uint32_t address) {
    const uint32_t i = address & 7u;
    return (uint8_t)((uint8_t)(v - l->add_table[i]) ^ l->xor_table[i]);
}

static uint8_t decrypt_byte(const struct rtpro_link* l, uint8_t v, uint32_t address) {
    const uint32_t i = address & 7u;
    return (uint8_t)((v ^ l->xor_table[i]) + l->add_table[i]);
}

/* A write the game sends to the extension's registers: what it means for
 * the encryption, and on a Pro Controller's link the 0xAA that would turn
 * the Pro's own encryption on becomes 0x55. Returns RTPRO_CHANGED when it
 * changed the write. */
static uint32_t extension_write(struct rtpro_state* st, struct rtpro_link* l, uint32_t address, uint8_t* data,
                                uint32_t size) {
    uint32_t i, what = 0;
    for (i = 0; i < size && i < 16u; ++i) {
        const uint32_t a = address + i;
        if (a == 0xA400F0u) {
            if (data[i] == 0xAAu) {
                l->cipher = RTPRO_ARMED;
                l->key_parts = 0;
                l->encrypted = 1;
                if (l->pro) {
                    data[i] = 0x55u;
                    what = RTPRO_CHANGED;
                }
            } else if (data[i] == 0x55u) {
                l->cipher = RTPRO_PLAIN;  /* the new way of starting: no encryption */
                l->key_parts = 0;
            }
        } else if (a >= 0xA40040u && a < 0xA40050u) {
            l->key[a - 0xA40040u] = data[i];
            l->key_parts = (uint16_t)(l->key_parts | (1u << (a - 0xA40040u)));
        }
    }
    if (address == 0xA40040u && l->cipher == RTPRO_PLAIN && size == 1u && data[0] == 0) {
        /* The old way: one 0x00 at 0xA40040 turns on the fixed transform. */
        for (i = 0; i < 8u; ++i) {
            l->xor_table[i] = 0x17u;
            l->add_table[i] = 0x17u;
        }
        l->cipher = RTPRO_KEYED;
        l->encrypted = 1;
        st->keys++;
    } else if (l->cipher == RTPRO_ARMED && l->key_parts == 0xFFFFu) {
        if (st->tables && make_tables(st->tables, l->key, l->xor_table, l->add_table)) {
            l->cipher = RTPRO_KEYED;
            st->keys++;
        } else {
            l->cipher = RTPRO_NO_TABLES;
            st->bad_keys++;
        }
    }
    return what;
}

static struct rtpro_link* link_of(struct rtpro_state* st, uint32_t handle) {
    uint32_t i;
    for (i = 0; i < RTPRO_LINKS; ++i) {
        if (st->links[i].used && st->links[i].handle == handle) return &st->links[i];
    }
    return 0;
}

/* A link the trace never saw connect (the blob came in late): followed
 * from its first packet, as not a Pro Controller until its ID says so. */
static struct rtpro_link* link_or_new(struct rtpro_state* st, uint32_t handle) {
    struct rtpro_link* l = link_of(st, handle);
    uint32_t i, j;
    if (l) return l;
    for (i = 0; i < RTPRO_LINKS; ++i) {
        l = &st->links[i];
        if (l->used) continue;
        {
            uint8_t* b = (uint8_t*)l;
            for (j = 0; j < sizeof(*l); ++j) b[j] = 0;
        }
        l->used = 1;
        l->handle = (uint16_t)handle;
        return l;
    }
    return 0;
}

static int same_address(const uint8_t* a, const uint8_t* b) {
    uint32_t i;
    for (i = 0; i < 6u; ++i) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

/* SYSCONF and HCI may keep the six bytes in opposite orders. */
static int same_either_way(const uint8_t* a, const uint8_t* b) {
    uint32_t i;
    if (same_address(a, b)) return 1;
    for (i = 0; i < 6u; ++i) {
        if (a[i] != b[5u - i]) return 0;
    }
    return 1;
}

void rtpro_event(struct rtpro_state* st, const uint8_t* addresses, uint32_t count, const uint8_t* event,
                 uint32_t size) {
    const uint8_t* p;
    uint32_t i;
    if (size < 2u || size < 2u + (uint32_t)event[1]) return;
    p = event + 2;
    if (event[0] == EVENT_CONNECTION_COMPLETE && event[1] >= 11u && p[0] == 0) {
        const uint32_t handle = le16(p + 1) & 0x0FFFu;
        struct rtpro_link* l = link_of(st, handle);
        if (l) l->used = 0;  /* a handle used again: a new link */
        l = link_or_new(st, handle);
        if (!l) return;
        if (count == RTPRO_EVERY_LINK) {
            l->pro = RTPRO_BY_ADDRESS;
            st->pros++;
            return;
        }
        if (count > RTPRO_ADDRESSES) count = RTPRO_ADDRESSES;
        for (i = 0; i < count; ++i) {
            if (same_either_way(addresses + 6u * i, p + 3)) {
                l->pro = RTPRO_BY_ADDRESS;
                st->pros++;
                break;
            }
        }
        return;
    }
    if (event[0] == EVENT_DISCONNECTION_COMPLETE && event[1] >= 4u && p[0] == 0) {
        struct rtpro_link* l = link_of(st, le16(p + 1) & 0x0FFFu);
        if (l) l->used = 0;
    }
}

/* The HID report in an ACL packet: on a channel of a followed link, whole
 * (not the rest of a split one), not the L2CAP signalling channel. Sets
 * `report` and its size `n`; 0 when there is none. */
static struct rtpro_link* report_of(struct rtpro_state* st, uint8_t* acl, uint32_t size, int create, uint8_t** report,
                                    uint32_t* n) {
    uint32_t handle, flags, acl_len, l2_len, cid;
    if (size < 10u) return 0;
    handle = le16(acl) & 0x0FFFu;
    flags = (le16(acl) >> 12) & 3u;
    acl_len = le16(acl + 2);
    l2_len = le16(acl + 4);
    cid = le16(acl + 6);
    if (flags == 1u || cid < 0x40u || acl_len != l2_len + 4u || 8u + l2_len > size || l2_len < 2u) return 0;
    *report = acl + 8;
    *n = l2_len;
    return create ? link_or_new(st, handle) : link_of(st, handle);
}

/* The packet's L2CAP and ACL lengths for a report of `n` bytes. */
static void set_lengths(uint8_t* acl, uint32_t n) {
    put_le16(acl + 4, n);
    put_le16(acl + 2, n + 4u);
}

uint32_t rtpro_acl_out(struct rtpro_state* st, uint8_t* acl, uint32_t size) {
    uint8_t* p;
    uint32_t n, what = RTPRO_SEEN;
    struct rtpro_link* l = report_of(st, acl, size, 1, &p, &n);
    if (!l || p[0] != HID_OUTPUT) return 0;
    if (p[1] == OUT_MODE && n >= 4u) {
        l->mode = p[3];
        if (l->pro && p[3] != RTPRO_MODE) {
            p[3] = RTPRO_MODE;
            st->modes++;
            what |= RTPRO_CHANGED;
        }
    } else if (p[1] == OUT_READ && n >= 8u) {
        /* Kept until answered, the oldest given way to a new one. */
        uint32_t i, slot = 0;
        for (i = 0; i < RTPRO_READS; ++i) {
            if (!l->reads[i].used) {
                slot = i;
                break;
            }
        }
        if (i == RTPRO_READS) {
            for (i = 1; i < RTPRO_READS; ++i) l->reads[i - 1u] = l->reads[i];
            slot = RTPRO_READS - 1u;
        }
        l->reads[slot].address = ((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 8) | p[5];
        if (p[2] & 0x04u) l->reads[slot].address |= RTPRO_REGISTERS;
        l->reads[slot].size = (uint16_t)(((uint32_t)p[6] << 8) | p[7]);
        l->reads[slot].used = 1;
    } else if (p[1] == OUT_WRITE && n >= 7u && (p[2] & 0x04u)) {
        const uint32_t address = ((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 8) | p[5];
        uint32_t size = p[6];
        if (size > n - 7u) size = n - 7u;
        if ((address & 0xFF0000u) == 0xA40000u) what |= extension_write(st, l, address, p + 7, size);
    }
    return what;
}

/* The Classic Controller's calibration, 16 bytes (at 0xA40020, again at
 * 0xA40030): each stick axis's maximum, minimum and centre, the triggers'
 * rest, and two check bytes (the sum of the others plus 0x55, plus 0xAA). */
static uint8_t classic_calibration(uint32_t i) {
    /* Twelve axis bytes summing to 4 * 0x180: the check bytes are 0x55, 0xAA. */
    if (i < 12u) {
        const uint32_t k = i % 3u;
        return (uint8_t)(k == 0 ? 0xE0u : k == 1 ? 0x20u : 0x80u);
    }
    if (i == 14u) return 0x55u;
    if (i == 15u) return 0xAAu;
    return 0;
}

/* The Wii Remote's accelerometer calibration, 10 bytes (EEPROM 0x16, again
 * at 0x20): the zero point and the 1 g point of each axis, the speaker's
 * volume, and a check byte (the sum of the others plus 0x55). */
static uint8_t accel_calibration(uint32_t i) {
    if (i < 3u) return ACCEL_ZERO;
    if (i >= 4u && i < 7u) return ACCEL_ONE_G;
    if (i == 8u) return 0x40u;
    if (i == 9u) return (uint8_t)(3u * ACCEL_ZERO + 3u * ACCEL_ONE_G + 0x40u + 0x55u);
    return 0;
}

/* A Classic Controller Pro's extension ID (0xA400FA-0xA400FF). */
static uint8_t classic_pro_id(uint32_t j) {
    if (j == 0) return 0x01u;
    if (j == 2) return 0xA4u;
    if (j == 3) return 0x20u;
    if (j == 1) return 0x00u;
    return 0x01u;
}

/* The read `addr16` answers: its full address (with RTPRO_REGISTERS), or
 * 0xFFFFFFFF when none is known. Forgotten once its last part is in. */
static uint32_t answered_read(struct rtpro_link* l, uint32_t addr16, uint32_t len, uint32_t error) {
    uint32_t i;
    for (i = 0; i < RTPRO_READS; ++i) {
        struct rtpro_read* r = &l->reads[i];
        const uint32_t start = r->address & 0xFFFFu;
        if (!r->used || addr16 < start || addr16 >= start + r->size) continue;
        {
            const uint32_t full = (r->address & ~0xFFFFu) | addr16;
            if (error || addr16 + len >= start + r->size) {
                uint32_t j;
                for (j = i + 1u; j < RTPRO_READS; ++j) l->reads[j - 1u] = l->reads[j];
                l->reads[RTPRO_READS - 1u].used = 0;
            }
            return full;
        }
    }
    return 0xFFFFFFFFu;
}

/* The calibration byte at `address` for a Pro Controller, or -1. */
static int calibration_at(uint32_t address) {
    if (address & RTPRO_REGISTERS) {
        const uint32_t a = address & 0xFFFFFFu;
        if (a >= 0xA40020u && a < 0xA40040u) return classic_calibration((a - 0xA40020u) & 15u);
        return -1;
    }
    if (address >= 0x16u && address < 0x20u) return accel_calibration(address - 0x16u);
    if (address >= 0x20u && address < 0x2Au) return accel_calibration(address - 0x20u);
    return -1;
}

/* An answer to a memory read: a1 21 BB BB SE AH AL D0-D15. */
static uint32_t read_answer(struct rtpro_state* st, struct rtpro_link* l, uint8_t* p, uint32_t n) {
    uint32_t addr16, len, error, full, k, what = 0;
    if (n < 23u) return 0;
    addr16 = ((uint32_t)p[5] << 8) | p[6];
    len = (p[4] >> 4) + 1u;
    error = p[4] & 0x0Fu;
    full = answered_read(l, addr16, len, error);
    if (full == 0xFFFFFFFFu) {
        /* Not one we saw asked: only an extension ID is recognised. */
        if (addr16 < 0x00F0u) return 0;
        full = 0xA40000u | addr16 | RTPRO_REGISTERS;
    }
    /* The extension ID: the Pro's (..01 20) becomes a Classic Controller
     * Pro's, and says the link is a Pro Controller. */
    if (!error && (full & RTPRO_REGISTERS)) {
        /* The game reads the extension's registers through its encryption
         * once it is on, and the Pro sends them plain unless it was seen
         * encrypting. */
        const int game_decrypts = l->cipher == RTPRO_KEYED && (full & 0xFF0000u) == 0xA40000u;
        const int pro_plain = l->pro_encrypts != RTPRO_SENDS_ENCRYPTED;
        int at4 = -1, at5 = -1;
        uint8_t id4 = 0, id5 = 0;
        for (k = 0; k < len && k < 16u; ++k) {
            const uint32_t a = (full & 0xFFFFFFu) + k;
            if (a == 0xA400FEu) at4 = (int)k;
            if (a == 0xA400FFu) at5 = (int)k;
        }
        if (at4 >= 0 && at5 >= 0) {
            id4 = p[7 + at4];
            id5 = p[7 + at5];
            if (game_decrypts && !pro_plain) {
                id4 = decrypt_byte(l, id4, 0xFEu);
                id5 = decrypt_byte(l, id5, 0xFFu);
            }
        }
        if (at4 >= 0 && at5 >= 0 && id4 == 0x01u && id5 == 0x20u) {
            for (k = 0; k < len && k < 16u; ++k) {
                const uint32_t a = (full & 0xFFFFFFu) + k;
                if (a >= 0xA400FAu && a <= 0xA400FFu) {
                    const uint8_t v = classic_pro_id(a - 0xA400FAu);
                    p[7 + k] = game_decrypts ? encrypt_byte(l, v, a) : v;
                }
            }
            if (!l->pro) {
                l->pro = RTPRO_BY_ID;
                st->pros++;
            }
            st->ids++;
            return RTPRO_CHANGED;
        }
    }
    if (!l->pro || calibration_at(full) < 0) return 0;
    /* Calibration: ours, whatever the Pro Controller holds there (or if it
     * refused the read). */
    if (error) {
        len = 16u;
        p[4] = (uint8_t)((len - 1u) << 4);
    }
    for (k = 0; k < len && k < 16u; ++k) {
        const int v = calibration_at(full + k);
        if (v >= 0) {
            const int in_extension = (full & RTPRO_REGISTERS) && ((full + k) & 0xFF0000u) == 0xA40000u;
            p[7 + k] = in_extension && l->cipher == RTPRO_KEYED ? encrypt_byte(l, (uint8_t)v, full + k) : (uint8_t)v;
            what = RTPRO_CHANGED;
        }
    }
    if (what) st->calibrations++;
    return what;
}

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* A 12-bit stick axis as one of `bits` bits about its centre. */
static int stick(const uint8_t* lo_hi, int reach, int centre, int top) {
    const int raw = (int)lo_hi[0] | ((int)(lo_hi[1] & 0x0Fu) << 8);
    return clamp(centre + (raw - STICK_CENTRE) * reach / STICK_REACH, 0, top);
}

void rtpro_classic(const uint8_t* pro, uint8_t* c) {
    /* The Pro's bytes 0-7: left X, right X, left Y, right Y (12 bits,
     * little-endian); 8-9 the buttons, active low, in the Classic
     * Controller's own bit places; 10 the stick clicks. */
    const int lx = stick(pro + 0, LEFT_REACH, 32, 63);
    const int rx = stick(pro + 2, RIGHT_REACH, 16, 31);
    const int ly = stick(pro + 4, LEFT_REACH, 32, 63);
    const int ry = stick(pro + 6, RIGHT_REACH, 16, 31);
    /* The Pro's L and R are buttons: an analog trigger all the way, or not. */
    const int lt = (pro[8] & 0x20u) ? 0 : 31;
    const int rt = (pro[8] & 0x02u) ? 0 : 31;
    c[0] = (uint8_t)(((rx & 0x18) << 3) | lx);
    c[1] = (uint8_t)(((rx & 0x06) << 5) | ly);
    c[2] = (uint8_t)(((rx & 0x01) << 7) | ((lt & 0x18) << 2) | ry);
    c[3] = (uint8_t)(((lt & 0x07) << 5) | rt);
    c[4] = (uint8_t)(pro[8] | 0x01u);  /* bit 0 is always 1 on a Classic Controller */
    c[5] = pro[9];
}

/* Where a data report keeps its extension bytes, and how many; 0 when it
 * has none. */
static uint32_t extension_of(uint32_t id, uint32_t* offset) {
    if (id == 0x32u) { *offset = 4; return 8; }
    if (id == 0x34u) { *offset = 4; return 19; }
    if (id == 0x35u) { *offset = 7; return 16; }
    if (id == 0x36u) { *offset = 14; return 9; }
    if (id == 0x37u) { *offset = 17; return 6; }
    if (id == 0x3Du) { *offset = 2; return 21; }
    return 0;
}

/* A data report in mode `mode` (the game's), from the Classic Controller
 * bytes: the core buttons none (the Pro has none of its own), the
 * accelerometer at rest, no IR points. Returns its size. */
static uint32_t build_report(uint8_t* p, uint32_t mode, const uint8_t* classic, const struct rtpro_link* l) {
    uint32_t at = 2, ext = 0, ir = 0, accel = 0, i;
    if (mode == 0x31u) accel = 1;
    if (mode == 0x32u) ext = 8;
    if (mode == 0x33u) { accel = 1; ir = 12; }
    if (mode == 0x34u) ext = 19;
    if (mode == 0x35u) { accel = 1; ext = 16; }
    if (mode == 0x36u) { ir = 10; ext = 9; }
    if (mode == 0x37u) { accel = 1; ir = 10; ext = 6; }
    p[0] = HID_INPUT;
    p[1] = (uint8_t)mode;
    if (mode == 0x3Du) {
        ext = 21;
    } else {
        p[2] = 0;
        p[3] = 0;
        at = 4;
    }
    if (accel) {
        p[at++] = ACCEL_ZERO;
        p[at++] = ACCEL_ZERO;
        p[at++] = ACCEL_ONE_G;
    }
    for (i = 0; i < ir; ++i) p[at++] = 0xFFu;
    /* The extension's bytes are its registers from 0xA40000 on, and
     * encrypted as such once the game turned encryption on. */
    for (i = 0; i < ext; ++i) {
        const uint8_t v = i < 6u ? classic[i] : 0;
        p[at++] = l->cipher == RTPRO_KEYED ? encrypt_byte(l, v, i) : v;
    }
    return at;
}

uint32_t rtpro_acl_in(struct rtpro_state* st, uint8_t* acl, uint32_t size, uint32_t room, uint32_t* what) {
    uint8_t* p;
    uint32_t n;
    struct rtpro_link* l = report_of(st, acl, size, 0, &p, &n);
    (void)room;  /* a rebuilt report is never longer than the one it replaces */
    *what = 0;
    if (!l || p[0] != HID_INPUT) return size;
    *what = RTPRO_SEEN;
    if (p[1] == IN_READ) {
        *what |= read_answer(st, l, p, n);
        return size;
    }
    if (!l->pro) {
        if (p[1] >= 0x30u && p[1] <= 0x3Fu) *what |= RTPRO_DATA;
        return size;
    }
    if (p[1] == IN_ACK && n >= 6u) {
        if (p[5] != 0) {
            p[5] = 0;
            st->acks++;
            *what |= RTPRO_CHANGED;
        }
        return size;
    }
    if (p[1] >= 0x30u && p[1] <= 0x3Fu) {
        uint32_t offset = 0, mode = l->mode;
        const uint32_t ext = extension_of(p[1], &offset);
        uint8_t pro[11], classic[6];
        uint32_t i, rebuilt;
        *what |= RTPRO_DATA;
        /* All eleven of the Pro's bytes, or nothing to translate. */
        if (ext < 11u || offset + 11u > n) return size;
        for (i = 0; i < 11u; ++i) pro[i] = p[offset + i];
        if (l->cipher == RTPRO_KEYED) {
            /* Whether the Pro encrypts its own bytes too (it was not asked
             * to): its first report with nothing pressed says, as its two
             * button bytes are 0xFF one way and not the other. */
            if (l->pro_encrypts == RTPRO_UNKNOWN) {
                const int plain_rest = pro[8] == 0xFFu && pro[9] == 0xFFu;
                const int decrypted_rest = decrypt_byte(l, pro[8], 8) == 0xFFu && decrypt_byte(l, pro[9], 9) == 0xFFu;
                if (plain_rest != decrypted_rest) l->pro_encrypts = plain_rest ? RTPRO_SENDS_PLAIN : RTPRO_SENDS_ENCRYPTED;
            }
            if (l->pro_encrypts == RTPRO_SENDS_ENCRYPTED) {
                for (i = 0; i < 11u; ++i) pro[i] = decrypt_byte(l, pro[i], i);
            }
        }
        rtpro_classic(pro, classic);
        if (mode < 0x30u || (mode > 0x37u && mode != 0x3Du)) mode = RTPRO_MODE;
        rebuilt = build_report(p, mode, classic, l);
        set_lengths(acl, rebuilt);
        l->translated++;
        st->translated++;
        *what |= RTPRO_CHANGED;
        return 8u + rebuilt;
    }
    return size;
}
