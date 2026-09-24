// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The progress line of the launch screen: what the launch is doing and a
// bar with its percentage, drawn straight into the framebuffer under the
// log (the GUI is stopped by then). Every call is a no-op until
// ProgressAttach, so autorun launches and tests just skip it.
namespace riftwii::wii {

// `xfb` is the YUYV frame on screen, `fb_width` x `fb_height`; the line
// takes x..x+width, y..y+32 (the label row, then the bar).
void ProgressAttach(void* xfb, int fb_width, int fb_height, int x, int y, int width);

// A new stage at `percent` (0-100). The bar never goes back.
void ProgressStage(const char* stage, int percent);
// Progress within the current stage: `done` of `total` maps onto
// from..to percent.
void ProgressWithin(unsigned long long done, unsigned long long total, int from, int to);

}  // namespace riftwii::wii
