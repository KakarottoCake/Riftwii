/* SPDX-FileCopyrightText: 2026 RiftWii contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * RiftWii's return stub for homebrew apps: the layout the stub
 * (runtime/stub) and the loader (src/patching/returnstub.cpp,
 * wii/launch/channel.cpp) share.
 *
 * libogc's exit() ends in a jump to 0x80001800 when "STUBHAXX" is at
 * 0x80001804, and otherwise goes to the Wii Menu. The Homebrew Channel
 * leaves its own reload stub there, which is how an app gets back to it.
 * RiftWii started from its channel finds none, so an app it starts would
 * exit to the Wii Menu, where only a Wii Remote can go on. Before it
 * starts an app RiftWii puts this stub there instead: it launches the
 * first title it can of a short list, through /dev/es, each with the
 * ticket view the loader read for it beforehand, so the stub asks ES for
 * nothing but the launch itself.
 *
 * "RWRS" after "STUBHAXX" tells it from the Homebrew Channel's stub: a
 * RiftWii started by an app that RiftWii started finds this one, and its
 * way back is the RiftWii channel, not the Homebrew Channel.
 *
 * Every field is big-endian, as the Wii reads it. The image's own bytes
 * end at `size`; its stack follows, still under RTSTUB_LIMIT.
 */
#ifndef RIFTWII_RETURNSTUB_H
#define RIFTWII_RETURNSTUB_H

#define RTSTUB_BASE 0x80001800u
/* The debugger area runs to 0x80003000; the stub and its stack keep to
 * its first half. */
#define RTSTUB_LIMIT 0x80002400u

#define RTSTUB_OFFSET_HBC_MAGIC 0x04u  /* "STUB", then "HAXX" */
#define RTSTUB_OFFSET_MAGIC 0x0Cu      /* "RWRS" */
#define RTSTUB_OFFSET_VERSION 0x10u
#define RTSTUB_OFFSET_SIZE 0x14u       /* the image's bytes, as built */
#define RTSTUB_OFFSET_COUNT 0x18u      /* titles to try, in order */
#define RTSTUB_OFFSET_RESULT 0x1Cu     /* the stub's last result from IOS, for a debugger */
#define RTSTUB_OFFSET_TITLES 0x20u

/* One title to try: its ID, then its first ticket view, each where ES can
 * read it (32-byte aligned). */
#define RTSTUB_TITLE_BYTES 0x100u
#define RTSTUB_TITLE_VIEW 0x20u
#define RTSTUB_VIEW_BYTES 0xD8u
#define RTSTUB_MAX_TITLES 3u

#define RTSTUB_HBC_MAGIC_STUB 0x53545542u  /* "STUB" */
#define RTSTUB_HBC_MAGIC_HAXX 0x48415858u  /* "HAXX" */
#define RTSTUB_MAGIC 0x52575253u           /* "RWRS" */
#define RTSTUB_VERSION 1u

/* Where the code starts: after the header and the titles. */
#define RTSTUB_OFFSET_CODE (RTSTUB_OFFSET_TITLES + RTSTUB_MAX_TITLES * RTSTUB_TITLE_BYTES)

#endif
