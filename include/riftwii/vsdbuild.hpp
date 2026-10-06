// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// A virtual SD card's image made from files on the menu's side: the menu
// lists what goes in (a code build's folders, wii/vsdmake.hpp), the card
// is planned here, then written front to back in one pass. The card is
// laid out as Dolphin's and the netplay builds' sd.raw are: FAT32 with no
// partition table, 512-byte sectors, 32 reserved sectors, two FATs, every
// folder and file in one run of clusters, long names where a name is not
// a plain 8.3 one. The rest of the card is free space for what the game
// writes (replays, custom stages, settings).
namespace riftwii {

struct VsdItem {
    std::string path;        // on the new card, "/Project+/codes/RSBE01.gct" (UTF-8)
    bool directory = false;  // a folder may be listed before or after what it holds
    std::uint64_t size = 0;  // a file's
};

constexpr std::uint32_t kVsdSectorBytes = 512;
constexpr std::uint32_t kVsdReservedSectors = 32;
constexpr std::uint64_t kVsdMaxFileBytes = 0xFFFFFFFFull;  // FAT32's

struct VsdPlan {
    std::uint32_t cluster_bytes = 0;
    std::uint32_t fat_sectors = 0;      // each of the two
    std::uint32_t data_start = 0;       // sector of cluster 2
    std::uint32_t cluster_count = 0;    // data clusters, 2 .. cluster_count + 1
    std::uint32_t used_clusters = 0;    // by the folders and files, from cluster 2
    std::uint64_t total_sectors = 0;
    std::uint64_t image_bytes = 0;      // total_sectors * 512
    std::uint64_t file_bytes = 0;       // the files' own bytes
    std::uint32_t files = 0, folders = 0;
    std::uint32_t serial = 0x52574944;  // the volume's serial number
    std::uint16_t fat_date = (46u << 9) | (10u << 5) | 6u;  // 2026-10-06
    std::uint16_t fat_time = 0;

    struct Node {
        std::string name;           // UTF-8; the root's is empty
        int parent = -1;
        bool directory = false;
        std::uint64_t size = 0;     // a file's bytes; a folder's entries' bytes
        std::uint32_t first_cluster = 0;  // 0: an empty file
        std::uint32_t clusters = 0;
        std::vector<int> children;
        std::string short_name;     // 11 bytes, as stored ("RSBE01  GCT")
        bool long_name = false;     // needs long-name entries
        std::string path;           // "/Project+/codes/RSBE01.gct" ("/" for the root)
    };
    std::vector<Node> nodes;        // [0] is the root; the rest in cluster order
};

// Plans a card holding `items` with at least `spare_bytes` free. False,
// with `error`, for a path or name a FAT32 card cannot hold (a name over
// 255 characters, a file of 4 GiB or more, one path listed twice).
bool plan_vsd_image(const std::vector<VsdItem>& items, std::uint64_t spare_bytes, VsdPlan& out, std::string& error);

// Where the image's bytes go, in order.
class VsdSink {
public:
    virtual ~VsdSink() = default;
    virtual bool write(const std::uint8_t* data, std::size_t length) = 0;
};

// The bytes of the source file for a card path ("/Project+/info.xml").
using VsdRead = std::function<bool(const std::string& path, std::uint64_t offset, std::uint8_t* out, std::size_t length)>;
// Every few megabytes and at each file: false stops the writing.
using VsdProgress = std::function<bool(std::uint64_t written, const std::string& path)>;

// Writes the whole image planned: boot sectors, both FATs, the folders and
// files, then the free space (zeros). `buffer` is working room, at least
// one cluster and best a few hundred KB.
bool write_vsd_image(const VsdPlan& plan, VsdSink& sink, const VsdRead& read, const VsdProgress& progress,
                     std::vector<std::uint8_t>& buffer, std::string& error);

// The top folders a build's code list names as "/name/" (its codes load
// files from them, as Project+'s do from "/Project+/pf/"), in the order
// first named, each once (ASCII case-insensitive). Names of 1 to 64
// characters a FAT32 folder may have; the menu keeps those that are on
// the SD card.
std::vector<std::string> vsd_gct_folders(const std::vector<std::uint8_t>& gct);

// The 8.3 name a long name is stored under in a folder already holding
// `taken` (each 11 bytes, as stored), and whether long-name entries are
// needed. For tests and the planner.
std::string vsd_short_name(const std::string& name, const std::vector<std::string>& taken, bool& needs_long);

}  // namespace riftwii
