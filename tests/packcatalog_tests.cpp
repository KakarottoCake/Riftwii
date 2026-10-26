// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "packcatalog.hpp"

namespace {
int failures = 0;
std::vector<riftwii::wii::PackFile> files;
std::map<std::string, std::string> contents;
std::vector<std::string> reads;
unsigned enumerations = 0;
std::vector<riftwii::wii::CodeBuildFile> builds = {
    {"mod/codes/RMGE01.gct", "mod", "sd:/mod/codes", "sd:/mod/codes/RMGE01.gct", "RMGE01", true}};

void expect(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
}
}

namespace riftwii::wii {
void logf(const char*, ...) {}

std::vector<PackFile> ListPackFiles(std::size_t limit, bool& limited, bool keep_text) {
    expect(limit == 256 && keep_text, "Home's full index is retained with its already-read text");
    ++enumerations;
    limited = false;
    return files;
}

std::string ReadPackText(const std::string& path) {
    reads.push_back(path);
    return contents[path];
}

std::vector<CodeBuildFile> ListCodeBuilds() {
    return builds;
}
}

int main() {
    using namespace riftwii::wii;

    files = {{"local.xml", "sd:/riivolution/local.xml", "already read", true},
             {"remote.xml", "sd:/riftwii/riifs/server/remote.xml", "", false}};
    contents[files[1].path] = "remote pack";
    const PackageCatalog first = ReadPackageCatalog();
    expect(first.ready, "a finished snapshot is marked ready");
    expect(reads.size() == 1 && reads[0] == files[1].path, "local XMLs are not opened twice");
    expect(first.files[0].text == "already read" && first.files[1].text == "remote pack",
           "local and network packs keep their exact text");
    expect(first.code_builds.size() == 1 && first.code_builds[0].picked,
           "code mods and hand-picked mods survive the handover");

    reads.clear();
    files = {{"full.xml", "full", std::string(kPackTextCacheBytes - 4, 'x'), true},
             {"large.xml", "large", "", false},
             {"small.xml", "small", "", false}};
    contents["large"] = std::string(20, 'y');
    contents["small"] = "fits";
    const PackageCatalog bounded = ReadPackageCatalog();
    expect(bounded.files.size() == 3, "a full text cache never removes a pack from the list");
    expect(!bounded.files[1].text_cached && bounded.files[1].path == "large",
           "a pack past the memory limit remains available for a fresh read");
    expect(bounded.files[2].text_cached && bounded.files[2].text == "fits",
           "a smaller later pack can fill the remaining cache space");
    std::size_t cached_bytes = 0;
    for (const PackFile& file : bounded.files) cached_bytes += file.text.size();
    expect(cached_bytes == kPackTextCacheBytes, "the retained XML text stays within the memory limit");

    files.clear();
    const PackageCatalog empty = ReadPackageCatalog();
    expect(empty.ready && empty.files.empty(), "an empty card is a known snapshot, not a reason to scan again");
    expect(first.files.size() == 2 && first.files[1].text == "remote pack",
           "a new drive scan cannot mutate a snapshot already handed to the menu");
    expect(enumerations == 3, "each explicit scan reads a fresh catalog");

    // A pack that does not fit must still give Home its badge without a
    // second read; cached text must keep the same game filters too.
    reads.clear();
    files = {{"cached.xml", "cached", "<wiidisc><id game=\"RMG\"/></wiidisc>", true},
             {"oversized.xml", "oversized", "", false}};
    contents["oversized"] = "<wiidisc><id game=\"RMC\"/></wiidisc>" + std::string(kPackTextCacheBytes, ' ');
    riftwii::PackIndex index;
    const PackageCatalog indexed = ReadPackageCatalog(&index);
    expect(index.has_packs("RMGE01") && index.has_packs("RMCE01"),
           "Home indexes both retained and uncached packs");
    expect(reads.size() == 1 && !indexed.files[1].text_cached,
           "indexing a pack beyond the cache limit reads it only once");

    // Match the old full parse's visibility and details, including
    // revision/region filters, invalid packs and packs for every game.
    builds.clear();
    const std::string options = "<options><section name=\"Test\"><option name=\"Mode\">"
                                "<choice name=\"On\"><patch id=\"p\"/></choice></option></section></options>"
                                "<patch id=\"p\"><memory offset=\"0x80004000\" value=\"60000000\"/></patch>";
    const std::vector<std::string> xmls = {
        "<wiidisc version=\"1\"><id game=\"RMG\"/>" + options + "</wiidisc>",
        "<wiidisc version=\"1\"><id game=\"RMC\"/>" + options + "</wiidisc>",
        "<wiidisc version=\"1\"><id game=\"RMG\" revision=\"1\"/>" + options + "</wiidisc>",
        "<wiidisc version=\"1\"><id game=\"RMG\" developer=\"02\"><region type=\"P\"/></id>" + options + "</wiidisc>",
        "<wiidisc version=\"1\">" + options + "</wiidisc>",
        "<wiidisc><id game=\"RMG\"/><broken>",
        "<wiidisc><id game=\"RMC\"/><broken>",
        "<unrelated/>"};
    files.clear();
    for (std::size_t i = 0; i < xmls.size(); ++i) {
        const std::string name = "pack-" + std::to_string(i) + ".xml";
        files.push_back({name, name, xmls[i], true});
    }
    PackageCatalog summarized = ReadPackageCatalog();
    riftwii::DiscIdentity disc;
    disc.id = "RMGE01";
    riftwii::LaunchModel expected, actual;
    for (const PackFile& pack : files) expected.add(pack.file, pack.path, pack.text, &disc);
    AddCatalogPackages(summarized, actual, &disc);
    expect(actual.packages.size() == expected.packages.size(), "summaries preserve the full pack list");
    for (std::size_t i = 0; i < actual.packages.size(); ++i) {
        const auto& got = actual.packages[i];
        const auto& want = expected.packages[i];
        expect(got.file == want.file && got.valid == want.valid && got.for_disc == want.for_disc &&
               got.detail == want.detail, "a summary preserves validity, visibility and the displayed reason");
        if (want.for_disc) {
            expect(got.package.options.size() == want.package.options.size() &&
                   got.package.patches.size() == want.package.patches.size(),
                   "matching and universal packs retain their complete options and patches");
        } else if (want.valid) {
            expect(got.package.options.empty() && got.package.patches.empty(),
                   "an unrelated pack does not build unused options and patches");
        }
    }

    // Even a pack beyond the text limit requires no card access when it
    // belongs to another game. A matching pack still reads its full text.
    summarized.files[1].text.clear();
    summarized.files[1].text_cached = false;
    contents[summarized.files[1].path] = xmls[1];
    reads.clear();
    actual.packages.clear();
    AddCatalogPackages(summarized, actual, &disc);
    expect(reads.empty(), "unrelated packs beyond the text cache do not touch storage");
    disc.id = "RMCE01";
    actual.packages.clear();
    AddCatalogPackages(summarized, actual, &disc);
    expect(reads.size() == 1 && reads[0] == summarized.files[1].path,
           "an uncached pack is read when its own game is selected");

    files = {{"folder.xml", "folder", "<wiidisc version=\"1\" root=\"/MoD/\">" + options + "</wiidisc>", true}};
    builds = {{"auto", "mod", "sd:/mod/codes", "sd:/mod/codes/RMGE01.gct", "RMGE01", false},
              {"picked", "mod", "sd:/mod/codes", "sd:/mod/codes/RMGE01.gct", "RMGE01", true}};
    const PackageCatalog filtered = ReadPackageCatalog();
    expect(filtered.code_builds.size() == 1 && filtered.code_builds[0].picked,
           "folder checks ignore case and retain a code build chosen by hand");
    return failures == 0 ? 0 : 1;
}
