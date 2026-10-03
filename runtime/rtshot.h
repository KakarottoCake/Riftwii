/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* In-game screenshots: the logic the screenshot blob (runtime/shot) runs,
 * freestanding (no libc, no constant data) so the host tests cover it.
 *
 *  - The combo. Wii Remote: hold 1, press HOME. GameCube controller: hold
 *    L and R, press Down. The press that completes it is hidden from the
 *    game for as long as it is held, so HOME opens no HOME Menu. The Wii
 *    Remote's buttons are read from the game's own Bluetooth traffic:
 *    each input report IOS hands the game (an HCI ACL packet from the
 *    bulk-in endpoint of /dev/usb/oh1/57e/305) is looked at, and changed
 *    in place, before the game's callback sees it. A GameCube pad's are
 *    read from PADRead's results.
 *  - The picture: what the video interface shows, from its registers.
 *  - The raw file the menu turns into a PNG (riftwii/shotfile.hpp): a
 *    64-byte header, then the rows.
 *
 * Sources (no code taken from any of them):
 *  - wiibrew, "Wiimote": HID over L2CAP (the 0xA1 input prefix), the
 *    reports that begin with the core buttons (0x20-0x22, 0x30-0x37,
 *    0x3E/0x3F) and the button bits.
 *  - the Bluetooth core specification: the HCI ACL and L2CAP headers.
 *  - wiibrew, "/dev/usb/oh1" and libogc's usb.c: the USBV0 bulk message
 *    ioctlv (1) with the endpoint and length as its two inputs.
 *  - YAGCD and libogc's video.c: the VI registers (VTR's active lines,
 *    the picture configuration's stride and width in 16-pixel units, the
 *    top field base with its page-offset bit).
 *  - wiibrew, "/dev/fs": CreateDir, CreateFile and the attribute block. */
#ifndef RIFTWII_RTSHOT_H
#define RIFTWII_RTSHOT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RTSHOT_REMOTES 4u
#define RTSHOT_PADS 4u
#define RTSHOT_HEADER_BYTES 64u
#define RTSHOT_MAX_WIDTH 720u
#define RTSHOT_MAX_LINES 576u
#define RTSHOT_MAGIC 0x52575348u   /* "RWSH", as riftwii/shotfile.hpp */
#define RTSHOT_VERSION 1u
#define RTSHOT_DOUBLE_LINES 1u     /* each line was shown twice */
#define RTSHOT_FRAME_BYTES (RTSHOT_HEADER_BYTES + RTSHOT_MAX_WIDTH * 2u * RTSHOT_MAX_LINES)

/* The Bluetooth dongle's ACL input endpoint and the USBV0 bulk ioctlv. */
#define RTSHOT_ACL_IN 0x82u
#define RTSHOT_USBV0_BLKMSG 1u

/* Wii Remote core buttons, the report's second button byte. */
#define RTSHOT_REMOTE_ONE 0x02u
#define RTSHOT_REMOTE_HOME 0x80u
/* PADStatus button bits. */
#define RTSHOT_PAD_DOWN 0x0004u
#define RTSHOT_PAD_R 0x0020u
#define RTSHOT_PAD_L 0x0040u
#define RTSHOT_PAD_TRIGGER 0x80u   /* an analog trigger this far down counts as held */

struct rtshot_remote {
    uint16_t handle;   /* the ACL connection */
    uint8_t used;
    uint8_t buttons;   /* the second button byte of its last report, as sent */
    uint8_t masking;   /* HOME is hidden until it is let go */
    uint8_t pad[3];
};

struct rtshot_input {
    struct rtshot_remote remotes[RTSHOT_REMOTES];
    uint32_t next_remote;          /* the slot a new connection takes */
    uint8_t pad_down[RTSHOT_PADS];     /* Down in the last PADRead */
    uint8_t pad_masking[RTSHOT_PADS];
    uint32_t reports;              /* Wii Remote reports seen */
};

/* One ACL packet of `size` bytes from the Bluetooth bulk-in endpoint.
 * Returns 1 when it completed the combo. */
int rtshot_acl(struct rtshot_input* in, uint8_t* acl, uint32_t size);

/* PADRead's four 12-byte PADStatus records. Returns 1 when one completed
 * the combo. */
int rtshot_pads(struct rtshot_input* in, uint8_t* status);

/* What the video interface shows. */
struct rtshot_frame {
    uint32_t address;     /* physical, of the first line */
    uint32_t width;       /* pixels */
    uint32_t lines;
    uint32_t stride;      /* bytes from one line to the next */
    uint32_t flags;       /* RTSHOT_DOUBLE_LINES */
};

/* From VTR (0xCC002000), the picture configuration (0xCC002048) and the
 * top field base (0xCC00201C). Returns 0 when they describe nothing a
 * screenshot can take (VI off, odd sizes, outside RAM). */
int rtshot_frame(uint16_t vtr, uint16_t picture, uint32_t top_base, struct rtshot_frame* out);

/* The raw file's header. */
void rtshot_header(uint8_t* out, const struct rtshot_frame* f, const uint8_t* game_id, uint32_t index,
                   uint32_t time);

/* "/shared2/riftwii/shotNNNN.raw" (NNNN = number % 10000) into 32 bytes. */
void rtshot_path(char* out, uint32_t number);
/* "/shared2/riftwii". */
void rtshot_dir(char* out);

/* The /dev/fs attribute block (0x4C bytes) for CreateDir and CreateFile
 * of `path`: read and write for everyone, so the menu, which runs under
 * another title's ids, can read and delete it. */
#define RTSHOT_FS_ATTR_BYTES 0x4Cu
void rtshot_fs_attr(uint8_t* out, const char* path);

#ifdef __cplusplus
}
#endif

#endif
