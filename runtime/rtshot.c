/* SPDX-License-Identifier: GPL-3.0-or-later */
/* In-game screenshots: combo, picture and file (rtshot.h). */
#include "rtshot.h"

static struct rtshot_remote* remote_for(struct rtshot_input* in, uint16_t handle) {
    uint32_t i;
    struct rtshot_remote* r;
    for (i = 0; i < RTSHOT_REMOTES; ++i)
        if (in->remotes[i].used && in->remotes[i].handle == handle) return &in->remotes[i];
    /* A new connection (or one that came back with another handle). */
    r = &in->remotes[in->next_remote % RTSHOT_REMOTES];
    in->next_remote = (in->next_remote + 1u) % RTSHOT_REMOTES;
    r->handle = handle;
    r->used = 1;
    r->buttons = 0;
    r->masking = 0;
    return r;
}

int rtshot_acl(struct rtshot_input* in, uint8_t* acl, uint32_t size) {
    uint32_t l2cap_len, id;
    uint8_t* hid;
    uint8_t buttons, previous;
    struct rtshot_remote* r;
    int fired = 0;
    /* HCI ACL header (handle and flags, length), L2CAP header (length,
     * channel), then the HID transaction: 0xA1 (input data), report id,
     * two button bytes. */
    if (size < 12u) return 0;
    l2cap_len = (uint32_t)acl[4] | ((uint32_t)acl[5] << 8);
    if (((uint32_t)acl[2] | ((uint32_t)acl[3] << 8)) != l2cap_len + 4u || l2cap_len + 8u > size || l2cap_len < 4u)
        return 0;
    if (((uint32_t)acl[6] | ((uint32_t)acl[7] << 8)) < 0x40u) return 0;  /* a signalling channel, not data */
    hid = acl + 8;
    if (hid[0] != 0xA1u) return 0;
    id = hid[1];
    if (!((id >= 0x20u && id <= 0x22u) || (id >= 0x30u && id <= 0x37u) || id == 0x3Eu || id == 0x3Fu)) return 0;
    ++in->reports;
    r = remote_for(in, (uint16_t)(((uint32_t)acl[0] | ((uint32_t)acl[1] << 8)) & 0x0FFFu));
    buttons = hid[3];
    previous = r->buttons;
    r->buttons = buttons;
    if (r->masking) {
        if (buttons & RTSHOT_REMOTE_HOME) hid[3] = (uint8_t)(buttons & ~RTSHOT_REMOTE_HOME);
        else r->masking = 0;
    } else if ((buttons & RTSHOT_REMOTE_HOME) && !(previous & RTSHOT_REMOTE_HOME) &&
               (buttons & RTSHOT_REMOTE_ONE) && (previous & RTSHOT_REMOTE_ONE)) {
        /* 1 was held before HOME went down. */
        r->masking = 1;
        hid[3] = (uint8_t)(buttons & ~RTSHOT_REMOTE_HOME);
        fired = 1;
    }
    return fired;
}

int rtshot_pads(struct rtshot_input* in, uint8_t* status) {
    uint32_t i;
    int fired = 0;
    for (i = 0; i < RTSHOT_PADS; ++i) {
        uint8_t* s = status + i * 12u;
        uint32_t buttons, down, l, r;
        if (s[10] != 0) {  /* err: no pad there, or no new data */
            if ((int8_t)s[10] == -1) in->pad_down[i] = in->pad_masking[i] = 0;  /* unplugged */
            continue;
        }
        buttons = ((uint32_t)s[0] << 8) | s[1];
        down = buttons & RTSHOT_PAD_DOWN;
        l = (buttons & RTSHOT_PAD_L) || s[6] >= RTSHOT_PAD_TRIGGER;
        r = (buttons & RTSHOT_PAD_R) || s[7] >= RTSHOT_PAD_TRIGGER;
        if (in->pad_masking[i]) {
            if (down) s[1] = (uint8_t)(s[1] & ~RTSHOT_PAD_DOWN);
            else in->pad_masking[i] = 0;
        } else if (down && !in->pad_down[i] && l && r) {
            in->pad_masking[i] = 1;
            s[1] = (uint8_t)(s[1] & ~RTSHOT_PAD_DOWN);
            fired = 1;
        }
        in->pad_down[i] = down ? 1u : 0u;
    }
    return fired;
}

int rtshot_frame(uint16_t vtr, uint16_t picture, uint32_t top_base, struct rtshot_frame* out) {
    const uint32_t active = ((uint32_t)vtr >> 4) & 0x3FFu;  /* lines a field */
    const uint32_t std = (uint32_t)picture & 0xFFu;         /* stride, 16-pixel words */
    const uint32_t wpl = ((uint32_t)picture >> 8) & 0x7Fu;  /* width, 16-pixel words */
    uint32_t address = top_base & 0x00FFFFFFu, step, bytes, end;
    if (top_base & 0x10000000u) address <<= 5;              /* page offset: in 32-byte units */
    if (active == 0 || wpl == 0 || std == 0) return 0;
    /* Both fields interleaved in one buffer (a stride of two lines), or
     * one line after the other. */
    if (std == wpl) step = 1;
    else if (std == wpl * 2u) step = 2;
    else return 0;
    out->address = address;
    out->width = wpl * 16u;
    out->stride = out->width * 2u;
    out->lines = active * step;
    /* 240 lines or so: a field-rendered or double-strike picture. */
    out->flags = out->lines <= 288u ? RTSHOT_DOUBLE_LINES : 0u;
    if (out->width > RTSHOT_MAX_WIDTH || out->lines > RTSHOT_MAX_LINES) return 0;
    bytes = out->stride * out->lines;
    end = address + bytes;
    if ((address & 31u) != 0) return 0;
    if (!(end <= 0x01800000u || (address >= 0x10000000u && end <= 0x14000000u))) return 0;
    return 1;
}

static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

void rtshot_header(uint8_t* out, const struct rtshot_frame* f, const uint8_t* game_id, uint32_t index,
                   uint32_t time) {
    uint32_t i;
    for (i = 0; i < RTSHOT_HEADER_BYTES; ++i) out[i] = 0;
    put32(out, RTSHOT_MAGIC);
    put32(out + 4, RTSHOT_VERSION);
    put32(out + 8, f->width);
    put32(out + 12, f->lines);
    put32(out + 16, f->flags);
    for (i = 0; i < 6u; ++i) out[20 + i] = game_id[i];
    put32(out + 28, index);
    put32(out + 32, time);
}

/* Character by character: the blob has no constant data. */
static char* put_dir(char* p) {
    *p++ = '/'; *p++ = 's'; *p++ = 'h'; *p++ = 'a'; *p++ = 'r'; *p++ = 'e'; *p++ = 'd'; *p++ = '2';
    *p++ = '/'; *p++ = 'r'; *p++ = 'i'; *p++ = 'f'; *p++ = 't'; *p++ = 'w'; *p++ = 'i'; *p++ = 'i';
    return p;
}

void rtshot_dir(char* out) {
    *put_dir(out) = 0;
}

void rtshot_path(char* out, uint32_t number) {
    char* p = put_dir(out);
    uint32_t n = number % 10000u, place;
    *p++ = '/'; *p++ = 's'; *p++ = 'h'; *p++ = 'o'; *p++ = 't';
    for (place = 1000u; place != 0; place /= 10u) {
        *p++ = (char)('0' + n / place);
        n %= place;
    }
    *p++ = '.'; *p++ = 'r'; *p++ = 'a'; *p++ = 'w';
    *p = 0;
}

void rtshot_fs_attr(uint8_t* out, const char* path) {
    uint32_t i;
    for (i = 0; i < RTSHOT_FS_ATTR_BYTES; ++i) out[i] = 0;
    /* owner id (4), group id (2): IOS fills them in. */
    for (i = 0; i < 63u && path[i]; ++i) out[6 + i] = (uint8_t)path[i];
    out[70] = 3;  /* owner: read and write */
    out[71] = 3;  /* group */
    out[72] = 3;  /* others */
}
