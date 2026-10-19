// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "entropy.hpp"

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/mutex.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "bearssl.h"
#include "log.hpp"

namespace riftwii::wii {

namespace {

constexpr const char* kSeedFile = "sd:/riftwii/seed.bin";
constexpr std::size_t kPoolBytes = 32;

// The GUI thread stirs in the Remotes every frame while a download thread
// takes a seeding: one lock for both.
mutex_t g_lock = LWP_MUTEX_NULL;
unsigned char g_pool[kPoolBytes] = {};
std::uint64_t g_count = 0;

struct Locked {
    Locked() {
        if (g_lock == LWP_MUTEX_NULL) LWP_MutexInit(&g_lock, false);
        LWP_MutexLock(g_lock);
    }
    ~Locked() { LWP_MutexUnlock(g_lock); }
};

// pool = SHA-256(pool, label, count, time base, data). Under the lock.
void Stir(const char* label, const void* data, std::size_t size) {
    br_sha256_context hash;
    br_sha256_init(&hash);
    br_sha256_update(&hash, g_pool, sizeof(g_pool));
    br_sha256_update(&hash, label, std::strlen(label));
    const std::uint64_t count = ++g_count;
    const u64 now = gettime();
    br_sha256_update(&hash, &count, sizeof(count));
    br_sha256_update(&hash, &now, sizeof(now));
    if (data != nullptr && size != 0) br_sha256_update(&hash, data, size);
    br_sha256_out(&hash, g_pool);
}

// SHA-256(pool, label, count) into `out`, without moving the pool.
void Derive(const char* label, unsigned char out[kPoolBytes]) {
    br_sha256_context hash;
    br_sha256_init(&hash);
    br_sha256_update(&hash, g_pool, sizeof(g_pool));
    br_sha256_update(&hash, label, std::strlen(label));
    br_sha256_update(&hash, &g_count, sizeof(g_count));
    br_sha256_out(&hash, out);
}

// A new seed file from the pool. Not the pool itself: what the card holds
// says nothing about the seedings already given out.
void RenewSeedFile() {
    unsigned char next[kPoolBytes];
    Derive("seed file", next);
    FILE* f = std::fopen(kSeedFile, "wb");
    if (!f) return;
    const bool ok = std::fwrite(next, 1, sizeof(next), f) == sizeof(next);
    if (std::fclose(f) != 0 || !ok) std::remove(kSeedFile);
}

}  // namespace

void EntropyStart() {
    Locked lock;
    const std::time_t now = std::time(nullptr);
    Stir("start", &now, sizeof(now));
    u32 console = 0;
    if (ES_GetDeviceID(&console) >= 0) Stir("console", &console, sizeof(console));
    unsigned char saved[kPoolBytes];
    bool had = false;
    if (FILE* f = std::fopen(kSeedFile, "rb")) {
        had = std::fread(saved, 1, sizeof(saved), f) == sizeof(saved);
        std::fclose(f);
    }
    if (had) Stir("seed file", saved, sizeof(saved));
    std::memset(saved, 0, sizeof(saved));
    // Replaced at once: a start that stops before its first connection
    // must not leave the next start the same file.
    RenewSeedFile();
    logf("TLS: random seed %s\n", had ? "from the card's seed file, the console and the clock"
                                      : "from the console and the clock (no seed file yet; one is made now)");
}

void EntropyAdd(const void* data, std::size_t size) {
    Locked lock;
    Stir("event", data, size);
}

void EntropyTake(void* out, std::size_t size) {
    Locked lock;
    Stir("take", nullptr, 0);
    unsigned char* to = static_cast<unsigned char*>(out);
    while (size != 0) {
        unsigned char block[kPoolBytes];
        Derive("output", block);
        const std::size_t n = size < sizeof(block) ? size : sizeof(block);
        std::memcpy(to, block, n);
        to += n;
        size -= n;
        Stir("given", nullptr, 0);  // each block from a pool that has moved on
    }
    RenewSeedFile();
}

}  // namespace riftwii::wii
