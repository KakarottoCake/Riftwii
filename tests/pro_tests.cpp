// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The Wii U Pro Controller translator (runtime/rtpro.h): links followed
// from HCI events, the reporting mode asked for, the extension ID, the
// calibration reads, the acknowledgements and the data reports rebuilt as
// a Classic Controller Pro's; the extension's encryption, with made-up
// lookup tables (a game's own are never in the repository).
#include "rtpro.h"

#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << +(a) << " != " << +(b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using Bytes = std::vector<std::uint8_t>;

namespace {

constexpr std::uint16_t kPro = 0x0021, kRemote = 0x0022;
const std::uint8_t kProAddress[6] = {0x93, 0x11, 0x22, 0x33, 0x44, 0x55};    // as in SYSCONF
const std::uint8_t kOtherAddress[6] = {0x15, 0x66, 0x77, 0x88, 0x99, 0xAA};

// An ACL packet: the handle (first fragment), the lengths, CID 0x0041.
Bytes Acl(std::uint16_t handle, const Bytes& report) {
    const std::size_t n = report.size();
    Bytes b = {static_cast<std::uint8_t>(handle), static_cast<std::uint8_t>(0x20 | (handle >> 8)),
               static_cast<std::uint8_t>(n + 4), static_cast<std::uint8_t>((n + 4) >> 8),
               static_cast<std::uint8_t>(n), static_cast<std::uint8_t>(n >> 8), 0x41, 0x00};
    b.insert(b.end(), report.begin(), report.end());
    return b;
}

// Connection Complete for `handle` from `address` (HCI order: reversed).
void Connect(rtpro_state& st, std::uint16_t handle, const std::uint8_t* address) {
    Bytes e = {0x03, 11, 0x00, static_cast<std::uint8_t>(handle), static_cast<std::uint8_t>(handle >> 8)};
    for (int i = 5; i >= 0; --i) e.push_back(address[i]);
    e.push_back(0x01);
    e.push_back(0x00);
    rtpro_event(&st, kProAddress, 1, e.data(), static_cast<std::uint32_t>(e.size()));
}

// A read reply: a1 21 00 00 SE AH AL and 16 data bytes.
Bytes ReadReply(std::uint32_t addr16, std::uint32_t len, std::uint32_t error, const Bytes& data) {
    Bytes r = {0xA1, 0x21, 0x00, 0x00, static_cast<std::uint8_t>(((len - 1) << 4) | error),
               static_cast<std::uint8_t>(addr16 >> 8), static_cast<std::uint8_t>(addr16)};
    for (std::size_t i = 0; i < 16; ++i) r.push_back(i < data.size() ? data[i] : 0);
    return r;
}

std::uint32_t In(rtpro_state& st, Bytes& acl, std::uint32_t* what = nullptr) {
    std::uint32_t w = 0;
    const std::uint32_t n = rtpro_acl_in(&st, acl.data(), static_cast<std::uint32_t>(acl.size()),
                                         static_cast<std::uint32_t>(acl.size()), &w);
    if (what) *what = w;
    return n;
}

std::uint32_t Out(rtpro_state& st, Bytes& acl) {
    return rtpro_acl_out(&st, acl.data(), static_cast<std::uint32_t>(acl.size()));
}

// The Pro Controller's eleven bytes: sticks (12 bits LE), buttons (active low).
Bytes ProBytes(int lx, int rx, int ly, int ry, std::uint8_t b8, std::uint8_t b9) {
    return {static_cast<std::uint8_t>(lx), static_cast<std::uint8_t>(lx >> 8), static_cast<std::uint8_t>(rx),
            static_cast<std::uint8_t>(rx >> 8), static_cast<std::uint8_t>(ly), static_cast<std::uint8_t>(ly >> 8),
            static_cast<std::uint8_t>(ry), static_cast<std::uint8_t>(ry >> 8), b8, b9, 0xFF};
}

void test_links() {
    rtpro_state st;
    rtpro_init(&st);
    Connect(st, kPro, kProAddress);
    Connect(st, kRemote, kOtherAddress);
    EXPECT_EQ(st.pros, 1u);
    // The Pro's mode request becomes 0x34; a Wii Remote's is left alone.
    Bytes mode = Acl(kPro, {0xA2, 0x12, 0x04, 0x37});
    EXPECT_TRUE(Out(st, mode) & RTPRO_CHANGED);
    EXPECT_EQ(mode[11], 0x34);
    Bytes remote = Acl(kRemote, {0xA2, 0x12, 0x04, 0x37});
    EXPECT_EQ(Out(st, remote) & RTPRO_CHANGED, 0u);
    EXPECT_EQ(remote[11], 0x37);
    // Disconnection forgets the link.
    const Bytes gone = {0x05, 4, 0x00, static_cast<std::uint8_t>(kPro), 0x00, 0x13};
    rtpro_event(&st, kProAddress, 1, gone.data(), static_cast<std::uint32_t>(gone.size()));
    Bytes again = Acl(kPro, {0xA2, 0x12, 0x04, 0x37});
    EXPECT_EQ(Out(st, again) & RTPRO_CHANGED, 0u);
}

void test_extension_id() {
    rtpro_state st;
    rtpro_init(&st);
    Connect(st, kRemote, kOtherAddress);  // not known by address
    Bytes read = Acl(kRemote, {0xA2, 0x17, 0x04, 0xA4, 0x00, 0xFA, 0x00, 0x06});
    Out(st, read);
    Bytes reply = Acl(kRemote, ReadReply(0x00FA, 6, 0, {0x00, 0x00, 0xA4, 0x20, 0x01, 0x20}));
    std::uint32_t what = 0;
    In(st, reply, &what);
    EXPECT_TRUE(what & RTPRO_CHANGED);
    const std::uint8_t want[6] = {0x01, 0x00, 0xA4, 0x20, 0x01, 0x01};
    for (int i = 0; i < 6; ++i) EXPECT_EQ(reply[15 + i], want[i]);
    EXPECT_EQ(st.pros, 1u);
    // A real Classic Controller's ID is left as it is.
    rtpro_state other;
    rtpro_init(&other);
    Connect(other, kRemote, kOtherAddress);
    Bytes read2 = Acl(kRemote, {0xA2, 0x17, 0x04, 0xA4, 0x00, 0xFA, 0x00, 0x06});
    Out(other, read2);
    Bytes classic = Acl(kRemote, ReadReply(0x00FA, 6, 0, {0x00, 0x00, 0xA4, 0x20, 0x01, 0x01}));
    In(other, classic, &what);
    EXPECT_EQ(what & RTPRO_CHANGED, 0u);
    EXPECT_EQ(other.pros, 0u);
}

void test_calibration() {
    rtpro_state st;
    rtpro_init(&st);
    Connect(st, kPro, kProAddress);
    // The Wii Remote's accelerometer calibration, which the Pro refuses.
    Bytes read = Acl(kPro, {0xA2, 0x17, 0x00, 0x00, 0x00, 0x16, 0x00, 0x0A});
    Out(st, read);
    Bytes reply = Acl(kPro, ReadReply(0x0016, 1, 0x8, {}));
    std::uint32_t what = 0;
    In(st, reply, &what);
    EXPECT_TRUE(what & RTPRO_CHANGED);
    EXPECT_EQ(reply[12] & 0x0F, 0);  // no error
    EXPECT_EQ(reply[15], 0x80);
    EXPECT_EQ(reply[19], 0x9A);
    unsigned sum = 0;
    for (int i = 0; i < 9; ++i) sum += reply[15 + i];
    EXPECT_EQ(reply[24], static_cast<std::uint8_t>(sum + 0x55));
    // The Classic Controller's, 32 bytes in two answers.
    Bytes cread = Acl(kPro, {0xA2, 0x17, 0x04, 0xA4, 0x00, 0x20, 0x00, 0x20});
    Out(st, cread);
    for (std::uint32_t half = 0; half < 2; ++half) {
        Bytes r = Acl(kPro, ReadReply(0x0020 + 16 * half, 16, 0, Bytes(16, 0x33)));
        In(st, r);
        EXPECT_EQ(r[15], 0xE0);
        EXPECT_EQ(r[16], 0x20);
        EXPECT_EQ(r[17], 0x80);
        unsigned s = 0;
        for (int i = 0; i < 14; ++i) s += r[15 + i];
        EXPECT_EQ(r[29], static_cast<std::uint8_t>(s + 0x55));
        EXPECT_EQ(r[30], static_cast<std::uint8_t>(s + 0xAA));
    }
    EXPECT_EQ(st.calibrations, 3u);
    // An acknowledgement's error is cleared on the Pro only.
    Bytes ack = Acl(kPro, {0xA1, 0x22, 0x00, 0x00, 0x16, 0x03});
    In(st, ack);
    EXPECT_EQ(ack[13], 0);
}

void test_classic_bytes() {
    // At rest, nothing pressed: sticks centred, bit 0 of byte 4 set.
    std::uint8_t c[6];
    const Bytes rest = ProBytes(0x800, 0x800, 0x800, 0x800, 0xFF, 0xFF);
    rtpro_classic(rest.data(), c);
    EXPECT_EQ(c[0] & 0x3F, 32);
    EXPECT_EQ(c[1] & 0x3F, 32);
    EXPECT_EQ(c[2] & 0x1F, 16);
    const int rx = ((c[0] >> 3) & 0x18) | ((c[1] >> 5) & 0x06) | (c[2] >> 7);
    EXPECT_EQ(rx, 16);
    EXPECT_EQ(c[3] & 0x1F, 0);  // R not pressed
    EXPECT_EQ(c[4], 0xFF);
    EXPECT_EQ(c[5], 0xFF);
    // Pushed all the way: left right and up, right stick left; A and L.
    const Bytes push = ProBytes(0x800 + 896, 0x800 - 896, 0x800 + 896, 0x800, 0xDF, 0xEF);
    rtpro_classic(push.data(), c);
    EXPECT_EQ(c[0] & 0x3F, 56);
    EXPECT_EQ(c[1] & 0x3F, 56);
    const int rx2 = ((c[0] >> 3) & 0x18) | ((c[1] >> 5) & 0x06) | (c[2] >> 7);
    EXPECT_EQ(rx2, 4);
    const int lt = ((c[2] >> 2) & 0x18) | (c[3] >> 5);
    EXPECT_EQ(lt, 31);
    EXPECT_EQ(c[4], 0xDF);  // L's bit cleared, as the Classic's
    EXPECT_EQ(c[5], 0xEF);  // A
    // Far past the end: clamped.
    const Bytes far = ProBytes(0xFFF, 0x000, 0x000, 0xFFF, 0xFF, 0xFF);
    rtpro_classic(far.data(), c);
    EXPECT_EQ(c[0] & 0x3F, 63);
    EXPECT_EQ(c[1] & 0x3F, 0);
    EXPECT_EQ(c[2] & 0x1F, 31);
}

void test_data_reports() {
    rtpro_state st;
    rtpro_init(&st);
    Connect(st, kPro, kProAddress);
    const Bytes pro = ProBytes(0x800, 0x800, 0x800, 0x800, 0xFF, 0xEF);
    const auto report34 = [&]() {
        Bytes r = {0xA1, 0x34, 0x00, 0x00};
        r.insert(r.end(), pro.begin(), pro.end());
        r.resize(23, 0x00);
        return Acl(kPro, r);
    };
    // The game asked for 0x37 (buttons, accelerometer, IR, 6 extension bytes).
    Bytes mode = Acl(kPro, {0xA2, 0x12, 0x04, 0x37});
    Out(st, mode);
    Bytes r = report34();
    std::uint32_t what = 0;
    const std::uint32_t n = In(st, r, &what);
    EXPECT_TRUE(what & RTPRO_CHANGED);
    EXPECT_EQ(n, 8u + 23u);
    EXPECT_EQ(r[9], 0x37);
    EXPECT_EQ(r[12], 0x80);  // accelerometer at rest
    EXPECT_EQ(r[14], 0x9A);
    EXPECT_EQ(r[15], 0xFF);  // no IR points
    EXPECT_EQ(r[24], 0xFF);
    EXPECT_EQ(r[25] & 0x3F, 32);  // the Classic's left stick, centred
    EXPECT_EQ(r[29], 0xFF);
    EXPECT_EQ(r[30], 0xEF);  // A
    // A shorter mode: the packet's lengths follow.
    rtpro_state st2;
    rtpro_init(&st2);
    Connect(st2, kPro, kProAddress);
    Bytes mode30 = Acl(kPro, {0xA2, 0x12, 0x00, 0x30});
    Out(st2, mode30);
    Bytes r2 = report34();
    const std::uint32_t n2 = In(st2, r2);
    EXPECT_EQ(n2, 8u + 4u);
    EXPECT_EQ(r2[2], 8);   // ACL length: L2CAP header + 4
    EXPECT_EQ(r2[4], 4);   // L2CAP length
    EXPECT_EQ(r2[9], 0x30);
    // 0x3D: extension bytes only.
    rtpro_state st3;
    rtpro_init(&st3);
    Connect(st3, kPro, kProAddress);
    Bytes mode3d = Acl(kPro, {0xA2, 0x12, 0x00, 0x3D});
    Out(st3, mode3d);
    Bytes r3 = report34();
    In(st3, r3);
    EXPECT_EQ(r3[9], 0x3D);
    EXPECT_EQ(r3[10] & 0x3F, 32);
    EXPECT_EQ(r3[15], 0xEF);
    // A Wii Remote's reports pass untouched.
    rtpro_state st4;
    rtpro_init(&st4);
    Connect(st4, kRemote, kOtherAddress);
    Bytes remote = Acl(kRemote, {0xA1, 0x30, 0x00, 0x08});
    const Bytes before = remote;
    EXPECT_EQ(In(st4, remote), static_cast<std::uint32_t>(before.size()));
    EXPECT_TRUE(remote == before);
}

// Made-up lookup tables in the layout a game keeps them: a 7 x 6 answer
// table (48 bytes with padding), a key box and eight table boxes, each a
// shuffle of 0..255.
std::vector<std::uint8_t> FakeTables() {
    std::vector<std::uint8_t> t(RTPRO_TABLE_BYTES, 0);
    std::uint32_t seed = 12345;
    const auto next = [&]() {
        seed = seed * 1103515245u + 12345u;
        return (seed >> 16) & 0x7FFF;
    };
    for (int i = 0; i < 42; ++i) t[i] = static_cast<std::uint8_t>(next());
    for (int box = 0; box < 9; ++box) {
        std::uint8_t* b = &t[48 + 256 * box];
        for (int i = 0; i < 256; ++i) b[i] = static_cast<std::uint8_t>(i);
        for (int i = 255; i > 0; --i) std::swap(b[i], b[next() % (i + 1)]);
    }
    return t;
}

std::uint8_t RotateRight(std::uint8_t v, unsigned n) {
    n &= 7;
    return static_cast<std::uint8_t>((v >> n) | (v << ((8 - n) & 7)));
}

// What a game does: a random part and a row make the key's last six bytes,
// and the two tables it decrypts with.
struct GameKey {
    std::uint8_t written[16];
    std::uint8_t xor_table[8], add_table[8];
};

GameKey MakeKey(const std::vector<std::uint8_t>& tables, unsigned row) {
    const std::uint8_t rnd[10] = {0x13, 0x5A, 0xC7, 0x02, 0x99, 0x41, 0xEE, 0x70, 0x3B, 0xD4};
    const std::uint8_t* keybox = &tables[48];
    std::uint8_t t[10], key[6];
    for (int i = 0; i < 10; ++i) t[i] = keybox[rnd[i]];
    const std::uint8_t* ans = &tables[6 * row];
    key[0] = static_cast<std::uint8_t>(static_cast<std::uint8_t>(RotateRight(ans[0] ^ t[5], t[2]) - t[9]) ^ t[4]);
    key[1] = static_cast<std::uint8_t>(static_cast<std::uint8_t>(RotateRight(ans[1] ^ t[1], t[0]) - t[5]) ^ t[7]);
    key[2] = static_cast<std::uint8_t>(static_cast<std::uint8_t>(RotateRight(ans[2] ^ t[6], t[8]) - t[2]) ^ t[0]);
    key[3] = static_cast<std::uint8_t>(static_cast<std::uint8_t>(RotateRight(ans[3] ^ t[4], t[7]) - t[3]) ^ t[2]);
    key[4] = static_cast<std::uint8_t>(static_cast<std::uint8_t>(RotateRight(ans[4] ^ t[1], t[6]) - t[3]) ^ t[4]);
    key[5] = static_cast<std::uint8_t>(static_cast<std::uint8_t>(RotateRight(ans[5] ^ t[7], t[8]) - t[5]) ^ t[9]);
    GameKey k;
    for (int i = 0; i < 10; ++i) k.written[i] = rnd[9 - i];
    for (int i = 0; i < 6; ++i) k.written[10 + i] = key[5 - i];
    const std::uint8_t* a = &tables[48 + 256 * (1 + row)];
    const std::uint8_t* b = a + 256;
    const std::uint8_t add[8] = {
        static_cast<std::uint8_t>(a[key[4]] ^ b[rnd[3]]), static_cast<std::uint8_t>(a[key[2]] ^ b[rnd[5]]),
        static_cast<std::uint8_t>(a[key[5]] ^ b[rnd[7]]), static_cast<std::uint8_t>(a[key[0]] ^ b[rnd[2]]),
        static_cast<std::uint8_t>(a[key[1]] ^ b[rnd[4]]), static_cast<std::uint8_t>(a[key[3]] ^ b[rnd[9]]),
        static_cast<std::uint8_t>(a[rnd[0]] ^ b[rnd[6]]), static_cast<std::uint8_t>(a[rnd[1]] ^ b[rnd[8]])};
    const std::uint8_t xr[8] = {
        static_cast<std::uint8_t>(a[key[0]] ^ b[rnd[1]]), static_cast<std::uint8_t>(a[key[5]] ^ b[rnd[4]]),
        static_cast<std::uint8_t>(a[key[3]] ^ b[rnd[0]]), static_cast<std::uint8_t>(a[key[2]] ^ b[rnd[9]]),
        static_cast<std::uint8_t>(a[key[4]] ^ b[rnd[7]]), static_cast<std::uint8_t>(a[key[1]] ^ b[rnd[8]]),
        static_cast<std::uint8_t>(a[rnd[3]] ^ b[rnd[5]]), static_cast<std::uint8_t>(a[rnd[2]] ^ b[rnd[6]])};
    for (int i = 0; i < 8; ++i) {
        k.add_table[i] = add[i];
        k.xor_table[i] = xr[i];
    }
    return k;
}

std::uint8_t GameDecrypt(const GameKey& k, std::uint8_t v, unsigned address) {
    return static_cast<std::uint8_t>((v ^ k.xor_table[address & 7]) + k.add_table[address & 7]);
}

// The writes a game sends to turn encryption on: 0xAA, then the key in 6 + 6 + 4.
void SendKey(rtpro_state& st, std::uint16_t handle, const GameKey& k, std::uint8_t* f0_sent) {
    Bytes on = Acl(handle, {0xA2, 0x16, 0x04, 0xA4, 0x00, 0xF0, 0x01, 0xAA});
    on.resize(8 + 23, 0);
    Out(st, on);
    if (f0_sent) *f0_sent = on[15];
    const unsigned parts[3][2] = {{0, 6}, {6, 6}, {12, 4}};
    for (const auto& part : parts) {
        Bytes w = {0xA2, 0x16, 0x04, 0xA4, 0x00, static_cast<std::uint8_t>(0x40 + part[0]),
                   static_cast<std::uint8_t>(part[1])};
        for (unsigned i = 0; i < 16; ++i) w.push_back(i < part[1] ? k.written[part[0] + i] : 0);
        Bytes acl = Acl(handle, w);
        Out(st, acl);
    }
}

void test_encryption() {
    const std::vector<std::uint8_t> tables = FakeTables();
    for (unsigned row = 0; row < 7; ++row) {
        rtpro_state st;
        rtpro_init(&st);
        rtpro_set_tables(&st, tables.data());
        Connect(st, kPro, kProAddress);
        Bytes mode = Acl(kPro, {0xA2, 0x12, 0x04, 0x35});
        Out(st, mode);
        const GameKey k = MakeKey(tables, row);
        std::uint8_t f0 = 0;
        SendKey(st, kPro, k, &f0);
        EXPECT_EQ(f0, 0x55);  // the Pro is kept plain
        EXPECT_EQ(st.keys, 1u);
        EXPECT_EQ(st.bad_keys, 0u);
        // A report with A held: what the game decrypts is the Classic's bytes.
        const Bytes pro = ProBytes(0x800, 0x800, 0x800, 0x800, 0xFF, 0xEF);
        Bytes r = {0xA1, 0x34, 0x00, 0x00};
        r.insert(r.end(), pro.begin(), pro.end());
        r.resize(23, 0);
        Bytes acl = Acl(kPro, r);
        In(st, acl);
        std::uint8_t plain[6];
        rtpro_classic(pro.data(), plain);
        for (unsigned i = 0; i < 6; ++i) EXPECT_EQ(GameDecrypt(k, acl[8 + 7 + i], i), plain[i]);
        // The Classic Controller's calibration, as the game decrypts it.
        Bytes cread = Acl(kPro, {0xA2, 0x17, 0x04, 0xA4, 0x00, 0x20, 0x00, 0x10});
        Out(st, cread);
        Bytes cal = Acl(kPro, ReadReply(0x0020, 16, 0, Bytes(16, 0x33)));
        In(st, cal);
        EXPECT_EQ(GameDecrypt(k, cal[15], 0x20), 0xE0);
        EXPECT_EQ(GameDecrypt(k, cal[17], 0x22), 0x80);
        // The Wii Remote's own calibration (EEPROM) is never encrypted.
        Bytes eread = Acl(kPro, {0xA2, 0x17, 0x00, 0x00, 0x00, 0x16, 0x00, 0x0A});
        Out(st, eread);
        Bytes eep = Acl(kPro, ReadReply(0x0016, 10, 0, {}));
        In(st, eep);
        EXPECT_EQ(eep[15], 0x80);
    }
    // A Pro that encrypts its own bytes anyway: seen at its first report.
    {
        rtpro_state st;
        rtpro_init(&st);
        rtpro_set_tables(&st, tables.data());
        Connect(st, kPro, kProAddress);
        Bytes mode = Acl(kPro, {0xA2, 0x12, 0x04, 0x34});
        Out(st, mode);
        const GameKey k = MakeKey(tables, 3);
        SendKey(st, kPro, k, nullptr);
        const Bytes pro = ProBytes(0x800, 0x800, 0x800, 0x800, 0xFF, 0xFF);
        Bytes r = {0xA1, 0x34, 0x00, 0x00};
        for (unsigned i = 0; i < pro.size(); ++i) {
            // As the game's tables would encrypt it.
            const std::uint8_t e = static_cast<std::uint8_t>(
                static_cast<std::uint8_t>(pro[i] - k.add_table[i & 7]) ^ k.xor_table[i & 7]);
            r.push_back(e);
        }
        r.resize(23, 0);
        Bytes acl = Acl(kPro, r);
        In(st, acl);
        EXPECT_EQ(st.links[0].pro_encrypts, RTPRO_SENDS_ENCRYPTED);
        EXPECT_EQ(GameDecrypt(k, acl[8 + 4], 0) & 0x3F, 32);  // the left stick, centred
        EXPECT_EQ(GameDecrypt(k, acl[8 + 4 + 4], 4), 0xFF);   // nothing pressed
    }
    // A key that matches no row, or no tables found: counted, left plain.
    {
        rtpro_state st;
        rtpro_init(&st);
        Connect(st, kPro, kProAddress);
        const GameKey k = MakeKey(tables, 0);
        SendKey(st, kPro, k, nullptr);
        EXPECT_EQ(st.bad_keys, 1u);
        EXPECT_EQ(st.links[0].cipher, RTPRO_NO_TABLES);
    }
    // The old way: one 0x00 at 0xA40040, the fixed 0x17 transform.
    {
        rtpro_state st;
        rtpro_init(&st);
        Connect(st, kPro, kProAddress);
        Bytes w = {0xA2, 0x16, 0x04, 0xA4, 0x00, 0x40, 0x01, 0x00};
        w.resize(23, 0);
        Bytes acl = Acl(kPro, w);
        Out(st, acl);
        EXPECT_EQ(st.links[0].cipher, RTPRO_KEYED);
        Bytes mode = Acl(kPro, {0xA2, 0x12, 0x04, 0x34});
        Out(st, mode);
        const Bytes pro = ProBytes(0x800, 0x800, 0x800, 0x800, 0xFF, 0xEF);
        Bytes r = {0xA1, 0x34, 0x00, 0x00};
        r.insert(r.end(), pro.begin(), pro.end());
        r.resize(23, 0);
        Bytes rep = Acl(kPro, r);
        In(st, rep);
        // The old games decrypt with (x ^ 0x17) + 0x17.
        EXPECT_EQ(static_cast<std::uint8_t>((rep[8 + 4 + 5] ^ 0x17) + 0x17), 0xEF);
    }
}

void test_bad_packets() {
    rtpro_state st;
    rtpro_init(&st);
    Connect(st, kPro, kProAddress);
    // Too short, lengths that disagree, a continuation fragment, signalling.
    Bytes tiny = {0x21, 0x20, 0x01};
    EXPECT_EQ(In(st, tiny), 3u);
    Bytes wrong = Acl(kPro, {0xA1, 0x34, 0x00, 0x00});
    wrong[4] = 0x40;
    EXPECT_EQ(In(st, wrong), static_cast<std::uint32_t>(wrong.size()));
    Bytes cont = Acl(kPro, {0xA1, 0x22, 0x00, 0x00, 0x16, 0x03});
    cont[1] = 0x10;
    In(st, cont);
    EXPECT_EQ(cont[13], 0x03);
    Bytes signalling = Acl(kPro, {0xA1, 0x22, 0x00, 0x00, 0x16, 0x03});
    signalling[6] = 0x01;
    In(st, signalling);
    EXPECT_EQ(signalling[13], 0x03);
}

}  // namespace

int main() {
    test_links();
    test_extension_id();
    test_calibration();
    test_classic_bytes();
    test_data_reports();
    test_encryption();
    test_bad_packets();
    if (g_failures == 0) {
        std::cout << "ALL PRO TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
