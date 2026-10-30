// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the real mapping loader with a full config folder and a connected
// pad whose only mapping is controller.ini, outside the directory preload.
#include "hidpads.hpp"
#include "loadersettings.hpp"
#include "pentablet.h"

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
    CHECK(riftwii::wii::HidPadsCatalog().count == 0);
    if (needs_initialize && list_calls == 1) return -4;
    if (needs_initialize && list_calls == 2) {
        *count = 0;
        return 0;
    }
    entries[0] = {42, 0x1234, 0x5678};
    entries[1] = {43, 0x057E, 0x0337};  // adapter has its own driver
    *count = 2;
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
    const rthid_catalog& catalog = riftwii::wii::HidPadsCatalog();
    if (disabled) {
        CHECK(list_calls == 0);
        CHECK(catalog.count == 0);
        CHECK(catalog.known_count == 0);
    } else {
        CHECK(list_calls == (needs_initialize ? 3u : 1u));
        CHECK(initialize_calls == (needs_initialize ? 1u : 0u));
        CHECK(catalog.count == RTHID_CONFIG_MAX);
        CHECK(catalog.maps[0].vid == 0x1234);
        CHECK(catalog.maps[0].pid == 0x5678);
        CHECK(rthid_find(&catalog, 0x12345678) != nullptr);
        CHECK(rthid_find(&catalog, 0x057E0337) == nullptr);
        CHECK(catalog.known_count == 2);
        CHECK(catalog.known[0].id == 42);
        CHECK(catalog.known[0].vid_pid == 0x12345678);
        rthid_catalog copied{};
        riftwii::wii::HidPadsCopyCatalog(copied);
        CHECK(copied.adapter_disabled == 1);
        CHECK(copied.count == RTHID_CONFIG_MAX);
    }
    const unsigned previous_calls = list_calls;
    riftwii::wii::HidPadsPreload();
    CHECK(list_calls == previous_calls);
    if (failures == 0) std::printf("hidpads %s: all passed\n", argv[1]);
    return failures == 0 ? 0 : 1;
}
