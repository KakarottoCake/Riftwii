// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The virtual SD card blob's C code (runtime/vsd/vsd_hook.c) against a
// fake IOS: the game's calls on the card, sync and async, and the requests
// the blob sends to the device holding the image (the SD slot, d2x's SD
// and USB devices), with their retries, the CMD13 polls after writes, the
// bounce buffer and the card event. The async path is what a game with an
// async SD driver takes; Brawl (the Dolphin test) only calls sync.
//
// The blob keeps addresses as 32-bit words, and tells MEM1 from MEM2 by
// the address, so the test places everything where the Wii has it: the
// state and MEM2 buffers at 0x90000000, MEM1 buffers at 0x80100000.
extern "C" {
#include "vsd_hook.h"
}

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <vector>

#if defined(_WIN32) || defined(__CYGWIN__)
#include <windows.h>
static std::uint8_t* FixedBuffer(std::uintptr_t at, std::size_t bytes) {
    return static_cast<std::uint8_t*>(
        VirtualAlloc(reinterpret_cast<void*>(at), bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
}
#elif defined(__linux__)
#include <sys/mman.h>
static std::uint8_t* FixedBuffer(std::uintptr_t at, std::size_t bytes) {
    void* p = mmap(reinterpret_cast<void*>(at), bytes, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    return p == MAP_FAILED || p != reinterpret_cast<void*>(at) ? nullptr : static_cast<std::uint8_t*>(p);
}
#else
static std::uint8_t* FixedBuffer(std::uintptr_t, std::size_t) { return nullptr; }
#endif

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << std::hex << (a) << " != " << (b) << std::dec << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

namespace {

constexpr std::uintptr_t kMem2 = 0x90000000;
constexpr std::uintptr_t kMem1 = 0x80100000;
constexpr std::size_t kArena = 0x100000;
constexpr std::uint32_t kComplete = 0xC0DE0000;  // vsd_complete, as the blob hands it to IOS
constexpr std::uint32_t kDeviceFd = 7;
constexpr std::uint32_t kDeviceSectors = 8192;
constexpr std::uint32_t kImageSectors = 3000;   // a standard-capacity card (byte addresses)

std::uint8_t* g_mem1 = nullptr;
std::uint8_t* g_mem2 = nullptr;
std::size_t g_mem1_used = 0, g_mem2_used = 0;

std::uint32_t Alloc(bool mem2, std::size_t bytes) {
    std::size_t& used = mem2 ? g_mem2_used : g_mem1_used;
    std::uint8_t* base = mem2 ? g_mem2 : g_mem1;
    used = (used + 63) & ~std::size_t(63);
    const std::uint32_t at = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(base + used));
    std::memset(base + used, 0, bytes);
    used += bytes;
    return at;
}

template <typename T> T* At(std::uint32_t address) { return reinterpret_cast<T*>(static_cast<std::uintptr_t>(address)); }

// ---- The fake IOS ---------------------------------------------------------

struct Request {
    bool vector;
    std::int32_t fd;
    std::uint32_t ioctl, in, in_len, out, out_len;
    std::uint32_t in_count, out_count, vec;
    std::uint32_t callback, tag;
};

struct Piece {
    std::uint32_t ioctl;
    bool write;
    std::uint32_t device_sector, count, data;
};

struct GameAnswer {
    std::uint32_t callback, user_data;
    std::int32_t result;
};

struct FakeIos {
    std::uint32_t backend = VSD_BACKEND_SLOT0;
    std::uint32_t sdhc = 1;         // SLOT0: the real card takes block addresses
    std::vector<std::uint8_t> device = std::vector<std::uint8_t>(std::size_t(kDeviceSectors) * 512);
    std::deque<Request> pending;
    std::vector<Piece> pieces;
    std::vector<GameAnswer> answers;
    std::uint32_t fail_next = 0;    // data requests that fail
    std::uint32_t land_then_fail = 0;   // writes the device takes, then answers with an error
    bool scribble_on_fail = false;  // a failed read leaves garbage in its buffer (a DMA cut short)
    std::uint32_t busy_polls = 0;   // CMD13s answering "programming" after each write
    std::uint32_t busy_left = 0;
    std::uint32_t status_polls = 0;
    std::uint32_t bad_callbacks = 0;
    std::uint32_t bad_buffers = 0;  // USB data outside MEM2
    std::uint32_t sync_calls = 0;
} g_ios;

std::int32_t RunData(std::uint32_t ioctl, bool write, std::uint32_t device_sector, std::uint32_t count,
                     std::uint32_t data) {
    g_ios.pieces.push_back({ioctl, write, device_sector, count, data});
    if (g_ios.fail_next) {
        --g_ios.fail_next;
        if (g_ios.scribble_on_fail && !write) std::memset(At<std::uint8_t>(data), 0xEE, std::size_t(count) * 512);
        return -5;
    }
    if (device_sector + count > kDeviceSectors) return -4;
    std::uint8_t* dev = g_ios.device.data() + std::size_t(device_sector) * 512;
    if (write && g_ios.land_then_fail) {
        --g_ios.land_then_fail;
        std::memcpy(dev, At<std::uint8_t>(data), std::size_t(count) * 512);
        return -5;
    }
    if (write) {
        std::memcpy(dev, At<std::uint8_t>(data), std::size_t(count) * 512);
        g_ios.busy_left = g_ios.busy_polls;
    } else {
        std::memcpy(At<std::uint8_t>(data), dev, std::size_t(count) * 512);
    }
    return 0;
}

std::int32_t Execute(const Request& r) {
    if (r.fd != static_cast<std::int32_t>(kDeviceFd)) return -6;
    if (!r.vector) {
        if (r.ioctl == RTVSD_IOCTL_GETSTATUS) {
            if (r.out == 0 || r.out_len < 4) return -4;
            *At<std::uint32_t>(r.out) = RTVSD_STATUS_INSERTED | RTVSD_STATUS_INITIALIZED;
            return 0;
        }
        if (r.ioctl == RTVSD_IOCTL_SENDCMD) {
            const rtvsd_request* rq = At<rtvsd_request>(r.in);
            if (rq->cmd != 13 || r.out == 0) return -4;
            ++g_ios.status_polls;
            std::uint32_t state = RTVSD_STATE_TRAN;
            if (g_ios.busy_left) {
                --g_ios.busy_left;
                state = 7;  // programming
            }
            At<std::uint32_t>(r.out)[0] = (state << 9) | RTVSD_R1_READY_FOR_DATA;
            return 0;
        }
        return -4;
    }
    const vsd_ioctlv* v = At<vsd_ioctlv>(r.vec);
    if (g_ios.backend == VSD_BACKEND_SLOT0) {
        if (r.ioctl != RTVSD_IOCTL_SENDCMD || r.in_count != 2 || r.out_count != 1) return -4;
        const rtvsd_request* rq = At<rtvsd_request>(v[0].data);
        if ((rq->cmd != 18 && rq->cmd != 25) || rq->blk_size != 512 || rq->dma_addr != v[1].data ||
            v[1].len != rq->blk_cnt * 512)
            return -4;
        const std::uint32_t sector = g_ios.sdhc ? rq->arg : rq->arg / 512;
        return RunData(r.ioctl, rq->cmd == 25, sector, rq->blk_cnt, v[1].data);
    }
    if (g_ios.backend == VSD_BACKEND_D2X_SD) {
        if (r.ioctl == 4) return r.in_count == 0 && r.out_count == 0 ? 1 : -4;  // ISINSERTED
        const bool write = r.ioctl == 3;
        if ((r.ioctl != 2 && r.ioctl != 3) || r.in_count != (write ? 3u : 2u) || r.out_count != (write ? 0u : 1u))
            return -4;
        const std::uint32_t count = *At<std::uint32_t>(v[1].data);
        if (v[2].len != count * 512) return -4;
        return RunData(r.ioctl, write, *At<std::uint32_t>(v[0].data), count, v[2].data);
    }
    if (r.ioctl == 0x554D5302) {  // GET_CAPACITY
        if (r.in_count != 0 || r.out_count != 1) return -4;
        *At<std::uint32_t>(v[0].data) = 512;
        return kDeviceSectors;
    }
    const bool write = r.ioctl == 0x554D5304;
    if ((r.ioctl != 0x554D5303 && !write) || r.in_count != 2 || r.out_count != 1) return -4;
    const std::uint32_t count = *At<std::uint32_t>(v[1].data);
    const std::uint32_t physical = v[2].data & 0x3FFFFFFF;
    if (physical < 0x10000000 || physical >= 0x14000000) ++g_ios.bad_buffers;
    if (v[2].len != count * 512) return -4;
    return RunData(r.ioctl, write, *At<std::uint32_t>(v[0].data), count, v[2].data);
}

std::int32_t HostIoctl(std::int32_t fd, std::uint32_t ioctl, std::uint32_t in, std::uint32_t in_len, std::uint32_t out,
                       std::uint32_t out_len) {
    ++g_ios.sync_calls;
    return Execute({false, fd, ioctl, in, in_len, out, out_len, 0, 0, 0, 0, 0});
}
std::int32_t HostIoctlv(std::int32_t fd, std::uint32_t ioctl, std::uint32_t in_count, std::uint32_t out_count,
                        std::uint32_t vec) {
    ++g_ios.sync_calls;
    return Execute({true, fd, ioctl, 0, 0, 0, 0, in_count, out_count, vec, 0, 0});
}
std::int32_t HostIoctlAsync(std::int32_t fd, std::uint32_t ioctl, std::uint32_t in, std::uint32_t in_len,
                            std::uint32_t out, std::uint32_t out_len, std::uint32_t callback, std::uint32_t user_data) {
    if (callback != kComplete) ++g_ios.bad_callbacks;
    g_ios.pending.push_back({false, fd, ioctl, in, in_len, out, out_len, 0, 0, 0, callback, user_data});
    return 0;
}
std::int32_t HostIoctlvAsync(std::int32_t fd, std::uint32_t ioctl, std::uint32_t in_count, std::uint32_t out_count,
                             std::uint32_t vec, std::uint32_t callback, std::uint32_t user_data) {
    if (callback != kComplete) ++g_ios.bad_callbacks;
    g_ios.pending.push_back({true, fd, ioctl, 0, 0, 0, 0, in_count, out_count, vec, callback, user_data});
    return 0;
}

// ---- The blob ---------------------------------------------------------------

struct Blob {
    vsd_context* ctx;
    vsd_state* st;
};

// `bounce_bytes` 0: the size the loader gives the device (vsd_bounce_bytes_for).
Blob MakeBlobWith(std::uint32_t backend, const std::vector<rtvsd_extent>& extents, std::uint32_t image_sectors,
                  bool read_only = false, std::uint32_t bounce_bytes = 0);

// Image sectors 0-999 at device sector 100, 1000-2999 at 3000: a read
// across 1000 takes two extents.
Blob MakeBlob(std::uint32_t backend, bool read_only = false) {
    return MakeBlobWith(backend, {{0, 100, 1000}, {1000, 3000, 2000}}, kImageSectors, read_only);
}

Blob MakeBlobWith(std::uint32_t backend, const std::vector<rtvsd_extent>& extents, std::uint32_t image_sectors,
                  bool read_only, std::uint32_t bounce_bytes) {
    if (bounce_bytes == 0) bounce_bytes = vsd_bounce_bytes_for(backend);
    g_mem1_used = g_mem2_used = 0;
    g_ios.pending.clear();
    g_ios.pieces.clear();
    g_ios.answers.clear();
    g_ios.backend = backend;
    g_ios.fail_next = g_ios.busy_polls = g_ios.busy_left = g_ios.status_polls = 0;
    g_ios.land_then_fail = 0;
    g_ios.scribble_on_fail = false;
    g_ios.bad_callbacks = g_ios.bad_buffers = g_ios.sync_calls = 0;
    for (std::size_t i = 0; i < g_ios.device.size(); ++i)
        g_ios.device[i] = static_cast<std::uint8_t>((i * 7) ^ (i >> 9));
    Blob b;
    b.ctx = At<vsd_context>(Alloc(true, sizeof(vsd_context)));
    b.st = At<vsd_state>(Alloc(true, vsd_state_bytes(extents.size(), bounce_bytes)));
    b.ctx->magic = VSD_CONTEXT_MAGIC;
    b.ctx->state = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(b.st));
    vsd_state* st = b.st;
    st->magic = VSD_STATE_MAGIC;
    st->complete = kComplete;
    for (std::uint32_t i = 0; i < VSD_ENTRIES; ++i) st->original[i] = 0x81000000 + i * 0x100;
    st->backend = backend;
    st->fd = kDeviceFd;
    st->sdhc = g_ios.sdhc;
    st->rca = backend == VSD_BACKEND_SLOT0 ? 0x1234 : 0;
    // The bounce buffer after the extent table, as the loader places it.
    st->bounce = b.ctx->state + vsd_bounce_offset(extents.size());
    st->bounce_sectors = bounce_bytes / 512;
    st->card.sectors = image_sectors;
    for (std::size_t i = 0; i < extents.size(); ++i) rtvsd_extents(&st->card)[i] = extents[i];
    st->card.extent_count = static_cast<std::uint32_t>(extents.size());
    st->card.read_only = read_only ? 1 : 0;
    EXPECT_EQ(rtvsd_reset(&st->card), 0);
    st->card.state = RTVSD_STATE_TRAN;
    return b;
}

std::uint32_t DeviceSector(std::uint32_t image_sector) {
    return image_sector < 1000 ? 100 + image_sector : 3000 + (image_sector - 1000);
}

// Answers the oldest request the blob sent, as IOS's interrupt would.
bool Pump(const Blob& b) {
    if (g_ios.pending.empty()) return false;
    const Request r = g_ios.pending.front();
    g_ios.pending.pop_front();
    std::int32_t result = Execute(r);
    std::uint32_t callback = 0xFFFFFFFF, user_data = 0xFFFFFFFF;
    vsd_on_complete(b.ctx, &result, reinterpret_cast<void*>(static_cast<std::uintptr_t>(r.tag)), &callback,
                    &user_data);
    if (callback != 0) g_ios.answers.push_back({callback, user_data, result});
    return true;
}

void PumpAll(const Blob& b) {
    for (int i = 0; i < 100000 && Pump(b); ++i) {
    }
}

std::int32_t Call(const Blob& b, std::uint32_t entry, std::initializer_list<std::uint32_t> args, int* hooked = nullptr) {
    std::uint32_t a[8] = {};
    std::size_t i = 0;
    for (std::uint32_t x : args) a[i++] = x;
    std::int32_t result = 0x7777;
    const int h = vsd_on_ipc(b.ctx, entry, a, &result);
    if (hooked) *hooked = h;
    return result;
}

std::uint32_t Path(const char* s) {
    const std::uint32_t at = Alloc(false, 64);
    std::strcpy(At<char>(at), s);
    return at;
}

// A SENDCMD by ioctlv, as libogc's and the SDK's drivers send reads and
// writes: the block, the data, the reply.
struct Command {
    std::uint32_t request, vec, reply;
};

Command MakeCommand(std::uint32_t cmd, std::uint32_t arg, std::uint32_t blocks, std::uint32_t data) {
    Command c;
    c.request = Alloc(true, sizeof(rtvsd_request));
    c.vec = Alloc(true, 3 * sizeof(vsd_ioctlv));
    c.reply = Alloc(true, 32);
    rtvsd_request* rq = At<rtvsd_request>(c.request);
    rq->cmd = cmd;
    rq->cmd_type = 3;
    rq->rsp_type = 1;
    rq->arg = arg;
    rq->blk_cnt = blocks;
    rq->blk_size = blocks ? 512 : 0;
    rq->dma_addr = data & 0x3FFFFFFF;
    rq->isdma = data != 0;
    vsd_ioctlv* v = At<vsd_ioctlv>(c.vec);
    v[0] = {c.request, sizeof(rtvsd_request)};
    v[1] = {data, blocks * 512};
    v[2] = {c.reply, 16};
    std::memset(At<std::uint8_t>(c.reply), 0xEE, 16);
    return c;
}

std::int32_t SendAsync(const Blob& b, const Command& c, std::uint32_t callback, std::uint32_t user_data) {
    return Call(b, VSD_IOCTLV_ASYNC, {VSD_FAKE_FD, RTVSD_IOCTL_SENDCMD, 2, 1, c.vec, callback, user_data});
}

std::int32_t SendSync(const Blob& b, const Command& c) {
    return Call(b, VSD_IOCTLV, {VSD_FAKE_FD, RTVSD_IOCTL_SENDCMD, 2, 1, c.vec});
}

bool ImageMatches(std::uint32_t buffer, std::uint32_t image_sector, std::uint32_t count) {
    for (std::uint32_t s = 0; s < count; ++s) {
        const std::uint8_t* dev = g_ios.device.data() + std::size_t(DeviceSector(image_sector + s)) * 512;
        if (std::memcmp(At<std::uint8_t>(buffer + s * 512), dev, 512) != 0) return false;
    }
    return true;
}

void Fill(std::uint32_t buffer, std::uint32_t bytes, std::uint8_t seed) {
    for (std::uint32_t i = 0; i < bytes; ++i) At<std::uint8_t>(buffer)[i] = static_cast<std::uint8_t>(seed + i * 13);
}

// ---- Tests ------------------------------------------------------------------

void TestOpenAndStatus() {
    Blob b = MakeBlob(VSD_BACKEND_SLOT0);
    int hooked = 0;
    // Another device passes through.
    Call(b, VSD_OPEN_ASYNC, {Path("/dev/sdio/slot1"), 0, 0xA1, 0xB1}, &hooked);
    EXPECT_EQ(hooked, 0);
    // The slot opens async: 0 now, the handle in the callback, after the null trip.
    EXPECT_EQ(Call(b, VSD_OPEN_ASYNC, {Path("/dev/sdio/slot0"), 0, 0xA1, 0xB1}, &hooked), 0);
    EXPECT_EQ(hooked, 1);
    EXPECT_TRUE(g_ios.answers.empty());
    EXPECT_EQ(g_ios.pending.size(), 1u);
    PumpAll(b);
    EXPECT_EQ(g_ios.answers.size(), 1u);
    if (!g_ios.answers.empty()) {
        EXPECT_EQ(g_ios.answers[0].callback, 0xA1u);
        EXPECT_EQ(g_ios.answers[0].user_data, 0xB1u);
        EXPECT_EQ(g_ios.answers[0].result, VSD_FAKE_FD);
    }
    // The sync open.
    EXPECT_EQ(Call(b, VSD_OPEN, {Path("/dev/sdio/slot0"), 0}), VSD_FAKE_FD);
    // Calls on other handles pass through.
    Call(b, VSD_IOCTL, {5, RTVSD_IOCTL_GETSTATUS, 0, 0, 0, 0}, &hooked);
    EXPECT_EQ(hooked, 0);
    // GETSTATUS async: the answer written, the callback after the null trip.
    const std::uint32_t out = Alloc(false, 32);
    g_ios.answers.clear();
    EXPECT_EQ(Call(b, VSD_IOCTL_ASYNC, {VSD_FAKE_FD, RTVSD_IOCTL_GETSTATUS, 0, 0, out, 4, 0xA2, 0xB2}), 0);
    PumpAll(b);
    EXPECT_EQ(g_ios.answers.size(), 1u);
    if (!g_ios.answers.empty()) EXPECT_EQ(g_ios.answers[0].result, 0);
    EXPECT_TRUE((*At<std::uint32_t>(out) & RTVSD_STATUS_INSERTED) != 0);
    // Close async.
    g_ios.answers.clear();
    EXPECT_EQ(Call(b, VSD_CLOSE_ASYNC, {VSD_FAKE_FD, 0xA3, 0xB3}), 0);
    PumpAll(b);
    EXPECT_EQ(g_ios.answers.size(), 1u);
    EXPECT_EQ(b.st->opens, 2u);
    EXPECT_EQ(g_ios.bad_callbacks, 0u);
    for (std::uint32_t i = 0; i < VSD_DELIVERS; ++i) EXPECT_EQ(b.st->deliver[i].in_use, 0u);
    // More answers at once than slots: the extra one is refused, the others arrive.
    g_ios.answers.clear();
    int refused = 0;
    for (std::uint32_t i = 0; i < VSD_DELIVERS + 1; ++i)
        if (Call(b, VSD_IOCTL_ASYNC, {VSD_FAKE_FD, RTVSD_IOCTL_GETSTATUS, 0, 0, out, 4, 0xA4, i}) < 0) ++refused;
    EXPECT_EQ(refused, 1);
    PumpAll(b);
    EXPECT_EQ(g_ios.answers.size(), std::size_t(VSD_DELIVERS));
}

void TestAsyncRead(bool sdhc_card) {
    g_ios.sdhc = sdhc_card ? 1 : 0;
    Blob b = MakeBlob(VSD_BACKEND_SLOT0);
    // 310 sectors from 990: 10 in the first extent, then 128, 128, 44.
    const std::uint32_t buffer = Alloc(false, 310 * 512);
    const Command c = MakeCommand(18, 990 * 512, 310, buffer);
    EXPECT_EQ(SendAsync(b, c, 0xC1, 0xD1), 0);
    std::size_t most_pending = 0;
    while (!g_ios.pending.empty()) {
        most_pending = std::max(most_pending, g_ios.pending.size());
        Pump(b);
    }
    EXPECT_EQ(most_pending, 1u);
    EXPECT_EQ(g_ios.pieces.size(), 4u);
    if (g_ios.pieces.size() == 4) {
        EXPECT_EQ(g_ios.pieces[0].device_sector, 1090u);
        EXPECT_EQ(g_ios.pieces[0].count, 10u);
        EXPECT_EQ(g_ios.pieces[1].device_sector, 3000u);
        EXPECT_EQ(g_ios.pieces[1].count, 128u);
        EXPECT_EQ(g_ios.pieces[3].count, 44u);
    }
    EXPECT_TRUE(ImageMatches(buffer, 990, 310));
    EXPECT_EQ(g_ios.answers.size(), 1u);
    if (!g_ios.answers.empty()) {
        EXPECT_EQ(g_ios.answers[0].callback, 0xC1u);
        EXPECT_EQ(g_ios.answers[0].user_data, 0xD1u);
        EXPECT_EQ(g_ios.answers[0].result, 0);
    }
    EXPECT_EQ(At<std::uint32_t>(c.reply)[0] & RTVSD_R1_OUT_OF_RANGE, 0u);
    EXPECT_EQ((At<std::uint32_t>(c.reply)[0] >> 9) & 15u, RTVSD_STATE_TRAN);
    EXPECT_EQ(b.st->sectors_read, 310u);
    EXPECT_EQ(b.st->reads, 1u);
    EXPECT_EQ(b.st->transfer.in_use, 0u);
    EXPECT_EQ(g_ios.sync_calls, 0u);
    g_ios.sdhc = 1;
}

void TestAsyncWriteSettles() {
    Blob b = MakeBlob(VSD_BACKEND_SLOT0);
    g_ios.busy_polls = 3;
    // 20 sectors at 995: 5 in the first extent, 15 in the second.
    const std::uint32_t buffer = Alloc(false, 20 * 512);
    Fill(buffer, 20 * 512, 0x5A);
    const Command c = MakeCommand(25, 995 * 512, 20, buffer);
    EXPECT_EQ(SendAsync(b, c, 0xC2, 0xD2), 0);
    PumpAll(b);
    EXPECT_EQ(g_ios.pieces.size(), 2u);
    EXPECT_TRUE(ImageMatches(buffer, 995, 20));
    // Each piece: three polls while the card programs, then the one that finds it ready.
    EXPECT_EQ(g_ios.status_polls, 8u);
    EXPECT_EQ(g_ios.answers.size(), 1u);
    if (!g_ios.answers.empty()) EXPECT_EQ(g_ios.answers[0].result, 0);
    EXPECT_EQ(b.st->sectors_written, 20u);
    EXPECT_EQ(b.st->transfer.in_use, 0u);
    EXPECT_EQ(b.st->transfer.phase, VSD_PHASE_DATA);
    // The same without a card address: no polls.
    Blob c2 = MakeBlob(VSD_BACKEND_SLOT0);
    c2.st->rca = 0;
    g_ios.busy_polls = 3;
    const std::uint32_t buffer2 = Alloc(false, 4 * 512);
    Fill(buffer2, 4 * 512, 0x11);
    EXPECT_EQ(SendAsync(c2, MakeCommand(25, 10 * 512, 4, buffer2), 0xC3, 0xD3), 0);
    PumpAll(c2);
    EXPECT_EQ(g_ios.status_polls, 0u);
    EXPECT_TRUE(ImageMatches(buffer2, 10, 4));
}

// The resident runtime beside the blob on the same card (BUG-05): while
// the runtime's write is programmed the blob asks the card's status before
// a piece, and its own writes are marked for the runtime until the card
// has programmed them.
void TestRuntimeBeside() {
    Blob b = MakeBlob(VSD_BACKEND_SLOT0);
    const std::uint32_t runtime_busy = Alloc(false, 32);
    *At<std::uint32_t>(runtime_busy) = 1;
    b.st->peer_busy = runtime_busy;
    const std::uint32_t buffer = Alloc(false, 4 * 512);
    // Async: two CMD13s find the card programming, the third ready, then the read.
    g_ios.busy_left = 2;
    EXPECT_EQ(SendAsync(b, MakeCommand(18, 0, 4, buffer), 0xC4, 0xD4), 0);
    EXPECT_TRUE(g_ios.pieces.empty());
    PumpAll(b);
    EXPECT_EQ(g_ios.status_polls, 3u);
    EXPECT_EQ(g_ios.pieces.size(), 1u);
    EXPECT_TRUE(ImageMatches(buffer, 0, 4));
    EXPECT_EQ(g_ios.answers.size(), 1u);
    if (!g_ios.answers.empty()) EXPECT_EQ(g_ios.answers[0].result, 0);
    // Sync: the same.
    g_ios.status_polls = 0;
    g_ios.busy_left = 1;
    EXPECT_EQ(SendSync(b, MakeCommand(18, 8 * 512, 4, buffer)), 0);
    EXPECT_EQ(g_ios.status_polls, 2u);
    EXPECT_TRUE(ImageMatches(buffer, 8, 4));
    // The runtime idle: nothing asked before a read.
    *At<std::uint32_t>(runtime_busy) = 0;
    g_ios.status_polls = 0;
    EXPECT_EQ(SendSync(b, MakeCommand(18, 0, 4, buffer)), 0);
    EXPECT_EQ(g_ios.status_polls, 0u);
    // A write: "writing" set from its send until the card is seen ready.
    g_ios.busy_polls = 2;
    Fill(buffer, 4 * 512, 0x33);
    EXPECT_EQ(SendAsync(b, MakeCommand(25, 40 * 512, 4, buffer), 0xC5, 0xD5), 0);
    EXPECT_EQ(b.st->writing, 1u);
    PumpAll(b);
    EXPECT_EQ(b.st->writing, 0u);
    EXPECT_TRUE(ImageMatches(buffer, 40, 4));
}

void TestSyncWriteSettles() {
    Blob b = MakeBlob(VSD_BACKEND_SLOT0);
    g_ios.busy_polls = 2;
    const std::uint32_t buffer = Alloc(false, 140 * 512);
    Fill(buffer, 140 * 512, 0x33);
    EXPECT_EQ(SendSync(b, MakeCommand(25, 1500 * 512, 140, buffer)), 0);
    EXPECT_TRUE(ImageMatches(buffer, 1500, 140));
    EXPECT_EQ(g_ios.pieces.size(), 2u);  // 128 + 12
    EXPECT_EQ(g_ios.status_polls, 6u);
    EXPECT_TRUE(g_ios.pending.empty());
}

void TestRetries() {
    // Two failures: sent a third time, the same piece.
    Blob b = MakeBlob(VSD_BACKEND_SLOT0);
    const std::uint32_t buffer = Alloc(false, 8 * 512);
    g_ios.fail_next = 2;
    EXPECT_EQ(SendAsync(b, MakeCommand(18, 996 * 512, 8, buffer), 0xC4, 0xD4), 0);
    PumpAll(b);
    EXPECT_EQ(b.st->failures, 2u);
    EXPECT_EQ(b.st->gave_up, 0u);
    EXPECT_EQ(g_ios.pieces.size(), 4u);  // 4 sectors three times, then the other 4
    if (g_ios.pieces.size() == 4) {
        EXPECT_EQ(g_ios.pieces[0].device_sector, g_ios.pieces[2].device_sector);
        EXPECT_EQ(g_ios.pieces[0].data, g_ios.pieces[2].data);
    }
    EXPECT_TRUE(ImageMatches(buffer, 996, 8));
    EXPECT_EQ(g_ios.answers.size(), 1u);
    if (!g_ios.answers.empty()) EXPECT_EQ(g_ios.answers[0].result, 0);

    // Three: the game gets the error, the transfer is free again.
    g_ios.answers.clear();
    g_ios.fail_next = 3;
    const Command c = MakeCommand(18, 0, 2, buffer);
    EXPECT_EQ(SendAsync(b, c, 0xC5, 0xD5), 0);
    PumpAll(b);
    EXPECT_EQ(b.st->gave_up, 1u);
    EXPECT_EQ(g_ios.answers.size(), 1u);
    if (!g_ios.answers.empty()) {
        EXPECT_EQ(g_ios.answers[0].callback, 0xC5u);
        EXPECT_TRUE(g_ios.answers[0].result < 0);
    }
    EXPECT_EQ(b.st->transfer.in_use, 0u);
    // And the next one works.
    g_ios.answers.clear();
    EXPECT_EQ(SendAsync(b, MakeCommand(18, 0, 2, buffer), 0xC6, 0xD6), 0);
    PumpAll(b);
    EXPECT_TRUE(ImageMatches(buffer, 0, 2));
    EXPECT_EQ(g_ios.answers.size(), 1u);

    // Sync: the same.
    g_ios.fail_next = 3;
    EXPECT_TRUE(SendSync(b, MakeCommand(18, 0, 2, buffer)) < 0);
    EXPECT_EQ(b.st->transfer.in_use, 0u);
    g_ios.fail_next = 1;
    EXPECT_EQ(SendSync(b, MakeCommand(18, 20 * 512, 2, buffer)), 0);
    EXPECT_TRUE(ImageMatches(buffer, 20, 2));
}

void TestBusy() {
    Blob b = MakeBlob(VSD_BACKEND_SLOT0);
    const std::uint32_t one = Alloc(false, 4 * 512), two = Alloc(false, 4 * 512);
    EXPECT_EQ(SendAsync(b, MakeCommand(18, 0, 4, one), 0xC7, 0xD7), 0);
    EXPECT_EQ(SendAsync(b, MakeCommand(18, 8 * 512, 4, two), 0xC8, 0xD8), -8);
    EXPECT_EQ(SendSync(b, MakeCommand(18, 8 * 512, 4, two)), -8);
    EXPECT_EQ(b.st->busy, 2u);
    PumpAll(b);
    EXPECT_EQ(g_ios.answers.size(), 1u);
    EXPECT_TRUE(ImageMatches(one, 0, 4));
    // A command without data runs while a transfer is in flight.
    EXPECT_EQ(SendAsync(b, MakeCommand(18, 0, 4, one), 0xC7, 0xD7), 0);
    const Command status = MakeCommand(13, RTVSD_RCA << 16, 0, 0);
    EXPECT_EQ(Call(b, VSD_IOCTLV, {VSD_FAKE_FD, RTVSD_IOCTL_SENDCMD, 1, 1, status.vec}), 0);
    PumpAll(b);
}

void TestBounce() {
    // An unaligned buffer: through the bounce buffer, read and write.
    Blob b = MakeBlob(VSD_BACKEND_SLOT0);
    const std::uint32_t buffer = Alloc(false, 100 * 512 + 64) + 4;
    EXPECT_EQ(SendAsync(b, MakeCommand(18, 950 * 512, 100, buffer), 0xC9, 0xD9), 0);
    PumpAll(b);
    EXPECT_TRUE(ImageMatches(buffer, 950, 100));
    for (const Piece& p : g_ios.pieces) EXPECT_EQ(p.data, b.st->bounce);
    // Counted: two pieces (the read crosses an extent at 1000), both for
    // an unaligned buffer, 50 sectors the most one wanted.
    EXPECT_EQ(b.st->bounced, 2u);
    EXPECT_EQ(b.st->bounced_unaligned, 2u);
    EXPECT_EQ(b.st->bounced_sectors, 100u);
    EXPECT_EQ(b.st->bounced_most, 50u);
    EXPECT_EQ(b.st->requests, 2u);
    Fill(buffer, 100 * 512, 0x77);
    EXPECT_EQ(SendAsync(b, MakeCommand(25, 1200 * 512, 100, buffer), 0xCA, 0xDA), 0);
    PumpAll(b);
    EXPECT_TRUE(ImageMatches(buffer, 1200, 100));
    EXPECT_EQ(b.st->bounced, 4u);  // 64 + 36: the bounce buffer's 64 sectors cut the first
    EXPECT_EQ(b.st->bounced_most, 100u);
    EXPECT_EQ(b.st->bounced_sectors, 200u);
    // An aligned buffer: straight in, not counted.
    const std::uint32_t aligned = Alloc(false, 100 * 512);
    EXPECT_EQ(SendSync(b, MakeCommand(18, 950 * 512, 100, aligned)), 0);
    EXPECT_TRUE(ImageMatches(aligned, 950, 100));
    EXPECT_EQ(b.st->bounced, 4u);
    EXPECT_EQ(b.st->requests, 6u);
}

void TestEvent() {
    Blob b = MakeBlob(VSD_BACKEND_SLOT0);
    const Command reg = MakeCommand(RTVSD_CMD_EVENT_REGISTER, 0, 0, 0);
    // The event registered async stays unanswered.
    EXPECT_EQ(Call(b, VSD_IOCTL_ASYNC, {VSD_FAKE_FD, RTVSD_IOCTL_SENDCMD, reg.request, sizeof(rtvsd_request), reg.reply,
                                        16, 0xE1, 0xF1}),
              0);
    EXPECT_TRUE(g_ios.pending.empty());
    EXPECT_EQ(b.st->event_held, 1u);
    // Reads go on meanwhile.
    const std::uint32_t buffer = Alloc(false, 2 * 512);
    EXPECT_EQ(SendAsync(b, MakeCommand(18, 0, 2, buffer), 0xC1, 0xD1), 0);
    PumpAll(b);
    EXPECT_EQ(g_ios.answers.size(), 1u);
    // Taken back: the event answered with 0x0C210000, the unregister with 0.
    g_ios.answers.clear();
    const Command unreg = MakeCommand(RTVSD_CMD_EVENT_UNREGISTER, 0, 0, 0);
    EXPECT_EQ(Call(b, VSD_IOCTL_ASYNC, {VSD_FAKE_FD, RTVSD_IOCTL_SENDCMD, unreg.request, sizeof(rtvsd_request),
                                        unreg.reply, 16, 0xE2, 0xF2}),
              0);
    PumpAll(b);
    EXPECT_EQ(g_ios.answers.size(), 2u);
    if (g_ios.answers.size() == 2) {
        EXPECT_EQ(g_ios.answers[0].callback, 0xE1u);
        EXPECT_EQ(static_cast<std::uint32_t>(g_ios.answers[0].result), RTVSD_EVENT_INVALID);
        EXPECT_EQ(g_ios.answers[1].callback, 0xE2u);
        EXPECT_EQ(g_ios.answers[1].result, 0);
    }
    EXPECT_EQ(b.st->event_held, 0u);
}

void TestUsb() {
    Blob b = MakeBlob(VSD_BACKEND_USB);
    // MEM1 buffer: bounced, in pieces of 64 sectors at most.
    const std::uint32_t mem1 = Alloc(false, 200 * 512);
    EXPECT_EQ(SendAsync(b, MakeCommand(18, 900 * 512, 200, mem1), 0xC1, 0xD1), 0);
    PumpAll(b);
    EXPECT_TRUE(ImageMatches(mem1, 900, 200));
    for (const Piece& p : g_ios.pieces) EXPECT_TRUE(p.count <= 64u);
    EXPECT_EQ(g_ios.bad_buffers, 0u);
    // Counted as bounced, but not for alignment: the buffer is in MEM1.
    EXPECT_TRUE(b.st->bounced >= 4u);
    EXPECT_EQ(b.st->bounced_unaligned, 0u);
    EXPECT_EQ(b.st->bounced_sectors, 200u);
    // MEM2 buffer: straight in.
    g_ios.pieces.clear();
    const std::uint32_t mem2 = Alloc(true, 100 * 512);
    EXPECT_EQ(SendAsync(b, MakeCommand(18, 1100 * 512, 100, mem2), 0xC2, 0xD2), 0);
    PumpAll(b);
    EXPECT_TRUE(ImageMatches(mem2, 1100, 100));
    if (!g_ios.pieces.empty()) EXPECT_EQ(g_ios.pieces[0].data, mem2);
    // Writes from MEM1, sync and async.
    Fill(mem1, 80 * 512, 0x42);
    EXPECT_EQ(SendAsync(b, MakeCommand(25, 960 * 512, 80, mem1), 0xC3, 0xD3), 0);
    PumpAll(b);
    EXPECT_TRUE(ImageMatches(mem1, 960, 80));
    Fill(mem1, 70 * 512, 0x43);
    EXPECT_EQ(SendSync(b, MakeCommand(25, 20 * 512, 70, mem1)), 0);
    EXPECT_TRUE(ImageMatches(mem1, 20, 70));
    EXPECT_EQ(g_ios.bad_buffers, 0u);
    EXPECT_EQ(g_ios.status_polls, 0u);
    // The null trip on USB: a capacity request.
    g_ios.answers.clear();
    const std::uint32_t out = Alloc(false, 32);
    EXPECT_EQ(Call(b, VSD_IOCTL_ASYNC, {VSD_FAKE_FD, RTVSD_IOCTL_GETSTATUS, 0, 0, out, 4, 0xA2, 0xB2}), 0);
    PumpAll(b);
    EXPECT_EQ(g_ios.answers.size(), 1u);
    if (!g_ios.answers.empty()) EXPECT_EQ(g_ios.answers[0].result, 0);
}

void TestD2xSd() {
    Blob b = MakeBlob(VSD_BACKEND_D2X_SD);
    const std::uint32_t buffer = Alloc(false, 150 * 512);
    EXPECT_EQ(SendAsync(b, MakeCommand(18, 980 * 512, 150, buffer), 0xC1, 0xD1), 0);
    PumpAll(b);
    EXPECT_TRUE(ImageMatches(buffer, 980, 150));
    Fill(buffer, 150 * 512, 0x21);
    EXPECT_EQ(SendAsync(b, MakeCommand(25, 980 * 512, 150, buffer), 0xC2, 0xD2), 0);
    PumpAll(b);
    EXPECT_TRUE(ImageMatches(buffer, 980, 150));
    EXPECT_EQ(g_ios.answers.size(), 2u);
    for (const GameAnswer& a : g_ios.answers) EXPECT_EQ(a.result, 0);
    // The null trip: ISINSERTED, its positive answer not handed to the game.
    g_ios.answers.clear();
    const std::uint32_t out = Alloc(false, 32);
    EXPECT_EQ(Call(b, VSD_IOCTL_ASYNC, {VSD_FAKE_FD, RTVSD_IOCTL_GETSTATUS, 0, 0, out, 4, 0xA2, 0xB2}), 0);
    PumpAll(b);
    EXPECT_EQ(g_ios.answers.size(), 1u);
    if (!g_ios.answers.empty()) EXPECT_EQ(g_ios.answers[0].result, 0);
}

void TestErrors() {
    // Past the image: answered at once (after the null trip) with the card's error, nothing sent.
    Blob b = MakeBlob(VSD_BACKEND_SLOT0);
    const std::uint32_t buffer = Alloc(false, 8 * 512);
    const Command c = MakeCommand(18, 2998 * 512, 4, buffer);
    EXPECT_EQ(SendAsync(b, c, 0xC1, 0xD1), 0);
    PumpAll(b);
    EXPECT_TRUE(g_ios.pieces.empty());
    EXPECT_EQ(g_ios.answers.size(), 1u);
    if (!g_ios.answers.empty()) EXPECT_EQ(g_ios.answers[0].result, RTVSD_EINVAL);
    EXPECT_TRUE((At<std::uint32_t>(c.reply)[0] & RTVSD_R1_OUT_OF_RANGE) != 0);
    // A read-only image refuses writes.
    Blob ro = MakeBlob(VSD_BACKEND_SLOT0, true);
    EXPECT_EQ(SendSync(ro, MakeCommand(25, 0, 1, buffer)), RTVSD_EINVAL);
    EXPECT_TRUE(g_ios.pieces.empty());
    // Vector counts past what an ioctlv carries, and a command block with no
    // address, are refused before any vector is read (MIN-11).
    const Command wild = MakeCommand(18, 0, 1, buffer);
    EXPECT_EQ(Call(b, VSD_IOCTLV, {VSD_FAKE_FD, RTVSD_IOCTL_SENDCMD, 0x40000000u, 1, wild.vec}), -4);
    EXPECT_EQ(Call(b, VSD_IOCTLV, {VSD_FAKE_FD, RTVSD_IOCTL_SENDCMD, 2, 0xFFFFFFFFu, wild.vec}), -4);
    At<vsd_ioctlv>(wild.vec)[0].data = 0;
    EXPECT_EQ(Call(b, VSD_IOCTLV, {VSD_FAKE_FD, RTVSD_IOCTL_SENDCMD, 2, 1, wild.vec}), -4);
    EXPECT_TRUE(g_ios.pieces.empty());
    // No state: nothing hooked.
    vsd_context bare{};
    std::uint32_t a[8] = {Path("/dev/sdio/slot0"), 0};
    std::int32_t result = 0;
    EXPECT_EQ(vsd_on_ipc(&bare, VSD_OPEN, a, &result), 0);
}


// ---- Read-ahead (runtime/rtvsdra.h) -------------------------------------------

// In the bounce buffer, as many windows as fit, as the loader places it.
void EnableReadAhead(const Blob& b, std::uint32_t window, std::uint32_t anchor) {
    b.st->ra.buffer = b.st->bounce;
    b.st->ra.window = window;
    b.st->ra.slots = std::min<std::uint32_t>(b.st->bounce_sectors / window, RTVSD_RA_MAX_SLOTS);
    b.st->ra.anchor = anchor;
    rtvsd_ra_drop(&b.st->ra);
}

std::uint32_t RaBuffer(const Blob& b) { return b.st->ra.buffer; }

// The game's buffer against the device, through the blob's own map of the
// image (the device is the truth: every write lands there).
bool CardMatches(const Blob& b, std::uint32_t buffer, std::uint32_t image_sector, std::uint32_t count) {
    for (std::uint32_t s = 0; s < count; ++s) {
        std::uint32_t device = 0;
        if (rtvsd_map(&b.st->card, image_sector + s, 1, &device) != 1) return false;
        if (std::memcmp(At<std::uint8_t>(buffer + s * 512), g_ios.device.data() + std::size_t(device) * 512, 512) != 0)
            return false;
    }
    return true;
}

// Every request on the device stays inside one piece of the image.
bool PiecesInsideExtents(const Blob& b) {
    for (const Piece& p : g_ios.pieces) {
        bool inside = false;
        for (std::uint32_t i = 0; i < b.st->card.extent_count && !inside; ++i) {
            const rtvsd_extent& e = rtvsd_extents(&b.st->card)[i];
            inside = p.device_sector >= e.device_sector && p.device_sector + p.count <= e.device_sector + e.count;
        }
        if (!inside) return false;
    }
    return true;
}

// A read (cmd 17/18) or write (24/25) of the image, sync or async; the
// game's answer.
std::int32_t Transfer(const Blob& b, std::uint32_t cmd, std::uint32_t sector, std::uint32_t count, std::uint32_t buffer,
                      bool async) {
    const std::uint32_t arg = b.st->card.sdhc ? sector : sector * 512;
    const Command c = MakeCommand(cmd, arg, count, buffer);
    if (!async) return SendSync(b, c);
    g_ios.answers.clear();
    const std::int32_t sent = SendAsync(b, c, 0xCC, 0xDD);
    if (sent != 0) return sent;
    PumpAll(b);
    if (g_ios.answers.size() != 1 || g_ios.answers[0].callback != 0xCC || g_ios.answers[0].user_data != 0xDD)
        return -999;
    return g_ios.answers[0].result;
}

std::int32_t Read(const Blob& b, std::uint32_t sector, std::uint32_t count, std::uint32_t buffer, bool async) {
    return Transfer(b, count == 1 ? 17 : 18, sector, count, buffer, async);
}

std::int32_t Write(const Blob& b, std::uint32_t sector, std::uint32_t count, std::uint32_t buffer, bool async) {
    return Transfer(b, count == 1 ? 24 : 25, sector, count, buffer, async);
}

bool PieceIs(std::size_t i, std::uint32_t device_sector, std::uint32_t count, std::uint32_t data) {
    if (i >= g_ios.pieces.size()) return false;
    const Piece& p = g_ios.pieces[i];
    return !p.write && p.device_sector == device_sector && p.count == count && p.data == data;
}

// A smaller bounce buffer (the SD card's size is one constant, and may come
// down once Dolphin's counts say so): unaligned pieces are cut to it,
// aligned ones still go straight in at full size, and the read-ahead holds
// as many windows as fit.
void TestSmallBounce(bool async) {
    Blob b = MakeBlobWith(VSD_BACKEND_SLOT0, {{0, 100, 1000}, {1000, 3000, 2000}}, kImageSectors, false, 4096);
    EXPECT_EQ(b.st->bounce_sectors, 8u);
    const std::uint32_t buffer = Alloc(false, 100 * 512 + 64) + 4;
    std::memset(At<std::uint8_t>(buffer), 0xA5, 100 * 512);
    EXPECT_EQ(Read(b, 950, 100, buffer, async), 0);
    EXPECT_TRUE(ImageMatches(buffer, 950, 100));
    for (const Piece& p : g_ios.pieces) {
        EXPECT_TRUE(p.count <= 8u);
        EXPECT_EQ(p.data, b.st->bounce);
    }
    EXPECT_EQ(g_ios.pieces.size(), 14u);  // 50 sectors on each side of 1000, 8 at a time
    EXPECT_EQ(b.st->bounced, 14u);
    EXPECT_EQ(b.st->bounced_sectors, 100u);
    EXPECT_EQ(b.st->bounced_most, 50u);
    // The state ends where the buffer does: nothing past it is written.
    const std::uint32_t end = b.st->bounce + 4096;
    std::memset(At<std::uint8_t>(end), 0x5A, 64);
    Fill(buffer, 100 * 512, 0x66);
    EXPECT_EQ(Write(b, 1200, 100, buffer, async), 0);
    EXPECT_TRUE(ImageMatches(buffer, 1200, 100));
    for (std::uint32_t i = 0; i < 64; ++i) EXPECT_EQ(At<std::uint8_t>(end)[i], 0x5Au);
    // Aligned: one request a side of 1000, at full size.
    g_ios.pieces.clear();
    const std::uint32_t aligned = Alloc(false, 100 * 512);
    EXPECT_EQ(Read(b, 950, 100, aligned, async), 0);
    EXPECT_TRUE(ImageMatches(aligned, 950, 100));
    EXPECT_EQ(g_ios.pieces.size(), 2u);
    // Read-ahead with 8-sector windows: one fits.
    EnableReadAhead(b, 8, 0);
    EXPECT_EQ(b.st->ra.slots, 1u);
    g_ios.pieces.clear();
    EXPECT_EQ(Read(b, 16, 1, aligned, async), 0);
    EXPECT_EQ(Read(b, 17, 1, aligned, async), 0);
    EXPECT_TRUE(CardMatches(b, aligned, 17, 1));
    EXPECT_EQ(g_ios.pieces.size(), 1u);  // the window 16-23, read once
}

// Windows of 64 sectors lined up with sector 21 (a data area's start):
// what goes to the device, and that the game gets the image's bytes.
void TestReadAhead(bool async) {
    Blob b = MakeBlob(VSD_BACKEND_SLOT0);
    EnableReadAhead(b, 64, 21);
    const std::uint32_t buffer = Alloc(false, 400 * 512 + 64);
    // A small read brings in its whole window, from the cluster's start.
    EXPECT_EQ(Read(b, 30, 3, buffer, async), 0);
    EXPECT_TRUE(CardMatches(b, buffer, 30, 3));
    EXPECT_EQ(g_ios.pieces.size(), 1u);
    EXPECT_TRUE(PieceIs(0, DeviceSector(21), 64, RaBuffer(b)));
    // Reads inside it: nothing more on the device, single and multiple.
    EXPECT_EQ(Read(b, 33, 8, buffer, async), 0);
    EXPECT_TRUE(CardMatches(b, buffer, 33, 8));
    EXPECT_EQ(Read(b, 84, 1, buffer, async), 0);
    EXPECT_TRUE(CardMatches(b, buffer, 84, 1));
    EXPECT_EQ(Read(b, 21, 64, buffer, async), 0);
    EXPECT_TRUE(CardMatches(b, buffer, 21, 64));
    EXPECT_EQ(g_ios.pieces.size(), 1u);
    // Into an unaligned buffer (the bounce buffer's case), from memory too.
    EXPECT_EQ(Read(b, 40, 5, buffer + 4, async), 0);
    EXPECT_TRUE(CardMatches(b, buffer + 4, 40, 5));
    EXPECT_EQ(g_ios.pieces.size(), 1u);
    // Across the window's end: the held part copied, the next window read.
    EXPECT_EQ(Read(b, 80, 10, buffer, async), 0);
    EXPECT_TRUE(CardMatches(b, buffer, 80, 10));
    EXPECT_EQ(g_ios.pieces.size(), 2u);
    EXPECT_TRUE(PieceIs(1, DeviceSector(85), 64, RaBuffer(b)));
    // Across a piece of the image: straight up to the piece's end, then
    // the window cut at the piece's start.
    EXPECT_EQ(Read(b, 995, 8, buffer, async), 0);
    EXPECT_TRUE(CardMatches(b, buffer, 995, 8));
    EXPECT_EQ(g_ios.pieces.size(), 4u);
    EXPECT_TRUE(PieceIs(2, DeviceSector(995), 5, buffer));
    EXPECT_TRUE(PieceIs(3, DeviceSector(1000), 45, RaBuffer(b)));
    // The first piece's last window, cut at its end.
    EXPECT_EQ(Read(b, 985, 3, buffer, async), 0);
    EXPECT_TRUE(CardMatches(b, buffer, 985, 3));
    EXPECT_TRUE(PieceIs(4, DeviceSector(981), 19, RaBuffer(b)));
    // The image's last window, cut at its end.
    EXPECT_EQ(Read(b, 2998, 2, buffer, async), 0);
    EXPECT_TRUE(CardMatches(b, buffer, 2998, 2));
    EXPECT_TRUE(PieceIs(5, DeviceSector(2965), 35, RaBuffer(b)));
    // A big read stays big: straight into the game's buffer in the usual
    // pieces, and only its last window is read ahead.
    EXPECT_EQ(Read(b, 100, 300, buffer, async), 0);
    EXPECT_TRUE(CardMatches(b, buffer, 100, 300));
    EXPECT_EQ(g_ios.pieces.size(), 9u);
    EXPECT_TRUE(PieceIs(6, DeviceSector(100), 128, buffer));
    EXPECT_TRUE(PieceIs(7, DeviceSector(228), 113, buffer + 128 * 512));
    EXPECT_TRUE(PieceIs(8, DeviceSector(341), 64, RaBuffer(b)));
    EXPECT_TRUE(PiecesInsideExtents(b));
    EXPECT_EQ(b.st->transfer.in_use, 0u);
    // A piece through the bounce buffer (an unaligned buffer outside the
    // window) takes the window's room: it is forgotten, and read again.
    EXPECT_EQ(Read(b, 341, 20, buffer, async), 0);  // held
    std::size_t before = g_ios.pieces.size();
    EXPECT_EQ(Read(b, 1500, 70, buffer + 4, async), 0);
    EXPECT_TRUE(CardMatches(b, buffer + 4, 1500, 70));
    EXPECT_EQ(Read(b, 341, 20, buffer, async), 0);
    EXPECT_TRUE(CardMatches(b, buffer, 341, 20));
    EXPECT_TRUE(g_ios.pieces.size() > before + 1);
    // Small reads one after another: one request per window, not per read.
    // 405-1046 is twelve windows: nine from 405, 981-1044 cut in two at
    // the piece's edge (1000), and 1045's.
    g_ios.pieces.clear();
    for (std::uint32_t s = 405; s < 405 + 640; s += 3) EXPECT_EQ(Read(b, s, 3, buffer, async), 0);
    EXPECT_EQ(g_ios.pieces.size(), 12u);
    EXPECT_TRUE(CardMatches(b, buffer, 405 + 639, 3));
    EXPECT_TRUE(PiecesInsideExtents(b));
}

// Writes: the buffer keeps what the device holds.
void TestReadAheadWrites(bool async) {
    Blob b = MakeBlob(VSD_BACKEND_SLOT0);
    EnableReadAhead(b, 64, 21);
    const std::uint32_t buffer = Alloc(false, 64 * 512);
    const std::uint32_t data = Alloc(false, 20 * 512);
    EXPECT_EQ(Read(b, 30, 3, buffer, async), 0);
    // Inside the window: the next read sees the new bytes, from memory.
    Fill(data, 4 * 512, 0x5A);
    EXPECT_EQ(Write(b, 40, 4, data, async), 0);
    const std::size_t pieces = g_ios.pieces.size();
    EXPECT_EQ(Read(b, 30, 20, buffer, async), 0);
    EXPECT_EQ(g_ios.pieces.size(), pieces);
    EXPECT_TRUE(CardMatches(b, buffer, 30, 20));
    EXPECT_EQ(std::memcmp(At<std::uint8_t>(buffer + 10 * 512), At<std::uint8_t>(data), 4 * 512), 0);
    // Across the window's end: the held part updated.
    Fill(data, 20 * 512, 0x6B);
    EXPECT_EQ(Write(b, 80, 10, data, async), 0);
    EXPECT_EQ(Read(b, 82, 3, buffer, async), 0);
    EXPECT_TRUE(CardMatches(b, buffer, 82, 3));
    EXPECT_EQ(std::memcmp(At<std::uint8_t>(buffer), At<std::uint8_t>(data + 2 * 512), 3 * 512), 0);
    // A single-sector write.
    Fill(data, 512, 0x1C);
    EXPECT_EQ(Write(b, 21, 1, data, async), 0);
    EXPECT_EQ(Read(b, 21, 2, buffer, async), 0);
    EXPECT_TRUE(CardMatches(b, buffer, 21, 2));
    // A write the device took but answered with an error every try: the
    // card may hold any of it, so the buffer is dropped.
    g_ios.land_then_fail = VSD_TRIES;
    Fill(data, 2 * 512, 0x7C);
    EXPECT_TRUE(Write(b, 50, 2, data, async) < 0);
    const std::size_t before = g_ios.pieces.size();
    EXPECT_EQ(Read(b, 50, 2, buffer, async), 0);
    EXPECT_EQ(g_ios.pieces.size(), before + 1);  // read again from the device
    EXPECT_TRUE(CardMatches(b, buffer, 50, 2));
    EXPECT_EQ(std::memcmp(At<std::uint8_t>(buffer), At<std::uint8_t>(data), 2 * 512), 0);
    EXPECT_TRUE(PiecesInsideExtents(b));
    EXPECT_EQ(b.st->transfer.in_use, 0u);
}

// A window's read that fails: retried into the buffer, and when it fails
// every try nothing of it is believed afterwards.
void TestReadAheadFailures(bool async) {
    Blob b = MakeBlob(VSD_BACKEND_SLOT0);
    EnableReadAhead(b, 64, 21);
    const std::uint32_t buffer = Alloc(false, 8 * 512);
    g_ios.fail_next = 1;
    EXPECT_EQ(Read(b, 30, 3, buffer, async), 0);
    EXPECT_TRUE(CardMatches(b, buffer, 30, 3));
    EXPECT_EQ(g_ios.pieces.size(), 2u);
    EXPECT_TRUE(PieceIs(0, DeviceSector(21), 64, RaBuffer(b)));
    EXPECT_TRUE(PieceIs(1, DeviceSector(21), 64, RaBuffer(b)));
    // Another window fails every try, leaving garbage in the buffer: the
    // game's own two sectors are then read on their own, and it gets them.
    g_ios.fail_next = VSD_TRIES;
    g_ios.scribble_on_fail = true;
    std::size_t before_plain = g_ios.pieces.size();
    EXPECT_EQ(Read(b, 200, 2, buffer, async), 0);
    EXPECT_TRUE(CardMatches(b, buffer, 200, 2));
    EXPECT_EQ(g_ios.pieces.size(), before_plain + VSD_TRIES + 1);
    EXPECT_TRUE(PieceIs(g_ios.pieces.size() - 1, DeviceSector(200), 2, buffer));
    // When those fail too, the game gets the error.
    g_ios.fail_next = 2 * VSD_TRIES;
    EXPECT_TRUE(Read(b, 400, 2, buffer, async) < 0);
    EXPECT_EQ(b.st->transfer.in_use, 0u);
    // The first window is read again after (the failed ones left nothing).
    EXPECT_EQ(Read(b, 30, 3, buffer, async), 0);
    EXPECT_TRUE(CardMatches(b, buffer, 30, 3));
    EXPECT_EQ(b.st->transfer.in_use, 0u);
    g_ios.scribble_on_fail = false;
    // Something else writing over the window in memory (Project M EX
    // Remix's code menu did, in Dolphin): not served, read again, counted.
    const std::size_t before = g_ios.pieces.size();
    std::memcpy(At<std::uint8_t>(b.st->bounce) + 12 * 512, "JUMP\0\0\0\0SHIELD\0\0GRAB", 20);
    EXPECT_EQ(Read(b, 33, 1, buffer, async), 0);  // sector 33 is the window's 12th (from 21)
    EXPECT_TRUE(CardMatches(b, buffer, 33, 1));
    EXPECT_EQ(g_ios.pieces.size(), before + 1);
    EXPECT_EQ(b.st->ra.clobbered, 1u);
}

// With the resident runtime writing beside the blob, a window that fails
// every try falls back to the game's own sectors, and that read waits for
// the runtime's write again first: async, its earlier wait was the window's.
void TestReadAheadFallbackWaits() {
    Blob b = MakeBlob(VSD_BACKEND_SLOT0);
    EnableReadAhead(b, 64, 21);
    const std::uint32_t runtime_busy = Alloc(false, 32);
    *At<std::uint32_t>(runtime_busy) = 1;
    b.st->peer_busy = runtime_busy;
    const std::uint32_t buffer = Alloc(false, 4 * 512);
    g_ios.status_polls = 0;
    g_ios.busy_left = 1;  // the window's wait: one "programming", then ready
    g_ios.fail_next = VSD_TRIES;
    EXPECT_EQ(Read(b, 30, 2, buffer, true), 0);
    EXPECT_TRUE(CardMatches(b, buffer, 30, 2));
    EXPECT_EQ(g_ios.status_polls, 3u);  // two before the window, one more before the fallback
    EXPECT_TRUE(PieceIs(g_ios.pieces.size() - 1, DeviceSector(30), 2, buffer));
    *At<std::uint32_t>(runtime_busy) = 0;
}

// Reads going back and forth between a few clusters (Project M EX Remix's
// folder lists at start-up): with 16-sector windows the bounce buffer
// holds four, so three clusters read in turn cost one request each.
void TestReadAheadSlots(bool async) {
    Blob b = MakeBlob(VSD_BACKEND_SLOT0);
    EnableReadAhead(b, 16, 5);
    EXPECT_EQ(b.st->ra.slots, 4u);
    const std::uint32_t buffer = Alloc(false, 16 * 512);
    const std::uint32_t starts[3] = {5, 405, 1205};
    for (int round = 0; round < 5; ++round) {
        for (std::uint32_t s : starts) {
            const std::uint32_t sector = s + static_cast<std::uint32_t>(round) * 3;
            EXPECT_EQ(Read(b, sector, 2, buffer, async), 0);
            EXPECT_TRUE(CardMatches(b, buffer, sector, 2));
        }
    }
    EXPECT_EQ(g_ios.pieces.size(), 3u);
    // Each slot its own part of the bounce buffer.
    EXPECT_TRUE(PieceIs(0, DeviceSector(5), 16, RaBuffer(b)));
    EXPECT_TRUE(PieceIs(1, DeviceSector(405), 16, RaBuffer(b) + 16 * 512));
    EXPECT_TRUE(PieceIs(2, DeviceSector(1205), 16, RaBuffer(b) + 32 * 512));
    // Two more clusters: the fourth slot, then the one used longest ago
    // (5's) goes; the others stay.
    EXPECT_EQ(Read(b, 2005, 1, buffer, async), 0);
    EXPECT_EQ(Read(b, 2405, 1, buffer, async), 0);
    EXPECT_EQ(g_ios.pieces.size(), 5u);
    EXPECT_TRUE(PieceIs(4, DeviceSector(2405), 16, RaBuffer(b)));
    for (std::uint32_t s : {405u, 1205u, 2005u, 2405u}) EXPECT_EQ(Read(b, s + 1, 1, buffer, async), 0);
    EXPECT_EQ(g_ios.pieces.size(), 5u);
    EXPECT_EQ(Read(b, 6, 1, buffer, async), 0);
    EXPECT_EQ(g_ios.pieces.size(), 6u);
    EXPECT_TRUE(CardMatches(b, buffer, 6, 1));
    // A read across two windows that are both held: copied from both,
    // nothing sent (2005's and 2021's windows, 2021's read in first).
    EXPECT_EQ(Read(b, 2021, 1, buffer, async), 0);
    const std::size_t two = g_ios.pieces.size();
    EXPECT_EQ(Read(b, 2010, 16, buffer, async), 0);
    EXPECT_EQ(g_ios.pieces.size(), two);
    EXPECT_TRUE(CardMatches(b, buffer, 2010, 16));
    // A write reaches the slot holding it.
    const std::uint32_t data = Alloc(false, 512);
    Fill(data, 512, 0x4D);
    EXPECT_EQ(Write(b, 2410, 1, data, async), 0);
    EXPECT_EQ(Read(b, 2409, 3, buffer, async), 0);
    EXPECT_EQ(g_ios.pieces.size(), two + 1);  // the write only
    EXPECT_TRUE(CardMatches(b, buffer, 2409, 3));
    EXPECT_TRUE(PiecesInsideExtents(b));
}

// The other devices: d2x's SD and USB drivers; with the game's buffer in
// MEM1, which the USB drive cannot reach, the window still lands in MEM2.
void TestReadAheadDevices() {
    for (std::uint32_t backend : {VSD_BACKEND_D2X_SD, VSD_BACKEND_USB}) {
        for (bool async : {false, true}) {
            Blob b = MakeBlob(backend);
            EnableReadAhead(b, 32, 5);
            const std::uint32_t buffer = Alloc(false, 200 * 512);
            EXPECT_EQ(Read(b, 990, 3, buffer, async), 0);
            EXPECT_TRUE(CardMatches(b, buffer, 990, 3));
            EXPECT_TRUE(PieceIs(0, DeviceSector(965), 32, RaBuffer(b)));
            EXPECT_EQ(Read(b, 993, 4, buffer, async), 0);
            EXPECT_TRUE(CardMatches(b, buffer, 993, 4));
            EXPECT_EQ(g_ios.pieces.size(), 1u);
            EXPECT_EQ(Read(b, 900, 150, buffer, async), 0);
            EXPECT_TRUE(CardMatches(b, buffer, 900, 150));
            Fill(buffer, 4 * 512, 0x39);
            EXPECT_EQ(Write(b, 1030, 4, buffer, async), 0);
            EXPECT_EQ(Read(b, 1029, 6, buffer + 8 * 512, async), 0);
            EXPECT_TRUE(CardMatches(b, buffer + 8 * 512, 1029, 6));
            EXPECT_TRUE(PiecesInsideExtents(b));
            EXPECT_EQ(g_ios.bad_buffers, 0u);
        }
    }
}

// The whole thing against the device, byte for byte: an image in 40
// pieces scattered over the device, every window size, every device, sync
// and async, reads and writes of all lengths at random places (mostly
// running on from the last, as a file is read), into aligned, unaligned,
// MEM1 and MEM2 buffers, with a failed try now and then.
void TestReadAheadAgainstDevice() {
    std::uint32_t seed = 0x2611u;
    const auto next = [&seed](std::uint32_t below) {
        seed = seed * 1103515245u + 12345u;
        return ((seed >> 8) & 0xFFFFFFu) % below;
    };
    for (std::uint32_t backend : {VSD_BACKEND_SLOT0, VSD_BACKEND_D2X_SD, VSD_BACKEND_USB}) {
        for (std::uint32_t window : {8u, 16u, 32u, 64u}) {
            // 40 pieces of 1-149 sectors, laid on the device in a shuffled
            // order with gaps.
            std::vector<std::uint32_t> lengths;
            std::uint32_t image_sectors = 0;
            for (int i = 0; i < 40; ++i) {
                lengths.push_back(1 + next(149));
                image_sectors += lengths.back();
            }
            std::vector<std::uint32_t> order(lengths.size());
            for (std::uint32_t i = 0; i < order.size(); ++i) order[i] = i;
            for (std::uint32_t i = static_cast<std::uint32_t>(order.size()); i > 1; --i) std::swap(order[i - 1], order[next(i)]);
            std::vector<std::uint32_t> device_at(lengths.size());
            std::uint32_t at = 64;
            for (std::uint32_t i : order) {
                device_at[i] = at;
                at += lengths[i] + next(20);
            }
            std::vector<rtvsd_extent> extents;
            std::uint32_t file_sector = 0;
            for (std::uint32_t i = 0; i < lengths.size(); ++i) {
                extents.push_back({file_sector, device_at[i], lengths[i]});
                file_sector += lengths[i];
            }
            Blob b = MakeBlobWith(backend, extents, image_sectors);
            EnableReadAhead(b, window, next(window * 3));
            const std::uint32_t buffers[3] = {Alloc(false, 210 * 512), Alloc(false, 210 * 512) + 4,
                                              Alloc(true, 210 * 512)};
            const std::uint32_t data = Alloc(false, 210 * 512);
            const std::size_t mark = g_mem2_used;
            std::uint32_t position = 0;
            for (int op = 0; op < 1500; ++op) {
                g_mem2_used = mark;
                g_ios.pieces.clear();
                const bool async = next(2) != 0;
                const bool big = next(10) == 0;
                const std::uint32_t count = big ? 1 + next(200) : 1 + next(8);
                std::uint32_t sector = next(4) != 0 ? position : next(image_sectors);
                if (sector + count > image_sectors) sector = image_sectors - count;
                position = sector + count;
                if (next(20) == 0) g_ios.fail_next = 1;
                const std::uint32_t buffer = buffers[next(3)];
                if (next(7) == 0) {
                    for (std::uint32_t i = 0; i < count * 512; ++i) At<std::uint8_t>(data)[i] = static_cast<std::uint8_t>(next(256));
                    EXPECT_EQ(Write(b, sector, count, data, async), 0);
                    EXPECT_TRUE(CardMatches(b, data, sector, count));
                } else {
                    std::memset(At<std::uint8_t>(buffer), 0xA5, std::size_t(count) * 512);
                    EXPECT_EQ(Read(b, sector, count, buffer, async), 0);
                    if (!CardMatches(b, buffer, sector, count)) {
                        std::cerr << "FAILED: read " << count << " at " << sector << " (window " << window
                                  << ", device " << backend << ", op " << op << ")" << std::endl;
                        ++g_failures;
                        break;
                    }
                }
                EXPECT_TRUE(PiecesInsideExtents(b));
                // With a buffer the device reaches, only windows go through
                // the shared bounce buffer, never more than a window.
                const bool reached = (buffer & 31) == 0 && (backend != VSD_BACKEND_USB || buffer >= kMem2);
                for (const Piece& p : g_ios.pieces) {
                    if (reached && !p.write && p.data == RaBuffer(b)) EXPECT_TRUE(p.count <= window);
                }
                g_ios.fail_next = 0;
            }
            EXPECT_EQ(g_ios.bad_buffers, 0u);
            EXPECT_EQ(b.st->transfer.in_use, 0u);
            EXPECT_EQ(b.st->gave_up, 0u);
        }
    }
}

}  // namespace

int main() {
    g_mem1 = FixedBuffer(kMem1, kArena);
    g_mem2 = FixedBuffer(kMem2, kArena);
    if (!g_mem1 || !g_mem2) {
        std::cout << "vsdblob tests skipped: no memory at the Wii's addresses on this host" << std::endl;
        return 77;  // ctest: skipped, not passed
    }
    vsd_host_ioctl = HostIoctl;
    vsd_host_ioctlv = HostIoctlv;
    vsd_host_ioctl_async = HostIoctlAsync;
    vsd_host_ioctlv_async = HostIoctlvAsync;

    TestOpenAndStatus();
    TestAsyncRead(true);
    TestAsyncRead(false);
    TestAsyncWriteSettles();
    TestSyncWriteSettles();
    TestRuntimeBeside();
    TestReadAheadFallbackWaits();
    TestRetries();
    TestBusy();
    TestBounce();
    TestSmallBounce(false);
    TestSmallBounce(true);
    TestEvent();
    TestUsb();
    TestD2xSd();
    TestErrors();
    for (bool async : {false, true}) {
        TestReadAhead(async);
        TestReadAheadWrites(async);
        TestReadAheadFailures(async);
        TestReadAheadSlots(async);
    }
    TestReadAheadDevices();
    TestReadAheadAgainstDevice();

    if (g_failures == 0) std::cout << "vsdblob tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
