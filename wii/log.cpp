// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "log.hpp"

#include <ogc/lwp_watchdog.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdio>
#include <string>

namespace riftwii::wii {
namespace {
FILE* g_file = nullptr;
std::string g_path;
bool g_line_start = true;
u64 g_epoch = 0;  // time base at the first LogOpen: file lines carry ms since then
// Off while the GUI owns the screen: libwiigui's framebuffer is also the
// text console's, so every echoed line flashed over the menus.
bool g_echo = false;
// LogHoldScreen: the lines not printed, the newest kHeldMax bytes.
bool g_hold = false;
std::string g_held;
constexpr std::size_t kHeldMax = 4096;
}

void LogEchoToScreen(bool on) {
    g_echo = on;
    if (on) g_hold = false;
}

void LogHoldScreen() {
    g_echo = false;
    g_hold = true;
    g_held.clear();
}

void LogShowHeld() {
    if (!g_hold) return;
    g_hold = false;
    g_echo = true;
    // From a line's start, after clearing the window the message was in.
    std::size_t from = 0;
    if (g_held.size() >= kHeldMax) {
        const std::size_t nl = g_held.find('\n');
        if (nl != std::string::npos) from = nl + 1;
    }
    std::printf("\x1b[2J\x1b[0;0H");
    std::fputs(g_held.c_str() + from, stdout);
    g_held.clear();
}

void LogOpen(const char* sd_path, bool append) {
    LogClose();
    if (g_epoch == 0) g_epoch = gettime();
    g_path = sd_path;
    g_file = std::fopen(sd_path, append ? "a" : "w");
    g_line_start = true;
}

void LogReopen() {
    if (g_file || g_path.empty()) return;
    g_file = std::fopen(g_path.c_str(), "a");
    g_line_start = true;
}

void LogClose() {
    if (g_file) std::fclose(g_file);
    g_file = nullptr;
}

void logf(const char* format, ...) {
    va_list args;
    if (g_echo) {
        va_start(args, format);
        std::vprintf(format, args);
        va_end(args);
    }
    if (g_file || g_hold) {
        char text[1024];
        va_start(args, format);
        std::vsnprintf(text, sizeof(text), format, args);
        va_end(args);
        if (g_hold) {
            g_held += text;
            if (g_held.size() > kHeldMax) g_held.erase(0, g_held.size() - kHeldMax);
        }
        if (!g_file) return;
        if (g_line_start) {
            const unsigned long ms = static_cast<unsigned long>(ticks_to_millisecs(gettime() - g_epoch));
            std::fprintf(g_file, "[%6lu.%03lu] ", ms / 1000, ms % 1000);
        }
        std::fputs(text, g_file);
        const std::size_t n = std::char_traits<char>::length(text);
        g_line_start = n > 0 && text[n - 1] == '\n';
        // fflush only reaches libfat's sector cache; fsync writes the
        // cache to the card. Without it a hang loses every line since
        // the last close, which on hardware is exactly the part needed.
        std::fflush(g_file);
        fsync(fileno(g_file));
    }
}

}  // namespace riftwii::wii
