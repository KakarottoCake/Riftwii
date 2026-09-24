#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds the RVZ test fixtures in tests/fixtures/rvz/.

A small Wii disc is made from scratch (no Nintendo data): a disc header,
a partition table, one data partition with a ticket, TMD, H3 table and
192 encrypted, correctly hashed clusters holding a boot area, an FST, three
files and Wii-style padding between them, and more padding after the
partition. DolphinTool then converts it to the RVZ and WIA variants the
tests read. `manifest.txt` records a SHA-1 for every 0x8000 bytes of what
a reader must present: the disc outside partition data, and the partition
data decrypted and without hashes.

The title key comes from the ticket's encrypted key and the console's
common key, which this script does not have. So the disc is built twice:
the first conversion reveals the key (RVZ stores it), the second disc is
encrypted with it.

Needs pycryptodome and DolphinTool:
  python tools/rvz/make_test_disc.py <DolphinTool.exe>
"""
import hashlib
import os
import random
import struct
import subprocess
import sys
import tempfile

from Crypto.Cipher import AES

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, '..', '..', 'tests', 'fixtures', 'rvz'))

GAME_ID = b'RVZT01'
PART = 0x50000            # partition header on the disc
DATA = 0x20000            # partition data, from the partition header
CLUSTERS = 192            # three 2 MiB hash groups
PLAIN = 0x7C00
TAIL = 0x80000            # raw padding after the partition
DISC_SIZE = PART + DATA + CLUSTERS * 0x8000 + TAIL

# Files in the partition's data space (decrypted, without hashes).
FILES = [
    (b'a.bin', 0x10000, 0x9000),
    (b'b.bin', 0x1F0000, 0x18000),   # crosses the 2 MiB boundary of the first group
    (b'c.bin', 0x3A1234 & ~3, 0x20000),
]
FST_AT = 0x8000
JUNK = [                  # (start, end) in data space
    (0x20000, 0x1F0000),
    (0x208000, 0x3A1234 & ~3),
    (0x3C1234 & ~3, CLUSTERS * PLAIN),
]
RAW_JUNK = (PART + DATA + CLUSTERS * 0x8000, DISC_SIZE)
BAD_HASH_CLUSTER = 70     # its first H0 entry is wrong on purpose


def lfg_words(seed):
    """Wii padding generator: xor lagged Fibonacci, j = 32, k = 521."""
    buf = list(seed) + [0] * (521 - 17)
    for i in range(17, 521):
        buf[i] = ((buf[i - 17] << 23) ^ (buf[i - 16] >> 9) ^ buf[i - 1]) & 0xFFFFFFFF

    def advance():
        for i in range(32):
            buf[i] ^= buf[i + 521 - 32]
        for i in range(32, 521):
            buf[i] ^= buf[i - 32]

    for _ in range(4):
        advance()
    while True:
        for w in buf:
            yield w
        advance()


def junk(seed, length):
    out = bytearray()
    for w in lfg_words(seed):
        out += bytes(((w >> 24) & 0xFF, (w >> 18) & 0xFF, (w >> 8) & 0xFF, w & 0xFF))
        if len(out) >= length:
            return bytes(out[:length])


def fill_junk(buf, base, start, end, rng):
    """Padding as discs have it: a fresh seed every 0x8000 bytes."""
    pos = start
    while pos < end:
        block = pos - pos % 0x8000
        stop = min(end, block + 0x8000)
        seed = [rng.getrandbits(32) for _ in range(17)]
        stream = junk(seed, stop - block)
        buf[pos - base:stop - base] = stream[pos - block:]
        pos = stop


def pattern(name, length):
    """File bytes: compressible but not trivially so."""
    h = hashlib.sha1(name).digest()
    return bytes((h[i % 20] + (i >> 5) + (i * 7 >> 11)) & 0xFF for i in range(length))


def build_plain(rng):
    data = bytearray(CLUSTERS * PLAIN)
    boot = bytearray(0x440)
    boot[0:6] = GAME_ID
    boot[0x18:0x1C] = struct.pack('>I', 0x5D1C9EA3)
    boot[0x20:0x40] = b'RiftWii RVZ test disc'.ljust(0x20, b'\0')
    names = b''.join(n + b'\0' for n, _, _ in FILES)
    fst = bytearray(12 * (len(FILES) + 1))
    fst[0:12] = struct.pack('>III', 0x01000000, 0, len(FILES) + 1)
    at = 0
    for i, (name, off, size) in enumerate(FILES, 1):
        fst[12 * i:12 * i + 12] = struct.pack('>III', at, off >> 2, size)
        at += len(name) + 1
    fst += names
    fst_size = (len(fst) + 3) & ~3
    boot[0x420:0x430] = struct.pack('>IIII', 0x3000 >> 2, FST_AT >> 2, fst_size >> 2, fst_size >> 2)
    data[0:0x440] = boot
    app = bytearray(0x120)
    app[0:10] = b'2026/09/23'
    app[0x10:0x1C] = struct.pack('>III', 0x81200000, 0x100, 0)
    data[0x2440:0x2440 + len(app)] = app
    dol = bytearray(0x200)
    dol[0:4] = struct.pack('>I', 0x100)
    dol[0x48:0x4C] = struct.pack('>I', 0x80004000)
    dol[0x90:0x94] = struct.pack('>I', 0x100)
    data[0x3000:0x3200] = dol
    data[FST_AT:FST_AT + len(fst)] = fst
    for name, off, size in FILES:
        data[off:off + size] = pattern(name, size)
    for start, end in JUNK:
        fill_junk(data, 0, start, end, rng)
    return bytes(data)


def sha1(b):
    return hashlib.sha1(b).digest()


def encrypt_partition(plain, key):
    """Hashes (H0..H3) and encryption, as on a real disc."""
    out = bytearray()
    h3 = bytearray(0x18000)
    for g in range(CLUSTERS // 64):
        clusters = [plain[(g * 64 + c) * PLAIN:(g * 64 + c + 1) * PLAIN] for c in range(64)]
        h0 = [b''.join(sha1(cl[i * 0x400:(i + 1) * 0x400]) for i in range(31)) for cl in clusters]
        if g == BAD_HASH_CLUSTER // 64:
            bad = BAD_HASH_CLUSTER % 64
            h0[bad] = bytes(20 * [0xEE]) + h0[bad][20:]
        h1 = [b''.join(sha1(h0[s * 8 + c]) for c in range(8)) for s in range(8)]
        h2 = b''.join(sha1(t) for t in h1)
        h3[g * 20:(g + 1) * 20] = sha1(h2)
        for c in range(64):
            hb = (h0[c] + bytes(0x14) + h1[c // 8] + bytes(0x20) + h2 + bytes(0x20))
            assert len(hb) == 0x400
            enc_h = AES.new(key, AES.MODE_CBC, bytes(16)).encrypt(hb)
            enc_d = AES.new(key, AES.MODE_CBC, enc_h[0x3D0:0x3E0]).encrypt(clusters[c])
            out += enc_h + enc_d
    return bytes(out), bytes(h3)


def build_disc(key, plain, rng_tail):
    disc = bytearray(DISC_SIZE)
    disc[0:6] = GAME_ID
    disc[0x18:0x1C] = struct.pack('>I', 0x5D1C9EA3)
    disc[0x20:0x40] = b'RiftWii RVZ test disc'.ljust(0x20, b'\0')
    disc[0x40000:0x40008] = struct.pack('>II', 1, 0x40020 >> 2)
    disc[0x40020:0x40028] = struct.pack('>II', PART >> 2, 0)
    disc[0x4E000:0x4E004] = struct.pack('>I', 1)   # region: USA
    enc, h3 = encrypt_partition(plain, key)
    title_id = struct.pack('>II', 0x00010000, struct.unpack('>I', GAME_ID[:4])[0])
    ticket = bytearray(0x2A4)
    ticket[0:4] = struct.pack('>I', 0x10001)
    ticket[0x140:0x140 + 26] = b'Root-CA00000001-XS00000003'
    ticket[0x1BF:0x1CF] = bytes(range(0x40, 0x50))   # encrypted title key, any value
    ticket[0x1DC:0x1E4] = title_id
    ticket[0x1E4:0x1E6] = b'\xff\xff'
    tmd = bytearray(0x1E4 + 0x24)
    tmd[0:4] = struct.pack('>I', 0x10001)
    tmd[0x140:0x140 + 26] = b'Root-CA00000001-CP00000004'
    tmd[0x184:0x18C] = struct.pack('>Q', 0x000000010000003A)
    tmd[0x18C:0x194] = title_id
    tmd[0x194:0x198] = struct.pack('>I', 1)
    tmd[0x1DE:0x1E2] = struct.pack('>HH', 1, 0)
    tmd[0x1E4:0x208] = struct.pack('>IHHQ', 0, 0, 3, CLUSTERS * 0x8000) + sha1(h3)
    cert = bytes(0x100)
    hdr = bytearray(DATA)
    hdr[0:0x2A4] = ticket
    hdr[0x2A4:0x2C0] = struct.pack('>IIIIIII', len(tmd), 0x2C0 >> 2, len(cert), 0x500 >> 2,
                                   0x8000 >> 2, DATA >> 2, (CLUSTERS * 0x8000) >> 2)
    hdr[0x2C0:0x2C0 + len(tmd)] = tmd
    hdr[0x500:0x500 + len(cert)] = cert
    hdr[0x8000:0x20000] = h3
    disc[PART:PART + DATA] = hdr
    disc[PART + DATA:PART + DATA + len(enc)] = enc
    fill_junk(disc, 0, RAW_JUNK[0], RAW_JUNK[1], rng_tail)
    return bytes(disc)


def convert(tool, user, iso, out, fmt, method, level, block):
    if os.path.exists(out):
        os.remove(out)
    cmd = [tool, 'convert', '-u', user, '-i', iso, '-o', out, '-f', fmt, '-b', str(block), '-c', method]
    if method != 'none':
        cmd += ['-l', str(level)]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)


def rvz_title_key(path):
    with open(path, 'rb') as f:
        head = f.read(0x48 + 0xDC)
        part_off = struct.unpack('>Q', head[0x48 + 0x98:0x48 + 0xA0])[0]
        f.seek(part_off)
        return f.read(16)


VARIANTS = [
    # file, format, method, level, chunk
    ('zstd-128k-l5.rvz', 'rvz', 'zstd', 5, 0x20000),
    ('zstd-32k-l19.rvz', 'rvz', 'zstd', 19, 0x8000),
    ('zstd-256k-l22.rvz', 'rvz', 'zstd', 22, 0x40000),
    ('zstd-2m-l3.rvz', 'rvz', 'zstd', 3, 0x200000),
    ('none-128k.rvz', 'rvz', 'none', 0, 0x20000),
    ('lzma-128k.rvz', 'rvz', 'lzma', 5, 0x20000),
    ('lzma2-2m.wia', 'wia', 'lzma2', 5, 0x200000),
]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    tool = sys.argv[1]
    os.makedirs(OUT, exist_ok=True)
    plain = build_plain(random.Random(1137))
    with tempfile.TemporaryDirectory() as tmp:
        user = os.path.join(tmp, 'user')
        iso = os.path.join(tmp, 'disc.iso')
        probe = os.path.join(tmp, 'probe.rvz')
        with open(iso, 'wb') as f:
            f.write(build_disc(bytes(16), plain, random.Random(2)))
        convert(tool, user, iso, probe, 'rvz', 'none', 0, 0x20000)
        key = rvz_title_key(probe)
        disc = build_disc(key, plain, random.Random(2))
        with open(iso, 'wb') as f:
            f.write(disc)
        for name, fmt, method, level, block in VARIANTS:
            convert(tool, user, iso, os.path.join(OUT, name), fmt, method, level, block)
            assert rvz_title_key(os.path.join(OUT, name)) == key
            if fmt == 'wia':
                # WIA cannot pack padding, so the file is as large as the
                # disc; the tests only read its headers.
                with open(os.path.join(OUT, name), 'r+b') as f:
                    f.truncate(0x1000)
    part_data = PART + DATA
    part_end = part_data + CLUSTERS * 0x8000
    lines = [
        '# RiftWii RVZ fixtures (tools/rvz/make_test_disc.py). SHA-1 per 0x8000 bytes.',
        'disc_size %x' % DISC_SIZE,
        'partition_data %x %x' % (part_data, CLUSTERS * PLAIN),
        'bad_hash_cluster %d' % BAD_HASH_CLUSTER,
    ]
    for name, off, size in FILES:
        lines.append('file %s %x %x' % (name.decode(), off, size))
    for off in range(0, DISC_SIZE, 0x8000):
        if part_data <= off < part_end:
            continue
        lines.append('raw %x %s' % (off, sha1(disc[off:off + 0x8000]).hex()))
    for off in range(0, len(plain), 0x8000):
        lines.append('part %x %s' % (off, sha1(plain[off:off + 0x8000]).hex()))
    with open(os.path.join(OUT, 'manifest.txt'), 'w', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    for name, *_ in VARIANTS:
        print('%-20s %8d bytes' % (name, os.path.getsize(os.path.join(OUT, name))))


if __name__ == '__main__':
    main()
