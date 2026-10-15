/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Wii U Pro Controller translator: what the Wii Remote log blob
 * (runtime/wlog) does to the game's Bluetooth traffic so a Wii U Pro
 * Controller reaches the game as a Wii Remote with a Classic Controller
 * Pro plugged in. Freestanding (no libc, no constant data) so the host
 * tests cover it.
 *
 * A Pro Controller speaks the Wii Remote's protocol, but its buttons and
 * sticks come only in the extension bytes, in a layout of its own, behind
 * an extension ID no Wii game knows (0x0120). For a link that is a Pro
 * Controller (its address among the console's pairings named
 * "Nintendo RVL-CNT-01-UC", or its extension ID read back as the Pro's):
 *
 *  - Out: the reporting mode the game asks for is kept, and the
 *    controller is asked for 0x34 instead (buttons and 19 extension
 *    bytes), the only mode with room for all of its 11.
 *  - In: each 0x34 report is rebuilt in the game's mode, the extension
 *    bytes as a Classic Controller's (sticks scaled, the button bytes are
 *    the same bits), the accelerometer at rest, no IR points.
 *  - The extension ID reads back as a Classic Controller Pro's; its
 *    calibration, and the Wii Remote's accelerometer calibration, as
 *    standard values; an error on a write's acknowledgement (the Pro has
 *    no IR camera or speaker) as success.
 *  - Encryption: a game that turns on the extension's encryption (0xAA
 *    to 0xA400F0, then a 16-byte key at 0xA40040) decrypts every
 *    extension byte it gets, so what the translator hands it is encrypted
 *    with that key. The per-byte tables come from the key through the
 *    game's own lookup tables (an answer table of 7 rows of 6 bytes, a key
 *    box and eight table boxes, 256 bytes each), which the loader finds
 *    in the game: RiftWii carries none of them. The Pro gets 0x55 in the
 *    0xAA's place so its own bytes stay plain (and if it encrypts them
 *    anyway, that shows in its first report and they are decrypted). An
 *    older game's single 0x00 at 0xA40040 means the fixed 0x17 transform.
 *  - What a Pro Controller cannot take never reaches it: on a real one
 *    (2610-407, a tester's Bluetooth trace) the game's first request, a
 *    read of the Wii Remote's own memory (EEPROM 0x1770, the firmware
 *    probe), made the Pro hang up 27 ms later, at every connection. The
 *    Pro gets only what the Wii U asks of it: rumble, LEDs, the reporting
 *    mode, status, and reads and writes of the extension's registers
 *    (0xA4xxxx). A read of anything else goes out as a read of the
 *    extension's calibration and its answers come back as the Wii
 *    Remote's would (the accelerometer calibration ours, the rest erased,
 *    error 8 past the end of its memory, as a Wii Remote answers the
 *    probe). Any other report goes out as a status request when the game
 *    waits for its acknowledgement (the status answer comes back as that
 *    acknowledgement), as the LEDs again when it does not.
 *
 * Sources (no code taken from any of them):
 *  - wiibrew, "Wiimote": the HID reports (0x12, 0x16, 0x17 out; 0x20,
 *    0x21, 0x22, 0x30-0x3D in), the data reporting modes and their
 *    layouts, the EEPROM calibration and its checksum.
 *  - wiibrew, "Wiimote/Extension Controllers" and "Classic Controller":
 *    the extension IDs, the Classic Controller's six data bytes and its
 *    calibration block.
 *  - wiibrew, "Wii U Pro Controller": its extension ID and data bytes.
 *  - wiibrew, "Wiimote/Extension Controllers": the encryption's per-byte
 *    transform; Dolphin's WiimoteEmu Encryption.cpp (GPL-2.0-or-later):
 *    how the key and the lookup tables make the two 8-byte tables. Our own
 *    code; the tables themselves are the game's.
 *  - the Bluetooth core specification: HCI events (Connection Complete,
 *    Disconnection Complete), ACL and L2CAP headers. */
#ifndef RIFTWII_RTPRO_H
#define RIFTWII_RTPRO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RTPRO_LINKS 7u        /* ACL connections followed at once */
#define RTPRO_ADDRESSES 8u    /* Pro Controller pairings the loader hands over */
#define RTPRO_EVERY_LINK 0xFFFFFFFFu  /* as the count: every link is taken as a Pro (Dolphin tests) */
#define RTPRO_READS 4u        /* memory reads awaiting their answer, per link */
#define RTPRO_STATUSES 8u     /* status requests awaiting their answer, per link */

/* rtpro_link.pro */
#define RTPRO_NOT_PRO 0u
#define RTPRO_BY_ADDRESS 1u   /* its address is a Pro Controller pairing */
#define RTPRO_BY_ID 2u        /* its extension ID read back as the Pro's */

/* The reporting mode the controller is asked for. */
#define RTPRO_MODE 0x34u

struct rtpro_read {
    uint32_t address;         /* 24 bits; RTPRO_REGISTERS when in register space */
    uint16_t size;
    uint16_t used;            /* RTPRO_READ_* */
    uint32_t asked;           /* RTPRO_READ_STAND_IN: what the game asked for (address as above) */
    uint32_t asked_size;
};
#define RTPRO_REGISTERS 0x01000000u
#define RTPRO_READ_FREE 0u
#define RTPRO_READ_SENT 1u        /* sent to the controller as the game asked */
#define RTPRO_READ_STAND_IN 2u    /* a calibration read went in its place (answered as the Wii Remote's) */
/* The Wii Remote's memory (EEPROM) ends here: a read past it gets error 8. */
#define RTPRO_EEPROM_END 0x1700u

/* rtpro_link.cipher */
#define RTPRO_PLAIN 0u        /* the game reads the extension's bytes as they are */
#define RTPRO_ARMED 1u        /* 0xAA written: the key is coming */
#define RTPRO_KEYED 2u        /* the game decrypts with the link's tables */
#define RTPRO_NO_TABLES 3u    /* it decrypts, but its key did not check out against the game's tables */

/* rtpro_link.pro_encrypts */
#define RTPRO_UNKNOWN 0u
#define RTPRO_SENDS_PLAIN 1u
#define RTPRO_SENDS_ENCRYPTED 2u

struct rtpro_link {
    uint16_t handle;
    uint8_t used;
    uint8_t pro;              /* RTPRO_* */
    uint8_t mode;             /* the reporting mode the game asked for, 0 before */
    uint8_t encrypted;        /* the game turned on encryption */
    uint8_t cipher;           /* RTPRO_PLAIN.. */
    uint8_t pro_encrypts;     /* RTPRO_UNKNOWN.. */
    struct rtpro_read reads[RTPRO_READS];
    uint32_t translated;      /* reports rebuilt */
    uint16_t key_parts;       /* which of the key's 16 bytes the game wrote */
    uint8_t pad2[2];
    uint8_t key[16];          /* as written, 0xA40040 on */
    uint8_t xor_table[8];     /* encrypted = (plain - add_table[a % 8]) ^ xor_table[a % 8] */
    uint8_t add_table[8];
    /* The answers expected to the status requests sent, oldest first: 0
     * for the game's own, else the report ID whose acknowledgement the
     * answer stands for. */
    uint8_t statuses[RTPRO_STATUSES];
    uint8_t status_count;
    uint8_t leds;             /* the player LEDs the game last set (0x11's first byte, its top bits) */
    uint8_t pad3[2];
};

struct rtpro_state {
    struct rtpro_link links[RTPRO_LINKS];
    uint32_t pros;            /* links found to be Pro Controllers */
    uint32_t translated;      /* reports rebuilt, all links */
    uint32_t ids;             /* extension IDs answered as a Classic Controller Pro */
    uint32_t calibrations;    /* calibration reads answered */
    uint32_t acks;            /* acknowledgement errors cleared */
    uint32_t modes;           /* reporting modes changed to RTPRO_MODE */
    uint32_t keys;            /* encryption keys turned into tables */
    uint32_t bad_keys;        /* keys that did not check out (or no tables) */
    uint32_t memory_reads;    /* reads of the Wii Remote's memory answered in the Pro's place */
    uint32_t replaced;        /* other reports a Pro cannot take, sent as status or LEDs */
    const uint8_t* tables;    /* the game's lookup tables (rtpro_set_tables), 0: none found */
};

/* The game's lookup tables for the extension's encryption: 48 bytes (an
 * answer table of 7 rows of 6, padded), a key box, then eight table boxes
 * of 256 bytes. */
#define RTPRO_TABLE_BYTES (48u + 9u * 256u)
void rtpro_set_tables(struct rtpro_state* st, const uint8_t* tables);

/* What one packet was, for the trace (rtpro_acl_in/out's results). */
#define RTPRO_SEEN 1u         /* an HID report on a followed link */
#define RTPRO_CHANGED 2u      /* rewritten */
#define RTPRO_DATA 4u         /* a data report (0x30-0x3F) */

void rtpro_init(struct rtpro_state* st);

/* An HCI event (code, parameter length, parameters) from the dongle's
 * interrupt endpoint: Connection Complete starts following a link (a Pro
 * Controller when its address is one of the `count` in `addresses`, six
 * bytes each as in SYSCONF; every link when `count` is RTPRO_EVERY_LINK),
 * Disconnection Complete ends it. */
void rtpro_event(struct rtpro_state* st, const uint8_t* addresses, uint32_t count, const uint8_t* event,
                 uint32_t size);

/* An ACL packet the game sends (bulk-out), changed in place. `size` is
 * its size, and its new one after (a report put in another's place can
 * be shorter: the caller sends that many bytes). Returns RTPRO_* bits. */
uint32_t rtpro_acl_out(struct rtpro_state* st, uint8_t* acl, uint32_t* size);

/* An ACL packet the game receives (bulk-in), `size` bytes in a buffer of
 * `room`, changed in place. Returns the packet's new size; `what` gets
 * RTPRO_* bits. */
uint32_t rtpro_acl_in(struct rtpro_state* st, uint8_t* acl, uint32_t size, uint32_t room, uint32_t* what);

/* The Classic Controller's six data bytes from a Pro Controller's first
 * eleven extension bytes. */
void rtpro_classic(const uint8_t* pro, uint8_t* classic);

#ifdef __cplusplus
}
#endif

#endif
