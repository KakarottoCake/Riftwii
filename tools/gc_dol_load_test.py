# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile and run the GameCube loader's DOL reader with synthetic headers.

    python3 tools/gc_dol_load_test.py --cxx g++

The actual reader and its data types are compiled in isolation because the
rest of gcnative.cpp uses Wii hardware. No game files are needed.
"""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

struct ByteSource {
    std::uint8_t header[0x100] = {};
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const {
        if (offset == 0 && length == sizeof(header)) {
            std::memcpy(destination, header, length);
            return true;
        }
        if (offset != 0x100 || length > 0x40) return false;
        std::memset(destination, 0, length);
        return true;
    }
};

// LOADER_CODE

void put(ByteSource& disc, unsigned at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) disc.header[at + i] = value >> (24 - 8 * i);
}

bool load(std::uint32_t address, std::uint32_t size, std::uint32_t bss = 0, std::uint32_t bss_size = 0,
          bool data = false) {
    ByteSource disc;
    put(disc, 0, 0x100);
    put(disc, 0x48, data ? kGameFloor : address);
    put(disc, 0x90, data ? 4 : size);
    if (data) {
        put(disc, 7 * 4, 0x100);
        put(disc, 0x48 + 7 * 4, address);
        put(disc, 0x90 + 7 * 4, size);
    }
    put(disc, 0xD8, bss);
    put(disc, 0xDC, bss_size);
    put(disc, 0xE0, kGameFloor);
    Dol dol;
    std::string error;
    return read_dol(disc, 0, dol, error);
}

int main() {
    for (const bool data : {false, true}) {
        // A small section whose high address wraps used to pass validation,
        // and the launch would memcpy to it after disabling interrupts.
        assert(!load(0xFFFFFFE0, 0x40, 0, 0, data));
        assert(!load(kGameFloor - 4, 4, 0, 0, data));
        assert(!load(kLoaderStart - 0x20, 0x40, 0, 0, data));
        assert(!load(kLoaderStart, 4, 0, 0, data));
        assert(load(kGameFloor, 4, 0, 0, data));
        assert(load(kLoaderStart - 0x40, 0x40, 0, 0, data));
    }
    assert(!load(kGameFloor, 4, 0xFFFFFFE0, 0x40));
    assert(!load(kGameFloor, 4, 0, 4));
    assert(!load(kGameFloor, 4, kGameFloor - 4, 4));
    assert(!load(kGameFloor, 4, kLoaderStart - 0x20, 0x40));
    assert(load(kGameFloor, 4, kLoaderStart - 0x40, 0x40));
    assert(load(kGameFloor, 4, 0xFFFFFFFF, 0));
    std::puts("GameCube DOL load: section and BSS bounds reject wrapped addresses and preserve valid boundaries");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="g++")
    args = parser.parse_args()
    source = (ROOT / "plugins/gamecube/gcnative.cpp").read_text()
    constants = []
    for name in ("kGameFloor", "kLoaderStart"):
        constant = re.search(rf"constexpr std::uint32_t {name} = [^;]+;", source)
        if constant is None:
            raise RuntimeError(f"loader constant {name} not found")
        constants.append(constant.group())
    start = source.index("std::uint32_t be32(")
    end = source.index("std::string hex32(", start)
    code = "\n".join(constants) + "\n" + source[start:end]
    with tempfile.TemporaryDirectory(prefix="riftwii-gc-dol-") as directory:
        temporary = Path(directory)
        test = temporary / "dol.cpp"
        binary = temporary / "dol.exe"
        test.write_text(HARNESS.replace("// LOADER_CODE", code))
        subprocess.run([args.cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        str(test), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
