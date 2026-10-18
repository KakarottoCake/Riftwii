// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gchost.hpp"

#include <cstdarg>
#include <cstdio>

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

bool open_gc_disc(const std::string& path, bool rvz, std::unique_ptr<ByteSource>& out,
                  const RvzDiscSource*& rvz_disc, std::string& error) {
    rvz_disc = nullptr;
    if (!rvz) {
        std::unique_ptr<CardFile> iso(new CardFile(path));
        if (!iso->ok()) {
            error = "cannot open " + path;
            return false;
        }
        out = std::move(iso);
        return true;
    }
    std::shared_ptr<CardFile> file(new CardFile(path));  // not make_shared: its tag type needs RTTI
    if (!file->ok()) {
        error = "cannot open " + path;
        return false;
    }
    std::unique_ptr<RvzDiscSource> disc;
    if (!RvzDiscSource::open(file, disc, error)) {
        error = path + ": " + error;
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
