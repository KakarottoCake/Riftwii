// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The GameCube blob's storage requests (runtime/gc/gc_storage.h) against
// the code before they shared one request path (tests/gc_storage_legacy.h).
// Both run on the same made-up IOS: every cache flush and invalidate, every
// request with its IPC block, vectors, SENDCMD block and response as IOS
// gets it, every reply that was someone else's, every guard check and
// GC_FLAG_SD_SLOW wait, and the time-base reads are logged, and the two
// logs, results, counters and buffers must be the same for SD reads and
// writes (SDSC and SDHC, with and without the CMD13 status loop), USB reads
// split into 32 KB requests and USB writes, with failures thrown in.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <random>
#include <vector>

#define RT_TARGET_PPC
#define GC_HOST_COLD
#include "gc_rt.h"

#define GC_IPC_PPCMSG 0xCD000000u
#define GC_IPC_PPCCTRL 0xCD000004u
#define GC_IPC_ARMMSG 0xCD000008u
#define GC_IPC_X1 0x01u
#define GC_IPC_Y2 0x02u
#define GC_IPC_Y1 0x04u
#define GC_IPC_X2 0x08u
#define GC_IOS_IOCTL 6u
#define GC_IOS_IOCTLV 7u
#define GC_SDIO_SENDCMD 7u
#define GC_SD_READMULTIBLOCK 0x12u
#define GC_SD_WRITEMULTIBLOCK 0x19u
#define GC_SD_SEND_STATUS 0x0Du
#define GC_SD_CMDTYPE_AC 3u
#define GC_SD_RESPONSE_R1 1u
#define GC_SECTOR 512u
#define GC_SD_RESPONSE_BYTES 16u

namespace {

enum Event : std::uint32_t { kFlush = 1, kInvalidate, kSend, kOther, kGuard, kSlowWait, kWrite32 };

std::vector<std::uint32_t> g_log;
gc_rt_cold g_cold;
std::uint32_t g_clock;
// The made-up IOS: each request's result, CMD13's card states, and replies
// of other requests that come before a request's own.
std::deque<std::int32_t> g_results;
std::deque<std::uint32_t> g_states;
std::deque<std::uint32_t> g_mailbox;
std::mt19937 g_rng;
bool g_reply_waiting;

void note(std::initializer_list<std::uint32_t> words) { g_log.insert(g_log.end(), words); }

}  // namespace

gc_rt_cold* gc_cold(const gc_rt_context*) { return &g_cold; }

static std::uint32_t gc_physical(std::uint32_t address) { return address & 0x3FFFFFFFu; }
static std::uint32_t gc_timebase() { return ++g_clock; }

void gc_flush(std::uint32_t address, std::uint32_t length) { note({kFlush, address, length}); }
void gc_invalidate(std::uint32_t address, std::uint32_t length) { note({kInvalidate, address, length}); }
void gc_guard(gc_rt_context*) { note({kGuard}); }

int32_t gc_ipc_send(std::uint32_t phys, gc_rt_context*) {
    note({kSend, phys});
    for (std::uint32_t w : g_cold.ipc) note({w});
    for (std::uint32_t w : g_cold.vec) note({w});
    for (std::uint32_t w : g_cold.request) note({w});
    note({g_cold.response[0]});
    const std::int32_t result = g_results.empty() ? 0 : g_results.front();
    if (!g_results.empty()) g_results.pop_front();
    g_cold.ipc[1] = static_cast<std::uint32_t>(result);
    if (g_cold.ipc[0] == GC_IOS_IOCTL) {
        const std::uint32_t state = g_states.empty() ? 4 : g_states.front();
        if (!g_states.empty()) g_states.pop_front();
        g_cold.response[0] = state << 9;
    }
    if (g_rng() % 4 == 0) g_mailbox.push_back(0x00ABC000u | (g_rng() & 0xFFFu));
    g_mailbox.push_back(phys);
    g_reply_waiting = true;
    return 0;
}

static std::uint32_t gc_read32(std::uint32_t address) {
    if (address == GC_IPC_PPCCTRL) return g_reply_waiting && !g_mailbox.empty() ? GC_IPC_Y1 : 0;
    if (address == GC_IPC_ARMMSG) {
        const std::uint32_t reply = g_mailbox.front();
        g_mailbox.pop_front();
        return reply;
    }
    return 0;
}
static void gc_write32(std::uint32_t address, std::uint32_t value) { note({kWrite32, address, value}); }
static int gc_ipc_wait(gc_rt_context*, std::uint32_t, std::uint32_t) { return 1; }
static void gc_ipc_other(gc_rt_context*, std::uint32_t reply) { note({kOther, reply}); }
static std::uint32_t gc_sd_test_start(gc_rt_context*) { return 0; }
static void gc_sd_test_wait(gc_rt_context* ctx, std::uint32_t, std::uint32_t bytes) {
    if (ctx->flags & GC_FLAG_SD_SLOW) note({kSlowWait, bytes});
}

namespace legacy {
#include "../tests/gc_storage_legacy.h"
}
namespace current {
#include "gc_storage.h"
}

namespace {

struct Outcome {
    std::vector<std::uint32_t> log;
    std::int32_t result = 0;
    gc_rt_context ctx{};
    gc_rt_cold cold{};
};

struct Scenario {
    unsigned op = 0;
    std::uint32_t sector = 0, count = 0, buffer = 0;
    std::uint32_t sdhc = 0, rca = 0, flags = 0;
    std::vector<std::int32_t> results;
    std::vector<std::uint32_t> states;
    std::uint32_t seed = 0;
};

template <typename Ops>
Outcome run(const Scenario& s) {
    Outcome out;
    g_log.clear();
    std::memset(&g_cold, 0, sizeof(g_cold));
    g_clock = 0;
    g_results.assign(s.results.begin(), s.results.end());
    g_states.assign(s.states.begin(), s.states.end());
    g_mailbox.clear();
    g_rng.seed(s.seed);
    g_reply_waiting = false;
    gc_rt_context ctx{};
    ctx.sdio_fd = 3;
    ctx.usb_fd = 9;
    ctx.sdhc = s.sdhc;
    ctx.card_rca = s.rca;
    ctx.flags = s.flags;
    out.result = Ops::call(&ctx, s);
    out.log = g_log;
    out.ctx = ctx;
    out.cold = g_cold;
    return out;
}

struct LegacyOps {
    static std::int32_t call(gc_rt_context* ctx, const Scenario& s) {
        switch (s.op) {
        case 0: return legacy::gc_sd_read(ctx, s.sector, s.count, s.buffer);
        case 1: return legacy::gc_sd_write_locked(ctx, s.sector, s.count, s.buffer);
        case 2: return legacy::gc_usb_read(ctx, s.sector, s.count, s.buffer);
        default: return legacy::gc_usb_write(ctx, s.sector, s.count, s.buffer);
        }
    }
};

struct CurrentOps {
    static std::int32_t call(gc_rt_context* ctx, const Scenario& s) {
        switch (s.op) {
        case 0: return current::gc_sd_read(ctx, s.sector, s.count, s.buffer);
        case 1: return current::gc_sd_write_locked(ctx, s.sector, s.count, s.buffer);
        case 2: return current::gc_usb_read(ctx, s.sector, s.count, s.buffer);
        default: return current::gc_usb_write(ctx, s.sector, s.count, s.buffer);
        }
    }
};

bool same(const Outcome& a, const Outcome& b) {
    return a.log == b.log && a.result == b.result && std::memcmp(&a.ctx, &b.ctx, sizeof(a.ctx)) == 0 &&
           std::memcmp(&a.cold, &b.cold, sizeof(a.cold)) == 0;
}

}  // namespace

int main() {
    std::mt19937 rng(504);
    unsigned checked = 0, requests = 0;
    for (unsigned i = 0; i < 20000; ++i) {
        Scenario s;
        s.op = i % 4;
        s.sector = rng();
        s.count = s.op == 2 ? 1 + rng() % 300 : 1 + rng() % 16;
        s.buffer = 0x91000000u + (rng() % 0x10000u) * 32u;
        s.sdhc = rng() % 2;
        s.rca = rng() % 3 == 0 ? 0 : 1 + rng() % 0xFFFF;
        s.flags = rng() % 3 == 0 ? GC_FLAG_SD_SLOW : 0;
        for (unsigned k = 0; k < 8; ++k) s.results.push_back(rng() % 7 == 0 ? -static_cast<std::int32_t>(rng() % 9) : 0);
        for (unsigned k = 0; k < 4; ++k) s.states.push_back(rng() % 3 == 0 ? 4 : 6 + rng() % 2);
        s.seed = rng();
        const Outcome before = run<LegacyOps>(s);
        const Outcome after = run<CurrentOps>(s);
        if (!same(before, after)) {
            std::fprintf(stderr, "FAILED: scenario %u (op %u, sector %u, count %u): the new requests differ\n", i, s.op,
                         s.sector, s.count);
            return 1;
        }
        for (std::size_t k = 0; k < before.log.size();) {
            const std::uint32_t tag = before.log[k];
            if (tag == kSend) {
                ++requests;
                k += 2 + 16 + 8 + 16 + 1;
            } else if (tag == kGuard) {
                k += 1;
            } else if (tag == kOther || tag == kSlowWait) {
                k += 2;
            } else {
                k += 3;
            }
        }
        ++checked;
    }
    std::printf("gcstorage tests passed: %u scenarios, %u requests the same as before\n", checked, requests);
    return 0;
}
