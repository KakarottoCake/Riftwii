// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Test tool only: record the HBC argument block, then use libogc's normal
// exit path so the loader's return stub is exercised too.
#include <fat.h>
#include <stdio.h>

int main(int argc, char** argv) {
    if (!fatInitDefault()) return 1;
    FILE* output = fopen("sd:/argv-test.txt", "wb");
    if (!output) return 2;
    fprintf(output, "argc=%d\n", argc);
    for (int index = 0; index < argc; ++index) {
        fprintf(output, "argv[%d]=<%s>\n", index, argv[index] ? argv[index] : "(null)");
    }
    const int failed = fclose(output);
    fatUnmount("sd:");
    return failed != 0 ? 3 : 0;
}
