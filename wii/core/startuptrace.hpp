// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace riftwii::wii::startup {

enum class Step : std::uint32_t {
    None, Stash, Arguments, Libogc, NandRecovery, Memory, Cards, Restart, Crash,
    SdStart, SdMount, SdStop, TestLimits, ArgumentsRead, Autorun,
    LogDirectory, LogRotate, LogOpen, Count
};

// Begin runs before BSS and libogc initialization: no heap, IOS or logging.
void Begin();
void Mark(Step step);
// Called only after fopen succeeded, including the headless and test logs.
void LogReady();

}  // namespace riftwii::wii::startup
