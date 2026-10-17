// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The original and patched files behind tests/fixtures/vcdiff/xdelta3.vcdiff
// (made with `xdelta3 -e -S none -W 16384 -B 16384 -s original patched`).
// Pseudo-random bytes, so the patch has to work for its matches: the
// patched file keeps stretches of the original, moves some, repeats a
// pattern, inserts new bytes and ends with zeros.
static std::vector<std::uint8_t> vcdiff_original() {
    std::vector<std::uint8_t> b(40000);
    std::uint32_t x = 12345;
    for (auto& v : b) {
        x = x * 1103515245u + 12345u;
        v = std::uint8_t(x >> 16);
    }
    return b;
}
static std::vector<std::uint8_t> vcdiff_patched() {
    const std::vector<std::uint8_t> a = vcdiff_original();
    std::vector<std::uint8_t> b;
    b.insert(b.end(), a.begin() + 200, a.begin() + 8200);     // moved
    for (int i = 0; i < 600; ++i) b.push_back(std::uint8_t(i * 7));  // new
    b.insert(b.end(), a.begin(), a.begin() + 180);              // the start, later
    for (int i = 0; i < 50; ++i) b.insert(b.end(), {1, 2, 3, 4, 5, 6, 7});  // a pattern
    b.insert(b.end(), a.begin() + 24000, a.begin() + 39800);
    b.insert(b.end(), 1000, 0);                                  // zeros
    b.insert(b.end(), a.begin() + 10000, a.begin() + 14000);    // copied twice
    b.insert(b.end(), a.begin() + 10000, a.begin() + 14000);
    return b;
}
