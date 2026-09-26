#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds the RiftWii channel (docs/CHANNEL.md) from our own art: the
Wii Menu banner, icon and sound (content 0), the forwarder (content 1),
and the TMD and ticket RiftWii installs them with.

    make_channel.py logo <icon.png> <logo.rgba>
        the forwarder's logo, raw RGBA (channel/forwarder/data)
    make_channel.py build <forwarder.dol> <icon.png> <out.bin> [preview dir]
        the package wii/channel.cpp installs, and beside it
        riftwii_channel_info.h (its title ID and version, for the menu)

Nothing here is encrypted or signed: the console does that when RiftWii
installs the channel. The formats (U8, IMD5, LZ77, TPL, BRLYT, BRLAN,
BNS, IMET, TMD, ticket) are written from their public descriptions and
checked against what a retail disc's banner holds; no Nintendo data is
used. Standard library only.
"""
import hashlib
import math
import os
import struct
import sys
import zlib

TITLE_ID = 0x0001000152465457  # 00010001-RFTW
TITLE_VERSION = 1
IOS = 58
CHANNEL_NAME = "RiftWii"


# ---------------------------------------------------------------- PNG ----
def read_png(path):
    """RGBA8 rows of an 8-bit RGB or RGBA, non-interlaced PNG."""
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, idat, width = 8, b"", 0
    while pos < len(data):
        length, kind = struct.unpack_from(">I4s", data, pos)
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", body)
            assert depth == 8 and color in (2, 6) and interlace == 0, "want 8-bit RGB(A)"
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    channels = 4 if color == 6 else 3
    raw = zlib.decompress(idat)
    stride = width * channels
    rows, prev = [], bytearray(stride)
    for y in range(height):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = prev[i]
            c = prev[i - channels] if i >= channels else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        if channels == 3:
            line = bytearray(b for i in range(width) for b in (*line[i * 3:i * 3 + 3], 255))
        rows.append(bytes(line))
    return width, height, rows


# ------------------------------------------------------------ textures ----
class Image:
    def __init__(self, width, height, pixel):
        self.width, self.height = width, height
        self.pixels = [pixel(x, y) for y in range(height) for x in range(width)]

    def at(self, x, y):
        return self.pixels[y * self.width + x]


def png_image(path):
    w, h, rows = read_png(path)
    return Image(w, h, lambda x, y: tuple(rows[y][x * 4:x * 4 + 4]))


def glow_image(size):
    """White with a soft round alpha: lights and the sweeping shine."""
    def pixel(x, y):
        dx = (x + 0.5) / size * 2 - 1
        dy = (y + 0.5) / size * 2 - 1
        d = min(1.0, math.sqrt(dx * dx + dy * dy))
        return (255, 255, 255, int(255 * (1 - d) ** 2 * (1 + 2 * d) + 0.5))
    return Image(size, size, pixel)


def tpl(image):
    """One RGBA8 (format 6) image: 4x4 tiles of 16 alpha-red pairs, then
    16 green-blue pairs."""
    w, h = image.width, image.height
    data = bytearray()
    for ty in range(0, h, 4):
        for tx in range(0, w, 4):
            ar, gb = bytearray(), bytearray()
            for y in range(ty, ty + 4):
                for x in range(tx, tx + 4):
                    r, g, b, a = image.at(min(x, w - 1), min(y, h - 1))
                    ar += bytes((a, r))
                    gb += bytes((g, b))
            data += ar + gb
    header = struct.pack(">III", 0x0020AF30, 1, 0x0C) + struct.pack(">II", 0x14, 0)
    header += struct.pack(">HHIIIIIIfBBBB", h, w, 6, 0x40, 0, 0, 1, 1, 0.0, 0, 0, 0, 0)
    header += bytes(0x40 - len(header))
    return bytes(header + data)


# ---------------------------------------------------------------- BRLYT ----
def pad4(b):
    return b + bytes(-len(b) % 4)


def section(magic, body):
    body = pad4(body)
    return magic + struct.pack(">I", 8 + len(body)) + body


def name_field(name, size):
    raw = name.encode("ascii")
    assert len(raw) < size, name
    return raw + bytes(size - len(raw))


def pane_body(name, trans=(0, 0), size=(0, 0), scale=(1, 1), rot=0.0, alpha=255):
    return (struct.pack(">BBBB", 0x01, 4, alpha, 0) + name_field(name, 16) + bytes(8) +
            struct.pack(">3f3f2f2f", trans[0], trans[1], 0, 0, 0, rot, scale[0], scale[1], size[0], size[1]))


class Layout:
    """A banner or icon layout: one root pane holding pictures and panes,
    every picture with its own simple textured material (one texture map,
    one SRT, one coordinate generator, no TEV stages: the Wii Menu's
    default shading, the vertex colours times the texture)."""

    def __init__(self, width, height):
        self.width, self.height = width, height
        self.textures, self.materials, self.panes = [], [], []

    def texture(self, name):
        if name not in self.textures:
            self.textures.append(name)
        return self.textures.index(name)

    def picture(self, name, texture, trans, size, colors, rot=0.0, alpha=255):
        mat = len(self.materials)
        self.materials.append((name, self.texture(texture)))
        colors = [c if len(c) == 4 else (*c, 255) for c in colors]
        body = pane_body(name, trans, size, rot=rot, alpha=alpha)
        body += b"".join(bytes(c) for c in colors)  # top left, top right, bottom left, bottom right
        body += struct.pack(">HBB", mat, 1, 0) + struct.pack(">8f", 0, 0, 1, 0, 0, 1, 1, 1)
        self.panes.append(section(b"pic1", body))

    def pane(self, name, trans=(0, 0)):
        self.panes.append(section(b"pan1", pane_body(name, trans, (30, 40))))

    def begin(self):
        self.panes.append(section(b"pas1", b""))

    def end(self):
        self.panes.append(section(b"pae1", b""))

    def build(self):
        names = b""
        entries = b""
        for t in self.textures:
            entries += struct.pack(">II", 8 * len(self.textures) + len(names), 0)
            names += t.encode("ascii") + b"\0"
        txl = section(b"txl1", struct.pack(">HH", len(self.textures), 0) + entries + names)
        mats = []
        for name, tex in self.materials:
            m = name_field(name, 20)
            m += struct.pack(">4h", 0, 0, 0, 0) + struct.pack(">4h", 255, 255, 255, 255) + struct.pack(">4h", 255, 255, 255, 255)
            m += b"\xff" * 16
            m += struct.pack(">I", 0x111)
            m += struct.pack(">HBB", tex, 0, 0)
            m += struct.pack(">5f", 0, 0, 0, 1, 1)
            m += bytes((1, 4, 0x1E, 0))
            mats.append(m)
        table = 12 + 4 * len(mats)
        offsets, at = b"", table
        for m in mats:
            offsets += struct.pack(">I", at)
            at += len(m)
        mat = section(b"mat1", struct.pack(">HH", len(mats), 0) + offsets + b"".join(mats))
        lyt = section(b"lyt1", struct.pack(">B3xff", 1, self.width, self.height))
        root = section(b"pan1", pane_body("RootPane", size=(self.width, self.height)))
        grp = section(b"grp1", name_field("RootGroup", 16) + struct.pack(">HH", 0, 0))
        parts = [lyt, txl, mat, root, section(b"pas1", b"")] + self.panes + [section(b"pae1", b""), grp]
        body = b"".join(parts)
        return b"RLYT" + struct.pack(">HHIHH", 0xFEFF, 0x0008, 16 + len(body), 0x10, len(parts)) + body


# ---------------------------------------------------------------- BRLAN ----
def hermite(keys):
    """(frame, value) keys with slopes that ease in and out (flat at each key)."""
    return [(f, v, 0.0) for f, v in keys]


def anim_group(target, keys):
    body = struct.pack(">BBBBHHI", 0, target, 2, 0, len(keys), 0, 12)
    return body + b"".join(struct.pack(">3f", *k) for k in keys)


def anim_tag(magic, groups):
    head = 8 + 4 * len(groups)
    offsets, at = b"", head
    for g in groups:
        offsets += struct.pack(">I", at)
        at += len(g)
    return magic + struct.pack(">B3x", len(groups)) + offsets + b"".join(groups)


# RLPA targets: 0/1 translate x/y, 5 rotate z, 6/7 scale x/y. RLVC 16: the pane's alpha.
def anim_entry(name, tags):
    head = 24 + 4 * len(tags)
    offsets, at = b"", head
    for t in tags:
        offsets += struct.pack(">I", at)
        at += len(t)
    return name_field(name, 20) + struct.pack(">BBH", len(tags), 0, 0) + offsets + b"".join(tags)


def brlan(frames, entries):
    head = 0x14 + 4 * len(entries)
    offsets, at = b"", head
    for e in entries:
        offsets += struct.pack(">I", at)
        at += len(e)
    pai = section(b"pai1", struct.pack(">HBBHHI", frames, 1, 0, 0, len(entries), 0x14) + offsets + b"".join(entries))
    return b"RLAN" + struct.pack(">HHIHH", 0xFEFF, 0x0008, 16 + len(pai), 0x10, 1) + pai


# ------------------------------------------------------------------- U8 ----
def u8(tree):
    """tree: {name: bytes or {name: ...}}, packed depth first, names sorted."""
    nodes, names, blobs = [], bytearray(b"\0"), []

    def walk(d, parent):
        for name in sorted(d, key=str.lower):
            v = d[name]
            name_off = len(names)
            names.extend(name.encode("ascii") + b"\0")
            if isinstance(v, dict):
                index = len(nodes)
                nodes.append([1, name_off, parent, 0])
                walk(v, index)
                nodes[index][3] = len(nodes)
            else:
                nodes.append([0, name_off, len(blobs), len(v)])
                blobs.append(v)

    nodes.append([1, 0, 0, 0])
    walk(tree, 0)
    nodes[0][3] = len(nodes)
    header_size = 12 * len(nodes) + len(names)
    data_at = (0x20 + header_size + 0x1F) & ~0x1F
    out, data = bytearray(), bytearray()
    for kind, name_off, a, size in nodes:
        if kind == 0:
            blob_at = data_at + len(data)
            data += blobs[a] + bytes(-len(blobs[a]) % 0x20)
            a = blob_at
        out += struct.pack(">II", (kind << 24) | name_off, a) + struct.pack(">I", size)
    head = struct.pack(">IIII", 0x55AA382D, 0x20, header_size, data_at) + bytes(16)
    body = head + out + names
    return bytes(body + bytes(data_at - len(body)) + data)


# -------------------------------------------------------- LZ77 and IMD5 ----
def lz77(data):
    """LZ77 type 0x10 as banners use it: greedy, matches of 3 to 18 bytes
    up to 4096 back."""
    out = bytearray(b"LZ77" + struct.pack("<I", (len(data) << 8) | 0x10))
    i, n = 0, len(data)
    index = {}
    while i < n:
        flag_at = len(out)
        out.append(0)
        flags = 0
        for bit in range(8):
            if i >= n:
                break
            best_len, best_disp = 0, 0
            if i + 3 <= n:
                for j in reversed(index.get(data[i:i + 3], [])):
                    if i - j > 4096:
                        break
                    length = 3
                    while length < 18 and i + length < n and data[j + length] == data[i + length]:
                        length += 1
                    if length > best_len:
                        best_len, best_disp = length, i - j
                        if length == 18:
                            break
            step = best_len if best_len >= 3 else 1
            if best_len >= 3:
                flags |= 0x80 >> bit
                out += struct.pack(">H", ((best_len - 3) << 12) | (best_disp - 1))
            else:
                out.append(data[i])
            for k in range(i, i + step):
                if k + 3 <= n:
                    index.setdefault(data[k:k + 3], []).append(k)
            i += step
        out[flag_at] = flags
    return bytes(out + bytes(-len(out) % 4))


def imd5(payload):
    return b"IMD5" + struct.pack(">I", len(payload)) + bytes(8) + hashlib.md5(payload).digest() + payload


# ------------------------------------------------------------------ BNS ----
ADPCM_COEFS = [(0, 0), (2048, 0), (0, 2048), (1024, 1024), (4096, -2048), (3584, -1536), (3072, -1024), (4608, -2560)]


def adpcm(samples):
    """DSP-ADPCM, 14 samples per 8-byte frame, the best of eight fixed
    predictors per frame."""
    out = bytearray()
    h1 = h2 = 0
    for f in range(0, len(samples), 14):
        block = samples[f:f + 14] + [0] * (14 - len(samples[f:f + 14]))
        best = None
        for p, (c1, c2) in enumerate(ADPCM_COEFS):
            for scale in range(12):
                a, b = h1, h2
                nibbles, err = [], 0
                for s in block:
                    pred = (c1 * a + c2 * b + 1024) >> 11
                    n = int(round((s - pred) / (1 << scale)))
                    n = max(-8, min(7, n))
                    v = max(-32768, min(32767, pred + (n << scale)))
                    err += (v - s) ** 2
                    nibbles.append(n & 15)
                    a, b = v, a
                if best is None or err < best[0]:
                    best = (err, p, scale, nibbles, a, b)
                if err == 0:
                    break
        _, p, scale, nibbles, h1, h2 = best
        out.append((p << 4) | scale)
        for k in range(0, 14, 2):
            out.append((nibbles[k] << 4) | nibbles[k + 1])
    return bytes(out)


def chime(rate=32000):
    """Two soft bell notes, a fifth apart, about a second long."""
    total = int(rate * 1.2)
    samples = []
    for i in range(total):
        t = i / rate
        v = 0.0
        for start, freq in ((0.0, 1318.5), (0.14, 1975.5)):
            if t >= start:
                u = t - start
                env = math.exp(-u * 4.0) * min(1.0, u * 200)
                v += env * (math.sin(2 * math.pi * freq * u) + 0.25 * math.sin(4 * math.pi * freq * u))
        samples.append(int(max(-1, min(1, v * 0.28)) * 32767))
    return rate, samples


def bns(rate, samples):
    data = adpcm(samples)
    coefs = b"".join(struct.pack(">hh", *c) for c in ADPCM_COEFS)
    info = struct.pack(">BBBBHHIIII", 0, 0, 1, 0, rate, 0, 0, len(samples), 0x18, 0)
    info += struct.pack(">I", 0x1C)                   # channel table
    info += struct.pack(">III", 0, 0x28, 0)           # channel 0: data, ADPCM state
    info += coefs + struct.pack(">HHhhHhhH", 0, data[0], 0, 0, 0, 0, 0, 0)
    info = b"INFO" + struct.pack(">I", 8 + len(info)) + info
    info += bytes(-len(info) % 0x20)
    info = info[:4] + struct.pack(">I", len(info)) + info[8:]
    body = b"DATA" + struct.pack(">I", 8 + len(data)) + data
    body += bytes(-len(body) % 0x20)
    size = 0x20 + len(info) + len(body)
    head = b"BNS " + struct.pack(">IIHHIIII", 0xFEFF0100, size, 0x20, 2, 0x20, len(info), 0x20 + len(info), len(body))
    return head + info + body


# ------------------------------------------------------------ the banner ----
NAVY_TOP, NAVY_BOTTOM = (26, 44, 84), (8, 14, 30)
LIGHT = (70, 150, 255)


def banner_art(logo):
    """The banner (608x456, shown when the channel is picked) and the icon
    (the Wii Menu tile, 128x96 in the middle of the same space)."""
    textures = {"rw_white.tpl": tpl(Image(8, 8, lambda x, y: (255, 255, 255, 255))),
                "rw_glow.tpl": tpl(glow_image(64)),
                "rw_logo.tpl": tpl(logo)}
    lw, lh = logo.width, logo.height

    def scene(width, height, logo_scale, glow_size, shine_size, sweep):
        lay = Layout(608, 456)
        lay.picture("P_back", "rw_white.tpl", (0, 0), (width, height),
                    [NAVY_TOP, NAVY_TOP, NAVY_BOTTOM, NAVY_BOTTOM])
        lay.picture("P_glow", "rw_glow.tpl", (0, 0), (glow_size, glow_size), [(*LIGHT, 150)] * 4)
        lay.pane("N_logo")
        lay.begin()
        lay.picture("P_logo", "rw_logo.tpl", (0, 0), (lw * logo_scale, lh * logo_scale), [(255, 255, 255)] * 4)
        lay.end()
        lay.picture("P_shine", "rw_glow.tpl", (-sweep, 0), shine_size, [(255, 255, 255, 110)] * 4, rot=-20.0, alpha=0)
        # 4 seconds at 60 frames: the light breathes, the logo swells a
        # little, and a shine crosses the logo once.
        anim = brlan(240, [
            anim_entry("P_glow", [
                anim_tag(b"RLPA", [anim_group(6, hermite([(0, 1.0), (120, 1.15), (240, 1.0)])),
                                   anim_group(7, hermite([(0, 1.0), (120, 1.15), (240, 1.0)]))]),
                anim_tag(b"RLVC", [anim_group(16, hermite([(0, 150), (120, 230), (240, 150)]))])]),
            anim_entry("N_logo", [
                anim_tag(b"RLPA", [anim_group(6, hermite([(0, 1.0), (120, 1.04), (240, 1.0)])),
                                   anim_group(7, hermite([(0, 1.0), (120, 1.04), (240, 1.0)]))])]),
            anim_entry("P_shine", [
                anim_tag(b"RLPA", [anim_group(0, [(0, -sweep, 0.0), (100, sweep, 2 * sweep / 100), (240, sweep, 0.0)])]),
                anim_tag(b"RLVC", [anim_group(16, hermite([(0, 0), (20, 255), (80, 255), (100, 0), (240, 0)]))])]),
        ])
        return lay.build(), anim

    banner_lyt, banner_anim = scene(832, 456, 3.0, 460, (140, 300), 420)
    icon_lyt, icon_anim = scene(128, 96, 0.9, 110, (36, 110), 80)
    banner = u8({"arc": {"anim": {"banner.brlan": banner_anim}, "blyt": {"banner.brlyt": banner_lyt},
                         "timg": textures}})
    icon = u8({"arc": {"anim": {"icon.brlan": icon_anim}, "blyt": {"icon.brlyt": icon_lyt}, "timg": textures}})
    return banner, icon


def opening(banner, icon, sound):
    """Content 0 (00000000.app): the IMET header, then meta/ with the
    three files. In a channel the header starts after 0x40 bytes of build
    information (zero here) and 0x40 of padding; its MD5 covers the 0x600
    bytes from 0x40 with the MD5 field zeroed."""
    files = {"banner.bin": imd5(lz77(banner)), "icon.bin": imd5(lz77(icon)), "sound.bin": imd5(sound)}
    head = bytearray(0x640)
    struct.pack_into(">4sIIIIII", head, 0x80, b"IMET", 0x600, 3, len(icon), len(banner), len(sound), 0)
    name = CHANNEL_NAME.encode("utf-16-be")
    for lang in range(10):
        head[0x9C + lang * 0x54:0x9C + lang * 0x54 + len(name)] = name
    head[0x630:0x640] = hashlib.md5(bytes(head[0x40:0x640])).digest()
    return bytes(head) + u8({"meta": files})


# -------------------------------------------------------- TMD and ticket ----
def tmd(contents):
    """Unsigned: RiftWii signs it on the console, as it does the ticket."""
    t = bytearray(0x1E4)
    struct.pack_into(">I", t, 0, 0x00010001)
    t[0x140:0x140 + 26] = b"Root-CA00000001-CP00000004"
    struct.pack_into(">QQIH", t, 0x184, 0x0000000100000000 | IOS, TITLE_ID, 1, 0x3031)
    struct.pack_into(">H", t, 0x19C, 3)  # region free
    struct.pack_into(">IHHHH", t, 0x1D8, 0, TITLE_VERSION, len(contents), 1, 0)
    for index, data in enumerate(contents):
        t += struct.pack(">IHHQ", index, index, 1, len(data)) + hashlib.sha1(data).digest()
    return bytes(t)


def ticket():
    """Unsigned, with no title key: RiftWii puts the key in."""
    t = bytearray(0x2A4)
    struct.pack_into(">I", t, 0, 0x00010001)
    t[0x140:0x140 + 26] = b"Root-CA00000001-XS00000003"
    struct.pack_into(">QIQ", t, 0x1D0, TITLE_ID ^ 0x52465457, 0, TITLE_ID)
    struct.pack_into(">HH", t, 0x1E4, 0xFFFF, TITLE_VERSION)
    t[0x222:0x262] = b"\xff" * 0x40
    return bytes(t)


def package(contents):
    """What RiftWii embeds (wii/channel.cpp): a header, the TMD, the ticket
    and the plain contents, each 32-byte aligned."""
    parts = [tmd(contents), ticket()] + contents
    head = struct.pack(">4sIQII", b"RWCH", 1, TITLE_ID, TITLE_VERSION, len(parts))
    at = 0x20 + 8 * len(parts)
    at = (at + 31) & ~31
    table, body = b"", b""
    for p in parts:
        table += struct.pack(">II", at + len(body), len(p))
        body += p + bytes(-len(p) % 32)
    head += table
    return head + bytes(at - len(head)) + body


def preview(out_dir, logo, lw_scale=3.0):
    """A still of the banner and icon (their first frame), to check the
    art without a Wii Menu."""
    os.makedirs(out_dir, exist_ok=True)

    def still(w, h, s, glow, name):
        rows = []
        for y in range(h):
            row = bytearray()
            t = y / (h - 1)
            for x in range(w):
                c = [NAVY_TOP[k] * (1 - t) + NAVY_BOTTOM[k] * t for k in range(3)]
                dx, dy = (x - w / 2) / (glow / 2), (y - h / 2) / (glow / 2)
                d = min(1.0, math.sqrt(dx * dx + dy * dy))
                a = (1 - d) ** 2 * (1 + 2 * d) * 150 / 255
                c = [c[k] * (1 - a) + LIGHT[k] * a for k in range(3)]
                lx = int((x - (w - logo.width * s) / 2) / s)
                ly = int((y - (h - logo.height * s) / 2) / s)
                if 0 <= lx < logo.width and 0 <= ly < logo.height:
                    p = logo.at(lx, ly)
                    la = p[3] / 255
                    c = [p[k] * la + c[k] * (1 - la) for k in range(3)]
                row += bytes(int(v) for v in c)
            rows.append(bytes(row))
        raw = b"".join(b"\0" + r for r in rows)

        def chunk(kind, body):
            return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
        png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
        open(os.path.join(out_dir, name), "wb").write(png)

    still(832, 456, 3.0, 460, "banner.png")
    still(128, 96, 0.9, 110, "icon.png")


def main(argv):
    if len(argv) == 4 and argv[1] == "logo":
        logo = png_image(argv[2])
        open(argv[3], "wb").write(b"".join(bytes(p) for p in logo.pixels))
        return 0
    if len(argv) in (5, 6) and argv[1] == "build":
        forwarder = open(argv[2], "rb").read()
        logo = png_image(argv[3])
        banner, icon = banner_art(logo)
        app0 = opening(banner, icon, bns(*chime()))
        blob = package([app0, forwarder])
        open(argv[4], "wb").write(blob)
        info = os.path.join(os.path.dirname(argv[4]) or ".", "riftwii_channel_info.h")
        open(info, "w").write("// Written by tools/make_channel.py.\n#pragma once\n"
                              f"#define RIFTWII_CHANNEL_TITLE 0x{TITLE_ID:016x}ull\n"
                              f"#define RIFTWII_CHANNEL_VERSION {TITLE_VERSION}u\n")
        if len(argv) == 6:
            preview(argv[5], logo)
            open(os.path.join(argv[5], "00000000.app"), "wb").write(app0)
        print(f"channel: banner {len(app0)} bytes, forwarder {len(forwarder)} bytes, package {len(blob)} bytes")
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
