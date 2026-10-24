// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/disccache.hpp"

#include <iostream>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Failed: " #condition " at line " << __LINE__ << '\n'; \
    ++failures; \
} } while (0)

using riftwii::CachedDisc;
using riftwii::DiscCache;
using Action = DiscCache::Action;

static const CachedDisc first_disc{"RFTE01", "First disc", 2, 1, true};
static const CachedDisc other_disc{"RFTJ01", "Other disc", 3, 0, false};

static void FirstProbeAndRepeatedPages() {
    DiscCache cache;
    CHECK(cache.action() == Action::Probe);
    cache.observe_cover(true);
    CHECK(cache.action() == Action::Probe);
    cache.probe_succeeded(first_disc);

    // Back to Home, reopen the page, and revisit after selecting an image:
    // none of those selections owns or overwrites the physical-disc cache.
    for (unsigned visit = 0; visit < 4; ++visit) {
        cache.observe_cover(true);
        CHECK(cache.action() == Action::Reuse);
        CHECK(cache.disc().game_id == first_disc.game_id);
        CHECK(cache.disc().title == first_disc.title);
        CHECK(cache.disc().revision == 2);
        CHECK(cache.disc().disc_number == 1);
        CHECK(cache.disc().burned);
    }
}

static void EjectAndInsert() {
    DiscCache cache;
    cache.observe_cover(false);
    CHECK(cache.action() == Action::Empty);

    for (const CachedDisc& inserted : {first_disc, other_disc}) {
        cache.probe_succeeded(first_disc);
        cache.observe_cover(false);
        CHECK(cache.action() == Action::Empty);
        CHECK(cache.disc().game_id.empty());
        CHECK(!cache.disc().burned);
        cache.observe_cover(false);
        CHECK(cache.action() == Action::Empty);
        cache.observe_cover(true);
        CHECK(cache.action() == Action::Probe);
        cache.probe_succeeded(inserted);
        CHECK(cache.action() == Action::Reuse);
        CHECK(cache.disc().game_id == inserted.game_id);
        CHECK(cache.disc().burned == inserted.burned);
    }
}

static void FailedProbeAndInvalidation() {
    DiscCache cache;
    cache.probe_succeeded(first_disc);
    cache.invalidate();  // failed probe/read, cover error, DI reset or IOS reload
    CHECK(cache.disc().game_id.empty());
    CHECK(cache.action() == Action::Probe);
    cache.observe_cover(true);
    CHECK(cache.action() == Action::Probe);
    cache.invalidate();  // retry also failed: never retain the old identity
    cache.observe_cover(true);
    CHECK(cache.action() == Action::Probe);
    cache.probe_succeeded(other_disc);
    CHECK(cache.action() == Action::Reuse);
    CHECK(cache.disc().game_id == other_disc.game_id);
}

static void LaunchAlwaysProbes() {
    DiscCache cache;
    cache.probe_succeeded(first_disc);
    cache.observe_cover(true);  // includes a swap missed between cover polls
    CHECK(cache.action() == Action::Reuse);
    cache.begin_launch();
    CHECK(cache.action() == Action::Probe);
    CHECK(cache.disc().game_id.empty());
    cache.observe_cover(true);
    CHECK(cache.action() == Action::Probe);
    cache.observe_cover(false);
    cache.begin_launch();
    CHECK(cache.action() == Action::Probe);
}

int main() {
    FirstProbeAndRepeatedPages();
    EjectAndInsert();
    FailedProbeAndInvalidation();
    LaunchAlwaysProbes();
    if (failures != 0) return 1;
    std::cout << "Disc page reuse, ejection, failures and fresh launches passed\n";
}
