// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/ghcheats.hpp"
#include "riftwii/http.hpp"
#include "zipbuild.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;
using ziptest::make_zip;
using ziptest::stored_block_inflate;
using ziptest::TestFile;

namespace {

// The directory page as the site serves it: Apache's table, with the ads
// and scripts around it that a real answer carries.
std::string listing(const std::vector<std::string>& names) {
    std::string page = "<html><head><script>var note = text_wii_21000101.zip;</script></head><body><table>\n";
    for (const std::string& name : names)
        page += "<tr><td><a href=\"" + name + "\">" + name + "</a>  </td><td>2026-09-27 01:20</td><td>690K</td></tr>\n";
    return page + "</table></body></html>";
}

std::string cheat_text(const std::string& id, const std::string& title, int cheats = 1) {
    std::string text = id + "\n" + title + "\n\n";
    for (int i = 0; i < cheats; ++i) text += "Cheat " + std::to_string(i) + " [Someone]\n04000000 00000001\n\n";
    return text;
}

// A zip whose entries are stored or "deflated" as the test's inflater reads.
std::vector<std::uint8_t> zip_of(const std::vector<TestFile>& files) { return make_zip(files); }

bool build(const std::vector<std::uint8_t>& zip, GameHackingIndex& index, std::string& error,
           GameHackingLimits limits = GameHackingLimits()) {
    return gamehacking_build_index(zip.data(), zip.size(), limits, stored_block_inflate, index, error);
}

}  // namespace

int main() {
    // ---- The listing: the newest date wins, whatever the order.
    {
        std::string name, error;
        EXPECT_TRUE(gamehacking_newest_wii_zip(listing({"text_ngc_20261023.zip", "text_wii_20260927.zip"}), name, error));
        EXPECT_EQ(name, "text_wii_20260927.zip");
        EXPECT_TRUE(gamehacking_newest_wii_zip(
            listing({"text_wii_20260927.zip", "text_wii_20251231.zip", "text_wii_20261102.zip", "text_wii_20260101.zip"}),
            name, error));
        EXPECT_EQ(name, "text_wii_20261102.zip");
        // The page's script mentions a newer name that is not a link target.
        EXPECT_TRUE(gamehacking_newest_wii_zip(listing({"text_wii_20200101.zip"}), name, error));
        EXPECT_EQ(name, "text_wii_20200101.zip");
        // GameCube zips are not Wii zips; nothing else qualifies.
        EXPECT_TRUE(!gamehacking_newest_wii_zip(listing({"text_ngc_20261023.zip"}), name, error));
        EXPECT_TRUE(!gamehacking_newest_wii_zip("", name, error));
        EXPECT_TRUE(!gamehacking_newest_wii_zip("<html>nothing here</html>", name, error));
        // Dates that cannot be real, wrong digit counts, other endings.
        EXPECT_TRUE(!gamehacking_newest_wii_zip(listing({"text_wii_20261301.zip", "text_wii_20260100.zip",
                                                          "text_wii_2026092.zip", "text_wii_202609270.zip",
                                                          "text_wii_20260927.zip.bak", "text_wii_20260927.tar"}),
                                                name, error));
        // Only the name is taken from a link, whatever path it has.
        EXPECT_TRUE(gamehacking_newest_wii_zip(listing({"../text_wii_20260927.zip"}), name, error));
        EXPECT_EQ(name, "text_wii_20260927.zip");
        EXPECT_TRUE(gamehacking_zip_name_ok("text_wii_20260927.zip"));
        EXPECT_TRUE(!gamehacking_zip_name_ok("../text_wii_20260927.zip"));
        EXPECT_TRUE(!gamehacking_zip_name_ok("text_wii_20260927.zip/x"));
        EXPECT_TRUE(!gamehacking_zip_name_ok("text_wii_2026092a.zip"));
    }

    // ---- The request names RiftWii and its version.
    {
        HttpUrl url;
        std::string error;
        EXPECT_TRUE(parse_http_url("https://gamehacking.org/text/", url, error));
        const std::string request = http_get_request(url, "RiftWii/2610-639");
        EXPECT_TRUE(request.find("GET /text/ HTTP/1.1\r\nHost: gamehacking.org\r\nUser-Agent: RiftWii/2610-639\r\n") == 0);
        EXPECT_TRUE(http_get_request(url).find("User-Agent: RiftWii\r\n") != std::string::npos);
    }

    // ---- When a cached copy is looked for again.
    {
        const std::int64_t day = 24 * 60 * 60;
        EXPECT_TRUE(gamehacking_stale(0, 1000));
        EXPECT_TRUE(!gamehacking_stale(1000000, 1000000 + 29 * day));
        EXPECT_TRUE(!gamehacking_stale(1000000, 1000000 + 30 * day));
        EXPECT_TRUE(gamehacking_stale(1000000, 1000000 + 30 * day + 1));
        // A clock set back never makes it due.
        EXPECT_TRUE(!gamehacking_stale(1000000, 500));
    }

    // ---- ID lines and entry names.
    {
        std::string id;
        EXPECT_TRUE(gamehacking_text_id("RMCE01\nMario Kart Wii", 21, id));
        EXPECT_EQ(id, "RMCE01");
        EXPECT_TRUE(gamehacking_text_id("\xEF\xBB\xBFRSBE01\r\nSuper Smash", 24, id));
        EXPECT_EQ(id, "RSBE01");
        EXPECT_TRUE(gamehacking_text_id("WAAA\nA channel", 14, id));
        EXPECT_EQ(id, "WAAA");
        EXPECT_TRUE(!gamehacking_text_id("rmce01\nx", 8, id));
        EXPECT_TRUE(!gamehacking_text_id("RMCE0\nx", 7, id));
        EXPECT_TRUE(!gamehacking_text_id("RMCE012\nx", 9, id));
        EXPECT_TRUE(!gamehacking_text_id("\nRMCE01\n", 8, id));
        EXPECT_TRUE(!gamehacking_text_id("", 0, id));
        EXPECT_TRUE(gamehacking_entry_name_ok("Mario Kart Wii (USA) (En,Fr,Es) [0CC7B4D5].txt"));
        EXPECT_TRUE(gamehacking_entry_name_ok("Link's Crossbow Training (USA) [98D41086].txt"));
        EXPECT_TRUE(!gamehacking_entry_name_ok(""));
        EXPECT_TRUE(!gamehacking_entry_name_ok("../a.txt"));
        EXPECT_TRUE(!gamehacking_entry_name_ok("a/b.txt"));
        EXPECT_TRUE(!gamehacking_entry_name_ok("a\\b.txt"));
        EXPECT_TRUE(!gamehacking_entry_name_ok("a..b.txt"));
        EXPECT_TRUE(!gamehacking_entry_name_ok(".hidden.txt"));
        EXPECT_TRUE(gamehacking_entry_name_ok("Call of Duty: Black Ops (USA) [E46478BB].txt"));
        EXPECT_TRUE(!gamehacking_entry_name_ok(std::string("a\0b.txt", 7)));
        EXPECT_TRUE(!gamehacking_entry_name_ok("a\nb.txt"));
        EXPECT_TRUE(!gamehacking_entry_name_ok(std::string(201, 'a')));
    }

    // ---- The ID index: stored and deflated entries, the biggest file wins
    // for a game, and what is not a cheat file is left out.
    {
        const std::string mk_small = cheat_text("RMCE01", "Mario Kart Wii (USA) v1.0", 1);
        const std::string mk_big = cheat_text("RMCE01", "Mario Kart Wii (USA) v1.1", 4);
        const std::vector<std::uint8_t> zip = zip_of({
            {"Mario Kart Wii (USA) [AAAAAAAA].txt", mk_small, 0},
            {"Mario Kart Wii (USA) (v1.01) [BBBBBBBB].txt", mk_big, 8},
            {"Brawl (USA) [CCCCCCCC].txt", cheat_text("RSBE01", "Brawl"), 8},
            {"Channel (World) [DDDDDDDD].txt", cheat_text("WAAA", "A channel"), 0},
            {"README.md", "not a cheat file", 0},
            {"No id (USA) [EEEEEEEE].txt", "just some words\nand more\n", 0},
        });
        GameHackingIndex index;
        std::string error;
        EXPECT_TRUE(build(zip, index, error));
        EXPECT_EQ(index.entry.size(), 3u);
        EXPECT_EQ(index.entry["RMCE01"], 1u);
        EXPECT_EQ(index.entry["RSBE01"], 2u);
        EXPECT_EQ(index.entry["WAAA"], 3u);
        EXPECT_EQ(index.left_out, 3u);  // the small Mario Kart file, the README, the ID-less file
        EXPECT_TRUE(index.entry.count("NOPE00") == 0);

        // Reading a game's text out of the zip by the index.
        GameHackingLimits limits;
        std::string text;
        EXPECT_TRUE(gamehacking_read_cheats(zip.data(), zip.size(), index, "RMCE01", limits, stored_block_inflate, text, error));
        EXPECT_EQ(text, mk_big);
        EXPECT_TRUE(gamehacking_read_cheats(zip.data(), zip.size(), index, "WAAA", limits, nullptr, text, error));
        EXPECT_TRUE(!gamehacking_read_cheats(zip.data(), zip.size(), index, "NOPE00", limits, stored_block_inflate, text, error));
        EXPECT_TRUE(text.empty());

        // The index as a file, and back.
        index.zip = "text_wii_20260927.zip";
        index.fetched = 1792000000;
        const std::string saved = gamehacking_index_text(index);
        GameHackingIndex again;
        EXPECT_TRUE(gamehacking_parse_index(saved, again, error));
        EXPECT_EQ(again.zip, "text_wii_20260927.zip");
        EXPECT_EQ(again.fetched, 1792000000);
        EXPECT_TRUE(again.entry == index.entry);

        // An index that does not match its zip is refused, not trusted:
        // an entry past the directory, a folder, a different game's file.
        GameHackingIndex wrong = again;
        wrong.entry["RMCE01"] = 99;
        EXPECT_TRUE(!gamehacking_read_cheats(zip.data(), zip.size(), wrong, "RMCE01", limits, stored_block_inflate, text, error));
        wrong = again;
        wrong.entry["RMCE01"] = 2;  // Brawl's file
        EXPECT_TRUE(!gamehacking_read_cheats(zip.data(), zip.size(), wrong, "RMCE01", limits, stored_block_inflate, text, error));
        EXPECT_TRUE(text.empty());
        wrong = again;
        wrong.entry["RMCE01"] = 4;  // the README
        EXPECT_TRUE(!gamehacking_read_cheats(zip.data(), zip.size(), wrong, "RMCE01", limits, stored_block_inflate, text, error));
    }

    // ---- The index file's parser is strict.
    {
        GameHackingIndex index;
        std::string error;
        const std::string good = "RiftWii GameHacking.org index 1\nzip text_wii_20260927.zip\nfetched 100\nentries 1\nRMCE01 4\n";
        EXPECT_TRUE(gamehacking_parse_index(good, index, error));
        EXPECT_EQ(index.entry["RMCE01"], 4u);
        EXPECT_TRUE(!gamehacking_parse_index("", index, error));
        EXPECT_TRUE(!gamehacking_parse_index("RiftWii GameHacking.org index 2\nzip text_wii_20260927.zip\nfetched 100\nentries 0\n", index, error));
        EXPECT_TRUE(!gamehacking_parse_index("RiftWii GameHacking.org index 1\nzip ../x.zip\nfetched 100\nentries 0\n", index, error));
        EXPECT_TRUE(!gamehacking_parse_index("RiftWii GameHacking.org index 1\nzip text_wii_20260927.zip\nfetched x\nentries 0\n", index, error));
        EXPECT_TRUE(!gamehacking_parse_index("RiftWii GameHacking.org index 1\nzip text_wii_20260927.zip\nfetched 100\nentries 2\nRMCE01 4\n", index, error));
        EXPECT_TRUE(!gamehacking_parse_index("RiftWii GameHacking.org index 1\nzip text_wii_20260927.zip\nfetched 100\nentries 1\nRMCE01 4x\n", index, error));
        EXPECT_TRUE(!gamehacking_parse_index("RiftWii GameHacking.org index 1\nzip text_wii_20260927.zip\nfetched 100\nentries 1\nrmce01 4\n", index, error));
        EXPECT_TRUE(!gamehacking_parse_index("RiftWii GameHacking.org index 1\nzip text_wii_20260927.zip\nfetched 100\nentries 2\nRMCE01 4\nRMCE01 5\n", index, error));
    }

    // ---- Every bound refuses.
    {
        GameHackingIndex index;
        std::string error;
        const std::vector<std::uint8_t> small = zip_of({{"A (USA) [11111111].txt", cheat_text("RAAE01", "A"), 0}});

        // The download over the zip bound.
        GameHackingLimits limits;
        limits.zip_bytes = small.size() - 1;
        EXPECT_TRUE(!build(small, index, error, limits));
        EXPECT_TRUE(error.find("bigger") != std::string::npos);
        limits.zip_bytes = small.size();
        EXPECT_TRUE(build(small, index, error, limits));

        // Too many entries.
        const std::vector<std::uint8_t> two = zip_of({{"A (USA) [11111111].txt", cheat_text("RAAE01", "A"), 0},
                                                      {"B (USA) [22222222].txt", cheat_text("RBBE01", "B"), 0}});
        limits = GameHackingLimits();
        limits.entries = 1;
        EXPECT_TRUE(!build(two, index, error, limits));
        EXPECT_TRUE(error.find("too many") != std::string::npos);
        limits.entries = 2;
        EXPECT_TRUE(build(two, index, error, limits));

        // All entries together over the unpacked bound (as the zip declares them).
        limits = GameHackingLimits();
        limits.total_unpacked = cheat_text("RAAE01", "A").size() + cheat_text("RBBE01", "B").size() - 1;
        EXPECT_TRUE(!build(two, index, error, limits));
        EXPECT_TRUE(error.find("unpacks") != std::string::npos);
        limits.total_unpacked += 1;
        EXPECT_TRUE(build(two, index, error, limits));

        // One entry over its bound is left out, not fatal; the rest is kept.
        limits = GameHackingLimits();
        limits.entry_unpacked = static_cast<std::uint32_t>(cheat_text("RAAE01", "A").size());
        const std::vector<std::uint8_t> mixed = zip_of({{"A (USA) [11111111].txt", cheat_text("RAAE01", "A"), 0},
                                                        {"Big (USA) [33333333].txt", cheat_text("RBBE01", "Big", 40), 0}});
        EXPECT_TRUE(build(mixed, index, error, limits));
        EXPECT_EQ(index.entry.size(), 1u);
        EXPECT_TRUE(index.entry.count("RAAE01") == 1);
        EXPECT_EQ(index.left_out, 1u);
    }

    // ---- Names that could leave the cache folder refuse the whole zip.
    {
        GameHackingIndex index;
        std::string error;
        const char* const bad[] = {"../evil.txt", "a/b.txt", "/abs.txt", "a\\b.txt", "x..y.txt", ".hidden.txt"};
        for (const char* name : bad) {
            const auto zip = zip_of({{"A (USA) [11111111].txt", cheat_text("RAAE01", "A"), 0}, {name, cheat_text("RBBE01", "B"), 0}});
            EXPECT_TRUE(!build(zip, index, error));
            EXPECT_TRUE(error.find("plain file name") != std::string::npos);
        }
    }

    // ---- A damaged entry (wrong CRC), a damaged zip, and a lying inflater.
    {
        GameHackingIndex index;
        std::string error;
        auto zip = zip_of({{"A (USA) [11111111].txt", cheat_text("RAAE01", "A"), 0, true}});
        EXPECT_TRUE(!build(zip, index, error));
        EXPECT_TRUE(error.find("damaged") != std::string::npos);
        const std::vector<std::uint8_t> junk(500, 0x41);
        EXPECT_TRUE(!build(junk, index, error));
        EXPECT_TRUE(!build(std::vector<std::uint8_t>(), index, error));
        // A deflated entry with no inflater to read it.
        zip = zip_of({{"A (USA) [11111111].txt", cheat_text("RAAE01", "A"), 8}});
        EXPECT_TRUE(!gamehacking_build_index(zip.data(), zip.size(), GameHackingLimits(), nullptr, index, error));
    }

    if (g_failures) return 1;
    std::cout << "ghcheats tests passed" << std::endl;
    return 0;
}
