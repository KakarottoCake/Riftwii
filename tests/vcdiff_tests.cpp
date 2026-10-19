// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// VCDIFF patches: one made by xdelta3 (tests/fixtures/vcdiff, from the
// files in vcdiff_data.inc), and small ones built here an instruction at a
// time, so each part of RFC 3284 the decoder follows (the default code
// table, the address modes, copies that run into their own output,
// windows that copy from earlier output) is checked on its own.
#include "riftwii/vcdiff.hpp"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;
using Bytes = std::vector<std::uint8_t>;

#include "vcdiff_data.inc"

static std::string g_dir;

static VcdiffReadFn reader(const Bytes& b) {
    return [&b](std::uint64_t at, void* out, std::size_t n) {
        if (at > b.size() || n > b.size() - at) return false;
        std::memcpy(out, b.data() + at, n);
        return true;
    };
}

static bool apply(const Bytes& patch, const Bytes& original, Bytes& out, std::string& error,
                  const VcdiffLimits& limits = {}) {
    out.clear();
    return apply_vcdiff(reader(patch), patch.size(), reader(original), original.size(),
                        [&out](const std::uint8_t* d, std::size_t n) {
                            out.insert(out.end(), d, d + n);
                            return true;
                        },
                        reader(out), {}, error, limits);
}

// --- Building patches -------------------------------------------------

static void put_int(Bytes& b, std::uint64_t v) {
    std::uint8_t digits[10];
    int n = 0;
    do {
        digits[n++] = std::uint8_t(v & 0x7F);
        v >>= 7;
    } while (v != 0);
    while (n > 1) b.push_back(digits[--n] | 0x80);
    b.push_back(digits[0]);
}

struct Window {
    std::uint8_t indicator = 0;  // 1: from the original, 2: from earlier output
    std::uint64_t seg_len = 0, seg_pos = 0;
    Bytes data, inst, addr;
    std::uint64_t length = 0;
    bool checksum = false;
    std::uint32_t adler = 0;
    void add(const Bytes& bytes) {
        inst.push_back(1);  // ADD, size follows
        put_int(inst, bytes.size());
        data.insert(data.end(), bytes.begin(), bytes.end());
        length += bytes.size();
    }
    void run(std::uint8_t byte, std::uint64_t size) {
        inst.push_back(0);  // RUN, size follows
        put_int(inst, size);
        data.push_back(byte);
        length += size;
    }
    // COPY with mode `mode`, size following; the address as `encoded`
    // (an integer, or for modes 6-8 one byte).
    void copy(std::uint8_t mode, std::uint64_t size, std::uint64_t encoded) {
        inst.push_back(std::uint8_t(19 + 16 * mode));
        put_int(inst, size);
        if (mode >= 6) addr.push_back(std::uint8_t(encoded));
        else put_int(addr, encoded);
        length += size;
    }
};

static Bytes patch_of(const std::vector<Window>& windows, std::uint8_t header = 0) {
    Bytes p = {0xD6, 0xC3, 0xC4, 0x00, header};
    for (const Window& w : windows) {
        p.push_back(std::uint8_t(w.indicator | (w.checksum ? 4 : 0)));
        if (w.indicator) {
            put_int(p, w.seg_len);
            put_int(p, w.seg_pos);
        }
        Bytes body;
        put_int(body, w.length);
        body.push_back(0);
        put_int(body, w.data.size());
        put_int(body, w.inst.size());
        put_int(body, w.addr.size());
        if (w.checksum)
            for (int s = 24; s >= 0; s -= 8) body.push_back(std::uint8_t(w.adler >> s));
        body.insert(body.end(), w.data.begin(), w.data.end());
        body.insert(body.end(), w.inst.begin(), w.inst.end());
        body.insert(body.end(), w.addr.begin(), w.addr.end());
        put_int(p, body.size());
        p.insert(p.end(), body.begin(), body.end());
    }
    return p;
}

static Bytes text(const char* s) { return Bytes(s, s + std::strlen(s)); }

// --- xdelta3's own patch ----------------------------------------------

static bool read_file(const std::string& path, Bytes& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    std::uint8_t buf[4096];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) != 0) out.insert(out.end(), buf, buf + n);
    std::fclose(f);
    return true;
}

static void test_xdelta3_fixture() {
    Bytes patch;
    EXPECT_TRUE(read_file(g_dir + "/vcdiff/xdelta3.vcdiff", patch));
    const Bytes original = vcdiff_original(), patched = vcdiff_patched();
    Bytes out;
    std::string error;
    EXPECT_TRUE(apply(patch, original, out, error));
    EXPECT_EQ(error, "");
    EXPECT_TRUE(out == patched);

    // The same through the block cache (every copy window counts as large).
    VcdiffLimits small;
    small.whole_copy_window = 0;
    small.cache_block = 512;
    small.cache_blocks = 3;
    EXPECT_TRUE(apply(patch, original, out, error, small));
    EXPECT_TRUE(out == patched);

    // The head, checked by the first window's Adler-32.
    Bytes head;
    bool checked = false;
    EXPECT_TRUE(vcdiff_head(reader(patch), patch.size(), reader(original), original.size(), 0x20, head, checked,
                            error));
    EXPECT_TRUE(checked);
    EXPECT_TRUE(head == Bytes(patched.begin(), patched.begin() + 0x20));

    // Another original: refused by the checksum, not patched into rubbish.
    Bytes other = original;
    other[300] ^= 0xFF;
    EXPECT_FALSE(apply(patch, other, out, error));
    EXPECT_TRUE(error.find("checksum") != std::string::npos);
    EXPECT_FALSE(vcdiff_head(reader(patch), patch.size(), reader(other), other.size(), 0x20, head, checked, error));

    // A patch cut short, at any point, fails.
    for (std::size_t cut : {std::size_t(3), std::size_t(20), patch.size() / 2, patch.size() - 1}) {
        Bytes part(patch.begin(), patch.begin() + static_cast<std::ptrdiff_t>(cut));
        EXPECT_FALSE(apply(part, original, out, error));
    }
}

// --- Built patches ----------------------------------------------------

static void test_add_run_and_self_copy() {
    // "ab" then a copy of it 9 bytes long: a copy that runs into its own
    // output repeats it (RFC 3284 section 3). Then a run of 5 'z'.
    Window w;
    w.add(text("ab"));
    w.copy(0, 9, 0);  // VCD_SELF, address 0 of U (no copy window)
    w.run('z', 5);
    Bytes out;
    std::string error;
    EXPECT_TRUE(apply(patch_of({w}), {}, out, error));
    EXPECT_TRUE(out == text("abababababazzzzz"));
}

static void test_address_modes() {
    const Bytes original = text("0123456789ABCDEFGHIJ");
    Window w;
    w.indicator = 1;
    w.seg_len = 20;
    w.seg_pos = 0;
    w.copy(0, 3, 5);         // SELF: 5 -> "567"      (here 20)
    w.copy(1, 2, 23 - 10);   // HERE: 23 - 13 = 10 -> "AB" (here 23)
    w.copy(2, 2, 2);         // near[0] (5) + 2 = 7 -> "78"
    w.copy(3, 2, 0);         // near[1] (10) + 0 -> "AB"
    // Same cache: address 10 sits at slot 10 % 768 = 10, mode 6, byte 10.
    w.copy(6, 3, 10);        // -> "ABC"
    w.copy(0, 2, 20);        // the first bytes this window made: "56"
    Bytes out;
    std::string error;
    EXPECT_TRUE(apply(patch_of({w}), original, out, error));
    EXPECT_EQ(std::string(out.begin(), out.end()), "567AB78ABABC56");
    EXPECT_EQ(error, "");
}

static void test_combined_codes() {
    // Code 179: ADD 2 then COPY 5 in mode 1 (163 + 12*1 + 3*(2-1) + (5-4)).
    // Code 248: COPY 4 in mode 1 then ADD 1.
    const Bytes original = text("abcdefghij");
    Window w;
    w.indicator = 1;
    w.seg_len = 10;
    w.inst = {179, 248};
    w.data = text("XYZ");
    put_int(w.addr, 12 - 0);   // HERE at 12 back to 0: "abcde"
    put_int(w.addr, 17 - 3);   // HERE at 17 back to 3: "defg"
    w.length = 2 + 5 + 4 + 1;
    Bytes out;
    std::string error;
    EXPECT_TRUE(apply(patch_of({w}), original, out, error));
    EXPECT_EQ(std::string(out.begin(), out.end()), "XYabcdedefgZ");
}

static void test_target_window() {
    // The second window copies from what the first made.
    Window a;
    a.add(text("hello "));
    Window b;
    b.indicator = 2;
    b.seg_len = 5;
    b.seg_pos = 0;
    b.copy(0, 5, 0);
    b.add(text("!"));
    Bytes out;
    std::string error;
    EXPECT_TRUE(apply(patch_of({a, b}), {}, out, error));
    EXPECT_EQ(std::string(out.begin(), out.end()), "hello hello!");
}

static void test_checksums_and_refusals() {
    Window w;
    w.add(text("data"));
    w.checksum = true;
    const Bytes four = text("data");
    w.adler = vcdiff_adler32(four.data(), four.size());
    Bytes out;
    std::string error;
    EXPECT_TRUE(apply(patch_of({w}), {}, out, error));
    w.adler ^= 1;
    EXPECT_FALSE(apply(patch_of({w}), {}, out, error));
    w.adler ^= 1;
    // Secondary compression and a code table of its own: refused.
    EXPECT_FALSE(apply(patch_of({w}, 0x01), {}, out, error));
    EXPECT_TRUE(error.find("compressed") != std::string::npos);
    EXPECT_FALSE(apply(patch_of({w}, 0x02), {}, out, error));
    // A copy from beyond the original's end.
    Window far;
    far.indicator = 1;
    far.seg_len = 10;
    far.seg_pos = 5;
    far.copy(0, 2, 0);
    EXPECT_FALSE(apply(patch_of({far}), text("short"), out, error));
    // A copy from bytes not made yet.
    Window ahead;
    ahead.add(text("x"));
    ahead.copy(0, 1, 1);
    EXPECT_FALSE(apply(patch_of({ahead}), {}, out, error));
    // Not a patch at all.
    EXPECT_FALSE(apply(text("PK\x03\x04 not vcdiff"), {}, out, error));
    // A short patch whose window claims sections far larger than the file:
    // refused before anything is allocated (BUG-02). The limits are raised
    // so only the file's own size can stop it; allocating 256 GiB first
    // would throw instead of returning.
    {
        Bytes huge = {0xD6, 0xC3, 0xC4, 0x00, 0x00, 0x00};
        Bytes body;
        put_int(body, 4);           // window length
        body.push_back(0);          // delta indicator
        put_int(body, 1ull << 38);  // data section: 256 GiB claimed
        put_int(body, 2);
        put_int(body, 0);
        put_int(huge, body.size() + 6);
        huge.insert(huge.end(), body.begin(), body.end());
        VcdiffLimits big;
        big.max_window = 1ull << 40;
        bool threw = false;
        bool ok = true;
        try {
            ok = apply(huge, {}, out, error, big);
        } catch (...) {
            threw = true;
        }
        EXPECT_FALSE(threw);
        EXPECT_FALSE(ok);
        EXPECT_TRUE(error.find("past the file's end") != std::string::npos);
    }
    // A window whose delta length disagrees with its contents (MIN-08).
    {
        Window ok_window;
        ok_window.add(text("data"));
        Bytes bad = patch_of({ok_window});
        // The delta length is the byte after the window indicator; one more
        // than the truth, with a spare byte after so nothing runs short.
        bad[6] = std::uint8_t(bad[6] + 1);
        bad.push_back(0);
        EXPECT_FALSE(apply(bad, {}, out, error));
        EXPECT_TRUE(error.find("length does not match") != std::string::npos);
    }
    // Adler-32 as zlib defines it: "Wikipedia" is 0x11E60398.
    const Bytes wiki = text("Wikipedia");
    EXPECT_EQ(vcdiff_adler32(wiki.data(), wiki.size()), 0x11E60398u);
}

int main(int argc, char** argv) {
    g_dir = argc > 1 ? argv[1] : "tests/fixtures";
    test_xdelta3_fixture();
    test_add_run_and_self_copy();
    test_address_modes();
    test_combined_codes();
    test_target_window();
    test_checksums_and_refusals();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "vcdiff: all tests passed" << std::endl;
    return 0;
}
