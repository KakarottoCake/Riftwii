// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "sdwritecheck.hpp"

#include "log.hpp"
#include "riftwii/sdwrite.hpp"

#include <ogc/lwp_watchdog.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace riftwii::wii {
namespace {
// The kind can be read from the menu while the scan thread still runs, so it is atomic.
// The notice flag is only touched on the menu thread.
// The probe guard lives in the pure helper and is only touched on the scan
// thread, or on the menu thread after the scan thread has joined.
std::atomic<::riftwii::SdWriteKind> g_kind{::riftwii::SdWriteKind::Fine};
bool g_notice_shown = false;
constexpr const char* kPath = "sd:/riftwii/writetest.tmp";
constexpr const char* kDir = "sd:/riftwii";

int RealOpen(void*, int* fd_out) {
    errno = 0;
    int fd = ::open(kPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        int open_error = errno;
        if (open_error == 0) {
            open_error = EIO;
        }
        return open_error;
    }
    *fd_out = fd;
    return 0;
}

int RealMkdir(void*) {
    errno = 0;
    if (::mkdir(kDir, 0777) == 0) {
        return 0;
    }
    int mkdir_error = errno;
    if (mkdir_error == 0) {
        mkdir_error = EIO;
    }
    return mkdir_error;
}

int RealWrite(void*, int fd) {
    char probe_byte = 'x';
    errno = 0;
    ssize_t write_result = ::write(fd, &probe_byte, 1);
    if (write_result == 1) {
        return 0;
    }
    int write_error = errno;
    if (write_error == 0) {
        write_error = EIO;
    }
    return write_error;
}

int RealClose(void*, int fd) {
    errno = 0;
    if (::close(fd) == 0) {
        return 0;
    }
    int close_error = errno;
    if (close_error == 0) {
        close_error = EIO;
    }
    return close_error;
}

int RealRemove(void*) {
    errno = 0;
    if (std::remove(kPath) == 0) {
        return 0;
    }
    int remove_error = errno;
    if (remove_error == 0) {
        remove_error = EIO;
    }
    return remove_error;
}

int RealFreeBytes(void*, unsigned long long* bytes_out) {
    errno = 0;
    struct statvfs info{};
    if (::statvfs("sd:/", &info) != 0) {
        int free_error = errno;
        if (free_error == 0) {
            free_error = EIO;
        }
        return free_error;
    }
    unsigned long long unit = static_cast<unsigned long long>(info.f_frsize);
    if (unit == 0) {
        unit = static_cast<unsigned long long>(info.f_bsize);
    }
    unsigned long long blocks = static_cast<unsigned long long>(info.f_bavail);
    *bytes_out = blocks * unit;
    return 0;
}

void LogRemoveError(int remove_error) {
    if (remove_error == 0) {
        return;
    }
    const char* detail = ::strerror(remove_error);
    if (detail == nullptr) {
        detail = "unknown";
    }
    logf("SD card: the test file could not be removed (%s)\n", detail);
}
}  // namespace

::riftwii::SdWriteKind SdWriteKindGet() {
    return g_kind.load();
}

bool SdWriteNoticeShown() {
    return g_notice_shown;
}

void SdWriteNoticeMarkShown() {
    g_notice_shown = true;
}

void SdWriteReset() {
    g_kind.store(::riftwii::SdWriteKind::Fine);
    g_notice_shown = false;
    ::riftwii::SdWriteProbeReset();
}

void ProbeSdWrite(bool sd_mounted) {
    // Check free space only after failed writes: statvfs can walk the whole FAT on a big card.
    // libfat reports a full-card write as EIO, so the probe needs the free-space query to tell full from faulty.
    u64 t0 = gettime();
    ::riftwii::SdWriteOps ops{};
    ops.ctx = nullptr;
    ops.open_file = RealOpen;
    ops.make_dir = RealMkdir;
    ops.write_file = RealWrite;
    ops.close_file = RealClose;
    ops.remove_file = RealRemove;
    ops.free_bytes = RealFreeBytes;
    ::riftwii::SdWriteResult result{};
    if (!::riftwii::SdWriteProbe(sd_mounted, ops, &result)) {
        return;
    }
    if (!result.did_probe) {
        return;
    }
    g_kind.store(result.kind);
    if (result.kind == ::riftwii::SdWriteKind::Fine) {
        LogRemoveError(result.first_remove_error);
        LogRemoveError(result.remove_error);
        return;
    }
    const char* detail = ::strerror(result.error);
    if (detail == nullptr) {
        detail = "unknown";
    }
    char free_text[32];
    if (result.free_bytes_known) {
        unsigned long long free_kib = result.free_bytes / 1024ULL;
        std::snprintf(free_text, sizeof(free_text), "%llu KiB", static_cast<unsigned long long>(free_kib));
    } else {
        std::snprintf(free_text, sizeof(free_text), "unknown");
    }
    u64 probe_msec = diff_msec(t0, gettime());
    switch (result.kind) {
    case ::riftwii::SdWriteKind::Full:
        logf("SD card: a test file could not be written (%s), %s free: the card is full; settings, game choices, covers and logs may not be saved until space is freed\n", detail, free_text);
        break;
    case ::riftwii::SdWriteKind::Locked:
        logf("SD card: a test file could not be written (%s), %s free: the card may be locked or read-only; settings, game choices, covers and logs may not be saved\n", detail, free_text);
        break;
    default:
        logf("SD card: a test file could not be written (%s), %s free; settings, game choices, covers and logs may not be saved; check the card for errors\n", detail, free_text);
        break;
    }
    LogRemoveError(result.first_remove_error);
    LogRemoveError(result.remove_error);
    logf("SD card: write probe took %llu ms\n", static_cast<unsigned long long>(probe_msec));
}

}  // namespace riftwii::wii
