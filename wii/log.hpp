// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// printf-style logging for the console/boot path: goes to the on-screen
// console (and the USB Gecko when one is attached, see ConsoleStart) and,
// while a log file is open, to the SD card as well.
namespace riftwii::wii {

void LogOpen(const char* sd_path, bool append = false);
void LogClose();
void logf(const char* format, ...) __attribute__((format(printf, 1, 2)));

}  // namespace riftwii::wii
