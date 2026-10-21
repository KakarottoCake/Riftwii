// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "textfile.hpp"

#include <sys/stat.h>

#include <cstdio>

#include "log.hpp"

namespace riftwii::wii {

bool ReadTextFile(const std::string& path, std::string& out, std::size_t cap) {
    out.clear();
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    if (st.st_size < 0 || static_cast<unsigned long long>(st.st_size) > cap) {
        logf("%s is %lld bytes, more than the %u RiftWii reads; left alone\n", path.c_str(),
             static_cast<long long>(st.st_size), static_cast<unsigned>(cap));
        return false;
    }
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.resize(static_cast<std::size_t>(st.st_size));
    const std::size_t got = out.empty() ? 0 : std::fread(&out[0], 1, out.size(), f);
    std::fclose(f);
    out.resize(got);
    return true;
}

bool ReadWholeFile(const std::string& path, std::string& out) {
    out.clear();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    // The whole file in one read when its size can be told, so a large
    // pack XML is not copied about as the string grows; then chunks until
    // the end, for a file whose size was not known (or that grew).
    long size = -1;
    if (std::fseek(f, 0, SEEK_END) == 0) size = std::ftell(f);
    std::rewind(f);
    if (size > 0) {
        out.resize(static_cast<std::size_t>(size));
        out.resize(std::fread(&out[0], 1, out.size(), f));
    }
    constexpr std::size_t kChunk = 4096;
    int next = std::fgetc(f);
    while (next != EOF) {
        out.push_back(static_cast<char>(next));
        const std::size_t had = out.size();
        out.resize(had + kChunk);
        out.resize(had + std::fread(&out[had], 1, kChunk, f));
        next = std::fgetc(f);
    }
    std::fclose(f);
    return true;
}

bool WriteWholeFile(const std::string& path, const void* data, std::size_t size) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const std::size_t written = size == 0 ? 0 : std::fwrite(data, 1, size, f);
    const bool closed = std::fclose(f) == 0;
    return written == size && closed;
}

}  // namespace riftwii::wii
