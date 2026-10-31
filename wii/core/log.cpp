// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "log.hpp"
#include "gecko.hpp"
#include "startuptrace.hpp"
#include "nandpermissions.hpp"

#include <ogc/lwp_watchdog.h>
#include <ogc/mutex.h>
#include <unistd.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
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
int g_batch = 0;        // LogBatch scopes alive
bool g_unsynced = false;  // lines in libfat's cache, not yet on the card

// The menu's scan thread logs while the menu thread does (homescan.hpp),
// as the network's and the screenshots' threads do: a line's time and its
// text are two writes, and without the lock two threads' lines came out
// mixed in session.log. Recursive: logf syncs through LogSync. Made at the
// first use, which is main's, before any other thread runs. Nothing logs
// from an interrupt; a crash's report is logged by a thread of its own,
// which the crash screen stops waiting for after a few seconds.
mutex_t g_lock = LWP_MUTEX_NULL;
struct Locked {
    Locked() {
        if (g_lock == LWP_MUTEX_NULL) LWP_MutexInit(&g_lock, true);
        LWP_MutexLock(g_lock);
    }
    ~Locked() { LWP_MutexUnlock(g_lock); }
};

std::string g_backlog;
constexpr std::size_t kMaxBacklog = 8 * 1024;
}

void LogEchoToScreen(bool on) { g_echo = on; }

void LogOpen(const char* sd_path, bool append) {
    Locked lock;
    LogClose();
    if (g_epoch == 0) g_epoch = gettime();
    g_path = sd_path;
    g_file = std::fopen(sd_path, append ? "a" : "w");
    g_line_start = true;
    g_backlog.clear();
    if (g_file) {
        startup::LogReady();
        LogNandPermissionRecovery();
    }
}

void LogReopen() {
    Locked lock;
    if (g_file || g_path.empty()) return;
    g_file = std::fopen(g_path.c_str(), "a");
    // Closed-file output already advanced the line state, including a
    // partial final line. Failed reopen attempts must preserve it too.
    if (g_file && !g_backlog.empty()) {
        std::fputs(g_backlog.c_str(), g_file);
        g_backlog.clear();
        std::fflush(g_file);
        fsync(fileno(g_file));
    }
}

void LogSync() {
    Locked lock;
    if (!g_file || !g_unsynced) return;
    fsync(fileno(g_file));
    g_unsynced = false;
}

LogBatch::LogBatch() {
    Locked lock;
    ++g_batch;
}

LogBatch::~LogBatch() {
    Locked lock;
    if (--g_batch == 0) LogSync();
}

void LogClose() {
    Locked lock;
    if (g_file) std::fclose(g_file);
    g_unsynced = false;
    g_file = nullptr;
}

void logf(const char* format, ...) {
    Locked lock;
    va_list args;
    if (g_echo) {
        va_start(args, format);
        std::vprintf(format, args);
        va_end(args);
    }
    // With Settings > USB Gecko log, each line also goes to the USB Gecko
    // as it is logged, with its time, card or no card; the text console
    // then leaves its own copy out (ConsoleStart).
    const bool to_gecko = GeckoActive();
    if (g_file || !g_path.empty() || to_gecko) {
        char text[1024];
        va_start(args, format);
        std::vsnprintf(text, sizeof(text), format, args);
        va_end(args);
        char line[1088];
        std::size_t used = 0;
        if (g_line_start) {
            const unsigned long ms = static_cast<unsigned long>(ticks_to_millisecs(gettime() - g_epoch));
            used = static_cast<std::size_t>(std::snprintf(line, sizeof(line), "[%6lu.%03lu] ", ms / 1000, ms % 1000));
        }
        const std::size_t text_chars = std::char_traits<char>::length(text);
        const std::size_t take = std::min(text_chars, sizeof(line) - 1 - used);
        std::memcpy(line + used, text, take);
        used += take;
        line[used] = '\0';
        if (to_gecko) GeckoWrite(line, used);
        if (!g_file && g_path.empty()) {
            g_line_start = take > 0 && text[take - 1] == '\n';
            return;
        }
        if (!g_file) {
            if (g_backlog.size() + used > kMaxBacklog) {
                const std::size_t drop = std::min(g_backlog.size(), g_backlog.size() + used - kMaxBacklog);
                g_backlog.erase(0, drop);
            }
            g_backlog.append(line, used);
            g_line_start = take > 0 && text[take - 1] == '\n';
            return;
        }
        std::fputs(line, g_file);
        const std::size_t n = std::char_traits<char>::length(text);
        g_line_start = n > 0 && text[n - 1] == '\n';
        // fflush only reaches libfat's sector cache; fsync writes the
        // cache to the card. Without it a hang loses every line since
        // the last close, which on hardware is exactly the part needed.
        std::fflush(g_file);
        g_unsynced = true;
        if (g_batch == 0) LogSync();
    }
}

}  // namespace riftwii::wii
