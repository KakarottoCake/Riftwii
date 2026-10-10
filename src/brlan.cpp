// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/brlan.hpp"

#include <cmath>
#include <algorithm>
#include <cstdio>
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

bool kind_of(const std::uint8_t* tag, AnimKind& kind) {
    static const struct {
        const char* magic;
        AnimKind kind;
    } kKinds[] = {{"RLPA", AnimKind::PaneSrt},     {"RLTS", AnimKind::TextureSrt},    {"RLVI", AnimKind::Visibility},
                  {"RLVC", AnimKind::VertexColor}, {"RLMC", AnimKind::MaterialColor}, {"RLTP", AnimKind::TexturePattern}};
    for (const auto& k : kKinds) {
        if (std::memcmp(tag, k.magic, 4) == 0) {
            kind = k.kind;
            return true;
        }
    }
    return false;
}

std::uint8_t to8(float v) {
    const long r = std::lround(v);
    return static_cast<std::uint8_t>(r < 0 ? 0 : r > 255 ? 255 : r);
}

std::int16_t to16(float v) {
    const long r = std::lround(v);
    return static_cast<std::int16_t>(r < -32768 ? -32768 : r > 32767 ? 32767 : r);
}

std::uint8_t* channel(LytColor& c, int i) { return i == 0 ? &c.r : i == 1 ? &c.g : i == 2 ? &c.b : &c.a; }

}  // namespace

bool parse_brlan(const std::uint8_t* data, std::size_t size, Animation& out, std::string& error) {
    if (!data || size < 0x10 || std::memcmp(data, "RLAN", 4) != 0 || be16(data + 4) != 0xFEFF) {
        error = "not a big-endian BRLAN";
        return false;
    }
    std::size_t pos = be16(data + 0xC);
    while (pos + 8 <= size) {
        const std::uint8_t* s = data + pos;
        const std::uint32_t len = be32(s + 4);
        if (len < 8 || len > size - pos) {
            error = "BRLAN section runs past the file";
            return false;
        }
        if (std::memcmp(s, "pai1", 4) != 0) {
            pos += len;
            continue;
        }
        if (len < 0x14) {
            error = "pai1 too short";
            return false;
        }
        Animation a;
        a.frames = be16(s + 8);
        a.loop = s[0xA] != 0;
        const std::uint16_t ntex = be16(s + 0xC), nanim = be16(s + 0xE);
        const std::uint32_t table = be32(s + 0x10);
        if (0x14 + static_cast<std::size_t>(ntex) * 4 > len || table > len || static_cast<std::size_t>(nanim) * 4 > len - table) {
            error = "pai1 tables run past it";
            return false;
        }
        for (std::uint16_t i = 0; i < ntex; ++i) {
            const std::uint32_t off = be32(s + 0x14 + i * 4);
            if (0x14 + static_cast<std::size_t>(off) >= len) {
                error = "pai1 texture name outside it";
                return false;
            }
            a.textures.push_back(fixed_name(s + 0x14 + off, len - 0x14 - off));
        }
        for (std::uint16_t i = 0; i < nanim; ++i) {
            const std::uint32_t ao = be32(s + table + i * 4);
            if (ao > len || len - ao < 0x18) {
                error = "animation outside pai1";
                return false;
            }
            const std::uint8_t* an = s + ao;
            const std::size_t aroom = len - ao;
            AnimTarget t;
            t.name = fixed_name(an, 20);
            const std::uint8_t ntags = an[0x14];
            t.material = an[0x15] == 1;
            if (0x18 + static_cast<std::size_t>(ntags) * 4 > aroom) {
                error = "animation " + t.name + " runs past pai1";
                return false;
            }
            for (std::uint8_t g = 0; g < ntags; ++g) {
                const std::uint32_t to = be32(an + 0x18 + g * 4);
                if (to > aroom || aroom - to < 8) {
                    error = "animation " + t.name + " tag outside it";
                    return false;
                }
                const std::uint8_t* tag = an + to;
                const std::size_t troom = aroom - to;
                AnimKind kind;
                const std::uint8_t nentries = tag[4];
                if (!kind_of(tag, kind)) {  // a newer kind: nothing RiftWii draws
                    const std::string name(reinterpret_cast<const char*>(tag), 4);
                    if (std::find(a.skipped.begin(), a.skipped.end(), name) == a.skipped.end()) a.skipped.push_back(name);
                    continue;
                }
                if (8 + static_cast<std::size_t>(nentries) * 4 > troom) {
                    error = "animation " + t.name + " tag runs past it";
                    return false;
                }
                for (std::uint8_t e = 0; e < nentries; ++e) {
                    const std::uint32_t eo = be32(tag + 8 + e * 4);
                    if (eo > troom || troom - eo < 0xC) {
                        error = "animation " + t.name + " track outside it";
                        return false;
                    }
                    const std::uint8_t* en = tag + eo;
                    AnimTrack tr;
                    tr.kind = kind;
                    tr.index = en[0];
                    tr.target = en[1];
                    tr.step = en[2] == 1;
                    const std::uint16_t nkeys = be16(en + 4);
                    const std::uint32_t ko = be32(en + 8);
                    const std::size_t key_size = tr.step ? 8 : 12;
                    if (ko > troom - eo || static_cast<std::size_t>(nkeys) * key_size > troom - eo - ko) {
                        error = "animation " + t.name + " keys run past it";
                        return false;
                    }
                    for (std::uint16_t k = 0; k < nkeys; ++k) {
                        const std::uint8_t* key = en + ko + k * key_size;
                        AnimKey kv;
                        kv.frame = bef(key);
                        if (tr.step) kv.value = be16(key + 4);
                        else {
                            kv.value = bef(key + 4);
                            kv.slope = bef(key + 8);
                        }
                        tr.keys.push_back(kv);
                    }
                    if (!tr.keys.empty()) t.tracks.push_back(std::move(tr));
                }
            }
            a.targets.push_back(std::move(t));
        }
        out = std::move(a);
        return true;
    }
    error = "BRLAN has no pai1";
    return false;
}

float track_value(const AnimTrack& track, float frame) {
    const std::vector<AnimKey>& k = track.keys;
    if (k.empty()) return 0;
    if (frame <= k.front().frame) return k.front().value;
    if (frame >= k.back().frame) return k.back().value;
    std::size_t i = 0;
    while (i + 1 < k.size() && k[i + 1].frame <= frame) ++i;
    if (track.step) return k[i].value;
    const AnimKey& a = k[i];
    const AnimKey& b = k[i + 1];
    const float span = b.frame - a.frame;
    if (span <= 0) return b.value;
    const float t = (frame - a.frame) / span, t2 = t * t, t3 = t2 * t;
    const float h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t, h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
    return h00 * a.value + h10 * span * a.slope + h01 * b.value + h11 * span * b.slope;
}

AnimBinding bind_animation(const Animation& anim, const Layout& layout) {
    AnimBinding b;
    for (const AnimTarget& t : anim.targets) {
        // A material's tracks name the material, or a pane whose material it is.
        const int pi = t.material ? -1 : layout.find_pane(t.name);
        int mi = -1;
        if (t.material) {
            for (std::size_t i = 0; i < layout.materials.size(); ++i)
                if (layout.materials[i].name == t.name) mi = static_cast<int>(i);
        } else if (pi >= 0) {
            mi = layout.panes[pi].material;
        }
        b.pane.push_back(pi);
        b.material.push_back(mi);
    }
    return b;
}

std::string describe_banner(const Layout& layout, const Animation* start, const Animation* loop) {
    // snprintf into one buffer: string concatenation here cost the Wii DOL 12 KB.
    unsigned tev = 0, max_stages = 0, indirect = 0, blend = 0, alpha = 0;
    for (const LytMaterial& m : layout.materials) {
        if (!m.tev.empty()) ++tev;
        max_stages = std::max(max_stages, static_cast<unsigned>(m.tev.size()));
        if (m.ind_stages) ++indirect;
        if (m.has_blend) ++blend;
        if (m.has_alpha_compare) ++alpha;
    }
    char buf[640];
    std::size_t n = 0;
    auto add = [&](const char* fmt, auto... args) {
        if (n < sizeof buf) n += static_cast<std::size_t>(std::snprintf(buf + n, sizeof buf - n, fmt, args...));
    };
    add("%dx%d, %u panes, %u materials (%u with TEV stages, up to %u; %u with indirect stages (their warp skipped); "
        "%u blend; %u alpha compare), %u textures",
        static_cast<int>(layout.width), static_cast<int>(layout.height), static_cast<unsigned>(layout.panes.size()),
        static_cast<unsigned>(layout.materials.size()), tev, max_stages, indirect, blend, alpha,
        static_cast<unsigned>(layout.textures.size()));
    static const char* const kNames[] = {"pane", "texture SRT", "visibility", "vertex colour", "material colour",
                                         "texture pattern"};
    std::vector<std::string> skipped;
    const char* const labels[] = {"start", "loop"};
    const Animation* const anims[] = {start, loop};
    for (int a = 0; a < 2; ++a) {
        const Animation* anim = anims[a];
        if (!anim) {
            add("; no %s", labels[a]);
            continue;
        }
        unsigned count[6] = {};
        for (const AnimTarget& t : anim->targets)
            for (const AnimTrack& tr : t.tracks) ++count[static_cast<std::size_t>(tr.kind)];
        add("; %s %u frames (", labels[a], static_cast<unsigned>(anim->frames));
        const char* sep = "";
        for (std::size_t k = 0; k < 6; ++k) {
            if (!count[k]) continue;
            add("%s%s %u", sep, kNames[k], count[k]);
            sep = ", ";
        }
        add(")");
        for (const std::string& k : anim->skipped)
            if (std::find(skipped.begin(), skipped.end(), k) == skipped.end()) skipped.push_back(k);
    }
    if (!skipped.empty()) add("; skipped kinds:");
    for (const std::string& k : skipped) add(" %s", k.c_str());
    return std::string(buf, std::min(n, sizeof buf - 1));
}

void apply_animation(const Animation& anim, float frame, Layout& layout) {
    apply_animation(anim, frame, layout, bind_animation(anim, layout));
}

void apply_animation(const Animation& anim, float frame, Layout& layout, const AnimBinding& binding) {
    for (std::size_t ti = 0; ti < anim.targets.size() && ti < binding.pane.size(); ++ti) {
        const AnimTarget& t = anim.targets[ti];
        const int pi = binding.pane[ti];
        const int mi = binding.material[ti];
        LytMaterial* m = mi >= 0 && static_cast<std::size_t>(mi) < layout.materials.size() ? &layout.materials[mi] : nullptr;
        if (m) {
            for (const AnimTrack& tr : t.tracks) {
                const float v = track_value(tr, frame);
                if (tr.kind == AnimKind::MaterialColor) {
                    const int c = tr.target & 3;
                    if (tr.target < 4) {
                        m->has_material_color = true;
                        *channel(m->material_color, c) = to8(v);
                    } else if (tr.target < 8) {
                        m->black[c] = to16(v);
                    } else if (tr.target < 12) {
                        m->white[c] = to16(v);
                    } else if (tr.target < 16) {
                        m->color3[c] = to16(v);
                    } else if (tr.target < 32) {
                        *channel(m->tev_k[(tr.target - 16) / 4], c) = to8(v);
                    }
                } else if (tr.kind == AnimKind::TextureSrt) {
                    if (tr.index >= m->srts.size()) continue;
                    LytTexSrt& s = m->srts[tr.index];
                    float* f[5] = {&s.tx, &s.ty, &s.rotate, &s.sx, &s.sy};
                    if (tr.target < 5) *f[tr.target] = v;
                } else if (tr.kind == AnimKind::TexturePattern) {
                    if (tr.index >= m->maps.size()) continue;
                    // NaN, negative or past the list: no texture (casting
                    // those to an index is undefined).
                    if (!(v >= 0.0f) || v >= static_cast<float>(anim.textures.size())) continue;
                    const std::size_t pick = static_cast<std::size_t>(v);
                    const std::string& name = anim.textures[pick];
                    std::size_t found = layout.textures.size();
                    for (std::size_t i = 0; i < layout.textures.size(); ++i)
                        if (layout.textures[i] == name) found = i;
                    if (found == layout.textures.size()) layout.textures.push_back(name);
                    m->maps[tr.index].texture = static_cast<std::uint16_t>(found);
                }
            }
        }
        if (pi < 0 || static_cast<std::size_t>(pi) >= layout.panes.size()) continue;
        LytPane& p = layout.panes[pi];
        for (const AnimTrack& tr : t.tracks) {
            const float v = track_value(tr, frame);
            switch (tr.kind) {
                case AnimKind::PaneSrt: {
                    float* f[10] = {&p.tx, &p.ty, &p.tz, &p.rx, &p.ry, &p.rz, &p.sx, &p.sy, &p.width, &p.height};
                    if (tr.target < 10) *f[tr.target] = v;
                    break;
                }
                case AnimKind::Visibility:
                    p.flags = static_cast<std::uint8_t>(v != 0 ? (p.flags | 1) : (p.flags & ~1));
                    break;
                case AnimKind::VertexColor:
                    if (tr.target < 16) *channel(p.vertex[tr.target / 4], tr.target & 3) = to8(v);
                    else if (tr.target == 16) p.alpha = to8(v);
                    break;
                default:
                    break;
            }
        }
    }
}

void reset_animated(const Layout& base, Layout& work) {
    for (std::size_t i = 0; i < work.panes.size() && i < base.panes.size(); ++i) {
        const LytPane& b = base.panes[i];
        LytPane& w = work.panes[i];
        w.flags = b.flags;
        w.alpha = b.alpha;
        w.tx = b.tx;
        w.ty = b.ty;
        w.tz = b.tz;
        w.rx = b.rx;
        w.ry = b.ry;
        w.rz = b.rz;
        w.sx = b.sx;
        w.sy = b.sy;
        w.width = b.width;
        w.height = b.height;
        for (int k = 0; k < 4; ++k) w.vertex[k] = b.vertex[k];
    }
    for (std::size_t i = 0; i < work.materials.size() && i < base.materials.size(); ++i) {
        const LytMaterial& b = base.materials[i];
        LytMaterial& w = work.materials[i];
        for (int k = 0; k < 4; ++k) {
            w.black[k] = b.black[k];
            w.white[k] = b.white[k];
            w.color3[k] = b.color3[k];
            w.tev_k[k] = b.tev_k[k];
        }
        w.has_material_color = b.has_material_color;
        w.material_color = b.material_color;
        for (std::size_t k = 0; k < w.srts.size() && k < b.srts.size(); ++k) w.srts[k] = b.srts[k];
        for (std::size_t k = 0; k < w.maps.size() && k < b.maps.size(); ++k) w.maps[k].texture = b.maps[k].texture;
    }
}

}  // namespace riftwii
