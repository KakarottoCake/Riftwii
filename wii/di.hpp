// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "riftwii/overlay.hpp"

// Thin client for the IOS disc interface (/dev/di), written from the
// command table documented at https://wiibrew.org/wiki//dev/di . Offsets
// are in 4-byte words as the drive expects; buffers handed to read
// functions must be 32-byte aligned with lengths that are multiples of
// 32, because IOS DMAs straight into them. The ByteSource adapters below
// hide that for the parsers in riftwii/disc.hpp.
namespace riftwii::di {

// DI reply codes (the ioctl return value, positive on completion).
constexpr int kReplySuccess = 1;
constexpr int kReplyCoverClosed = 4;

// Opens /dev/di. Fails when IOS refuses (no DI module, or already open).
bool open(std::string& error);
void close();
bool is_open();

// Cover / drive state.
bool cover_status(bool& disc_inserted, std::string& error);   // 0x88
bool wait_for_cover_close(std::string& error);                // 0x79
bool reset(bool spin_up, std::string& error);                 // 0x8A
bool inquiry(std::uint8_t out32[32], std::string& error);     // 0x12

// d2x custom DIP commands. They are deliberately separate from the normal
// DI path: a plain IOS does not implement them. F9 reads a native-endian
// fragment list from MEM1/MEM2 after the caller flushes its full buffer.
bool probe_d2x(std::uint32_t& mode, std::string& error);       // 0xFA
bool disable_reset(std::string& error);                         // 0xF6
bool configure_frag(std::uint32_t device, const void* list32, std::uint32_t bytes, std::string& error); // 0xF9
inline bool configure_frag_usb(const void* list32, std::uint32_t bytes, std::string& error) {
    return configure_frag(1, list32, bytes, error);
}

// Disc ID (the first 0x20 bytes of the disc, copied to `out32`).
bool read_disc_id(std::uint8_t out32[32], std::string& error);  // 0x70

// Reads that do not need an open partition (system area, first 0x50000
// bytes). `word_offset` is bytes / 4.
bool read_unencrypted(void* buffer32, std::uint32_t length, std::uint32_t word_offset,
                      std::string& error);                    // 0x8D

// Opens the partition at `word_offset` and returns its TMD (up to 0x49E4
// bytes) plus the ES verification result. This also sets the title
// context in ES, which the game relies on.
constexpr std::size_t kTmdBufferBytes = 0x49E4;
bool open_partition(std::uint32_t word_offset, std::uint8_t* tmd_out32, std::size_t tmd_capacity,
                    std::int32_t& es_result, std::string& error);  // 0x8B
bool close_partition(std::string& error);                     // 0x8C

// Decrypted read inside the open partition.
bool read(void* buffer32, std::uint32_t length, std::uint32_t word_offset,
          std::string& error);                                // 0x71

// RVZ games: the disc d2x (or Dolphin) presents holds only the headers,
// not the partitions' data. `resolve` is asked, for each partition opened,
// by its disc offset, for a source of its decrypted data; what it returns
// answers partition reads (0x71) until the partition is closed. Null from
// it, or no resolver: the drive answers.
using PartitionResolver = std::function<const ByteSource*(std::uint64_t partition_offset)>;
void set_partition_resolver(PartitionResolver resolve);
bool has_partition_resolver();

// The raw reply of the last call, for diagnostics.
int last_reply();

// ByteSource over the unencrypted system area (0 .. 0x50000), for the
// disc header and partition table parsers. size() reports the nominal
// disc size so partition offsets validate; reads past the system area
// fail (the drive refuses them anyway).
class SystemAreaSource final : public ByteSource {
public:
    static constexpr std::uint64_t kReadableBytes = 0x50000;
    static constexpr std::uint64_t kDualLayerBytes = 0x1FB4E0000ull;
    explicit SystemAreaSource(std::uint64_t disc_bytes = kDualLayerBytes) : bytes_(disc_bytes) {}
    std::uint64_t size() const override { return bytes_; }
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override;

private:
    std::uint64_t bytes_;
};

// ByteSource over the decrypted data of the open partition.
class PartitionSource final : public ByteSource {
public:
    explicit PartitionSource(std::uint64_t bytes = 0xFFFFFFFFull << 2) : bytes_(bytes) {}
    std::uint64_t size() const override { return bytes_; }
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override;

private:
    std::uint64_t bytes_;
};

}  // namespace riftwii::di
