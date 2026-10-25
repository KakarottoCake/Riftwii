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

void expect(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
}
}

namespace riftwii::wii {
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
    return {{"mod/codes/RMGE01.gct", "mod", "sd:/mod/codes", "sd:/mod/codes/RMGE01.gct", "RMGE01", true}};
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
    return failures == 0 ? 0 : 1;
}
