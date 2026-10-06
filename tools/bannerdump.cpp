// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Host tool: a game's opening.bnr from an RVZ (or a loose opening.bnr),
// what it holds, and its icon and banner drawn as RiftWii draws them
// (riftwii/brlyt.hpp's quads, rasterised here) for checking the banner
// code against real games.
//   bannerdump <game.rvz | opening.bnr> <out dir>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "riftwii/bnr.hpp"
#include "riftwii/brlan.hpp"
#include "riftwii/brlyt.hpp"
#include "riftwii/disc.hpp"
#include "riftwii/pngencode.hpp"
#include "riftwii/rvz.hpp"
#include "riftwii/tpl.hpp"

using namespace riftwii;

namespace {

class StreamSource final : public ByteSource {
public:
    explicit StreamSource(const std::string& path) : in_(path, std::ios::binary) {
        in_.seekg(0, std::ios::end);
        size_ = in_ ? static_cast<std::uint64_t>(in_.tellg()) : 0;
    }
    bool ok() const { return static_cast<bool>(in_); }
    std::uint64_t size() const override { return size_; }
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override {
        in_.clear();
        in_.seekg(static_cast<std::streamoff>(offset));
        in_.read(reinterpret_cast<char*>(destination), static_cast<std::streamsize>(length));
        return static_cast<std::size_t>(in_.gcount()) == length;
    }

private:
    mutable std::ifstream in_;
    std::uint64_t size_ = 0;
};

bool write_file(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

bool opening_from_rvz(const std::string& path, std::vector<std::uint8_t>& bnr, std::string& error) {
    auto file = std::make_shared<StreamSource>(path);
    std::unique_ptr<RvzImage> image;
    if (!file->ok() || !RvzImage::open(file, image, error)) return false;
    RvzRawSource raw(*image);
    std::vector<PartitionEntry> table;
    PartitionEntry game;
    PartitionHeader header;
    if (!read_partition_table(raw, table, error)) return false;
    if (!find_game_partition(table, game)) {
        error = "no game partition";
        return false;
    }
    if (!read_partition_header(raw, game.offset, header, error)) return false;
    RvzPartitionSource data(*image, image->partition_at(header.data_offset));
    return read_partition_file(data, "/opening.bnr", bnr, error);
}

struct Texture {
    int w = 0, h = 0;
    std::vector<std::uint8_t> rgba;
};

int wrap(int v, int n, int mode) {
    if (mode == 1) return ((v % n) + n) % n;
    if (mode == 2) {
        const int p = ((v % (2 * n)) + 2 * n) % (2 * n);
        return p < n ? p : 2 * n - 1 - p;
    }
    return v < 0 ? 0 : v >= n ? n - 1 : v;
}

// Draws one layout's quads into a w x h RGBA canvas (layout centre at the
// canvas centre, y up).
void rasterise(const Layout& lyt, const std::vector<Texture*>& textures, int W, int H, std::vector<std::uint8_t>& px) {
    px.assign(static_cast<std::size_t>(W) * H * 4, 0);
    for (const LytQuad& q : layout_quads(lyt)) {
        const LytMaterial* mat = q.material >= 0 ? &lyt.materials[q.material] : nullptr;
        const Texture* tex = nullptr;
        const Texture* mask_tex = nullptr;
        LytTexMap map;
        if (mat && !mat->maps.empty() && mat->maps[0].texture < textures.size()) {
            map = mat->maps[0];
            tex = textures[map.texture];
        }
        if (mat && mat->maps.size() >= 2 && mat->maps[1].texture < textures.size()) mask_tex = textures[mat->maps[1].texture];
        const std::array<float, 8>* uv = (q.uvs && !q.uvs->empty()) ? &(*q.uvs)[0] : nullptr;
        LytTexSrt srt;
        if (mat && !mat->srts.empty()) srt = mat->srts[0];
        float sx[4], sy[4];
        for (int k = 0; k < 4; ++k) {
            sx[k] = q.x[k] + W / 2.0f;
            sy[k] = H / 2.0f - q.y[k];
        }
        // Two triangles: TL TR BR, TL BR BL.
        const int tris[2][3] = {{0, 1, 3}, {0, 3, 2}};
        for (const auto& t : tris) {
            const int a = t[0], b = t[1], c = t[2];
            const float area = (sx[b] - sx[a]) * (sy[c] - sy[a]) - (sx[c] - sx[a]) * (sy[b] - sy[a]);
            if (std::fabs(area) < 1e-6f) continue;
            const int x0 = std::max(0, static_cast<int>(std::floor(std::min({sx[a], sx[b], sx[c]}))));
            const int x1 = std::min(W - 1, static_cast<int>(std::ceil(std::max({sx[a], sx[b], sx[c]}))));
            const int y0 = std::max(0, static_cast<int>(std::floor(std::min({sy[a], sy[b], sy[c]}))));
            const int y1 = std::min(H - 1, static_cast<int>(std::ceil(std::max({sy[a], sy[b], sy[c]}))));
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    const float px_ = x + 0.5f, py_ = y + 0.5f;
                    const float wa = ((sx[b] - px_) * (sy[c] - py_) - (sx[c] - px_) * (sy[b] - py_)) / area;
                    const float wb = ((sx[c] - px_) * (sy[a] - py_) - (sx[a] - px_) * (sy[c] - py_)) / area;
                    const float wc = 1.0f - wa - wb;
                    if (wa < 0 || wb < 0 || wc < 0) continue;
                    const auto lerp = [&](float va, float vb, float vc) { return va * wa + vb * wb + vc * wc; };
                    LytColor raster;
                    raster.r = static_cast<std::uint8_t>(lerp(q.color[a].r, q.color[b].r, q.color[c].r));
                    raster.g = static_cast<std::uint8_t>(lerp(q.color[a].g, q.color[b].g, q.color[c].g));
                    raster.b = static_cast<std::uint8_t>(lerp(q.color[a].b, q.color[b].b, q.color[c].b));
                    raster.a = static_cast<std::uint8_t>(lerp(q.color[a].a, q.color[b].a, q.color[c].a));
                    LytColor texel, mask;
                    const LytColor* tp = nullptr;
                    const LytColor* mp = nullptr;
                    if (tex && uv) {
                        float u = lerp((*uv)[2 * a], (*uv)[2 * b], (*uv)[2 * c]);
                        float v = lerp((*uv)[2 * a + 1], (*uv)[2 * b + 1], (*uv)[2 * c + 1]);
                        // The texture SRT turns and scales about the middle, then moves.
                        const float r = srt.rotate * 3.14159265f / 180.0f;
                        const float du = (u - 0.5f) * srt.sx, dv = (v - 0.5f) * srt.sy;
                        u = du * std::cos(r) - dv * std::sin(r) + 0.5f + srt.tx;
                        v = du * std::sin(r) + dv * std::cos(r) + 0.5f + srt.ty;
                        const int tx = wrap(static_cast<int>(std::floor(u * tex->w)), tex->w, map.wrap_s);
                        const int ty = wrap(static_cast<int>(std::floor(v * tex->h)), tex->h, map.wrap_t);
                        const std::uint8_t* s = &tex->rgba[(static_cast<std::size_t>(ty) * tex->w + tx) * 4];
                        texel = {s[0], s[1], s[2], s[3]};
                        tp = &texel;
                        if (mask_tex) {
                            const int mx = wrap(static_cast<int>(std::floor(u * mask_tex->w)), mask_tex->w, map.wrap_s);
                            const int my = wrap(static_cast<int>(std::floor(v * mask_tex->h)), mask_tex->h, map.wrap_t);
                            const std::uint8_t* ms = &mask_tex->rgba[(static_cast<std::size_t>(my) * mask_tex->w + mx) * 4];
                            mask = {ms[0], ms[1], ms[2], ms[3]};
                            mp = &mask;
                        }
                    }
                    const LytColor out = mat ? shade_texel(*mat, tp, raster, mp) : raster;
                    std::uint8_t* d = &px[(static_cast<std::size_t>(y) * W + x) * 4];
                    const float sa = out.a / 255.0f, da = d[3] / 255.0f;
                    const float oa = sa + da * (1 - sa);
                    if (oa <= 0) continue;
                    const std::uint8_t src[3] = {out.r, out.g, out.b};
                    for (int i = 0; i < 3; ++i)
                        d[i] = static_cast<std::uint8_t>((src[i] * sa + d[i] * da * (1 - sa)) / oa);
                    d[3] = static_cast<std::uint8_t>(oa * 255);
                }
            }
        }
    }
}

bool dump_archive(const std::vector<std::uint8_t>& arc, const std::string& kind, const std::string& out_dir) {
    std::string error;
    U8Archive u8;
    if (!U8Archive::parse(arc.data(), arc.size(), u8, error)) {
        std::cerr << kind << ": " << error << "\n";
        return false;
    }
    for (const std::string& f : u8.files()) std::cout << "  " << kind << f << "\n";
    const std::vector<std::string> layouts = u8.files_in("/arc/blyt", ".brlyt");
    if (layouts.empty()) {
        std::cerr << kind << ": no layout\n";
        return false;
    }
    const std::uint8_t* d = nullptr;
    std::size_t n = 0;
    u8.find(layouts[0], d, n);
    Layout lyt;
    if (!parse_brlyt(d, n, lyt, error)) {
        std::cerr << kind << ": " << error << "\n";
        return false;
    }
    std::cout << kind << ": " << lyt.width << "x" << lyt.height << ", " << lyt.panes.size() << " panes, "
              << lyt.materials.size() << " materials, " << lyt.textures.size() << " textures\n";
    if (std::getenv("BANNER_VERBOSE")) {
        for (std::size_t i = 0; i < lyt.panes.size(); ++i) {
            const LytPane& p = lyt.panes[i];
            std::cout << "  pane " << i << " " << p.name << " kind " << int(p.kind) << " parent " << p.parent << " flags "
                      << int(p.flags) << " alpha " << int(p.alpha) << " origin " << int(p.origin) << " t " << p.tx << ","
                      << p.ty << " s " << p.sx << "," << p.sy << " r " << p.rz << " size " << p.width << "x" << p.height
                      << " mat " << p.material << " vtx0 " << int(p.vertex[0].r) << "," << int(p.vertex[0].a) << "\n";
        }
        for (std::size_t i = 0; i < lyt.materials.size(); ++i) {
            const LytMaterial& m = lyt.materials[i];
            std::cout << "  mat " << i << " " << m.name << " black " << m.black[0] << "," << m.black[1] << "," << m.black[2] << ","
                      << m.black[3] << " white " << m.white[0] << "," << m.white[1] << "," << m.white[2] << "," << m.white[3]
                      << " maps " << m.maps.size() << " (tex " << (m.maps.empty() ? -1 : m.maps[0].texture) << ") tev "
                      << m.tev.size() << " chan " << m.has_channel << " matcol " << m.has_material_color << " "
                      << int(m.material_color.r) << "," << int(m.material_color.a) << " blend " << m.has_blend << "\n";
            for (const LytTexMap& t : m.maps) {
                std::cout << "    map tex " << t.texture << " wrap " << int(t.wrap_s) << "," << int(t.wrap_t) << " filter min "
                          << int(t.min_filter) << " mag " << int(t.mag_filter) << "\n";
            }
            for (const LytTexSrt& s : m.srts)
                std::cout << "    srt t " << s.tx << "," << s.ty << " r " << s.rotate << " s " << s.sx << "," << s.sy << "\n";
            for (const auto& s : m.tev) {
                std::cout << "    tev";
                for (std::uint8_t b : s) std::cout << " " << std::hex << int(b) << std::dec;
                std::cout << "\n";
            }
        }
    }
    std::map<std::string, Texture> loaded;
    const auto texture = [&](const std::string& name) -> Texture* {
        auto it = loaded.find(name);
        if (it != loaded.end()) return &it->second;
        Texture& t = loaded[name];
        const std::uint8_t* td = nullptr;
        std::size_t tn = 0;
        std::vector<TplImage> images;
        if (!u8.find("/arc/timg/" + name, td, tn) || !parse_tpl(td, tn, images, error) ||
            !tpl_to_rgba(td, tn, images[0], t.rgba, error)) {
            std::cerr << "  texture " << name << ": " << (error.empty() ? "missing" : error) << "\n";
            t = Texture{1, 1, {255, 0, 255, 255}};
        } else {
            t.w = images[0].width;
            t.h = images[0].height;
            std::cout << "  texture " << name << " " << t.w << "x" << t.h << " format " << images[0].format << "\n";
        }
        return &t;
    };
    // The animations: a start played once, then a loop (banners), or one
    // looping animation (icons).
    Animation start, loop;
    bool has_start = false, has_loop = false;
    for (const std::string& a : u8.files_in("/arc/anim", ".brlan")) {
        const std::uint8_t* ad = nullptr;
        std::size_t an = 0;
        u8.find(a, ad, an);
        Animation anim;
        if (!parse_brlan(ad, an, anim, error)) {
            std::cerr << "  " << a << ": " << error << "\n";
            continue;
        }
        std::cout << "  " << a << ": " << anim.frames << " frames" << (anim.loop ? ", loops" : "") << ", "
                  << anim.targets.size() << " targets\n";
        if (std::getenv("BANNER_TRACKS")) {
            for (const AnimTarget& tg : anim.targets) {
                for (const AnimTrack& tr : tg.tracks) {
                    std::cout << "    " << tg.name << (tg.material ? " (material)" : "") << " kind "
                              << int(tr.kind) << " index " << int(tr.index) << " target " << int(tr.target)
                              << (tr.step ? " step" : "") << ":";
                    for (const AnimKey& k : tr.keys) std::cout << " " << k.frame << "=" << k.value << "/" << k.slope;
                    std::cout << "\n";
                }
            }
        }
        const bool is_start = a.find("_Start") != std::string::npos || a.find("_start") != std::string::npos;
        if (is_start) {
            start = std::move(anim);
            has_start = true;
        } else if (!has_loop) {
            loop = std::move(anim);
            has_loop = true;
        }
    }
    const int start_frames = has_start ? start.frames : 0;
    const int loop_frames = has_loop ? loop.frames : 0;
    std::vector<int> frames;
    if (const char* f = std::getenv("BANNER_FRAMES")) {
        for (const char* s = f; *s;) {
            frames.push_back(std::atoi(s));
            while (*s && *s != ',') ++s;
            if (*s) ++s;
        }
    } else {
        frames = {0, start_frames, start_frames + loop_frames / 4, start_frames + loop_frames / 2,
                  start_frames + 3 * loop_frames / 4};
    }
    // The Wii Menu shows an icon's middle 128x96.
    const bool icon = kind == "icon";
    const int W = icon ? 128 : static_cast<int>(lyt.width), H = icon ? 96 : static_cast<int>(lyt.height);
    for (const int frame : frames) {
        Layout at = lyt;
        if (has_start) apply_animation(start, static_cast<float>(std::min(frame, start_frames)), at);
        if (has_loop && frame >= start_frames)
            apply_animation(loop, static_cast<float>(loop_frames ? (frame - start_frames) % loop_frames : 0), at);
        const char* lang = std::getenv("BANNER_LANGUAGE");
        show_language(at, lang ? lang : "ENG");
        std::vector<Texture*> textures;
        for (const std::string& name : at.textures) textures.push_back(texture(name));
        std::vector<std::uint8_t> px, png;
        rasterise(at, textures, W, H, px);
        encode_png_rgba(px.data(), W, H, png);
        write_file(out_dir + "/" + kind + "_" + std::to_string(frame) + ".png", png);
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: bannerdump <game.rvz | opening.bnr> <out dir>\n";
        return 2;
    }
    const std::string in = argv[1], out_dir = argv[2];
    std::vector<std::uint8_t> bnr;
    std::string error;
    if (in.size() > 4 && in.substr(in.size() - 4) == ".rvz") {
        if (!opening_from_rvz(in, bnr, error)) {
            std::cerr << error << "\n";
            return 1;
        }
        write_file(out_dir + "/opening.bnr", bnr);
    } else {
        StreamSource f(in);
        bnr.resize(static_cast<std::size_t>(f.size()));
        if (!f.ok() || !f.read(0, bnr.data(), bnr.size())) {
            std::cerr << "cannot read " << in << "\n";
            return 1;
        }
    }
    OpeningBanner b;
    if (!parse_opening_bnr(bnr.data(), bnr.size(), b, error)) {
        std::cerr << error << "\n";
        return 1;
    }
    std::cout << "name (English): " << b.names[kBnrEnglish] << "\n";
    const bool icon = dump_archive(b.icon, "icon", out_dir);
    const bool banner = dump_archive(b.banner, "banner", out_dir);
    return icon && banner ? 0 : 1;
}
