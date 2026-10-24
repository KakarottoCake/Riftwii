// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/channelupdate.hpp"

#include <iostream>

int main() {
    struct Case {
        const char* name;
        bool installed;
        unsigned version;
        unsigned needed;
        bool opened;
        bool refused;
        riftwii::ChannelUpdateState expected;
    };
    const Case cases[] = {
        {"first old-channel start", true, 13, 14, false, false, {true, false, false}},
        {"installer opened without an observed finish", true, 13, 14, true, false, {true, true, false}},
        {"failed attempt still present next start", true, 13, 14, true, false, {true, true, false}},
        {"installer returned without updating and removed its marker", true, 13, 14, false, false, {true, false, false}},
        {"refused hand-off clears a stale marker", true, 13, 14, true, true, {true, false, true}},
        {"refused without a marker", true, 13, 14, false, true, {true, false, false}},
        {"channel is current", true, 14, 14, true, false, {false, false, true}},
        {"channel is newer", true, 15, 14, true, false, {false, false, true}},
        {"channel was removed", false, 0, 14, true, false, {false, false, true}},
        {"no channel or attempt", false, 0, 14, false, false, {false, false, false}},
        {"Wii minimum differs from vWii", true, 13, 11, true, false, {false, false, true}},
    };
    unsigned failures = 0;
    for (const Case& test : cases) {
        const auto state = riftwii::channel_update_state(test.installed, test.version, test.needed,
                                                        test.opened, test.refused);
        if (state.required != test.expected.required ||
            state.installer_interrupted != test.expected.installer_interrupted ||
            state.clear_marker != test.expected.clear_marker) {
            std::cerr << "Unexpected update state: " << test.name << '\n';
            ++failures;
        }
    }
    if (failures == 0) std::cout << "channelupdate: all passed\n";
    return failures == 0 ? 0 : 1;
}
