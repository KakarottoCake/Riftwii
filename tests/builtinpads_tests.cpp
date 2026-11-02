// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The USB controller layouts RiftWii carries (riftwii/builtinpads.hpp):
// each one read the way an SD card file is, so a layout the reader would
// refuse never ships.
#include "riftwii/builtinpads.hpp"
#include "riftwii/hidconfig.hpp"
#include "rthid.h"

#include <cstring>
#include <iostream>
#include <string>

namespace {

unsigned failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::cerr << __LINE__ << ": " #condition "\n"; ++failures; } } while (0)

// Every layout reads, names the IDs it is filed under, and fits the
// reader's size limit; the table is sorted with no ID twice.
void test_every_layout_reads() {
    CHECK(riftwii::kBuiltinPadCount > 0);
    for (std::size_t i = 0; i < riftwii::kBuiltinPadCount; ++i) {
        const riftwii::BuiltinPad& pad = riftwii::kBuiltinPads[i];
        rthid_mapping mapping{};
        std::string error;
        const bool valid = riftwii::parse_hid_config(pad.text, mapping, error);
        if (!valid) std::cerr << pad.file << ": " << error << '\n';
        CHECK(valid);
        CHECK(rthid_valid(&mapping));
        CHECK(((static_cast<std::uint32_t>(mapping.vid) << 16) | mapping.pid) == pad.vid_pid);
        CHECK(std::strlen(pad.text) <= riftwii::kHidConfigMaxBytes);
        CHECK(pad.name != nullptr && pad.name[0] != 0);
        if (i > 0) CHECK(riftwii::kBuiltinPads[i - 1].vid_pid < pad.vid_pid);
        // The Wii U GameCube adapter is RiftWii's own driver's.
        CHECK(pad.vid_pid != 0x057E0337u);
    }
}

// The pads players ask about are there, and an unknown one is not.
void test_lookup() {
    const riftwii::BuiltinPad* ds3 = riftwii::find_builtin_pad(0x054C0268u);
    CHECK(ds3 != nullptr);
    if (ds3) CHECK(std::string(ds3->name) == "DualShock 3");
    CHECK(riftwii::find_builtin_pad(0x054C05C4u) != nullptr);  // DualShock 4, first model
    CHECK(riftwii::find_builtin_pad(0x054C09CCu) != nullptr);  // DualShock 4, second model
    CHECK(riftwii::find_builtin_pad(0x054C0CE6u) != nullptr);  // DualSense
    // The 8BitDo SNES30 Pro shares the first DualShock 4's IDs; the
    // DualShock 4's layout is the one kept.
    const riftwii::BuiltinPad* ds4 = riftwii::find_builtin_pad(0x054C05C4u);
    if (ds4) CHECK(std::string(ds4->file) == "controller_ps4.ini");
    CHECK(riftwii::find_builtin_pad(0x12345678u) == nullptr);
    CHECK(riftwii::find_builtin_pad(0x057E0337u) == nullptr);
}

}  // namespace

int main() {
    test_every_layout_reads();
    test_lookup();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "builtinpads tests passed\n";
    return 0;
}
