// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Home's held-back buttons (riftwii/homechord.hpp): a tap against a hold,
// one button against a pair, and what each screen change leaves behind.
#include <cstdint>
#include <iostream>

#include "riftwii/homechord.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using riftwii::HeldBack;
using riftwii::SearchHold;
using riftwii::ViewPair;

constexpr std::uint32_t kY = 0x0800, kZ = 0x0010, kB = 0x0200, kA = 0x0100;

// A controller, one frame at a time: what's held, and what went down since
// the last frame, as libogc gives them.
struct Pad {
    std::uint32_t was = 0;
    std::uint32_t down = 0, held = 0;
    void frame(std::uint32_t now) {
        down = now & ~was;
        held = now;
        was = now;
    }
};

static void TestTapAndUse() {
    HeldBack c;
    Pad p;
    p.frame(kB);
    EXPECT_TRUE(riftwii::hold_back(c, p.down, p.held, kB));
    EXPECT_TRUE(!(p.down & kB));  // kept back
    p.frame(0);
    EXPECT_TRUE(!riftwii::hold_back(c, p.down, p.held, kB));
    EXPECT_TRUE(p.down & kB);  // let go alone: the press, late
    // Used while held: nothing when let go.
    p.frame(kB);
    riftwii::hold_back(c, p.down, p.held, kB);
    c.used = true;
    p.frame(0);
    riftwii::hold_back(c, p.down, p.held, kB);
    EXPECT_TRUE(!(p.down & kB));
    // Spent while held (a screen change): nothing either.
    p.frame(kB);
    riftwii::hold_back(c, p.down, p.held, kB);
    riftwii::spend(c);
    p.frame(0);
    riftwii::hold_back(c, p.down, p.held, kB);
    EXPECT_TRUE(!(p.down & kB));
}

static bool PairFrame(ViewPair& v, bool watching, Pad& p, std::uint32_t now) {
    p.frame(now);
    return riftwii::watch_pair(v, watching, p.down, p.held, kY, kZ);
}

static void TestPair() {
    // Y then Z: the pair, once, and neither press afterwards.
    {
        ViewPair v;
        Pad p;
        EXPECT_TRUE(!PairFrame(v, true, p, kY));
        EXPECT_TRUE(!(p.down & kY));
        EXPECT_TRUE(PairFrame(v, true, p, kY | kZ));
        EXPECT_TRUE(!PairFrame(v, true, p, kY | kZ));  // once
        PairFrame(v, true, p, kY);
        EXPECT_TRUE(!(p.down & kZ));
        PairFrame(v, true, p, 0);
        EXPECT_TRUE(!(p.down & kY));
    }
    // Y alone: its press when let go, no pair.
    {
        ViewPair v;
        Pad p;
        PairFrame(v, true, p, kY);
        EXPECT_TRUE(!PairFrame(v, true, p, 0));
        EXPECT_TRUE(p.down & kY);
    }
    // Y held since before Home watched isn't half of a pair.
    {
        ViewPair v;
        Pad p;
        PairFrame(v, false, p, kY);
        EXPECT_TRUE(p.down & kY);  // not watching: straight through
        EXPECT_TRUE(!PairFrame(v, true, p, kY | kZ));
    }
    // Watching stops with Y kept back: let go, Y comes late; a new Z
    // meanwhile goes straight through (a banner's own button, say).
    {
        ViewPair v;
        Pad p;
        PairFrame(v, true, p, kY);
        PairFrame(v, false, p, kY | kZ);
        EXPECT_TRUE(p.down & kZ);
        PairFrame(v, false, p, kZ);
        EXPECT_TRUE(p.down & kY);
    }
    // A popup closes after half a pair (forgotten and spent): the other
    // half completes nothing, and letting go of either does nothing (not
    // Z's Settings, #382).
    {
        ViewPair v;
        Pad p;
        PairFrame(v, true, p, kY);
        riftwii::forget_pair(v);
        riftwii::spend(v);
        EXPECT_TRUE(!PairFrame(v, true, p, kY | kZ));
        PairFrame(v, true, p, kZ);
        EXPECT_TRUE(!(p.down & kY));
        PairFrame(v, true, p, 0);
        EXPECT_TRUE(!(p.down & kZ));
    }
    // The same with Z let go first: neither comes either.
    {
        ViewPair v;
        Pad p;
        PairFrame(v, true, p, kY);
        riftwii::forget_pair(v);
        riftwii::spend(v);
        EXPECT_TRUE(!PairFrame(v, true, p, kY | kZ));
        PairFrame(v, true, p, kY);
        EXPECT_TRUE(!(p.down & kZ));
        PairFrame(v, true, p, 0);
        EXPECT_TRUE(!(p.down & kY));
    }
    // Y woke the screen (spent) and is still held: Z makes no pair, and
    // neither press comes when let go.
    {
        ViewPair v;
        Pad p;
        PairFrame(v, true, p, kY);
        riftwii::spend(v);
        EXPECT_TRUE(!PairFrame(v, true, p, kY | kZ));
        PairFrame(v, true, p, kY);
        EXPECT_TRUE(!(p.down & kZ));
        PairFrame(v, true, p, 0);
        EXPECT_TRUE(!(p.down & kY));
    }
    // After a pair, Y still held and Z pressed again: no second pair, and
    // that Z's press doesn't come late either.
    {
        ViewPair v;
        Pad p;
        PairFrame(v, true, p, kY);
        EXPECT_TRUE(PairFrame(v, true, p, kY | kZ));
        PairFrame(v, true, p, kY);
        EXPECT_TRUE(!PairFrame(v, true, p, kY | kZ));
        PairFrame(v, true, p, kY);
        EXPECT_TRUE(!(p.down & kZ));
        PairFrame(v, true, p, 0);
        EXPECT_TRUE(!(p.down & kY));
    }
    // Other buttons pass untouched.
    {
        ViewPair v;
        Pad p;
        PairFrame(v, true, p, kA);
        EXPECT_TRUE(p.down & kA);
    }
}

constexpr std::uint32_t kFrames = 50;

static bool HoldFrame(SearchHold& h, bool watched, std::uint32_t now, Pad& p, std::uint32_t buttons) {
    p.frame(buttons);
    return riftwii::watch_hold(h, watched, now, kFrames, p.down, p.held, kB);
}

static void TestHold() {
    // A short press: the search, on release.
    {
        SearchHold h;
        Pad p;
        std::uint32_t t = 100;
        HoldFrame(h, true, t++, p, kB);
        EXPECT_TRUE(!(p.down & kB));
        for (int i = 0; i < 10; ++i) EXPECT_TRUE(!HoldFrame(h, true, t++, p, kB));
        HoldFrame(h, true, t++, p, 0);
        EXPECT_TRUE(p.down & kB);
    }
    // Held its frames: the game at random, once, and no search after.
    {
        SearchHold h;
        Pad p;
        std::uint32_t t = 100;
        int fired = 0;
        for (std::uint32_t i = 0; i <= kFrames + 10; ++i) fired += HoldFrame(h, true, t++, p, kB);
        EXPECT_TRUE(fired == 1);
        HoldFrame(h, true, t++, p, 0);
        EXPECT_TRUE(!(p.down & kB));
    }
    // Home busy for a while mid-hold: the pause never counts.
    {
        SearchHold h;
        Pad p;
        std::uint32_t t = 100;
        for (int i = 0; i < 30; ++i) EXPECT_TRUE(!HoldFrame(h, true, t++, p, kB));
        for (int i = 0; i < 40; ++i) EXPECT_TRUE(!HoldFrame(h, false, t++, p, kB));
        bool fired = false;
        std::uint32_t after = 0;
        for (; after < kFrames + 5 && !fired; ++after) fired = HoldFrame(h, true, t++, p, kB);
        EXPECT_TRUE(fired && after >= kFrames);  // a full count after Home watched again
    }
    // Let go while Home is busy: still the search.
    {
        SearchHold h;
        Pad p;
        std::uint32_t t = 100;
        HoldFrame(h, true, t++, p, kB);
        HoldFrame(h, false, t++, p, kB);
        HoldFrame(h, false, t++, p, 0);
        EXPECT_TRUE(p.down & kB);
    }
    // A new press while not watched goes straight through.
    {
        SearchHold h;
        Pad p;
        HoldFrame(h, false, 100, p, kB);
        EXPECT_TRUE(p.down & kB);
    }
    // Spent as Home hands over (a game opens): nothing when let go.
    {
        SearchHold h;
        Pad p;
        std::uint32_t t = 100;
        HoldFrame(h, true, t++, p, kB);
        riftwii::spend(h);
        HoldFrame(h, true, t++, p, 0);
        EXPECT_TRUE(!(p.down & kB));
    }
}

int main() {
    TestTapAndUse();
    TestPair();
    TestHold();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "homechord tests passed" << std::endl;
    return 0;
}
