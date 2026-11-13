// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgpcheck.hpp"

#include <algorithm>
#include <cstring>

namespace riftwii {

namespace {

void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(std::uint8_t(value >> 24));
    out.push_back(std::uint8_t(value >> 16));
    out.push_back(std::uint8_t(value >> 8));
    out.push_back(std::uint8_t(value));
}

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

std::array<std::uint8_t, 8> name_bytes(const std::string& name) {
    std::array<std::uint8_t, 8> out{};
    std::memcpy(out.data(), name.data(), std::min<std::size_t>(name.size(), out.size()));
    return out;
}

constexpr std::uint32_t kReplyWithUpdate = 0x52;
constexpr std::uint32_t kReplyNoUpdate = 0x56;
constexpr std::size_t kReplyBase = 0x38;
constexpr std::size_t kMaxSignature = 48;

}  // namespace

std::vector<CtgpPackage> ctgp_check_records(const std::vector<CtgpPackage>& package_disk,
                                            const std::vector<CtgpPackage>& hash_disk) {
    std::vector<CtgpPackage> out;
    for (const auto* list : {&package_disk, &hash_disk}) {
        for (const CtgpPackage& p : *list) {
            const auto same = [&](const CtgpPackage& q) { return name_bytes(q.name) == name_bytes(p.name); };
            if (std::find_if(out.begin(), out.end(), same) == out.end()) out.push_back(p);
        }
    }
    std::sort(out.begin(), out.end(), [](const CtgpPackage& a, const CtgpPackage& b) {
        return name_bytes(a.name) < name_bytes(b.name);
    });
    return out;
}

bool ctgp_check_request(const std::array<std::uint8_t, 6>& mac, const std::vector<CtgpPackage>& records, std::vector<std::uint8_t>& out,
                        std::string& error) {
    if (records.empty() || records.size() > kCtgpCheckMaxRecords) {
        error = "the update check needs 1 to " + std::to_string(kCtgpCheckMaxRecords) + " packages, not " +
                std::to_string(records.size());
        return false;
    }
    out.clear();
    out.reserve((records.size() + 9) * 16);
    out.insert(out.end(), {'C', 'T', 'G', '0'});
    put32(out, kCtgpCheckClientVersion);
    out.push_back(0);
    out.push_back(std::uint8_t(records.size()));
    out.insert(out.end(), mac.begin(), mac.end());
    out.insert(out.end(), kCtgpCheckBlockBytes, 0);
    for (const CtgpPackage& p : records) {
        const auto name = name_bytes(p.name);
        out.insert(out.end(), name.begin(), name.end());
        put32(out, p.version);
        put32(out, p.flags & 0x0F000000u);
    }
    return true;
}

bool ctgp_check_reply_size(const std::uint8_t* first, std::size_t& total, std::string& error) {
    const std::uint32_t length = be32(first);
    if (length != kReplyWithUpdate && length != kReplyNoUpdate) {
        error = "the update server's reply has an unknown length (" + std::to_string(length) + ")";
        return false;
    }
    total = kReplyBase + length;
    return true;
}

bool ctgp_check_parse_reply(const std::uint8_t* data, std::size_t size, CtgpCheckReply& out, std::string& error) {
    std::size_t total = 0;
    if (size < kCtgpCheckReplyLengthBytes || !ctgp_check_reply_size(data, total, error)) {
        if (error.empty()) error = "the update server's reply is too short";
        return false;
    }
    if (size != total) {
        error = "the update server's reply is " + std::to_string(size) + " bytes, not " + std::to_string(total);
        return false;
    }
    const std::uint32_t signature = be32(data + 4);
    if (signature > kMaxSignature || 8 + signature > 0x4C) {
        error = "the update server's reply has a bad signature length";
        return false;
    }
    out = CtgpCheckReply();
    out.timestamp = be32(data + 0x4C);
    out.status = data[0x50];
    out.flag = data[0x51];
    if (be32(data) == kReplyWithUpdate) {
        const std::uint32_t second = be32(data + 0x56);
        if (second > kMaxSignature || 0x5A + second > total) {
            error = "the update server's reply has a bad second signature length";
            return false;
        }
        out.update_bytes = be32(data + 0x52);
        if (out.update_bytes > kCtgpCheckMaxUpdateBytes) {
            error = "the update server offers an update of " + std::to_string(out.update_bytes) +
                    " bytes, more than RiftWii takes";
            return false;
        }
        out.update_waiting = out.update_bytes != 0;
    }
    return true;
}

std::array<std::uint8_t, 8> ctgp_download_request(std::uint32_t offset, std::uint32_t size) {
    std::vector<std::uint8_t> bytes;
    put32(bytes, offset);
    put32(bytes, size);
    std::array<std::uint8_t, 8> out{};
    std::copy(bytes.begin(), bytes.end(), out.begin());
    return out;
}

namespace {

bool receive_exactly(const CtgpCheckIo& io, std::uint8_t* out, std::size_t length, std::string& error) {
    while (length > 0) {
        std::size_t got = 0;
        if (!io.receive_some(out, length, got)) {
            error = "the update server stopped answering";
            return false;
        }
        if (got == 0) {
            error = "the update server closed the connection early";
            return false;
        }
        out += got;
        length -= got;
    }
    return true;
}

}  // namespace

bool ctgp_check_exchange(const CtgpCheckIo& io, const std::vector<std::uint8_t>& request, CtgpCheckReply& out,
                         std::string& error) {
    if (!io.send(request.data(), request.size())) {
        error = "the update check could not be sent";
        return false;
    }
    std::vector<std::uint8_t> reply(kCtgpCheckReplyLengthBytes);
    if (!receive_exactly(io, reply.data(), reply.size(), error)) return false;
    std::size_t total = 0;
    if (!ctgp_check_reply_size(reply.data(), total, error)) return false;
    reply.resize(total);
    if (!receive_exactly(io, reply.data() + kCtgpCheckReplyLengthBytes, total - kCtgpCheckReplyLengthBytes, error))
        return false;
    return ctgp_check_parse_reply(reply.data(), reply.size(), out, error);
}

bool ctgp_download(const CtgpCheckIo& io, std::uint32_t size, const CtgpDownloadSink& sink,
                   const CtgpDownloadProgress& progress, std::string& error, std::size_t piece) {
    if (size == 0 || size > kCtgpCheckMaxUpdateBytes || piece == 0) {
        error = "the update's size is out of range";
        return false;
    }
    const auto ask = ctgp_download_request(0, size);
    if (!io.send(ask.data(), ask.size())) {
        error = "the download request could not be sent";
        return false;
    }
    std::vector<std::uint8_t> buffer(std::min<std::size_t>(piece, size));
    std::uint64_t done = 0;
    while (done < size) {
        std::size_t got = 0;
        const std::size_t want = std::min<std::uint64_t>(buffer.size(), size - done);
        if (!io.receive_some(buffer.data(), want, got)) {
            error = "the download stopped at " + std::to_string(done) + " of " + std::to_string(size) + " bytes";
            return false;
        }
        if (got == 0) {
            error = "the server closed the download at " + std::to_string(done) + " of " + std::to_string(size) +
                    " bytes";
            return false;
        }
        if (!sink(buffer.data(), got)) {
            error = "the update could not be saved";
            return false;
        }
        done += got;
        if (progress && !progress(done, size)) {
            error = "the download was cancelled";
            return false;
        }
    }
    // Ends the session the way the channel does; the server has sent all
    // it will, so a failed goodbye does not undo the download.
    const auto bye = ctgp_download_request(0, 0);
    io.send(bye.data(), bye.size());
    return true;
}

}  // namespace riftwii
