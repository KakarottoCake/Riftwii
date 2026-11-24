// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <zlib.h>

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/appcatalog.hpp"
#include "zipbuild.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;
using ziptest::make_zip;
using ziptest::TestFile;

namespace {

const char* const kSha = "8cf92160ae5267827e7e2f4b9a9730f001e9f6d9ce5647d202e5fa8589ca5617";
const char* const kOtherSha = "0000000000000000000000000000000000000000000000000000000000000001";

AppListing app(const char* id, const char* name, const char* author = "Tantric", const char* category = "emulators") {
    AppListing a;
    std::memset(&a, 0, sizeof(a));
    std::snprintf(a.id, sizeof(a.id), "%s", id);
    std::snprintf(a.name, sizeof(a.name), "%s", name);
    std::snprintf(a.author, sizeof(a.author), "%s", author);
    std::snprintf(a.category, sizeof(a.category), "%s", category);
    std::snprintf(a.version, sizeof(a.version), "5.0.6");
    std::snprintf(a.sha256, sizeof(a.sha256), "%s", kSha);
    a.format = AppFormat::Zip;
    a.files = 10;
    a.unpacked_size = 1000;
    a.install_count = 2;
    std::snprintf(a.install[0].from, sizeof(a.install[0].from), "apps/%s/", id);
    std::snprintf(a.install[0].to, sizeof(a.install[0].to), "apps/%s/", id);
    std::snprintf(a.install[1].from, sizeof(a.install[1].from), "data/");
    std::snprintf(a.install[1].to, sizeof(a.install[1].to), "%s/", id);
    a.data_folder_count = 1;
    std::snprintf(a.data_folders[0], sizeof(a.data_folders[0]), "%s", id);
    return a;
}

void test_categories() {
    std::size_t count = 0;
    const AppCategory* list = app_categories(count);
    EXPECT_EQ(count, 13u);
    EXPECT_EQ(std::string(list[0].key), "games");
    EXPECT_EQ(std::string(list[count - 1].key), "more");
    EXPECT_EQ(app_category_index("emulators"), 1u);
    // A key this RiftWii does not know shows under "More".
    EXPECT_EQ(app_category_index("robots"), count - 1);
    EXPECT_EQ(app_category_index("more"), count - 1);
}

// The note lists what the store put on the card; an update and a removal
// keep what the app or the player changed since, and what they made.
void test_record_files_and_keeping() {
    const AppListing a = app("snes9xgx", "Snes9x GX");
    const std::vector<AppRecordFile> files = {{"apps/snes9xgx/boot.dol", 3000000, 0x12345678u},
                                              {"apps/snes9xgx/settings.xml", 900, 0xabcdef01u},
                                              {"snes9xgx/cheats/readme.txt", 40, 0x1u}};
    AppRecord record;
    EXPECT_TRUE(app_record_parse(app_record_text(a, files), record));
    EXPECT_TRUE(record.has_files && !record.removed);
    EXPECT_EQ(record.files.size(), 3u);
    EXPECT_EQ(record.files[1].size, 900u);
    EXPECT_EQ(record.files[1].crc, 0xabcdef01u);
    EXPECT_TRUE(app_record_find(record, "snes9xgx/cheats/readme.txt") != nullptr);
    EXPECT_TRUE(app_record_find(record, "snes9xgx/saves/a.srm") == nullptr);

    // An old note (no list) and the start of a note cut off mid-line.
    AppRecord old;
    EXPECT_TRUE(app_record_parse(app_record_text(a, files).substr(0, 90), old));
    EXPECT_TRUE(!old.has_files && old.files.empty());
    const std::string text = app_record_text(a, files);
    EXPECT_TRUE(app_record_parse(text.substr(0, text.size() - 5), old));
    EXPECT_EQ(old.files.size(), 2u);  // the cut last line is left out
    AppRecord bare;
    EXPECT_TRUE(app_record_parse(std::string("version=5\nsha256=") + kSha + "\n", bare));
    EXPECT_TRUE(!bare.has_files);
    // A path that climbs out, or a backslash, is not taken.
    AppRecord bad;
    EXPECT_TRUE(app_record_parse(std::string("sha256=") + kSha + "\nfiles=3\nfile=1 00000001 apps/x/../../riftwii/a\n" +
                                     "file=1 00000001 apps\\x\nfile=1 00000001 apps/x/ok.txt\n",
                                 bad));
    EXPECT_EQ(bad.files.size(), 1u);

    // An update: the program always; the store's unchanged files; nothing
    // the app changed or the player made in a data folder.
    using F = AppFileOnCard;
    EXPECT_TRUE(app_install_replaces(a, "apps/snes9xgx/boot.dol", F::Changed, &record));
    EXPECT_TRUE(app_install_replaces(a, "apps/snes9xgx/boot.dol", F::NotOurs, &record));
    // Whatever its case: FAT does not tell BOOT.DOL from boot.dol.
    EXPECT_TRUE(app_install_replaces(a, "apps/snes9xgx/BOOT.DOL", F::Changed, &record));
    EXPECT_TRUE(app_install_replaces(a, "Apps/Snes9xGX/Meta.xml", F::Changed, &record));

    // The search keyboard's next keys follow a filter.
    AppListing pads[2] = {app("snes9xgx", "Snes9x GX"), app("sudoku", "Sudoku")};
    pads[0].controllers = kAppWiiRemote | kAppClassic;
    pads[1].controllers = kAppWiiRemote;
    EXPECT_EQ(app_search_next(pads, 2, "s").find('u') != std::string::npos, true);
    const auto no_remote = [](const AppListing& l) { return app_needs_no_wii_remote(l.controllers); };
    EXPECT_EQ(app_search_next(pads, 2, "s", +no_remote).find('u') == std::string::npos, true);
    EXPECT_TRUE(app_install_replaces(a, "apps/snes9xgx/settings.xml", F::AsInstalled, &record));
    EXPECT_TRUE(app_install_replaces(a, "apps/snes9xgx/new.txt", F::Absent, &record));
    EXPECT_TRUE(!app_install_replaces(a, "apps/snes9xgx/settings.xml", F::Changed, &record));
    EXPECT_TRUE(!app_install_replaces(a, "snes9xgx/settings.xml", F::NotOurs, &record));
    EXPECT_TRUE(!app_install_replaces(a, "apps/snes9xgx/settings.xml", F::NotOurs, &record));
    // A copy put there by hand (no note), or a note from before the lists:
    // its own folder is replaced, its data folders never.
    EXPECT_TRUE(app_install_replaces(a, "apps/snes9xgx/settings.xml", F::NotOurs, nullptr));
    EXPECT_TRUE(app_install_replaces(a, "apps/snes9xgx/settings.xml", F::NotOurs, &bare));
    EXPECT_TRUE(!app_install_replaces(a, "snes9xgx/settings.xml", F::NotOurs, nullptr));

    // A removal: the unchanged files of the note.
    const std::vector<std::string> gone = app_remove_files(a, &record, {F::AsInstalled, F::Changed, F::Absent});
    EXPECT_EQ(gone.size(), 1u);
    EXPECT_EQ(gone[0], std::string("apps/snes9xgx/boot.dol"));
    // Without a list: the program files only.
    const std::vector<std::string> program = app_remove_files(a, &bare, {});
    EXPECT_EQ(program.size(), 4u);
    EXPECT_EQ(program[0], std::string("apps/snes9xgx/boot.dol"));

    // A removal that kept saves leaves its note: the app is to get again,
    // and the note still tells a reinstall what to keep.
    AppRecord kept;
    EXPECT_TRUE(app_record_parse(app_record_text(a, {files[1]}, true), kept));
    EXPECT_TRUE(kept.removed && kept.files.size() == 1);
    EXPECT_TRUE(app_state(a, false, &kept) == AppState::Get);
    EXPECT_TRUE(app_state(a, true, &kept) == AppState::OnCard);
}

void test_records_and_states() {
    const AppListing a = app("snes9xgx", "Snes9x GX");
    AppRecord record;
    EXPECT_TRUE(app_record_parse(app_record_text(a), record));
    EXPECT_EQ(record.version, std::string("5.0.6"));
    EXPECT_EQ(record.sha256, std::string(kSha));
    EXPECT_TRUE(app_state(a, true, &record) == AppState::Installed);
    EXPECT_TRUE(app_state(a, false, nullptr) == AppState::Get);
    EXPECT_TRUE(app_state(a, true, nullptr) == AppState::OnCard);
    // A record whose folder was deleted by hand: the app is to get again.
    EXPECT_TRUE(app_state(a, false, &record) == AppState::Get);
    AppRecord old;
    EXPECT_TRUE(app_record_parse(std::string("version=5.0.5\r\nsha256=") + kOtherSha + "\r\n", old));
    EXPECT_TRUE(app_state(a, true, &old) == AppState::Update);
    // Not a record: no SHA-256, a short one, or text far too long.
    EXPECT_TRUE(!app_record_parse("version=1\n", old));
    EXPECT_TRUE(!app_record_parse("sha256=abc\n", old));
    EXPECT_TRUE(!app_record_parse(std::string(600, 'x'), old));
    AppListing packed = a;
    packed.format = AppFormat::SevenZip;
    EXPECT_TRUE(app_state(packed, false, nullptr) == AppState::PcOnly);
    EXPECT_TRUE(!app_installable(packed));
    packed.format = AppFormat::Dol;
    EXPECT_TRUE(app_installable(packed));
}

void test_search() {
    const AppListing snes = app("snes9xgx", "Snes9x GX");
    const AppListing fceu = app("fceugx", "FCE Ultra GX");
    const AppListing pong = app("pong", "Pong Deluxe", "Someone", "games");
    EXPECT_TRUE(app_matches(snes, ""));
    EXPECT_TRUE(app_matches(snes, "snes"));
    EXPECT_TRUE(app_matches(snes, "SNES"));
    EXPECT_TRUE(app_matches(snes, "9x"));       // a part of the name
    EXPECT_TRUE(app_matches(snes, "gx snes"));   // words in any order
    EXPECT_TRUE(app_matches(snes, "tantric"));   // the author
    EXPECT_TRUE(app_matches(snes, "emul"));      // the category
    EXPECT_TRUE(!app_matches(snes, "snes zzz"));  // every word must be found
    EXPECT_TRUE(!app_matches(pong, "tantric"));
    // The name's start first, then a word of it, then the author.
    EXPECT_TRUE(app_match_rank(snes, "snes") > app_match_rank(fceu, "gx"));
    EXPECT_TRUE(app_match_rank(fceu, "ultra") > app_match_rank(fceu, "tantric"));
    const AppListing apps[] = {snes, fceu, pong};
    const std::string next = app_search_next(apps, 3, "sn");
    EXPECT_TRUE(next.find('e') != std::string::npos);
    EXPECT_TRUE(next.find('q') == std::string::npos);
    // A space only after a word.
    EXPECT_TRUE(app_search_next(apps, 3, "").find(' ') == std::string::npos);
    EXPECT_TRUE(app_search_next(apps, 3, "pong").find(' ') != std::string::npos);
}

void test_install_plan() {
    const AppListing a = app("snes9xgx", "Snes9x GX");
    std::vector<AppInstallStep> plan;
    std::string error;
    std::vector<ZipEntry> entries;
    const auto read = [&](const std::vector<TestFile>& files) {
        const std::vector<std::uint8_t> zip = make_zip(files);
        return zip_read_directory(zip.data(), zip.size(), entries, error);
    };
    EXPECT_TRUE(read({{"apps/snes9xgx/boot.dol", "dol"}, {"apps/snes9xgx/meta.xml", "<app/>"}, {"data/roms/x", "1"},
                      {"readme.txt", "not installed"}}));
    EXPECT_TRUE(app_plan_install(a, entries, plan, error));
    EXPECT_EQ(plan.size(), 3u);
    EXPECT_EQ(plan[0].path, std::string("apps/snes9xgx/boot.dol"));
    EXPECT_EQ(plan[2].path, std::string("snes9xgx/roms/x"));
    // A mixed-case id (WiiMedic) still finds its boot.dol.
    const AppListing mixed = app("WiiMedic", "WiiMedic");
    EXPECT_TRUE(read({{"apps/WiiMedic/boot.dol", "dol"}}));
    EXPECT_TRUE(app_plan_install(mixed, entries, plan, error));
    // No boot.dol for the app: refused.
    EXPECT_TRUE(read({{"apps/snes9xgx/meta.xml", "<app/>"}}));
    EXPECT_TRUE(!app_plan_install(a, entries, plan, error));
    // One name twice, told apart by case only.
    EXPECT_TRUE(read({{"apps/snes9xgx/boot.dol", "a"}, {"apps/snes9xgx/BOOT.DOL", "b"}}));
    EXPECT_TRUE(!app_plan_install(a, entries, plan, error));
    // More than the list said.
    AppListing small = a;
    small.files = 1;
    EXPECT_TRUE(read({{"apps/snes9xgx/boot.dol", "a"}, {"apps/snes9xgx/meta.xml", "b"}}));
    EXPECT_TRUE(!app_plan_install(small, entries, plan, error));
    small = a;
    small.unpacked_size = 3;
    EXPECT_TRUE(read({{"apps/snes9xgx/boot.dol", "dol!"}}));
    EXPECT_TRUE(!app_plan_install(small, entries, plan, error));
    // A path that would climb out is not installed (and leaves no boot.dol).
    EXPECT_TRUE(read({{"apps/snes9xgx/../../boot.dol", "a"}}));
    EXPECT_TRUE(!app_plan_install(a, entries, plan, error));
}

// zlib's raw inflater, as the plugin's (which reaches zlib through RiftWii).
class ZlibStream : public ZipInflateStream {
public:
    bool begin() override {
        std::memset(&z_, 0, sizeof(z_));
        return inflateInit2(&z_, -MAX_WBITS) == Z_OK;
    }
    Step feed(const std::uint8_t* in, std::size_t size, const ZipSink& sink) override {
        z_.next_in = const_cast<Bytef*>(in);
        z_.avail_in = static_cast<uInt>(size);
        std::uint8_t out[4096];
        for (;;) {
            z_.next_out = out;
            z_.avail_out = sizeof(out);
            const int result = inflate(&z_, Z_NO_FLUSH);
            const std::size_t made = sizeof(out) - z_.avail_out;
            if (made > 0 && !sink(out, made)) return Step::Bad;
            if (result == Z_STREAM_END) return Step::Done;
            if (result != Z_OK && result != Z_BUF_ERROR) return Step::Bad;
            if (z_.avail_in == 0 && z_.avail_out != 0) return Step::More;
        }
    }
    void end() override { inflateEnd(&z_); }

private:
    z_stream z_;
};

std::vector<std::uint8_t> raw_deflate(const std::string& data) {
    z_stream z;
    std::memset(&z, 0, sizeof(z));
    deflateInit2(&z, 9, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    std::vector<std::uint8_t> out(compressBound(static_cast<uLong>(data.size())) + 64);
    z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    z.avail_in = static_cast<uInt>(data.size());
    z.next_out = out.data();
    z.avail_out = static_cast<uInt>(out.size());
    deflate(&z, Z_FINISH);
    out.resize(z.total_out);
    deflateEnd(&z);
    return out;
}

// A zip of one deflated entry, its bytes from zlib.
std::vector<std::uint8_t> deflated_zip(const std::string& name, const std::string& data) {
    std::vector<std::uint8_t> zip = make_zip({{name, data, 8}});
    // make_zip's "deflated" entry is one stored block: swap in zlib's
    // stream and fix the sizes and offsets that follow it.
    const std::vector<std::uint8_t> packed = raw_deflate(data);
    const std::size_t header = 30 + name.size();
    const std::size_t old_packed = 5 + data.size();
    std::vector<std::uint8_t> out(zip.begin(), zip.begin() + header);
    out.insert(out.end(), packed.begin(), packed.end());
    out.insert(out.end(), zip.begin() + header + old_packed, zip.end());
    const auto put32 = [&](std::size_t at, std::uint32_t v) {
        for (int i = 0; i < 4; ++i) out[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
    };
    put32(18, static_cast<std::uint32_t>(packed.size()));                  // local header
    const std::size_t directory = header + packed.size();
    put32(directory + 20, static_cast<std::uint32_t>(packed.size()));       // central record
    const std::size_t end = out.size() - 22;
    put32(end + 16, static_cast<std::uint32_t>(directory));                 // end record
    return out;
}

void test_zip_read_in_pieces() {
    // Big enough for several reads, and compressible.
    std::string data;
    for (int i = 0; i < 9000; ++i) data += "line " + std::to_string(i) + "\n";
    const std::vector<std::uint8_t> zip = deflated_zip("apps/x/boot.dol", data);
    std::size_t reads = 0;
    const ZipReadAt read = [&](std::uint64_t offset, std::uint8_t* out, std::size_t bytes) {
        ++reads;
        if (offset > zip.size() || bytes > zip.size() - offset) return false;
        std::memcpy(out, zip.data() + offset, bytes);
        return true;
    };
    std::vector<ZipEntry> entries;
    std::string error;
    EXPECT_TRUE(zip_read_directory_at(read, zip.size(), entries, error));
    EXPECT_EQ(entries.size(), 1u);
    std::string unpacked;
    ZlibStream zlib;
    EXPECT_TRUE(zip_extract_at(read, entries[0], &zlib, [&](const std::uint8_t* bytes, std::size_t size) {
        unpacked.append(reinterpret_cast<const char*>(bytes), size);
        return true;
    }, error));
    EXPECT_TRUE(unpacked == data);
    EXPECT_TRUE(reads > 3);  // read a chunk at a time, not whole

    // A damaged stream, a wrong CRC, and a reader that fails.
    std::vector<std::uint8_t> broken = zip;
    broken[30 + 15 + 10] ^= 0xFF;
    const ZipReadAt read_broken = [&](std::uint64_t offset, std::uint8_t* out, std::size_t bytes) {
        std::memcpy(out, broken.data() + offset, bytes);
        return true;
    };
    EXPECT_TRUE(!zip_extract_at(read_broken, entries[0], &zlib, [](const std::uint8_t*, std::size_t) { return true; },
                                error));
    ZipEntry wrong = entries[0];
    wrong.crc ^= 1;
    EXPECT_TRUE(!zip_extract_at(read, wrong, &zlib, [](const std::uint8_t*, std::size_t) { return true; }, error));
    const ZipReadAt failing = [](std::uint64_t, std::uint8_t*, std::size_t) { return false; };
    EXPECT_TRUE(!zip_read_directory_at(failing, zip.size(), entries, error));
    // A sink that stops (the card is full).
    EXPECT_TRUE(!zip_extract_at(read, wrong, &zlib, [](const std::uint8_t*, std::size_t) { return false; }, error));
    EXPECT_EQ(error, std::string("a file could not be written"));
}

void test_sizes() {
    EXPECT_EQ(app_size_text(3100000), std::string("3.1 MB"));
    EXPECT_EQ(app_size_text(640000), std::string("640 KB"));
    EXPECT_EQ(app_size_text(1), std::string("1 KB"));
    // The plugin passes the translated formats: French writes "2,4 Mo".
    EXPECT_EQ(app_size_text(2357802, "{1},{2} Mo", "{1} Ko"), std::string("2,4 Mo"));
    EXPECT_EQ(app_size_text(640000, "{1},{2} Mo", "{1} Ko"), std::string("640 Ko"));
}

}  // namespace

int main() {
    test_categories();
    test_records_and_states();
    test_record_files_and_keeping();
    test_search();
    test_install_plan();
    test_zip_read_in_pieces();
    test_sizes();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "appcatalog tests passed" << std::endl;
    return 0;
}
