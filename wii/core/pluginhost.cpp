// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "pluginhost.hpp"

#include <dirent.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "httpmem.hpp"
#include "loadersettings.hpp"
#include "log.hpp"
#include "netsock.hpp"
#include "tls.hpp"
#include "riftwii/http.hpp"
#include "riftwii/sha256.hpp"

namespace riftwii::wii::pluginhost {
namespace {

void CopyError(const std::string& text, char* error, uint32_t error_bytes) {
    if (error == nullptr || error_bytes == 0) return;
    std::snprintf(error, error_bytes, "%s", text.c_str());
}

// A block of memalign'd memory given back when it goes out of scope.
struct Block {
    void* data = nullptr;
    explicit Block(std::size_t bytes) : data(memalign(32, bytes)) {}
    ~Block() { std::free(data); }
    Block(const Block&) = delete;
    Block& operator=(const Block&) = delete;
};

void (*g_job)(void*) = nullptr;
void* g_jobArg = nullptr;

void RunJob() {
    void (*job)(void*) = g_job;
    void* arg = g_jobArg;
    if (job != nullptr) job(arg);
}

}  // namespace

int32_t NetReady(char* error, uint32_t error_bytes) {
    // The player's switch for everything RiftWii downloads.
    if (!Settings().online) {
        CopyError("downloads are off in Settings > Online", error, error_bytes);
        return 0;
    }
    std::string why;
    if (!NetStart(why)) {
        CopyError(why, error, error_bytes);
        return 0;
    }
    return 1;
}

int32_t HttpGet(const char* url, uint8_t* buffer, uint32_t capacity, uint32_t* length, char* error,
                uint32_t error_bytes) {
    if (length != nullptr) *length = 0;
    if (url == nullptr || buffer == nullptr || capacity == 0 || length == nullptr) {
        CopyError("the download was not set up", error, error_bytes);
        return 0;
    }
#ifdef RIFTWII_TEST_BUILD
    // The test build only: a list on the card stands in for the store's
    // (Dolphin tests of list fields the published list does not have yet).
    if (std::strcmp(url, "https://riftwii.github.io/apps/apps.json") == 0) {
        if (FILE* local = std::fopen("sd:/riftwii/test_apps.json", "rb")) {
            const std::size_t read = std::fread(buffer, 1, capacity, local);
            std::fclose(local);
            logf("Test build: the app list is sd:/riftwii/test_apps.json (%u bytes)\n", static_cast<unsigned>(read));
            *length = static_cast<uint32_t>(read);
            return 1;
        }
    }
#endif
    Block tls(TlsStream::memory_needed());
    if (tls.data == nullptr) {
        CopyError("not enough memory for a secure connection", error, error_bytes);
        return 0;
    }
    MemoryGet get;
    get.buffer = buffer;
    get.capacity = capacity;
    get.tls_block = tls.data;
    get.tls_bytes = TlsStream::memory_needed();
    std::string why;
    if (!HttpGetInMemory(url, get, why, 15000, nullptr, HttpRedirects::Follow)) {
        CopyError(why, error, error_bytes);
        return 0;
    }
    // The body to the buffer's start, where the plugin reads it.
    std::memmove(buffer, buffer + get.body_offset, get.body_length);
    *length = static_cast<uint32_t>(get.body_length);
    return 1;
}

int32_t HttpDownload(const char* url, const char* path, uint32_t max_bytes, uint8_t sha256[32],
                     void (*progress)(uint32_t done, void* arg), void* arg, char* error, uint32_t error_bytes) {
    if (url == nullptr || path == nullptr || sha256 == nullptr || max_bytes == 0) {
        CopyError("the download was not set up", error, error_bytes);
        return 0;
    }
    const HttpProgress report = [&](std::size_t received) {
        if (progress != nullptr) progress(static_cast<uint32_t>(received), arg);
    };
    // Never half a file: HttpDownloadToFile removes it on failure, and the
    // plugin checks for the whole one.
    FileDownload got;
    std::string why;
    if (!HttpDownloadToFile(url, path, max_bytes, got, why, 15000, report, HttpRedirects::Follow)) {
        CopyError(why, error, error_bytes);
        return 0;
    }
    std::memcpy(sha256, got.digest.data(), got.digest.size());
    return 1;
}

int32_t BackgroundStart(void (*job)(void* arg), void* arg) {
    if (job == nullptr || NetBackgroundBusy()) return -1;
    g_job = job;
    g_jobArg = arg;
    // Never on the menu's thread: the plugin's screen waits for it.
    if (!NetRunInBackground(RunJob, true, false)) return -1;
    return 0;
}

uint32_t BackgroundRunning() { return NetBackgroundBusy() ? 1 : 0; }

void BackgroundCancel() { NetCancelBackground(); }

int32_t RemovePath(const char* path) {
    if (path == nullptr) return -1;
    struct stat info;
    if (stat(path, &info) != 0) return -1;
    const int result = S_ISDIR(info.st_mode) ? rmdir(path) : unlink(path);
    return result == 0 ? 0 : -1;
}

int32_t RenamePath(const char* from, const char* to) {
    if (from == nullptr || to == nullptr) return -1;
    struct stat info;
    if (stat(to, &info) == 0) return -1;  // never over something
    return std::rename(from, to) == 0 ? 0 : -1;
}

int32_t ListDirectory(const char* path, void (*each)(const char* name, uint32_t is_directory, void* arg), void* arg) {
    if (path == nullptr || each == nullptr) return -1;
    DIR* dir = opendir(path);
    if (dir == nullptr) return -1;
    int32_t count = 0;
    while (struct dirent* entry = readdir(dir)) {
        if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) continue;
        each(entry->d_name, entry->d_type == DT_DIR ? 1u : 0u, arg);
        ++count;
    }
    closedir(dir);
    return count;
}

int32_t SdFreeKib(uint32_t* kib) {
    if (kib == nullptr) return -1;
    struct statvfs info{};
    if (statvfs("sd:/", &info) != 0) return -1;
    const unsigned long long unit = info.f_frsize != 0 ? info.f_frsize : info.f_bsize;
    const unsigned long long free_kib = static_cast<unsigned long long>(info.f_bavail) * unit / 1024u;
    *kib = free_kib > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(free_kib);
    return 0;
}

}  // namespace riftwii::wii::pluginhost
