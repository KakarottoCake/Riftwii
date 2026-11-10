// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The native CTGP-R launch's buffer arena: first-fit allocation with joining
// of freed neighbours, and the std allocator that draws from it.
#include <cstdlib>
#include <iostream>

#include "riftwii/ctgparena.hpp"

using namespace riftwii;

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

static void test_arena_blocks() {
    void* memory = std::aligned_alloc(32, 1 << 20);
    CtgpArena arena;
    arena.attach(memory, 1 << 20);
    void* a = arena.allocate(300 * 1024);
    void* b = arena.allocate(300 * 1024);
    void* c = arena.allocate(300 * 1024);
    EXPECT_TRUE(a && b && c);
    EXPECT_TRUE(arena.owns(a) && arena.owns(c));
    EXPECT_TRUE(reinterpret_cast<std::uintptr_t>(b) % 32 == 0);
    EXPECT_TRUE(arena.allocate(300 * 1024) == nullptr);  // 100 KiB left: no room
    EXPECT_EQ(arena.live_blocks(), std::size_t(3));
    // Two freed neighbours join up into one piece big enough for 500 KiB.
    arena.release(a);
    arena.release(b);
    void* d = arena.allocate(500 * 1024);
    EXPECT_TRUE(d == a);
    arena.release(d);
    arena.release(c);
    EXPECT_EQ(arena.in_use(), std::size_t(0));
    EXPECT_TRUE(arena.peak() >= 900 * 1024);
    // Whole again: one block of everything fits.
    void* all = arena.allocate(1 << 20);
    EXPECT_TRUE(all == memory);
    EXPECT_TRUE(arena.detach() == nullptr);  // not while a block is out
    arena.release(all);
    EXPECT_TRUE(arena.detach() == memory);
    EXPECT_TRUE(!arena.attached());
    EXPECT_TRUE(arena.allocate(32) == nullptr);
    std::free(memory);
}

static void test_allocator_draws_big_blocks() {
    void* memory = std::aligned_alloc(32, 256 * 1024);
    CtgpArena arena;
    arena.attach(memory, 256 * 1024);
    g_ctgp_arena = &arena;
    {
        CtgpBytes big(100 * 1024, 1);
        EXPECT_TRUE(arena.owns(big.data()));
        CtgpBytes small(100, 2);
        EXPECT_TRUE(!arena.owns(small.data()));  // under the threshold: the heap
        CtgpBytes second(100 * 1024, 3);
        EXPECT_TRUE(arena.owns(second.data()));
        // No room left for another big one: it falls back to the heap.
        CtgpBytes third(100 * 1024, 4);
        EXPECT_TRUE(!arena.owns(third.data()));
        EXPECT_EQ(third[99 * 1024], std::uint8_t(4));
        // A vector that grows moves its block and frees the old one.
        CtgpBytes grown;
        for (int i = 0; i < 40 * 1024; ++i) grown.push_back(static_cast<std::uint8_t>(i));
        EXPECT_EQ(grown[300], std::uint8_t(300 & 0xFF));
    }
    EXPECT_EQ(arena.live_blocks(), std::size_t(0));  // everything went back
    g_ctgp_arena = nullptr;
    EXPECT_TRUE(arena.detach() == memory);
    std::free(memory);
}

static void test_no_arena_is_the_heap() {
    g_ctgp_arena = nullptr;
    CtgpBytes v(1 << 20, 9);
    EXPECT_EQ(v[1 << 19], std::uint8_t(9));
}

int main() {
    test_arena_blocks();
    test_allocator_draws_big_blocks();
    test_no_arena_is_the_heap();
    if (g_failures == 0) std::cout << "ctgparena tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
