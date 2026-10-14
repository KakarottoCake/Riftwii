// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/wpadlog.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "rtwlog.h"

namespace riftwii {
namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

// The compiler's empty varargs function, word for word.
constexpr std::uint32_t kPrintStub[] = {
    0x9421FF90u,  // stwu r1,-0x70(r1)
    0x40860024u,  // bne cr1,+0x24 (no floating-point arguments)
    0xD8210028u, 0xD8410030u, 0xD8610038u, 0xD8810040u,  // stfd f1-f4
    0xD8A10048u, 0xD8C10050u, 0xD8E10058u, 0xD9010060u,  // stfd f5-f8
    0x90610008u, 0x9081000Cu, 0x90A10010u, 0x90C10014u,  // stw r3-r6
    0x90E10018u, 0x9101001Cu, 0x91210020u, 0x91410024u,  // stw r7-r10
    0x38210070u,  // addi r1,r1,0x70
    0x4E800020u,  // blr
};
constexpr std::size_t kPrintStubWords = sizeof(kPrintStub) / sizeof(kPrintStub[0]);

// li rD,value
constexpr std::uint32_t li(unsigned d, std::uint16_t value) { return 0x38000000u | (d << 21) | value; }

// The first of returnto.cpp's places: "li rL,2; li rH,1; li rZ,0". The
// address of its "li rZ,0", or 0.
std::uint32_t first_launch_place(const std::vector<CodeSpan>& spans, unsigned rl, unsigned rh, unsigned rz) {
    for (const CodeSpan& s : spans) {
        for (std::size_t o = 0; o + 12 <= s.size; o += 4) {
            const std::uint8_t* p = s.bytes + o;
            if (be32(p) == li(rl, 2) && be32(p + 4) == li(rh, 1) && be32(p + 8) == li(rz, 0)) {
                return s.address + static_cast<std::uint32_t>(o) + 8;
            }
        }
    }
    return 0;
}

// The conversions of a printf format, in order.
struct Conversion {
    std::size_t start = 0;  // the '%'
    std::size_t end = 0;    // past the conversion letter
    std::string spec;       // flags, width, precision and length ("02", "l")
    char letter = 0;
    bool floating = false;  // takes a floating-point register, not one of r4-r10
};

std::vector<Conversion> conversions(const std::string& format) {
    std::vector<Conversion> out;
    std::size_t i = 0;
    while (i < format.size()) {
        if (format[i] != '%') {
            ++i;
            continue;
        }
        Conversion c;
        c.start = i++;
        while (i < format.size() && std::strchr("-+ #.lhLz0123456789", format[i]) != nullptr) c.spec += format[i++];
        if (i >= format.size()) break;
        c.letter = format[i++];
        c.end = i;
        if (c.letter == '%') continue;
        c.floating = std::strchr("fFeEgGa", c.letter) != nullptr;
        out.push_back(c);
    }
    return out;
}

// Bluetooth addresses are printed as six "%02x" joined by ':'. Returns,
// for each conversion, whether it is one of an address's first five bytes.
std::vector<bool> address_bytes(const std::string& format, const std::vector<Conversion>& list) {
    std::vector<bool> hidden(list.size(), false);
    for (std::size_t first = 0; first + 6 <= list.size(); ++first) {
        bool address = true;
        for (std::size_t k = first; k < first + 6 && address; ++k) {
            address = list[k].letter == 'x' && list[k].spec == "02";
            if (address && k + 1 < first + 6) {
                address = list[k + 1].start == list[k].end + 1 && format[list[k].end] == ':';
            }
        }
        if (!address) continue;
        for (std::size_t k = first; k < first + 5; ++k) hidden[k] = true;
        first += 5;
    }
    return hidden;
}

// One integer argument as its conversion asks.
std::string integer_text(const Conversion& c, std::uint32_t value) {
    char buffer[48];
    std::string spec = c.spec;
    // printf's length letters ("l", "h") mean nothing for a 32-bit word.
    std::string flags;
    for (char ch : spec) {
        if (ch != 'l' && ch != 'h' && ch != 'L' && ch != 'z') flags += ch;
    }
    const std::string pattern = "%" + flags;
    switch (c.letter) {
        case 'd':
        case 'i':
            std::snprintf(buffer, sizeof(buffer), (pattern + "d").c_str(), static_cast<int>(static_cast<std::int32_t>(value)));
            break;
        case 'u':
            std::snprintf(buffer, sizeof(buffer), (pattern + "u").c_str(), static_cast<unsigned>(value));
            break;
        case 'x':
        case 'X':
        case 'o':
            std::snprintf(buffer, sizeof(buffer), (pattern + c.letter).c_str(), static_cast<unsigned>(value));
            break;
        case 'c':
            std::snprintf(buffer, sizeof(buffer), "%c", std::isprint(static_cast<int>(value & 0xFF)) ? static_cast<int>(value & 0xFF) : '?');
            break;
        case 'p':
            std::snprintf(buffer, sizeof(buffer), "0x%08x", static_cast<unsigned>(value));
            break;
        default:
            std::snprintf(buffer, sizeof(buffer), "0x%x", static_cast<unsigned>(value));
            break;
    }
    return buffer;
}

std::string lower(std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

std::string text_field(const std::uint8_t* p, std::size_t limit) {
    std::size_t n = 0;
    while (n < limit && p[n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(p), n);
}

// One message, its format filled in.
std::string message_text(const std::uint8_t* entry) {
    const std::string format = text_field(entry + 40, RTWLOG_FMT_BYTES);
    std::uint32_t args[RTWLOG_ARGS];
    for (std::uint32_t i = 0; i < RTWLOG_ARGS; ++i) args[i] = be32(entry + 12 + 4 * i);
    const std::uint8_t* strings = entry + 40 + RTWLOG_FMT_BYTES;

    const std::vector<Conversion> list = conversions(format);
    const std::vector<bool> hidden = address_bytes(format, list);
    // A link key (or anything else printed as a key) is never shown.
    const bool secret = lower(format).find("key") != std::string::npos;

    std::string out;
    std::size_t at = 0;
    std::uint32_t gpr = 0, string_index = 0;
    for (std::size_t k = 0; k < list.size(); ++k) {
        const Conversion& c = list[k];
        out += format.substr(at, c.start - at);
        at = c.end;
        if (c.floating) {
            out += "?";  // in a floating-point register, which is not kept
            continue;
        }
        if (gpr >= RTWLOG_ARGS) {
            out += "?";
            continue;
        }
        const std::uint32_t value = args[gpr++];
        if (secret) {
            out += "**";
        } else if (hidden[k]) {
            out += "xx";
        } else if (c.letter == 's') {
            out += string_index < RTWLOG_STRINGS
                       ? "\"" + text_field(strings + string_index * RTWLOG_STRING_BYTES, RTWLOG_STRING_BYTES) + "\""
                       : std::string("(text)");
            ++string_index;
        } else {
            out += integer_text(c, value);
        }
    }
    out += format.substr(at);
    // One line: the format's own line breaks become spaces.
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) out.pop_back();
    for (char& ch : out) {
        if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
    }
    return out;
}

}  // namespace

std::vector<std::uint32_t> find_print_stubs(const std::vector<CodeSpan>& spans) {
    std::vector<std::uint32_t> out;
    for (const CodeSpan& s : spans) {
        for (std::size_t o = 0; o + kPrintStubWords * 4 <= s.size; o += 4) {
            bool match = true;
            for (std::size_t w = 0; w < kPrintStubWords && match; ++w) match = be32(s.bytes + o + 4 * w) == kPrintStub[w];
            if (match) out.push_back(s.address + static_cast<std::uint32_t>(o));
        }
    }
    return out;
}

std::uint32_t find_launch_menu_exit(const std::vector<CodeSpan>& spans) {
    // Newer SDKs load the title into r3/r4 (r5 = 0), older ones into r5/r6 (r7 = 0).
    const std::uint32_t newer = first_launch_place(spans, 4, 3, 5);
    return newer != 0 ? newer : first_launch_place(spans, 6, 5, 7);
}

bool describe_wpad_log(const std::uint8_t* bytes, std::size_t size, std::string& text, std::string& error) {
    constexpr std::size_t kHeader = 32;
    constexpr std::size_t kEntry = 152;
    static_assert(sizeof(rtwlog_entry) == kEntry, "the entry layout the decoder reads");
    if (size < kHeader + kEntry * RTWLOG_ENTRIES || be32(bytes) != RTWLOG_MAGIC) {
        error = "not a Wii Remote log";
        return false;
    }
    if (be32(bytes + 4) != RTWLOG_VERSION) {
        error = "a Wii Remote log of another version (" + std::to_string(be32(bytes + 4)) + ")";
        return false;
    }
    const std::string game = text_field(bytes + 8, 6);
    const std::uint32_t ticks_per_second = be32(bytes + 16);
    const std::uint32_t total = be32(bytes + 20);
    const std::uint32_t stubs = be32(bytes + 24);
    const double units_per_second = ticks_per_second / 256.0;

    char line[160];
    std::snprintf(line, sizeof(line), "Wii Remote log: %s, %u message(s), %u print function(s) hooked\n",
                  game.c_str(), static_cast<unsigned>(total), static_cast<unsigned>(stubs));
    text = line;
    if (total == 0) {
        text += "(the game printed nothing about its controllers)\n";
        return true;
    }

    const auto entry_at = [&](std::uint32_t n) { return bytes + kHeader + kEntry * rtwlog_slot(n); };
    const std::uint32_t start_tick = be32(entry_at(0));
    const auto write_message = [&](std::uint32_t n) {
        const std::uint8_t* e = entry_at(n);
        const double seconds = units_per_second > 0 ? static_cast<std::uint32_t>(be32(e) - start_tick) / units_per_second : 0;
        const std::uint32_t repeat = be32(e + 4);
        std::snprintf(line, sizeof(line), "[%8.3f] ", seconds);
        text += line;
        text += message_text(e);
        if (repeat != 0) text += " (" + std::to_string(repeat) + " more time(s))";
        text += "\n";
    };

    // The first messages, then (when there were more than fit) the last ones.
    const std::uint32_t first = total < RTWLOG_FIRST ? total : RTWLOG_FIRST;
    for (std::uint32_t n = 0; n < first; ++n) write_message(n);
    if (total > RTWLOG_FIRST) {
        const std::uint32_t last_start = total - RTWLOG_LAST > RTWLOG_FIRST ? total - RTWLOG_LAST : RTWLOG_FIRST;
        if (last_start > RTWLOG_FIRST) {
            text += "... " + std::to_string(last_start - RTWLOG_FIRST) + " message(s) not kept ...\n";
        }
        for (std::uint32_t n = last_start; n < total; ++n) write_message(n);
    }
    return true;
}

}  // namespace riftwii
