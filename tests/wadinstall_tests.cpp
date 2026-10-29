// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Production folder scan and installer, with synthetic WADs and a raw USB
// view. Every key and content byte is made up here.
#include "wadfolder.hpp"
#include "wadinstall.hpp"
#include "sharednandfind.hpp"
#include "usbcatalog.hpp"
#include "riftwii/wadfile.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>

namespace fs = std::filesystem;
using namespace riftwii;
using namespace riftwii::wii;

namespace {
int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::cerr << __LINE__ << ": " #condition "\n"; ++failures; } } while (0)
constexpr std::uint64_t kTitle = 0x0001000157413445ull;
const std::uint8_t kCommonKey[16] = {1, 2, 3};
const std::uint8_t kTitleKey[16] = {4, 5, 6};
std::map<std::string, std::vector<std::uint8_t>> usb_files;
unsigned usb_banner_reads = 0;
unsigned key_reads = 0;
bool fail_commit = false;
int writes_before_failure = -1;

void put(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint64_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i) bytes[at + count - 1 - i] = static_cast<std::uint8_t>(value >> (8 * i));
}

std::vector<std::uint8_t> make_wad(std::uint8_t seed, bool shared = false) {
    std::vector<std::uint8_t> ticket(kTicketBytes);
    put(ticket, 0, 0x10001, 4);
    put(ticket, 0x1DC, kTitle, 8);
    std::uint8_t iv[16] = {};
    std::memcpy(iv, ticket.data() + 0x1DC, 8);
    Aes128(kCommonKey).cbc_encrypt(iv, kTitleKey, ticket.data() + 0x1BF, 16);
    std::vector<std::uint8_t> tmd(0x1E4 + 3 * 36);
    put(tmd, 0, 0x10001, 4);
    put(tmd, 0x18C, kTitle, 8);
    put(tmd, 0x198, 0x3031, 2);
    put(tmd, 0x1DE, 3, 2);
    put(tmd, 0x1E0, 1, 2);
    std::vector<std::uint8_t> data;
    for (unsigned i = 0; i < 3; ++i) {
        std::vector<std::uint8_t> content(i == 0 ? 0x700 : 0x123, static_cast<std::uint8_t>(seed + i));
        if (i == 0) {
            std::fill(content.begin(), content.end(), 0);
            std::memcpy(content.data() + 0x40, "IMET", 4);
            // English name, UTF-16BE at IMET + 0x1C + 84.
            const std::string name = "Test Game";
            for (std::size_t c = 0; c < name.size(); ++c) content[0x40 + 0x1C + 84 + c * 2 + 1] = name[c];
        }
        const std::size_t at = 0x1E4 + i * 36;
        put(tmd, at, i + 0x10, 4);
        put(tmd, at + 4, i, 2);
        put(tmd, at + 6, shared && i == 2 ? 0x8001 : 1, 2);
        put(tmd, at + 8, content.size(), 8);
        const auto hash = sha1(content.data(), content.size());
        std::copy(hash.begin(), hash.end(), tmd.begin() + at + 16);
        content.resize((content.size() + 15) & ~std::size_t(15));
        std::memset(iv, 0, sizeof iv);
        iv[1] = static_cast<std::uint8_t>(i);
        Aes128(kTitleKey).cbc_encrypt(iv, content.data(), content.data(), content.size());
        data.insert(data.end(), content.begin(), content.end());
        data.resize((data.size() + 63) & ~std::size_t(63));
    }
    std::vector<std::uint8_t> header(0x20);
    put(header, 0, 0x20, 4);
    header[4] = 'I';
    header[5] = 's';
    put(header, 0x10, ticket.size(), 4);
    put(header, 0x14, tmd.size(), 4);
    put(header, 0x18, data.size(), 4);
    std::vector<std::uint8_t> wad;
    for (const auto* part : {&header, &ticket, &tmd, &data}) {
        wad.insert(wad.end(), part->begin(), part->end());
        wad.resize((wad.size() + 63) & ~std::size_t(63));
    }
    return wad;
}

void write(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    CHECK(file.good());
}

std::vector<std::uint8_t> read(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

const std::string title = "sd:/nands/01/title/00010001/57413445";

void test_installer_rollback() {
    auto good = make_wad(20, true);
    write("sd:/wiiware/game.wad", good);
    std::string code, error;
    CHECK(install_wad("sd:/wiiware/game.wad", "sd:/nands/01", kCommonKey, {}, code, error));
    const auto original_tmd = read(title + "/content/title.tmd");
    const auto original_program = read(title + "/content/00000011.app");
    const auto original_map = read("sd:/nands/01/shared1/content.map");
    const auto original_ticket = read("sd:/nands/01/ticket/00010001/57413445.tik");
    const auto original_uid = read("sd:/nands/01/sys/uid.sys");
    write(title + "/data/save.dat", {7, 8, 9});

    auto changed = make_wad(90, true);
    // Break a later content, after earlier files have been staged.
    changed[changed.size() - 64] ^= 1;
    write("sd:/wiiware/game.wad", changed);
    CHECK(!install_wad("sd:/wiiware/game.wad", "sd:/nands/01", kCommonKey, {}, code, error));
    CHECK(read(title + "/content/title.tmd") == original_tmd);
    CHECK(read(title + "/content/00000011.app") == original_program);
    CHECK(read("sd:/nands/01/shared1/content.map") == original_map);
    CHECK(read("sd:/nands/01/ticket/00010001/57413445.tik") == original_ticket);
    CHECK(read(title + "/data/save.dat") == std::vector<std::uint8_t>({7, 8, 9}));
    CHECK(!fs::exists(title + "/.riftwii-stage"));
    CHECK(!install_wad("sd:/wiiware/game.wad", "sd:/nands/broken", kCommonKey, {}, code, error));
    CHECK(!fs::exists("sd:/nands/broken/title/00010001/57413445"));

    write("sd:/wiiware/game.wad", make_wad(90, true));
    fail_commit = true;
#ifdef RIFTWII_TEST_WRAP_RENAME
    CHECK(!install_wad("sd:/wiiware/game.wad", "sd:/nands/01", kCommonKey, {}, code, error));
    CHECK(read(title + "/content/title.tmd") == original_tmd);
    CHECK(read("sd:/nands/01/shared1/content.map") == original_map);
    CHECK(read("sd:/nands/01/ticket/00010001/57413445.tik") == original_ticket);
    CHECK(read("sd:/nands/01/sys/uid.sys") == original_uid);
    CHECK(!fs::exists("sd:/nands/01/shared1/00000001.app"));
    fail_commit = false;
    writes_before_failure = 1;
    CHECK(!install_wad("sd:/wiiware/game.wad", "sd:/nands/01", kCommonKey, {}, code, error));
    CHECK(read(title + "/content/title.tmd") == original_tmd);
    CHECK(read(title + "/content/00000011.app") == original_program);
    CHECK(!fs::exists(title + "/.riftwii-stage"));
    writes_before_failure = -1;
#endif
    fail_commit = false;
    CHECK(install_wad("sd:/wiiware/game.wad", "sd:/nands/01", kCommonKey, {}, code, error));
    CHECK(read(title + "/content/00000011.app") != original_program);
    CHECK(read(title + "/data/save.dat") == std::vector<std::uint8_t>({7, 8, 9}));
    CHECK(!install_wad("sd:/wiiware/game.wad", "sd:/../escape", kCommonKey, {}, code, error));
    CHECK(!install_wad("sd:/wiiware/game.wad", "sd:/nands/01", kCommonKey, {}, code, error, kTitle + 1));
}

void test_drop_play_and_cache() {
    fs::remove_all("sd:");
    write("sd:/wiiware/nested/game.WAD", make_wad(20));
    write("sd:/wiiware/nested/deeper/ignored.wad", make_wad(30));
    write("sd:/wiiware/._ignored.wad", make_wad(30));
    std::vector<NandChannel> channels;
    list_wad_channels(false, channels);
    CHECK(channels.size() == 1);
    if (channels.empty()) return;
    CHECK(channels[0].names[kBnrEnglish] == "Test Game");
    CHECK(!channels[0].wad_banner_cache.empty());
    const unsigned keys_after_scan = key_reads;
    channels.clear();
    list_wad_channels(false, channels);
    CHECK(key_reads == keys_after_scan);  // the banner was read from its small cache
    unsigned progress_calls = 0;
    std::string error;
    CHECK(prepare_wad_channel(channels[0], [&](double) { ++progress_calls; }, error));
    CHECK(progress_calls != 0);
    CHECK(wad_managed_title("sd:/nands/01", kTitle));
    write(title + "/data/save.dat", {1, 3, 5});
    progress_calls = 0;
    CHECK(prepare_wad_channel(channels[0], [&](double) { ++progress_calls; }, error));
    CHECK(progress_calls == 0);
    write("sd:/wiiware/nested/game.WAD", make_wad(60));
    CHECK(prepare_wad_channel(channels[0], [&](double) { ++progress_calls; }, error));
    CHECK(progress_calls != 0);
    CHECK(read(title + "/data/save.dat") == std::vector<std::uint8_t>({1, 3, 5}));
    fs::remove("sd:/wiiware/nested/game.WAD");
    channels.clear();
    list_wad_channels(false, channels);
    CHECK(channels.empty());
    CHECK(fs::exists(title + "/data/save.dat"));
    write("sd:/wiiware/broken.wad", {1, 2, 3});
    CHECK(list_wad_channels(false, channels) == 1);
    CHECK(channels.empty());
}

void test_usb_partitions_and_merge() {
    usb_files["usb:/wiiware/game.wad"] = make_wad(20);
    usb_files["usb2:/wiiware/nested/game.wad"] = make_wad(30);
    usb_files["usb2:/wiiware/nested/deeper/ignored.wad"] = make_wad(40);
    std::vector<NandChannel> channels;
    list_wad_channels(true, channels);
    CHECK(channels.size() == 1);
    CHECK(channels[0].wad_path == "usb:/wiiware/game.wad");
    const unsigned banner_reads = usb_banner_reads;
    channels.clear();
    list_wad_channels(true, channels);
    CHECK(usb_banner_reads == banner_reads);
    std::string error;
    CHECK(prepare_wad_channel(channels[0], {}, error));
    NandChannel installed = channels[0];
    installed.wad_path.clear();
    installed.emu_nand.clear();
    std::vector<NandChannel> real = {installed};
    merge_wad_channels(real, channels);
    CHECK(real.size() == 1 && real[0].wad_path.empty());
    installed.emu_nand = "sd:/nands/old";
    real = {installed};
    merge_wad_channels(real, channels);
    CHECK(real.size() == 1 && real[0].emu_nand == "sd:/nands/old" && !real[0].wad_path.empty());
}
}  // namespace

#ifdef RIFTWII_TEST_WRAP_RENAME
extern "C" int __real_rename(const char*, const char*);
extern "C" std::size_t __real_fwrite(const void*, std::size_t, std::size_t, FILE*);
extern "C" std::size_t __wrap_fwrite(const void* data, std::size_t size, std::size_t count, FILE* file) {
    if (writes_before_failure == 0) return 0;
    if (writes_before_failure > 0) --writes_before_failure;
    return __real_fwrite(data, size, count, file);
}
extern "C" int __wrap_rename(const char* from, const char* to) {
    if (fail_commit && std::string(from).find("/.riftwii-stage/content") != std::string::npos &&
        std::string(to) == title + "/content") return -1;
    return __real_rename(from, to);
}
#endif

namespace riftwii::wii {
void logf(const char*, ...) {}
bool ConsoleCommonKey(std::uint8_t out[16]) {
    ++key_reads;
    std::memcpy(out, kCommonKey, 16);
    return true;
}
SharedNandLookup look_up_channel_nand() {
    SharedNandLookup result;
    result.usable = true;
    result.folder = "sd:/nands/01";
    return result;
}
bool prepare_wad_nand_system(const std::string&, std::string&) { return true; }
std::vector<std::string> usb_volume_roots() { return {"usb:/", "usb2:/"}; }
bool read_usb_range(const std::string& path, std::uint64_t offset, std::uint8_t* out, std::size_t length,
                    std::uint64_t& size) {
    const auto found = usb_files.find(path);
    if (found == usb_files.end()) return false;
    const auto& bytes = found->second;
    size = bytes.size();
    if (offset > size || length > size - offset) return false;
    if (length == 0x700) ++usb_banner_reads;
    if (length) std::memcpy(out, bytes.data() + offset, length);
    return true;
}
bool list_usb_folder(const std::string& path, std::vector<VolumeEntry>& out, std::string*) {
    std::map<std::string, VolumeEntry> entries;
    const std::string prefix = path + "/";
    for (const auto& file : usb_files) {
        if (file.first.compare(0, prefix.size(), prefix) != 0) continue;
        const std::string rest = file.first.substr(prefix.size());
        const auto slash = rest.find('/');
        const std::string name = rest.substr(0, slash);
        entries[name] = {name, slash != std::string::npos, file.second.size()};
    }
    for (const auto& entry : entries) out.push_back(entry.second);
    return true;
}
}  // namespace riftwii::wii

int main() {
    fs::remove_all("sd:");
    test_installer_rollback();
    test_drop_play_and_cache();
    test_usb_partitions_and_merge();
    fs::remove_all("sd:");
    return failures == 0 ? 0 : 1;
}
