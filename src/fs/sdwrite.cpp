// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/sdwrite.hpp"

#include <cerrno>

namespace riftwii {

SdWriteKind classify_sd_write_errno(int err) {
    if (err == 0) {
        return SdWriteKind::Fine;
    }
    if (err == ENOSPC) {
        return SdWriteKind::Full;
    }
    if (err == EROFS) {
        return SdWriteKind::Locked;
    }
    return SdWriteKind::Other;
}

namespace {
bool g_probed = false;
constexpr unsigned long long kFullFreeBytesThreshold = 64ULL * 1024ULL;  // A few clusters, room for nothing RiftWii writes.

int ProbeOnce(const SdWriteOps& ops, int* remove_error_out) {
    *remove_error_out = 0;
    int fd = -1;
    int open_error = ops.open_file(ops.ctx, &fd);
    if (open_error == ENOENT) {
        int mkdir_error = ops.make_dir(ops.ctx);
        if (mkdir_error == 0 || mkdir_error == EEXIST) {
            open_error = ops.open_file(ops.ctx, &fd);
            if (open_error != 0) {
                return open_error;
            }
        } else {
            return mkdir_error;
        }
    } else if (open_error != 0) {
        return open_error;
    }
    int write_error = ops.write_file(ops.ctx, fd);
    int close_error = ops.close_file(ops.ctx, fd);
    int remove_error = ops.remove_file(ops.ctx);
    *remove_error_out = remove_error;
    if (write_error != 0) {
        return write_error;
    }
    return close_error;
}
}  // namespace

void SdWriteProbeReset() {
    g_probed = false;
}

bool SdWriteProbe(bool sd_mounted, const SdWriteOps& ops, SdWriteResult* out) {
    if (g_probed) {
        if (out != nullptr) {
            out->did_probe = false;
        }
        return false;
    }
    g_probed = true;
    if (out != nullptr) {
        out->did_probe = true;
        out->kind = SdWriteKind::Fine;
        out->error = 0;
        out->first_remove_error = 0;
        out->remove_error = 0;
        out->free_bytes_known = false;
        out->free_bytes = 0;
    }
    if (!sd_mounted) {
        return true;
    }
    int first_remove = 0;
    int first_error = ProbeOnce(ops, &first_remove);
    if (first_error == 0) {
        if (out != nullptr) {
            out->kind = SdWriteKind::Fine;
            out->remove_error = first_remove;
        }
        return true;
    }
    int second_remove = 0;
    int second_error = ProbeOnce(ops, &second_remove);
    if (out != nullptr) {
        out->first_remove_error = first_remove;
        out->remove_error = second_remove;
        out->error = second_error;
        out->free_bytes_known = false;
        out->free_bytes = 0;
        if (second_error == 0) {
            out->kind = SdWriteKind::Fine;
        } else {
            out->kind = classify_sd_write_errno(second_error);
            if (second_error != EROFS && ops.free_bytes != nullptr) {
                unsigned long long free_bytes = 0;
                int free_error = ops.free_bytes(ops.ctx, &free_bytes);
                if (free_error == 0) {
                    out->free_bytes_known = true;
                    out->free_bytes = free_bytes;
                    if (free_bytes < kFullFreeBytesThreshold) {
                        out->kind = SdWriteKind::Full;
                    }
                }
            }
        }
    }
    return true;
}

}  // namespace riftwii
