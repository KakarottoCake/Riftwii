// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the real mapping loader with a full config folder and a connected
// pad whose only mapping is controller.ini, outside the directory preload.
// Launch hooks and automatic diagnostics require a connected mapped pad.
#include "hidpads.hpp"
#include "loadersettings.hpp"
#include "pentablet.h"
#include "wpadhook.hpp"

#include <ogc/usb.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

namespace {

int failures = 0;
unsigned list_calls = 0;
unsigned initialize_calls = 0;
bool needs_initialize = false;
bool configs_loaded = false;
bool list_failed = false;
usb_device_entry listed_devices[2]{};
std::uint8_t listed_count = 0;
riftwii::LoaderSettings settings;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::printf("FAILED: %s at line %d\n", #condition, __LINE__); \
            ++failures; \
        } \
    } while (0)

bool WriteConfig(const char* path, unsigned vid, unsigned pid) {
    FILE* file = std::fopen(path, "wb");
    if (!file) return false;
    const bool written = std::fprintf(file, "VID=%04X\nPID=%04X\nPolltype=1\nA=0,01\n", vid, pid) > 0;
    return std::fclose(file) == 0 && written;
}

}  // namespace

namespace riftwii::wii {

LoaderSettings& Settings() { return settings; }
void logf(const char*, ...) {}

}  // namespace riftwii::wii

std::int32_t USB_GetDeviceListUnfiltered(usb_device_entry* entries, std::uint8_t capacity,
                                       std::uint8_t device_class, std::uint8_t* count) {
    ++list_calls;
    CHECK(capacity == RTHID_DEVICE_MAX);
    CHECK(device_class == USB_CLASS_HID);
    // No absent mapping may consume a slot before the connected list is read.
    if (!configs_loaded) CHECK(riftwii::wii::HidPadsCatalog().count == 0);
    if (list_failed) return -4;
    if (needs_initialize && list_calls == 1) return -4;
    if (needs_initialize && list_calls == 2) {
        *count = 0;
        return 0;
    }
    for (unsigned i = 0; i < listed_count; ++i) entries[i] = listed_devices[i];
    *count = listed_count;
    return 0;
}

std::int32_t USB_Initialize(void) {
    ++initialize_calls;
    return 0;
}

int main(int argc, char** argv) {
    if (argc != 3) return 1;
    const bool disabled = std::string(argv[1]) == "off";
    needs_initialize = std::string(argv[1]) == "initialize";
    const bool connected = std::string(argv[1]) == "connected" || needs_initialize;
    if (connected) {
        listed_devices[0] = {42, 0x1234, 0x5678};
        listed_devices[1] = {43, 0x057E, 0x0337};
        listed_count = 2;
    } else if (std::string(argv[1]) == "unmapped") {
        listed_devices[0] = {44, 0x9999, 0x8888};
        listed_count = 1;
    } else if (std::string(argv[1]) == "adapter") {
        listed_devices[0] = {43, 0x057E, 0x0337};
        listed_count = 1;
    }
    settings.usb_input = disabled ? "off" : "on";
    settings.gc_adapter = "off";

    std::error_code error;
    const std::filesystem::path folder = argv[2];
    std::filesystem::create_directories(folder / "sd:/riftwii/controllers", error);
    CHECK(!error);
    if (error) return 1;
    std::filesystem::current_path(folder, error);
    CHECK(!error);
    if (error) return 1;
    for (unsigned pid = 1; pid <= RTHID_CONFIG_MAX; ++pid) {
        char path[96];
        std::snprintf(path, sizeof(path), "sd:/riftwii/controllers/ABCD_%04X.ini", pid);
        CHECK(WriteConfig(path, 0xABCD, pid));
    }
    CHECK(WriteConfig("sd:/controller.ini", 0x1234, 0x5678));

    riftwii::wii::HidPadsPreload();
    configs_loaded = true;
    const rthid_catalog& catalog = riftwii::wii::HidPadsCatalog();
    CHECK(riftwii::wii::HidPadsWanted() == !disabled);
    // This is the launch's USB driver gate. Configs, unrelated devices,
    // and a real adapter alone do not request the driver for mapped pads.
    CHECK(riftwii::wii::HidPadsConnected() == connected);
    std::string why;
    CHECK(riftwii::wii::wpad_log_wanted(0, true, why) == connected);
    if (connected) CHECK(why.find("plugged in at launch") != std::string::npos);
    CHECK(!riftwii::wii::wpad_log_wanted(0, false, why));
    settings.other["wpad_log"] = "off";
    CHECK(!riftwii::wii::wpad_log_wanted(0, true, why));
    settings.other["wpad_log"] = "on";
    CHECK(riftwii::wii::wpad_log_wanted(0, false, why));
    settings.other.erase("wpad_log");
    if (disabled) {
        CHECK(list_calls == 0);
        CHECK(catalog.count == 0);
        CHECK(catalog.known_count == 0);
    } else {
        CHECK(list_calls == (needs_initialize ? 3u : 1u));
        CHECK(initialize_calls == (needs_initialize ? 1u : 0u));
        CHECK(catalog.count == RTHID_CONFIG_MAX);
        if (connected) {
            CHECK(catalog.maps[0].vid == 0x1234);
            CHECK(catalog.maps[0].pid == 0x5678);
            CHECK(rthid_find(&catalog, 0x12345678) != nullptr);
            CHECK(catalog.known[0].id == 42);
            CHECK(catalog.known[0].vid_pid == 0x12345678);
        }
        CHECK(rthid_find(&catalog, 0x057E0337) == nullptr);
        CHECK(catalog.known_count == listed_count);
        rthid_catalog copied{};
        riftwii::wii::HidPadsCopyCatalog(copied);
        CHECK(copied.adapter_disabled == 1);
        CHECK(copied.count == RTHID_CONFIG_MAX);
    }
    const unsigned previous_calls = list_calls;
    riftwii::wii::HidPadsPreload();
    CHECK(list_calls == previous_calls);
    if (!disabled) {
        // Refresh at launch even if the menu loaded the folder earlier.
        listed_devices[0] = {45, catalog.maps[0].vid, catalog.maps[0].pid};
        listed_count = 1;
        riftwii::wii::HidPadsPreload(true);
        CHECK(riftwii::wii::HidPadsConnected());
        CHECK(riftwii::wii::wpad_log_wanted(0, true, why));

        listed_count = 0;
        riftwii::wii::HidPadsPreload(true);
        CHECK(riftwii::wii::HidPadsWanted());
        CHECK(!riftwii::wii::HidPadsConnected());
        CHECK(!riftwii::wii::wpad_log_wanted(0, true, why));
        CHECK(!riftwii::wii::wpad_log_wanted(1, true, why));

        listed_count = 1;
        riftwii::wii::HidPadsPreload(true);
        CHECK(riftwii::wii::HidPadsConnected());
        list_failed = true;
        riftwii::wii::HidPadsPreload(true);
        CHECK(!riftwii::wii::HidPadsConnected());
        CHECK(!riftwii::wii::wpad_log_wanted(0, true, why));

        // Turning USB input off also overrides a previously remembered pad.
        const rthid_device device = {45, (static_cast<std::uint32_t>(catalog.maps[0].vid) << 16) |
                                             catalog.maps[0].pid};
        riftwii::wii::HidPadsRemember(&device, 1);
        settings.usb_input = "off";
        CHECK(!riftwii::wii::HidPadsConnected());
        CHECK(!riftwii::wii::wpad_log_wanted(0, true, why));
    }
    riftwii::wii::note_wiiu_pro_seen();
    CHECK(riftwii::wii::wpad_log_wanted(0, false, why));
    if (failures == 0) std::printf("hidpads %s: all passed\n", argv[1]);
    return failures == 0 ? 0 : 1;
}
