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
#include "riftwii/patch.hpp"
#include "riftwii/videopatch.hpp"
#include "riftwii/wfcpatch.hpp"

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

// The card log (riftwii/cardlog.hpp): created blank before a game with
// the savegame redirect, filled by the resident runtime during it.
inline constexpr const char* kCardLogPath = "sd:/riftwii/cardlog.bin";
// The last game's card log as text lines, empty when it noted nothing (or
// there is none); a log with events is blanked once read, so each is
// reported once. Needs the card mounted.
std::vector<std::string> TakeCardLog();

struct ProbeOptions {
    // d2x FRAG mode exposes a virtual disc and must not receive the normal
    // cover wait/reset sequence, which would clear its configured image.
    bool virtual_source = false;
    // Only the header and the partition table: the game partition stays
    // shut. Under d2x an opened partition counts as a running title, and
    // until the next IOS reload d2x then refuses to open its USB and SD
    // devices (its stealth mode), which libogc's USB driver needs too.
    bool header_only = false;
};

// Brings the drive up and identifies the disc, its game partition and the
// IOS the title wants. Leaves /dev/di open with the partition open.
bool probe_disc(DiscProbe& out, std::string& error, const ProbeOptions& options = ProbeOptions());

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

// A file whose FST entry must point elsewhere (the virtual window) with a
// new size, applied to the FST the apploader loads. With `create` the
// entry does not exist yet: it (and any missing directory on its path) is
// added, the table is rebuilt and the partition data header's FST size
// follows (E6).
struct FstRelocation {
    std::string disc_path;
    std::uint64_t offset = 0;
    std::uint32_t size = 0;
    bool create = false;
};

struct BootOptions {
    // When the title's IOS cannot be loaded, launch under the current one
    // and report the expected version to the game (Brainslug does this).
    bool allow_ios_fallback = true;
    // Retain the currently loaded IOS. USB FRAG images require this so a
    // reload does not discard the virtual DI backend. boot_game also turns
    // it on automatically when the resident runtime needs the SD card that
    // was successfully mounted under the current IOS. The game still sees
    // its TMD-requested IOS in low memory.
    bool preserve_current_ios = false;
    // E2: install the resident runtime and hook the game's IOS_IoctlAsync
    // (wii/resident.hpp); `resident_gecko` makes it report DI reads over
    // the USB Gecko.
    bool install_resident = false;
    bool resident_gecko = false;
    // E3: same-size replacements the runtime serves from memory (requires
    // install_resident). Built by the caller from the FST while the SD card
    // is still mounted.
    std::vector<MemReplacement> replacements;
    // E5: files the game sees with new content of any size. Their FST
    // entries are moved into the virtual window (riftwii/hook.hpp) in the
    // FST the apploader loads, and the runtime serves them from memory or
    // from SD sectors (requires install_resident).
    std::vector<VirtualFile> virtual_files;
    // E4: same-size replacements served from SD sectors (requires
    // install_resident). Resolved by the caller with wii/sdfile.hpp while
    // the card is mounted; the loader brings the card up again after the
    // IOS reload and hands the runtime the fd. With `verify_sd` the loader
    // first reads every SD run itself and logs a checksum of the bytes.
    std::vector<SdReplacement> sd_replacements;
    bool verify_sd = false;
    // Compiled packages (wii/modplan.hpp): ready-made table entries and
    // the FST relocations they need (requires install_resident).
    std::vector<rt_entry> table_entries;
    std::vector<FstRelocation> relocations;
    // <memory> patches (riftwii/mempatch.hpp), applied once the apploader
    // has loaded the game and before the runtime is installed; `value`
    // must already hold the bytes.
    std::vector<MemoryPatch> memory_patches;
    // A replaced executable (CompiledMod::main_dol): the apploader loads
    // it from memory; the disc's DOL is not read.
    std::vector<std::uint8_t> main_dol;
    // <savegame>: the sd:/ folder the title's data directory is served
    // from by the runtime (requires install_resident and the card; the
    // folder is created when missing). Empty: the save stays on NAND.
    std::string savegame_dir;
    // <savegame clone>: when the folder is created at this launch (or a
    // hidden marker in it says an earlier clone did not run to its end),
    // the runtime copies the title's NAND save into it before the game's
    // first request. Any other existing folder is used as it is.
    bool savegame_clone = false;
    // Riivolution's "file" device for the game (Pulsar's settings and
    // ghosts on the card): served by the resident runtime from the card's
    // root whenever the runtime is installed and the card is up after the
    // IOS reload; otherwise left off with a log line.
    bool file_device = true;
};

// The GameCube controller adapter for Wii U. Auto: on when one is plugged
// in at launch (boot_game settles it to On or Off). Demo: no adapter
// needed, the first empty port presses A (Dolphin tests).
enum class GcAdapterMode { Off, On, Auto, Demo };

// What the menu adds to the next launch, whichever way it boots: video
// mode patches and cheats (a GCT for the Gecko code handler,
// riftwii/cheats.hpp). Applied after the apploader has loaded the game
// and after the packs' memory patches.

struct LaunchExtras {
    std::string game_id;
    GcAdapterMode gc_adapter = GcAdapterMode::Off;
    bool gc_adapter_forced = false;     // the setting is On: the adapter is tried where Automatic leaves it off
    VideoSettings video;  // its target is set at boot, from its mode
    int language = -1;    // riftwii/gamelang.hpp code; -1: the console's
    WfcServer server = WfcServer::Off;  // online play (wii/wfc.hpp)
    std::string wfc_domain;             // the server's domain; empty for WiiLink
    std::vector<std::uint8_t> cheat_gct;  // empty: no cheats
    std::size_t cheat_count = 0;
};
void SetLaunchExtras(LaunchExtras extras);

// A Wii U: its Wii mode has the BC-NAND title (00000001-00000200), a Wii
// has none. Asked once.
bool is_wii_u();

// Reloads IOS, runs the apploader and jumps to the game. Only returns on
// failure. The caller must have shut down its own GUI, audio and pads;
// the SD card is unmounted here because the IOS reload kills its fd.
bool boot_game(const DiscProbe& probe, const BootOptions& options, std::string& error);

}  // namespace riftwii::wii
