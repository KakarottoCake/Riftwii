/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A packed DOL (tools/pack_dol.py): this stub at DOLPACK_BASE and, right
 * under it, a table and one Zstandard frame per section of the DOL it
 * stands for. The Homebrew Channel or RiftWii's channel loads both as
 * any DOL; the stub unpacks the sections to their own addresses, clears
 * the DOL's BSS, moves the command line to where a loader would have put
 * it for that DOL, and jumps to its entry. Everything is big endian. */
#ifndef RIFTWII_DOLPACK_H
#define RIFTWII_DOLPACK_H

#include <stdint.h>

/* Where the stub is linked (dolpack.ld): above RiftWii's channel
 * forwarders and their heaps, below RiftWii's own sections (0x80a00000),
 * which leaves the packed sections room under it. */
#define DOLPACK_BASE 0x80900000u

/* The fields after the "_arg" block at the stub's start (dolpack_start.S),
 * which the packer finds by DOLPACK_FIELDS_MAGIC. */
#define DOLPACK_FIELDS_MAGIC 0x52575043u /* "RWPC" */

#define DOLPACK_TABLE_MAGIC 0x5257504Bu  /* "RWPK" */
#define DOLPACK_TABLE_VERSION 1u
#define DOLPACK_MAX_SECTIONS 18u

struct dolpack_section {
    uint32_t address;  /* where it goes */
    uint32_t size;     /* its bytes there */
    uint32_t offset;   /* its frame, from the table's start */
    uint32_t bytes;    /* the frame's bytes */
};

struct dolpack_table {
    uint32_t magic;        /* DOLPACK_TABLE_MAGIC */
    uint32_t version;      /* DOLPACK_TABLE_VERSION */
    uint32_t count;        /* sections, at most DOLPACK_MAX_SECTIONS */
    uint32_t entry;        /* the DOL's entry */
    uint32_t text0;        /* its first text section, where its "_arg" block is */
    uint32_t bss_address;
    uint32_t bss_size;
    uint32_t end;          /* past its last section and BSS: where a loader puts the command line */
    struct dolpack_section section[];
};

#endif
