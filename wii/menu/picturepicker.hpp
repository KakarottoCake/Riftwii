// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <functional>
#include "libwiigui/gui.h"
#include "riftwii/picturepick.hpp"

namespace riftwii::wii {

struct RoundClip;
struct PicturePreview {
    std::size_t pixel_bytes = 0;
    // Raw tiled pixels, without a file header. A future cover picker can
    // supply its portrait crop and draw it fitted inside the tile's box.
    std::function<std::vector<std::uint8_t>(const std::uint8_t*, int, int)> make;
    std::function<void(const u8*, const RoundClip&, int)> draw;
};

// The cover picker's thumbnails: a stored cover, portrait, fitted in the
// tile (the default preview is an emulator game's Channels picture).
PicturePreview CoverPicturePreview();

// A reusable modal widget: a metadata provider in, a chosen original PNG
// out. It owns paging, cancellation and six thumbnails, but no save path.
// Construct, Pump and destroy with the GUI halted. Draw/Update do no IO.
class GuiPicturePicker : public GuiElement {
public:
    using Discover = std::function<bool(std::vector<PictureCandidate>&, std::string&)>;
    enum class Result { Waiting, Cancelled, Automatic, Picked };

    GuiPicturePicker(Discover discover, bool wide, PicturePreview preview = {});
    ~GuiPicturePicker() override;
    void Pump();
    Result Choice() const { return choice_; }
    std::vector<std::uint8_t> TakePicture();
    void Back() { choice_ = Result::Cancelled; }
    void Accept() { Choose(focus_); }
    void Draw() override;
    void Update(GuiTrigger* trigger) override;

private:
    enum class Job { None, Discover, Thumbnail, Pick };
    struct Tile {
        bool attempted = false;
        bool ready = false;
        GuiText* caption = nullptr;
    };
    static void RunJob();
    void Work();
    void ClearPage();
    void TurnPage(int direction);
    void Choose(int slot);
    void Labels();
    int Count() const;
    int TileAt(int x, int y) const;
    float TileX(int slot) const;
    float TileWidth() const;
    static int TileY(int slot);

    Discover discover_;
    PicturePreview preview_;
    std::size_t page_bytes_ = 0;
    bool wide_ = false;
    std::vector<PictureCandidate> candidates_;
    std::array<Tile, kPicturePageSize> tiles_{};
    u8* pixels_ = nullptr;
    bool texture_dirty_ = false;
    GuiText page_text_;
    GuiText status_text_;
    GuiText automatic_text_;
    Result choice_ = Result::Waiting;
    bool discovered_ = false;
    int page_ = 0;
    int requested_page_ = 0;
    int focus_ = 0;
    int pick_slot_ = -1;
    unsigned frame_ = 0;

    // These request/result fields belong to the worker until done_ is
    // published. Pump consumes them only after that acquire operation.
    Job job_ = Job::None;
    int job_page_ = 0;
    int job_slot_ = 0;
    bool job_ok_ = false;
    bool discard_job_ = false;
    Sha256Digest job_hash_{};
    std::atomic<bool> done_{false};
    std::vector<PictureCandidate> found_;
    std::vector<std::uint8_t> answer_;
    std::string error_;
};

}  // namespace riftwii::wii
