// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/brlyt.hpp"

// Banner animations (arc/anim/*.brlan), from the public format pages
// https://mkwiiki.org/wiki/BRLAN_(File_Format) and its Targets page: for
// named panes and materials, tracks of key frames that move a pane, turn
// and scale it, fade it, show or hide it, change its vertex or material
// colours, slide its texture or swap the texture shown.
namespace riftwii {

enum class AnimKind : std::uint8_t { PaneSrt, TextureSrt, Visibility, VertexColor, MaterialColor, TexturePattern };

struct AnimKey {
    float frame = 0, value = 0, slope = 0;
};

struct AnimTrack {
    AnimKind kind = AnimKind::PaneSrt;
    std::uint8_t index = 0;   // which texture SRT / texture map
    std::uint8_t target = 0;  // which value (the Targets page's tables)
    bool step = false;        // key type 1: held values, no blending
    std::vector<AnimKey> keys;
};

struct AnimTarget {
    std::string name;  // a pane's or a material's
    bool material = false;
    std::vector<AnimTrack> tracks;
};

struct Animation {
    std::uint16_t frames = 0;
    bool loop = false;
    std::vector<std::string> textures;  // what texture-pattern tracks pick from
    std::vector<AnimTarget> targets;
};

bool parse_brlan(const std::uint8_t* data, std::size_t size, Animation& out, std::string& error);

// A track's value at `frame`: held before the first key and after the
// last, Hermite between keys (each key's slope is per frame), or the last
// key's value at or before `frame` for step tracks.
float track_value(const AnimTrack& track, float frame);

// Sets what `anim` animates in `layout` to its values at `frame`.
// Texture-pattern tracks name a texture of `anim`; it is looked up in
// the layout's texture list (and added when missing, so the caller loads it).
void apply_animation(const Animation& anim, float frame, Layout& layout);

// Puts back in `work` (a copy of `base`, maybe animated since) every value
// an animation can change, without allocating: what a frame starts from.
void reset_animated(const Layout& base, Layout& work);

}  // namespace riftwii
