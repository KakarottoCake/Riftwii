// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ratingbadge.hpp"

#include <gccore.h>

#include <cstdint>
#include <map>
#include <vector>

#include "covers.hpp"
#include "log.hpp"
#include "riftwii/canvas.hpp"
#include "riftwii/ratingmark.hpp"

#include "rating_cero_a_png.h"
#include "rating_cero_b_png.h"
#include "rating_cero_c_png.h"
#include "rating_cero_d_png.h"
#include "rating_cero_z_png.h"
#include "rating_esrb_ao_png.h"
#include "rating_esrb_e10_png.h"
#include "rating_esrb_e_png.h"
#include "rating_esrb_ec_png.h"
#include "rating_esrb_m_png.h"
#include "rating_esrb_rp_png.h"
#include "rating_esrb_t_png.h"
#include "rating_grac_12_png.h"
#include "rating_grac_15_png.h"
#include "rating_grac_19_png.h"
#include "rating_grac_all_png.h"
#include "rating_gsrr_g_png.h"
#include "rating_gsrr_p_png.h"
#include "rating_gsrr_pg12_png.h"
#include "rating_gsrr_pg15_png.h"
#include "rating_gsrr_r_png.h"
#include "rating_pegi_12_png.h"
#include "rating_pegi_16_png.h"
#include "rating_pegi_18_png.h"
#include "rating_pegi_3_png.h"
#include "rating_pegi_6_png.h"
#include "rating_pegi_7_png.h"

namespace riftwii::wii {
namespace {

struct Embedded {
    const char* name;  // riftwii::rating_mark_name's
    const u8* png;
    u32 size;
};

#define RATING_PNG(name) {#name, rating_##name##_png, rating_##name##_png_size}
const Embedded kPictures[] = {
    RATING_PNG(esrb_ec),  RATING_PNG(esrb_e),    RATING_PNG(esrb_e10),  RATING_PNG(esrb_t),   RATING_PNG(esrb_m),
    RATING_PNG(esrb_ao),  RATING_PNG(esrb_rp),   RATING_PNG(pegi_3),    RATING_PNG(pegi_6),   RATING_PNG(pegi_7),
    RATING_PNG(pegi_12),  RATING_PNG(pegi_16),   RATING_PNG(pegi_18),   RATING_PNG(cero_a),   RATING_PNG(cero_b),
    RATING_PNG(cero_c),   RATING_PNG(cero_d),    RATING_PNG(cero_z),    RATING_PNG(grac_all), RATING_PNG(grac_12),
    RATING_PNG(grac_15),  RATING_PNG(grac_19),   RATING_PNG(gsrr_g),    RATING_PNG(gsrr_p),   RATING_PNG(gsrr_pg12),
    RATING_PNG(gsrr_pg15), RATING_PNG(gsrr_r),
};
#undef RATING_PNG

// The marks are 64 high and at most 64 wide (a few KB each as a texture).
constexpr int kMaxSide = 64;

bool Decode(const Embedded& picture, skin::Tex& out) {
    const std::vector<std::uint8_t> png(picture.png, picture.png + picture.size);
    std::vector<std::uint8_t> rgba;
    int width = 0, height = 0;
    std::string error;
    if (!DecodePngRgba(png, rgba, width, height, error)) {
        logf("About: the %s rating mark can't be read: %s\n", picture.name, error.c_str());
        return false;
    }
    if (width > kMaxSide || height > kMaxSide) {
        logf("About: the %s rating mark is %dx%d, larger than the badge\n", picture.name, width, height);
        return false;
    }
    const std::size_t bytes = rgba.size();
    u8* pixels = skin::Mem2Alloc(bytes);
    if (!pixels) return false;
    if (!riftwii::to_gx_rgba8(rgba.data(), width, height, pixels)) {
        // Not a multiple of 4 on a side: the block is the last taken, so it goes back.
        skin::Mem2Release(pixels, bytes);
        logf("About: the %s rating mark is %dx%d, not a multiple of 4\n", picture.name, width, height);
        return false;
    }
    DCFlushRange(pixels, bytes);
    out.data = pixels;
    out.w = width;
    out.h = height;
    return true;
}

}  // namespace

const skin::Tex& RatingBadge(const std::string& system, const std::string& value) {
    static const skin::Tex none{};
    // Decoded once each, a failure too (so it is logged once).
    static std::map<std::string, skin::Tex> decoded;
    const std::string name = riftwii::rating_mark_name(system, value);
    if (name.empty()) return none;
    const auto found = decoded.find(name);
    if (found != decoded.end()) return found->second;
    skin::Tex& tex = decoded[name];
    for (const Embedded& picture : kPictures) {
        if (name == picture.name) {
            Decode(picture, tex);
            break;
        }
    }
    return tex;
}

}  // namespace riftwii::wii
