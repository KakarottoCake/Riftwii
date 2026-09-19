// SPDX-License-Identifier: GPL-3.0-or-later
#include "log.hpp"

#include <cstdarg>
#include <cstdio>
#include <string>

namespace riftwii::wii {
namespace {
FILE* g_file = nullptr;
std::string g_path;
}

void LogOpen(const char* sd_path, bool append) {
    LogClose();
    g_path = sd_path;
    g_file = std::fopen(sd_path, append ? "a" : "w");
}

void LogReopen() {
    if (g_file || g_path.empty()) return;
    g_file = std::fopen(g_path.c_str(), "a");
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
        va_start(args, format);
        std::vfprintf(g_file, format, args);
        va_end(args);
        std::fflush(g_file);
    }
}

}  // namespace riftwii::wii
