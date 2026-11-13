// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgpupdate.hpp"

#include "riftwii/ctgpdsa.hpp"
#include "riftwii/ctgpyaz0.hpp"
#include "riftwii/sha1.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>

namespace riftwii {
namespace {

constexpr std::uint32_t kCtgpDifVersion = 0x00020001u;
constexpr std::uint32_t kCtgpDiaHeaderBytes = 0x64;
constexpr std::size_t kCtgpYaz0HeaderSize = kCtgpYaz0HeaderBytes;
constexpr std::size_t kCtgpDifMaxHeader = 0x1000;

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}

bool need(std::size_t size, std::size_t offset, std::size_t length) {
    return offset <= size && length <= size - offset;
}

// The eight-byte names are NUL padded; anything after the first NUL is not
// part of the name.
std::string eight_byte_name(const std::uint8_t* p) {
    std::size_t length = 0;
    while (length < 8 && p[length] != 0) {
        ++length;
    }
    return std::string(reinterpret_cast<const char*>(p), length);
}

}  // namespace

bool ctgp_yaz0_decode_nintendo(const std::uint8_t* data, std::size_t size,
                               std::vector<std::uint8_t>& out, std::string& error) {
    if (!need(size, 0, kCtgpYaz0HeaderSize) || std::memcmp(data, "Yaz0", 4) != 0) {
        error = "not a Yaz0 file";
        return false;
    }
    const std::uint32_t unpacked = be32(data + 4);
    if (unpacked > kCtgpYaz0MaxBytes) {
        error = "Yaz0 file claims to be too large";
        return false;
    }
    if (be32(data + 8) != 0 || be32(data + 12) != 0) {
        error = "Yaz0 file has an unknown format";
        return false;
    }

    out.clear();
    out.reserve(unpacked);
    std::array<std::uint8_t, kCtgpYaz0WindowBytes> window{};
    std::size_t position = 0;
    std::size_t offset = kCtgpYaz0HeaderSize;
    while (out.size() < unpacked) {
        if (offset >= size) {
            error = "Yaz0 file ends before its data is complete";
            return false;
        }
        const std::uint8_t flags = data[offset++];
        for (int bit = 7; bit >= 0 && out.size() < unpacked; --bit) {
            if ((flags >> bit) & 1) {
                if (offset >= size) {
                    error = "Yaz0 file ends in a literal";
                    return false;
                }
                const std::uint8_t value = data[offset++];
                window[position & (kCtgpYaz0WindowBytes - 1)] = value;
                out.push_back(value);
                ++position;
                continue;
            }
            if (offset + 1 >= size) {
                error = "Yaz0 file ends in a match";
                return false;
            }
            const std::uint8_t first = data[offset];
            const std::uint8_t second = data[offset + 1];
            offset += 2;
            // A zero high nibble means a long match: a third byte holds the
            // length minus 0x12 (18 to 273).
            std::size_t length = (first >> 4) + 2;
            if ((first >> 4) == 0) {
                if (offset >= size) {
                    error = "Yaz0 file ends in a match";
                    return false;
                }
                length = std::size_t(data[offset++]) + 0x12;
            }
            const std::size_t distance = (((first & 0x0F) << 8) | second) + 1;
            if (distance > position) {
                error = "Yaz0 file has a match before its start";
                return false;
            }
            const std::size_t store = std::min(length, std::size_t(unpacked - out.size()));
            std::size_t source = position - distance;
            for (std::size_t i = 0; i < store; ++i) {
                const std::uint8_t value = window[source & (kCtgpYaz0WindowBytes - 1)];
                ++source;
                window[position & (kCtgpYaz0WindowBytes - 1)] = value;
                out.push_back(value);
                ++position;
            }
        }
    }
    return true;
}

CtgpYaz0StreamDecoder::CtgpYaz0StreamDecoder(Sink sink) : sink_(std::move(sink)) {
    out_.reserve(32 * 1024);
}

bool CtgpYaz0StreamDecoder::emit(std::uint8_t value) {
    window_[produced_ & (kCtgpYaz0WindowBytes - 1)] = value;
    ++produced_;
    out_.push_back(value);
    return out_.size() < 32 * 1024 || flush();
}

bool CtgpYaz0StreamDecoder::flush() {
    if (out_.empty()) {
        return true;
    }
    const bool ok = sink_(out_.data(), out_.size());
    out_.clear();
    return ok;
}

bool CtgpYaz0StreamDecoder::feed(const std::uint8_t* data, std::size_t length, std::string& error) {
    if (failed_) {
        error = "Yaz0 stream already failed";
        return false;
    }
    if (header_ && produced_ == unpacked_) {
        return true;  // padding after the file
    }
    in_.insert(in_.end(), data, data + length);
    if (!run(false, error)) {
        failed_ = true;
        return false;
    }
    return true;
}

bool CtgpYaz0StreamDecoder::finish(std::string& error) {
    if (failed_) {
        error = "Yaz0 stream already failed";
        return false;
    }
    if (!run(true, error) || !flush()) {
        if (error.empty()) {
            error = "the unpacked file could not be kept";
        }
        failed_ = true;
        return false;
    }
    return true;
}

bool CtgpYaz0StreamDecoder::run(bool last, std::string& error) {
    std::size_t at = 0;
    if (!header_) {
        if (in_.size() < kCtgpYaz0HeaderSize) {
            if (last) {
                error = "not a Yaz0 file";
                return false;
            }
            return true;
        }
        if (std::memcmp(in_.data(), "Yaz0", 4) != 0) {
            error = "not a Yaz0 file";
            return false;
        }
        unpacked_ = be32(in_.data() + 4);
        if (unpacked_ > kCtgpYaz0MaxBytes) {
            error = "Yaz0 file claims to be too large";
            return false;
        }
        if (be32(in_.data() + 8) != 0 || be32(in_.data() + 12) != 0) {
            error = "Yaz0 file has an unknown format";
            return false;
        }
        header_ = true;
        at = kCtgpYaz0HeaderSize;
    }
    // A flag byte and eight items of up to three bytes each.
    constexpr std::size_t kGroupMax = 1 + 8 * 3;
    const std::size_t size = in_.size();
    const std::uint8_t* data = in_.data();
    while (produced_ < unpacked_) {
        if (!last && size - at < kGroupMax) {
            break;
        }
        if (at >= size) {
            error = "Yaz0 file ends before its data is complete";
            return false;
        }
        std::size_t offset = at;
        const std::uint8_t flags = data[offset++];
        for (int bit = 7; bit >= 0 && produced_ < unpacked_; --bit) {
            if ((flags >> bit) & 1) {
                if (offset >= size) {
                    error = "Yaz0 file ends in a literal";
                    return false;
                }
                if (!emit(data[offset++])) {
                    error = "the unpacked file could not be kept";
                    return false;
                }
                continue;
            }
            if (offset + 1 >= size) {
                error = "Yaz0 file ends in a match";
                return false;
            }
            const std::uint8_t first = data[offset];
            const std::uint8_t second = data[offset + 1];
            offset += 2;
            std::size_t length = (first >> 4) + 2;
            if ((first >> 4) == 0) {
                if (offset >= size) {
                    error = "Yaz0 file ends in a match";
                    return false;
                }
                length = std::size_t(data[offset++]) + 0x12;
            }
            const std::size_t distance = (((first & 0x0F) << 8) | second) + 1;
            if (distance > produced_) {
                error = "Yaz0 file has a match before its start";
                return false;
            }
            const std::size_t store = std::size_t(std::min<std::uint64_t>(length, unpacked_ - produced_));
            std::uint64_t source = produced_ - distance;
            for (std::size_t i = 0; i < store; ++i) {
                if (!emit(window_[source++ & (kCtgpYaz0WindowBytes - 1)])) {
                    error = "the unpacked file could not be kept";
                    return false;
                }
            }
        }
        at = offset;
    }
    if (produced_ == unpacked_) {
        in_.clear();
        in_.shrink_to_fit();
    } else {
        in_.erase(in_.begin(), in_.begin() + std::ptrdiff_t(at));
    }
    if (last && produced_ != unpacked_) {
        error = "Yaz0 file ends before its data is complete";
        return false;
    }
    return true;
}

CtgpPieces::~CtgpPieces() { clear(); }

bool CtgpPieces::use_file(const std::string& path, std::string& error) {
    clear();
    file_ = std::fopen(path.c_str(), "w+b");
    if (file_ == nullptr) {
        error = "the scratch file " + path + " could not be created";
        return false;
    }
    path_ = path;
    return true;
}

bool CtgpPieces::append(const std::uint8_t* data, std::size_t length) {
    if (file_ != nullptr) {
        if (length == 0) return true;
        if (std::fseek(file_, 0, SEEK_END) != 0 || std::fwrite(data, 1, length, file_) != length) return false;
        size_ += length;
        return true;
    }
    while (length > 0) {
        const std::size_t used = size_ % kPieceBytes;
        if (used == 0 && size_ / kPieceBytes == pieces_.size()) {
            pieces_.emplace_back(new std::uint8_t[kPieceBytes]);
        }
        const std::size_t take = std::min(length, kPieceBytes - used);
        std::memcpy(pieces_[size_ / kPieceBytes].get() + used, data, take);
        size_ += take;
        data += take;
        length -= take;
    }
    return true;
}

bool CtgpPieces::read(std::size_t offset, std::uint8_t* out, std::size_t length) const {
    if (offset > size_ || length > size_ - offset) {
        return false;
    }
    if (file_ != nullptr) {
        return length == 0 || (std::fseek(file_, static_cast<long>(offset), SEEK_SET) == 0 &&
                                std::fread(out, 1, length, file_) == length);
    }
    while (length > 0) {
        const std::size_t within = offset % kPieceBytes;
        const std::size_t take = std::min(length, kPieceBytes - within);
        std::memcpy(out, pieces_[offset / kPieceBytes].get() + within, take);
        out += take;
        offset += take;
        length -= take;
    }
    return true;
}

void CtgpPieces::clear() {
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
        std::remove(path_.c_str());
        path_.clear();
    }
    pieces_.clear();
    pieces_.shrink_to_fit();
    size_ = 0;
}

bool ctgp_dia_parse(const std::uint8_t* data, std::size_t size, CtgpDia& out, std::string& error) {
    return ctgp_dia_parse_prefix(data, size, size, out, error);
}

bool ctgp_dia_parse_prefix(const std::uint8_t* data, std::size_t size, std::uint64_t total,
                           CtgpDia& out, std::string& error) {
    out = CtgpDia();
    if (!need(size, 0, kCtgpDiaHeaderBytes) || std::memcmp(data, "DIA0", 4) != 0) {
        error = "not a DIA0 update";
        return false;
    }
    if (total > 0xFFFFFFFFull || be32(data + 4) != total) {
        error = "DIA0 size field does not match the file";
        return false;
    }
    if (be32(data + 8) != 0 || be32(data + 12) != 0) {
        error = "DIA0 has an unknown format";
        return false;
    }
    const std::uint32_t signature_length = be32(data + 0x20);
    if (signature_length > 0x30 || !need(size, 0x24, signature_length)) {
        error = "DIA0 signature is malformed";
        return false;
    }
    const std::uint32_t base_count = be32(data + 0x54);
    const std::uint32_t base_offset = be32(data + 0x58);
    const std::uint32_t section_count = be32(data + 0x5C);
    const std::uint32_t section_offset = be32(data + 0x60);
    if (base_count == 0 || base_count > kCtgpDiaMaxBaseRecords) {
        error = "DIA0 has an unexpected number of base records";
        return false;
    }
    if (section_count == 0 || section_count > kCtgpDiaMaxSections) {
        error = "DIA0 has an unexpected number of sections";
        return false;
    }
    const std::uint64_t base_end = std::uint64_t(base_offset) + std::uint64_t(base_count) * 16;
    const std::uint64_t table_end = std::uint64_t(section_offset) + std::uint64_t(section_count) * 8;
    if (base_offset < kCtgpDiaHeaderBytes || base_end > size || section_offset < kCtgpDiaHeaderBytes ||
        table_end > size) {
        error = "DIA0 tables are outside the file";
        return false;
    }

    out.total_size = std::uint32_t(total);
    out.unpacked_size = be32(data + 0x1C);
    out.component = eight_byte_name(data + 0x10);
    out.version = be32(data + 0x18);
    out.signature.assign(data + 0x24, data + 0x24 + signature_length);
    out.bases.reserve(base_count);
    for (std::uint32_t i = 0; i < base_count; ++i) {
        const std::uint8_t* record = data + base_offset + i * 16;
        CtgpDiaBase base;
        base.component = eight_byte_name(record);
        base.version = be32(record + 8);
        out.bases.push_back(base);
    }
    out.sections.reserve(section_count);
    // The sections fill the rest of the file, in order and with no gap,
    // starting after the tables and ending at its last byte. A table that
    // lists fewer (or other) sections than the file holds leaves bytes over
    // and is refused, so no file of the update can be skipped.
    std::uint64_t next = std::max(base_end, table_end);
    for (std::uint32_t i = 0; i < section_count; ++i) {
        const std::uint8_t* entry = data + section_offset + i * 8;
        CtgpDiaSection section;
        section.offset = be32(entry);
        section.size = be32(entry + 4);
        if (section.size < 0x20 || std::uint64_t(section.offset) + section.size > total) {
            error = "DIA0 section is outside the file";
            out = CtgpDia();
            return false;
        }
        if (section.offset != next) {
            error = "DIA0 sections do not follow one another";
            out = CtgpDia();
            return false;
        }
        next = std::uint64_t(section.offset) + section.size;
        out.sections.push_back(section);
    }
    if (next != total) {
        error = "DIA0 sections do not fill the file";
        out = CtgpDia();
        return false;
    }
    return true;
}

bool ctgp_dif_parse(const std::uint8_t* data, std::size_t size, CtgpDif& out, std::string& error) {
    out = CtgpDif();
    if (!need(size, 0, 0x20) || std::memcmp(data, "DIF0", 4) != 0) {
        error = "not a DIF0 section";
        return false;
    }
    const std::uint32_t declared = be32(data + 4);
    if (declared != size) {
        error = "DIF0 size field does not match the section";
        return false;
    }
    if (be32(data + 8) != 0) {
        error = "DIF0 has an unknown format";
        return false;
    }
    const std::uint32_t path_table = be32(data + 0x0C);
    const std::uint32_t header_size = be32(data + 0x10);
    if (be32(data + 0x14) != kCtgpDifVersion) {
        error = "DIF0 has an unknown version";
        return false;
    }
    const std::uint32_t unpacked = be32(data + 0x18);
    if (unpacked > kCtgpUpdateMaxFileBytes) {
        error = "DIF0 output is too large";
        return false;
    }
    const std::uint32_t count = be32(data + 0x1C);
    if (count > kCtgpDifMaxEntries) {
        error = "DIF0 has too many entries";
        return false;
    }
    const std::uint64_t entries_end = 0x20ull + std::uint64_t(count) * 16;
    if (header_size < entries_end || header_size > kCtgpDifMaxHeader || header_size > path_table ||
        path_table > size) {
        error = "DIF0 header is malformed";
        return false;
    }
    out.size = declared;
    out.header_size = header_size;
    out.path_table = path_table;
    out.unpacked_size = unpacked;
    out.entries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint8_t* entry = data + 0x20 + i * 16;
        CtgpDifEntry parsed;
        parsed.component = eight_byte_name(entry);
        parsed.path_offset = be32(entry + 8);
        if (parsed.path_offset >= size) {
            error = "DIF0 entry path is outside the section";
            out = CtgpDif();
            return false;
        }
        out.entries.push_back(parsed);
    }
    return true;
}

std::string ctgp_dif_path(const std::uint8_t* data, std::size_t size, std::uint32_t offset) {
    if (offset >= size) {
        return std::string();
    }
    const char* text = reinterpret_cast<const char*>(data + offset);
    const std::size_t limit = size - offset;
    const void* end = std::memchr(text, 0, limit);
    if (end == nullptr) {
        return std::string();
    }
    return std::string(text, static_cast<const char*>(end) - text);
}

namespace {

// The sink's staging buffer: the rebuild writes a byte at a time, and the
// caller only sees whole chunks.
class ApplyOut {
public:
    ApplyOut(CtgpUpdateWrite write, void* user) : write_(write), user_(user) {}
    ~ApplyOut() = default;

    bool put(std::uint8_t value) {
        buffer_.push_back(value);
        return buffer_.size() < buffer_.capacity() || flush();
    }
    bool flush() {
        if (buffer_.empty()) {
            return true;
        }
        const bool ok = write_(user_, buffer_.data(), buffer_.size());
        buffer_.clear();
        return ok;
    }

private:
    CtgpUpdateWrite write_;
    void* user_;
    std::vector<std::uint8_t> buffer_;

public:
    void reserve(std::size_t bytes) { buffer_.reserve(bytes); }
};

}  // namespace

bool ctgp_dif_apply(const std::uint8_t* data, std::size_t size, const CtgpDif& section,
                    const std::vector<const CtgpBaseSource*>& bases,
                    CtgpUpdateWrite write, void* user, std::string& error) {
    return ctgp_dif_apply(data, size, section, bases, write, user, nullptr, error);
}

bool ctgp_dif_apply(const std::uint8_t* data, std::size_t size, const CtgpDif& section,
                    const std::vector<const CtgpBaseSource*>& bases,
                    CtgpUpdateWrite write, void* user, bool* base_short, std::string& error) {
    if (base_short != nullptr) *base_short = false;
    if (data == nullptr || write == nullptr) {
        error = "empty update section";
        return false;
    }
    if (section.size != size || section.header_size > size || section.path_table > size) {
        error = "update section does not match its header";
        return false;
    }
    if (bases.size() != section.entries.size()) {
        error = "update section is missing its base files";
        return false;
    }
    for (const CtgpBaseSource* base : bases) {
        if (base == nullptr) {
            error = "update section has an empty base file";
            return false;
        }
    }

    std::vector<std::uint8_t> window(kCtgpUpdateWindowBytes, 0);
    std::size_t position = 0;
    std::size_t distance = 0;
    std::vector<std::int64_t> base_position(bases.size() + 1, 0);
    std::vector<std::uint8_t> base_chunk(64 * 1024);
    std::size_t offset = section.header_size;
    const std::size_t end = section.path_table;
    ApplyOut out(write, user);
    out.reserve(64 * 1024);
    const std::uint32_t target = section.unpacked_size;

    auto put_byte = [&](std::uint8_t value) {
        window[position & (window.size() - 1)] = value;
        ++position;
        return out.put(value);
    };

    bool done = false;
    while (offset < end && !done && position < target) {
        if (position > target) {
            error = "update section writes past its size";
            return false;
        }
        const std::uint8_t control = data[offset++];
        const std::uint8_t opcode = control & 0x0F;
        const std::uint8_t high = control >> 4;

        auto read_u8 = [&](std::uint8_t& value) {
            if (offset >= end) {
                return false;
            }
            value = data[offset++];
            return true;
        };
        auto read_u32 = [&](std::uint32_t& value) {
            if (offset + 4 > end) {
                return false;
            }
            value = be32(data + offset);
            offset += 4;
            return true;
        };
        auto literal = [&](std::uint32_t count) {
            if (count > target - position || count > end - offset) {
                return false;
            }
            for (std::uint32_t i = 0; i < count; ++i) {
                if (!put_byte(data[offset + i])) {
                    return false;
                }
            }
            offset += count;
            distance = 0;
            return true;
        };
        // The copy reads from distance + 1 bytes back. The distance holds
        // for one copy only: after any literal or window copy it is 0
        // again, so a copy with no distance before it repeats the previous
        // byte.
        auto window_copy = [&](std::uint32_t count) {
            const std::size_t back = distance + 1;
            // The ring holds the last window.size() bytes, and each byte is
            // read before the one written in its place, so a copy may reach
            // exactly window.size() bytes back.
            if (count > target - position || back > position || back > window.size()) {
                return false;
            }
            std::size_t source = position - back;
            for (std::uint32_t i = 0; i < count; ++i) {
                const std::uint8_t value = window[source & (window.size() - 1)];
                ++source;
                if (!put_byte(value)) {
                    return false;
                }
            }
            distance = 0;
            return true;
        };
        auto base_copy = [&](std::uint8_t index, std::uint32_t count) {
            if (index == 0) {
                return window_copy(count);
            }
            if (index > bases.size() || count > target - position) {
                return false;
            }
            const CtgpBaseSource& base = *bases[index - 1];
            if (base_position[index] < 0 || std::uint64_t(base_position[index]) + count > base.size()) {
                if (base_position[index] >= 0 && base_short != nullptr) *base_short = true;
                return false;
            }
            std::uint32_t left = count;
            std::uint64_t at = std::uint64_t(base_position[index]);
            while (left > 0) {
                const std::size_t take = std::min<std::size_t>(left, base_chunk.size());
                if (!base.read(std::uint32_t(at), base_chunk.data(), take)) {
                    return false;
                }
                for (std::size_t i = 0; i < take; ++i) {
                    if (!put_byte(base_chunk[i])) {
                        return false;
                    }
                }
                at += take;
                left -= std::uint32_t(take);
            }
            base_position[index] = std::int64_t(at);
            return true;
        };

        bool ok = true;
        switch (opcode) {
        case 0x0:
            ok = literal(high + 1ull);
            break;
        case 0x1: {
            std::uint32_t count = 0;
            if (control == 0xF1) {
                done = true;
                break;
            }
            if (control == 0x01) {
                std::uint8_t value = 0;
                ok = read_u8(value) && (count = 0x11u + value, true);
            } else if (control == 0x11) {
                std::uint8_t a = 0, b = 0;
                ok = read_u8(a) && read_u8(b) && (count = 0x111u + (std::uint32_t(a) << 8) + b, true);
            } else if (control == 0x21) {
                std::uint8_t a = 0, b = 0, c = 0;
                ok = read_u8(a) && read_u8(b) && read_u8(c) &&
                     (count = 0x10111u + (std::uint32_t(a) << 16) + (std::uint32_t(b) << 8) + c, true);
            } else if (control == 0x31) {
                std::uint8_t a = 0, b = 0, c = 0, d = 0;
                ok = read_u8(a) && read_u8(b) && read_u8(c) && read_u8(d) &&
                     (count = 0x1010111u + (std::uint32_t(a) << 24) + (std::uint32_t(b) << 16) +
                                      (std::uint32_t(c) << 8) + d,
                      true);
            } else {
                ok = false;
            }
            if (ok && !done) {
                ok = literal(count);
            }
            break;
        }
        case 0x3:
            ok = window_copy(high + 2u);
            break;
        case 0x4: {
            std::uint8_t value = 0;
            ok = read_u8(value) && base_copy(high, 6u + value);
            break;
        }
        case 0x5: {
            std::uint8_t a = 0, b = 0;
            ok = read_u8(a) && read_u8(b) && base_copy(high, 0x106u + (std::uint32_t(a) << 8) + b);
            break;
        }
        case 0x6: {
            std::uint8_t a = 0, b = 0, c = 0;
            ok = read_u8(a) && read_u8(b) && read_u8(c) &&
                 base_copy(high, 0x10106u + (std::uint32_t(a) << 16) + (std::uint32_t(b) << 8) + c);
            break;
        }
        case 0x7: {
            std::uint8_t a = 0, b = 0, c = 0, d = 0;
            ok = read_u8(a) && read_u8(b) && read_u8(c) && read_u8(d);
            if (ok) {
                const std::uint64_t count = 0x1010106ull + (std::uint64_t(a) << 24) +
                                            (std::uint64_t(b) << 16) + (std::uint64_t(c) << 8) + d;
                ok = count <= target - position && base_copy(high, std::uint32_t(count));
            }
            break;
        }
        case 0x8:
            distance = high + 1u;
            break;
        case 0x9: {
            std::uint8_t value = 0;
            ok = read_u8(value);
            if (ok) {
                if (high == 0) {
                    distance = value + 1u;
                } else if (high <= bases.size()) {
                    base_position[high] -= std::int64_t(value) + 1;
                } else {
                    ok = false;
                }
            }
            break;
        }
        case 0xA: {
            std::uint8_t a = 0, b = 0;
            ok = read_u8(a) && read_u8(b);
            if (ok) {
                const std::uint32_t amount = (std::uint32_t(a) << 8) + b + 0x101u;
                if (high == 0) {
                    distance = amount;
                } else if (high <= bases.size()) {
                    base_position[high] -= std::int64_t(amount);
                } else {
                    ok = false;
                }
            }
            break;
        }
        case 0xB: {
            std::uint32_t value = 0;
            ok = read_u32(value);
            if (ok) {
                if (high == 0 || high > bases.size()) {
                    ok = false;
                } else {
                    base_position[high] = std::int64_t(value);
                }
            }
            break;
        }
        case 0xD: {
            std::uint8_t value = 0;
            ok = read_u8(value);
            if (ok) {
                if (high == 0 || high > bases.size()) {
                    ok = false;
                } else {
                    base_position[high] += std::int64_t(value) + 1;
                }
            }
            break;
        }
        case 0xE: {
            std::uint8_t a = 0, b = 0;
            ok = read_u8(a) && read_u8(b);
            if (ok) {
                const std::uint32_t amount = (std::uint32_t(a) << 8) + b + 0x101u;
                if (high == 0 || high > bases.size()) {
                    ok = false;
                } else {
                    base_position[high] += std::int64_t(amount);
                }
            }
            break;
        }
        case 0xF: {
            std::uint8_t a = 0, b = 0, c = 0;
            ok = read_u8(a) && read_u8(b) && read_u8(c);
            if (ok) {
                if (high == 0 || high > bases.size()) {
                    ok = false;
                } else {
                    base_position[high] = std::int64_t((std::uint32_t(a) << 16) | (std::uint32_t(b) << 8) | c);
                }
            }
            break;
        }
        case 0x2:
        case 0xC:
            break;
        default:
            ok = false;
            break;
        }
        if (!ok) {
            error = "update section has a bad command";
            return false;
        }
        for (std::size_t i = 1; i < base_position.size(); ++i) {
            if (base_position[i] < 0 || std::uint64_t(base_position[i]) > bases[i - 1]->size()) {
                if (base_position[i] >= 0 && base_short != nullptr) *base_short = true;
                error = "update section seeks outside a base file";
                return false;
            }
        }
    }

    if (!out.flush()) {
        error = "could not write the rebuilt file";
        return false;
    }
    if (position != target) {
        error = "update section stopped before the file was complete";
        return false;
    }
    return true;
}


namespace {

using CtgpSectionLoader = std::function<bool(const CtgpDiaSection&, std::vector<std::uint8_t>&, std::string&)>;
// Reads `length` bytes at `offset` of the update, for a look at a section's
// header before the section itself is loaded.
using CtgpUpdatePeek = std::function<bool(std::uint64_t offset, std::uint8_t* out, std::size_t length)>;

// How long a file takes, for the bar: on Jack's Wii (2611-186) the five files'
// times fit 18.3 s per MB of the update's own section plus 5.3 s per MB of
// the file it rebuilds (within 8% for four of them, 29% for the fifth; the
// unpacked size alone was off by up to 25%). So a file weighs its section's
// size 3.45 times plus its unpacked size.
constexpr double kSectionWeight = 3.45;

// The rebuilt bytes go straight into the packer; the packed bytes into
// pieces and the checksum. Nothing the size of the file is held unpacked.
struct RebuildSink {
    CtgpYaz0ExactWriter* writer = nullptr;
    std::string why;
    std::uint64_t written = 0;
    std::uint64_t total = 0;
    std::uint64_t next_tick = 0;
    std::function<void(double)> tick;
};

bool rebuild_write(void* user, const std::uint8_t* data, std::size_t length) {
    RebuildSink& sink = *static_cast<RebuildSink*>(user);
    if (!sink.writer->push(data, length, sink.why)) {
        return false;
    }
    sink.written += length;
    if (sink.tick && sink.written >= sink.next_tick) {
        sink.next_tick = sink.written + 128 * 1024;
        sink.tick(sink.total ? double(sink.written) / double(sink.total) : 1.0);
    }
    return true;
}

// Shares of one file's bar: unpacking its bases, then rebuilding and packing
// it, then checking and storing it.
// In Dolphin (pr-8) unpacking a file's bases took about half its time and
// storing almost none; on a Wii storing is slower (each 32 KiB goes to the
// journal and is synced first), so it gets a fifth. The log's stage lines
// (bases unpacked, packed, stored) give the real split.
constexpr double kBaseShare = 0.45;
constexpr double kRebuildShare = 0.35;

// For the callers that don't ask why an apply failed.
CtgpUpdateFailure& ignored_failure() {
    static CtgpUpdateFailure unused = CtgpUpdateFailure::None;
    return unused;
}

// A base file whose reads are watched: a section's commands that fail with
// every base read fine are the update's own fault.
class WatchedBase : public CtgpBaseSource {
public:
    explicit WatchedBase(const CtgpBaseSource& inner) : inner_(inner) {}
    std::uint32_t size() const override { return inner_.size(); }
    bool read(std::uint32_t offset, std::uint8_t* out, std::size_t length) const override {
        const bool ok = inner_.read(offset, out, length);
        if (!ok) failed = true;
        return ok;
    }
    mutable bool failed = false;

private:
    const CtgpBaseSource& inner_;
};

bool apply_sections(const CtgpDia& update, const CtgpSectionLoader& load_section, const CtgpUpdatePeek& peek,
                    CtgpUpdateTarget& target, const std::function<void(const std::string&)>& log,
                    const CtgpUpdateFileDone& file_done, const CtgpUpdateStep& step, CtgpUpdateFailure& failure,
                    std::string& error) {
    // Anything that goes wrong is Temporary unless it is set to BadUpdate
    // where the update itself is at fault.
    failure = CtgpUpdateFailure::Temporary;
    if (log) {
        log("CTGP-R update " + update.component + ": " + std::to_string(update.sections.size()) + " files");
    }
    // One buffer for every section, sized once for the largest, so the heap
    // is never asked for a new few-megabyte block per file.
    std::size_t largest = 0;
    for (const CtgpDiaSection& place : update.sections) {
        largest = std::max<std::size_t>(largest, place.size);
    }
    std::vector<std::uint8_t> section;
    section.reserve(largest);
    CtgpPieces packed;
    // The header's unpacked size is the sum of the sections': a section left
    // out of the table shows here, and the install is then rolled back.
    std::uint64_t unpacked_total = 0;
    const std::size_t count = update.sections.size();
    // Each file's weight in the bar (kSectionWeight), from the table and a
    // look at each section's header (its unpacked size at 0x18). A header
    // that can't be read only makes the bar less even; the section itself is
    // checked when it is loaded.
    std::vector<double> weight(count, 0.0);
    double weight_total = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const CtgpDiaSection& place = update.sections[index];
        std::uint8_t head[4] = {};
        double unpacked = double(update.unpacked_size) / double(count ? count : 1);
        if (peek && place.size >= 0x1c && peek(std::uint64_t(place.offset) + 0x18, head, sizeof head)) {
            const std::uint32_t value = (std::uint32_t(head[0]) << 24) | (std::uint32_t(head[1]) << 16) |
                                        (std::uint32_t(head[2]) << 8) | head[3];
            if (value <= kCtgpUpdateMaxFileBytes) unpacked = double(value);
        }
        weight[index] = kSectionWeight * double(place.size) + unpacked;
        weight_total += weight[index];
    }
    if (weight_total <= 0) weight_total = 1;
    double weight_before = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const CtgpDiaSection& place = update.sections[index];
        if (!load_section(place, section, error)) {
            return false;
        }
        if (section.size() != place.size) {
            error = "an update section could not be read";
            return false;
        }
        const std::uint8_t* data = section.data();
        CtgpDif files;
        if (!ctgp_dif_parse(data, section.size(), files, error)) {
            failure = CtgpUpdateFailure::BadUpdate;
            return false;
        }
        const std::string path = ctgp_dif_path(data, section.size(), files.path_table);
        if (path.empty()) {
            error = "an update section has no file path";
            failure = CtgpUpdateFailure::BadUpdate;
            return false;
        }
        unpacked_total += files.unpacked_size;
        if (unpacked_total > update.unpacked_size) {
            error = "the update's files are larger than its header says";
            failure = CtgpUpdateFailure::BadUpdate;
            return false;
        }
        // This file's share of the whole, by weight.
        const double before = weight_before;
        const double mine = weight[index];
        weight_before += mine;
        const auto report = [&](double part) {
            if (step) {
                const double clamped = std::min(1.0, std::max(0.0, part));
                const double overall = (before + clamped * mine) / weight_total;
                step(index, count, path, clamped, std::min(1.0, std::max(0.0, overall)));
            }
        };
        report(0.0);
        std::vector<std::unique_ptr<CtgpBaseSource>> owned;
        std::vector<std::unique_ptr<WatchedBase>> watched;
        watched.reserve(files.entries.size());
        std::vector<const CtgpBaseSource*> sources;
        owned.reserve(files.entries.size());
        sources.reserve(files.entries.size());
        const std::size_t base_count = files.entries.size();
        for (std::size_t b = 0; b < base_count; ++b) {
            const CtgpDifEntry& entry = files.entries[b];
            const std::string base_path = ctgp_dif_path(data, section.size(), entry.path_offset);
            if (base_path.empty()) {
                error = "an update entry has no path";
                failure = CtgpUpdateFailure::BadUpdate;
                return false;
            }
            owned.emplace_back();
            const CtgpProgress base_progress = [&](std::uint64_t done, std::uint64_t total) {
                const double inner = total ? double(done) / double(total) : 0.0;
                report(kBaseShare * (double(b) + std::min(1.0, inner)) / double(base_count));
            };
            CtgpUpdateFailure base_failure = CtgpUpdateFailure::Temporary;
            if (!target.load_base(entry.component, base_path, owned.back(), base_progress, base_failure, error)) {
                failure = base_failure == CtgpUpdateFailure::BadUpdate ? CtgpUpdateFailure::BadUpdate
                                                                        : CtgpUpdateFailure::Temporary;
                return false;
            }
            watched.emplace_back(new WatchedBase(*owned.back()));
            sources.push_back(watched.back().get());
        }
        report(kBaseShare);
        if (log) {
            std::uint64_t base_bytes = 0;
            for (const auto& base : owned) base_bytes += base->size();
            log(path + ": " + std::to_string(base_count) + " base file(s) unpacked, " + std::to_string(base_bytes) +
                " bytes");
        }

        // Packed exactly as CTGP-R packs it: its hash lists cover the stored
        // bytes.
        packed.clear();
        const std::string scratch = target.packed_scratch_path();
        if (!scratch.empty() && !packed.use_file(scratch, error)) {
            return false;
        }
        Sha1 digest;
        bool kept = true;
        CtgpYaz0ExactWriter writer(files.unpacked_size, [&](const std::uint8_t* bytes, std::size_t length) {
            digest.update(bytes, length);
            kept = packed.append(bytes, length);
            return kept;
        });
        RebuildSink sink;
        sink.writer = &writer;
        sink.total = files.unpacked_size;
        sink.next_tick = 128 * 1024;
        sink.tick = [&](double part) { report(kBaseShare + kRebuildShare * part); };
        bool base_short = false;
        if (!ctgp_dif_apply(data, section.size(), files, sources, rebuild_write, &sink, &base_short, error)) {
            bool base_failed = false;
            for (const auto& base : watched) base_failed = base_failed || base->failed;
            if (!sink.why.empty()) {
                error = path + ": " + sink.why;
            } else if (base_short) {
                // A base file shorter than the commands expect: a damaged
                // file in the pack or on the disc, which can be put right.
                error = path + ": a file it is rebuilt from is shorter than the update expects "
                               "(a damaged file in the pack or on the game disc?)";
            } else if (!base_failed) {
                failure = CtgpUpdateFailure::BadUpdate;  // the commands themselves
            }
            return false;
        }
        if (sink.written != files.unpacked_size) {
            error = path + " was not rebuilt completely";
            failure = CtgpUpdateFailure::BadUpdate;
            return false;
        }
        if (!writer.finish(error)) {
            error = path + ": " + error;
            return false;
        }
        if (!kept) {
            error = path + ": the packed file could not be kept until it is stored";
            return false;
        }
        // The bases are not needed any more: their scratch files go now.
        sources.clear();
        watched.clear();
        owned.clear();
        report(kBaseShare + kRebuildShare);
        if (log) log(path + ": rebuilt and packed, " + std::to_string(packed.size()) + " bytes to store");
        const Sha1Digest packed_digest = digest.finish();
        if (!target.check_file(path, packed_digest, error)) {
            return false;
        }
        const CtgpProgress store_progress = [&](std::uint64_t done, std::uint64_t total) {
            report(kBaseShare + kRebuildShare +
                   (1.0 - kBaseShare - kRebuildShare) * (total ? double(done) / double(total) : 1.0));
        };
        if (!target.store_file(path, packed, store_progress, error)) {
            return false;
        }
        const std::size_t packed_size = packed.size();
        packed.clear();
        if (log) {
            log("stored " + path + " (" + std::to_string(files.unpacked_size) + " bytes, " +
                std::to_string(packed_size) + " packed)");
        }
        report(1.0);
        if (file_done) {
            file_done(index, count, path);
        }
    }
    if (unpacked_total != update.unpacked_size) {
        error = "the update's files do not add up to the size its header says";
        failure = CtgpUpdateFailure::BadUpdate;
        return false;
    }
    failure = CtgpUpdateFailure::None;
    return true;
}

}  // namespace

bool ctgp_update_apply(const std::uint8_t* dia, std::size_t size, CtgpUpdateTarget& target,
                       const std::function<void(const std::string&)>& log, std::string& error) {
    return ctgp_update_apply(dia, size, target, log, CtgpUpdateFileDone(), error);
}

bool ctgp_update_apply(const std::uint8_t* dia, std::size_t size, CtgpUpdateTarget& target,
                       const std::function<void(const std::string&)>& log,
                       const CtgpUpdateFileDone& file_done, std::string& error) {
    CtgpDia update;
    if (!ctgp_dia_parse(dia, size, update, error) || !ctgp_dia_supported(update, error)) {
        return false;
    }
    return apply_sections(
        update,
        [dia](const CtgpDiaSection& place, std::vector<std::uint8_t>& out, std::string&) {
            out.assign(dia + place.offset, dia + place.offset + place.size);
            return true;
        },
        [dia, size](std::uint64_t offset, std::uint8_t* out, std::size_t length) {
            if (offset + length > size) return false;
            std::memcpy(out, dia + offset, length);
            return true;
        },
        target, log, file_done, CtgpUpdateStep(), ignored_failure(), error);
}

bool ctgp_update_apply(const CtgpUpdateRead& read, std::uint64_t size, CtgpUpdateTarget& target,
                       const std::function<void(const std::string&)>& log,
                       const CtgpUpdateFileDone& file_done, std::string& error) {
    return ctgp_update_apply(read, size, target, log, file_done, CtgpUpdateStep(), error);
}

bool ctgp_update_apply(const CtgpUpdateRead& read, std::uint64_t size, CtgpUpdateTarget& target,
                       const std::function<void(const std::string&)>& log,
                       const CtgpUpdateFileDone& file_done, const CtgpUpdateStep& step,
                       std::string& error) {
    CtgpUpdateFailure failure = CtgpUpdateFailure::None;
    return ctgp_update_apply(read, size, target, log, file_done, step, failure, error);
}

bool ctgp_update_apply(const CtgpUpdateRead& read, std::uint64_t size, CtgpUpdateTarget& target,
                       const std::function<void(const std::string&)>& log,
                       const CtgpUpdateFileDone& file_done, const CtgpUpdateStep& step,
                       CtgpUpdateFailure& failure, std::string& error) {
    failure = CtgpUpdateFailure::BadUpdate;
    if (!read || size < 0x64) {
        error = "the update file is too small";
        return false;
    }
    const std::size_t prefix_size = size < kCtgpDiaPrefixBytes ? std::size_t(size) : kCtgpDiaPrefixBytes;
    std::vector<std::uint8_t> prefix(prefix_size);
    if (!read(0, prefix.data(), prefix.size())) {
        error = "the update file could not be read";
        failure = CtgpUpdateFailure::Temporary;
        return false;
    }
    CtgpDia update;
    if (!ctgp_dia_parse_prefix(prefix.data(), prefix.size(), size, update, error) ||
        !ctgp_dia_supported(update, error)) {
        return false;  // BadUpdate
    }
    return apply_sections(
        update,
        [&read](const CtgpDiaSection& place, std::vector<std::uint8_t>& out, std::string&) {
            out.resize(place.size);
            return read(place.offset, out.data(), out.size());
        },
        read,
        target, log, file_done, step, failure, error);
}

bool ctgp_dia_supported(const CtgpDia& dia, std::string& error) {
    if (dia.component == "CTGPR") return true;
    error = "the update is for a package RiftWii doesn't install (" + dia.component + "), only CTGPR";
    return false;
}

bool ctgp_dia_verify_signature(const CtgpUpdateRead& read, std::uint64_t size, const CtgpDsaKey& key,
                               const std::function<void(std::uint64_t done, std::uint64_t total)>& progress,
                               std::string& error) {
    constexpr std::size_t kSignedHeader = 0x68;
    if (!read || size < kSignedHeader) {
        error = "the update is too small to carry a signature";
        return false;
    }
    std::vector<std::uint8_t> chunk(64 * 1024);
    if (!read(0, chunk.data(), kSignedHeader)) {
        error = "the update file could not be read";
        return false;
    }
    if (std::memcmp(chunk.data(), "DIA0", 4) != 0) {
        error = "not a DIA0 update";
        return false;
    }
    const std::uint32_t length = be32(chunk.data() + 0x20);
    if (length == 0 || length > 0x30) {
        error = "the update is not signed";
        return false;
    }
    std::uint8_t signature[0x30];
    std::memcpy(signature, chunk.data() + 0x24, length);
    // What the channel puts in place of the signature before it hashes.
    static const std::uint8_t kFill[0x30] = {
        0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
        0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x0F, 0x1E, 0x2D, 0x3C, 0x4B, 0x5A, 0x69, 0x78,
        0xDE, 0xAD, 0xBE, 0xEF, 0x0B, 0xAD, 0xC0, 0xDE, 0xDE, 0xFA, 0xCE, 0xD0, 0xDA, 0xD0, 0x00, 0x00};
    std::memset(chunk.data() + 0x20, 0, 4);
    std::memcpy(chunk.data() + 0x24, kFill, sizeof kFill);
    Sha1 digest;
    digest.update(chunk.data(), kSignedHeader);
    std::uint64_t done = kSignedHeader;
    while (done < size) {
        const std::size_t take = std::size_t(std::min<std::uint64_t>(chunk.size(), size - done));
        if (!read(done, chunk.data(), take)) {
            error = "the update file could not be read";
            return false;
        }
        digest.update(chunk.data(), take);
        done += take;
        if (progress) progress(done, size);
    }
    const Sha1Digest value = digest.finish();
    if (!ctgp_dsa_verify(key, value.data(), value.size(), signature, length)) {
        error = "the update is not signed by CTGP-R, or was changed after it was signed";
        return false;
    }
    return true;
}

}  // namespace riftwii