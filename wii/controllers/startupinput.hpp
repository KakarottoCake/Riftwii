// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace riftwii::wii {

// After SetupPads, remember B while startup work keeps the GUI halted.
// Finish joins the sampler before the GUI can start scanning controllers.
void BeginStartupInput();
bool FinishStartupInput();

}  // namespace riftwii::wii
