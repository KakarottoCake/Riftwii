// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// A plugin file, <name>.<version>.rwp (docs/PLUGINS.md), as
// tools/make_plugin.py writes it. All numbers big-endian.
//
//   0    "RWPL"
//   4    u8 header version (1), three zero bytes
//   8    char[16] name, A-Za-z0-9_-, NUL-padded: the file's base name
//   24   u32 the plugin's own version
//   28   u32 API version (RWP_API_VERSION, include/riftwii/plugin_api.h)
//   32   char[32] build stamp, NUL-padded: the DOL's ("2610-460-g1a2b3c4d")
//   64   u32 init offset in the image
//   68   u32 shutdown offset (0: none)
//   72   u32 offset of the plugin's rwp_plugin table
//   76   u32 image bytes
//   80   u32 .bss bytes (cleared by the loader, right after the image)
//   84   u32 relocation count
//   88   u32 state bytes the plugin asks for (after the .bss)
//   92   u32 flags (RWP_FLAG_*)
//   96   u8[32] SHA-256 of the whole file with this field zeroed
//   128  the image, linked at 0
//   then the relocations, 12 bytes each: u32 offset in the image, u32
//   type (R_PPC_ADDR32, _ADDR16_LO, _ADDR16_HI, _ADDR16_HA), u32 target
//   offset; the loader writes load address + target there.
//
// Everything here is pure and testable on the host: the Wii loader
// (wii/core/plugins.cpp) reads the file and calls these in order.
namespace riftwii {

constexpr std::size_t kPluginHeaderBytes = 128;
constexpr std::uint8_t kPluginHeaderVersion = 1;
constexpr std::size_t kPluginNameBytes = 16;
constexpr std::size_t kPluginStampBytes = 32;
constexpr std::size_t kPluginHashOffset = 96;
constexpr std::size_t kPluginRelocBytes = 12;
// The rwp_plugin table of API 2 as the Wii lays it out (plugin_api.h):
// size, API version, version, flags, then the probe, shutdown and launch
// pointers, 32 bits each. Every byte of it the loader reads must be inside
// the image; wii/core/plugins.cpp checks it against sizeof(rwp_plugin).
constexpr std::uint32_t kPluginTableBytes = 28;
// A sanity bound before any allocation: far more than a plugin needs.
constexpr std::uint32_t kPluginMaxRelocs = 65536;

constexpr std::uint32_t kRelocAddr32 = 1;
constexpr std::uint32_t kRelocAddr16Lo = 4;
constexpr std::uint32_t kRelocAddr16Hi = 5;
constexpr std::uint32_t kRelocAddr16Ha = 6;

struct PluginHeader {
    std::string name;
    std::uint32_t plugin_version = 0;
    std::uint32_t api_version = 0;
    std::string stamp;
    std::uint32_t init_offset = 0;
    std::uint32_t shutdown_offset = 0;
    std::uint32_t table_offset = 0;
    std::uint32_t image_bytes = 0;
    std::uint32_t bss_bytes = 0;
    std::uint32_t reloc_count = 0;
    std::uint32_t state_bytes = 0;
    std::uint32_t flags = 0;
    std::uint8_t hash[32] = {};

    // Image, .bss and state together, each 32-byte aligned: what the
    // loader takes from the arena.
    std::uint32_t memory_bytes() const;
    // The whole file: header, image, relocations.
    std::uint64_t file_bytes() const;
};

// Why a plugin is not run. The loader turns each into one log line and
// one message on screen; nothing is ever loaded in a degraded mode.
enum class PluginRefusal {
    None,
    Missing,      // no file for this version
    Damaged,      // the header or a size is wrong, or the file is cut short
    Hash,         // the SHA-256 does not match: damaged on the card
    Api,          // built for another RWP_API_VERSION
    Stamp,        // built with another RiftWii (build stamp)
    NoMemory,     // the arena has no room for it
    InitFailed,   // its init said no
};

const char* plugin_refusal_name(PluginRefusal refusal);

// Whether `name` is a plugin name: 1 to 16 of A-Za-z0-9_-.
bool plugin_name_valid(const std::string& name);

// The checks that need only the 128 header bytes, in the order of
// docs/PLUGINS.md: magic, header version, the name (it must be
// `expected_name`), every size inside `file_bytes` and the whole of image,
// .bss and state inside `budget`, the entry offsets and the whole
// rwp_plugin table (kPluginTableBytes) inside the image. Run before
// anything is allocated. Damaged with `why` when one fails.
PluginRefusal plugin_parse_header(const std::uint8_t* bytes, std::size_t available, std::uint64_t file_bytes,
                                  const std::string& expected_name, std::uint32_t budget, PluginHeader& out,
                                  std::string& why);

// The file's SHA-256 as the header says it should be, from the header
// bytes (its hash field is zeroed for the sum) and everything after them.
// A caller reading in pieces uses plugin_hash_begin and Sha256 itself.
bool plugin_hash_matches(const std::uint8_t* header_bytes, const std::uint8_t* rest, std::size_t rest_bytes);

// The header with its hash field zeroed, to start the sum with.
void plugin_hash_header(const std::uint8_t* header_bytes, std::uint8_t (&out)[kPluginHeaderBytes]);

// API version, then build stamp (the API number gives the clearer message
// when only the table changed). Api or Stamp, with `why` naming both sides.
PluginRefusal plugin_check_versions(const PluginHeader& header, std::uint32_t api_version, const std::string& stamp,
                                    std::string& why);

// Applies the relocation list to an image loaded at `base` (`image`
// holds image_bytes, followed in memory by the .bss). Every record is
// checked first: its field inside the image, its target inside image and
// .bss, a known type. False with `why`, and the image untouched, when one
// is wrong.
bool plugin_relocate(std::uint8_t* image, const PluginHeader& header, const std::uint8_t* relocs, std::uint32_t base,
                     std::string& why);

// The plugin's rwp_plugin table in an image relocated to `base` (the
// header already put all kPluginTableBytes of it inside the image): its
// size covers the table, its API is `api_version`, its probe and launch
// are each NULL or code inside the image, and its shutdown is the header's (both none, or
// the same address). Damaged with `why` otherwise, before any of its code
// runs.
PluginRefusal plugin_check_table(const std::uint8_t* image, const PluginHeader& header, std::uint32_t base,
                                 std::uint32_t api_version, std::string& why);

// "<name>.<version>.rwp": the file of `name` for RiftWii `version`.
std::string plugin_file_name(const std::string& name, const std::string& version);

// Splits "<name>.<version>.rwp"; false for anything else (another
// extension, a name that is not a plugin name, a second dot, a folder).
bool plugin_file_parse(const std::string& file, std::string& name, std::string& version);

// For the clean-up after an update: whether `file` is a plugin file of
// another version ("selftest.2610-12.rwp" while `version` is 2610-13).
// `name` comes back with the plugin's name.
bool plugin_file_of_other_version(const std::string& file, const std::string& version, std::string& name);

// The folder the plugins of a DOL started as `argv0` are in: next to it
// ("sd:/apps/riftwii/boot.dol" gives "sd:/apps/riftwii/plugins/"; an older
// loader's "/apps/riftwii/boot.dol" is the SD card's), else
// sd:/apps/riftwii/plugins/ when argv0 is empty or not a path on a drive.
std::string plugin_directory_for(const std::string& argv0);

// A loaded plugin, for crash reports.
struct PluginRange {
    const char* name;  // the plugin's, or what it registered
    std::uint32_t start;
    std::uint32_t bytes;
};

// Writes "plugin <name> +0x<offset>" for `address` when one of `ranges`
// holds it; false (and `out` untouched) when none does. No allocation:
// the crash screen calls it.
bool plugin_describe_address(std::uint32_t address, const PluginRange* ranges, std::size_t count, char* out,
                             std::size_t out_bytes);

}  // namespace riftwii
