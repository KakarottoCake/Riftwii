// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "riftwii/saveinstall.hpp"
#include "riftwii/saveinflate.hpp"

namespace fs = std::filesystem;
using namespace riftwii;

namespace {

int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::cerr << "Line " << __LINE__ << ": " #condition "\n"; ++failures; } } while (0)

SaveListing listing(bool gamecube = false) {
    SaveListing save;
    std::strcpy(save.id, "test-save");
    std::strcpy(save.game_id, gamecube ? "GAAE01" : "RAAE01");
    std::strcpy(save.title, "Finished game");
    std::strcpy(save.description, "All stages unlocked");
    std::strcpy(save.author, "Test author");
    std::strcpy(save.url, "https://example.org/save.zip");
    std::strcpy(save.page, "https://example.org/saves");
    std::memset(save.sha256, 'a', 64);
    save.platform = gamecube ? SavePlatform::GameCube : SavePlatform::Wii;
    save.format = gamecube ? SaveFormat::Gci : SaveFormat::WiiZip;
    save.size = gamecube ? 64 + 8192 : 1234;
    save.files = gamecube ? 0 : 2;
    save.unpacked_size = gamecube ? 0 : 13;
    return save;
}

std::string json_save() {
    return "{\"id\":\"all-stages\",\"game_id\":\"GAAE01\",\"title\":\"All stages\","
           "\"description\":\"Game finished\",\"author\":\"Player\",\"platform\":\"gamecube\","
           "\"format\":\"gci\",\"page\":\"https://example.org/saves\",\"url\":\"https://example.org/save.gci\","
           "\"size\":8256,\"sha256\":\"" + std::string(64, 'a') + "\"}";
}

void parser_tests() {
    std::vector<SaveListing> out(kSaveListMaxEntries);
    std::size_t count = 0, skipped = 0;
    std::string error;
    const auto parse = [&](const std::string& text) {
        return parse_save_list(text.data(), text.size(), out.data(), out.size(), count, skipped, error);
    };
    const std::string record = json_save();
    const auto list = [](const std::string& item) { return "{\"schema\":1,\"saves\":[" + item + "]}"; };
    CHECK(parse(list(record)) && count == 1 && skipped == 0);
    CHECK(save_matches(out[0], "GAAE01", SavePlatform::GameCube));
    CHECK(!save_matches(out[0], "GAAP01", SavePlatform::GameCube));
    CHECK(!save_matches(out[0], "GAAE01", SavePlatform::Wii));
    CHECK(parse(list(record + "," + record)) && count == 1 && skipped == 1);
    CHECK(!parse("{\"schema\":2,\"saves\":[]}"));
    CHECK(!parse("{\"schema\":1,\"schema\":1,\"saves\":[]}"));
    CHECK(!parse(list(record) + "garbage"));
    CHECK(!parse(list(record).substr(0, list(record).size() - 2)) && count == 0);
    CHECK(!parse("{\"schema\":1,\"saves\":[],\"saves\":[]}"));
    for (const auto& bad : std::vector<std::pair<std::string, std::string>>{
        {"GAAE01", "GAAE/1"}, {"gamecube", "wii"}, {"8256", "4294967296"},
        {"8256", "-1"}, {"8256", "1.5"}, {"8256", "0"}, {"gci", "raw"},
        {"https://example.org/save.gci", "http://example.org/save.gci"},
        {"https://example.org/save.gci", "https://user@example.org/save.gci"},
        {std::string(64, 'a'), std::string(64, 'G')}, {"Player", std::string(1100, 'x')}}) {
        std::string damaged = record;
        damaged.replace(damaged.find(bad.first), bad.first.size(), bad.second);
        CHECK(parse(list(damaged)) && count == 0 && skipped == 1);
    }
    std::string many = record;
    for (unsigned i = 1; i <= kSaveListMaxEntries; ++i) many += "," + record;
    CHECK(!parse(list(many)));
    CHECK(!parse(std::string(kSaveListMaxBytes + 1, ' ')));
    CHECK(!save_url_ok("https://127.0.0.1/file"));
    CHECK(!save_url_ok("https://0x7f.0.0.1/file"));
    CHECK(!save_url_ok("https://example..org/file"));
    CHECK(!save_url_ok("https://-example.org/file"));
    CHECK(!save_url_ok("https://example.org:443/file"));
    CHECK(!save_url_ok("https://example.org/a\r\n"));
    CHECK(!save_id_ok("../bad"));
    CHECK(save_wii_title_path("RAAE01") == "/title/00010000/52414145");
    CHECK(save_wii_title_path("../bad").empty());

    const auto extend = [&](const std::string& fields) {
        return parse(list(record.substr(0, record.size() - 1) + "," + fields + "}"));
    };
    CHECK(extend("\"member\":\"shared/progress.gci\"") && count == 1);
    CHECK(std::string(out[0].member) == "shared/progress.gci");
    for (const char* bad : {"../progress.gci", "/progress.gci", "save//progress.gci", "save/progress.gci.",
                            "save/progress.gci/", "save/a:b.gci", "save/progress.dat", "save/\\u0009progress.gci"}) {
        CHECK(extend("\"member\":\"" + std::string(bad) + "\"") && count == 0 && skipped == 1);
    }
    CHECK(extend("\"root\":\"folder/\"") && count == 0);
    CHECK(extend("\"game_ids\":[\"GAAE01\",\"GAAP01\"]") && count == 0);
    CHECK(extend("\"game_ids\":[\"GAAE01\",\"GAAE01\"]") && count == 0);
    CHECK(extend("\"game_ids\":[\"GAAE01\"]") && count == 1);
    CHECK(extend("\"game_ids\":[]") && count == 0);
    CHECK(extend("\"game_ids\":\"GAAE01\"") && count == 0);
    CHECK(!extend("\"game_ids\":[\"GAAE01\",\"GAAE01\",\"GAAE01\",\"GAAE01\",\"GAAE01\",\"GAAE01\",\"GAAE01\",\"GAAE01\",\"GAAE01\"]"));

    std::string wii_record = record;
    wii_record.replace(wii_record.find("GAAE01"), 6, "RAAE01");
    wii_record.replace(wii_record.find("gamecube"), 8, "wii");
    wii_record.replace(wii_record.find("\"gci\""), 5, "\"wii-zip\"");
    wii_record.pop_back();
    wii_record += ",\"files\":2,\"unpacked_size\":13,";
    CHECK(parse(list(wii_record + "\"root\":\"private/wii/title/RAAE/\",\"game_ids\":[\"RAAE01\",\"RAAP01\"]}")));
    CHECK(count == 1 && skipped == 0 && out[0].has_root && out[0].game_id_count == 2);
    CHECK(parse(list(wii_record + "\"root\":\"\"}")) && count == 1 && out[0].has_root);
    CHECK(parse(list(wii_record + "\"root\":\"../unsafe/\"}")) && count == 0);
    CHECK(parse(list(wii_record + "\"root\":\"folder/\\u0009/\"}")) && count == 0);
    CHECK(parse(list(wii_record + "\"root\":\"" + std::string(128, 'x') + "/\"}")) && count == 0);
    CHECK(parse(list(wii_record + "\"game_ids\":[\"RAAA01\",\"RAAB01\",\"RAAC01\",\"RAAE01\",\"RAAJ01\",\"RAAK01\",\"RAAP01\",\"RAAW01\"]}")));
    CHECK(count == 1 && out[0].game_id_count == kSaveMaxGameIds);

    SaveListing regions = listing();
    regions.game_id_count = 2;
    std::strcpy(regions.game_ids[0], "RAAE01");
    std::strcpy(regions.game_ids[1], "RAAP01");
    CHECK(save_listing_ok(regions));
    CHECK(save_matches(regions, "RAAP01", SavePlatform::Wii));
    CHECK(!save_matches(regions, "RAAJ01", SavePlatform::Wii));
    SaveListing selected;
    CHECK(save_select_game(regions, "RAAP01", SavePlatform::Wii, selected));
    CHECK(std::string(selected.game_id) == "RAAP01" && save_listing_ok(selected));
    CHECK(save_wii_title_path(selected.game_id) == "/title/00010000/52414150");
    CHECK(!save_select_game(regions, "RAAJ01", SavePlatform::Wii, selected));
    std::strcpy(regions.game_ids[1], "RBAP01");
    CHECK(!save_listing_ok(regions));
    regions.game_id_count = kSaveMaxGameIds + 1;
    CHECK(!save_listing_ok(regions));
}

void path_tests() {
    std::string relative, error;
    CHECK(save_wii_zip_path("data/banner.bin", relative) && relative == "banner.bin");
    CHECK(save_wii_zip_path("data/sub/save.dat", relative));
    for (const char* bad : {"data/../other", "data//save", "data/", "data/.hidden", "data/file.",
                            "data/a /b", "data/a:b", "data/a\\b", "/data/save", "title/other/data/save",
                            "data/1234567890123", "data/a/b/c/d/e/f/g/h/i", "data/é"}) {
        CHECK(!save_wii_zip_path(bad, relative));
    }
    SaveListing save = listing();
    std::vector<ZipEntry> entries(2);
    entries[0].name = "data/banner.bin";
    entries[0].unpacked_size = 4;
    entries[1].name = "data/save.dat";
    entries[1].unpacked_size = 9;
    std::vector<std::size_t> plan;
    CHECK(save_plan_wii_zip(save, entries, plan, error) && plan.size() == 2);
    entries[1].name = "data/BANNER.BIN";
    CHECK(!save_plan_wii_zip(save, entries, plan, error));
    entries[1].name = "data/banner.bin/sub";
    CHECK(!save_plan_wii_zip(save, entries, plan, error));
    entries[1].name = "data/save.dat";
    ++save.unpacked_size;
    CHECK(!save_plan_wii_zip(save, entries, plan, error));
    --save.unpacked_size;
    save.has_root = true;
    std::strcpy(save.root, "private/wii/title/RAAE/");
    entries[0].name = std::string(save.root) + "banner.bin";
    entries[1].name = std::string(save.root) + "save.dat";
    CHECK(save_plan_wii_zip(save, entries, plan, error) && plan.size() == 2);
    CHECK(save_wii_zip_relative(save, entries[1].name, relative) && relative == "save.dat");
    entries.push_back({});
    entries.back().name = "README.txt";
    CHECK(save_plan_wii_zip(save, entries, plan, error) && plan.size() == 2);
    entries.back().name = "private/wii/title/RAAE/banner.bin/child";
    CHECK(!save_plan_wii_zip(save, entries, plan, error));
    entries.pop_back();
    std::strcpy(save.root, "private/../RAAE/");
    CHECK(!save_listing_ok(save));
    save.root[0] = '\0';
    entries[0].name = "banner.bin";
    entries[1].name = "save.dat";
    CHECK(save_plan_wii_zip(save, entries, plan, error));
}

std::vector<std::uint8_t> gci_file(const char* name = "progress") {
    std::vector<std::uint8_t> gci(64 + 8192, 0);
    std::memcpy(gci.data(), "GAAE01", 6);
    std::memcpy(gci.data() + 8, name, std::strlen(name));
    gci[0x39] = 1;  // Big-endian count: one data block.
    gci[64] = 42;
    return gci;
}

void gci_tests() {
    SaveListing save = listing(true);
    std::vector<std::uint8_t> card;
    gc_card_format(card, GcCardFormat{});
    std::vector<std::uint8_t> gci = gci_file();
    std::string error;
    CHECK(save_apply_gci(save, gci, card, false, error));
    GcCardInfo info;
    CHECK(gc_card_read(card, info, error) && info.saves.size() == 1);
    std::vector<std::uint8_t> exported;
    CHECK(gc_card_export(card, info.saves[0].slot, exported, error));
    CHECK(exported[64] == 42);
    const auto before = card;
    CHECK(!save_apply_gci(save, gci, card, false, error) && card == before);
    gci[64] = 99;
    CHECK(save_apply_gci(save, gci, card, true, error));
    CHECK(gc_card_read(card, info, error) && info.saves.size() == 1);
    CHECK(gc_card_export(card, info.saves[0].slot, exported, error) && exported[64] == 99);
    const auto replaced = card;
    gci[0] = 'X';
    CHECK(!save_apply_gci(save, gci, card, true, error) && card == replaced);
    gci = gci_file();
    gci[0x39] = 2;
    CHECK(!save_apply_gci(save, gci, card, true, error) && card == replaced);
    gci = gci_file("second");
    CHECK(save_apply_gci(save, gci, card, true, error));
    CHECK(gc_card_read(card, info, error) && info.saves.size() == 2);
    GcCardFormat japanese;
    japanese.shift_jis = true;
    gc_card_format(card, japanese);
    const auto other_encoding = card;
    CHECK(!save_apply_gci(save, gci, card, true, error) && card == other_encoding);

    GcCardFormat small;
    small.size_mbits = 4;
    gc_card_format(card, small);
    auto full_save = gci_file("other-game");
    std::memcpy(full_save.data(), "GBBE01", 6);
    full_save.resize(64 + 59 * 8192);
    full_save[0x39] = 59;
    CHECK(gc_card_import(card, full_save, error));
    const auto full_card = card;
    CHECK(!save_apply_gci(save, gci_file(), card, true, error) && card == full_card);
}

void publish_tests() {
    // Inject failures at each move, including rollback. Nothing overwrites
    // an existing backup; a failed rollback still retains the original bytes.
    for (unsigned fail = 0; fail < 4; ++fail) {
        std::map<std::string, std::string> files{{"stage", "new"}, {"data", "old"}};
        unsigned moves = 0;
        SavePublishOps ops;
        ops.exists = [&](const std::string& name) { return files.count(name) != 0; };
        ops.move = [&](const std::string& from, const std::string& to) {
            ++moves;
            if ((fail == 1 && moves == 1) || (fail == 2 && moves == 2) ||
                (fail == 3 && moves >= 2) || !files.count(from) || files.count(to)) return false;
            files[to] = files[from];
            files.erase(from);
            return true;
        };
        std::string error;
        CHECK(!save_publish("stage", "data", "backup", false, ops, error));
        CHECK(files.at("data") == "old" && moves == 0);
        const bool ok = save_publish("stage", "data", "backup", true, ops, error);
        CHECK(ok == (fail == 0));
        if (fail == 0) CHECK(files.at("data") == "new" && files.at("backup") == "old");
        else if (fail == 3) CHECK(!files.count("data") && files.at("backup") == "old");
        else CHECK(files.at("data") == "old");
    }
}

void put16(std::vector<std::uint8_t>& bytes, unsigned value) {
    bytes.push_back(value & 255);
    bytes.push_back((value >> 8) & 255);
}

void put32(std::vector<std::uint8_t>& bytes, unsigned value) {
    put16(bytes, value & 65535);
    put16(bytes, value >> 16);
}

// Synthetic stored ZIPs. No commercial save bytes or console files.
std::string deflate_bytes(const std::string& input) {
    z_stream stream{};
    CHECK(deflateInit2(&stream, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) == Z_OK);
    std::string output(compressBound(input.size()), '\0');
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in = input.size();
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = output.size();
    CHECK(deflate(&stream, Z_FINISH) == Z_STREAM_END);
    output.resize(stream.total_out);
    deflateEnd(&stream);
    return output;
}

std::vector<std::uint8_t> archive_for(const std::vector<std::pair<std::string, std::string>>& files,
                                     bool deflated = false) {
    std::vector<std::uint8_t> bytes, directory;
    for (const auto& file : files) {
        const unsigned offset = bytes.size();
        const unsigned crc = zip_crc32_update(0, reinterpret_cast<const std::uint8_t*>(file.second.data()), file.second.size());
        const std::string packed = deflated ? deflate_bytes(file.second) : file.second;
        put32(bytes, 0x04034b50);
        put16(bytes, 20);
        put16(bytes, 0);
        put16(bytes, deflated ? 8 : 0);
        put16(bytes, 0);
        put16(bytes, 0);
        put32(bytes, crc);
        put32(bytes, packed.size());
        put32(bytes, file.second.size());
        put16(bytes, file.first.size());
        put16(bytes, 0);
        bytes.insert(bytes.end(), file.first.begin(), file.first.end());
        bytes.insert(bytes.end(), packed.begin(), packed.end());
        put32(directory, 0x02014b50);
        put16(directory, 20);
        put16(directory, 20);
        put16(directory, 0);
        put16(directory, deflated ? 8 : 0);
        put16(directory, 0);
        put16(directory, 0);
        put32(directory, crc);
        put32(directory, packed.size());
        put32(directory, file.second.size());
        put16(directory, file.first.size());
        for (unsigned i = 0; i < 4; ++i) put16(directory, 0);
        put32(directory, 0);
        put32(directory, offset);
        directory.insert(directory.end(), file.first.begin(), file.first.end());
    }
    const unsigned start = bytes.size();
    bytes.insert(bytes.end(), directory.begin(), directory.end());
    put32(bytes, 0x06054b50);
    put16(bytes, 0);
    put16(bytes, 0);
    put16(bytes, files.size());
    put16(bytes, files.size());
    put32(bytes, directory.size());
    put32(bytes, start);
    put16(bytes, 0);
    return bytes;
}

std::string read_text(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void zipped_gci_tests() {
    const auto fixture = gci_file();
    const std::string contents(reinterpret_cast<const char*>(fixture.data()), fixture.size());
    SaveListing save = listing(true);
    std::strcpy(save.member, "Finished Save/progress.gci");
    auto archive = archive_for({{save.member, contents}, {"README.txt", "Instructions"}}, true);
    save.size = archive.size();
    const ZipReadAt read = [&](std::uint64_t at, std::uint8_t* out, std::size_t count) {
        if (at > archive.size() || count > archive.size() - at) return false;
        std::memcpy(out, archive.data() + at, count);
        return true;
    };
    std::vector<std::uint8_t> gci, card;
    std::string error;
    SaveZipInflater inflater;
    CHECK(save_read_gci(save, read, &inflater, gci, error) && gci == fixture);
    gc_card_format(card, {});
    CHECK(save_apply_gci(save, gci, card, false, error));
    const auto installed = card;
    gci[0] = 'X';
    CHECK(!save_apply_gci(save, gci, card, true, error) && card == installed);
    gci.clear();
    CHECK(!save_apply_gci(save, gci, card, true, error) && card == installed);
    for (const char* second : {"other.gci", "Finished Save/PROGRESS.GCI", "../escape", "Finished Save/progress.gci/sub"}) {
        archive = archive_for({{save.member, contents}, {second, contents}});
        save.size = archive.size();
        CHECK(!save_read_gci(save, read, nullptr, gci, error) && gci.empty());
    }
    archive = archive_for({{"other.gci", contents}});
    save.size = archive.size();
    CHECK(!save_read_gci(save, read, nullptr, gci, error));
    archive = archive_for({{save.member, contents}});
    save.size = archive.size();
    CHECK(save_read_gci(save, read, nullptr, gci, error) && gci == fixture);
    archive[30 + std::strlen(save.member)] ^= 1;
    CHECK(!save_read_gci(save, read, nullptr, gci, error) && gci.empty());
}

void wii_install_tests() {
    const fs::path previous = fs::current_path();
    const fs::path scratch = fs::temp_directory_path() / ("riftwii-saves-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(scratch);
    fs::current_path(scratch);
    SaveListing save = listing();
    auto archive = archive_for({{"data/banner.bin", "WIBN"}, {"data/sub/save.dat", "new-save!"}});
    save.size = archive.size();
    const ZipReadAt read = [&](std::uint64_t at, std::uint8_t* out, std::size_t bytes) {
        if (at > archive.size() || bytes > archive.size() - at) return false;
        std::memcpy(out, archive.data() + at, bytes);
        return true;
    };
    const std::string root = "sd:/nands/01";
    const std::string title = root + save_wii_title_path(save.game_id);
    const std::string data = title + "/data";
    fs::create_directories(data);
    std::ofstream(data + "/old.dat") << "previous-save";
    fs::create_directories(root + "/title/00010000/other/data");
    std::ofstream(root + "/title/00010000/other/data/keep") << "other-title";
    std::string backup, error;
    const auto install = [&](bool replace, const SaveInstallProgress& progress = {}) {
        return save_install_wii_zip(save, root, read, nullptr, replace, "20261126-120000", backup, error, progress);
    };
    CHECK(!install(false) && read_text(data + "/old.dat") == "previous-save");
    CHECK(!install(true, [](auto, auto) { return false; }));
    CHECK(read_text(data + "/old.dat") == "previous-save" && !fs::exists(title + "/data.download"));
    CHECK(install(true));
    CHECK(read_text(data + "/sub/save.dat") == "new-save!");
    CHECK(read_text(backup + "/old.dat") == "previous-save");
    CHECK(!fs::exists(data + "/old.dat"));
    const std::string first_backup = backup;
    CHECK(install(true) && backup != first_backup && fs::exists(first_backup));
    CHECK(read_text(root + "/title/00010000/other/data/keep") == "other-title");
    CHECK(save_install_wii_zip(save, "sd:/nands/02", read, nullptr, false, "20261126-120000", backup, error));
    CHECK(backup.empty());
    CHECK(read_text("sd:/nands/02" + save_wii_title_path(save.game_id) + "/data/sub/save.dat") == "new-save!");
    fs::create_directories(title + "/data.download");
    std::ofstream(title + "/data.download/keep") << "interrupted";
    CHECK(!install(true) && read_text(title + "/data.download/keep") == "interrupted");
    fs::remove_all(title + "/data.download");
    // An incorrect CRC must discard staging without moving the live save.
    archive[30 + std::string("data/banner.bin").size()] ^= 1;
    CHECK(!install(true) && read_text(data + "/sub/save.dat") == "new-save!");
    CHECK(!fs::exists(title + "/data.download"));
    CHECK(!save_install_wii_zip(save, "/", read, nullptr, true, "20261126-120000", backup, error));
    CHECK(!save_install_wii_zip(save, "sd:/../escape", read, nullptr, true, "20261126-120000", backup, error));

    // Exercise the same inflater used on the Wii, with exact output-chunk
    // boundaries and multiple input feeds. Each byte arrives separately.
    const std::string large(32768, 'x');
    const std::string packed = deflate_bytes(large);
    SaveZipInflater inflater;
    CHECK(inflater.begin());
    std::string inflated;
    auto step = ZipInflateStream::Step::More;
    for (char byte : packed) {
        const auto input = static_cast<std::uint8_t>(byte);
        step = inflater.feed(&input, 1, [&](const auto* data, auto size) {
            inflated.append(reinterpret_cast<const char*>(data), size);
            return true;
        });
        CHECK(step != ZipInflateStream::Step::Bad);
    }
    CHECK(step == ZipInflateStream::Step::Done && inflated == large);
    inflater.end();
    archive = archive_for({{"data/banner.bin", "WIBN"}, {"data/save.dat", large}}, true);
    save.size = archive.size();
    save.unpacked_size = 4 + large.size();
    CHECK(save_install_wii_zip(save, "sd:/nands/03", read, &inflater, false, "20261126-120000", backup, error));
    CHECK(read_text("sd:/nands/03" + save_wii_title_path(save.game_id) + "/data/save.dat") == large);
    SaveZipInflater cancelled([] { return true; });
    CHECK(!save_install_wii_zip(save, "sd:/nands/04", read, &cancelled, false, "20261126-120000", backup, error));
    CHECK(!fs::exists("sd:/nands/04" + save_wii_title_path(save.game_id) + "/data"));

    // Named roots are removed before writing. Bind an approved portable
    // payload to PAL, rather than writing the listing's primary US title.
    save.has_root = true;
    std::strcpy(save.root, "private/wii/title/RAAE/");
    save.game_id_count = 2;
    std::strcpy(save.game_ids[0], "RAAE01");
    std::strcpy(save.game_ids[1], "RAAP01");
    archive = archive_for({{std::string(save.root) + "banner.bin", "WIBN"},
                           {std::string(save.root) + "save.dat", "portable!"}, {"README.txt", "Read me"}}, true);
    save.size = archive.size();
    save.unpacked_size = 13;
    SaveListing selected;
    CHECK(save_select_game(save, "RAAP01", SavePlatform::Wii, selected));
    const std::string named_root = "sd:/nands/05";
    CHECK(save_install_wii_zip(selected, named_root, read, &inflater, false, "20261126-120000", backup, error));
    const std::string pal_data = named_root + save_wii_title_path("RAAP01") + "/data";
    CHECK(read_text(pal_data + "/save.dat") == "portable!");
    CHECK(!fs::exists(named_root + save_wii_title_path("RAAE01")));
    CHECK(!fs::exists(pal_data + "/private") && !fs::exists(pal_data + "/README.txt"));
    archive = archive_for({{std::string(save.root) + "banner.bin", "FAIL"},
                           {std::string(save.root) + "save.dat", "portable!"}});
    selected.size = archive.size();
    CHECK(!save_install_wii_zip(selected, named_root, read, nullptr, true, "20261126-120000", backup, error));
    CHECK(read_text(pal_data + "/save.dat") == "portable!");
    fs::current_path(previous);
    fs::remove_all(scratch);
}

}  // namespace

int main() {
    parser_tests();
    path_tests();
    gci_tests();
    zipped_gci_tests();
    publish_tests();
    wii_install_tests();
    if (failures) std::cerr << failures << " save check(s) failed\n";
    return failures ? 1 : 0;
}
