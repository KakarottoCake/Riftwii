// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "usbprobe.hpp"

#include <fat.h>
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/usb.h>
#include <sdcard/wiisd_io.h>
#include <sys/stat.h>

#include <unistd.h>

#include <cstdio>
#include <vector>

#include "boot.hpp"
#include "gcadapter.hpp"
#include "ios_reload.hpp"
#include "log.hpp"
#include "menuios.hpp"
#include "padhook.hpp"
#include "usbcatalog.hpp"

namespace riftwii::wii {
namespace {

constexpr const char* kPortFile = "sd:/riftwii/usbcheck_port.txt";
constexpr const char* kResultFile = "sd:/riftwii/usbcheck.txt";

// The SD card and the log are let go around the reload, as StartMenuIos
// does.
bool Reload(int ios, bool sd_mounted, std::string& error) {
    if (sd_mounted) {
        LogClose();
        fatUnmount("sd:");
        __io_wiisd.shutdown();
    }
    const ReloadResult r = reload_ios(ios, error, true);
    if (r == ReloadResult::Terminal) halt_after_terminal_reload();
    if (sd_mounted && __io_wiisd.startup() && __io_wiisd.isInserted() && fatMountSimple("sd", &__io_wiisd)) LogReopen();
    forget_usb_hid_stuck();  // a request left unanswered went with the old IOS
    return (r == ReloadResult::Ok || r == ReloadResult::AlreadyRunning) && IOS_GetVersion() == ios;
}

// What the running IOS's USB shows: the device list (with the adapter or
// not) and how /dev/usb/hid answers. True when the adapter is listed.
bool LookHere(std::string& line) {
    std::string how;
    // As a launch does: our own /dev/usb/hid first (look_for_gc_adapter),
    // then closed without a Shutdown.
    const AdapterSeen seen = look_for_gc_adapter(how);  // waits for devices just after the reload
    close_adapter_session();
    USB_Deinitialize();
    line = "adapter " + how;
    // With libogc's USB shut, as at a launch: how this IOS's USB answers
    // our own requests, then the game's driver on our own handle for up
    // to 3 s (each step it takes lands in session.log).
    line += "\n    open matrix: " + usb_open_matrix();
    std::string why;
    if (!GcAdapterStart(why, true)) {
        line += "\n    as a game: did not start: " + why;
    } else {
        GcAdapterView view;
        const u64 start = gettime();
        while (diff_msec(start, gettime()) < 3000) {
            GcAdapterPoll(view);
            if (view.link == GCAD_LINK_POLL && view.reports >= 30) break;
            usleep(20000);
        }
        unsigned ports = 0;
        for (unsigned p = 0; p < GCAD_PORTS; ++p)
            if (view.present[p]) ++ports;
        line += "\n    as a game: " + std::string(view.link == GCAD_LINK_POLL && view.reports > 0 ? "WORKS" : "FAILS") +
                " after " + std::to_string(diff_msec(start, gettime())) + " ms, " + std::to_string(ports) +
                " controller(s) seen; " + GcAdapterDiag();
        GcAdapterStop();
    }
    close_adapter_session();
    forget_usb_hid_stuck();
    return seen == AdapterSeen::Found;
}

std::string FirstLine(const char* path) {
    std::string text;
    if (FILE* f = std::fopen(path, "rb")) {
        char buf[32];
        if (std::fgets(buf, sizeof(buf), f)) text = buf;
        std::fclose(f);
    }
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    return text;
}

}  // namespace

void SaveUsbCheckPort(const char* port) {
    mkdir("sd:/riftwii", 0777);
    if (FILE* f = std::fopen(kPortFile, "wb")) {
        std::fprintf(f, "%s\n", port);
        std::fclose(f);
    }
}

std::string RunUsbCheck(bool sd_mounted) {
    const std::string port = sd_mounted ? FirstLine(kPortFile) : std::string();
    std::remove(kPortFile);
    std::string report = std::string("RiftWii USB check (") + RIFTWII_VERSION + "), " +
                         (is_wii_u() ? "Wii U" : "Wii") + ", adapter in a " +
                         (port.empty() ? std::string("port not given") : port + " port") + "\n";
    std::string seenBy, missedBy;
    std::vector<int> slots{58};
    for (int slot : MenuIosChoices())
        if (slot != 0) slots.push_back(slot);
    for (int ios : slots) {
        std::string line, error;
        bool found = false;
        if (ios != IOS_GetVersion() && !Reload(ios, sd_mounted, error)) {
            line = "could not be loaded (" + error + "), running IOS" + std::to_string(IOS_GetVersion());
        } else {
            found = LookHere(line);
            const int base = ios == 58 ? 0 : d2x_base(ios);
            line = "rev " + std::to_string(IOS_GetRevision()) + (base ? " (d2x base " + std::to_string(base) + ")" : "") +
                   ": " + line;
        }
        const std::string name = "IOS" + std::to_string(ios);
        logf("USB check: %s %s\n", name.c_str(), line.c_str());
        report += name + " " + line + "\n";
        std::string& list = found ? seenBy : missedBy;
        list += (list.empty() ? "" : ", ") + std::to_string(ios);
    }
    if (IOS_GetVersion() != 58) {
        std::string error;
        if (!Reload(58, sd_mounted, error)) logf("USB check: back to IOS58 failed (%s)\n", error.c_str());
    }
    USB_Deinitialize();  // the menu's USB starts afresh
    if (sd_mounted) {
        if (FILE* f = std::fopen(kResultFile, "wb")) {
            std::fputs(report.c_str(), f);
            std::fclose(f);
        }
    }
    return "USB check (" + (port.empty() ? std::string("adapter") : port + " port") + "): IOS " +
           (seenBy.empty() ? std::string("none") : seenBy) + " found the adapter" +
           (missedBy.empty() ? std::string() : ", IOS " + missedBy + " did not") +
           ". Send a problem report so the developers see the details.";
}

}  // namespace riftwii::wii
