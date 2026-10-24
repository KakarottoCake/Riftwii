// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/channelupdate.hpp"

#include <iostream>

int main() {
    using Attempt = riftwii::ChannelInstallerAttempt;
    struct Case {
        const char* name;
        bool installed;
        unsigned version;
        unsigned needed;
        Attempt attempt;
        bool refused;
        riftwii::ChannelUpdateState expected;
    };
    const Case cases[] = {
        // Expected: required, interrupted, returned, offer Not now, clear marker.
        {"first old-channel start", true, 13, 14, Attempt::None, false, {true, false, false, false, false}},
        {"installer opened without an observed finish", true, 13, 14, Attempt::Opened, false, {true, true, false, true, false}},
        {"interruption still present next start", true, 13, 14, Attempt::Opened, false, {true, true, false, true, false}},
        {"installer returned without updating offers Not now", true, 13, 14, Attempt::Returned, false, {true, false, true, true, false}},
        {"normal return still offers Not now next start", true, 13, 14, Attempt::Returned, false, {true, false, true, true, false}},
        {"refused hand-off clears a stale marker", true, 13, 14, Attempt::Opened, true, {true, false, false, true, true}},
        {"refused hand-off supersedes a returned marker", true, 13, 14, Attempt::Returned, true, {true, false, false, true, true}},
        {"refused without a marker", true, 13, 14, Attempt::None, true, {true, false, false, true, false}},
        {"channel is current", true, 14, 14, Attempt::Opened, false, {false, false, false, false, true}},
        {"successful return clears a stale marker", true, 14, 14, Attempt::Returned, false, {false, false, false, false, true}},
        {"channel is newer", true, 15, 14, Attempt::Opened, false, {false, false, false, false, true}},
        {"channel was removed", false, 0, 14, Attempt::Returned, false, {false, false, false, false, true}},
        {"no channel or attempt", false, 0, 14, Attempt::None, false, {false, false, false, false, false}},
        {"current channel and refused marker need no recovery", true, 14, 14, Attempt::Opened, true, {false, false, false, false, true}},
        {"absent channel and refused marker need no recovery", false, 0, 14, Attempt::None, true, {false, false, false, false, false}},
        {"Wii minimum differs from vWii", true, 13, 11, Attempt::Returned, false, {false, false, false, false, true}},
    };
    unsigned failures = 0;
    for (const Case& test : cases) {
        const auto state = riftwii::channel_update_state(test.installed, test.version, test.needed,
                                                        test.attempt, test.refused);
        if (state.required != test.expected.required ||
            state.installer_interrupted != test.expected.installer_interrupted ||
            state.installer_returned != test.expected.installer_returned ||
            state.offer_not_now != test.expected.offer_not_now ||
            state.clear_marker != test.expected.clear_marker) {
            std::cerr << "Unexpected update state: " << test.name << '\n';
            ++failures;
        }
    }
    if (failures == 0) std::cout << "channelupdate: all passed\n";
    return failures == 0 ? 0 : 1;
}
