// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "riftwii/plugin_api.h"

// The runtime of a plugin written in C++ (docs/PLUGINS.md, "Stage (b)"):
// what libstdc++ and the C library would give the link, put on RiftWii's
// table (rwpruntime.cpp). A plugin is built without exceptions or RTTI and
// has no unwinder, so nothing may throw: a failure that would have thrown
// (no memory, a container asked for too much) leaves through escape()
// instead, back to the plugin's launch, which returns it to RiftWii.
namespace riftwii::plugin {

// Called first by the plugin's rwp_init: the table every other function
// here uses. False when `table` is from another API or too short.
bool start_runtime(const rwp_host* table);

// RiftWii's table, as start_runtime was given it.
const rwp_host& host();

// What escape() leaves with: the RWP_LAUNCH_* result and a sentence.
struct Escape {
    std::int32_t result = 0;
    const char* why = nullptr;
};

// The way out of a launch for what cannot return normally. The launch
// calls __builtin_setjmp(escape_buffer()) and, while it is 0, arm_escape();
// from then until disarm_escape(), escape() comes back to that setjmp with
// 1 and escape_taken() says why. Outside that window escape() logs the
// reason and stops the plugin with a trap, which the crash report names.
// Nothing between is unwound: what the launch had allocated stays
// allocated, which is why its failure path restarts RiftWii.
void** escape_buffer();
void arm_escape();
void disarm_escape();
Escape escape_taken();
[[noreturn]] void escape(std::int32_t result, const char* why);

}  // namespace riftwii::plugin
