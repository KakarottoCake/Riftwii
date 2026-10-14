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

}  // namespace riftwii::wii
