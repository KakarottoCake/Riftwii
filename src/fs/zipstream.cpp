// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/zipstream.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>
#include <zlib.h>

#include "riftwii/checkedalloc.hpp"

namespace riftwii {
namespace {

constexpr std::size_t kInputBytes = 32u << 10;
constexpr std::size_t kOutputBytes = 64u << 10;

std::uint32_t LittleEndian(const std::uint8_t* bytes, unsigned count) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < count; ++i) {
        value |= std::uint32_t(bytes[i]) << (8 * i);
    }
    return value;
}

bool Fail(std::string& error, const char* reason) {
    error = reason;
    return false;
}

// Always release zlib's window, including on cancellation and short reads.
struct InflateState {
    z_stream stream = {};
    ~InflateState() { inflateEnd(&stream); }
};

}  // namespace

bool zip_stream_first(const std::string& path, std::uint64_t max_archive_size,
                      std::uint64_t max_unpacked_size, const ZipSink& sink, std::string& error,
                      const std::function<bool(double)>& progress) {
    error.clear();
    std::unique_ptr<FILE, decltype(&std::fclose)> file(std::fopen(path.c_str(), "rb"), &std::fclose);
    if (!file) return Fail(error, "the zip file could not be opened");
    if (std::fseek(file.get(), 0, SEEK_END) != 0) return Fail(error, "the zip file could not be measured");
    const long file_size = std::ftell(file.get());
    if (file_size < 30 || static_cast<std::uint64_t>(file_size) > max_archive_size)
        return Fail(error, "the zip file is too short or too large for a database");
    if (std::fseek(file.get(), 0, SEEK_SET) != 0) return Fail(error, "the zip file could not be read");

    std::uint8_t header[30];
    if (std::fread(header, 1, sizeof header, file.get()) != sizeof header ||
        LittleEndian(header, 4) != 0x04034B50u)
        return Fail(error, "the database is not a zip file");
    const unsigned flags = LittleEndian(header + 6, 2);
    const unsigned method = LittleEndian(header + 8, 2);
    const std::uint32_t expected_crc = LittleEndian(header + 14, 4);
    const std::uint32_t packed_size = LittleEndian(header + 18, 4);
    const std::uint32_t unpacked_size = LittleEndian(header + 22, 4);
    const std::uint64_t data_offset = sizeof header + LittleEndian(header + 26, 2) + LittleEndian(header + 28, 2);
    // Only deflate options and the UTF-8 name flag are meaningful here.
    // In particular, bit 3 would put the sizes/CRC in a later descriptor.
    if ((flags & ~0x0806u) != 0 || (method != 0 && method != 8))
        return Fail(error, "the zip entry uses unsupported packing or flags");
    if (packed_size == 0xFFFFFFFFu || unpacked_size == 0xFFFFFFFFu)
        return Fail(error, "ZIP64 databases are not supported");
    if (unpacked_size > max_unpacked_size)
        return Fail(error, "the zip entry unpacks to more than a database would");
    if (method == 0 && packed_size != unpacked_size)
        return Fail(error, "the stored zip entry has inconsistent lengths");
    if (data_offset > static_cast<std::uint64_t>(file_size) ||
        packed_size > static_cast<std::uint64_t>(file_size) - data_offset)
        return Fail(error, "the zip entry ends before its declared length");
    if (std::fseek(file.get(), static_cast<long>(data_offset), SEEK_SET) != 0)
        return Fail(error, "the zip entry could not be read");
    if (progress && !progress(0.0)) return Fail(error, "stopped");

    std::vector<std::uint8_t> input;
    std::vector<std::uint8_t> output;
    if (!try_resize(input, kInputBytes) || (method == 8 && !try_resize(output, kOutputBytes)))
        return Fail(error, "there is not enough memory to read the zip entry");
    std::uint64_t produced = 0;
    std::uint32_t crc = 0;
    const auto deliver = [&](const std::uint8_t* bytes, std::size_t count) {
        if (count > unpacked_size - produced)
            return Fail(error, "the zip entry unpacks past its declared length");
        crc = zip_crc32_update(crc, bytes, count);
        produced += count;
        if (count != 0 && !sink(bytes, count)) return Fail(error, "stopped");
        return true;
    };
    std::uint32_t remaining = packed_size;
    if (method == 0) {
        while (remaining != 0) {
            const std::size_t count = std::min<std::size_t>(remaining, input.size());
            if (std::fread(input.data(), 1, count, file.get()) != count)
                return Fail(error, "the zip entry ends before its declared length");
            remaining -= count;
            if (!deliver(input.data(), count)) return false;
            if (progress && !progress(static_cast<double>(packed_size - remaining) / packed_size))
                return Fail(error, "stopped");
        }
    } else {
        InflateState state;
        z_stream& stream = state.stream;
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) return Fail(error, "zlib did not start");
        for (;;) {
            if (stream.avail_in == 0 && remaining != 0) {
                const std::size_t count = std::min<std::size_t>(remaining, input.size());
                if (std::fread(input.data(), 1, count, file.get()) != count)
                    return Fail(error, "the zip entry ends before its declared length");
                remaining -= count;
                stream.next_in = input.data();
                stream.avail_in = static_cast<uInt>(count);
            }
            stream.next_out = output.data();
            stream.avail_out = static_cast<uInt>(output.size());
            const int result = inflate(&stream, Z_NO_FLUSH);
            if (result != Z_OK && result != Z_STREAM_END)
                return Fail(error, "the zip entry has damaged or truncated deflate data");
            if (!deliver(output.data(), output.size() - stream.avail_out)) return false;
            const std::uint64_t consumed = packed_size - remaining - stream.avail_in;
            if (progress && !progress(packed_size == 0 ? 0.0 : static_cast<double>(consumed) / packed_size))
                return Fail(error, "stopped");
            if (result == Z_STREAM_END) {
                // Ending before the declared packed range is also corruption:
                // the next ZIP record must never be treated as deflate data.
                if (remaining != 0 || stream.avail_in != 0)
                    return Fail(error, "the deflate stream ends before its declared length");
                break;
            }
        }
    }
    if (produced != unpacked_size) return Fail(error, "the zip entry is not its declared unpacked length");
    if (crc != expected_crc) return Fail(error, "the zip entry has the wrong CRC-32");
    if (progress && !progress(1.0)) return Fail(error, "stopped");
    return true;
}

}  // namespace riftwii
