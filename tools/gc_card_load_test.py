# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the GameCube loader's card-open block with failing host stdio.

    python3 tools/gc_card_load_test.py --cxx g++

Compile the actual block in isolation: the rest of gcnative.cpp needs Wii
hardware and libogc. Only its stdio and console dependencies are substituted.
No card image is created or modified by this test.
"""

import argparse
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

constexpr unsigned GC_CARD_BYTES = 0x200000;
struct Image {
    std::string id;
    bool shared_card = false;
};

bool can_open;
long file_size;
std::size_t read_bytes;
unsigned writes;
unsigned formats;
bool erased_header;

std::string gc_card_path(const std::string&, bool) { return "card.raw"; }
bool gc_card_shift_jis(const std::string&) { return false; }
void logf(const char*, ...) {}
void forget_sd_layout() {}
void format_card(std::vector<std::uint8_t>&, bool) { ++formats; }

std::FILE* test_open(const char*, const char* mode) {
    if (mode[0] == 'w') {
        ++writes;
        return reinterpret_cast<std::FILE*>(1);
    }
    return can_open ? reinterpret_cast<std::FILE*>(1) : nullptr;
}
int test_seek(std::FILE*, long, int) { return 0; }
long test_tell(std::FILE*) { return file_size; }
int test_close(std::FILE*) { return 0; }
std::size_t test_read(void* buffer, std::size_t, std::size_t count, std::FILE*) {
    const auto bytes = std::min(count, read_bytes);
    std::memset(buffer, erased_header ? 0xFF : 0x53, bytes);
    return bytes;
}
std::size_t test_write(const void*, std::size_t, std::size_t count, std::FILE*) {
    return count;
}

bool load() {
    Image image;
    std::vector<std::uint8_t> card_image;
    // LOADER_BLOCK
    return ready;
}

void check(bool opens, long size, std::size_t bytes, bool erased, bool should_format, bool ready) {
    can_open = opens;
    file_size = size;
    read_bytes = bytes;
    erased_header = erased;
    writes = 0;
    formats = 0;
    assert(load() == ready);
    assert(writes == unsigned(should_format));
    assert(formats == unsigned(should_format));
}

int main() {
    // Failed reads must never truncate an existing, correctly sized card.
    for (const auto bytes : {0u, 128u, 512u, GC_CARD_BYTES - 1}) {
        check(true, GC_CARD_BYTES, bytes, false, false, false);
        check(true, GC_CARD_BYTES, bytes, true, false, false);
    }
    check(true, GC_CARD_BYTES, GC_CARD_BYTES, false, false, true);
    check(true, GC_CARD_BYTES, GC_CARD_BYTES, true, true, true);
    check(true, 0, 0, false, true, true);
    check(false, -1, 0, false, true, true);
    check(true, 0x80000, 0, false, false, false);
    check(true, -1, 0, false, false, false);
    std::puts("GameCube card load: failed reads preserve the file; only new, empty or erased cards format");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="g++")
    args = parser.parse_args()
    source = (ROOT / "plugins/gamecube/gcnative.cpp").read_text()
    card_start = source.index("    std::vector<std::uint8_t> card_image;")
    start = source.index("        const std::string path = gc_card_path", card_start)
    end = source.index("        // The unlocked emulated card", start)
    block = source[start:end]
    for name in ("fopen", "fseek", "ftell", "fclose", "fread", "fwrite"):
        substitute = {"fopen": "open", "fseek": "seek", "ftell": "tell",
                      "fclose": "close", "fread": "read", "fwrite": "write"}[name]
        block = block.replace(f"std::{name}", f"test_{substitute}")
    with tempfile.TemporaryDirectory(prefix="riftwii-gc-card-") as directory:
        temporary = Path(directory)
        test = temporary / "card.cpp"
        binary = temporary / "card.exe"
        test.write_text(HARNESS.replace("// LOADER_BLOCK", block))
        subprocess.run([args.cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        str(test), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
