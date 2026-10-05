// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <gccore.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "riftwii/bnr.hpp"
#include "riftwii/brlan.hpp"
#include "riftwii/brlyt.hpp"

// Plays a game's icon or banner (riftwii/bnr.hpp) with GX: its layout's
// quads (riftwii/brlyt.hpp), animated frame by frame (riftwii/brlan.hpp),
// textures drawn from the archive where they lie. Each material maps its
// texture's black and white onto its own two colours, times the pane's
// vertex colours, in two TEV stages.
namespace riftwii::wii {

class BannerPlayer {
public:
    BannerPlayer() = default;
    ~BannerPlayer();
    BannerPlayer(const BannerPlayer&) = delete;
    BannerPlayer& operator=(const BannerPlayer&) = delete;

    // `icon`: the Wii Menu's 128x96 tile picture; else the full banner.
    bool Load(const std::vector<std::uint8_t>& opening_bnr, bool icon, std::string& error);
    bool Loaded() const { return loaded_; }
    // The game's name in the Wii's language, as the banner gives it.
    const std::string& Name() const { return name_; }
    // One frame on: the start animation once, then the loop.
    void Step();
    void Restart();
    // The shown area (the icon's middle 128x96, or the whole banner) fitted
    // into w x h at (x, y) in screen pixels, clipped to that box.
    void Draw(float x, float y, float w, float h, int alpha);
    // Memory held (the unpacked archive).
    std::size_t Bytes() const { return arc_size_; }

private:
    struct Texture {
        GXTexObj obj;
        GXTlutObj tlut;
        bool ci = false;
        bool ok = false;
        std::uint8_t* own = nullptr;  // a copy, when the archive's was not 32-byte aligned
    };
    Texture* TextureOf(std::size_t index);
    void Free();

    bool loaded_ = false;
    bool icon_ = false;
    std::string name_;
    std::uint8_t* arc_ = nullptr;
    std::size_t arc_size_ = 0;
    U8Archive u8_;
    Layout base_, work_;
    Animation start_, loop_;
    bool has_start_ = false, has_loop_ = false;
    int frame_ = 0;
    std::map<std::string, Texture> textures_;
};

}  // namespace riftwii::wii
