// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace riftwii {

enum class SdWriteKind : unsigned char {
    Fine = 0,
    Full = 1,
    Locked = 2,
    Other = 3,
};

SdWriteKind classify_sd_write_errno(int err);

// All callbacks return 0 on success or an errno value on failure.
// On success open_file sets *fd_out; write_file writes one probe byte.
// free_bytes reports free bytes; queried only after both writes fail.
struct SdWriteOps {
    void* ctx = nullptr;
    int (*open_file)(void* ctx, int* fd_out) = nullptr;
    int (*make_dir)(void* ctx) = nullptr;
    int (*write_file)(void* ctx, int fd) = nullptr;
    int (*close_file)(void* ctx, int fd) = nullptr;
    int (*remove_file)(void* ctx) = nullptr;
    int (*free_bytes)(void* ctx, unsigned long long* bytes_out) = nullptr;
};

struct SdWriteResult {
    bool did_probe = false;
    SdWriteKind kind = SdWriteKind::Fine;
    int error = 0;
    int first_remove_error = 0;
    int remove_error = 0;
    bool free_bytes_known = false;
    unsigned long long free_bytes = 0;
};

// One-shot guard is shared scan-thread state:
// SdWriteProbe() probes once until SdWriteProbeReset() re-arms it.
// Probe only on scan thread; reset only there, or on menu thread after join.
void SdWriteProbeReset();
bool SdWriteProbe(bool sd_mounted, const SdWriteOps& ops, SdWriteResult* out);

}  // namespace riftwii
