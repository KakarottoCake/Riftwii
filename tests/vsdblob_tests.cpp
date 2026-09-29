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
        return -5;
    }
    if (device_sector + count > kDeviceSectors) return -4;
    std::uint8_t* dev = g_ios.device.data() + std::size_t(device_sector) * 512;
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

// Image sectors 0-999 at device sector 100, 1000-2999 at 3000: a read
// across 1000 takes two extents.
Blob MakeBlob(std::uint32_t backend, bool read_only = false) {
    g_mem1_used = g_mem2_used = 0;
    g_ios.pending.clear();
    g_ios.pieces.clear();
    g_ios.answers.clear();
    g_ios.backend = backend;
    g_ios.fail_next = g_ios.busy_polls = g_ios.busy_left = g_ios.status_polls = 0;
    g_ios.bad_callbacks = g_ios.bad_buffers = g_ios.sync_calls = 0;
    for (std::size_t i = 0; i < g_ios.device.size(); ++i)
        g_ios.device[i] = static_cast<std::uint8_t>((i * 7) ^ (i >> 9));
    Blob b;
    b.ctx = At<vsd_context>(Alloc(true, sizeof(vsd_context)));
    b.st = At<vsd_state>(Alloc(true, sizeof(vsd_state)));
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
    st->card.sectors = kImageSectors;
    st->card.extents[0] = {0, 100, 1000};
    st->card.extents[1] = {1000, 3000, 2000};
    st->card.extent_count = 2;
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
    for (const Piece& p : g_ios.pieces)
        EXPECT_EQ(p.data, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(b.st->bounce)));
    Fill(buffer, 100 * 512, 0x77);
    EXPECT_EQ(SendAsync(b, MakeCommand(25, 1200 * 512, 100, buffer), 0xCA, 0xDA), 0);
    PumpAll(b);
    EXPECT_TRUE(ImageMatches(buffer, 1200, 100));
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
    // No state: nothing hooked.
    vsd_context bare{};
    std::uint32_t a[8] = {Path("/dev/sdio/slot0"), 0};
    std::int32_t result = 0;
    EXPECT_EQ(vsd_on_ipc(&bare, VSD_OPEN, a, &result), 0);
}

}  // namespace

int main() {
    g_mem1 = FixedBuffer(kMem1, kArena);
    g_mem2 = FixedBuffer(kMem2, kArena);
    if (!g_mem1 || !g_mem2) {
        std::cout << "vsdblob tests skipped: no memory at the Wii's addresses on this host" << std::endl;
        return 0;
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
    TestRetries();
    TestBusy();
    TestBounce();
    TestEvent();
    TestUsb();
    TestD2xSd();
    TestErrors();

    if (g_failures == 0) std::cout << "vsdblob tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
