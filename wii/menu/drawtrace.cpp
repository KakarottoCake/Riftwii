// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "drawtrace.hpp"

namespace riftwii::wii {

const char* volatile g_guiPhase = "not started";
const char* volatile g_guiElement = "";
volatile unsigned long long g_drawTraceUntil = 0;

}  // namespace riftwii::wii
