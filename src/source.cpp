#include "riftwii/source.hpp"

#include <cstdio>
#include <cstring>
#include <limits>

namespace riftwii {

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

bool FileByteSource::open(const std::string& path, std::unique_ptr<FileByteSource>& out,
                          std::string& error) {
    if (path.empty()) {
        error = "empty file path";
        return false;
    }
    if (path.find('\0') != std::string::npos) {
        error = "embedded null in file path";
        return false;
    }
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        error = "cannot open file '" + path + "'";
        return false;
    }
    if (std::fseek(f, 0, SEEK_END) != 0) {
        error = "cannot seek file '" + path + "'";
        std::fclose(f);
        return false;
    }
    long end = std::ftell(f);
    std::fclose(f);
    if (end < 0) {
        error = "cannot stat file '" + path + "'";
        return false;
    }
    std::uint64_t size = static_cast<std::uint64_t>(end);
    if (size > kMaxFileBytes) {
        error = "file too large '" + path + "'";
        return false;
    }
    try {
        out.reset(new FileByteSource(path, size));
    } catch (...) {
        error = "allocation failure";
        return false;
    }
    error.clear();
    return true;
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
    // fopen per call keeps the method const and safe to call from the GUI
    // thread while scans run elsewhere; files are small enough that the
    // extra open is negligible for this milestone.
    std::FILE* f = std::fopen(path_.c_str(), "rb");
    if (f == nullptr) {
        return false;
    }
    // ftell/fseek use long; sizes are capped at 256 MiB so the cast is safe.
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
