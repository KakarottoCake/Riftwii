#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds the RiftWii channel (docs/CHANNEL.md) from our own art: the
Wii Menu banner, icon and sound (content 0), the forwarder (content 1),
and the TMD and ticket RiftWii installs them with.

    make_channel.py build <forwarder.dol> <art dir> <out.bin> [dir]
        the package wii/channel.cpp installs, and beside it
        riftwii_channel_info.h (its title ID and version, for the menu);
        a fourth argument also writes the banner (00000000.app) there

Nothing here is encrypted or signed: the console does that when RiftWii
installs the channel. The formats (U8, IMD5, LZ77, TPL, BRLYT, BRLAN,
BNS, IMET, TMD, ticket) are written from their public descriptions and
checked against what a retail disc's banner holds; no Nintendo data is
used. Standard library only.
"""
import hashlib
import json
import math
import os
import random
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
    def __init__(self, width, height, pixels):
        self.width, self.height = width, height
        self.pixels = pixels

    def at(self, x, y):
        return self.pixels[y * self.width + x]


def png_image(path):
    w, h, rows = read_png(path)
    return Image(w, h, [tuple(rows[y][x * 4:x * 4 + 4]) for y in range(h) for x in range(w)])


def shrink(image, n):
    """The image n times smaller each way (the mean of each n x n block)."""
    w, h = max(1, image.width // n), max(1, image.height // n)

    def pixel(x, y):
        block = [image.at(x * n + i, y * n + j) for j in range(n) for i in range(n)]
        return tuple(sum(p[k] for p in block) // len(block) for k in range(4))
    return Image(w, h, [pixel(x, y) for y in range(h) for x in range(w)])


# Texture formats: 2 IA4 (one byte a pixel: the soft shapes), 3 IA8 (two
# bytes: the letters, whose edges want the finer steps), 4 RGB565 (the
# backdrop). The shapes are white; the banner tints them.
TILE = {2: (8, 4), 3: (4, 4), 4: (4, 4)}


def tpl(image, fmt):
    w, h = image.width, image.height
    bw, bh = TILE[fmt]
    data = bytearray()
    for ty in range(0, h, bh):
        for tx in range(0, w, bw):
            for y in range(ty, ty + bh):
                for x in range(tx, tx + bw):
                    r, g, b, a = image.at(min(x, w - 1), min(y, h - 1))
                    if fmt == 2:
                        data.append(((a * 15 + 127) // 255) << 4 | (max(r, g, b) * 15 + 127) // 255)
                    elif fmt == 3:
                        data += bytes((a, max(r, g, b)))
                    else:
                        data += struct.pack(">H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    header = struct.pack(">III", 0x0020AF30, 1, 0x0C) + struct.pack(">II", 0x14, 0)
    header += struct.pack(">HHIIIIIIfBBBB", h, w, fmt, 0x40, 0, 0, 1, 1, 0.0, 0, 0, 0, 0)
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


class Node:
    """A pane (a group that moves its children) or a picture (a textured
    quad, tinted by its four vertex colours: top left, top right, bottom
    left, bottom right). Coordinates: the screen's centre is 0,0, y up."""

    def __init__(self, name, tex=None, pos=(0, 0), size=(0, 0), colors=None, rot=0.0, scale=(1, 1),
                 alpha=255, children=()):
        self.name, self.tex = name, tex
        self.pos, self.size, self.rot, self.scale, self.alpha = pos, size, rot, scale, alpha
        colors = colors or [(255, 255, 255)] * 4
        if len(colors) == 1:
            colors = colors * 4
        elif len(colors) == 2:
            colors = [colors[0], colors[0], colors[1], colors[1]]
        self.colors = [c if len(c) == 4 else (*c, 255) for c in colors]
        self.children = list(children)

    def walk(self):
        yield self
        for c in self.children:
            yield from c.walk()


def brlyt(root, textures):
    """root's children become the layout's panes. Every picture gets its
    own material, the Wii Menu's plain textured one: one texture map, one
    SRT, one coordinate generator and no TEV stages (texture times vertex
    colour), as retail banners use."""
    tex_names = sorted(textures)
    mats, panes = [], []

    def body(n):
        # flags: visible, and children take on this pane's alpha
        return (struct.pack(">BBBB", 0x03, 4, n.alpha, 0) + name_field(n.name, 16) + bytes(8) +
                struct.pack(">3f3f2f2f", n.pos[0], n.pos[1], 0, 0, 0, n.rot, n.scale[0], n.scale[1],
                            n.size[0], n.size[1]))

    def emit(n):
        if n.tex:
            m = name_field(n.name, 20)
            m += struct.pack(">4h", 0, 0, 0, 0) + struct.pack(">4h", 255, 255, 255, 255) * 2
            m += b"\xff" * 16 + struct.pack(">I", 0x111)
            m += struct.pack(">HBB", tex_names.index(n.tex), 0, 0)
            m += struct.pack(">5f", 0, 0, 0, 1, 1) + bytes((1, 4, 0x1E, 0))
            b = body(n) + b"".join(bytes(c) for c in n.colors)
            b += struct.pack(">HBB", len(mats), 1, 0) + struct.pack(">8f", 0, 0, 1, 0, 0, 1, 1, 1)
            mats.append(m)
            panes.append(section(b"pic1", b))
        else:
            panes.append(section(b"pan1", body(n)))
        if n.children:
            panes.append(section(b"pas1", b""))
            for c in n.children:
                emit(c)
            panes.append(section(b"pae1", b""))

    for c in root.children:
        emit(c)
    names, entries = b"", b""
    for t in tex_names:
        entries += struct.pack(">II", 8 * len(tex_names) + len(names), 0)
        names += t.encode("ascii") + b"\0"
    txl = section(b"txl1", struct.pack(">HH", len(tex_names), 0) + entries + names)
    table = 12 + 4 * len(mats)
    offsets, at = b"", table
    for m in mats:
        offsets += struct.pack(">I", at)
        at += len(m)
    mat = section(b"mat1", struct.pack(">HH", len(mats), 0) + offsets + b"".join(mats))
    lyt = section(b"lyt1", struct.pack(">B3xff", 1, 608, 456))
    rootpane = section(b"pan1", struct.pack(">BBBB", 0x01, 4, 255, 0) + name_field("RootPane", 16) + bytes(8) +
                       struct.pack(">3f3f2f2f", 0, 0, 0, 0, 0, 0, 1, 1, 608, 456))
    grp = section(b"grp1", name_field("RootGroup", 16) + struct.pack(">HH", 0, 0))
    parts = [lyt, txl, mat, rootpane, section(b"pas1", b"")] + panes + [section(b"pae1", b""), grp]
    data = b"".join(parts)
    return b"RLYT" + struct.pack(">HHIHH", 0xFEFF, 0x0008, 16 + len(data), 0x10, len(parts)) + data


# ---------------------------------------------------------------- BRLAN ----
# What a track animates. RLPA: 0/1 move x/y, 5 turn, 6/7 scale x/y.
# RLVC 16: the pane's alpha.
TARGETS = {"x": (b"RLPA", 0), "y": (b"RLPA", 1), "rot": (b"RLPA", 5), "sx": (b"RLPA", 6),
           "sy": (b"RLPA", 7), "alpha": (b"RLVC", 16)}


def hermite_at(keys, f):
    """The value of a track at frame f, as the Wii Menu works it out: cubic
    Hermite between keys, slopes in value per frame, flat outside."""
    if f <= keys[0][0]:
        return keys[0][1]
    if f >= keys[-1][0]:
        return keys[-1][1]
    for (f0, v0, s0), (f1, v1, s1) in zip(keys, keys[1:]):
        if f0 <= f < f1:
            if f1 == f0:
                continue
            t = (f - f0) / (f1 - f0)
            d = f1 - f0
            return (v0 * (2 * t ** 3 - 3 * t ** 2 + 1) + v1 * (-2 * t ** 3 + 3 * t ** 2) +
                    s0 * d * (t ** 3 - 2 * t ** 2 + t) + s1 * d * (t ** 3 - t ** 2))
    return keys[-1][1]


def brlan(frames, tracks):
    """tracks: {(pane name, target): [(frame, value, slope), ...]}."""
    by_pane = {}
    for (pane, target), keys in tracks.items():
        by_pane.setdefault(pane, []).append((TARGETS[target], keys))
    entries = []
    for pane, groups in by_pane.items():
        tags = []
        for magic in (b"RLPA", b"RLVC"):
            gs = [(idx, keys) for (m, idx), keys in groups if m == magic]
            if not gs:
                continue
            blobs = []
            for idx, keys in sorted(gs):
                blob = struct.pack(">BBBBHHI", 0, idx, 2, 0, len(keys), 0, 12)
                blobs.append(blob + b"".join(struct.pack(">3f", *k) for k in keys))
            head = 8 + 4 * len(blobs)
            offsets, at = b"", head
            for b in blobs:
                offsets += struct.pack(">I", at)
                at += len(b)
            tags.append(magic + struct.pack(">B3x", len(blobs)) + offsets + b"".join(blobs))
        head = 24 + 4 * len(tags)
        offsets, at = b"", head
        for t in tags:
            offsets += struct.pack(">I", at)
            at += len(t)
        entries.append(name_field(pane, 20) + struct.pack(">BBH", len(tags), 0, 0) + offsets + b"".join(tags))
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


def banner_sound(rate=32000):
    """The banner's sound, timed to banner_start (60 frames a second): a
    rising whoosh as the sky fades up, a crackling zap as the rift tears
    open, a plucked note for each letter as it flies out (rising, in the
    order they come), and a warm chord as the word settles."""
    total = int(rate * 3.8)
    out = [0.0] * total
    rnd = random.Random(1)

    def add(at, samples, gain):
        start = int(at * rate)
        for i, v in enumerate(samples):
            if start + i < total:
                out[start + i] += v * gain

    # the whoosh: noise through a resonant filter sweeping up
    n = int(rate * 0.55)
    low = band = 0.0
    whoosh = []
    for i in range(n):
        t = i / n
        cutoff = 250 + 3200 * t * t
        f = 2 * math.sin(math.pi * cutoff / rate)
        x = rnd.uniform(-1, 1)
        low += f * band
        high = x - low - 0.35 * band
        band += f * high
        whoosh.append(band * (t ** 1.5) * (1 - max(0.0, t - 0.85) / 0.15))
    add(0.0, whoosh, 0.55)

    # the zap: a falling tone with crackle, as the rift opens (frame 28)
    n = int(rate * 0.45)
    zap, phase = [], 0.0
    for i in range(n):
        t = i / rate
        freq = 200 + 2600 * math.exp(-t * 14)
        phase += 2 * math.pi * freq / rate
        env = math.exp(-t * 7)
        crackle = rnd.uniform(-1, 1) if rnd.random() < 0.08 * env else 0.0
        zap.append(env * (0.6 * math.sin(phase) + 0.25 * math.sin(phase * 2.01)) + crackle * 0.7)
    add(28 / 60, zap, 0.5)

    # the letters: A major pentatonic, one note per letter, 6 frames apart
    notes = [880.0, 987.8, 1108.7, 1318.5, 1480.0, 1760.0, 1975.5]
    for k, freq in enumerate(notes):
        n = int(rate * 0.5)
        pluck = []
        for i in range(n):
            t = i / rate
            env = math.exp(-t * 9) * min(1.0, t * 400)
            pluck.append(env * (math.sin(2 * math.pi * freq * t) + 0.3 * math.sin(4 * math.pi * freq * t) +
                                0.12 * math.sin(2 * math.pi * freq * 3.003 * t)))
        add((34 + 6 * k) / 60, pluck, 0.22)

    # the word settles: a soft A major chord with a shimmer on top
    n = total - int(rate * 1.45)
    chord = []
    for i in range(n):
        t = i / rate
        env = min(1.0, t / 0.12) * math.exp(-t * 1.3)
        v = sum(math.sin(2 * math.pi * f * t) * a for f, a in ((220.0, 0.5), (277.2, 0.35), (329.6, 0.35), (440.0, 0.3)))
        v += 0.18 * math.sin(2 * math.pi * 1760.0 * t) * (0.5 + 0.5 * math.sin(2 * math.pi * 6 * t))
        chord.append(v * env)
    add(1.45, chord, 0.32)

    peak = max(abs(v) for v in out) or 1.0
    fade = int(rate * 0.3)
    samples = []
    for i, v in enumerate(out):
        g = 0.85 / peak * (min(1.0, (total - i) / fade))
        samples.append(int(max(-1.0, min(1.0, v * g)) * 32767))
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
# channel/art/ holds the pictures (tools/make_channel_art.py draws them).
# Here they are placed and animated: the banner plays banner_start once when
# the channel is picked, then banner_loop for as long as it stays open; the
# icon (the Wii Menu tile) loops icon.brlan.
ICE = (205, 238, 255)
CYAN = (47, 182, 233)      # RiftWii's accent colour
DEEP = (20, 110, 210)
VIOLET = (150, 90, 255)
PINK = (235, 110, 225)

START = 110                # banner_start: about 1.8 seconds
LOOP = 480                 # banner_loop and icon: 8 seconds


def ease(*points):
    """(frame, value) keys that ease in and out of each point."""
    return [(float(f), float(v), 0.0) for f, v in points]


def line(f0, v0, f1, v1):
    s = (v1 - v0) / (f1 - f0)
    return [(float(f0), float(v0), s), (float(f1), float(v1), s)]


def sampled(fn, f0, f1, step=8):
    """Keys that follow fn(frame) closely: samples with their slopes."""
    frames = list(range(int(f0), int(f1), step)) + [int(f1)]
    return [(float(f), float(fn(f)), (fn(f + 0.5) - fn(f - 0.5))) for f in frames]


def wave(base, amp, period, phase=0.0):
    """base + a sine that starts at 0, so the loop picks up where the
    start animation leaves the pane."""
    return lambda f: base + amp * (math.sin(2 * math.pi * f / period + phase) - math.sin(phase))


def cycle(fn, period, phase, length=LOOP, step=15):
    """A motion that repeats every period frames, restarting with a jump
    (two keys on one frame): fn(u) for u from 0 to 1. The loop's length is
    a whole number of periods, so it joins up."""
    assert length % period == 0
    h = 0.5 / period

    def key(f, u):
        u1, u2 = max(0.0, u - h), min(1.0, u + h)
        return (float(f), float(fn(u)), (fn(u2) - fn(u1)) / ((u2 - u1) * period))

    keys, f, off = [], 0.0, phase * period
    while f < length:
        u = ((f + off) % period) / period
        boundary = f + (1 - u) * period
        end = min(length, boundary)
        a = f
        while a < end:
            keys.append(key(a, ((a + off) % period) / period))
            a = min(end, a + step)
        keys.append(key(end, 1.0 if end == boundary else ((end + off) % period) / period))
        f = end
    return keys


def scene_banner(art):
    L = art["letters"]
    gap = 26.0                                     # the rift shows between "Rift" and "Wii"
    width = art["width"] + gap
    scale = 1.22
    centers = []
    for i, l in enumerate(L):
        shift = gap if i >= 4 else 0.0
        centers.append(l["x"] + l["w"] / 2 + shift - width / 2)
    seam = (sum(l["advance"] for l in L[:4]) + gap / 2 - width / 2) * scale
    word_y = 18.0

    root = Node("root")
    kids = root.children
    kids.append(Node("P_bg", "rw_nebula.tpl", size=(860, 484)))
    kids.append(Node("P_cloudA", "rw_cloud.tpl", (-190, 60), (620, 620), [(*VIOLET, 150)], alpha=150))
    kids.append(Node("P_cloudB", "rw_cloud.tpl", (220, -70), (560, 560), [(*CYAN, 120)], rot=140, alpha=130))
    rnd = random.Random(7)
    stars = []
    for i in range(18):
        while True:
            p = (rnd.uniform(-400, 400), rnd.uniform(-215, 215))
            if abs(p[1] - word_y) > 90 or abs(p[0]) > 300:
                break
        s = rnd.uniform(10, 26)
        stars.append(Node(f"P_star{i:02d}", "rw_sparkle.tpl", p, (s, s), [(*rnd.choice([ICE, ICE, CYAN, PINK]), 255)]))
    kids.extend(stars)

    portal = Node("N_portal", pos=(0, word_y))
    kids.append(portal)
    portal.children.append(Node("P_halo", "rw_glow.tpl", (seam, 0), (560, 400), [(*DEEP, 255)], alpha=150))
    tilt = Node("N_tilt", pos=(seam, -6), scale=(1.0, 0.30), rot=-8)
    tilt.children.append(Node("P_ringA", "rw_ring_a.tpl", size=(600, 600), colors=[(*CYAN, 255)], alpha=230))
    tilt.children.append(Node("P_ringB", "rw_ring_b.tpl", size=(520, 520), colors=[(*VIOLET, 255)], alpha=200))
    portal.children.append(tilt)
    portal.children.append(Node("P_riftGlow", "rw_glow.tpl", (seam, 0), (150, 420), [(*CYAN, 255)], alpha=210))
    portal.children.append(Node("P_rift", "rw_rift.tpl", (seam, 0), (108, 250), [(*ICE, 255)]))
    sparks = []
    for i in range(12):
        sparks.append(Node(f"P_spark{i:02d}", "rw_glow.tpl", (seam, 0), (12, 12), [(*ICE, 255)], alpha=0))
    portal.children.extend(sparks)

    word = Node("N_word", pos=(0, 0), scale=(scale, scale))
    portal.children.append(word)
    for i, l in enumerate(L):
        rift_part = i < 4
        glow_color = (*(CYAN if rift_part else VIOLET), 255)
        fill = [(255, 255, 255), (200, 228, 255)] if rift_part else [(150, 232, 255), (30, 150, 235)]
        n = Node(f"N_L{i}", pos=(centers[i], 0))
        n.children.append(Node(f"P_G{i}", "rw_" + l["tex"] + "_glow.tpl", size=(l["w"], l["h"]), colors=[glow_color], alpha=150))
        n.children.append(Node(f"P_L{i}", "rw_" + l["tex"] + ".tpl", size=(l["w"], l["h"]), colors=fill))
        word.children.append(n)
    portal.children.append(Node("P_shine", "rw_streak.tpl", (-330, 0), (60, 250), [(255, 255, 255, 255)], rot=-18, alpha=0))
    kids.append(Node("P_tag", "rw_tagline.tpl", (0, -128), (art["tag_w"], art["tag_h"]), [(150, 196, 240)], alpha=210))
    kids.append(Node("P_flash", "rw_glow.tpl", (seam, word_y), (900, 900), [(235, 248, 255, 255)], alpha=0))

    # ---- banner_loop
    loop = {}
    loop[("P_bg", "x")] = sampled(wave(0, 14, LOOP), 0, LOOP, 32)
    loop[("P_cloudA", "rot")] = line(0, 0, LOOP, -40)
    loop[("P_cloudA", "sx")] = sampled(wave(1, 0.08, LOOP), 0, LOOP, 32)
    loop[("P_cloudA", "sy")] = sampled(wave(1, 0.08, LOOP), 0, LOOP, 32)
    loop[("P_cloudB", "rot")] = line(0, 140, LOOP, 180)
    loop[("P_cloudB", "alpha")] = sampled(wave(130, 50, LOOP / 2, 1.0), 0, LOOP, 24)
    for i, s in enumerate(stars):
        period = rnd.choice([120, 160, 240])
        ph = rnd.uniform(0, 1)
        loop[(s.name, "alpha")] = cycle(lambda u: 255 * max(0.0, math.sin(math.pi * u)) ** 2, period, ph)
        sc = rnd.uniform(0.7, 1.2)
        loop[(s.name, "sx")] = cycle(lambda u, sc=sc: sc * (0.6 + 0.4 * math.sin(math.pi * u)), period, ph)
        loop[(s.name, "sy")] = loop[(s.name, "sx")]
        loop[(s.name, "rot")] = line(0, 0, LOOP, rnd.choice([-90, 90]))
    loop[("P_halo", "alpha")] = sampled(wave(150, 45, 240), 0, LOOP, 24)
    loop[("P_halo", "sx")] = sampled(wave(1, 0.05, 240), 0, LOOP, 24)
    loop[("P_ringA", "rot")] = line(0, 0, LOOP, 360)
    loop[("P_ringB", "rot")] = line(0, 0, LOOP, -360)
    loop[("P_riftGlow", "sx")] = sampled(lambda f: 1 + 0.18 * (math.sin(f / 7.0) * math.sin(f / 17.0)), 0, LOOP, 8)
    loop[("P_riftGlow", "alpha")] = sampled(wave(210, 40, 120), 0, LOOP, 20)
    loop[("P_rift", "sx")] = sampled(lambda f: 1 + 0.22 * math.sin(f / 5.0) * math.sin(f / 13.0), 0, LOOP, 8)
    for i, sp in enumerate(sparks):
        period = [160, 240, 120][i % 3]
        ph = (i * 0.37) % 1
        drift = rnd.uniform(-70, 70)
        loop[(sp.name, "y")] = cycle(lambda u: -60 + 230 * u, period, ph)
        loop[(sp.name, "x")] = cycle(lambda u, d=drift: seam + d * u ** 1.5, period, ph)
        loop[(sp.name, "alpha")] = cycle(lambda u: 255 * min(1.0, u * 6) * (1 - u) ** 1.5, period, ph)
    for i in range(len(L)):
        loop[(f"N_L{i}", "y")] = sampled(wave(0, 4.0, 240, i * 0.7), 0, LOOP, 24)
        loop[(f"P_G{i}", "alpha")] = sampled(wave(150, 60, 240, i * 0.7 + 1.5), 0, LOOP, 24)
    loop[("P_shine", "x")] = [(0.0, -330.0, 0.0), (250.0, -330.0, 0.0), (250.0, -330.0, 660 / 70), (320.0, 330.0, 660 / 70),
                              (320.0, 330.0, 0.0), (float(LOOP), 330.0, 0.0), (float(LOOP), -330.0, 0.0)]
    loop[("P_shine", "alpha")] = ease((0, 0), (250, 0), (270, 170), (300, 170), (320, 0), (LOOP, 0))

    # ---- banner_start: the backdrop fades up, the rift tears open with a
    # flash, the rings spin up, and the letters fly out of the rift one by
    # one, nearest first, and settle into the word.
    start = {}
    start[("P_bg", "alpha")] = ease((0, 0), (22, 255))
    start[("P_cloudA", "alpha")] = ease((0, 0), (40, 150))
    start[("P_cloudB", "alpha")] = ease((0, 0), (40, 130))
    start[("P_rift", "sy")] = ease((0, 0), (10, 0), (30, 1.12), (38, 1.0))
    start[("P_rift", "sx")] = ease((0, 0.3), (26, 0.3), (32, 1.6), (44, 1.0))
    start[("P_riftGlow", "alpha")] = ease((0, 0), (12, 0), (30, 255), (60, 210))
    start[("P_riftGlow", "sy")] = ease((0, 0), (10, 0), (32, 1.0))
    start[("P_flash", "alpha")] = ease((0, 0), (28, 0), (33, 230), (60, 0))
    start[("P_flash", "sx")] = ease((0, 0.2), (28, 0.2), (60, 1.2))
    start[("P_flash", "sy")] = ease((0, 0.2), (28, 0.2), (60, 1.2))
    start[("P_halo", "alpha")] = ease((0, 0), (30, 0), (60, 150))
    start[("N_tilt", "sx")] = ease((0, 0.2), (32, 0.2), (62, 1.08), (74, 1.0))
    start[("N_tilt", "sy")] = ease((0, 0.06), (32, 0.06), (62, 0.33), (74, 0.30))
    start[("P_ringA", "alpha")] = ease((0, 0), (32, 0), (56, 230))
    start[("P_ringB", "alpha")] = ease((0, 0), (36, 0), (60, 200))
    start[("P_ringA", "rot")] = ease((0, -240), (32, -240)) + [(32.0, -240.0, 9.0), (float(START), 0.0, 0.75)]
    start[("P_ringB", "rot")] = ease((0, 240), (36, 240)) + [(36.0, 240.0, -9.0), (float(START), 0.0, -0.75)]
    seam_word = seam / scale
    order = sorted(range(len(L)), key=lambda i: abs(centers[i] - seam_word))
    for rank, i in enumerate(order):
        t0 = 34 + rank * 6
        t1 = t0 + 20
        side = 1 if centers[i] > seam_word else -1
        start[(f"N_L{i}", "x")] = ease((0, seam_word), (t0, seam_word), (t1, centers[i] + side * 10), (t1 + 10, centers[i]))
        start[(f"N_L{i}", "y")] = ease((0, 0), (t0, 0), (t0 + 10, 22), (t1, -4), (t1 + 10, 0))
        start[(f"N_L{i}", "sx")] = ease((0, 0.05), (t0, 0.05), (t1, 1.15), (t1 + 10, 1.0))
        start[(f"N_L{i}", "sy")] = ease((0, 0.05), (t0, 0.05), (t1, 1.15), (t1 + 10, 1.0))
        start[(f"N_L{i}", "rot")] = ease((0, side * -35), (t0, side * -35), (t1, side * 4), (t1 + 10, 0))
        start[(f"N_L{i}", "alpha")] = ease((0, 0), (t0, 0), (t0 + 6, 255))
        start[(f"P_G{i}", "alpha")] = ease((0, 0), (t0, 0), (t1, 255), (t1 + 26, 150))
    start[("P_shine", "x")] = ease((0, -330), (84, -330)) + [(84.0, -330.0, 660 / 26), (float(START), 330.0, 660 / 26)]
    start[("P_shine", "alpha")] = ease((0, 0), (84, 0), (94, 170), (104, 170), (START, 0))
    start[("P_tag", "alpha")] = ease((0, 0), (80, 0), (104, 210))
    start[("P_tag", "y")] = ease((0, -142), (80, -142), (104, -128))
    for s in stars:
        v0 = hermite_at(loop[(s.name, "alpha")], 0)
        start[(s.name, "alpha")] = ease((0, 0), (60, 0), (START, v0))
    for sp in sparks:
        start[(sp.name, "alpha")] = ease((0, 0), (START - 1, 0), (START, hermite_at(loop[(sp.name, "alpha")], 0)))
    # the loop's first frame, for everything the start leaves alone
    for (pane, target), keys in loop.items():
        if (pane, target) not in start:
            start[(pane, target)] = ease((0, hermite_at(keys, 0)))
    return root, start, loop


def scene_icon(art):
    """The Wii Menu tile: 128x96 in the middle of the screen."""
    root = Node("root")
    kids = root.children
    ww, wh = art["word_w"], art["word_h"]
    s = 104.0 / ww
    split = art["word_split"]
    kids.append(Node("P_bg", "rw_nebula.tpl", size=(176, 104)))
    kids.append(Node("P_cloud", "rw_cloud.tpl", (-30, 16), (150, 150), [(*VIOLET, 150)], alpha=150))
    kids.append(Node("P_halo", "rw_glow.tpl", (0, 4), (170, 110), [(*DEEP, 255)], alpha=170))
    tilt = Node("N_tilt", pos=(0, 0), scale=(1.0, 0.30), rot=-8)
    tilt.children.append(Node("P_ringA", "rw_ring_a.tpl", size=(150, 150), colors=[(*CYAN, 255)], alpha=230))
    tilt.children.append(Node("P_ringB", "rw_ring_b.tpl", size=(128, 128), colors=[(*VIOLET, 255)], alpha=200))
    kids.append(tilt)
    seam = (split - 0.5) * ww * s
    kids.append(Node("P_riftGlow", "rw_glow.tpl", (seam, 4), (40, 104), [(*CYAN, 255)], alpha=200))
    kids.append(Node("P_rift", "rw_rift.tpl", (seam, 4), (32, 70), [(*ICE, 255)]))
    word = Node("N_word", pos=(0, 4), scale=(s, s))
    word.children.append(Node("P_wglow", "rw_word_glow.tpl", size=(ww, wh), colors=[(*CYAN, 255)], alpha=170))
    word.children.append(Node("P_word", "rw_word.tpl", size=(ww, wh), colors=[(255, 255, 255), (196, 228, 255)]))
    kids.append(word)
    kids.append(Node("P_shine", "rw_streak.tpl", (-90, 4), (18, 90), [(255, 255, 255, 255)], rot=-18, alpha=0))
    rnd = random.Random(3)
    stars = []
    for i in range(6):
        p = (rnd.choice([-1, 1]) * rnd.uniform(30, 60), rnd.choice([-1, 1]) * rnd.uniform(26, 42))
        stars.append(Node(f"P_star{i}", "rw_sparkle.tpl", p, (9, 9), [(*rnd.choice([ICE, CYAN, PINK]), 255)]))
    kids.extend(stars)

    anim = {}
    anim[("P_ringA", "rot")] = line(0, 0, LOOP, 360)
    anim[("P_ringB", "rot")] = line(0, 0, LOOP, -360)
    anim[("P_cloud", "rot")] = line(0, 0, LOOP, -60)
    anim[("P_halo", "alpha")] = sampled(wave(170, 50, 240), 0, LOOP, 24)
    anim[("P_rift", "sx")] = sampled(lambda f: 1 + 0.22 * math.sin(f / 5.0) * math.sin(f / 13.0), 0, LOOP, 8)
    anim[("P_riftGlow", "alpha")] = sampled(wave(200, 45, 120), 0, LOOP, 20)
    anim[("N_word", "y")] = sampled(wave(4, 1.5, 240), 0, LOOP, 24)
    anim[("P_wglow", "alpha")] = sampled(wave(170, 70, 240, 1.5), 0, LOOP, 24)
    anim[("P_shine", "x")] = [(0.0, -90.0, 0.0), (300.0, -90.0, 0.0), (300.0, -90.0, 180 / 50), (350.0, 90.0, 180 / 50),
                              (350.0, 90.0, 0.0), (float(LOOP), 90.0, 0.0), (float(LOOP), -90.0, 0.0)]
    anim[("P_shine", "alpha")] = ease((0, 0), (300, 0), (315, 190), (335, 190), (350, 0), (LOOP, 0))
    for i, st in enumerate(stars):
        period = [120, 160, 240][i % 3]
        ph = rnd.uniform(0, 1)
        anim[(st.name, "alpha")] = cycle(lambda u: 255 * max(0.0, math.sin(math.pi * u)) ** 2, period, ph)
        anim[(st.name, "sx")] = cycle(lambda u: 0.6 + 0.5 * math.sin(math.pi * u), period, ph)
        anim[(st.name, "sy")] = anim[(st.name, "sx")]
    return root, anim


# The Wii Menu sets aside room for a channel's icon and banner by the sizes
# in its header, and crashes when it cannot (seen on a vWii with a 272 KB
# icon). Nintendo's own channels stay under about 92 KB and 490 KB; ours
# must stay well under those.
ICON_LIMIT = 0x10000
BANNER_LIMIT = 0x60000

# Per picture: how much smaller than drawn, and the format; the banner's
# and the icon's own. The letters keep IA8; the blurry shapes lose little
# at a quarter of the size or at IA4.
BANNER_FORMS = {"nebula": (1, 4), "cloud": (2, 2), "glow": (1, 2), "sparkle": (1, 2), "ring_a": (1, 2),
                "ring_b": (1, 2), "rift": (1, 2), "streak": (1, 2), "tagline": (1, 2), "word": (1, 3),
                "word_glow": (2, 2)}
ICON_FORMS = {"nebula": (4, 4), "cloud": (4, 2), "glow": (2, 2), "sparkle": (1, 2), "ring_a": (2, 2),
              "ring_b": (2, 2), "rift": (2, 2), "streak": (2, 2), "word": (2, 3), "word_glow": (4, 2)}


def form(name, forms):
    if name in forms:
        return forms[name]
    if name.endswith("_glow"):
        return (2, 2)  # a letter's glow
    return (1, 3)      # a letter


def load_art(art_dir, forms=BANNER_FORMS):
    """The pictures, as TPLs (and as images, for the previewer), and the
    letter placement. Sizes in the layout are the drawn pictures' own."""
    art = json.load(open(os.path.join(art_dir, "letters.json")))
    textures, images = {}, {}
    for f in sorted(os.listdir(art_dir)):
        if f.endswith(".png"):
            full = png_image(os.path.join(art_dir, f))
            if f[:-4] == "word":
                art["word_w"], art["word_h"] = full.width, full.height
            if f[:-4] == "tagline":
                art["tag_w"], art["tag_h"] = full.width, full.height
            factor, fmt = form(f[:-4], forms)
            img = shrink(full, factor) if factor > 1 else full
            name = "rw_" + f[:-4] + ".tpl"
            images[name] = img
            textures[name] = tpl(img, fmt)
    return art, textures, images


def banner_art(art_dir):
    art, textures, _ = load_art(art_dir)
    root, start, loop = scene_banner(art)
    used = {n.tex for n in root.walk() if n.tex}
    banner = u8({"arc": {"anim": {"banner_start.brlan": brlan(START, start), "banner_loop.brlan": brlan(LOOP, loop)},
                         "blyt": {"banner.brlyt": brlyt(root, used)},
                         "timg": {k: v for k, v in textures.items() if k in used}}})
    _, textures, _ = load_art(art_dir, ICON_FORMS)
    root, anim = scene_icon(art)
    used = {n.tex for n in root.walk() if n.tex}
    icon = u8({"arc": {"anim": {"icon.brlan": brlan(LOOP, anim)}, "blyt": {"icon.brlyt": brlyt(root, used)},
                       "timg": {k: v for k, v in textures.items() if k in used}}})
    if len(icon) > ICON_LIMIT or len(banner) > BANNER_LIMIT:
        sys.exit(f"channel: icon {len(icon)} bytes (at most {ICON_LIMIT}), banner {len(banner)} bytes "
                 f"(at most {BANNER_LIMIT}): the Wii Menu crashes on a channel it has no room for")
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


def main(argv):
    if len(argv) in (5, 6) and argv[1] == "build":
        forwarder = open(argv[2], "rb").read()
        banner, icon = banner_art(argv[3])
        app0 = opening(banner, icon, bns(*banner_sound()))
        blob = package([app0, forwarder])
        open(argv[4], "wb").write(blob)
        info = os.path.join(os.path.dirname(argv[4]) or ".", "riftwii_channel_info.h")
        open(info, "w").write("// Written by tools/make_channel.py.\n#pragma once\n"
                              f"#define RIFTWII_CHANNEL_TITLE 0x{TITLE_ID:016x}ull\n"
                              f"#define RIFTWII_CHANNEL_VERSION {TITLE_VERSION}u\n")
        if len(argv) == 6:
            open(os.path.join(argv[5], "00000000.app"), "wb").write(app0)
        print(f"channel: banner {len(app0)} bytes, forwarder {len(forwarder)} bytes, package {len(blob)} bytes")
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
