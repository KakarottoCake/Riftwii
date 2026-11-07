// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The virtual SD card's read-ahead rules (runtime/rtvsdra.c): which window
// a sector is read with, cut at the image's pieces and its end, what the
// buffer holds, which part of a read goes straight to the game, and how a
// write updates what is held. The blob's use of them, against a fake
// device, is in vsdblob_tests.cpp.
#include "rtvsdra.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <vector>

#if defined(_WIN32) || defined(__CYGWIN__)
#include <windows.h>
#elif defined(__linux__)
#include <sys/mman.h>
#endif

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << std::hex << (a) << " != " << (b) << std::dec << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

namespace {

struct CardDeleter {
    void operator()(rtvsd_card* card) const { std::free(card); }
};
using CardPtr = std::unique_ptr<rtvsd_card, CardDeleter>;

CardPtr make_card(std::uint32_t sectors, std::initializer_list<rtvsd_extent> extents) {
    const std::size_t bytes = sizeof(rtvsd_card) + rtvsd_extent_bytes(extents.size());
    rtvsd_card* card = static_cast<rtvsd_card*>(std::calloc(1, bytes));
    card->sectors = sectors;
    for (const rtvsd_extent& e : extents) rtvsd_extents(card)[card->extent_count++] = e;
    return CardPtr(card);
}

// The buffer sits where the Wii's MEM2 is (main maps it): the holding rule
// checks its bytes, the write rule writes them.
rtvsd_ra make_ra(std::uint32_t window, std::uint32_t anchor) {
    rtvsd_ra ra{};
    ra.buffer = 0x90000000u;
    ra.window = window;
    ra.slots = 1;
    ra.anchor = anchor;
    return ra;
}

// A window into its slot, as the blob reads one: the slot it takes, then filled.
std::uint32_t fill(rtvsd_ra& ra, std::uint32_t start, std::uint32_t count) {
    const std::uint32_t at = rtvsd_ra_take(&ra);
    rtvsd_ra_filled(&ra, start, count);
    return at;
}

std::uint32_t holds(rtvsd_ra& ra, std::uint32_t sector, std::uint32_t count, std::uint32_t* from = nullptr) {
    std::uint32_t at = 0;
    const std::uint32_t n = rtvsd_ra_holds(&ra, sector, count, &at);
    if (from) *from = at;
    return n;
}

void TestOn() {
    rtvsd_ra ra = make_ra(64, 0);
    EXPECT_TRUE(rtvsd_ra_on(&ra));
    for (std::uint32_t w : {8u, 16u, 32u, 64u}) {
        ra.window = w;
        EXPECT_TRUE(rtvsd_ra_on(&ra));
    }
    // Not a power of two, too small, too large for one request, no buffer.
    for (std::uint32_t w : {0u, 4u, 48u, 128u}) {
        ra.window = w;
        EXPECT_TRUE(!rtvsd_ra_on(&ra));
    }
    ra.window = 64;
    ra.buffer = 0;
    EXPECT_TRUE(!rtvsd_ra_on(&ra));
    // No slots, or more than the state keeps.
    ra.buffer = 0x90000000u;
    for (std::uint32_t s : {0u, RTVSD_RA_MAX_SLOTS + 1}) {
        ra.slots = s;
        EXPECT_TRUE(!rtvsd_ra_on(&ra));
    }
    ra.slots = 1;
    ra.buffer = 0;
    // Off: no window, and every read goes straight.
    CardPtr card = make_card(3000, {{0, 100, 3000}});
    std::uint32_t start = 77;
    EXPECT_EQ(rtvsd_ra_window(&ra, card.get(), 10, &start), 0u);
    EXPECT_EQ(start, 77u);
    EXPECT_EQ(rtvsd_ra_direct(&ra, card.get(), 10, 5), 5u);
}

// Windows line up with the data area's clusters (anchor), not with
// multiples of the window: Project M EX Remix's data area starts at
// sector 2323, Project+'s at 794.
void TestWindowLinesUpWithClusters() {
    CardPtr card = make_card(100000, {{0, 5000, 100000}});
    rtvsd_ra ra = make_ra(64, 2323);
    std::uint32_t start = 0;
    EXPECT_EQ(rtvsd_ra_window(&ra, card.get(), 2323, &start), 64u);
    EXPECT_EQ(start, 2323u);
    EXPECT_EQ(rtvsd_ra_window(&ra, card.get(), 2386, &start), 64u);
    EXPECT_EQ(start, 2323u);
    EXPECT_EQ(rtvsd_ra_window(&ra, card.get(), 2387, &start), 64u);
    EXPECT_EQ(start, 2387u);
    EXPECT_EQ(rtvsd_ra_window(&ra, card.get(), 50000, &start), 64u);
    EXPECT_EQ((start - 2323u) % 64u, 0u);
    EXPECT_TRUE(start <= 50000u && 50000u < start + 64u);
    // Below the anchor (the file allocation tables): the same grid.
    EXPECT_EQ(rtvsd_ra_window(&ra, card.get(), 2322, &start), 64u);
    EXPECT_EQ(start, 2259u);
    EXPECT_EQ(rtvsd_ra_window(&ra, card.get(), 20, &start), 64u);
    EXPECT_EQ(start, 19u);  // 2323 - 36 * 64
    // The first window is cut at sector 0.
    EXPECT_EQ(rtvsd_ra_window(&ra, card.get(), 5, &start), 19u);
    EXPECT_EQ(start, 0u);
    // A smaller window: half clusters, still on the grid.
    ra.window = 16;
    EXPECT_EQ(rtvsd_ra_window(&ra, card.get(), 2340, &start), 16u);
    EXPECT_EQ(start, 2339u);
}

// A window never crosses the edge of a piece of the image file: it is one
// request on the device.
void TestWindowStopsAtPieces() {
    // Three pieces: 0-999, 1000-1009 (a short one), 1010-2999.
    CardPtr card = make_card(3000, {{0, 100, 1000}, {1000, 5000, 10}, {1010, 7000, 1990}});
    rtvsd_ra ra = make_ra(64, 0);
    std::uint32_t start = 0;
    // 960-1023 holds 960-999 of the first piece.
    EXPECT_EQ(rtvsd_ra_window(&ra, card.get(), 999, &start), 40u);
    EXPECT_EQ(start, 960u);
    // The short piece is a window of its own.
    EXPECT_EQ(rtvsd_ra_window(&ra, card.get(), 1000, &start), 10u);
    EXPECT_EQ(start, 1000u);
    EXPECT_EQ(rtvsd_ra_window(&ra, card.get(), 1009, &start), 10u);
    EXPECT_EQ(start, 1000u);
    // The third piece's part of the grid's window 960-1023.
    EXPECT_EQ(rtvsd_ra_window(&ra, card.get(), 1010, &start), 14u);
    EXPECT_EQ(start, 1010u);
    // Each window maps to one run on the device.
    for (std::uint32_t s = 0; s < 3000; ++s) {
        const std::uint32_t n = rtvsd_ra_window(&ra, card.get(), s, &start);
        std::uint32_t device = 0;
        EXPECT_TRUE(n != 0 && start <= s && s < start + n);
        EXPECT_EQ(rtvsd_map(card.get(), start, n, &device), n);
    }
}

void TestWindowStopsAtImageEnd() {
    CardPtr card = make_card(3000, {{0, 100, 4000}});  // the piece runs past the image (a file's last cluster)
    rtvsd_ra ra = make_ra(64, 21);
    std::uint32_t start = 0;
    EXPECT_EQ(rtvsd_ra_window(&ra, card.get(), 2999, &start), 35u);
    EXPECT_EQ(start, 2965u);
    EXPECT_EQ(rtvsd_ra_window(&ra, card.get(), 3000, &start), 0u);
    // A grid window that would wrap past the top of the sector numbers.
    CardPtr big = make_card(0xFFFFFFF0u, {{0, 0, 0xFFFFFFF0u}});
    const std::uint32_t count = rtvsd_ra_window(&ra, big.get(), 0xFFFFFFE0u, &start);
    EXPECT_EQ(count + start, 0xFFFFFFF0u);
}

void TestHolds() {
    rtvsd_ra ra = make_ra(64, 0);
    EXPECT_EQ(holds(ra, 0, 4), 0u);
    EXPECT_EQ(fill(ra, 128, 64), 0x90000000u);
    std::uint32_t from = 0;
    EXPECT_EQ(holds(ra, 127, 4), 0u);
    EXPECT_EQ(holds(ra, 128, 4, &from), 4u);
    EXPECT_EQ(from, 0x90000000u);
    EXPECT_EQ(holds(ra, 180, 100, &from), 12u);
    EXPECT_EQ(from, 0x90000000u + 52 * 512);
    EXPECT_EQ(holds(ra, 191, 1), 1u);
    EXPECT_EQ(holds(ra, 192, 1), 0u);
    // One slot: the next window replaces it.
    fill(ra, 192, 64);
    EXPECT_EQ(holds(ra, 130, 1), 0u);
    EXPECT_EQ(holds(ra, 200, 1), 1u);
    rtvsd_ra_drop(&ra);
    EXPECT_EQ(holds(ra, 200, 1), 0u);
}

// Several windows at once (16 sectors, four slots): reads going back and
// forth between them stay in memory, and a new one takes the place of the
// one used longest ago.
void TestSlots() {
    rtvsd_ra ra = make_ra(16, 0);
    ra.slots = 4;
    EXPECT_EQ(fill(ra, 0, 16), 0x90000000u);
    EXPECT_EQ(fill(ra, 160, 16), 0x90000000u + 16 * 512);
    EXPECT_EQ(fill(ra, 320, 16), 0x90000000u + 32 * 512);
    std::uint32_t from = 0;
    EXPECT_EQ(holds(ra, 165, 2, &from), 2u);
    EXPECT_EQ(from, 0x90000000u + 16 * 512 + 5 * 512);
    EXPECT_EQ(holds(ra, 3, 1), 1u);
    EXPECT_EQ(fill(ra, 480, 16), 0x90000000u + 48 * 512);  // the last empty slot
    // Uses since: 160, 0, 480 (320 not since its fill). 320 goes first.
    EXPECT_EQ(holds(ra, 485, 1), 1u);
    EXPECT_EQ(fill(ra, 640, 16), 0x90000000u + 32 * 512);
    EXPECT_EQ(holds(ra, 320, 1), 0u);
    EXPECT_EQ(holds(ra, 160, 1), 1u);
    EXPECT_EQ(holds(ra, 0, 1), 1u);
    // Now 480 is the oldest.
    EXPECT_EQ(fill(ra, 800, 16), 0x90000000u + 48 * 512);
    EXPECT_EQ(holds(ra, 480, 1), 0u);
    EXPECT_EQ(holds(ra, 640, 16), 16u);
    EXPECT_EQ(holds(ra, 800, 16), 16u);
    // The clock wrapping does not upset which is oldest.
    ra.clock = 0xFFFFFFFFu;
    for (std::uint32_t i = 0; i < 4; ++i) ra.used[i] = 0xFFFFFFF0u + i;  // slot 0 oldest
    EXPECT_EQ(holds(ra, 0, 1), 1u);  // slot 0 used now, at clock 0: slot 1 is oldest
    EXPECT_EQ(fill(ra, 960, 16), 0x90000000u + 16 * 512);
    EXPECT_EQ(holds(ra, 0, 1), 1u);
    // Dropped: all of them.
    rtvsd_ra_drop(&ra);
    for (std::uint32_t s : {0u, 640u, 800u, 960u}) EXPECT_EQ(holds(ra, s, 1), 0u);
}

// Which part of a read goes straight to the game: everything before the
// window of its last sector, so a big read stays big and only its tail
// is read ahead.
void TestDirect() {
    CardPtr card = make_card(3000, {{0, 100, 1000}, {1000, 3000, 2000}});
    rtvsd_ra ra = make_ra(64, 21);
    // Inside one window: read it into the buffer.
    EXPECT_EQ(rtvsd_ra_direct(&ra, card.get(), 30, 3), 0u);
    EXPECT_EQ(rtvsd_ra_direct(&ra, card.get(), 21, 64), 0u);
    // Across a window edge: up to the last window.
    EXPECT_EQ(rtvsd_ra_direct(&ra, card.get(), 80, 10), 5u);
    EXPECT_EQ(rtvsd_ra_direct(&ra, card.get(), 100, 300), 241u);
    // Across a piece edge: the last window starts at the piece.
    EXPECT_EQ(rtvsd_ra_direct(&ra, card.get(), 995, 8), 5u);
    EXPECT_EQ(rtvsd_ra_direct(&ra, card.get(), 0, 0), 0u);
}

// The buffer is a 32-bit address, as in the game: placed where the Wii's
// MEM2 is (as vsdblob_tests.cpp does).
std::uint8_t* FixedBuffer(std::uintptr_t at, std::size_t bytes) {
#if defined(_WIN32) || defined(__CYGWIN__)
    return static_cast<std::uint8_t*>(
        VirtualAlloc(reinterpret_cast<void*>(at), bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
#elif defined(__linux__)
    void* p = mmap(reinterpret_cast<void*>(at), bytes, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    return p == MAP_FAILED || p != reinterpret_cast<void*>(at) ? nullptr : static_cast<std::uint8_t*>(p);
#else
    (void)at;
    (void)bytes;
    return nullptr;
#endif
}

void TestWrites(std::uint8_t* memory) {
    constexpr std::uintptr_t kAt = 0x90000000u;
    rtvsd_ra ra = make_ra(64, 0);
    ra.buffer = static_cast<std::uint32_t>(kAt);
    for (std::size_t i = 0; i < 64 * 512; ++i) memory[i] = 0x11;
    std::vector<std::uint8_t> data(20 * 512, 0x22);
    // Nothing held: nothing changes.
    rtvsd_ra_wrote(&ra, 128, 20, data.data());
    EXPECT_EQ(memory[0], 0x11);
    fill(ra, 128, 64);
    // Straddling the start: its last 10 sectors land in the first 10 held.
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<std::uint8_t>(i / 512);
    rtvsd_ra_wrote(&ra, 118, 20, data.data());
    EXPECT_EQ(memory[0], 10);
    EXPECT_EQ(memory[9 * 512 + 511], 19);
    EXPECT_EQ(memory[10 * 512], 0x11);
    // Straddling the end: its first 4 sectors land in the last 4 held.
    rtvsd_ra_wrote(&ra, 188, 20, data.data());
    EXPECT_EQ(memory[60 * 512], 0);
    EXPECT_EQ(memory[63 * 512 + 511], 3);
    EXPECT_EQ(memory[64 * 512], 0);  // past the buffer: untouched
    EXPECT_EQ(memory[59 * 512], 0x11);
    // Past it: nothing.
    rtvsd_ra_wrote(&ra, 192, 20, data.data());
    EXPECT_EQ(memory[64 * 512], 0);
    // Dropped: a later write leaves the old bytes alone.
    rtvsd_ra_drop(&ra);
    rtvsd_ra_wrote(&ra, 128, 1, data.data());
    EXPECT_EQ(memory[0], 10);
    // Several slots: a write reaches every slot holding part of it, each
    // at its own place.
    for (std::size_t i = 0; i < 64 * 512; ++i) memory[i] = 0x11;
    ra.window = 16;
    ra.slots = 4;
    fill(ra, 32, 16);  // slot 0
    fill(ra, 16, 16);  // slot 1
    rtvsd_ra_wrote(&ra, 30, 4, data.data());  // 30, 31 into slot 1's end; 32, 33 into slot 0's start
    EXPECT_EQ(memory[0], 2);
    EXPECT_EQ(memory[1 * 512], 3);
    EXPECT_EQ(memory[2 * 512], 0x11);
    EXPECT_EQ(memory[16 * 512 + 14 * 512], 0);
    EXPECT_EQ(memory[16 * 512 + 15 * 512], 1);
    EXPECT_EQ(memory[16 * 512 + 13 * 512], 0x11);
    // And the written sectors still pass their checks: they are served.
    std::uint32_t from = 0;
    EXPECT_EQ(holds(ra, 30, 2, &from), 2u);
    EXPECT_EQ(holds(ra, 32, 2, &from), 2u);
    EXPECT_EQ(ra.clobbered, 0u);
}

// Something else writing into the buffer (a mod's code using that memory):
// the window is dropped and counted, not served.
void TestClobbered(std::uint8_t* memory) {
    rtvsd_ra ra = make_ra(16, 0);
    ra.slots = 4;
    for (std::size_t i = 0; i < 64 * 512; ++i) memory[i] = static_cast<std::uint8_t>(i * 7);
    fill(ra, 0, 16);   // slot 0
    fill(ra, 64, 16);  // slot 1
    EXPECT_EQ(holds(ra, 3, 1), 1u);
    memory[16 * 512 + 5 * 512 + 100] ^= 0x40;  // slot 1, its sixth sector
    EXPECT_EQ(holds(ra, 64, 5), 5u);           // its first five are untouched
    EXPECT_EQ(holds(ra, 69, 1), 0u);
    EXPECT_EQ(ra.clobbered, 1u);
    EXPECT_EQ(holds(ra, 64, 1), 0u);           // the whole window is gone
    EXPECT_EQ(holds(ra, 3, 1), 1u);            // the other one stays
    EXPECT_EQ(ra.clobbered, 1u);
}

// A held sector of one repeated word overwritten with another repeated
// word (zeros, then 0xFF): a rotate-xor check gave both the same value.
void TestUniformOverwrite(std::uint8_t* memory) {
    rtvsd_ra ra = make_ra(16, 0);
    ra.slots = 4;
    for (std::size_t i = 0; i < 64 * 512; ++i) memory[i] = 0;
    fill(ra, 0, 16);
    EXPECT_EQ(holds(ra, 2, 1), 1u);
    for (std::size_t i = 0; i < 512; ++i) memory[2 * 512 + i] = 0xFF;
    EXPECT_EQ(holds(ra, 2, 1), 0u);
    EXPECT_EQ(ra.clobbered, 1u);
    // And one repeated word for another (0x01010101 for 0x02020202).
    for (std::size_t i = 0; i < 16 * 512; ++i) memory[i] = 0x01;
    fill(ra, 0, 16);
    for (std::size_t i = 0; i < 512; ++i) memory[7 * 512 + i] = 0x02;
    EXPECT_EQ(holds(ra, 7, 1), 0u);
    EXPECT_EQ(ra.clobbered, 2u);
    // Words differing only in their top bit (0x00000000 for 0x80000000):
    // plain FNV-1a gave both sectors the same value.
    for (std::size_t i = 0; i < 16 * 512; ++i) memory[i] = 0;
    fill(ra, 0, 16);
    const std::uint32_t top_bit = 0x80000000u;  // as a whole word, in the host's own byte order
    for (std::size_t i = 0; i < 512; i += 4) std::memcpy(memory + 9 * 512 + i, &top_bit, 4);
    EXPECT_EQ(holds(ra, 9, 1), 0u);
    EXPECT_EQ(ra.clobbered, 3u);
}

}  // namespace

int main() {
    // The buffer is read now (its check values): MEM2's address on the host.
    std::uint8_t* memory = FixedBuffer(0x90000000u, 0x10000);
    if (memory == nullptr) {
        std::cout << "vsdra tests skipped: no memory at the Wii's addresses on this host" << std::endl;
        return 77;  // ctest: skipped, not passed
    }
    TestOn();
    TestWindowLinesUpWithClusters();
    TestWindowStopsAtPieces();
    TestWindowStopsAtImageEnd();
    TestHolds();
    TestSlots();
    TestDirect();
    TestWrites(memory);
    TestClobbered(memory);
    TestUniformOverwrite(memory);
    if (g_failures == 0) std::cout << "vsdra tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
