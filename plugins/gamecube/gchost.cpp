// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gchost.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>

namespace riftwii::wii {
namespace {

// A reason as the table hands it back: the bytes up to the first NUL.
constexpr std::size_t kReasonBytes = 256;

// The image file on the card, read with stdio (the DOL's CardFile in
// wii/launch/gclaunch.cpp, through rwpruntime.cpp's stdio).
class CardFile final : public ByteSource {
public:
    explicit CardFile(const std::string& path) : f_(std::fopen(path.c_str(), "rb")) {
        if (f_ && fseeko(f_, 0, SEEK_END) == 0) size_ = static_cast<std::uint64_t>(ftello(f_));
    }
    ~CardFile() override {
        if (f_) std::fclose(f_);
    }
    CardFile(const CardFile&) = delete;
    CardFile& operator=(const CardFile&) = delete;
    bool ok() const { return f_ != nullptr; }
    std::uint64_t size() const override { return size_; }
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override {
        if (offset > size_ || length > size_ - offset) return false;
        if (fseeko(f_, static_cast<off_t>(offset), SEEK_SET) != 0) return false;
        return std::fread(destination, 1, length, f_) == length;
    }

private:
    std::FILE* f_;
    std::uint64_t size_ = 0;
};

// The image file on the USB drive, read through the host's usb_read (d2x's
// /dev/usb2) by its pieces, a sector-aligned stretch at a time.
class UsbDriveFile final : public ByteSource {
public:
    UsbDriveFile(std::vector<Fragment> fragments, std::uint64_t size)
        : fragments_(std::move(fragments)), size_(size), sector_(kSectorBytes * kSectorsAtOnce) {}
    bool ok() const { return RWP_HOST_HAS(&plugin::host(), usb_read) && !fragments_.empty(); }
    std::uint64_t size() const override { return size_; }
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override {
        if (offset > size_ || length > size_ - offset) return false;
        while (length != 0) {
            // The piece holding the file's sector at `offset`.
            const std::uint64_t file_sector = offset / kSectorBytes;
            std::uint64_t first = 0;
            const Fragment* piece = nullptr;
            for (const Fragment& f : fragments_) {
                if (file_sector < first + f.sector_count) {
                    piece = &f;
                    break;
                }
                first += f.sector_count;
            }
            if (piece == nullptr) return false;
            const std::uint64_t into_piece = file_sector - first;
            std::uint64_t sectors = piece->sector_count - into_piece;
            if (sectors > kSectorsAtOnce) sectors = kSectorsAtOnce;
            const std::size_t skip = static_cast<std::size_t>(offset % kSectorBytes);
            const std::uint64_t wanted_sectors = (skip + length + kSectorBytes - 1) / kSectorBytes;
            if (sectors > wanted_sectors) sectors = wanted_sectors;
            if (!plugin::host().usb_read(piece->sector + into_piece, static_cast<std::uint32_t>(sectors),
                                         sector_.data())) {
                return false;
            }
            std::size_t got = static_cast<std::size_t>(sectors * kSectorBytes) - skip;
            if (got > length) got = length;
            std::memcpy(destination, sector_.data() + skip, got);
            destination += got;
            offset += got;
            length -= got;
        }
        return true;
    }

private:
    static constexpr std::uint64_t kSectorBytes = 512;
    static constexpr std::uint64_t kSectorsAtOnce = 64;  // 32 KB, as the menu reads the drive
    std::vector<Fragment> fragments_;
    std::uint64_t size_ = 0;
    mutable std::vector<std::uint8_t> sector_;
};

// A file shared with whatever else reads it, as the disc itself.
class ImageView final : public ByteSource {
public:
    explicit ImageView(std::shared_ptr<ByteSource> file) : file_(std::move(file)) {}
    std::uint64_t size() const override { return file_->size(); }
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override {
        return file_->read(offset, destination, length);
    }

private:
    std::shared_ptr<ByteSource> file_;
};

// The image's bytes as a file: on the card, or on the USB drive.
std::shared_ptr<ByteSource> open_image_file(const GcImage& image) {
    if (!image.usb_fragments.empty()) {
        std::shared_ptr<UsbDriveFile> usb(new UsbDriveFile(image.usb_fragments, image.usb_bytes));
        return usb->ok() ? usb : nullptr;
    }
    std::shared_ptr<CardFile> card(new CardFile(image.path));  // not make_shared: its tag type needs RTTI
    return card->ok() ? card : nullptr;
}

}  // namespace

void logf(const char* format, ...) {
    // The longest line the loader writes is under 600 characters (its MEM1
    // layout); a longer one is cut, never overrun.
    char line[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    plugin::host().log_text(line);
}

void LogClose() { plugin::host().log_close(); }

bool resolve_sd_file(const std::string& sd_path, Fat32File& out, std::string& error) {
    out = Fat32File{};
    // Most files are in a few pieces; a scattered one is asked again with
    // room for all of them.
    std::vector<rwp_fragment> pieces(16);
    std::uint32_t count = 0;
    std::uint64_t file_bytes = 0;
    char reason[kReasonBytes] = "";
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto room = static_cast<std::uint32_t>(pieces.size());
        if (!plugin::host().sd_file_fragments(sd_path.c_str(), pieces.data(), room, &count, &file_bytes, reason,
                                              sizeof(reason))) {
            error = reason;
            return false;
        }
        if (count <= room) break;
        pieces.resize(count);
    }
    if (count > pieces.size()) {
        error = "its pieces on the card changed while they were read";
        return false;
    }
    out.entry.size = static_cast<std::uint32_t>(file_bytes);
    for (std::uint32_t i = 0; i < count; ++i) out.fragments.push_back(Fragment{pieces[i].sector, pieces[i].sector_count});
    return true;
}

void forget_sd_layout() { plugin::host().sd_forget_layout(); }

bool hand_over_sd_card(rwp_sd_card& card, std::string& error) {
    char reason[kReasonBytes] = "";
    if (plugin::host().sd_hand_over(&card, reason, sizeof(reason))) return true;
    error = reason;
    return false;
}

bool open_gc_disc(const GcImage& image, std::unique_ptr<ByteSource>& out, const RvzDiscSource*& rvz_disc,
                  std::string& error) {
    rvz_disc = nullptr;
    std::shared_ptr<ByteSource> file = open_image_file(image);
    if (!file) {
        error = "cannot open " + image.path;
        return false;
    }
    if (!image.rvz) {
        out.reset(new ImageView(file));
        return true;
    }
    std::unique_ptr<RvzDiscSource> disc;
    if (!RvzDiscSource::open(file, disc, error)) {
        error = image.path + ": " + error;
        return false;
    }
    rvz_disc = disc.get();
    out = std::move(disc);
    return true;
}

bool plugin_clear_of(std::uint32_t start, std::uint32_t end, std::string& error) {
    // This function is in the plugin's image, which with its .bss is one
    // arena at most: an address further than that from [start, end) means
    // none of the plugin is inside.
    const auto here = reinterpret_cast<std::uint32_t>(&plugin_clear_of);
    const bool clear = here + RWP_ARENA_BYTES <= start || here >= end + RWP_ARENA_BYTES;
    if (clear) return true;
    char text[160];
    std::snprintf(text, sizeof(text),
                  "the gamecube plugin was loaded at 0x%08x, inside the game's memory in MEM2 (0x%08x-0x%08x)",
                  static_cast<unsigned>(here), static_cast<unsigned>(start), static_cast<unsigned>(end));
    error = text;
    return false;
}

}  // namespace riftwii::wii
