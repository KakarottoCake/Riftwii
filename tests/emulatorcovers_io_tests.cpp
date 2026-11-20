// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "emulatorcovers.hpp"

#include <ogc/mutex.h>
#include <sys/stat.h>

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <zlib.h>

#include "loadersettings.hpp"
#include "online.hpp"
#include "textfile.hpp"
#include "riftwii/emulatorart.hpp"
#include "riftwii/emulatorbanner.hpp"
#include "riftwii/imagevolume.hpp"
#include "riftwii/pngencode.hpp"
#include "riftwii/pngdecode.hpp"

namespace {
namespace fs = std::filesystem;
using namespace riftwii;
using namespace riftwii::wii;

int failures = 0;
unsigned api_calls = 0;
unsigned image_calls = 0;
std::string logs;
std::vector<std::uint8_t> picture;
const std::string key = "Example_0123456789-CaseSensitive";
const std::string found = "emu-" + std::string(64, 'a');
const std::string missing = "emu-" + std::string(64, 'b');
const std::string libretro_missing = "emu-" + std::string(64, 'c');
const std::string transient = "emu-" + std::string(64, 'd');

#define EXPECT(condition) do { if (!(condition)) { std::cerr << __LINE__ << ": " #condition "\n"; ++failures; } } while (0)

bool inflate_png(const std::uint8_t* data, std::size_t size, std::uint8_t* out, std::size_t out_size) {
    uLongf written = out_size;
    return uncompress(out, &written, data, size) == Z_OK && written == out_size;
}

void configure() {
    Settings().online = true;
    Settings().emulator_covers = true;
    Settings().steamgrid_covers = true;
    EmulatorCatalog catalog;
    EmulatorAddon addon;
    addon.file.system = "Example";
    catalog.addons.push_back(addon);
    addon.file.covers = "Example";
    catalog.addons.push_back(addon);
    catalog.games = {{0, "sd:/roms/Found.rom", "Found", found},
                     {0, "sd:/roms/Missing.rom", "Missing", missing},
                     {1, "sd:/roms/Other.rom", "Other", libretro_missing},
                     {0, "sd:/roms/Transient.rom", "Transient", transient}};
    ConfigureEmulatorCovers(catalog);
}

void fetch(const std::string& id, CoverFetch expected) {
    std::string error;
    const CoverFetch result = FetchEmulatorCover(id, error);
    if (result != expected) {
        std::cerr << "Picture " << id << ": expected " << static_cast<int>(expected)
                  << ", got " << static_cast<int>(result) << ": " << error << "\n";
        ++failures;
    }
}

void prepare() {
    // CTest gives this fixture its own directory. Only its card is removed.
    fs::remove_all("sd:");
    fs::create_directories("sd:/riftwii");
    EXPECT(ClearSteamGridKey());
    EXPECT(!SaveSteamGridKey("short"));
    EXPECT(!SteamGridKeyFound());
    EXPECT(SaveSteamGridKey(key));
    EXPECT(SteamGridKeySuffix() == "tive");
    EXPECT(!SaveSteamGridKey(key + "\nInjected"));
    EXPECT(SteamGridKeyFound());

    // A failed write must keep the previous key. No temporary secret may
    // remain after a successful replacement or an explicit clear.
    fs::create_directory("sd:/riftwii/steamgriddb.txt.part");
    EXPECT(!SaveSteamGridKey(std::string(32, 'z')));
    EXPECT(SteamGridKeySuffix() == "tive");
    fs::remove("sd:/riftwii/steamgriddb.txt.part");
    EXPECT(SaveSteamGridKey(std::string(32, 'z')));
    EXPECT(SteamGridKeySuffix() == "zzzz");
    EXPECT(!fs::exists("sd:/riftwii/steamgriddb.txt.part"));
    EXPECT(!fs::exists("sd:/riftwii/steamgriddb.txt.old"));
    EXPECT(WriteWholeFile("sd:/riftwii/steamgriddb.txt.part", key.data(), key.size()));
    EXPECT(WriteWholeFile("sd:/riftwii/steamgriddb.txt.old", key.data(), key.size()));
    EXPECT(ClearSteamGridKey());
    EXPECT(!SteamGridKeyFound());
    EXPECT(SteamGridKeySuffix().empty());
    EXPECT(!fs::exists("sd:/riftwii/steamgriddb.txt.part"));
    EXPECT(!fs::exists("sd:/riftwii/steamgriddb.txt.old"));

    // The original file-based setup still accepts CRLF from Windows.
    const std::string external = key + "\r\n";
    EXPECT(WriteWholeFile("sd:/riftwii/steamgriddb.txt", external.data(), external.size()));
    EXPECT(SteamGridKeyFound());
    // Covers require at least eight pixels per side; use a valid small
    // picture so this fixture exercises decoding and writing as well.
    const std::vector<std::uint8_t> rgba(8 * 8 * 4, 255);
    EXPECT(encode_png_rgba(rgba.data(), 8, 8, picture));
    fetch(found, CoverFetch::Stored);
    fetch(emulator_banner_key(found), CoverFetch::Stored);
    fetch(emulator_banner_key(found, true), CoverFetch::Stored);
    EXPECT(image_calls == 3 && api_calls == 6);
    fetch(missing, CoverFetch::NotFound);
    fetch(emulator_banner_key(missing), CoverFetch::NotFound);
    fetch(libretro_missing, CoverFetch::NotFound);
    EXPECT(api_calls == 9);
    const unsigned handled_calls = api_calls;
    fetch(missing, CoverFetch::NotFound);
    fetch(emulator_banner_key(missing, true), CoverFetch::NotFound);
    EXPECT(api_calls == handled_calls);

    // Service failures must remain retryable, rather than become a week
    // without pictures. A confirmed miss that cannot be saved is an error.
    fetch(transient, CoverFetch::NoAnswer);
    EXPECT(!fs::exists(emulator_art_miss_path(transient, "steamgrid")));
    const std::string miss_path = emulator_art_miss_path(missing, "steamgrid");
    fs::remove(miss_path);
    fs::create_directory(miss_path);
    fetch(missing, CoverFetch::Failed);
    fs::remove(miss_path);
    fetch(missing, CoverFetch::NotFound);
    EXPECT(logs.find(key) == std::string::npos);
}

void restart() {
    EXPECT(SteamGridKeyFound());
    for (const auto& id : {found, emulator_banner_key(found), emulator_banner_key(found, true)}) {
        EXPECT(!EmulatorCoverWanted(id));
        // Exercise the worker even if a caller skipped its queue check.
        fetch(id, CoverFetch::Stored);
    }
    for (const auto& id : {missing, emulator_banner_key(missing), emulator_banner_key(missing, true), libretro_missing}) {
        EXPECT(!EmulatorCoverWanted(id));
        fetch(id, CoverFetch::NotFound);
    }
    EXPECT(api_calls == 0 && image_calls == 0);
    std::cout << "Restart: saved pictures and recent misses made zero network requests.\n";

    const std::string miss_path = emulator_art_miss_path(missing, "steamgrid");
    std::string record;
    EXPECT(ReadTextFile(miss_path, record, 256));
    const auto source_start = record.find('\n') + 1;
    const auto source_end = record.find('\n', source_start);
    const std::string identity = record.substr(source_start, source_end - source_start);
    const std::string expired = emulator_art_miss(identity, std::time(nullptr) - kEmulatorArtWeek);
    EXPECT(WriteWholeFile(miss_path, expired.data(), expired.size()));
    EXPECT(EmulatorCoverWanted(missing));
    fetch(missing, CoverFetch::NotFound);
    EXPECT(api_calls == 1 && image_calls == 0);
    fetch(transient, CoverFetch::NoAnswer);
    EXPECT(api_calls == 2 && image_calls == 0);

    // Downloads off, or clearing a key, must not discard saved pictures.
    Settings().online = false;
    EXPECT(ClearSteamGridKey());
    fetch(found, CoverFetch::Stored);
    EXPECT(fs::exists(emulator_art_cache_path(found)));
    EXPECT(api_calls == 2 && image_calls == 0);
    EXPECT(logs.find(key) == std::string::npos);
}
}  // namespace

s32 LWP_MutexInit(mutex_t* mutex, bool) {
    *mutex = 1;
    return 0;
}
s32 LWP_MutexLock(mutex_t) { return 0; }
s32 LWP_MutexUnlock(mutex_t) { return 0; }

namespace riftwii::wii {
LoaderSettings& Settings() {
    static LoaderSettings settings;
    return settings;
}
bool DriveScanDone() { return true; }
// artfolder.cpp's listing cache (Wii threads only): outside a look it asks
// the card, as here.
bool ArtFileThere(const std::string& path) {
    struct stat info {};
    return stat(path.c_str(), &info) == 0;
}
bool ArtFileStat(const std::string& path, struct stat& info) { return stat(path.c_str(), &info) == 0; }
CardWriteHold::CardWriteHold() = default;
CardWriteHold::~CardWriteHold() = default;
std::uint64_t usb_mount_generation() { return 1; }
std::vector<std::string> usb_volume_roots() { return {}; }
bool list_usb_folder(const std::string&, std::vector<VolumeEntry>&, std::string*) { return false; }
bool read_usb_range(const std::string&, std::uint64_t, std::uint8_t*, std::size_t, std::uint64_t&) { return false; }

bool DecodePngRgba(const std::vector<std::uint8_t>& png, std::vector<std::uint8_t>& rgba,
                   int& width, int& height, std::string& error) {
    std::uint32_t decoded_width = 0;
    std::uint32_t decoded_height = 0;
    const bool decoded = decode_png(png.data(), png.size(), inflate_png, 1024, rgba,
                                    decoded_width, decoded_height, error);
    width = static_cast<int>(decoded_width);
    height = static_cast<int>(decoded_height);
    return decoded;
}

void logf(const char* format, ...) {
    char line[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    logs += line;
}

bool HttpGet(const std::string& url, std::vector<std::uint8_t>& body, std::string& error,
             std::size_t, int, const HttpProgress&, HttpRedirects) {
    ++image_calls;
    if (url == "https://cdn2.steamgriddb.com/grid/example.png") {
        body = picture;
        return true;
    }
    if (url.back() == '/') {
        const std::string index = "<a href=\"Unrelated.png\">Unrelated</a>";
        body.assign(index.begin(), index.end());
        return true;
    }
    error = "thumbnails.libretro.com answered 404 Not Found";
    return false;
}

bool HttpSteamGridGet(const std::string& path, const std::string& request_key,
                      std::vector<std::uint8_t>& body, std::string&) {
    ++api_calls;
    EXPECT(request_key == key);
    if (path.find("Transient") != std::string::npos) return false;
    std::string json;
    if (path.find("/search/") != std::string::npos) {
        json = path.find("Found") != std::string::npos ?
            "{\"success\":true,\"data\":[{\"id\":42,\"name\":\"Found\"}]}" : "{\"success\":true,\"data\":[]}";
    } else {
        const bool wide = path.find("460x215") != std::string::npos;
        json = std::string("{\"success\":true,\"data\":[{\"url\":\"https://cdn2.steamgriddb.com/grid/example.png\",") +
               (wide ? "\"width\":460,\"height\":215," : "\"width\":600,\"height\":900,") + "\"mime\":\"image/png\"}]}";
    }
    body.assign(json.begin(), json.end());
    return true;
}
}  // namespace riftwii::wii

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    configure();
    if (std::string(argv[1]) == "prepare") prepare();
    else if (std::string(argv[1]) == "restart") restart();
    else return 1;
    return failures ? 1 : 0;
}
