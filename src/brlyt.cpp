// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/brlyt.hpp"

#include <cmath>
#include <cstring>

namespace riftwii {
namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}
std::uint16_t be16(const std::uint8_t* p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }
float bef(const std::uint8_t* p) {
    const std::uint32_t v = be32(p);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

std::string fixed_name(const std::uint8_t* p, std::size_t n) {
    std::size_t len = 0;
    while (len < n && p[len]) ++len;
    return std::string(reinterpret_cast<const char*>(p), len);
}

LytColor rgba(const std::uint8_t* p) { return {p[0], p[1], p[2], p[3]}; }

// txl1 and fnl1: a count, then {offset, 0} pairs counted from the first pair.
bool name_list(const std::uint8_t* s, std::size_t len, std::vector<std::string>& out, std::string& error) {
    if (len < 12) {
        error = "name list too short";
        return false;
    }
    const std::uint16_t n = be16(s + 8);
    const std::uint8_t* table = s + 12;
    const std::size_t room = len - 12;
    if (static_cast<std::size_t>(n) * 8 > room) {
        error = "name list too short";
        return false;
    }
    for (std::uint16_t i = 0; i < n; ++i) {
        const std::uint32_t off = be32(table + i * 8);
        if (off >= room) {
            error = "name points outside its list";
            return false;
        }
        out.push_back(fixed_name(table + off, room - off));
    }
    return true;
}

bool parse_material(const std::uint8_t* m, std::size_t room, LytMaterial& out, std::string& error) {
    if (room < 0x40) {
        error = "material too short";
        return false;
    }
    LytMaterial mat;
    mat.name = fixed_name(m, 20);
    for (int i = 0; i < 4; ++i) {
        mat.black[i] = static_cast<std::int16_t>(be16(m + 0x14 + 2 * i));
        mat.white[i] = static_cast<std::int16_t>(be16(m + 0x1C + 2 * i));
        mat.color3[i] = static_cast<std::int16_t>(be16(m + 0x24 + 2 * i));
        mat.tev_k[i] = rgba(m + 0x2C + 4 * i);
    }
    const std::uint32_t f = be32(m + 0x3C);
    const unsigned maps = f & 15, srts = (f >> 4) & 15, gens = (f >> 8) & 15;
    const bool swap = (f >> 12) & 1;
    const unsigned ind_mtx = (f >> 13) & 3, ind_stages = (f >> 15) & 7, stages = (f >> 18) & 31;
    const bool alpha_cmp = (f >> 23) & 1, blend = (f >> 24) & 1, channel = (f >> 25) & 1, mat_color = (f >> 27) & 1;
    std::size_t p = 0x40;
    const auto need = [&](std::size_t n) {
        if (n > room || p > room - n) {
            error = "material " + mat.name + " runs past its section";
            return false;
        }
        return true;
    };
    if (!need(maps * 4)) return false;
    for (unsigned i = 0; i < maps; ++i, p += 4) {
        LytTexMap t;
        t.texture = be16(m + p);
        const std::uint16_t bits = be16(m + p + 2);
        // AAAB BBCC DDDD EEFF
        t.min_filter = static_cast<std::uint8_t>((bits >> 10) & 7);
        t.wrap_s = static_cast<std::uint8_t>((bits >> 8) & 3);
        t.mag_filter = static_cast<std::uint8_t>((bits >> 2) & 3);
        t.wrap_t = static_cast<std::uint8_t>(bits & 3);
        mat.maps.push_back(t);
    }
    if (!need(srts * 20)) return false;
    for (unsigned i = 0; i < srts; ++i, p += 20)
        mat.srts.push_back({bef(m + p), bef(m + p + 4), bef(m + p + 8), bef(m + p + 12), bef(m + p + 16)});
    if (!need(gens * 4)) return false;
    for (unsigned i = 0; i < gens; ++i, p += 4) mat.texgens.push_back({m[p], m[p + 1], m[p + 2], m[p + 3]});
    if (channel) {
        if (!need(4)) return false;
        mat.has_channel = true;
        mat.color_source = m[p];
        mat.alpha_source = m[p + 1];
        p += 4;
    }
    if (mat_color) {
        if (!need(4)) return false;
        mat.has_material_color = true;
        mat.material_color = rgba(m + p);
        p += 4;
    }
    if (swap) {
        if (!need(4)) return false;
        p += 4;
    }
    if (!need(ind_mtx * 20 + ind_stages * 4)) return false;
    p += ind_mtx * 20 + ind_stages * 4;
    if (!need(stages * 16)) return false;
    for (unsigned i = 0; i < stages; ++i, p += 16) {
        std::array<std::uint8_t, 16> s;
        std::memcpy(s.data(), m + p, 16);
        mat.tev.push_back(s);
    }
    if (alpha_cmp) {
        if (!need(4)) return false;
        mat.has_alpha_compare = true;
        std::memcpy(mat.alpha_compare.data(), m + p, 4);
        p += 4;
    }
    if (blend) {
        if (!need(4)) return false;
        mat.has_blend = true;
        std::memcpy(mat.blend.data(), m + p, 4);
        p += 4;
    }
    out = std::move(mat);
    return true;
}

// The base pane, 0x44 bytes from a pane section's +8.
void parse_pane(const std::uint8_t* s, LytPane& p) {
    const std::uint8_t* b = s + 8;
    p.flags = b[0];
    p.origin = b[1] < 9 ? b[1] : 4;
    p.alpha = b[2];
    p.name = fixed_name(b + 4, 16);
    p.tx = bef(b + 0x1C);
    p.ty = bef(b + 0x20);
    p.tz = bef(b + 0x24);
    p.rx = bef(b + 0x28);
    p.ry = bef(b + 0x2C);
    p.rz = bef(b + 0x30);
    p.sx = bef(b + 0x34);
    p.sy = bef(b + 0x38);
    p.width = bef(b + 0x3C);
    p.height = bef(b + 0x40);
}

// Vertex colours, material and UV sets at `c` (a picture's +0x4C).
bool parse_content(const std::uint8_t* c, std::size_t room, LytPane& p, std::string& error) {
    if (room < 0x14) {
        error = "pane " + p.name + " too short";
        return false;
    }
    for (int i = 0; i < 4; ++i) p.vertex[i] = rgba(c + 4 * i);
    p.material = be16(c + 0x10);
    const std::uint8_t n = c[0x12];
    if (static_cast<std::size_t>(n) * 32 > room - 0x14) {
        error = "pane " + p.name + " UV sets run past it";
        return false;
    }
    for (int i = 0; i < n; ++i) {
        std::array<float, 8> uv;
        for (int k = 0; k < 8; ++k) uv[k] = bef(c + 0x14 + i * 32 + k * 4);
        p.uvs.push_back(uv);
    }
    return true;
}

struct M34 {
    float m[3][4];
};

M34 identity() {
    M34 r{};
    r.m[0][0] = r.m[1][1] = r.m[2][2] = 1;
    return r;
}

M34 mul(const M34& a, const M34& b) {
    M34 r{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) {
            float v = j == 3 ? a.m[i][3] : 0;
            for (int k = 0; k < 3; ++k) v += a.m[i][k] * b.m[k][j];
            r.m[i][j] = v;
        }
    }
    return r;
}

M34 rotation(int axis, float degrees) {
    const float r = degrees * 3.14159265358979f / 180.0f, c = std::cos(r), s = std::sin(r);
    M34 m = identity();
    const int a = (axis + 1) % 3, b = (axis + 2) % 3;
    m.m[a][a] = c;
    m.m[a][b] = -s;
    m.m[b][a] = s;
    m.m[b][b] = c;
    return m;
}

// The pane's own move, turn and scale.
M34 local(const LytPane& p) {
    M34 t = identity();
    t.m[0][3] = p.tx;
    t.m[1][3] = p.ty;
    t.m[2][3] = p.tz;
    M34 s = identity();
    s.m[0][0] = p.sx;
    s.m[1][1] = p.sy;
    if (p.rx == 0 && p.ry == 0 && p.rz == 0) {
        // Most panes are not turned: move and scale only.
        t.m[0][0] = p.sx;
        t.m[1][1] = p.sy;
        return t;
    }
    M34 r = identity();
    if (p.rx != 0) r = mul(r, rotation(0, p.rx));
    if (p.ry != 0) r = mul(r, rotation(1, p.ry));
    if (p.rz != 0) r = mul(r, rotation(2, p.rz));
    return mul(mul(t, r), s);
}

std::uint8_t clamp8(int v) { return static_cast<std::uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v); }

}  // namespace

int Layout::find_pane(const std::string& name) const {
    for (std::size_t i = 0; i < panes.size(); ++i)
        if (panes[i].name == name) return static_cast<int>(i);
    return -1;
}

bool parse_brlyt(const std::uint8_t* data, std::size_t size, Layout& out, std::string& error) {
    if (!data || size < 0x10 || std::memcmp(data, "RLYT", 4) != 0 || be16(data + 4) != 0xFEFF) {
        error = "not a big-endian BRLYT";
        return false;
    }
    Layout lyt;
    std::size_t pos = be16(data + 0xC);
    int last = -1;                // the pane read last: a pas1 opens its children
    std::vector<int> parents;     // the open pas1s
    while (pos + 8 <= size) {
        const std::uint8_t* s = data + pos;
        const std::uint32_t len = be32(s + 4);
        if (len < 8 || len > size - pos) {
            error = "section runs past the file";
            return false;
        }
        const std::string magic(reinterpret_cast<const char*>(s), 4);
        if (magic == "lyt1") {
            if (len < 0x14) {
                error = "lyt1 too short";
                return false;
            }
            lyt.centered = s[8] != 0;
            lyt.width = bef(s + 0xC);
            lyt.height = bef(s + 0x10);
        } else if (magic == "txl1") {
            if (!name_list(s, len, lyt.textures, error)) return false;
        } else if (magic == "fnl1") {
            if (!name_list(s, len, lyt.fonts, error)) return false;
        } else if (magic == "mat1") {
            if (len < 12) {
                error = "mat1 too short";
                return false;
            }
            const std::uint16_t n = be16(s + 8);
            if (12 + static_cast<std::size_t>(n) * 4 > len) {
                error = "mat1 too short";
                return false;
            }
            for (std::uint16_t i = 0; i < n; ++i) {
                const std::uint32_t off = be32(s + 12 + i * 4);
                if (off >= len) {
                    error = "material lies outside mat1";
                    return false;
                }
                LytMaterial m;
                if (!parse_material(s + off, len - off, m, error)) return false;
                lyt.materials.push_back(std::move(m));
            }
        } else if (magic == "pan1" || magic == "pic1" || magic == "txt1" || magic == "wnd1" || magic == "bnd1") {
            if (len < 0x4C) {
                error = magic + " too short";
                return false;
            }
            LytPane p;
            parse_pane(s, p);
            if (magic == "pic1") {
                p.kind = PaneKind::Picture;
                if (!parse_content(s + 0x4C, len - 0x4C, p, error)) return false;
            } else if (magic == "wnd1") {
                p.kind = PaneKind::Window;
                if (len >= 0x68) {
                    const std::uint32_t c = be32(s + 0x60);
                    if (c < len && !parse_content(s + c, len - c, p, error)) return false;
                }
            } else if (magic == "txt1") {
                p.kind = PaneKind::Text;
            } else if (magic == "bnd1") {
                p.kind = PaneKind::Bound;
            }
            if (p.material >= static_cast<int>(lyt.materials.size())) p.material = -1;
            p.parent = parents.empty() ? -1 : parents.back();
            last = static_cast<int>(lyt.panes.size());
            if (p.parent >= 0) lyt.panes[p.parent].children.push_back(last);
            lyt.panes.push_back(std::move(p));
        } else if (magic == "pas1") {
            if (last < 0) {
                error = "pas1 before any pane";
                return false;
            }
            parents.push_back(last);
        } else if (magic == "pae1") {
            if (parents.empty()) {
                error = "pae1 without pas1";
                return false;
            }
            last = parents.back();
            parents.pop_back();
        } else if (magic == "grp1") {
            if (len < 0x1C) {
                error = "grp1 too short";
                return false;
            }
            LytGroup g;
            g.name = fixed_name(s + 8, 16);
            const std::uint16_t n = be16(s + 0x18);
            if (0x1C + static_cast<std::size_t>(n) * 16 > len) {
                error = "grp1 runs past it";
                return false;
            }
            for (std::uint16_t i = 0; i < n; ++i) g.panes.push_back(fixed_name(s + 0x1C + i * 16, 16));
            lyt.groups.push_back(std::move(g));
        }
        // grs1, gre1, usd1 and anything newer: not needed to draw.
        pos += len;
    }
    if (lyt.panes.empty()) {
        error = "the layout has no panes";
        return false;
    }
    out = std::move(lyt);
    return true;
}

LanguagePanes language_panes(const Layout& layout, const std::string& language) {
    static const char* const kCodes[] = {"JPN", "ENG", "GER", "FRA", "SPA", "ITA", "NED", "CHN", "KOR", "USA", "EUR"};
    LanguagePanes out;
    std::vector<const LytGroup*> langs;
    for (const LytGroup& g : layout.groups)
        for (const char* c : kCodes)
            if (g.name == c) langs.push_back(&g);
    if (langs.empty()) return out;
    const LytGroup* pick = nullptr;
    for (const std::string& want : {language, std::string("ENG")})
        for (const LytGroup* g : langs)
            if (!pick && g->name == want) pick = g;
    if (!pick) pick = langs.front();
    for (const LytGroup* g : langs) {
        for (const std::string& name : g->panes) {
            const int i = layout.find_pane(name);
            if (i >= 0) (g == pick ? out.show : out.hide).push_back(i);
        }
    }
    return out;
}

void apply_language(Layout& layout, const LanguagePanes& panes) {
    // Hidden first: a pane in two groups is shown if its language's has it.
    for (const int i : panes.hide) layout.panes[i].flags = static_cast<std::uint8_t>(layout.panes[i].flags & ~1);
    for (const int i : panes.show) layout.panes[i].flags = static_cast<std::uint8_t>(layout.panes[i].flags | 1);
}

void show_language(Layout& layout, const std::string& language) {
    apply_language(layout, language_panes(layout, language));
}

std::vector<LytQuad> layout_quads(const Layout& layout) {
    std::vector<LytQuad> quads;
    LytScratch scratch;
    layout_quads(layout, quads, scratch);
    return quads;
}

void layout_quads(const Layout& layout, std::vector<LytQuad>& quads, LytScratch& scratch) {
    static_assert(sizeof(M34) == sizeof(std::array<float, 12>), "M34 is twelve floats");
    quads.clear();
    scratch.world.resize(layout.panes.size());
    scratch.alpha.assign(layout.panes.size(), 1.0f);
    scratch.shown.assign(layout.panes.size(), 1);
    M34* world = reinterpret_cast<M34*>(scratch.world.data());
    float* alpha = scratch.alpha.data();
    std::uint8_t* shown = scratch.shown.data();
    // Parents come before their children in file order.
    for (std::size_t i = 0; i < layout.panes.size(); ++i) {
        const LytPane& p = layout.panes[i];
        float a = p.alpha / 255.0f;
        bool visible = p.visible();
        M34 parent = identity();
        if (p.parent >= 0) {
            const LytPane& up = layout.panes[p.parent];
            parent = world[p.parent];
            visible = visible && shown[p.parent];
            if (up.flags & 2) a *= alpha[p.parent];
        }
        world[i] = p.parent >= 0 ? mul(parent, local(p)) : local(p);
        alpha[i] = a;
        shown[i] = visible ? 1 : 0;
        if (!visible || (p.kind != PaneKind::Picture && p.kind != PaneKind::Window) || a <= 0.0f) continue;
        const int ox = p.origin % 3, oy = p.origin / 3;
        const float left = -p.width * ox / 2.0f, top = p.height * oy / 2.0f;
        const float cx[4] = {left, left + p.width, left, left + p.width};
        const float cy[4] = {top, top, top - p.height, top - p.height};
        LytQuad q;
        q.pane = static_cast<int>(i);
        q.material = p.material;
        q.uvs = &p.uvs;
        const M34& w = world[i];
        for (int k = 0; k < 4; ++k) {
            q.x[k] = w.m[0][0] * cx[k] + w.m[0][1] * cy[k] + w.m[0][3];
            q.y[k] = w.m[1][0] * cx[k] + w.m[1][1] * cy[k] + w.m[1][3];
            q.color[k] = p.vertex[k];
            q.color[k].a = clamp8(static_cast<int>(std::lround(p.vertex[k].a * a)));
        }
        quads.push_back(q);
    }
}

LytColor shade_texel(const LytMaterial& m, const LytColor* texel, LytColor raster, const LytColor* mask) {
    if (m.has_channel) {
        const LytColor mc = m.has_material_color ? m.material_color : LytColor{};
        if (m.color_source == 0) {
            raster.r = mc.r;
            raster.g = mc.g;
            raster.b = mc.b;
        }
        if (m.alpha_source == 0) raster.a = mc.a;
    }
    int c[4];
    for (int i = 0; i < 4; ++i) {
        const int lo = m.black[i] < 0 ? 0 : m.black[i] > 255 ? 255 : m.black[i];
        const int hi = m.white[i] < 0 ? 0 : m.white[i] > 255 ? 255 : m.white[i];
        if (texel) {
            const int t = i == 0 ? texel->r : i == 1 ? texel->g : i == 2 ? texel->b : texel->a;
            c[i] = lo + (hi - lo) * t / 255;
        } else {
            c[i] = hi;
        }
    }
    if (mask) c[3] = c[3] * mask->a / 255;
    return {clamp8(c[0] * raster.r / 255), clamp8(c[1] * raster.g / 255), clamp8(c[2] * raster.b / 255),
            clamp8(c[3] * raster.a / 255)};
}

}  // namespace riftwii
