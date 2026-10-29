// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "picturepicker.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <ogc/cache.h>

#include "bannerplay.hpp"
#include "covers.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "netsock.hpp"
#include "online.hpp"
#include "skin.hpp"
#include "video.h"
#include "wiidrc.h"
#include "riftwii/checkedalloc.hpp"
#include "riftwii/emulatorart.hpp"
#include "riftwii/emulatorbanner.hpp"
#include "riftwii/skinpaint.hpp"

namespace riftwii::wii {
namespace {
GuiPicturePicker* active_picker = nullptr;
constexpr std::size_t kPreviewPixelsMax = 32u << 10;

bool pressed(GuiTrigger* trigger, u32 remote, u16 pad, u16 gamepad) {
    return (trigger->wpad && (trigger->wpad->btns_d & remote)) ||
           (trigger->pad.btns_d & pad) || (trigger->wiidrcdata.btns_d & gamepad);
}

void place(GuiText& text, GuiElement* parent, int x, int y) {
    text.SetParent(parent);
    text.SetAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
    text.SetPosition(x - screenwidth / 2, y);
}

const char* source_name(PictureSource source) {
    switch (source) {
        case PictureSource::Title: return tr("libretro title screen");
        case PictureSource::Screenshot: return tr("libretro screenshot");
        case PictureSource::Boxart: return tr("libretro box art");
        case PictureSource::SteamGrid: return "SteamGridDB";
    }
    return "";
}

void spinner(float x, float y, unsigned frame) {
    for (unsigned dot = 0; dot < 8; ++dot) {
        const float angle = dot * 3.14159265f / 4;
        const unsigned age = (dot + 8 - frame / 6 % 8) % 8;
        Menu_DrawRectangle(x + std::cos(angle) * 11 - 2, y + std::sin(angle) * 11 - 2, 4, 4,
                           skin::WithAlpha(skin::kAccent, 255 - age * 26), 1);
    }
}
}  // namespace

GuiPicturePicker::GuiPicturePicker(Discover discover, bool wide, PicturePreview preview)
    : discover_(std::move(discover)), preview_(std::move(preview)), wide_(wide),
      page_text_("", 17, skin::kInkSoft), status_text_("", 17, skin::kInkSoft),
      automatic_text_(tr("Back to automatic"), 20, skin::kInk) {
    width = screenwidth;
    height = screenheight;
    selectable = true;
    if (!preview_.make && !preview_.draw && preview_.pixel_bytes == 0) {
        preview_.pixel_bytes = kEmulatorBannerPixelBytes;
        preview_.make = [wide](const std::uint8_t* rgba, int w, int h) {
            auto file = make_emulator_banner(rgba, w, h, wide);
            if (file.size() == kEmulatorBannerFileSize) file.erase(file.begin(), file.begin() + 8);
            return file;
        };
        preview_.draw = DrawChannelPicture;
    }
    if (preview_.make && preview_.draw && preview_.pixel_bytes > 0 &&
        preview_.pixel_bytes <= kPreviewPixelsMax && preview_.pixel_bytes % 32 == 0) {
        page_bytes_ = kPicturePageSize * preview_.pixel_bytes;
        if (can_allocate(page_bytes_)) pixels_ = static_cast<u8*>(memalign(32, page_bytes_));
    }
    place(page_text_, this, 320, 70);
    place(status_text_, this, 320, 346);
    status_text_.SetWrap(true, 520, 1);
    place(automatic_text_, this, 320, 382);
    for (auto& tile : tiles_) {
        tile.caption = new GuiText("", 14, skin::kInkSoft);
        tile.caption->SetWrap(true, 160, 1);
    }
    Labels();
}

GuiPicturePicker::~GuiPicturePicker() {
    if (job_ != Job::None) {
        NetCancelBackground();
        NetWaitForBackground();
    }
    active_picker = nullptr;
    GX_DrawDone();
    std::free(pixels_);
    for (auto& tile : tiles_) delete tile.caption;
    logf("Picture picker: closed and released its page and downloads.\n");
}

int GuiPicturePicker::Count() const {
    const int first = page_ * static_cast<int>(kPicturePageSize);
    return std::max(0, std::min(static_cast<int>(kPicturePageSize), static_cast<int>(candidates_.size()) - first));
}

float GuiPicturePicker::TileWidth() const { return wide_ ? 171 : 128; }
float GuiPicturePicker::TileX(int slot) const {
    const float spacing = wide_ ? 182 : 160;
    return 320 + (slot % 3 - 1) * spacing - TileWidth() / 2;
}
int GuiPicturePicker::TileY(int slot) { return 106 + slot / 3 * 130; }

void GuiPicturePicker::Labels() {
    page_text_.SetText(tr("Page {1} of {2}", {std::to_string(page_ + 1),
                       std::to_string(picture_page_count(candidates_.size()))}).c_str());
    for (int slot = 0; slot < Count(); ++slot) {
        auto& tile = tiles_[slot];
        tile.caption->SetText(source_name(candidates_[page_ * kPicturePageSize + slot].source));
        place(*tile.caption, this, static_cast<int>(TileX(slot) + TileWidth() / 2), TileY(slot) + 94);
    }
}

void GuiPicturePicker::ClearPage() {
    // HaltGui prevents new draws, and this fence retires the GPU's reads
    // before the same six blocks are used for another page.
    GX_DrawDone();
    for (auto& tile : tiles_) {
        tile.attempted = false;
        tile.ready = false;
    }
    if (pixels_) std::memset(pixels_, 0, page_bytes_);
    texture_dirty_ = true;
    pick_slot_ = -1;
    status_text_.SetText("");
}

void GuiPicturePicker::TurnPage(int direction) {
    if (!discovered_) return;
    const int pages = static_cast<int>(picture_page_count(candidates_.size()));
    requested_page_ = (requested_page_ + direction + pages) % pages;
}

void GuiPicturePicker::Choose(int slot) {
    if (slot == Count()) {
        choice_ = Result::Automatic;
    } else if (job_ == Job::Pick) {
        return;
    } else if (slot >= 0 && slot < Count() && tiles_[slot].ready && job_ == Job::None) {
        pick_slot_ = slot;
    } else if (slot >= 0 && slot < Count() && tiles_[slot].ready) {
        // Selection takes priority over the remaining thumbnails.
        pick_slot_ = slot;
        discard_job_ = true;
        NetCancelBackground();
    }
}

void GuiPicturePicker::RunJob() { active_picker->Work(); }

void GuiPicturePicker::Work() {
    job_ok_ = false;
    error_.clear();
    if (job_ == Job::Discover) {
        job_ok_ = discover_(found_, error_);
    } else {
        const std::size_t index = job_page_ * kPicturePageSize + job_slot_;
        std::vector<std::uint8_t> png;
        std::vector<std::uint8_t> rgba;
        int image_width = 0, image_height = 0;
        const auto& candidate = candidates_[index];
        if (picture_candidate_valid(candidate) &&
            HttpGet(candidate.url, png, error_, kEmulatorCoverMax, 10000, nullptr, HttpRedirects::Never) &&
            !NetCancelRequested() && DecodePngRgba(png, rgba, image_width, image_height, error_) &&
            !NetCancelRequested()) {
            if (job_ == Job::Pick) {
                answer_ = std::move(png);
                job_ok_ = true;
            } else {
                answer_ = preview_.make(rgba.data(), image_width, image_height);
                job_ok_ = answer_.size() == preview_.pixel_bytes;
            }
        }
    }
    done_.store(true, std::memory_order_release);
}

void GuiPicturePicker::Pump() {
    if (choice_ != Result::Waiting) return;
    if (page_ != requested_page_) {
        if (job_ != Job::None) {
            discard_job_ = true;
            NetCancelBackground();
            logf("Picture picker: stopped page %d's unfinished download.\n", page_ + 1);
        }
        ClearPage();
        page_ = requested_page_;
        focus_ = 0;
        Labels();
        logf("Picture picker: showing page %d; only this page's pictures will be fetched.\n", page_ + 1);
    }
    if (job_ != Job::None) {
        if (!done_.load(std::memory_order_acquire)) return;
        NetWaitForBackground();
        if (!discard_job_) {
            if (job_ == Job::Discover) {
                discovered_ = true;
                if (job_ok_ && found_.size() <= kPictureCandidatesMax) candidates_ = std::move(found_);
                if (candidates_.empty()) status_text_.SetText(job_ok_ ? tr("No pictures found.") : tr("Could not load pictures."));
                Labels();
            } else if (job_ == Job::Pick) {
                pick_slot_ = -1;
                if (job_ok_) choice_ = Result::Picked;
                else status_text_.SetText(tr("Could not load this picture."));
            } else if (job_ok_ && pixels_) {
                u8* texture = pixels_ + job_slot_ * preview_.pixel_bytes;
                std::memcpy(texture, answer_.data(), preview_.pixel_bytes);
                DCFlushRange(texture, preview_.pixel_bytes);
                tiles_[job_slot_].ready = true;
                texture_dirty_ = true;
                logf("Picture picker: loaded picture %d on page %d.\n", job_slot_ + 1, job_page_ + 1);
            } else {
                logf("Picture picker: picture %d on page %d could not be loaded.\n", job_slot_ + 1, job_page_ + 1);
                status_text_.SetText(tr("A picture could not be loaded."));
            }
        }
        job_ = Job::None;
        discard_job_ = false;
        if (choice_ == Result::Picked) return;
        std::vector<std::uint8_t>().swap(answer_);
        std::vector<PictureCandidate>().swap(found_);
    }
    if (!pixels_) {
        discovered_ = true;
        status_text_.SetText(tr("Not enough memory."));
        return;
    }
    if (NetBackgroundBusy() || choice_ != Result::Waiting) return;
    Job next = Job::None;
    if (!discovered_) next = Job::Discover;
    else if (pick_slot_ >= 0) next = Job::Pick;
    else {
        for (int slot = 0; slot < Count(); ++slot) {
            if (!tiles_[slot].attempted) {
                job_slot_ = slot;
                next = Job::Thumbnail;
                break;
            }
        }
    }
    if (next == Job::None) return;
    if (next == Job::Pick) job_slot_ = pick_slot_;
    job_ = next;
    job_page_ = page_;
    done_.store(false, std::memory_order_relaxed);
    active_picker = this;
    if (!NetRunInBackground(RunJob, true, false)) {
        job_ = Job::None;
        return;
    }
    if (next == Job::Thumbnail) {
        tiles_[job_slot_].attempted = true;
        logf("Picture picker: loading picture %d on page %d.\n", job_slot_ + 1, job_page_ + 1);
    }
}

std::vector<std::uint8_t> GuiPicturePicker::TakePicture() { return std::move(answer_); }

void GuiPicturePicker::Draw() {
    if (!IsVisible()) return;
    ++frame_;
    if (texture_dirty_) {
        GX_InvalidateTexAll();
        texture_dirty_ = false;
    }
    page_text_.Draw();
    status_text_.Draw();
    for (int slot = 0; slot < Count(); ++slot) {
        const float x = TileX(slot);
        const int y = TileY(slot);
        const bool focused = slot == focus_;
        const float margin = kChannelFrameMargin;
        skin::DrawNine(focused ? skin::channelCardOver9 : skin::channelCard9,
                       x - margin, y - margin, TileWidth() + 2 * margin, 88 + 2 * margin, kChannelFrameCorner);
        if (tiles_[slot].ready) {
            const float radius = std::max(0.0f, kChannelRadius * skin::ChannelCorners() - 1);
            preview_.draw(pixels_ + slot * preview_.pixel_bytes,
                          {x + 1, static_cast<float>(y + 1), TileWidth() - 2, 86, radius}, 255);
            skin::DrawNine(focused ? skin::channelFrameOver9 : skin::channelFrame9,
                           x - margin, y - margin, TileWidth() + 2 * margin, 88 + 2 * margin, kChannelFrameCorner);
        } else if (job_ != Job::None && job_page_ == page_ && job_slot_ == slot && !discard_job_) {
            spinner(x + TileWidth() / 2, y + 44, frame_);
        }
        tiles_[slot].caption->Draw();
    }
    if (!discovered_) spinner(320, 220, frame_);
    if (job_ == Job::Pick) spinner(320, 360, frame_);
    if (picture_page_count(candidates_.size()) > 1) {
        skin::Draw(skin::arrowLeft, 8, 202);
        skin::Draw(skin::arrowRight, 588, 202);
    }
    skin::DrawNine(focus_ == Count() ? skin::tile9Over : skin::tile9, 148, 374, 344, 36, 12);
    automatic_text_.Draw();
}

int GuiPicturePicker::TileAt(int x, int y) const {
    for (int slot = 0; slot < Count(); ++slot) {
        if (x >= TileX(slot) && x < TileX(slot) + TileWidth() && y >= TileY(slot) && y < TileY(slot) + 114) return slot;
    }
    return x >= 148 && x < 492 && y >= 374 && y < 410 ? Count() : -1;
}

void GuiPicturePicker::Update(GuiTrigger* trigger) {
    if (state == STATE::DISABLED || !trigger || choice_ != Result::Waiting) return;
    if (pressed(trigger, WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B, WIIDRC_BUTTON_B)) {
        Back();
        return;
    }
    if (pressed(trigger, WPAD_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_MINUS, PAD_TRIGGER_L, WIIDRC_BUTTON_MINUS)) TurnPage(-1);
    if (pressed(trigger, WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS, PAD_TRIGGER_R, WIIDRC_BUTTON_PLUS)) TurnPage(1);
    const bool accept = pressed(trigger, WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A, PAD_BUTTON_A, WIIDRC_BUTTON_A);
    if (trigger->wpad && trigger->wpad->ir.valid) {
        const int x = trigger->wpad->ir.x, y = trigger->wpad->ir.y;
        const int slot = TileAt(x, y);
        if (slot >= 0) focus_ = slot;
        if (accept) {
            if (slot >= 0) Choose(slot);
            else if (y >= 190 && y < 260 && x < 52) TurnPage(-1);
            else if (y >= 190 && y < 260 && x >= 588) TurnPage(1);
        }
        return;
    }
    // As in Home, another controller's D-pad does not steal an active pointer.
    for (int channel = 0; channel < 4; ++channel) {
        if (userInput[channel].wpad && userInput[channel].wpad->ir.valid) return;
    }
    if (trigger->Left()) {
        if (focus_ < Count() && focus_ % 3 > 0) --focus_;
        else TurnPage(-1);
    } else if (trigger->Right()) {
        if (focus_ < Count() && focus_ % 3 < 2 && focus_ + 1 < Count()) ++focus_;
        else TurnPage(1);
    } else if (trigger->Down()) {
        focus_ = focus_ + 3 < Count() ? focus_ + 3 : Count();
    } else if (trigger->Up()) {
        focus_ = focus_ == Count() ? std::max(0, Count() - 1) : std::max(0, focus_ - 3);
    }
    if (accept) Choose(focus_);
}

}  // namespace riftwii::wii
