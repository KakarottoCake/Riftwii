// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The menu's SD watch (riftwii/sdhotplug.hpp): two polls the other way
// confirm a change, one alone does not.
#include <iostream>

#include "riftwii/sdhotplug.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using riftwii::SdHotplug;
using riftwii::SdHotplugEvent;

static void TestRemovalNeedsTwoMisses() {
    SdHotplug watch(true);
    EXPECT_TRUE(watch.update(true) == SdHotplugEvent::None);
    EXPECT_TRUE(watch.update(false) == SdHotplugEvent::None);
    EXPECT_TRUE(watch.mounted());
    EXPECT_TRUE(watch.update(false) == SdHotplugEvent::Removed);
    EXPECT_TRUE(!watch.mounted());
}

static void TestSingleMissDoesNotRemove() {
    SdHotplug watch(true);
    EXPECT_TRUE(watch.update(false) == SdHotplugEvent::None);
    EXPECT_TRUE(watch.mounted());
    EXPECT_TRUE(watch.update(true) == SdHotplugEvent::None);
    EXPECT_TRUE(watch.mounted());
    EXPECT_TRUE(watch.update(true) == SdHotplugEvent::None);
}

static void TestInsertionNeedsTwoHits() {
    SdHotplug watch(false);
    EXPECT_TRUE(watch.update(true) == SdHotplugEvent::None);
    EXPECT_TRUE(!watch.mounted());
    EXPECT_TRUE(watch.update(true) == SdHotplugEvent::Inserted);
    EXPECT_TRUE(watch.mounted());
}

static void TestSingleHitDoesNotInsert() {
    SdHotplug watch(false);
    EXPECT_TRUE(watch.update(true) == SdHotplugEvent::None);
    EXPECT_TRUE(watch.update(false) == SdHotplugEvent::None);
    EXPECT_TRUE(!watch.mounted());
}

static void TestRemovalDuringRescanNeedsTwoMisses() {
    SdHotplug watch(true);
    EXPECT_TRUE(watch.update(true) == SdHotplugEvent::None);
    // Pulled again while the rescan runs: one miss, back, one miss.
    EXPECT_TRUE(watch.update(false) == SdHotplugEvent::None);
    EXPECT_TRUE(watch.update(true) == SdHotplugEvent::None);
    EXPECT_TRUE(watch.update(false) == SdHotplugEvent::None);
    EXPECT_TRUE(watch.update(false) == SdHotplugEvent::Removed);
}

static void TestUsbOnlyStaysUnmounted() {
    SdHotplug watch(false);
    EXPECT_TRUE(watch.update(false) == SdHotplugEvent::None);
    EXPECT_TRUE(watch.update(false) == SdHotplugEvent::None);
    EXPECT_TRUE(!watch.mounted());
}

int main() {
    TestRemovalNeedsTwoMisses();
    TestSingleMissDoesNotRemove();
    TestInsertionNeedsTwoHits();
    TestSingleHitDoesNotInsert();
    TestRemovalDuringRescanNeedsTwoMisses();
    TestUsbOnlyStaysUnmounted();
    if (g_failures == 0) std::cout << "sdhotplug: all tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
