// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/source.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <sys/stat.h>

namespace riftwii {

const char* to_string(OpenStatus status) {
    switch (status) {
    case OpenStatus::Ok: return "ok";
    case OpenStatus::NotFound: return "not found";
    case OpenStatus::IoError: return "I/O error";
    case OpenStatus::TooLarge: return "too large";
    case OpenStatus::Invalid: return "invalid";
    }
    return "unknown";
}

MemorySource::MemorySource(std::vector<std::uint8_t> data) : data_(std::move(data)) {}

std::uint64_t MemorySource::size() const {
    return static_cast<std::uint64_t>(data_.size());
}

bool MemorySource::read(std::uint64_t offset, std::uint8_t* destination,
                        std::size_t length) const {
    if (offset > data_.size() ||
        static_cast<std::uint64_t>(length) > static_cast<std::uint64_t>(data_.size()) - offset) {
        return false;
    }
    if (length > 0) {
        if (destination == nullptr) return false;
        std::memcpy(destination, data_.data() + static_cast<std::size_t>(offset), length);
    }
    return true;
}

std::uint64_t ZeroSource::size() const {
    return std::numeric_limits<std::uint64_t>::max();
}

bool ZeroSource::read(std::uint64_t offset, std::uint8_t* destination,
                      std::size_t length) const {
    if (offset > size() || static_cast<std::uint64_t>(length) > size() - offset) {
        return false;
    }
    if (length > 0) {
        if (destination == nullptr) return false;
        std::memset(destination, 0, length);
    }
    return true;
}

FileByteSource::FileByteSource(std::string path, std::uint64_t size)
    : path_(std::move(path)), size_(size) {}

OpenStatus FileByteSource::open(const std::string& path, std::unique_ptr<FileByteSource>& out,
                                std::string& error) {
    if (path.empty()) {
        error = "empty file path";
        return OpenStatus::Invalid;
    }
    if (path.find('\0') != std::string::npos) {
        error = "embedded null in file path";
        return OpenStatus::Invalid;
    }
    struct stat st;
    std::memset(&st, 0, sizeof(st));
    if (::stat(path.c_str(), &st) != 0) {
        const int err = errno;
        if (err == ENOENT || err == ENOTDIR) {
            error = "no such file '" + path + "'";
            return OpenStatus::NotFound;
        }
        error = "cannot stat file '" + path + "': " + std::strerror(err);
        return OpenStatus::IoError;
    }
    if (!S_ISREG(st.st_mode)) {
        error = "not a regular file '" + path + "'";
        return OpenStatus::Invalid;
    }
    // A negative st_size can only mean a 32-bit off_t has wrapped, i.e. the
    // file is far beyond the cap.
    if (st.st_size < 0) {
        error = "file too large '" + path + "'";
        return OpenStatus::TooLarge;
    }
    const std::uint64_t size = static_cast<std::uint64_t>(st.st_size);
    if (size > kMaxFileBytes) {
        error = "file too large '" + path + "'";
        return OpenStatus::TooLarge;
    }
    // Prove readability now so a permission problem is reported at preflight
    // rather than as a mysterious read failure later.
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        const int err = errno;
        error = "cannot open file '" + path + "': " + std::strerror(err);
        return (err == ENOENT || err == ENOTDIR) ? OpenStatus::NotFound : OpenStatus::IoError;
    }
    std::fclose(f);
    try {
        out.reset(new FileByteSource(path, size));
    } catch (...) {
        error = "allocation failure";
        return OpenStatus::IoError;
    }
    error.clear();
    return OpenStatus::Ok;
}

std::uint64_t FileByteSource::size() const {
    return size_;
}

bool FileByteSource::read(std::uint64_t offset, std::uint8_t* destination,
                          std::size_t length) const {
    if (offset > size_ || static_cast<std::uint64_t>(length) > size_ - offset) {
        return false;
    }
    if (length == 0) {
        return true;
    }
    if (destination == nullptr) {
        return false;
    }
    // fseek positions with `long`; the cap keeps every valid offset inside
    // its range, and the guard makes that assumption explicit.
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<long>::max())) {
        return false;
    }
    // fopen per call keeps the method const and safe to call from the GUI
    // thread while scans run elsewhere; files are small enough that the
    // extra open is negligible for this milestone.
    std::FILE* f = std::fopen(path_.c_str(), "rb");
    if (f == nullptr) {
        return false;
    }
    if (std::fseek(f, static_cast<long>(offset), SEEK_SET) != 0) {
        std::fclose(f);
        return false;
    }
    std::size_t done = 0;
    while (done < length) {
        std::size_t got = std::fread(destination + done, 1, length - done, f);
        if (got == 0) {
            if (std::ferror(f) != 0) {
                std::fclose(f);
                return false;
            }
            break;  // EOF: tested size above, so this means truncation raced us.
        }
        done += got;
    }
    std::fclose(f);
    return done == length;
}

}  // namespace riftwii
