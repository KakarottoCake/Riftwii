// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/wpadlog.hpp"

#include <cctype>
#include <cstddef>
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

namespace {

constexpr std::uint16_t kProbeAddress = 0x1770;     // past a Wii Remote's memory
constexpr std::uint16_t kCalibrationAddress = 0x0016;
constexpr std::uint32_t kReadReport = 0x17;
constexpr std::size_t kNear = 16;                    // words searched around a place

// The span holding `address` (4 bytes), or nullptr.
const CodeSpan* span_at(const std::vector<CodeSpan>& spans, std::uint32_t address) {
    for (const CodeSpan& s : spans) {
        if (address >= s.address && address - s.address + 4 <= s.size) return &s;
    }
    return nullptr;
}

bool word_at(const std::vector<CodeSpan>& spans, std::uint32_t address, std::uint32_t& word) {
    const CodeSpan* s = span_at(spans, address);
    if (s == nullptr) return false;
    word = be32(s->bytes + (address - s->address));
    return true;
}

void put_word(const std::vector<CodeSpan>& spans, std::uint32_t address, std::uint32_t word) {
    const CodeSpan* s = span_at(spans, address);
    if (s == nullptr) return;
    std::uint8_t* p = s->bytes + (address - s->address);
    p[0] = static_cast<std::uint8_t>(word >> 24);
    p[1] = static_cast<std::uint8_t>(word >> 16);
    p[2] = static_cast<std::uint8_t>(word >> 8);
    p[3] = static_cast<std::uint8_t>(word);
}

unsigned rd(std::uint32_t word) { return (word >> 21) & 31; }
unsigned ra(std::uint32_t word) { return (word >> 16) & 31; }

// li rD,value (addi rD,0,value)
bool is_li(std::uint32_t word, std::uint16_t value) {
    return (word & 0xFC1F0000u) == 0x38000000u && (word & 0xFFFFu) == value;
}

// The callback loaded into a register just before the probe:
// "lis rC,hi" then "addi rC,rC,lo".
std::uint32_t callback_before(const std::vector<CodeSpan>& spans, std::uint32_t probe) {
    for (std::size_t back = 1; back <= kNear; ++back) {
        std::uint32_t addi = 0;
        if (!word_at(spans, probe - 4 * back, addi)) return 0;
        const bool is_addi = (addi >> 26) == 14 && rd(addi) == ra(addi) && ra(addi) != 0;
        if (!is_addi) continue;
        const unsigned reg = rd(addi);
        for (std::size_t further = 1; further <= kNear; ++further) {
            std::uint32_t lis = 0;
            if (!word_at(spans, probe - 4 * (back + further), lis)) return 0;
            if ((lis & 0xFC1F0000u) == 0x3C000000u && rd(lis) == reg) {
                const std::int32_t low = static_cast<std::int16_t>(addi & 0xFFFF);
                return ((lis & 0xFFFFu) << 16) + static_cast<std::uint32_t>(low);
            }
        }
    }
    return 0;
}

}  // namespace

bool find_wpad_probe(const std::vector<CodeSpan>& spans, WpadProbePatch& out, std::string& why) {
    out = WpadProbePatch{};
    unsigned found = 0;
    for (const CodeSpan& s : spans) {
        for (std::size_t o = 0; o + 4 <= s.size; o += 4) {
            if (!is_li(be32(s.bytes + o), kProbeAddress)) continue;
            const std::uint32_t at = s.address + static_cast<std::uint32_t>(o);
            // The read report's number close by.
            bool report = false;
            for (std::size_t k = 1; k <= kNear && !report; ++k) {
                std::uint32_t w = 0;
                if (word_at(spans, at + 4 * k, w) && is_li(w, kReadReport)) report = true;
                if (word_at(spans, at - 4 * k, w) && is_li(w, kReadReport)) report = true;
            }
            if (!report) continue;
            ++found;
            if (out.probe == 0) out.probe = at;
        }
    }
    if (found != 1) {
        why = found == 0 ? "the firmware probe was not found" : "more than one firmware probe was found";
        return false;
    }
    out.handler = callback_before(spans, out.probe);
    if (out.handler == 0 || span_at(spans, out.handler) == nullptr) {
        why = "the probe's answer handler was not found";
        return false;
    }
    // In the handler: "cmpwi r4,-1" (no controller), then "mr rD,r4".
    bool checked = false;
    for (std::size_t k = 0; k < kNear; ++k) {
        std::uint32_t w = 0;
        if (!word_at(spans, out.handler + 4 * k, w)) break;
        if (w == 0x2C04FFFFu) checked = true;
        if (checked && (w & 0xFFE0FFFFu) == 0x7C802378u) {
            out.result = out.handler + 4 * static_cast<std::uint32_t>(k);
            return true;
        }
    }
    why = "the handler does not keep the result as expected";
    return false;
}

void apply_wpad_probe(const std::vector<CodeSpan>& spans, const WpadProbePatch& patch) {
    std::uint32_t probe = 0, result = 0;
    if (!word_at(spans, patch.probe, probe) || !word_at(spans, patch.result, result)) return;
    put_word(spans, patch.probe, (probe & 0xFFFF0000u) | kCalibrationAddress);
    put_word(spans, patch.result, 0x38000001u | (ra(result) << 21));  // li rD,1
}

namespace {

std::uint32_t be16(const std::uint8_t* p) { return (std::uint32_t(p[0]) << 8) | p[1]; }
std::uint32_t le16(const std::uint8_t* p) { return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8); }

// The Bluetooth core specification's names, for the events and commands
// a game's stack uses.
const char* hci_event_name(unsigned code) {
    switch (code) {
    case 0x01: return "Inquiry Complete";
    case 0x02: return "Inquiry Result";
    case 0x03: return "Connection Complete";
    case 0x04: return "Connection Request";
    case 0x05: return "Disconnection Complete";
    case 0x06: return "Authentication Complete";
    case 0x07: return "Remote Name Request Complete";
    case 0x08: return "Encryption Change";
    case 0x0B: return "Read Remote Supported Features Complete";
    case 0x0C: return "Read Remote Version Information Complete";
    case 0x0E: return "Command Complete";
    case 0x0F: return "Command Status";
    case 0x10: return "Hardware Error";
    case 0x12: return "Role Change";
    case 0x14: return "Mode Change";
    case 0x15: return "Return Link Keys";
    case 0x16: return "PIN Code Request";
    case 0x17: return "Link Key Request";
    case 0x18: return "Link Key Notification";
    case 0x1B: return "Max Slots Change";
    case 0x1D: return "Connection Packet Type Changed";
    case 0x20: return "Page Scan Repetition Mode Change";
    default: return nullptr;
    }
}

const char* hci_command_name(unsigned opcode) {
    switch (opcode) {
    case 0x0401: return "Inquiry";
    case 0x0405: return "Create Connection";
    case 0x0406: return "Disconnect";
    case 0x0409: return "Accept Connection Request";
    case 0x040A: return "Reject Connection Request";
    case 0x040B: return "Link Key Request Reply";
    case 0x040C: return "Link Key Request Negative Reply";
    case 0x040D: return "PIN Code Request Reply";
    case 0x040E: return "PIN Code Request Negative Reply";
    case 0x040F: return "Change Connection Packet Type";
    case 0x0411: return "Authentication Requested";
    case 0x0413: return "Set Connection Encryption";
    case 0x0419: return "Remote Name Request";
    case 0x041B: return "Read Remote Supported Features";
    case 0x041D: return "Read Remote Version Information";
    case 0x0803: return "Sniff Mode";
    case 0x0804: return "Exit Sniff Mode";
    case 0x080B: return "Switch Role";
    case 0x080D: return "Write Link Policy Settings";
    case 0x0C01: return "Set Event Mask";
    case 0x0C03: return "Reset";
    case 0x0C05: return "Set Event Filter";
    case 0x0C0D: return "Read Stored Link Key";
    case 0x0C11: return "Write Stored Link Key";
    case 0x0C12: return "Delete Stored Link Key";
    case 0x0C13: return "Write Local Name";
    case 0x0C16: return "Write Connection Accept Timeout";
    case 0x0C18: return "Write Page Timeout";
    case 0x0C1A: return "Write Scan Enable";
    case 0x0C1C: return "Write Page Scan Activity";
    case 0x0C1E: return "Write Inquiry Scan Activity";
    case 0x0C20: return "Write Authentication Enable";
    case 0x0C24: return "Write Class Of Device";
    case 0x0C33: return "Host Buffer Size";
    case 0x0C37: return "Write Link Supervision Timeout";
    case 0x1001: return "Read Local Version Information";
    case 0x1005: return "Read Buffer Size";
    case 0x1009: return "Read BD_ADDR";
    default: return nullptr;
    }
}

// Disconnection Complete's reason: who ended the link.
const char* disconnect_reason(unsigned reason) {
    switch (reason) {
    case 0x05: return "authentication failure";
    case 0x06: return "the link key is missing";
    case 0x08: return "the connection timed out";
    case 0x13: return "the controller ended it";
    case 0x14: return "the controller ended it (low resources)";
    case 0x15: return "the controller is powering off";
    case 0x16: return "the Wii (the game's Bluetooth stack) ended it";
    case 0x22: return "the link layer timed out";
    default: return nullptr;
    }
}

const char* l2cap_signal_name(unsigned code) {
    switch (code) {
    case 0x01: return "L2CAP Command Reject";
    case 0x02: return "L2CAP Connection Request";
    case 0x03: return "L2CAP Connection Response";
    case 0x04: return "L2CAP Configuration Request";
    case 0x05: return "L2CAP Configuration Response";
    case 0x06: return "L2CAP Disconnection Request";
    case 0x07: return "L2CAP Disconnection Response";
    case 0x0A: return "L2CAP Information Request";
    case 0x0B: return "L2CAP Information Response";
    default: return nullptr;
    }
}

// The Wii Remote's HID reports by their first two bytes.
const char* hid_report_name(unsigned prefix, unsigned id) {
    if (prefix == 0xA2) {
        switch (id) {
        case 0x10: return "rumble";
        case 0x11: return "player LEDs";
        case 0x12: return "reporting mode";
        case 0x13: return "IR camera enable";
        case 0x14: return "speaker enable";
        case 0x15: return "status request";
        case 0x16: return "memory write";
        case 0x17: return "memory read";
        case 0x18: return "speaker data";
        case 0x19: return "speaker mute";
        case 0x1A: return "IR camera enable 2";
        default: return nullptr;
        }
    }
    if (prefix == 0xA1) {
        if (id == 0x20) return "status";
        if (id == 0x21) return "memory read answer";
        if (id == 0x22) return "acknowledgement";
        if (id >= 0x30 && id <= 0x3F) return "data report";
    }
    return nullptr;
}

// What of an HCI packet is not printed: all of an address but its last
// byte (as the game's own messages are kept), and link keys and PINs
// entirely. `data` starts at the packet, `params` is where its
// parameters begin.
enum class Hide : std::uint8_t { None, Address, Secret };

void hide_address(std::vector<Hide>& hide, std::size_t at) {
    // HCI keeps an address least significant byte first: that one is the
    // "last byte" the log keeps.
    for (std::size_t i = at + 1; i < at + 6 && i < hide.size(); ++i) hide[i] = Hide::Address;
}

void hide_secret(std::vector<Hide>& hide, std::size_t at) {
    for (std::size_t i = at; i < hide.size(); ++i) hide[i] = Hide::Secret;
}

std::vector<Hide> hci_hidden(bool command, const std::uint8_t* data, std::size_t n) {
    std::vector<Hide> hide(n, Hide::None);
    if (n < 3) return hide;
    const std::size_t p = command ? 3 : 2;
    const unsigned code = command ? le16(data) : data[0];
    // A list of address and key pairs after a count.
    const auto pairs = [&]() {
        for (std::size_t at = p + 1; at < n; at += 22) {
            hide_address(hide, at);
            hide_secret(hide, at + 6);
        }
    };
    if (command) {
        if (code == 0x040B || code == 0x040D) {
            hide_address(hide, p);
            hide_secret(hide, p + 6);
        } else if (code == 0x0C11) {
            pairs();
        } else if (code == 0x0405 || code == 0x0409 || code == 0x040A || code == 0x040C || code == 0x040E ||
                   code == 0x0419 || code == 0x0C0D || code == 0x0C12) {
            hide_address(hide, p);
        }
        return hide;
    }
    if (code == 0x18) {
        hide_address(hide, p);
        hide_secret(hide, p + 6);
    } else if (code == 0x15) {
        pairs();
    } else if (code == 0x03) {
        hide_address(hide, p + 3);
    } else if (code == 0x07 || code == 0x12) {
        hide_address(hide, p + 1);
    } else if (code == 0x04 || code == 0x16 || code == 0x17) {
        hide_address(hide, p);
    } else if (code == 0x02) {
        hide_secret(hide, p);  // inquiry results: addresses of anything nearby
    }
    return hide;
}

std::string hex_bytes(const std::uint8_t* data, std::size_t n, const std::vector<Hide>& hide) {
    std::string out;
    char b[4];
    for (std::size_t i = 0; i < n; ++i) {
        if (i) out += ' ';
        const Hide h = i < hide.size() ? hide[i] : Hide::None;
        if (h == Hide::Address) {
            out += "xx";
        } else if (h == Hide::Secret) {
            out += "**";
        } else {
            std::snprintf(b, sizeof(b), "%02x", data[i]);
            out += b;
        }
    }
    return out;
}

// One traced packet as a line (after its time).
std::string bt_line(const std::uint8_t* e) {
    const unsigned kind = e[4], flags = e[5];
    const unsigned handle = be16(e + 6), size = be16(e + 8), cid = be16(e + 10);
    const std::uint8_t* d = e + 12;
    const std::size_t kept = size < RTWLOG_BT_BYTES ? size : RTWLOG_BT_BYTES;
    char head[160];
    std::string what;
    if (kind == RTWLOG_BT_COMMAND || kind == RTWLOG_BT_EVENT) {
        const bool command = kind == RTWLOG_BT_COMMAND;
        if (command) {
            const unsigned opcode = kept >= 2 ? le16(d) : 0;
            const char* name = hci_command_name(opcode);
            std::snprintf(head, sizeof(head), "HCI command 0x%04x%s%s%s", opcode, name ? " (" : "", name ? name : "",
                          name ? ")" : "");
        } else {
            const unsigned code = kept >= 1 ? d[0] : 0;
            const char* name = hci_event_name(code);
            std::snprintf(head, sizeof(head), "HCI event 0x%02x%s%s%s", code, name ? " (" : "", name ? name : "",
                          name ? ")" : "");
            if (code == 0x05 && kept >= 6) {
                const char* reason = disconnect_reason(d[5]);
                char more[128];
                std::snprintf(more, sizeof(more), ", handle 0x%04x, reason 0x%02x%s%s", le16(d + 3) & 0x0FFFu, d[5],
                              reason ? ": " : "", reason ? reason : "");
                what = more;
            } else if ((code == 0x0E && kept >= 5) || (code == 0x0F && kept >= 6)) {
                const unsigned opcode = le16(d + (code == 0x0E ? 3 : 4));
                const char* name2 = hci_command_name(opcode);
                const unsigned status = code == 0x0E ? (kept >= 6 ? d[5] : 0) : d[2];
                char more[128];
                std::snprintf(more, sizeof(more), ", for 0x%04x%s%s, status 0x%02x", opcode, name2 ? " " : "",
                              name2 ? name2 : "", status);
                what = more;
            } else if (code == 0x14 && kept >= 6) {
                char more[64];
                std::snprintf(more, sizeof(more), ", handle 0x%04x, %s", le16(d + 3) & 0x0FFFu,
                              d[5] == 0 ? "active" : d[5] == 2 ? "sniff" : d[5] == 1 ? "hold" : "park");
                what = more;
            }
        }
        return std::string(head) + what + ": " + hex_bytes(d, kept, hci_hidden(command, d, kept)) +
               (size > kept ? " ..." : "");
    }
    const char* way = kind == RTWLOG_BT_OUT ? "to  " : kind == RTWLOG_BT_IN ? "from" : "game";
    std::snprintf(head, sizeof(head), "%s 0x%04x cid 0x%04x, %u byte(s)", way, handle, cid, size);
    std::string line = head;
    if (cid == 0x0001 && kept >= 1) {
        const char* name = l2cap_signal_name(d[0]);
        if (name) line += std::string(" (") + name + ")";
    } else if (kept >= 2) {
        const char* name = hid_report_name(d[0], d[1]);
        if (name) line += std::string(" (") + name + ")";
    }
    line += ": " + hex_bytes(d, kept, {}) + (size > kept ? " ..." : "");
    if (flags & 2u) line += kind == RTWLOG_BT_OUT ? "  [changed by the translator]" : "  [as the translator rebuilt it]";
    return line;
}

}  // namespace

namespace {

// 256 bytes that hold every value once. Random data shows a repeat within
// a few dozen bytes, so most places are turned down at once.
bool is_permutation(const std::uint8_t* p) {
    std::uint32_t seen[8] = {};
    for (unsigned i = 0; i < 256; ++i) {
        const unsigned v = p[i];
        const std::uint32_t bit = 1u << (v & 31u);
        if (seen[v >> 5] & bit) return false;
        seen[v >> 5] |= bit;
    }
    return true;
}

}  // namespace

std::uint32_t find_extension_tables(const std::vector<CodeSpan>& spans) {
    constexpr std::size_t kAnswers = 48, kBox = 256, kBoxes = 9;
    for (const CodeSpan& s : spans) {
        if (s.size < kAnswers + kBox * kBoxes) continue;
        for (std::size_t at = kAnswers; at + kBox * kBoxes <= s.size; at += 4) {
            unsigned boxes = 0;
            while (boxes < kBoxes && is_permutation(s.bytes + at + kBox * boxes)) ++boxes;
            if (boxes == kBoxes) return s.address + static_cast<std::uint32_t>(at - kAnswers);
        }
    }
    return 0;
}

bool describe_wpad_log(const std::uint8_t* bytes, std::size_t size, std::string& text, std::string& error) {
    constexpr std::size_t kHeader = offsetof(rtwlog_file, entries);
    constexpr std::size_t kEntry = 152;
    static_assert(kHeader == 40, "the header layout the decoder reads");
    static_assert(sizeof(rtwlog_entry) == kEntry, "the entry layout the decoder reads");
    if (size < kHeader + kEntry * RTWLOG_ENTRIES || be32(bytes) != RTWLOG_MAGIC) {
        error = "not a Wii Remote log";
        return false;
    }
    const std::uint32_t version = be32(bytes + 4);
    if (version != RTWLOG_VERSION && version != 2) {
        error = "a Wii Remote log of another version (" + std::to_string(version) + ")";
        return false;
    }
    constexpr std::size_t kPro = kHeader + kEntry * RTWLOG_ENTRIES;
    constexpr std::size_t kBt = kPro + sizeof(rtwlog_pro);
    constexpr std::size_t kBtEntry = 28;
    static_assert(sizeof(rtwlog_bt) == kBtEntry, "the trace entry layout the decoder reads");
    static_assert(offsetof(rtwlog_file, pro) == kPro && offsetof(rtwlog_file, bt) == kBt, "the trace's place");
    const bool traced = version >= 3 && size >= kBt + kBtEntry * RTWLOG_BT_ENTRIES;
    const std::string game = text_field(bytes + 8, 6);
    const std::uint32_t ticks_per_second = be32(bytes + 16);
    const std::uint32_t total = be32(bytes + 20);
    const std::uint32_t stubs = be32(bytes + 24);
    const double units_per_second = ticks_per_second / 256.0;

    char line[160];
    std::snprintf(line, sizeof(line), "Wii Remote log: %s, %u message(s), %u print function(s) hooked\n",
                  game.c_str(), static_cast<unsigned>(total), static_cast<unsigned>(stubs));
    text = line;
    // The Pro Controller test (settings.txt wpad_probe = valid).
    const std::uint32_t probe = be32(bytes + 32), probe_at = be32(bytes + 36);
    if (probe == RTWLOG_PROBE_CHANGED) {
        std::snprintf(line, sizeof(line), "Pro Controller test: on, the firmware probe at 0x%08x reads 0x0016\n",
                      static_cast<unsigned>(probe_at));
        text += line;
    } else if (probe == RTWLOG_PROBE_NOT_FOUND) {
        text += "Pro Controller test: on, but the firmware probe was not found in this game; nothing changed\n";
    } else if (probe == RTWLOG_PROBE_PROTECTED) {
        text += "Pro Controller test: on, but this game checks its own code; nothing changed\n";
    } else {
        text += "Pro Controller test: off\n";
    }
    // The Bluetooth side (version 3): the translator's counts first.
    const std::uint8_t* pro = bytes + kPro;
    const auto pro_word = [&](std::size_t field) { return be32(pro + 4 * field); };
    if (traced && pro_word(1) != 0) {
        if (pro_word(0)) {
            char counts[320];
            std::snprintf(counts, sizeof(counts),
                          "Pro Controller translator: on; %u Pro Controller link(s), %u report(s) rebuilt, %u "
                          "extension ID(s) answered as a Classic Controller Pro, %u calibration read(s) answered, "
                          "%u acknowledgement error(s) cleared, %u reporting mode(s) changed\n",
                          pro_word(2), pro_word(3), pro_word(4), pro_word(5), pro_word(6), pro_word(7));
            text += counts;
            if (pro_word(8)) {
                std::snprintf(counts, sizeof(counts),
                              "Pro Controller translator: the game turned on encryption; %u key(s) made into tables, "
                              "%u did not check out\n",
                              pro_word(12), pro_word(13));
                text += counts;
            }
            if (pro_word(14)) {
                std::snprintf(counts, sizeof(counts),
                              "Pro Controller translator: the game's encryption tables at 0x%08x\n", pro_word(14));
                text += counts;
            } else {
                text += "Pro Controller translator: the game's encryption tables were not found\n";
            }
            if (pro_word(15)) text += "Pro Controller translator: the Pro sent its bytes encrypted anyway\n";
        } else {
            text += "Pro Controller translator: off (settings.txt pro_controller = off); the trace only\n";
        }
    } else if (traced) {
        text += "Bluetooth trace: off (the game's IOS_IoctlvAsync was not hooked)\n";
    }
    if (total == 0) {
        text += "(the game printed nothing about its controllers)\n";
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
    if (!traced || pro_word(1) == 0) return true;

    // The Bluetooth trace, on the messages' clock.
    const std::uint32_t bt_total = pro_word(9);
    const auto bt_at = [&](std::uint32_t n) { return bytes + kBt + kBtEntry * rtwlog_bt_slot(n); };
    std::snprintf(line, sizeof(line),
                  "\nBluetooth trace: %u packet(s); %u data report(s) and %u Number Of Completed Packets event(s) "
                  "only counted\n",
                  static_cast<unsigned>(bt_total), pro_word(10), pro_word(11));
    text += line;
    if (bt_total == 0) return true;
    const std::uint32_t clock_start = total > 0 ? be32(entry_at(0)) : be32(bt_at(0));
    const auto write_packet = [&](std::uint32_t n) {
        const std::uint8_t* e = bt_at(n);
        const double seconds =
            units_per_second > 0 ? static_cast<std::int32_t>(be32(e) - clock_start) / units_per_second : 0;
        std::snprintf(line, sizeof(line), "[%8.3f] ", seconds);
        text += line;
        text += bt_line(e);
        text += "\n";
    };
    const std::uint32_t first_bt = bt_total < RTWLOG_BT_FIRST ? bt_total : RTWLOG_BT_FIRST;
    for (std::uint32_t n = 0; n < first_bt; ++n) write_packet(n);
    if (bt_total > RTWLOG_BT_FIRST) {
        const std::uint32_t last_start =
            bt_total - RTWLOG_BT_LAST > RTWLOG_BT_FIRST ? bt_total - RTWLOG_BT_LAST : RTWLOG_BT_FIRST;
        if (last_start > RTWLOG_BT_FIRST) {
            text += "... " + std::to_string(last_start - RTWLOG_BT_FIRST) + " packet(s) not kept ...\n";
        }
        for (std::uint32_t n = last_start; n < bt_total; ++n) write_packet(n);
    }
    return true;
}

}  // namespace riftwii
