// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: FIX94 and the Nintendont contributors, used with permission
// SPDX-License-Identifier: GPL-3.0-or-later
#include "hidpads.hpp"

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <atomic>
#include <ogc/usb.h>
#include <unistd.h>

#include "riftwii/hidconfig.hpp"
#include "loadersettings.hpp"
#include "log.hpp"
#include "pentablet.h"

namespace riftwii::wii {
namespace {

rthid_catalog g_catalog{};
std::atomic<unsigned> g_published_count{0};
// Scans run serially on the menu/loader side. Keep the bounded file
// buffer off the GUI thread's 24 KiB stack, including during preload.
char g_text[kHidConfigMaxBytes + 1];
// One attempt per plugged-in device, including missing/bad configurations.
rthid_device g_checked[RTHID_DEVICE_MAX]{};
unsigned g_checked_count = 0;
bool g_preloaded = false;

void StoreMapping(const rthid_mapping& mapping) {
    if (g_catalog.count >= RTHID_CONFIG_MAX) return;
    g_catalog.maps[g_catalog.count] = mapping;
    ++g_catalog.count;
    g_published_count.store(g_catalog.count, std::memory_order_release);
}

bool LoadConfig(std::uint32_t id, rthid_mapping& mapping) {
    char paths[3][96];
    const unsigned vid = id >> 16;
    const unsigned pid = id & 65535u;
    std::snprintf(paths[0], sizeof(paths[0]), "sd:/riftwii/controllers/%04X_%04X.ini", vid, pid);
    std::snprintf(paths[1], sizeof(paths[1]), "sd:/controllers/%04X_%04X.ini", vid, pid);
    std::snprintf(paths[2], sizeof(paths[2]), "sd:/controller.ini");
    for (unsigned i = 0; i < 3; ++i) {
        FILE* file = std::fopen(paths[i], "rb");
        if (!file) {
            // FAT is case insensitive on a Wii. Keep the same behaviour
            // for case-sensitive development cards and network mounts.
            if (i == 2) continue;
            std::snprintf(paths[i], sizeof(paths[i]), i == 0 ? "sd:/riftwii/controllers/%04x_%04x.ini"
                                                             : "sd:/controllers/%04x_%04x.ini", vid, pid);
            file = std::fopen(paths[i], "rb");
            if (!file) continue;
        }
        const std::size_t size = std::fread(g_text, 1, sizeof(g_text), file);
        const bool read_error = std::ferror(file) != 0;
        std::fclose(file);
        std::string error;
        if (read_error || !parse_hid_config(std::string_view(g_text, size), mapping, error)) {
            logf("USB controller %04x:%04x: cannot use %s: %s\n", vid, pid, paths[i],
                 read_error ? "The file could not be read." : error.c_str());
            // A higher-priority file that exists but is broken must not
            // silently select a different mapping from a lower folder.
            return false;
        }
        if ((((std::uint32_t)mapping.vid << 16) | mapping.pid) != id) {
            if (i != 2) logf("USB controller %04x:%04x: %s names a different device.\n", vid, pid, paths[i]);
            return false;
        }
        logf("USB controller %04x:%04x: using %s.\n", vid, pid, paths[i]);
        return true;
    }
    return false;
}

}  // namespace

bool HidPadsConfigured(std::uint32_t vid_pid) {
    return Settings().usb_input == "on" && rthid_find(&g_catalog, vid_pid) != nullptr;
}

void HidPadsRemember(const rthid_device* devices, unsigned count) {
    if (count > RTHID_DEVICE_MAX) count = RTHID_DEVICE_MAX;
    g_catalog.known_count = count;
    for (unsigned i = 0; i < count; ++i) g_catalog.known[i] = devices[i];
    rthid_device checked[RTHID_DEVICE_MAX];
    unsigned checked_count = 0;
    for (unsigned i = 0; i < count; ++i) {
        const std::uint32_t id = devices[i].vid_pid;
        if (id == 0x057E0337u || Settings().usb_input != "on") continue;
        bool attempted = false;
        for (unsigned j = 0; j < g_checked_count; ++j) {
            if (g_checked[j].id == devices[i].id && g_checked[j].vid_pid == id) attempted = true;
        }
        checked[checked_count++] = devices[i];
        if (attempted || rthid_find(&g_catalog, id)) continue;
        rthid_mapping mapping{};
        if (LoadConfig(id, mapping)) {
            if (g_catalog.count == RTHID_CONFIG_MAX) {
                logf("USB controller %04x:%04x: the limit of %u controller mappings was reached.\n",
                     static_cast<unsigned>(id >> 16), static_cast<unsigned>(id & 65535u), RTHID_CONFIG_MAX);
            } else {
                StoreMapping(mapping);
            }
        } else {
            logf("USB controller %04x:%04x: no usable config; put one in sd:/riftwii/controllers/%04X_%04X.ini.\n",
                 static_cast<unsigned>(id >> 16), static_cast<unsigned>(id & 65535u),
                 static_cast<unsigned>(id >> 16), static_cast<unsigned>(id & 65535u));
        }
    }
    std::memcpy(g_checked, checked, checked_count * sizeof(checked[0]));
    g_checked_count = checked_count;
}

const rthid_catalog& HidPadsCatalog() {
    return g_catalog;
}

void HidPadsCopyCatalog(rthid_catalog& out) {
    out = g_catalog;
    out.adapter_disabled = Settings().gc_adapter == "off" ? 1 : 0;
    if (Settings().usb_input != "on") out.count = 0;
}

void HidPadsPreload() {
    if (g_preloaded || Settings().usb_input == "off") return;
    g_preloaded = true;
    // A headless launch may reach here before any menu or adapter scan.
    // Use the unfiltered list so the keyboard/tablet filter cannot hide pads.
    static usb_device_entry list[RTHID_DEVICE_MAX] __attribute__((aligned(32)));
    std::uint8_t count = 0;
    std::int32_t result = USB_GetDeviceListUnfiltered(list, RTHID_DEVICE_MAX, USB_CLASS_HID, &count);
    if (result < 0 && USB_Initialize() >= 0) {
        // As in the adapter probe, allow libogc's first device-change reply.
        for (unsigned attempt = 0; attempt < 30; ++attempt) {
            result = USB_GetDeviceListUnfiltered(list, RTHID_DEVICE_MAX, USB_CLASS_HID, &count);
            if (result < 0 || count != 0) break;
            usleep(10000);
        }
    }
    if (result >= 0) {
        if (count > RTHID_DEVICE_MAX) count = RTHID_DEVICE_MAX;
        rthid_device devices[RTHID_DEVICE_MAX];
        for (unsigned i = 0; i < count; ++i) {
            devices[i] = {list[i].device_id, (static_cast<std::uint32_t>(list[i].vid) << 16) | list[i].pid};
        }
        HidPadsRemember(devices, count);
    }
    // Connected pads now have their mappings. Fill remaining slots for
    // in-game hotplug, with a bounded directory walk and no path from an
    // untrusted filename ever passed to fopen.
    const char* folders[] = {"sd:/riftwii/controllers", "sd:/controllers"};
    for (const char* folder : folders) {
        DIR* directory = opendir(folder);
        if (!directory) continue;
        unsigned visited = 0;
        while (g_catalog.count < RTHID_CONFIG_MAX && visited++ < 512) {
            const dirent* entry = readdir(directory);
            if (!entry) break;
            if (std::strlen(entry->d_name) != 13 || entry->d_name[4] != '_' ||
                entry->d_name[9] != '.' ||
                (entry->d_name[10] != 'i' && entry->d_name[10] != 'I') ||
                (entry->d_name[11] != 'n' && entry->d_name[11] != 'N') ||
                (entry->d_name[12] != 'i' && entry->d_name[12] != 'I')) continue;
            std::uint32_t id = 0;
            bool valid = true;
            for (unsigned i = 0; i < 9; ++i) {
                if (i == 4) continue;
                const char c = entry->d_name[i];
                unsigned digit = 16;
                if (c >= '0' && c <= '9') digit = c - '0';
                if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
                if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
                if (digit == 16) {
                    valid = false;
                    break;
                }
                id = (id << 4) | digit;
            }
            if (!valid || id == 0x057E0337u || rthid_find(&g_catalog, id)) continue;
            rthid_mapping mapping{};
            if (LoadConfig(id, mapping)) StoreMapping(mapping);
        }
        closedir(directory);
    }
    // The single-device file can describe a pad that is currently absent.
    if (g_catalog.count < RTHID_CONFIG_MAX) {
        FILE* file = std::fopen("sd:/controller.ini", "rb");
        if (!file) return;
        const std::size_t bytes = std::fread(g_text, 1, sizeof(g_text), file);
        const bool read_error = std::ferror(file) != 0;
        std::fclose(file);
        rthid_mapping mapping{};
        std::string error;
        if (!read_error && parse_hid_config(std::string_view(g_text, bytes), mapping, error)) {
            const std::uint32_t id = (static_cast<std::uint32_t>(mapping.vid) << 16) | mapping.pid;
            if (id != 0x057E0337u && !rthid_find(&g_catalog, id) && LoadConfig(id, mapping)) {
                StoreMapping(mapping);
            }
        }
    }
}

bool HidPadsReserved(std::uint32_t id) {
    // The USB scanner may call this on another thread. Published entries
    // never change; acquiring the count makes their earlier writes visible.
    const unsigned count = g_published_count.load(std::memory_order_acquire);
    for (unsigned i = 0; i < count; ++i) {
        const auto& mapping = g_catalog.maps[i];
        if (((static_cast<std::uint32_t>(mapping.vid) << 16) | mapping.pid) == id) return true;
    }
    return false;
}

}  // namespace riftwii::wii

extern "C" int riftwii_hid_pad_configured(std::uint16_t vid, std::uint16_t pid) {
    return riftwii::wii::HidPadsReserved((static_cast<std::uint32_t>(vid) << 16) | pid) ? 1 : 0;
}
