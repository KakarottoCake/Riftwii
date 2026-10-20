// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "entropy.hpp"

#include <gccore.h>
#include <ogc/conf.h>
#include <ogc/isfs.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/mutex.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "bearssl.h"
#include "log.hpp"
#include "nanduse.hpp"
#include "online.hpp"

namespace riftwii::wii {

namespace {

constexpr const char* kSeedFile = "sd:/riftwii/seed.bin";
constexpr std::size_t kPoolBytes = 32;

// The GUI thread stirs in the Remotes every frame while a download thread
// takes a seeding: one lock for both.
mutex_t g_lock = LWP_MUTEX_NULL;
unsigned char g_pool[kPoolBytes] = {};
std::uint64_t g_count = 0;

// The seed file is written outside g_lock, so the GUI thread's stirring
// never waits on the card. This one is held across a seeding and its
// file's write, so two seedings write their files in the order they were
// made, and the newest is the one left on the card.
mutex_t g_file_lock = LWP_MUTEX_NULL;

// Both made on the main thread at start (EntropyStart), before any other
// thread takes them.
struct Locked {
    explicit Locked(mutex_t& lock = g_lock) : lock_(lock) {
        if (lock_ == LWP_MUTEX_NULL) LWP_MutexInit(&lock_, false);
        LWP_MutexLock(lock_);
    }
    ~Locked() { LWP_MutexUnlock(lock_); }
    Locked(const Locked&) = delete;
    Locked& operator=(const Locked&) = delete;

private:
    mutex_t& lock_;
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

// Writes the next seed file, made from the pool by Derive("seed file")
// under g_lock: not the pool itself, so what the card holds says nothing
// about the seedings already given out. Called without g_lock (the GUI
// thread stirs in the Remotes under it every frame, and a card write could
// hold a frame back), with g_file_lock held by the caller, and under a
// card hold, so the power button waits for it rather than unmounting the
// card mid-write (NEW-02).
void WriteSeedFile(unsigned char next[kPoolBytes]) {
    {
        CardWriteHold hold;
        if (FILE* f = std::fopen(kSeedFile, "wb")) {
            const bool ok = std::fwrite(next, 1, kPoolBytes, f) == kPoolBytes;
            if (std::fclose(f) != 0 || !ok) std::remove(kSeedFile);
        }
    }
    std::memset(next, 0, kPoolBytes);
}

// The Wii's network connections as the Wii Menu saved them (three
// profiles: SSIDs, keys, addresses), the NAND's own file, not SYSCONF.
// Whether it was read.
bool StirNetworkConfig() {
    constexpr u32 kConfigBytes = 0x1B5C;
    if (NandOpen() < 0) return false;
    bool read = false;
    const s32 fd = ISFS_Open("/shared2/sys/net/02/config.dat", ISFS_OPEN_READ);
    if (fd >= 0) {
        static unsigned char config[kConfigBytes] ATTRIBUTE_ALIGN(32);
        const s32 got = ISFS_Read(fd, config, kConfigBytes);
        ISFS_Close(fd);
        if (got > 0) {
            Stir("network config", config, static_cast<std::size_t>(got));
            read = true;
        }
        std::memset(config, 0, sizeof(config));
    }
    NandClose();
    return read;
}

// What this console's settings hold that another's does not: the
// addresses of the Wii Remotes paired with it (SYSCONF's BT.DINF), its
// nickname (IPL.NIK) and its network connections (StirNetworkConfig). Not
// secret, but not the clock: with no SD card there is no seed file, and
// the console's 32-bit ID was all the start had besides the time (CAV-03).
// Under the lock; returns how many were found.
unsigned StirConsoleSettings() {
    unsigned found = StirNetworkConfig() ? 1 : 0;
    if (CONF_Init() < 0) return found;
    static const char* const kNames[] = {"BT.DINF", "IPL.NIK"};
    for (const char* name : kNames) {
        const s32 length = CONF_GetLength(name);
        if (length <= 0 || length > 0x2000) continue;
        static unsigned char value[0x2000] ATTRIBUTE_ALIGN(32);
        if (CONF_Get(name, value, static_cast<u32>(length)) < 0) continue;
        Stir(name, value, static_cast<std::size_t>(length));
        std::memset(value, 0, sizeof(value));
        ++found;
    }
    return found;
}

// The clock, the console's ID and settings and the card's seed file into
// the pool; `next` gets the seed file that replaces it, `settings` how
// many of the console's settings went in. Whether there was a seed file.
bool StirStart(unsigned char next[kPoolBytes], unsigned& settings) {
    Locked lock;
    const std::time_t now = std::time(nullptr);
    Stir("start", &now, sizeof(now));
    u32 console = 0;
    if (ES_GetDeviceID(&console) >= 0) Stir("console", &console, sizeof(console));
    settings = StirConsoleSettings();
    unsigned char saved[kPoolBytes];
    bool had = false;
    if (FILE* f = std::fopen(kSeedFile, "rb")) {
        had = std::fread(saved, 1, sizeof(saved), f) == sizeof(saved);
        std::fclose(f);
    }
    if (had) Stir("seed file", saved, sizeof(saved));
    std::memset(saved, 0, sizeof(saved));
    Derive("seed file", next);
    return had;
}

// `size` bytes of seeding into `to`, each block from a pool that has moved
// on; `next` gets the seed file that follows them.
void TakeLocked(unsigned char* to, std::size_t size, unsigned char next[kPoolBytes]) {
    Locked lock;
    Stir("take", nullptr, 0);
    while (size != 0) {
        unsigned char block[kPoolBytes];
        Derive("output", block);
        const std::size_t n = size < sizeof(block) ? size : sizeof(block);
        std::memcpy(to, block, n);
        to += n;
        size -= n;
        Stir("given", nullptr, 0);
    }
    Derive("seed file", next);
}

}  // namespace

void EntropyStart() {
    if (g_file_lock == LWP_MUTEX_NULL) LWP_MutexInit(&g_file_lock, false);
    Locked file_lock(g_file_lock);
    unsigned char next[kPoolBytes];
    unsigned settings = 0;
    const bool had = StirStart(next, settings);
    // Replaced at once: a start that stops before its first connection
    // must not leave the next start the same file.
    WriteSeedFile(next);
    logf("TLS: random seed %s (%u of the console's settings)\n",
         had ? "from the card's seed file, the console, its settings and the clock"
             : "from the console, its settings and the clock (no seed file yet; one is made now)",
         settings);
}

void EntropyAdd(const void* data, std::size_t size) {
    Locked lock;
    Stir("event", data, size);
}

void EntropyTake(void* out, std::size_t size) {
    Locked file_lock(g_file_lock);
    unsigned char next[kPoolBytes];
    TakeLocked(static_cast<unsigned char*>(out), size, next);
    WriteSeedFile(next);
}

}  // namespace riftwii::wii
