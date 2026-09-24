// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "boot.hpp"
#include "riftwii/launch.hpp"
#include "riftwii/patch.hpp"

// Packs served from a PC over RiiFS (docs/RIIFS.md). A server is named by
// <network protocol="riifs" address="…" port="…"/> in any XML in
// sd:/riivolution (the way Riivolution is pointed at one), or found on the
// local network when the Network packs setting is on. Its packs are
// copied to a cache on the card, sd:/riftwii/riifs/<address>_<port>/,
// laid out as on the server: the pack list when the menu scans, the files
// a launch needs just before it. The game then runs from the card like any
// other pack; saves stay in the cache.
namespace riftwii::wii {

constexpr const char* kNetCacheDir = "sd:/riftwii/riifs";

struct PackFile {
    std::string file;  // the name the menu keys choices by: "mod.xml", "mod.xml @ 192.168.1.20:1137"
    std::string path;  // "sd:/riivolution/mod.xml", "sd:/riftwii/riifs/…/riivolution/mod.xml"
};
// Every pack XML: sd:/riivolution and sd:/apps/riivolution first, then
// each server's cached lists (the same two folders).
std::vector<PackFile> ListPackFiles(std::size_t limit, bool& limited);

// The folder a pack XML's paths start from, as the pack sees it:
// "/riivolution" or "/apps/riivolution" (for a network pack, the folder on
// the server). Pass it to parse_package.
std::string PackFolderOf(const std::string& xml_sd_path);
// The disc identity packs are planned for, with the console ID that
// {$__ngid} names.
DiscIdentity PackIdentity(const DiscProbe& probe);

// The card folder a cached network pack's paths are relative to
// ("/riftwii/riifs/192.168.1.20_1137"); empty for a pack on the card.
std::string NetworkRootOf(const std::string& xml_sd_path);
// Prefixes every external path of `plan` with `root` (see NetworkRootOf).
void RebasePlan(Plan& plan, const std::string& root);

bool NetworkPacksEnabled();
void SetNetworkPacksEnabled(bool on);
// The next launch copies every file of its network packs again, same
// size or not (RiiFS reports no modification times).
void ForceNextSync();

// Menu scan: fetches each server's pack list into the cache. Returns a
// line for the status bar, empty when no server is configured. `busy`
// hears a status line before the network comes up (it takes seconds).
std::string RefreshNetworkPacks(const std::function<void(const char*)>& busy);

// Launch: copies what the chosen options of the network packs among
// `packages` need, before the compile reads the card. A server that is
// not reachable leaves the last copy in use, with a warning. The same
// selection twice in a row (the menu's check, then the launch) copies
// once.
bool SyncNetworkPackages(const std::vector<PackageChoices>& packages, const DiscProbe& probe,
                         std::vector<std::string>& warnings, std::string& error);

}  // namespace riftwii::wii
