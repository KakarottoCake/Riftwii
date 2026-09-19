// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "riftwii/disc.hpp"
#include "riftwii/dol.hpp"
#include "riftwii/fst.hpp"
#include "riftwii/hook.hpp"

// E1: boot an unmodified disc the way the System Menu would, from a
// homebrew loader. The sequence (drive reset, disc id, partition table,
// open partition for the TMD, reload the title's IOS, run the apploader
// with our own read callback, fill the low-memory globals, jump) follows
// the public description of the Wii boot process and Brainslug's MIT
// loader (see NOTICE.md); every disc structure is parsed by the
// host-tested code in riftwii/disc.hpp and riftwii/fst.hpp.
namespace riftwii::wii {

struct DiscProbe {
    std::uint8_t disc_id[32] = {};      // the raw 0x20 bytes at disc offset 0
    DiscHeader header;
    PartitionEntry partition;           // the game partition
    Tmd tmd;                            // from the drive's open-partition reply
    std::vector<std::uint8_t> tmd_bytes;  // the raw reply, for dumps
    std::int32_t es_result = 0;         // ES verification result reported by DI
    int running_ios = 0;                // IOS at probe time
};

// Brings the drive up and identifies the disc, its game partition and the
// IOS the title wants. Leaves /dev/di open with the partition open.
bool probe_disc(DiscProbe& out, std::string& error);

struct OpenedPartition {
    PartitionDataHeader data_header;
    ApploaderHeader apploader;
    Fst fst;
    std::vector<std::uint8_t> fst_bytes;
};

// Reads the partition data header, the FST and the apploader header of
// the (already open) game partition.
bool read_partition_layout(OpenedPartition& out, std::string& error);

// Copies a disc file (absolute FST path) to an SD path through DI.
bool dump_file(const OpenedPartition& partition, const std::string& disc_path,
               const std::string& sd_path, std::string& error);
// Writes the raw structures used above to `sd_dir` (header.bin,
// partitions.bin, tmd.bin, datahdr.bin, fst.bin, apploader.bin).
bool dump_metadata(const DiscProbe& probe, const OpenedPartition& partition, const std::string& sd_dir,
                   std::string& error);
// Copies the partition's main.dol (header plus every section) to `sd_path`.
bool dump_dol(const OpenedPartition& partition, const std::string& sd_path, std::string& error);

struct BootOptions {
    // When the title's IOS cannot be loaded, launch under the current one
    // and report the expected version to the game (Brainslug does this).
    bool allow_ios_fallback = true;
    // E2: install the resident runtime and hook the game's IOS_IoctlAsync
    // (wii/resident.hpp); `resident_gecko` makes it report DI reads over
    // the USB Gecko.
    bool install_resident = false;
    bool resident_gecko = false;
    // E3: same-size replacements the runtime serves from memory (requires
    // install_resident). Built by the caller from the FST while the SD card
    // is still mounted.
    std::vector<MemReplacement> replacements;
};

// Reloads IOS, runs the apploader and jumps to the game. Only returns on
// failure. The caller must have shut down its own GUI, audio and pads;
// the SD card is unmounted here because the IOS reload kills its fd.
bool boot_game(const DiscProbe& probe, const BootOptions& options, std::string& error);

}  // namespace riftwii::wii
