// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "textfile.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <cstdio>

#include "log.hpp"
#include "riftwii/checkedalloc.hpp"

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
    if (!try_resize(out, static_cast<std::size_t>(st.st_size))) {
        std::fclose(f);
        logf("%s is %lld bytes, more than there is memory for; left alone\n", path.c_str(),
             static_cast<long long>(st.st_size));
        return false;
    }
    const std::size_t got = out.empty() ? 0 : std::fread(&out[0], 1, out.size(), f);
    std::fclose(f);
    out.resize(got);
    return true;
}

namespace {

// ReadWholeFile for a string or a byte vector: both hold the file as it is.
// False also when there is no memory for the whole file (a pack's file
// sizes it, so it is checked).
template <typename Buffer>
bool read_whole(const std::string& path, Buffer& out) {
    using Byte = typename Buffer::value_type;
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
        if (!try_resize(out, static_cast<std::size_t>(size))) {
            std::fclose(f);
            logf("%s: no memory for its %ld bytes\n", path.c_str(), size);
            return false;
        }
        out.resize(std::fread(&out[0], 1, out.size(), f));
    }
    constexpr std::size_t kChunk = 4096;
    int next = std::fgetc(f);
    while (next != EOF) {
        // Grown twofold, as the library would, but checked.
        const std::size_t had = out.size() + 1;
        if (!try_reserve(out, std::max(had + kChunk, out.capacity() * 2))) {
            std::fclose(f);
            logf("%s: no memory for more than its first %u bytes\n", path.c_str(), static_cast<unsigned>(had - 1));
            return false;
        }
        out.push_back(static_cast<Byte>(next));
        out.resize(had + kChunk);
        out.resize(had + std::fread(&out[had], 1, kChunk, f));
        next = std::fgetc(f);
    }
    std::fclose(f);
    return true;
}

}  // namespace

bool ReadWholeFile(const std::string& path, std::string& out) { return read_whole(path, out); }

bool ReadWholeFile(const std::string& path, std::vector<std::uint8_t>& out) { return read_whole(path, out); }

bool WriteWholeFile(const std::string& path, const void* data, std::size_t size) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const std::size_t written = size == 0 ? 0 : std::fwrite(data, 1, size, f);
    const bool closed = std::fclose(f) == 0;
    return written == size && closed;
}

}  // namespace riftwii::wii
