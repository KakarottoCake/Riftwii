// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The report link's QR code: every version and mask against segno's
// symbols (tests/qrcode_ref.inc), and the automatic choice.
#include "riftwii/qrcode.hpp"

#include <cstdio>
#include <iostream>
#include <string>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

#include "qrcode_ref.inc"

using riftwii::QrCode;
using riftwii::make_qr;

static std::string rows_of(const QrCode& q) {
    std::string s;
    for (int y = 0; y < q.size; ++y)
        for (int x = 0; x < q.size; ++x) s += q.at(x, y) ? '1' : '0';
    return s;
}

int main(int argc, char** argv) {
    for (const QrRef& ref : kQrRefs) {
        QrCode q;
        EXPECT_TRUE(make_qr(ref.text, q, ref.mask));
        EXPECT_EQ(q.version, ref.version);
        EXPECT_EQ(q.mask, ref.mask);
        if (rows_of(q) != ref.rows) {
            std::cerr << "FAILED: \"" << ref.text << "\" version " << ref.version << " mask " << ref.mask
                      << " differs from segno" << std::endl;
            g_failures++;
        }
    }
    // The automatic mask is one of the eight, and gives the same symbol
    // as forcing it.
    for (const char* text : {"hi", "https://paste.rs/AbC12"}) {
        QrCode automatic, forced;
        EXPECT_TRUE(make_qr(text, automatic));
        EXPECT_TRUE(automatic.mask >= 0 && automatic.mask < 8);
        EXPECT_TRUE(make_qr(text, forced, automatic.mask));
        EXPECT_TRUE(automatic.dark == forced.dark);
    }
    // Up to 78 bytes fit (version 4); more do not.
    QrCode big;
    EXPECT_TRUE(make_qr(std::string(78, 'a'), big));
    EXPECT_EQ(big.version, 4);
    EXPECT_TRUE(!make_qr(std::string(79, 'a'), big));

    // With a directory given, each version's automatic symbol as a PBM,
    // for a decoder to read back (a check outside the suite).
    if (argc > 2) {
        for (const QrRef& ref : kQrRefs) {
            if (ref.mask != 0) continue;
            QrCode q;
            make_qr(ref.text, q);
            const std::string path = std::string(argv[2]) + "/qr_v" + std::to_string(q.version) + ".pbm";
            if (FILE* f = std::fopen(path.c_str(), "w")) {
                const int border = 4, scale = 8, n = (q.size + 2 * border) * scale;
                std::fprintf(f, "P1\n%d %d\n", n, n);
                for (int y = 0; y < n; ++y) {
                    for (int x = 0; x < n; ++x) {
                        const int mx = x / scale - border, my = y / scale - border;
                        const bool on = mx >= 0 && my >= 0 && mx < q.size && my < q.size && q.at(mx, my);
                        std::fputs(on ? "1 " : "0 ", f);
                    }
                    std::fputs("\n", f);
                }
                std::fclose(f);
            }
        }
    }
    if (g_failures == 0) {
        std::cout << "ALL QR TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
