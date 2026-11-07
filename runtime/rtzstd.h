/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef RIFTWII_RTZSTD_H
#define RIFTWII_RTZSTD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One independent RFC 8878 frame, without a dictionary. Input, output and
 * workspace must be disjoint. Workspace needs four-byte alignment. No heap,
 * libc, globals or output padding are required. Errors may leave partial
 * output; callers must discard it. Each call resets all frame history.
 * Literals use the unused output tail, rather than a private 128 KiB buffer. */
#define RTZSTD_WORKSPACE_BYTES 10784u
void* rtzstd_init(void* workspace, size_t bytes);
int32_t rtzstd_decode(void* workspace, uint8_t* output, uint32_t capacity,
                     const uint8_t* input, uint32_t length);

#ifdef __cplusplus
}
#endif
#endif
