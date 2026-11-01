// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: USB Loader GX contributors <https://github.com/wiidev/usbloadergx>
// SPDX-FileCopyrightText: WiiFlow Lite contributors <https://github.com/Fledge68/WiiFlow_Lite>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gecko.hpp"

#include <gccore.h>
#include <ogc/exi.h>
#include <ogc/irq.h>
#include <ogc/usbgecko.h>
#include <sys/iosupport.h>

#include <cstdarg>
#include <cstdio>

namespace riftwii::wii {
namespace {

constexpr s32 kSlotB = EXI_CHANNEL_1;
// How often the send asks the USB Gecko whether it can take a byte, for
// each byte of a line, before it gives up: plenty for a PC that reads. A
// USB Gecko nobody reads (no terminal open on the PC) never can, and the
// unbounded send waited for it with interrupts off, freezing the Wii.
constexpr int kPollsPerByte = 64;
bool g_active = false;
// Where stdout and stderr wrote before the USB Gecko took them (the text
// console, or nowhere): they still write there, so the screen keeps its
// text.
const devoptab_t* g_screen_out = nullptr;
// While logf prints its own line to the screen: logf sends that line to
// the USB Gecko itself, with its time.
bool g_leave_out_stdout = false;

void send(const char* text, std::size_t length) {
    if (!g_active || text == nullptr || length == 0) return;
    // With interrupts off no other thread's line lands in the middle of
    // this one (WiiFlow Lite's __out_write).
    const int size = static_cast<int>(length);
    const u32 level = IRQ_Disable();
    const int sent = usb_sendbuffer_safe_ex(kSlotB, text, size, size * kPollsPerByte);
    IRQ_Restore(level);
    // Nothing on the PC reads: the log stops here instead of stalling the
    // menu on every line after this one.
    if (sent < size) g_active = false;
}

ssize_t write_out(struct _reent* reent, void* fd, const char* text, size_t length) {
    if (!g_leave_out_stdout) send(text, length);
    if (g_screen_out && g_screen_out->write_r) return g_screen_out->write_r(reent, fd, text, length);
    return static_cast<ssize_t>(length);
}

// stdout and stderr as a device that only writes, to the USB Gecko.
// Every other entry is left empty, so it stays right whatever devkitPPC's
// devoptab_t adds next.
devoptab_t make_gecko_out() {
    devoptab_t device{};
    device.name = "stdout";
    device.write_r = write_out;
    return device;
}
const devoptab_t g_gecko_out = make_gecko_out();

void take_stdout() {
    if (devoptab_list[STD_OUT] != &g_gecko_out) g_screen_out = devoptab_list[STD_OUT];
    devoptab_list[STD_OUT] = &g_gecko_out;
    devoptab_list[STD_ERR] = &g_gecko_out;
}

char printable(unsigned char c) {
    return (c < 0x20 || c > 0x7E) ? '.' : static_cast<char>(c);
}

}  // namespace

bool GeckoInSlot(int slot) {
    return usb_isgeckoalive(slot) != 0;
}

bool GeckoStart() {
    if (!GeckoInSlot(kSlotB)) return false;
    usb_flush(kSlotB);
    g_active = true;
    take_stdout();
    // Unbuffered: a line printed just before a hang still reaches the PC.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    return true;
}

bool GeckoActive() {
    return g_active;
}

void GeckoWrite(const char* text, std::size_t length) {
    send(text, length);
}

void GeckoRetakeStdout() {
    if (g_active) take_stdout();
}

void GeckoLeaveOutStdout(bool leave_out) {
    g_leave_out_stdout = leave_out;
}

void GeckoPrintf(const char* format, ...) {
    if (!g_active) return;
    char text[1024];
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (written <= 0) return;
    // vsnprintf counts what it would have written; a long line is cut.
    const std::size_t wanted = static_cast<std::size_t>(written);
    const std::size_t length = wanted < sizeof(text) ? wanted : sizeof(text) - 1;
    send(text, length);
}

void GeckoHexDump(const void* data, std::size_t length) {
    if (!g_active || data == nullptr) return;
    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    GeckoPrintf("\n       0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F  0123456789ABCDEF");
    GeckoPrintf("\n====  ===============================================  ================\n");
    for (std::size_t row = 0; row < length; row += 16) {
        // One row a send: 6 + 48 + 1 + 16 + 1 characters.
        char line[80];
        int used = std::snprintf(line, sizeof(line), "%04x  ", static_cast<unsigned>(row));
        for (std::size_t i = 0; i < 16; ++i) {
            if (row + i < length) used += std::snprintf(line + used, sizeof(line) - used, "%02x ", bytes[row + i]);
            else used += std::snprintf(line + used, sizeof(line) - used, "   ");
        }
        line[used++] = ' ';
        for (std::size_t i = 0; i < 16; ++i) line[used++] = row + i < length ? printable(bytes[row + i]) : ' ';
        line[used++] = '\n';
        send(line, static_cast<std::size_t>(used));
    }
}

}  // namespace riftwii::wii
