// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/zipread.hpp"

// A small zip writer for the tests: stored entries, or "deflated" ones whose
// deflate stream is one stored block (all a test needs from the inflater).
namespace ziptest {

using namespace riftwii;

inline void put16(std::vector<std::uint8_t>& v, unsigned x) { v.push_back(x & 0xFF); v.push_back((x >> 8) & 0xFF); }
inline void put32(std::vector<std::uint8_t>& v, std::uint32_t x) { put16(v, x & 0xFFFF); put16(v, x >> 16); }

struct TestFile {
    std::string name;
    std::string data;
    int method = 0;
    bool bad_crc = false;
};

inline std::vector<std::uint8_t> make_zip(const std::vector<TestFile>& files) {
    std::vector<std::uint8_t> out, directory;
    for (const TestFile& f : files) {
        std::vector<std::uint8_t> packed;
        if (f.method == 8) {
            packed.push_back(0x01);  // final stored block
            put16(packed, static_cast<unsigned>(f.data.size()));
            put16(packed, static_cast<unsigned>(~f.data.size()) & 0xFFFF);
        }
        packed.insert(packed.end(), f.data.begin(), f.data.end());
        std::uint32_t crc = zip_crc32_update(0, reinterpret_cast<const std::uint8_t*>(f.data.data()), f.data.size());
        if (f.bad_crc) crc ^= 1;
        const std::uint32_t offset = static_cast<std::uint32_t>(out.size());
        put32(out, 0x04034B50); put16(out, 20); put16(out, 0); put16(out, f.method); put16(out, 0); put16(out, 0);
        put32(out, crc); put32(out, static_cast<std::uint32_t>(packed.size())); put32(out, static_cast<std::uint32_t>(f.data.size()));
        put16(out, static_cast<unsigned>(f.name.size())); put16(out, 0);
        out.insert(out.end(), f.name.begin(), f.name.end());
        out.insert(out.end(), packed.begin(), packed.end());
        put32(directory, 0x02014B50); put16(directory, 20); put16(directory, 20); put16(directory, 0); put16(directory, f.method);
        put16(directory, 0); put16(directory, 0); put32(directory, crc); put32(directory, static_cast<std::uint32_t>(packed.size()));
        put32(directory, static_cast<std::uint32_t>(f.data.size())); put16(directory, static_cast<unsigned>(f.name.size()));
        put16(directory, 0); put16(directory, 0); put16(directory, 0); put16(directory, 0); put32(directory, 0); put32(directory, offset);
        directory.insert(directory.end(), f.name.begin(), f.name.end());
    }
    const std::uint32_t directory_offset = static_cast<std::uint32_t>(out.size());
    out.insert(out.end(), directory.begin(), directory.end());
    put32(out, 0x06054B50); put16(out, 0); put16(out, 0); put16(out, static_cast<unsigned>(files.size()));
    put16(out, static_cast<unsigned>(files.size())); put32(out, static_cast<std::uint32_t>(directory.size()));
    put32(out, directory_offset); put16(out, 0);
    return out;
}

inline bool stored_block_inflate(const std::uint8_t* in, std::size_t size, std::uint64_t expected, const ZipSink& sink) {
    if (size < 5 || in[0] != 0x01) return false;
    const std::size_t length = in[1] | (in[2] << 8);
    if (size != 5 + length || length != expected) return false;
    return sink(in + 5, length);
}

}  // namespace ziptest
