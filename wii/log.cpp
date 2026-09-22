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
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
    if (g_file) {
        char text[1024];
        va_start(args, format);
        std::vsnprintf(text, sizeof(text), format, args);
        va_end(args);
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
