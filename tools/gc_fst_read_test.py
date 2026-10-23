# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile both actual GameCube FST allocation blocks with synthetic headers.

Run with python3 tools/gc_fst_read_test.py --cxx g++. The vector stand-in
records constructor sizes without allocating disc-controlled amounts of RAM.
The surrounding launch functions depend on Wii hardware, so only their FST
read blocks are compiled, with successful reads and no pack or TGC override.
"""

import argparse
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

std::vector<std::size_t> allocations;
unsigned reads = 0;
unsigned logs = 0;

struct TrackedBytes {
    std::size_t length = 0;
    std::uint8_t byte = 0;
    explicit TrackedBytes(std::size_t size = 0) : length(size) {
        allocations.push_back(size);
    }
    bool empty() const { return length == 0; }
    std::size_t size() const { return length; }
    std::uint8_t* data() { return &byte; }
};

struct Disc {
    bool read(std::uint64_t, std::uint8_t*, std::size_t) const {
        ++reads;
        return true;
    }
};

bool read_at(const Disc& disc, std::uint64_t offset, std::uint8_t* data, std::size_t length) {
    return disc.read(offset, data, length);
}

void logf(const char*, ...) { ++logs; }

std::uint32_t be32(const std::uint8_t* bytes) {
    return (std::uint32_t(bytes[0]) << 24) | (std::uint32_t(bytes[1]) << 16) |
           (std::uint32_t(bytes[2]) << 8) | bytes[3];
}

bool plugin_read(std::array<std::uint8_t, 0x440>& boot, std::string& error) {
    Disc f;
    bool ok = true;
    struct Pack { TrackedBytes fst; };
    const Pack* pack = nullptr;
    const std::uint32_t tgc_offset = 0;
    TrackedBytes tgc_fst;
    allocations.clear();
    // PLUGIN_CODE
    (void)fst_max;
    return ok;
}

bool menu_read(std::array<std::uint8_t, 0x440>& boot, std::string& error) {
    const Disc iso;
    // MENU_CODE
    return true;
}

int main() {
    unsigned failures = 0;
    for (auto read : {plugin_read, menu_read}) {
        for (const std::uint32_t size : {0u, 0x01800001u, 0x7fffffffu, 0xffffffffu,
                                         12u, 0x10000u, 0x017fffffu, 0x01800000u}) {
            std::array<std::uint8_t, 0x440> boot{};
            for (unsigned i = 0; i < 4; ++i) boot[0x428 + i] = size >> (24 - 8 * i);
            allocations.clear();
            reads = 0;
            logs = 0;
            std::string error;
            const bool accepted = read(boot, error);
            const bool valid = size != 0 && size <= 0x01800000;
            const auto check = [&](bool condition, const char* requirement) {
                if (!condition) {
                    std::printf("%s, size 0x%08x: %s\n",
                                read == plugin_read ? "plugin" : "menu", size, requirement);
                    ++failures;
                }
            };
            if (!valid) {
                check(!accepted, "invalid size must be refused");
                check(allocations.empty(), "invalid size must not reach the vector constructor");
                check(reads == 0, "invalid size must not reach the disc reader");
                check(logs == 1, "refusal must be logged");
                check(error.find("FST") != std::string::npos, "error must identify the FST");
                check(error.find("MEM1") != std::string::npos, "error must identify the MEM1 limit");
            } else {
                check(accepted, "valid size must be accepted");
                check(allocations.size() == 1 && allocations.front() == size, "allocation must match the valid size");
                check(reads == 1, "valid FST must be read once");
                check(logs == 0, "valid size must not log a refusal");
            }
        }
    }
    if (failures != 0) return 1;
    std::puts("Both GameCube FST readers reject zero and oversized sizes before allocation or reading");
}
'''


def read_block(path, start_marker, end_marker):
    source = (ROOT / path).read_text()
    start = source.index(start_marker)
    end = source.index(end_marker, start)
    return source[start:end].replace("std::vector<std::uint8_t>", "TrackedBytes")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="g++")
    args = parser.parse_args()
    plugin = read_block("plugins/gamecube/gcnative.cpp",
                        "    const std::uint32_t fst_size = be32(&boot[0x428]);",
                        "    if (!ok) {")
    menu_source = (ROOT / "wii/launch/gclaunch.cpp").read_text()
    menu_start = "    const std::uint32_t fst_size = be32(&boot[0x428]);"
    if menu_start not in menu_source:
        menu_start = "    std::vector<std::uint8_t> fst_bytes(be32(&boot[0x428]));"
    menu = read_block("wii/launch/gclaunch.cpp", menu_start, "    Fst fst;")
    harness = HARNESS.replace("// PLUGIN_CODE", plugin).replace("// MENU_CODE", menu)
    with tempfile.TemporaryDirectory(prefix="riftwii-gc-fst-") as directory:
        temporary = Path(directory)
        test = temporary / "fst.cpp"
        binary = temporary / "fst.exe"
        test.write_text(harness)
        subprocess.run([args.cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        str(test), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
