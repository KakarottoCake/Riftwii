// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2011 Dimok
// SPDX-FileCopyrightText: 2011 Rodries
// SPDX-FileCopyrightText: 2009 Kwiirk
// SPDX-License-Identifier: GPL-3.0-or-later
#include "umsdev.hpp"

#include <gccore.h>
#include <ogc/ipc.h>
#include <ogc/machine/processor.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>

#include "log.hpp"
#include "skin.hpp"
#include "usbcatalog.hpp"

namespace riftwii::wii::ums {
namespace {

// d2x's EHCI module ioctls ('U' 'M' 'S' base).
constexpr s32 kUmsBase = ('U' << 24) | ('M' << 16) | ('S' << 8);
constexpr s32 kInit = kUmsBase + 0x1;
constexpr s32 kGetCapacity = kUmsBase + 0x2;
constexpr s32 kReadSectors = kUmsBase + 0x3;
// Which of the Wii's two USB ports d2x drives (0 unless told; set before
// the init, as USB Loader GX does). A d2x without it answers an error.
constexpr s32 kSetPort = kUmsBase + 0x83;
constexpr std::uint32_t kFatSectorBytes = 512;  // packs and RVZ games
constexpr std::uint32_t kBounceBytes = 32 * 1024;  // per request, as d2x reads

s32 g_fd = -1;
std::uint32_t g_sector_bytes = kFatSectorBytes;
// A failed open is not retried under the same IOS; its reason is kept
// for the later callers. An IOS reload clears both (Forget).
bool g_failed = false;
std::string g_failure;
// The device opened but the drive did not start (not there yet, or slow
// to come up after the reload): worth asking again (OpenWaiting).
bool g_not_ready = false;
std::uint8_t* g_bounce = nullptr;
std::uint32_t g_args[8] ATTRIBUTE_ALIGN(32);
ioctlv g_vec[3] ATTRIBUTE_ALIGN(32);
std::unique_ptr<ImageVolume> g_volume;
// The port the drive was last found on, tried first (it outlives Forget:
// the drive stays where it is plugged in).
std::uint32_t g_port = 0;

// Every call into d2x's USB module is sent asynchronously and waited for
// with a time limit. A tester's Wii U froze inside the first one after the
// reload into the cIOS (boot.log ended before any answer), and a request
// IOS never answers cannot be cancelled: once one is overdue the device
// counts as stuck for the rest of this run. Its fd is then never closed
// (the close would queue behind the stuck request and hang too), and the
// buffers the request points at (g_args, g_vec, g_bounce, the path) are
// static or never freed, so a late answer lands harmlessly.
constexpr int kCallLimitSeconds = 10;

// g_reply (with g_wake and g_limit_alarm) is shared by every request: ums
// is only ever called from one thread, the main thread running the launch,
// so one request is in flight at a time. Keep it that way; a call from a
// second thread would need its own record, queue and alarm.
struct Reply {
    volatile bool done = false;
    volatile bool timed_out = false;
    volatile s32 result = 0;
};
Reply g_reply;
bool g_stuck = false;
// The waiting thread sleeps on this queue; the reply or the time limit's
// alarm wakes it (both run in interrupt context, as libogc's own
// synchronous IPC wakes its caller from the reply interrupt).
lwpq_t g_wake = LWP_TQUEUE_NULL;
syswd_t g_limit_alarm = SYS_WD_NULL;
bool g_wait_ready = false;

s32 on_reply(s32 result, void* user) {
    Reply* reply = static_cast<Reply*>(user);
    reply->result = result;
    reply->done = true;
    LWP_ThreadSignal(g_wake);
    return 0;
}

void on_time_limit(syswd_t, void* user) {
    static_cast<Reply*>(user)->timed_out = true;
    LWP_ThreadSignal(g_wake);
}

bool prepare_wait() {
    if (g_wait_ready) return true;
    if (LWP_InitQueue(&g_wake) < 0) return false;
    if (SYS_CreateAlarm(&g_limit_alarm) < 0) {
        LWP_CloseQueue(g_wake);
        return false;
    }
    g_wait_ready = true;
    return true;
}

// Sends one request (`send` queues it with on_reply and &g_reply) and
// waits for its answer, up to the time limit. The answer is checked with
// interrupts off before each sleep, as libogc's __ipc_syncrequest does, so
// a reply that comes in between cannot be missed. On the time limit the
// device is marked stuck. `call` names the request for the log.
template <typename Send>
s32 call_limited(Send send, const char* call) {
    if (g_stuck) return IPC_EINVAL;
    if (!prepare_wait()) return IPC_ENOMEM;
    g_reply.done = false;
    g_reply.timed_out = false;
    timespec limit = {kCallLimitSeconds, 0};
    SYS_SetAlarm(g_limit_alarm, &limit, on_time_limit, &g_reply);
    const s32 queued = send();
    if (queued < 0) {
        SYS_CancelAlarm(g_limit_alarm);
        return queued;  // libogc could not queue it
    }
    u32 level;
    _CPU_ISR_Disable(level);
    while (!g_reply.done && !g_reply.timed_out) LWP_ThreadSleep(g_wake);
    _CPU_ISR_Restore(level);
    SYS_CancelAlarm(g_limit_alarm);
    if (g_reply.done) return g_reply.result;
    g_stuck = true;
    logf("USB (d2x): %s\n", d2x_usb_stuck_log_line(call, kCallLimitSeconds).c_str());
    return IPC_ENOMEM;  // any error: the caller sees a failed call
}

s32 ioctlv_limited(s32 ioctl, s32 in, s32 io, ioctlv* vec, const char* call) {
    if (g_fd < 0) return IPC_EINVAL;
    return call_limited([&] { return IOS_IoctlvAsync(g_fd, ioctl, in, io, vec, on_reply, &g_reply); }, call);
}

s32 open_limited(const char* path) {
    return call_limited([&] { return IOS_OpenAsync(path, 0, on_reply, &g_reply); }, "open");
}

// After a stuck call: forget the fd without closing it, and say so to
// every later caller.
bool fail_stuck(std::string& error) {
    g_fd = -1;
    error = kD2xStuckReason;
    g_failed = true;
    g_failure = error;
    g_not_ready = false;
    return false;
}

struct Started {
    s32 init = -1;
    s32 sectors = 0;
    std::uint32_t sector_bytes = 0;
    bool counted = false;
    bool size_ok = false;
    bool ok() const { return init >= 0 && counted && size_ok; }
};

// d2x's drive on `port`: false when this d2x cannot switch ports.
bool set_port(std::uint32_t port) {
    g_args[0] = port;
    g_vec[0].data = &g_args[0];
    g_vec[0].len = 4;
    DCFlushRange(g_args, sizeof(g_args));
    return ioctlv_limited(kSetPort, 1, 0, g_vec, "set port") >= 0;
}

// Whether the started drive's sector 0 ends in 0x55AA (a partition table,
// a FAT32, NTFS or exFAT volume) or starts "WBFS"; a DVD drive (2048-byte
// sectors) counts too. A Wii U's own drive has neither.
// `hash` is usb_sector0_hash of its sector 0, to tell it from another
// drive (0 when it could not be read).
bool HasVolume(std::uint32_t sector_bytes, std::uint64_t& hash) {
    hash = 0;
    if ((sector_bytes != 512 && sector_bytes != 2048) || !g_bounce) return false;
    g_sector_bytes = sector_bytes;
    std::uint8_t first[2048];
    if (!Read(0, 1, first)) return sector_bytes == 2048;
    hash = usb_sector0_hash(first, sector_bytes);
    if (sector_bytes == 2048) return true;
    return (first[510] == 0x55 && first[511] == 0xAA) || std::memcmp(first, "WBFS", 4) == 0;
}

Started start_drive() {
    Started s;
    s.init = ioctlv_limited(kInit, 0, 0, nullptr, "init");
    g_vec[0].data = &g_args[0];
    g_vec[0].len = 4;
    DCFlushRange(g_args, sizeof(g_args));
    s.sectors = ioctlv_limited(kGetCapacity, 0, 1, g_vec, "capacity");
    DCInvalidateRange(g_args, sizeof(g_args));
    s.sector_bytes = g_args[0];
    // The count comes back as the ioctl's result: a drive past 2^31
    // sectors (1 TiB of 512-byte sectors) reads as negative. IOS's own
    // errors are small negative numbers.
    s.counted = s.sectors > 0 || s.sectors < -0x100000;
    // 512 for a hard drive or stick, 2048 for a DVD drive.
    s.size_ok = s.sector_bytes >= kFatSectorBytes && s.sector_bytes <= kBounceBytes &&
                (s.sector_bytes & (s.sector_bytes - 1)) == 0;
    return s;
}

bool ReadBlocks(std::uint64_t lba, std::uint32_t count, std::uint8_t* out) { return Read(lba, count, out); }

}  // namespace

bool Open(std::string& error) {
    if (g_fd >= 0) return true;
    if (g_stuck) {
        error = kD2xStuckReason;
        return false;
    }
    if (g_failed) {
        error = g_failure;
        return false;
    }
    g_not_ready = false;
    static char path[] ATTRIBUTE_ALIGN(32) = "/dev/usb2";
    g_fd = open_limited(path);
    if (g_stuck) return fail_stuck(error);
    if (g_fd < 0) {
        // libogc's driver is still up: a failure here leaves the menu's
        // USB drive mounted.
        error = "this IOS (" + std::to_string(IOS_GetVersion()) + ") has no d2x USB device (" +
                std::to_string(g_fd) + "); packs and RVZ games on the USB drive need a d2x cIOS";
        g_fd = -1;
        g_failed = true;
        g_failure = error;
        return false;
    }
    // One driver for the drive: libogc's lets go before d2x's starts.
    release_usb_driver();
    // The port it was last found on first, then the other: d2x only looks
    // at port 0 unless told, and a drive in the other port never started
    // (a tester's: init -100 for as long as RiftWii waited). A drive that
    // starts but holds nothing RiftWii reads (a Wii U's own drive, beside
    // the games' in the other port) gives way to one that does.
    if (!g_bounce) g_bounce = skin::Mem2Alloc(kBounceBytes);
    // With several drives plugged in, the menu chose the one with the
    // games (usbcatalog.cpp): d2x must read that one, not merely the first
    // it can read, or the game's sectors come from the other drive.
    std::uint32_t menu_bytes = 0;
    std::uint64_t menu_hash = 0;
    const bool match = usb_picked_drive(menu_bytes, menu_hash);
    Started s, any;
    int found = -1, readable = -1, matched = -1;
    std::uint32_t last = 0;
    for (int attempt = 0; attempt < 2 && (match ? matched < 0 : readable < 0); ++attempt) {
        const std::uint32_t port = attempt == 0 ? g_port : 1u - g_port;
        logf("USB (d2x): starting the drive in port %u\n", static_cast<unsigned>(port));
        const bool switched = set_port(port);
        if (g_stuck) return fail_stuck(error);
        if (!switched && port != 0) continue;  // no port switch in this d2x: port 0 only
        s = start_drive();
        if (g_stuck) return fail_stuck(error);
        last = port;
        logf("USB (d2x): fd %d, port %u, init %d, %u sectors of %u bytes\n", static_cast<int>(g_fd),
             static_cast<unsigned>(port), static_cast<int>(s.init), static_cast<unsigned>(s.sectors),
             static_cast<unsigned>(s.sector_bytes));
        if (s.init < 0 || !s.counted) continue;
        if (found < 0) {
            found = static_cast<int>(port);
            any = s;
        }
        std::uint64_t hash = 0;
        const bool has_volume = s.ok() && HasVolume(s.sector_bytes, hash);
        if (g_stuck) return fail_stuck(error);
        if (has_volume) {
            if (readable < 0) readable = static_cast<int>(port);
            if (match && s.sector_bytes == menu_bytes && hash == menu_hash) {
                matched = static_cast<int>(port);
                logf("USB (d2x): port %u holds the drive the menu read\n", static_cast<unsigned>(port));
            }
        } else {
            logf("USB (d2x): port %u: no volume RiftWii reads on that drive\n", static_cast<unsigned>(port));
        }
    }
    if (match && matched < 0 && readable >= 0) {
        // Another drive than the menu's: reading it would give the game
        // the wrong sectors. Say so instead (as before two-drive support).
        error = "d2x cannot find the USB drive the games were listed from (it sees only the other one); plug the "
                "games' drive into the Wii's other USB port, or leave only that drive plugged in";
        IOS_Close(g_fd);
        g_fd = -1;
        g_failed = true;
        g_failure = error;
        return false;
    }
    if (match && matched < 0 && found >= 0 && any.sector_bytes != menu_bytes) {
        // The only drive that started has other sectors than the menu's:
        // not the games' drive (that one may still be starting). Asked
        // again instead of reading the other drive.
        // A size of 0 is d2x finding no drive in that port at all.
        error = any.sector_bytes == 0 ? d2x_sector_size_problem(0, nullptr)
                                      : "d2x sees only a USB drive other than the games' (" +
                                            std::to_string(any.sector_bytes) + "-byte sectors)";
        IOS_Close(g_fd);
        g_fd = -1;
        g_failed = true;
        g_failure = error;
        g_not_ready = true;
        return false;
    }
    const int chosen = matched >= 0 ? matched : readable >= 0 ? readable : found;
    if (chosen >= 0) {
        if (chosen != matched && chosen != readable) s = any;
        // d2x drives the last port it was told: the chosen one again.
        if (static_cast<std::uint32_t>(chosen) != last) {
            set_port(static_cast<std::uint32_t>(chosen));
            s = start_drive();
            if (g_stuck) return fail_stuck(error);
            logf("USB (d2x): back to port %d (init %d)\n", chosen, static_cast<int>(s.init));
        }
        g_port = static_cast<std::uint32_t>(chosen);
    }
    const s32 init = s.init;
    const s32 sectors = s.sectors;
    const std::uint32_t sector_bytes = s.sector_bytes;
    const bool counted = s.counted;
    if (!s.ok()) {
        error = init < 0 || !counted ? "the USB drive did not start through d2x (" + std::to_string(init) + ", " +
                                           std::to_string(sectors) + ")"
                                     : d2x_sector_size_problem(sector_bytes, nullptr);
        IOS_Close(g_fd);
        g_fd = -1;
        g_failed = true;
        g_failure = error;
        g_not_ready = init < 0 || !counted;
        return false;
    }
    g_sector_bytes = sector_bytes;
    if (!g_bounce) g_bounce = skin::Mem2Alloc(kBounceBytes);
    if (!g_bounce) {
        error = "no MEM2 left for the USB buffer";
        IOS_Close(g_fd);
        g_fd = -1;
        return false;
    }
    return true;
}

bool OpenWaiting(std::string& error, int seconds) {
    for (int waited = 0;; ++waited) {
        if (Open(error)) {
            if (waited > 0) logf("USB (d2x): the drive started after %d s\n", waited);
            return true;
        }
        if (!g_not_ready || waited >= seconds) return false;
        Forget();  // asked again from scratch
        usleep(1000000);
    }
}

void Forget() {
    // A stuck device's close would wait behind its unanswered request.
    if (g_fd >= 0 && !g_stuck) IOS_Close(g_fd);
    g_fd = -1;
    g_failed = false;
    g_failure.clear();
    g_volume.reset();
}

int Fd() { return g_fd; }

bool Stuck() { return g_stuck; }

std::uint32_t SectorBytes() { return g_sector_bytes; }

bool Read(std::uint64_t sector, std::uint32_t count, std::uint8_t* out) {
    if (g_fd < 0 || !g_bounce || sector > 0xFFFFFFFFull) return false;
    while (count > 0) {
        const std::uint32_t n = std::min(count, kBounceBytes / g_sector_bytes);
        g_args[0] = static_cast<std::uint32_t>(sector);
        g_args[1] = n;
        g_vec[0].data = &g_args[0];
        g_vec[0].len = 4;
        g_vec[1].data = &g_args[1];
        g_vec[1].len = 4;
        g_vec[2].data = g_bounce;
        g_vec[2].len = n * g_sector_bytes;
        DCFlushRange(g_args, sizeof(g_args));
        DCInvalidateRange(g_bounce, n * g_sector_bytes);
        if (ioctlv_limited(kReadSectors, 2, 1, g_vec, "read") < 0) return false;
        DCInvalidateRange(g_bounce, n * g_sector_bytes);
        std::memcpy(out, g_bounce, n * g_sector_bytes);
        out += n * g_sector_bytes;
        sector += n;
        count -= n;
    }
    return true;
}

bool Volume(const ImageVolume*& out, std::string& error) {
    if (!Open(error)) return false;
    if (g_sector_bytes != kFatSectorBytes) {
        error = d2x_sector_size_problem(g_sector_bytes, "packs on it");
        return false;
    }
    if (!g_volume && !mount_image_volume(&ReadBlocks, g_volume, error, usb_wanted_folders())) {
        error = "the USB drive has no FAT32 or NTFS volume: " + error;
        g_volume.reset();
        return false;
    }
    out = g_volume.get();
    return true;
}

bool ReadText(const std::string& usb_path, std::string& out, std::string& error) {
    if (usb_path.compare(0, 5, "usb:/") != 0) {
        error = "'" + usb_path + "' is not a usb:/ path";
        return false;
    }
    const ImageVolume* volume = nullptr;
    if (!Volume(volume, error)) return false;
    VolumeFile file;
    if (!volume->lookup(usb_path.substr(4), file, error)) return false;
    if (file.entry.is_directory || file.entry.size > (1u << 20)) {
        error = "'" + usb_path + "' is not an XML file of 1 MiB or less";
        return false;
    }
    out.assign(file.entry.size, '\0');
    if (!volume->read(file, 0, reinterpret_cast<std::uint8_t*>(&out[0]), out.size())) {
        error = "cannot read '" + usb_path + "' from the USB drive";
        return false;
    }
    return true;
}

}  // namespace riftwii::wii::ums
