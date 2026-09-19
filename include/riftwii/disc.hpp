// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/overlay.hpp"
#include "riftwii/patch.hpp"

// Wii disc structures, from https://wiibrew.org/wiki/Wii_disc and
// https://wiibrew.org/wiki/Apploader . Every multi-byte value on disc is
// big-endian; fields marked ">> 2" are stored divided by four and are
// widened to 64 bits before being shifted back. Two views exist: the raw
// disc (header, partition table, partition headers, TMD) and the decrypted
// partition data (data header, apploader, DOL, FST), which is what DI
// returns for reads inside an open partition.
namespace riftwii {

constexpr std::uint64_t kDiscHeaderBytes = 0x80;
constexpr std::uint64_t kPartitionTableOffset = 0x40000;
constexpr std::uint64_t kPartitionHeaderBytes = 0x2C0;
constexpr std::uint64_t kPartitionDataHeaderBytes = 0x440;
constexpr std::uint64_t kApploaderOffset = 0x2440;
constexpr std::uint64_t kApploaderHeaderBytes = 0x20;
constexpr std::uint32_t kWiiMagic = 0x5D1C9EA3u;
constexpr std::uint32_t kGameCubeMagic = 0xC2339F3Du;

struct DiscHeader {
    std::string game_id;       // six characters: disc id, game code, region, maker
    std::uint8_t disc_number = 0;
    std::uint8_t version = 0;
    bool wii_magic = false;
    bool gamecube_magic = false;
    std::string title;         // trimmed
    DiscIdentity identity() const;
};

struct PartitionEntry {
    std::uint64_t offset = 0;  // absolute disc offset of the partition
    std::uint32_t type = 0;    // 0 game, 1 update, 2 channel installer
    std::uint32_t group = 0;   // which of the four partition groups listed it
};

struct PartitionHeader {
    std::uint64_t offset = 0;        // partition start (absolute)
    std::uint32_t tmd_size = 0;
    std::uint64_t tmd_offset = 0;    // absolute
    std::uint32_t cert_size = 0;
    std::uint64_t cert_offset = 0;   // absolute
    std::uint64_t h3_offset = 0;     // absolute
    std::uint64_t data_offset = 0;   // absolute start of the encrypted data area
    std::uint64_t data_size = 0;
};

struct Tmd {
    std::uint64_t sys_version = 0;   // 0x00000001'000000NN for IOS NN
    std::uint64_t title_id = 0;
    std::uint16_t title_version = 0;
    std::uint16_t content_count = 0;
    // The IOS the title asks for, or 0 when sys_version is not an IOS id.
    std::uint32_t required_ios() const;
};

struct PartitionDataHeader {
    std::uint64_t dol_offset = 0;    // inside the partition data
    std::uint64_t fst_offset = 0;
    std::uint64_t fst_size = 0;      // Dolphin shifts the size fields on Wii
    std::uint64_t fst_max_size = 0;  // like the offsets; confirm at E1
};

struct ApploaderHeader {
    std::string date;              // trimmed build date
    std::uint32_t entry = 0;       // PPC address of the apploader entry
    std::uint32_t size = 0;
    std::uint32_t trailer_size = 0;
    std::uint64_t code_offset = 0; // inside the partition data
    std::uint64_t total_size() const { return static_cast<std::uint64_t>(size) + trailer_size; }
};

bool parse_disc_header(const std::uint8_t* data, std::size_t size, DiscHeader& out, std::string& error);
bool read_disc_header(const ByteSource& disc, DiscHeader& out, std::string& error);

bool read_partition_table(const ByteSource& disc, std::vector<PartitionEntry>& out, std::string& error);
// The first type-0 partition, in table order.
bool find_game_partition(const std::vector<PartitionEntry>& table, PartitionEntry& out);

bool read_partition_header(const ByteSource& disc, std::uint64_t partition_offset,
                           PartitionHeader& out, std::string& error);
bool parse_tmd(const std::uint8_t* data, std::size_t size, Tmd& out, std::string& error);
bool read_tmd(const ByteSource& disc, const PartitionHeader& partition, Tmd& out, std::string& error);

// `data` is the decrypted partition data view.
bool read_partition_data_header(const ByteSource& data, PartitionDataHeader& out, std::string& error);
// The four DOL/FST fields as the partition data header stores them at
// kPartitionDataFieldsOffset (each >> 2, big-endian); `out` receives
// kPartitionDataFieldsBytes. Fails when a value is not word-aligned or does
// not fit, or the FST size exceeds its maximum.
constexpr std::uint64_t kPartitionDataFieldsOffset = 0x420;
constexpr std::size_t kPartitionDataFieldsBytes = 16;
bool encode_partition_data_fields(const PartitionDataHeader& header, std::uint8_t* out, std::string& error);
bool read_apploader_header(const ByteSource& data, ApploaderHeader& out, std::string& error);

}  // namespace riftwii
