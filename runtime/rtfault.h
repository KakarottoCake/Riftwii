/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A game's crash, as the fault blob (runtime/fault) records it: the
 * record it writes to the NAND when the game's exception handler runs,
 * which the menu turns into sd:/riftwii/gamecrash.txt at its next start
 * (riftwii/gamefault.hpp). Big-endian, as the Wii writes it.
 *
 * Sources (no code taken from any of them):
 *  - the PowerPC 750CL user's manual: the exceptions, SRR0/SRR1, DSISR
 *    and DAR, and the EABI stack frame (back chain at 0, saved LR at 4
 *    of the caller's frame);
 *  - YAGCD and Dolphin's behaviour: the OSContext layout (GPRs at 0, CR
 *    0x80, LR 0x84, CTR 0x88, XER 0x8C, SRR0 0x198, SRR1 0x19C) and the
 *    SDK's exception numbers;
 *  - wiibrew, "/dev/fs": CreateDir, CreateFile and the attribute block. */
#ifndef RIFTWII_RTFAULT_H
#define RIFTWII_RTFAULT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RTFAULT_MAGIC 0x52574743u  /* "RWGC" */
#define RTFAULT_VERSION 1u
#define RTFAULT_RECORD_BYTES 512u
#define RTFAULT_FRAMES 24u         /* saved return addresses from the stack */
#define RTFAULT_CODE_WORDS 8u      /* the instructions around SRR0, from SRR0 - 16 */
#define RTFAULT_VERSION_BYTES 24u  /* RiftWii's version, NUL-padded */

/* The SDK's exception numbers (__OSException). */
#define RTFAULT_MACHINE_CHECK 1u
#define RTFAULT_DSI 2u
#define RTFAULT_ISI 3u
#define RTFAULT_ALIGNMENT 5u
#define RTFAULT_PROGRAM 6u
#define RTFAULT_FLOATING_POINT 7u
#define RTFAULT_DECREMENTER 8u

/* OSContext word offsets. */
#define RTFAULT_CTX_CR 0x20u
#define RTFAULT_CTX_LR 0x21u
#define RTFAULT_CTX_CTR 0x22u
#define RTFAULT_CTX_XER 0x23u
#define RTFAULT_CTX_SRR0 0x66u
#define RTFAULT_CTX_SRR1 0x67u

/* SRR1's floating-point enabled exception bit (a program exception the
 * game's own FP handler may take). */
#define RTFAULT_SRR1_FPE 0x00100000u

struct rtfault_record {
    uint32_t magic;
    uint32_t version;
    char game[8];                      /* 0x80000000: ID, disc number, version */
    uint32_t exception;
    uint32_t srr0, srr1, dsisr, dar;
    uint32_t lr, cr, ctr, xer;
    uint32_t gpr[32];
    uint32_t tb_hi, tb_lo;             /* the time base: the loader sets it to seconds since 2000 */
    uint32_t ticks_per_second;
    uint32_t frame_count;
    uint32_t frames[RTFAULT_FRAMES];
    uint32_t code_count;               /* words of code[] read (0 when SRR0 is not in RAM) */
    uint32_t code[RTFAULT_CODE_WORDS];
    char riftwii[RTFAULT_VERSION_BYTES];
    uint32_t reserved[(RTFAULT_RECORD_BYTES - 352u) / 4u];
};

/* Whether exception `exception` (with SRR1 `srr1`) is a crash worth
 * recording: not the decrementer, not a floating-point exception the
 * game may handle itself. */
int rtfault_is_crash(uint32_t exception, uint32_t srr1);

/* Whether `address` is a word in MEM1 or MEM2 (cached). */
int rtfault_in_ram(uint32_t address);

/* The record from the exception's arguments; `memory_ok` says whether
 * the stack and code may be read at their addresses (the blob, not the
 * host tests). */
void rtfault_fill(struct rtfault_record* r, uint32_t exception, const uint32_t* context, uint32_t dsisr, uint32_t dar,
                  const char* game, uint32_t tb_hi, uint32_t tb_lo, uint32_t ticks_per_second, int memory_ok);

/* "/shared2/riftwii/crash.bin" (26 bytes and its NUL). */
void rtfault_path(char* out);

#ifdef __cplusplus
}
#endif

#endif
