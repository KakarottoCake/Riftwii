// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/hidimport.hpp"
#include "riftwii/hidconfig.hpp"
#include "nintendontimport.hpp"
#include "usbcatalog.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>

namespace riftwii::wii {

std::map<std::string, std::string> usb_files;
std::vector<VolumeEntry> usb_entries;
std::size_t largest_read = 0;

bool read_usb_range(const std::string& path, std::uint64_t offset, std::uint8_t* out, std::size_t length,
                    std::uint64_t& size) {
    const auto found = usb_files.find(path);
    if (found == usb_files.end()) return false;
    const std::string& text = found->second;
    size = text.size();
    if (offset > size || length > size - offset) return false;
    if (length) std::memcpy(out, text.data() + offset, length);
    if (length > largest_read) largest_read = length;
    return true;
}

bool list_usb_folder(const std::string& path, std::vector<VolumeEntry>& out, std::string*) {
    if (path != "usb:/controllers") return false;
    out = usb_entries;
    return true;
}

}  // namespace riftwii::wii

namespace {

unsigned failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::cerr << __LINE__ << ": " #condition "\n"; ++failures; } } while (0)

const std::string kLayout = "VID=ABCD\nPID=1234\nPolltype=1\nA=0,01\n";

std::string Read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void Write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    file << text;
    file.close();
    CHECK(file.good());
}

void NamesAndValidation() {
    std::uint32_t id = 0;
    CHECK(riftwii::hid_filename_id("abcd_1234.InI", id) && id == 0xABCD1234);
    CHECK(riftwii::hid_filename_id("0000_0000.ini", id) && id == 0);
    CHECK(riftwii::hid_filename_id("FFFF_FFFF.INI", id) && id == 0xFFFFFFFF);
    for (const char* name : {"../1234_5678.ini", "1234/5678.ini", "1234\\5678.ini", "123_45678.ini",
                             "1234_567G.ini", "1234_5678.ini.bak", "controller.ini", "1234_5678.in"}) {
        id = 42;
        CHECK(!riftwii::hid_filename_id(name, id) && id == 0);
    }
    CHECK(riftwii::hid_layout_filename(0xABCD0001) == "ABCD_0001.ini");
    CHECK(std::string(riftwii::kHidImportFiles[0]) == "sd:/controller.ini");
    CHECK(std::string(riftwii::kHidImportFiles[1]) == "usb:/controller.ini");
    CHECK(std::string(riftwii::kHidImportFiles[2]) == "sd:/nintendont/controller.ini");
    CHECK(std::string(riftwii::kHidImportFiles[3]) == "usb:/nintendont/controller.ini");
    CHECK(std::string(riftwii::kHidImportFolders[0]) == "sd:/controllers");
    CHECK(std::string(riftwii::kHidImportFolders[1]) == "usb:/controllers");

    std::string error;
    CHECK(riftwii::validate_hid_import(kLayout, "controller.ini", id, error) && id == 0xABCD1234);
    CHECK(riftwii::validate_hid_import(kLayout, "abcd_1234.INI", id, error) && id == 0xABCD1234);
    CHECK(!riftwii::validate_hid_import(kLayout, "ABCD_1235.ini", id, error) && id == 0);
    CHECK(error == "The device IDs in the file do not match its filename.");
    CHECK(!riftwii::validate_hid_import(kLayout, "../ABCD_1234.ini", id, error));
    CHECK(!riftwii::validate_hid_import(kLayout + "MultiIn=2\n", "controller.ini", id, error));
    CHECK(!riftwii::validate_hid_import(kLayout + std::string(1, '\0'), "controller.ini", id, error));
    std::string full = kLayout + "#";
    full.resize(riftwii::kHidConfigMaxBytes, ' ');
    CHECK(riftwii::validate_hid_import(full, "controller.ini", id, error));
    full += ' ';
    CHECK(!riftwii::validate_hid_import(full, "controller.ini", id, error));
}

void Saving() {
    namespace fs = std::filesystem;
    // CTest gives this suite its own executable; do not use console paths.
    const fs::path folder = fs::current_path() / "hidimport-test-layouts";
    CHECK(fs::create_directory(folder));
    const std::string root = folder.generic_string();
    const fs::path target = folder / "ABCD_1234.ini";
    const std::string changed = kLayout + "B=0,02\n";
    std::string destination;
    std::string error;
    using riftwii::HidImportSave;
    CHECK(riftwii::save_hid_import(root, kLayout, false, destination, error) == HidImportSave::Saved);
    CHECK(destination == target.generic_string() && error.empty());
    CHECK(Read(target) == kLayout);
    CHECK(riftwii::save_hid_import(root, changed, false, destination, error) == HidImportSave::Exists);
    CHECK(Read(target) == kLayout);
    CHECK(riftwii::save_hid_import(root, changed, true, destination, error) == HidImportSave::Saved);
    CHECK(Read(target) == changed);
    CHECK(!fs::exists(target.string() + ".import-old"));
    CHECK(!fs::exists(target.string() + ".import-new"));
    CHECK(riftwii::save_hid_import(root, "VID=../../outside\n", true, destination, error) == HidImportSave::Failed);
    CHECK(destination.empty() && Read(target) == changed);

    // A leftover recovery copy or unfinished write is never clobbered.
    const fs::path backup = target.string() + ".import-old";
    Write(backup, "recovery");
    CHECK(riftwii::save_hid_import(root, kLayout, true, destination, error) == HidImportSave::Failed);
    CHECK(Read(target) == changed && Read(backup) == "recovery");
    fs::remove(backup);
    const fs::path temporary = target.string() + ".import-new";
    Write(temporary, "unfinished");
    CHECK(riftwii::save_hid_import(root, kLayout, true, destination, error) == HidImportSave::Failed);
    CHECK(Read(target) == changed && Read(temporary) == "unfinished");
    fs::remove(temporary);

    fs::remove(target);
    const fs::path lowercase = folder / "abcd_1234.ini";
    Write(lowercase, "player's file");
    CHECK(riftwii::save_hid_import(root, kLayout, false, destination, error) == HidImportSave::Exists);
    CHECK(Read(lowercase) == "player's file");
    CHECK(riftwii::save_hid_import(root, kLayout, true, destination, error) == HidImportSave::Saved);
    CHECK(Read(lowercase) == kLayout);
    fs::remove(lowercase);

    fs::create_directory(target);
    CHECK(riftwii::save_hid_import(root, kLayout, true, destination, error) == HidImportSave::Failed);
    CHECK(fs::is_directory(target));
    CHECK(!fs::exists(temporary));
    fs::remove(target);
    CHECK(riftwii::save_hid_import(root + "/missing", kLayout, false, destination, error) == HidImportSave::Failed);
    fs::remove(folder);
}

void FindingAndReading() {
    using namespace riftwii::wii;
    usb_files = {
        {"usb:/controller.ini", kLayout},
        {"usb:/nintendont/controller.ini", kLayout},
        {"usb:/controllers/abcd_1234.INI", kLayout},
        {"usb:/controllers/ABCD_1235.ini", kLayout},
    };
    usb_entries = {{"abcd_1234.INI", false, kLayout.size()}, {"ABCD_1235.ini", false, kLayout.size()},
                   {"../ABCD_1234.ini", false, kLayout.size()}, {"ABCD_1236.ini", true, 0}};
    std::vector<std::string> paths;
    bool limited = true;
    CHECK(FindNintendontLayouts(paths, limited));
    CHECK(!limited && paths.size() == 4);
    CHECK(std::find(paths.begin(), paths.end(), "usb:/controller.ini") != paths.end());
    CHECK(std::find(paths.begin(), paths.end(), "usb:/nintendont/controller.ini") != paths.end());

    NintendontLayout layout;
    std::string detail;
    CHECK(ReadNintendontLayout("usb:/controllers/abcd_1234.INI", layout, detail) == NintendontRead::Ready);
    CHECK(std::string(layout.text.data(), layout.bytes) == kLayout);
    CHECK(ReadNintendontLayout("usb:/controllers/ABCD_1235.ini", layout, detail) == NintendontRead::WrongDevice);
    CHECK(ReadNintendontLayout("usb:/missing.ini", layout, detail) == NintendontRead::Unreadable);
    usb_files["usb:/controller.ini"] = kLayout + "MultiIn=2\n";
    CHECK(ReadNintendontLayout("usb:/controller.ini", layout, detail) == NintendontRead::Invalid);
    usb_files["usb:/controller.ini"] = std::string(riftwii::kHidConfigMaxBytes + 1, 'X');
    largest_read = 0;
    CHECK(ReadNintendontLayout("usb:/controller.ini", layout, detail) == NintendontRead::TooLarge);
    CHECK(largest_read == 0);

    usb_entries.clear();
    for (unsigned i = 0; i < 513; ++i) {
        const std::string name = riftwii::hid_layout_filename(0xABCD0000 + i);
        usb_entries.push_back({name, false, kLayout.size()});
        usb_files["usb:/controllers/" + name] = kLayout;
    }
    CHECK(FindNintendontLayouts(paths, limited));
    CHECK(limited && paths.size() == kHidImportMaxFiles);
    usb_entries.assign(513, {"invalid-name", false, 0});
    CHECK(FindNintendontLayouts(paths, limited));
    CHECK(limited && paths.size() == 2);

    // The same size and parser checks on the SD/stdio read path.
    const std::filesystem::path source = std::filesystem::current_path() / "controller.ini";
    Write(source, kLayout);
    CHECK(ReadNintendontLayout(source.generic_string(), layout, detail) == NintendontRead::Ready);
    Write(source, std::string(riftwii::kHidConfigMaxBytes + 1, 'X'));
    CHECK(ReadNintendontLayout(source.generic_string(), layout, detail) == NintendontRead::TooLarge);
    Write(source, kLayout + std::string(1, '\0'));
    CHECK(ReadNintendontLayout(source.generic_string(), layout, detail) == NintendontRead::Invalid);
    std::filesystem::remove(source);
}

}  // namespace

int main() {
    NamesAndValidation();
    Saving();
    FindingAndReading();
    return failures ? 1 : 0;
}
