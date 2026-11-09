// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Triforce arcade boards (runtime/gc/gc_tri.c): the baseboard's SI packets
// (one late, their checksum), the JVS I/O board's frames (escaping,
// checksums, reset and addressing, inputs, coins, unknown commands), the
// steering wheel's line, the media board's status words, the backup
// memory and registers; and the finder of the arcade SDK functions
// (riftwii/gctriforce.hpp) on made-up code laid out as the SDK's.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "gc_tri.h"
#include "riftwii/gctriforce.hpp"

namespace {

int failures = 0;

#define EXPECT_EQ(a, b)                                                                                      \
    do {                                                                                                     \
        const unsigned expect_a = static_cast<unsigned>(a);                                                  \
        const unsigned expect_b = static_cast<unsigned>(b);                                                  \
        if (expect_a != expect_b) {                                                                          \
            std::printf("FAILED: %s == %s (0x%x, 0x%x) at line %d\n", #a, #b, expect_a, expect_b, __LINE__); \
            ++failures;                                                                                      \
        }                                                                                                    \
    } while (0)

gc_tri fresh() {
    static gc_tri t;  // too big for comfort on the stack
    std::memset(&t, 0, sizeof(t));
    riftwii::gc_tri_identity(t, 'J');
    return t;
}

// A JVS frame as the arcade SDK sends it: node, length, data, checksum,
// escaped behind 0xE0.
std::vector<std::uint8_t> jvs_request(std::uint8_t node, const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> raw{node, std::uint8_t(data.size() + 1)};
    raw.insert(raw.end(), data.begin(), data.end());
    std::uint8_t sum = 0;
    for (std::uint8_t b : raw) sum = std::uint8_t(sum + b);
    raw.push_back(sum);
    std::vector<std::uint8_t> out{0xE0};
    for (std::uint8_t b : raw) {
        if (b == 0xE0 || b == 0xD0) {
            out.push_back(0xD0);
            out.push_back(std::uint8_t(b - 1));
        } else {
            out.push_back(b);
        }
    }
    return out;
}

// A 0x70 packet of baseboard commands ([command, length, data] each).
std::vector<std::uint8_t> packet(const std::vector<std::vector<std::uint8_t>>& commands) {
    std::vector<std::uint8_t> p(128, 0);
    p[0] = 0x70;
    std::size_t at = 2;
    for (const auto& c : commands)
        for (std::uint8_t b : c) p[at++] = b;
    p[1] = std::uint8_t(at - 2);
    return p;
}

std::vector<std::uint8_t> jvs_command(std::uint8_t node, const std::vector<std::uint8_t>& data) {
    const auto frame = jvs_request(node, data);
    std::vector<std::uint8_t> c{std::uint8_t(0x40 | (node & 0x1F)), std::uint8_t(frame.size())};
    c.insert(c.end(), frame.begin(), frame.end());
    return c;
}

// Sends `p`, then an empty packet, and returns the answer to `p`.
std::vector<std::uint8_t> exchange(gc_tri& t, const std::vector<std::uint8_t>& p) {
    std::vector<std::uint8_t> in(128, 0);
    gc_tri_si(&t, p.data(), 128, in.data(), 128);
    const auto empty = packet({});
    EXPECT_EQ(gc_tri_si(&t, empty.data(), 128, in.data(), 128), 128);
    return in;
}

bool checksum_ok(const std::vector<std::uint8_t>& r) {
    std::uint8_t sum = 0;
    for (std::uint8_t b : r) sum = std::uint8_t(sum + b);
    return sum == 0xFF && r[0] == 0x01;
}

// The answer to command `cmd` in a response packet; empty when none.
std::vector<std::uint8_t> answer(const std::vector<std::uint8_t>& r, std::uint8_t cmd) {
    for (std::size_t at = 2; at < 2u + r[1];) {
        const std::uint8_t c = r[at], n = r[at + 1];
        if (c == cmd) return std::vector<std::uint8_t>(r.begin() + at + 2, r.begin() + at + 2 + n);
        at += 2u + n;
    }
    return {};
}

// A JVS answer unescaped (after its 0xE0): node, length, status, reports, sum.
std::vector<std::uint8_t> unescape(const std::vector<std::uint8_t>& escaped) {
    std::vector<std::uint8_t> out;
    if (escaped.empty() || escaped[0] != 0xE0) return out;
    for (std::size_t i = 1; i < escaped.size(); ++i) {
        if (escaped[i] == 0xD0) out.push_back(std::uint8_t(escaped[++i] + 1));
        else out.push_back(escaped[i]);
    }
    return out;
}

bool jvs_sum_ok(const std::vector<std::uint8_t>& f) {
    if (f.size() < 3 || f.size() != 2u + f[1]) return false;
    std::uint8_t sum = 0;
    for (std::size_t i = 0; i + 1 < f.size(); ++i) sum = std::uint8_t(sum + f[i]);
    return sum == f.back();
}

void test_type_and_lateness() {
    gc_tri t = fresh();
    std::uint8_t in[128] = {};
    const std::uint8_t status = 0x00;
    EXPECT_EQ(gc_tri_si(&t, &status, 1, in, 3), 3);
    EXPECT_EQ(in[0], 0x10);
    EXPECT_EQ(in[1], 0x11);
    EXPECT_EQ(in[2], 0x09);
    // The first packet gets no answer; the second gets the first's.
    const auto p = packet({{0x15, 0x00}, {0x16, 0x00}, {0x11, 0x00}});
    EXPECT_EQ(gc_tri_si(&t, p.data(), 128, in, 128), 0);
    const auto r = exchange(t, packet({{0x10, 0x00}}));
    // `exchange`'s first send answered `p`; check the one it returned is the 0x10 one.
    EXPECT_EQ(checksum_ok(r), 1);
    EXPECT_EQ(answer(r, 0x10).size(), 2);
    EXPECT_EQ(answer(r, 0x10)[1] & 0x10, 0x10);  // the arcade SDK waits for this bit
    // Test and service up, DIP switch 1 on: Mario Kart skips its card
    // reader and camera check.
    EXPECT_EQ(answer(r, 0x10)[0], 0xFE);
    // Unknown one-byte commands get no answer.
    const std::uint8_t poll[3] = {0x40, 0x03, 0x00};
    EXPECT_EQ(gc_tri_si(&t, poll, 3, in, 8), 0);
    EXPECT_EQ(t.si_unknown >> 24, 0x40);
}

void test_board_identity() {
    gc_tri t = fresh();
    const auto r = exchange(t, packet({{0x15, 0x00}, {0x16, 0x00}, {0x11, 0x00}, {0x1F, 0x00, 1, 2, 3, 4}}));
    EXPECT_EQ(checksum_ok(r), 1);
    EXPECT_EQ(answer(r, 0x15)[1], 0x26);
    EXPECT_EQ(answer(r, 0x16)[0], 0x07);
    EXPECT_EQ(answer(r, 0x11).size(), 16);
    const auto region = answer(r, 0x1F);
    EXPECT_EQ(region.size(), 20);
    EXPECT_EQ(region[4], 1);  // Japan
    EXPECT_EQ(region[5], 0xFE);
}

void test_jvs_reset_and_address() {
    gc_tri t = fresh();
    t.status_switches[1] = 0xFE;
    // Reset: no answer, the sense line up.
    auto r = exchange(t, packet({jvs_command(0xFF, {0xF0, 0xD9}), {0x10, 0x00}}));
    EXPECT_EQ(answer(r, 0x5F).size(), 0);
    EXPECT_EQ(answer(r, 0x40).size(), 0);
    EXPECT_EQ(t.status_switches[1] & 1, 1);
    // Set address 1: answered (status OK, report OK), the sense line down.
    // The SDK sends a broadcast as node 0's frame (command 0x40).
    auto frame = jvs_request(0xFF, {0xF1, 0x01});
    std::vector<std::uint8_t> c{0x40, std::uint8_t(frame.size())};
    c.insert(c.end(), frame.begin(), frame.end());
    r = exchange(t, packet({c}));
    const auto f = unescape(answer(r, 0x40));
    EXPECT_EQ(jvs_sum_ok(f), 1);
    EXPECT_EQ(f.size(), 5);
    EXPECT_EQ(f[0], 0x00);
    EXPECT_EQ(f[2], 1);
    EXPECT_EQ(f[3], 1);
    EXPECT_EQ(t.jvs_address, 1);
    EXPECT_EQ(t.status_switches[1] & 1, 0);
}

void test_jvs_inputs() {
    gc_tri t = fresh();
    t.jvs_address = 1;
    // Start and B held, the stick right, R half way; A pressed twice (two coins).
    std::uint8_t pad[8] = {0x10 | 0x02, 0x00, 0xE0, 0x80, 0x80, 0x80, 0x00, 0x80};
    gc_tri_set_pad(&t, pad, 1);
    pad[0] |= 0x01;
    gc_tri_set_pad(&t, pad, 1);
    gc_tri_set_pad(&t, pad, 1);  // still held: no second coin
    pad[0] &= std::uint8_t(~0x01);
    gc_tri_set_pad(&t, pad, 1);
    pad[0] |= 0x01;
    gc_tri_set_pad(&t, pad, 1);
    EXPECT_EQ(t.coins[0], 2);
    const auto r = exchange(t, packet({jvs_command(1, {0x10, 0x14, 0x20, 1, 2, 0x21, 2, 0x22, 3})}));
    EXPECT_EQ(checksum_ok(r), 1);
    const auto f = unescape(answer(r, 0x41));
    EXPECT_EQ(jvs_sum_ok(f), 1);
    EXPECT_EQ(f[2], 1);  // status OK
    std::size_t at = 3;
    EXPECT_EQ(f[at++], 1);  // identify
    const std::size_t id_len = std::strlen(reinterpret_cast<const char*>(&f[at]));
    EXPECT_EQ(std::memcmp(&f[at], "namco ltd.;FCA-1", 16), 0);
    at += id_len + 1;
    EXPECT_EQ(f[at++], 1);  // features
    EXPECT_EQ(f[at], 0x01);
    EXPECT_EQ(f[at + 2], 16);
    at += t.io_features_len;
    EXPECT_EQ(f[at++], 1);  // switches: system, then the player's two bytes
    EXPECT_EQ(f[at++], 0);
    EXPECT_EQ(f[at++], 0x80);  // start
    EXPECT_EQ(f[at++], 0x20);  // B: item
    EXPECT_EQ(f[at++], 1);     // coins
    EXPECT_EQ(f[at++], 0);
    EXPECT_EQ(f[at++], 2);
    EXPECT_EQ(f[at++], 0);
    EXPECT_EQ(f[at++], 0);
    EXPECT_EQ(f[at++], 1);     // analog: wheel 0xE0E0 (escaped on the line), gas 0x8080, brake 0
    EXPECT_EQ(f[at++], 0xE0);
    EXPECT_EQ(f[at++], 0xE0);
    EXPECT_EQ(f[at++], 0x80);
    EXPECT_EQ(f[at++], 0x80);
    EXPECT_EQ(f[at++], 0);
    EXPECT_EQ(f[at++], 0);
    EXPECT_EQ(at + 1, f.size());
    // The escaped answer carries no bare 0xE0 after its first byte.
    const auto escaped = answer(r, 0x41);
    for (std::size_t i = 1; i < escaped.size(); ++i) EXPECT_EQ(escaped[i] == 0xE0, 0);
    // A coin taken off by the game.
    const auto r2 = exchange(t, packet({jvs_command(1, {0x30, 1, 0, 1})}));
    EXPECT_EQ(unescape(answer(r2, 0x41))[3], 1);
    EXPECT_EQ(t.coins[0], 1);
}

void test_jvs_errors() {
    gc_tri t = fresh();
    t.jvs_address = 1;
    auto r = exchange(t, packet({jvs_command(1, {0x11, 0x5A})}));
    auto f = unescape(answer(r, 0x41));
    EXPECT_EQ(jvs_sum_ok(f), 1);
    EXPECT_EQ(f[2], 2);  // unknown command
    EXPECT_EQ(t.jvs_errors >> 24, 0x5A);
    // A broken checksum.
    auto frame = jvs_request(1, {0x11});
    frame[frame.size() - 1] ^= 0x55;
    std::vector<std::uint8_t> c{0x41, std::uint8_t(frame.size())};
    c.insert(c.end(), frame.begin(), frame.end());
    r = exchange(t, packet({c}));
    f = unescape(answer(r, 0x41));
    EXPECT_EQ(f[2], 3);
    // Another node's frame: no answer.
    r = exchange(t, packet({jvs_command(2, {0x11})}));
    EXPECT_EQ(answer(r, 0x42).size(), 0);
}

// The output and odd commands take their request layouts whole: one frame
// of each answers OK (with data where they have some), and a request cut
// short ends the frame with report 2 (parameter size).
void test_jvs_outputs() {
    gc_tri t = fresh();
    t.jvs_address = 1;
    t.wheel_state = 2;
    auto r = exchange(t, packet({jvs_command(1, {0x25, 1, 0x2E, 1, 0x32, 1, 0x80, 0x34, 2, 'A', 'B', 0x33, 1, 0, 0,
                                                0x37, 1, 0x10, 0x38, 1, 1, 0x70, 0x18, 1, 2, 3, 4})}));
    auto f = unescape(answer(r, 0x41));
    EXPECT_EQ(jvs_sum_ok(f), 1);
    EXPECT_EQ(f[2], 1);  // status OK
    const std::vector<std::uint8_t> reports{1, 0, 0, 0, 0,  // screen position: zeros
                                            1, 0, 0, 0, 0,  // remaining payout: zeros
                                            1, 1, 1, 1, 1,  // lamps, display, analog out, byte, bit
                                            1, 0xFF};        // Namco's 0x70
    EXPECT_EQ(f.size(), 3u + reports.size() + 1u);
    for (std::size_t i = 0; i < reports.size() && 3u + i < f.size(); ++i) EXPECT_EQ(f[3 + i], reports[i]);
    EXPECT_EQ(t.wheel_state, 0);  // lamp bit 0x80: the wheel's reset
    // Cut short: lamps that say three bytes but carry one.
    r = exchange(t, packet({jvs_command(1, {0x32, 3, 0x01})}));
    f = unescape(answer(r, 0x41));
    EXPECT_EQ(f[2], 1);
    EXPECT_EQ(f[3], 2);
    // Namco's 0x70 without its 0x18.
    r = exchange(t, packet({jvs_command(1, {0x70, 0x17, 1, 2, 3, 4})}));
    f = unescape(answer(r, 0x41));
    EXPECT_EQ(f[3], 2);
    // A coin counter going up stops at 0x3FFF.
    t.coins[0] = 0x3FF0;
    r = exchange(t, packet({jvs_command(1, {0x35, 1, 0x01, 0x00})}));
    EXPECT_EQ(unescape(answer(r, 0x41))[3], 1);
    EXPECT_EQ(t.coins[0], 0x3FFF);
}

void test_wheel() {
    gc_tri t = fresh();
    const std::vector<std::uint8_t> request{0x31, 10, 0xFF, 0xFF, 0xFF, 0x01, 0, 0, 0, 0, 0, 0};
    auto r = exchange(t, packet({request}));
    auto a = answer(r, 0x31);
    EXPECT_EQ(a.size(), 3);
    EXPECT_EQ(a[0], 'E');
    r = exchange(t, packet({request}));
    a = answer(r, 0x31);
    EXPECT_EQ(a.size(), 3);
    EXPECT_EQ(a[0], 'C');
    EXPECT_EQ(a[2], '6');
}

void test_board_and_backup() {
    gc_tri t = fresh();
    EXPECT_EQ(gc_tri_is_board(0x80000140), 1);
    EXPECT_EQ(gc_tri_is_board(0x1F000000), 1);
    EXPECT_EQ(gc_tri_is_board(0x1E000000), 0);
    EXPECT_EQ(gc_tri_is_board(0x00000440), 0);
    std::uint8_t buf[32];
    gc_tri_board_read(&t, 0x80000140, buf, 32);
    EXPECT_EQ(buf[0], 1);
    gc_tri_board_read(&t, 0x80000040, buf, 32);
    EXPECT_EQ(std::memcmp(buf + 4, "GCAM", 4), 0);
    gc_tri_board_read(&t, 0x84000020, buf, 32);
    EXPECT_EQ(buf[2] | buf[3], 0);  // the mailbox reads idle
    const std::uint8_t data[4] = {1, 2, 3, 4};
    gc_tri_backup_write(&t, GC_TRI_BACKUP_BYTES - 2, data, 4);
    std::uint8_t back[4] = {};
    gc_tri_backup_read(&t, GC_TRI_BACKUP_BYTES - 2, back, 4);
    EXPECT_EQ(std::memcmp(back, data, 4), 0);
    EXPECT_EQ(t.backup[1], 4);  // wrapped
    // The media board's mailbox: a request, the doorbell, the answer with
    // its command | 0x80, the interrupt status set; the SDK then clears it.
    std::uint8_t request[32] = {7, 0, 0x01, 0x01};
    gc_tri_board_write(&t, GC_TRI_MEDIA_REQUEST, request, 32);
    EXPECT_EQ(t.irq_raise, 0);
    const std::uint8_t poke[32] = {1};
    gc_tri_board_write(&t, GC_TRI_MEDIA_DOORBELL, poke, 32);
    EXPECT_EQ(t.irq_raise, 1);
    EXPECT_EQ(t.irq_status & 0x04, 0x04);
    gc_tri_board_read(&t, GC_TRI_MEDIA_ANSWER, buf, 32);
    EXPECT_EQ(buf[0], 7);
    EXPECT_EQ(buf[2], 0x01);
    EXPECT_EQ(buf[3], 0x81);
    EXPECT_EQ(buf[4] != 0 && buf[5] != 0, 1);  // a version with both bytes set
    gc_tri_board_read(&t, GC_TRI_MEDIA_REQUEST, buf, 32);
    EXPECT_EQ(buf[2] | buf[3], 0);  // the request box reads idle
    const std::uint8_t zeros[32] = {};
    gc_tri_board_write(&t, GC_TRI_MEDIA_ANSWER, zeros, 32);
    gc_tri_board_read(&t, GC_TRI_MEDIA_ANSWER, buf, 32);
    EXPECT_EQ(buf[3], 0);
    // The network board's: its doorbell is a read; the link-up answer is 0x180.
    std::uint8_t link[32] = {9, 0, 0x01, 0x00, 0, 0, 0, 1};
    gc_tri_board_write(&t, GC_TRI_NET_REQUEST, link, 32);
    gc_tri_board_read(&t, GC_TRI_NET_DOORBELL, buf, 32);
    EXPECT_EQ(t.irq_status & 0x10, 0x10);
    gc_tri_board_read(&t, GC_TRI_NET_ANSWER, buf, 32);
    EXPECT_EQ(buf[2], 0x01);
    EXPECT_EQ(buf[3], 0x80);
    EXPECT_EQ(buf[7], 192);
    gc_tri_exi_register(&t, GC_TRI_EXI_CMD_WRITE_ISR, 0x14);
    gc_tri_exi_register(&t, GC_TRI_EXI_CMD_WRITE_LANCNT, 0);
    EXPECT_EQ(gc_tri_exi_register(&t, GC_TRI_EXI_CMD_READ_ISR, 0), 0x0402);
    gc_tri_exi_register(&t, GC_TRI_EXI_CMD_WRITE_ISR, 0x02);
    EXPECT_EQ(gc_tri_exi_register(&t, GC_TRI_EXI_CMD_READ_ISR, 0), 0x0400);
    EXPECT_EQ(gc_tri_exi_register(&t, GC_TRI_EXI_CMD_READ_IMR, 0), 0xFF81);
}

// ---- The finder, on made-up code laid out as the arcade SDK's ----------

struct Asm {
    std::uint32_t base = 0x80100000;
    std::vector<std::uint32_t> words;
    std::uint32_t here() const { return base + 4 * std::uint32_t(words.size()); }
    void w(std::uint32_t v) { words.push_back(v); }
    void bl(std::uint32_t to) { w(0x48000001u | ((to - here()) & 0x03FFFFFCu)); }
    std::uint32_t function() {
        w(0x4E800020);  // the previous function's blr
        return here();
    }
};

void test_finder() {
    Asm a;
    const std::uint32_t check = a.function();
    a.w(0x7C661B78);
    a.w(0x3CA08D80);  // lis r5,0x8D80
    a.w(0x4E800020);
    const std::uint32_t si = a.function();
    a.w(0x7C0802A6);
    a.w(0x4E800020);
    const std::uint32_t helper = a.function();
    a.w(0x7C0802A6);
    // Two EXI commands each.
    auto exi = [&](std::uint32_t first, std::uint32_t second, bool sixth) {
        const std::uint32_t f = a.function();
        a.w(0x9421FFC0);
        if (sixth) a.w(0x7D1E4378);  // mr r30,r8
        a.w(0x64030000 | first);     // oris r3,r0,first
        a.bl(check);
        if (second != 0) {
            a.w(0x3C600000 | second);  // lis r3,second
            a.bl(check);
        }
        return f;
    };
    const std::uint32_t wbyte = exi(0x0100, 0x0200, false);
    const std::uint32_t rbyte = exi(0x0100, 0x0300, false);
    const std::uint32_t wblock = exi(0x0100, 0x0500, true);
    const std::uint32_t rblock = exi(0x0100, 0x0300, true);
    const std::uint32_t risr = exi(0x8200, 0, false);
    const std::uint32_t wisr = exi(0x8300, 0, false);
    const std::uint32_t rimr = exi(0x8600, 0, false);
    const std::uint32_t wimr = exi(0x8700, 0, false);
    const std::uint32_t lan = exi(0xFF00, 0, false);
    const std::uint32_t set_callback = a.function();  // calls EXISetExiCallback only
    a.w(0x9421FFF0);
    a.bl(si);
    const std::uint32_t connect = a.function();
    a.w(0x3C03F9FC);
    a.w(0x28001000);
    // The JVS send.
    a.function();
    a.w(0x38600000);
    a.w(0x38A00080);
    a.w(0x38E00080);
    a.bl(si);
    // The media board's commands.
    auto di = [&](std::uint32_t word) {
        const std::uint32_t f = a.function();
        a.w(word);
        a.bl(helper);
        return f;
    };
    di(0x386000A8);
    const std::uint32_t write = di(0x386000AA);
    const std::uint32_t inquiry = di(0x38600012);
    const std::uint32_t execute = di(0x600300AB);
    a.w(0x4E800020);

    std::vector<std::uint8_t> bytes;
    for (std::uint32_t w : a.words) {
        bytes.push_back(std::uint8_t(w >> 24));
        bytes.push_back(std::uint8_t(w >> 16));
        bytes.push_back(std::uint8_t(w >> 8));
        bytes.push_back(std::uint8_t(w));
    }
    const std::vector<riftwii::CodeRange> text{{a.base, bytes.data(), bytes.size()}};
    const auto found = riftwii::find_gc_triforce_functions(text);
    if (!found.complete()) std::printf("missing: %s\n", found.missing.c_str());
    EXPECT_EQ(found.complete(), 1);
    EXPECT_EQ(found.hooks[GC_TRI_SI_TRANSFER], si);
    EXPECT_EQ(found.hooks[GC_TRI_EXI_CONNECT], connect);
    EXPECT_EQ(found.hooks[GC_TRI_EXI_WRITE_BYTE], wbyte);
    EXPECT_EQ(found.hooks[GC_TRI_EXI_READ_BYTE], rbyte);
    EXPECT_EQ(found.hooks[GC_TRI_EXI_WRITE_BLOCK], wblock);
    EXPECT_EQ(found.hooks[GC_TRI_EXI_READ_BLOCK], rblock);
    EXPECT_EQ(found.hooks[GC_TRI_EXI_READ_ISR], risr);
    EXPECT_EQ(found.hooks[GC_TRI_EXI_WRITE_ISR], wisr);
    EXPECT_EQ(found.hooks[GC_TRI_EXI_READ_IMR], rimr);
    EXPECT_EQ(found.hooks[GC_TRI_EXI_WRITE_IMR], wimr);
    EXPECT_EQ(found.hooks[GC_TRI_EXI_WRITE_LANCNT], lan);
    EXPECT_EQ(found.hooks[GC_TRI_DI_WRITE], write);
    EXPECT_EQ(found.hooks[GC_TRI_DI_INQUIRY], inquiry);
    EXPECT_EQ(found.hooks[GC_TRI_DI_EXECUTE], execute);
    EXPECT_EQ(found.hooks[GC_TRI_EXI_CALLBACK], set_callback);

    // A GameCube game has none of it.
    const std::vector<std::uint8_t> plain(64, 0);
    const std::vector<riftwii::CodeRange> none{{0x80003100, plain.data(), plain.size()}};
    EXPECT_EQ(riftwii::find_gc_triforce_functions(none).complete(), 0);
}

}  // namespace

int main() {
    test_type_and_lateness();
    test_board_identity();
    test_jvs_reset_and_address();
    test_jvs_inputs();
    test_jvs_errors();
    test_jvs_outputs();
    test_wheel();
    test_board_and_backup();
    test_finder();
    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("gctri: all passed\n");
    return 0;
}
