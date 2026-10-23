/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The way back's decisions (rtleave.h). */
#include "rtleave.h"

/* The request block's words (Brawl's 2007 SDK to Kamen Rider: Super
 * Climax Heroes' 2010 one). */
#define RTLEAVE_COMMAND 0u
#define RTLEAVE_ARG0 3u          /* open: path; write: buffer; ioctl: its number */
#define RTLEAVE_ARG1 4u          /* open: mode; ioctl: input buffer */
#define RTLEAVE_ARG2 5u          /* ioctl: input length */
#define RTLEAVE_OPEN 1u
#define RTLEAVE_WRITE 4u
#define RTLEAVE_IOCTL 6u
#define RTLEAVE_OPEN_WRITE 2u    /* the mode's write bit */
/* /dev/fs ioctls that change the NAND (wiibrew): CreateDir, SetAttr,
 * Delete, Rename, CreateFile. Delete and Rename take a path first; the
 * creates and SetAttr an owner and group (6 bytes) before it. */
#define RTLEAVE_FS_CHANGES ((1u << 3) | (1u << 5) | (1u << 7) | (1u << 8) | (1u << 9))
#define RTLEAVE_FS_PATH_AFTER_OWNER 6u

/* A file on the NAND: a path that is not a device's ("/dev/..."). */
static int rtleave_nand_path(const uint8_t* p) {
    if (p == 0 || p[0] != '/') return 0;
    return !(p[1] == 'd' && p[2] == 'e' && p[3] == 'v' && p[4] == '/');
}

int rtleave_held(const uint8_t* status) {
    uint32_t i;
    for (i = 0; i < RTLEAVE_PADS; ++i) {
        const uint8_t* s = status + i * RTLEAVE_STATUS_BYTES;
        const uint32_t buttons = ((uint32_t)s[0] << 8) | s[1];
        if ((int8_t)s[10] == 0 && (buttons & RTLEAVE_BUTTONS) == RTLEAVE_BUTTONS) return 1;
    }
    return 0;
}

int rtleave_save_request(const uint32_t* block, const uint8_t* arg0, const uint8_t* arg1) {
    const uint32_t command = block[RTLEAVE_COMMAND];
    if (command == RTLEAVE_WRITE) return 1;
    if (command == RTLEAVE_OPEN) {
        return (block[RTLEAVE_ARG1] & RTLEAVE_OPEN_WRITE) != 0 && rtleave_nand_path(arg0);
    }
    if (command == RTLEAVE_IOCTL) {
        const uint32_t ioctl = block[RTLEAVE_ARG0];
        const uint32_t length = block[RTLEAVE_ARG2];
        if (ioctl >= 32u || ((RTLEAVE_FS_CHANGES >> ioctl) & 1u) == 0 || arg1 == 0 || length == 0) return 0;
        if (rtleave_nand_path(arg1)) return 1;
        return length > RTLEAVE_FS_PATH_AFTER_OWNER && rtleave_nand_path(arg1 + RTLEAVE_FS_PATH_AFTER_OWNER);
    }
    return 0;
}

void rtleave_note_save(struct rtleave* l, uint32_t now) { l->activity = now | 1u; }

int rtleave_due(struct rtleave* l, int held, uint32_t now, int resident_wrote, uint32_t resident_last_write) {
    if (!held) {
        l->since = 0;
        return 0;
    }
    if (l->since == 0) {
        l->since = now | 1u;
        return 0;
    }
    if (now - l->since < l->hold) return 0;
    return rtleave_quiet(l, now, resident_wrote, resident_last_write);
}

int rtleave_quiet(const struct rtleave* l, uint32_t now, int resident_wrote, uint32_t resident_last_write) {
    if (l->activity != 0 && now - l->activity < l->quiet) return 0;
    if (resident_wrote && now - resident_last_write < l->quiet) return 0;
    return 1;
}
