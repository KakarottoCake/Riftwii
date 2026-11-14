// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "riftwii/ctgppackages.hpp"

// CTGP-R's update check and download, as the channel does them: one TCP
// connection to the update server. The client sends a "CTG0" request with
// the console's installed package versions; the reply says whether an update
// is waiting and how large it is; the client then asks for the update and
// the server streams it (the DIA0 file of riftwii/ctgpupdate.hpp).
//
// The request reports the versions exactly as installed: the records of
// /PACKAGES.BIN on the package disk (blob.bin) and on the hash disk
// (jelly.bin), merged and sorted by name.
namespace riftwii {

constexpr std::uint32_t kCtgpCheckClientVersion = 0x00040250;
constexpr std::uint16_t kCtgpCheckPort = 49382;
constexpr std::size_t kCtgpCheckBlockBytes = 0x80;
constexpr std::size_t kCtgpCheckMaxRecords = 32;
// The largest update RiftWii downloads: the channel's own bound on a file.
constexpr std::uint32_t kCtgpCheckMaxUpdateBytes = 64u * 1024u * 1024u;

// The installed packages of both disks, merged and sorted the way the
// request carries them (by the eight name bytes). A name on both disks is
// kept once, the package disk's.
std::vector<CtgpPackage> ctgp_check_records(const std::vector<CtgpPackage>& package_disk,
                                            const std::vector<CtgpPackage>& hash_disk);

// The request: "CTG0", the client version, a zero byte, the record count,
// the console's MAC, a 0x80-byte block, then one 16-byte record per package
// (name, version, the flags' type byte). (count + 9) * 16 bytes. RiftWii
// sends the block as zeros, so none of its own memory leaves the console.
bool ctgp_check_request(const std::array<std::uint8_t, 6>& mac, const std::vector<CtgpPackage>& records, std::vector<std::uint8_t>& out,
                        std::string& error);

// The reply's length from its first word: 0x38 plus that word, which is
// 0x52 (an update is waiting) or 0x56 (none).
constexpr std::size_t kCtgpCheckReplyLengthBytes = 4;
bool ctgp_check_reply_size(const std::uint8_t* first, std::size_t& total, std::string& error);

struct CtgpCheckReply {
    std::uint32_t timestamp = 0;   // the server's clock, seconds since 1970
    std::uint8_t status = 0;
    std::uint8_t flag = 0;
    bool update_waiting = false;
    std::uint32_t update_bytes = 0;  // the DIA0's size when an update waits
};

// Checks the reply's layout: both signature lengths fit inside it, and an
// update's size is within kCtgpCheckMaxUpdateBytes. The signatures are not
// verified here; the download is checked as a DIA0 before it is applied.
bool ctgp_check_parse_reply(const std::uint8_t* data, std::size_t size, CtgpCheckReply& out, std::string& error);

// The download request: the offset to start at and the number of bytes,
// big-endian. The client ends the session with offset and size zero.
std::array<std::uint8_t, 8> ctgp_download_request(std::uint32_t offset, std::uint32_t size);

// One connection's traffic. `receive_some` fills up to `max` bytes and sets
// `got` (0 when the server closed); false on a timeout or a socket error.
struct CtgpCheckIo {
    std::function<bool(const std::uint8_t* data, std::size_t length)> send;
    std::function<bool(std::uint8_t* out, std::size_t max, std::size_t& got)> receive_some;
};

// Sends the request and reads the whole reply.
bool ctgp_check_exchange(const CtgpCheckIo& io, const std::vector<std::uint8_t>& request, CtgpCheckReply& out,
                         std::string& error);

// After a reply that offers an update: asks for all `size` bytes and passes
// them to `sink` as they arrive (pieces of at most `piece` bytes), then
// ends the session. `progress` (may be empty) gets the bytes so far. The
// buffer is one piece, so a 12 MB update never sits in memory whole.
using CtgpDownloadSink = std::function<bool(const std::uint8_t* data, std::size_t length)>;
using CtgpDownloadProgress = std::function<bool(std::uint64_t done, std::uint64_t total)>;
bool ctgp_download(const CtgpCheckIo& io, std::uint32_t size, const CtgpDownloadSink& sink,
                   const CtgpDownloadProgress& progress, std::string& error, std::size_t piece = 32 * 1024);

}  // namespace riftwii
