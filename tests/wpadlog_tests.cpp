// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The Wii Remote log (riftwii/wpadlog.hpp, runtime/rtwlog.h): finding the
// print stubs and the HOME Menu exit in a game's code, and the saved log as
// text, with keys and addresses kept out of it; the Bluetooth trace and the
// Pro Controller translator's counts.
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/wpadlog.hpp"
#include "rtwlog.h"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

static bool has(const std::string& text, const std::string& what) { return text.find(what) != std::string::npos; }

static void put32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    b[at] = static_cast<std::uint8_t>(v >> 24);
    b[at + 1] = static_cast<std::uint8_t>(v >> 16);
    b[at + 2] = static_cast<std::uint8_t>(v >> 8);
    b[at + 3] = static_cast<std::uint8_t>(v);
}

static std::vector<std::uint8_t> words(const std::vector<std::uint32_t>& list) {
    std::vector<std::uint8_t> out(list.size() * 4);
    for (std::size_t i = 0; i < list.size(); ++i) put32(out, i * 4, list[i]);
    return out;
}

// A log file the way the blob leaves it.
struct LogFile {
    static constexpr std::size_t kHeader = 40;
    static constexpr std::size_t kEntry = 152;
    static constexpr std::size_t kPro = kHeader + kEntry * RTWLOG_ENTRIES;
    static constexpr std::size_t kBt = kPro + sizeof(rtwlog_pro);
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(sizeof(rtwlog_file), 0);
    std::uint32_t total = 0;
    std::uint32_t packets = 0;

    LogFile() {
        put32(bytes, 0, RTWLOG_MAGIC);
        put32(bytes, 4, RTWLOG_VERSION);
        std::memcpy(&bytes[8], "RMCE01", 6);
        put32(bytes, 16, 60750000u);  // the time base's rate
        put32(bytes, 24, 2);          // two print functions hooked
    }

    // Message number `total`, `seconds` after the start.
    void add(double seconds, const std::string& format, const std::vector<std::uint32_t>& args,
             const std::vector<std::string>& strings = {}, std::uint32_t repeat = 0) {
        const std::size_t at = kHeader + kEntry * rtwlog_slot(total);
        put32(bytes, at, static_cast<std::uint32_t>(seconds * 60750000.0 / 256.0));
        put32(bytes, at + 4, repeat);
        put32(bytes, at + 8, 0x8029a000u + total);
        for (std::size_t i = 0; i < args.size() && i < RTWLOG_ARGS; ++i) put32(bytes, at + 12 + 4 * i, args[i]);
        std::memcpy(&bytes[at + 40], format.c_str(), std::min<std::size_t>(format.size(), RTWLOG_FMT_BYTES - 1));
        for (std::size_t s = 0; s < strings.size() && s < RTWLOG_STRINGS; ++s) {
            std::memcpy(&bytes[at + 104 + s * RTWLOG_STRING_BYTES], strings[s].c_str(), strings[s].size());
        }
        ++total;
        put32(bytes, 20, total);
    }

    // The translator's count number `field` (rtwlog_pro's order).
    void pro(std::size_t field, std::uint32_t v) { put32(bytes, kPro + 4 * field, v); }

    // Traced packet number `packets`.
    void packet(double seconds, std::uint8_t kind, std::uint8_t flags, std::uint16_t handle, std::uint16_t cid,
                const std::vector<std::uint8_t>& data) {
        const std::size_t at = kBt + 28 * rtwlog_bt_slot(packets);
        put32(bytes, at, static_cast<std::uint32_t>(seconds * 60750000.0 / 256.0));
        bytes[at + 4] = kind;
        bytes[at + 5] = flags;
        bytes[at + 6] = static_cast<std::uint8_t>(handle >> 8);
        bytes[at + 7] = static_cast<std::uint8_t>(handle);
        bytes[at + 8] = 0;
        bytes[at + 9] = static_cast<std::uint8_t>(data.size());
        bytes[at + 10] = static_cast<std::uint8_t>(cid >> 8);
        bytes[at + 11] = static_cast<std::uint8_t>(cid);
        for (std::size_t i = 0; i < data.size() && i < RTWLOG_BT_BYTES; ++i) bytes[at + 12 + i] = data[i];
        ++packets;
        pro(9, packets);
    }
};

int main() {
    // The print stub: found exactly, even twice; one changed word refuses it.
    {
        const std::vector<std::uint32_t> stub = {0x9421FF90, 0x40860024, 0xD8210028, 0xD8410030, 0xD8610038,
                                                 0xD8810040, 0xD8A10048, 0xD8C10050, 0xD8E10058, 0xD9010060,
                                                 0x90610008, 0x9081000C, 0x90A10010, 0x90C10014, 0x90E10018,
                                                 0x9101001C, 0x91210020, 0x91410024, 0x38210070, 0x4E800020};
        std::vector<std::uint32_t> code = {0x60000000, 0x60000000};
        code.insert(code.end(), stub.begin(), stub.end());
        code.push_back(0x60000000);
        code.insert(code.end(), stub.begin(), stub.end());
        std::vector<std::uint32_t> changed = stub;
        changed[18] = 0x38210080;  // another frame size: not the empty stub
        code.insert(code.end(), changed.begin(), changed.end());
        std::vector<std::uint8_t> bytes = words(code);
        const std::vector<riftwii::CodeSpan> spans = {{bytes.data(), bytes.size(), 0x801c0000u}};
        const std::vector<std::uint32_t> found = riftwii::find_print_stubs(spans);
        EXPECT_EQ(found.size(), 2u);
        if (found.size() == 2) {
            EXPECT_EQ(found[0], 0x801c0008u);
            EXPECT_EQ(found[1], 0x801c0008u + 21 * 4);
        }
    }

    // The HOME Menu exit: the "li r5,0" after "li r4,2; li r3,1"; older
    // SDKs' "li r7,0" after "li r6,2; li r5,1"; nothing without the place.
    {
        std::vector<std::uint8_t> newer = words({0x60000000, 0x38800002, 0x38600001, 0x38A00000, 0x60000000});
        const std::vector<riftwii::CodeSpan> a = {{newer.data(), newer.size(), 0x80200000u}};
        EXPECT_EQ(riftwii::find_launch_menu_exit(a), 0x8020000Cu);
        std::vector<std::uint8_t> older = words({0x38C00002, 0x38A00001, 0x38E00000});
        const std::vector<riftwii::CodeSpan> b = {{older.data(), older.size(), 0x80100000u}};
        EXPECT_EQ(riftwii::find_launch_menu_exit(b), 0x80100008u);
        std::vector<std::uint8_t> neither = words({0x38800002, 0x38600001, 0x38A00001});
        const std::vector<riftwii::CodeSpan> c = {{neither.data(), neither.size(), 0x80100000u}};
        EXPECT_EQ(riftwii::find_launch_menu_exit(c), 0u);
    }

    // The firmware probe, as Mario Kart Wii has it: the handler at 0x80100000
    // ("cmpwi r4,-1" then "mr r26,r4"), the probe loading it into r6 and
    // the address 0x1770 next to the read report 0x17. Patched: 0x0016 and
    // "li r26,1". A probe without the report next to it is not one.
    {
        std::vector<std::uint32_t> handler = {0x9421FD80, 0x2C04FFFF, 0x7C7B1B78, 0x7C9A2378, 0x4E800020};
        std::vector<std::uint32_t> caller = {0x3CC08010,   // lis r6,0x8010
                                             0x39600000,   // li r11,0
                                             0x38C60000,   // addi r6,r6,0
                                             0x39001770,   // li r8,0x1770
                                             0x38E00017,   // li r7,0x17
                                             0x4E800020};
        std::vector<std::uint8_t> a = words(handler), b = words(caller);
        const std::vector<riftwii::CodeSpan> spans = {{a.data(), a.size(), 0x80100000u}, {b.data(), b.size(), 0x80200000u}};
        riftwii::WpadProbePatch patch;
        std::string why;
        EXPECT_TRUE(riftwii::find_wpad_probe(spans, patch, why));
        EXPECT_EQ(patch.probe, 0x8020000Cu);
        EXPECT_EQ(patch.handler, 0x80100000u);
        EXPECT_EQ(patch.result, 0x8010000Cu);
        riftwii::apply_wpad_probe(spans, patch);
        EXPECT_EQ(b[12] * 0x1000000u + b[13] * 0x10000u + b[14] * 0x100u + b[15], 0x39000016u);
        EXPECT_EQ(a[12] * 0x1000000u + a[13] * 0x10000u + a[14] * 0x100u + a[15], 0x3B400001u);

        std::vector<std::uint8_t> lone = words({0x39001770, 0x60000000, 0x4E800020});
        const std::vector<riftwii::CodeSpan> other = {{lone.data(), lone.size(), 0x80300000u}};
        EXPECT_FALSE(riftwii::find_wpad_probe(other, patch, why));
    }

    // The log as text: numbers, a %s copied by the blob, a floating-point
    // argument shown as "?", a repeat, keys and addresses kept out.
    {
        LogFile log;
        log.add(0.0, "WUDSecurityCallback: \n", {});
        log.add(0.5, "DISCOVER RESULT:  %02x:%02x:%02x:%02x:%02x:%02x   %s (%04x)\n",
                {0x00, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f, 0x80300000, 0x2508}, {"Nintendo RVL-CNT-01-UC"});
        log.add(1.25, "devId: %d, subId: %d\n", {3, 0xFFFFFFFFu}, {}, 4);
        log.add(2.0, "x = %lf, y = %lf\n", {});
        log.add(3.0, "LINKKEY: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
                {0xde, 0xad, 0xbe, 0xef, 0x01, 0x02, 0x03});
        log.add(4.0, "ack error --> report ID = %d, error code = %d\n", {0x16, 3});
        std::string text, error;
        EXPECT_TRUE(riftwii::describe_wpad_log(log.bytes.data(), log.bytes.size(), text, error));
        EXPECT_TRUE(has(text, "Wii Remote log: RMCE01, 6 message(s), 2 print function(s) hooked"));
        EXPECT_TRUE(has(text, "Pro Controller test: off\n"));
        EXPECT_TRUE(has(text, "[   0.000] WUDSecurityCallback:\n"));
        // Eight arguments, seven kept (r4-r10): the last one shows as "?".
        EXPECT_TRUE(has(text, "DISCOVER RESULT:  xx:xx:xx:xx:xx:5f   \"Nintendo RVL-CNT-01-UC\" (?)"));
        EXPECT_TRUE(has(text, "[   1.250] devId: 3, subId: -1 (4 more time(s))"));
        EXPECT_TRUE(has(text, "x = ?, y = ?"));
        EXPECT_TRUE(has(text, "LINKKEY: **:**:**"));
        EXPECT_FALSE(has(text, "de:ad") || has(text, "dead"));
        EXPECT_FALSE(has(text, "1b:2c"));
        EXPECT_TRUE(has(text, "ack error --> report ID = 22, error code = 3"));
    }

    // The Pro Controller test's state, from the header.
    {
        LogFile log;
        put32(log.bytes, 32, RTWLOG_PROBE_CHANGED);
        put32(log.bytes, 36, 0x801d5a10u);
        log.add(0.0, "hello\n", {});
        std::string text, error;
        EXPECT_TRUE(riftwii::describe_wpad_log(log.bytes.data(), log.bytes.size(), text, error));
        EXPECT_TRUE(has(text, "Pro Controller test: on, the firmware probe at 0x801d5a10 reads 0x0016\n"));
        put32(log.bytes, 32, RTWLOG_PROBE_NOT_FOUND);
        EXPECT_TRUE(riftwii::describe_wpad_log(log.bytes.data(), log.bytes.size(), text, error));
        EXPECT_TRUE(has(text, "the firmware probe was not found in this game"));
        // A version 1 log (no probe state) is another version.
        put32(log.bytes, 4, 1);
        EXPECT_FALSE(riftwii::describe_wpad_log(log.bytes.data(), log.bytes.size(), text, error));
    }

    // More messages than fit: the first 320, a note, then the last 128 in order.
    {
        LogFile log;
        for (std::uint32_t n = 0; n < 600; ++n) log.add(n * 0.01, "message %d\n", {n});
        std::string text, error;
        EXPECT_TRUE(riftwii::describe_wpad_log(log.bytes.data(), log.bytes.size(), text, error));
        EXPECT_TRUE(has(text, "message 0\n"));
        EXPECT_TRUE(has(text, "message 319\n"));
        EXPECT_FALSE(has(text, "message 320\n"));
        EXPECT_TRUE(has(text, "... 152 message(s) not kept ..."));
        EXPECT_FALSE(has(text, "message 471\n"));
        EXPECT_TRUE(has(text, "message 472\n"));
        EXPECT_TRUE(has(text, "message 599\n"));
        EXPECT_TRUE(text.find("message 472\n") < text.find("message 599\n"));
    }

    // The Bluetooth trace: named events and commands, addresses cut to their
    // last byte, link keys never shown, who ended a link, the translator's
    // counts and changes.
    {
        LogFile log;
        log.add(0.0, "BTA_Init() is started\n", {});
        log.pro(0, 1);  // translating
        log.pro(1, 1);  // hooked
        log.pro(2, 1);
        log.pro(3, 250);
        log.pro(4, 1);
        log.pro(10, 900);
        log.packet(0.5, RTWLOG_BT_COMMAND, 0, 0, 0, {0x03, 0x0C, 0x00});
        log.packet(0.6, RTWLOG_BT_COMMAND, 0, 0, 0,
                   {0x0B, 0x04, 0x16, 0x93, 0x11, 0x22, 0x33, 0x44, 0x55, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x12});
        log.packet(1.1, RTWLOG_BT_EVENT, 0, 0, 0,
                   {0x03, 0x0B, 0x00, 0x21, 0x00, 0x93, 0x11, 0x22, 0x33, 0x44, 0x55, 0x01, 0x00});
        log.packet(1.3, RTWLOG_BT_OUT, 2, 0x21, 0x41, {0xA2, 0x12, 0x04, 0x34});
        log.packet(1.4, RTWLOG_BT_IN, 0, 0x21, 0x01, {0x06, 0x05, 0x04, 0x00, 0x40, 0x00, 0x41, 0x00});
        log.packet(1.5, RTWLOG_BT_EVENT, 0, 0, 0, {0x05, 0x04, 0x00, 0x21, 0x00, 0x13});
        std::string text, error;
        EXPECT_TRUE(riftwii::describe_wpad_log(log.bytes.data(), log.bytes.size(), text, error));
        EXPECT_TRUE(has(text, "Pro Controller translator: on; 1 Pro Controller link(s), 250 report(s) rebuilt, 1 "
                              "extension ID(s)"));
        EXPECT_TRUE(has(text, "Bluetooth trace: 6 packet(s); 900 data report(s)"));
        EXPECT_TRUE(has(text, "HCI command 0x0c03 (Reset): 03 0c 00"));
        // Link Key Request Reply: the address's last byte, then nothing of the key.
        EXPECT_TRUE(has(text, "(Link Key Request Reply): 0b 04 16 93 xx xx xx xx xx ** ** ** ** ** ** **"));
        EXPECT_FALSE(has(text, "aa bb"));
        EXPECT_TRUE(has(text, "(Connection Complete): 03 0b 00 21 00 93 xx xx xx xx xx 01 00"));
        EXPECT_FALSE(has(text, "11 22 33"));
        EXPECT_TRUE(has(text, "to   0x0021 cid 0x0041, 4 byte(s) (reporting mode): a2 12 04 34  [changed by the translator]"));
        EXPECT_TRUE(has(text, "from 0x0021 cid 0x0001, 8 byte(s) (L2CAP Disconnection Request)"));
        EXPECT_TRUE(has(text, "handle 0x0021, reason 0x13: the controller ended it"));
        EXPECT_TRUE(text.find("BTA_Init") < text.find("HCI command 0x0c03"));
    }
    // Translator off: said so; not hooked: said so.
    {
        LogFile log;
        log.pro(1, 1);
        std::string text, error;
        EXPECT_TRUE(riftwii::describe_wpad_log(log.bytes.data(), log.bytes.size(), text, error));
        EXPECT_TRUE(has(text, "Pro Controller translator: off"));
        LogFile unhooked;
        EXPECT_TRUE(riftwii::describe_wpad_log(unhooked.bytes.data(), unhooked.bytes.size(), text, error));
        EXPECT_TRUE(has(text, "Bluetooth trace: off"));
    }

    // The encryption tables: nine boxes of 256 (each 0..255 once) after a
    // 48-byte answer table, found at the answer table; one repeated value
    // in a box, or eight boxes only, and nothing is found.
    {
        std::vector<std::uint8_t> data(0x100 + 48 + 9 * 256 + 0x40, 0x5A);
        const std::size_t answers = 0x100;
        for (std::size_t box = 0; box < 9; ++box) {
            for (std::size_t i = 0; i < 256; ++i) {
                data[answers + 48 + 256 * box + i] = static_cast<std::uint8_t>((i * 7 + box * 13) & 0xFF);
            }
        }
        std::vector<riftwii::CodeSpan> spans = {riftwii::CodeSpan{data.data(), data.size(), 0x80290000u}};
        EXPECT_EQ(riftwii::find_extension_tables(spans), 0x80290100u);
        data[answers + 48 + 256 * 8 + 3] = data[answers + 48 + 256 * 8 + 4];
        EXPECT_EQ(riftwii::find_extension_tables(spans), 0u);
    }

    // Not a log, or another version: refused with a reason.
    {
        std::vector<std::uint8_t> junk(64, 0);
        std::string text, error;
        EXPECT_FALSE(riftwii::describe_wpad_log(junk.data(), junk.size(), text, error));
        LogFile log;
        put32(log.bytes, 4, 9);
        EXPECT_FALSE(riftwii::describe_wpad_log(log.bytes.data(), log.bytes.size(), text, error));
        EXPECT_TRUE(has(error, "version"));
    }

    if (g_failures == 0) {
        std::cout << "ALL WII REMOTE LOG TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
