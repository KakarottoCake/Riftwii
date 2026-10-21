# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""sdimg.py: small edits to a FAT32 SD card image, so a big test card
(build-dolphin/user/Load/WiiSD-<card>.raw) is made once and Dolphin runs on
it with folder sync off (felk-run.sh with FELK_NOSYNC=1) instead of
rebuilding gigabytes at every boot and syncing them back at every close.

  sdimg.py <image> ls <dir>                  lists a folder ("riftwii")
  sdimg.py <image> get <path> [local]        prints a file or saves it
  sdimg.py <image> put <local> <path>        writes a file (replaced or new)
  sdimg.py <image> rm <path>                 deletes a file
  sdimg.py <image> mkdir <path>              makes a folder

Paths are from the card's root, without a leading slash (Git Bash turns
"/riftwii" into a Windows path). The folder of a new file must exist. Only for Dolphin's images: the
image must not be in use while it is edited."""
import struct
import sys


class Fat32:
    def __init__(self, path):
        self.f = open(path, "r+b")
        boot = self.read(0, 512)
        if boot[82:90] != b"FAT32   ":
            # Dolphin's images have no partition table; a card with one is not handled.
            raise SystemExit("not a FAT32 volume without a partition table")
        self.bytes_per_sector = struct.unpack_from("<H", boot, 11)[0]
        self.sectors_per_cluster = boot[13]
        reserved = struct.unpack_from("<H", boot, 14)[0]
        self.fat_count = boot[16]
        total_sectors = struct.unpack_from("<I", boot, 32)[0]
        self.fat_sectors = struct.unpack_from("<I", boot, 36)[0]
        self.root_cluster = struct.unpack_from("<I", boot, 44)[0]
        self.fat_offset = reserved * self.bytes_per_sector
        self.data_offset = (reserved + self.fat_count * self.fat_sectors) * self.bytes_per_sector
        self.cluster_bytes = self.sectors_per_cluster * self.bytes_per_sector
        data_sectors = total_sectors - reserved - self.fat_count * self.fat_sectors
        self.cluster_count = data_sectors // self.sectors_per_cluster
        self.fat = bytearray(self.read(self.fat_offset, (self.cluster_count + 2) * 4))
        self.fat_dirty = False
        self.next_free_hint = 2

    def read(self, offset, length):
        self.f.seek(offset)
        return self.f.read(length)

    def write(self, offset, data):
        self.f.seek(offset)
        self.f.write(data)

    def entry(self, cluster):
        return struct.unpack_from("<I", self.fat, cluster * 4)[0] & 0x0FFFFFFF

    def set_entry(self, cluster, value):
        old = struct.unpack_from("<I", self.fat, cluster * 4)[0]
        struct.pack_into("<I", self.fat, cluster * 4, (old & 0xF0000000) | value)
        self.fat_dirty = True

    def chain(self, first):
        clusters = []
        cluster = first
        while 2 <= cluster < 0x0FFFFFF8:
            clusters.append(cluster)
            if len(clusters) > self.cluster_count:
                raise SystemExit("a cluster chain loops")
            cluster = self.entry(cluster)
        return clusters

    def cluster_offset(self, cluster):
        return self.data_offset + (cluster - 2) * self.cluster_bytes

    def allocate(self, count):
        found = []
        cluster = self.next_free_hint
        scanned = 0
        while len(found) < count:
            if scanned > self.cluster_count:
                raise SystemExit("the image is full")
            if cluster >= self.cluster_count + 2:
                cluster = 2
            if self.entry(cluster) == 0:
                found.append(cluster)
            cluster += 1
            scanned += 1
        self.next_free_hint = cluster
        for here, after in zip(found, found[1:] + [0x0FFFFFFF]):
            self.set_entry(here, after)
        return found

    def free(self, first):
        for cluster in self.chain(first):
            self.set_entry(cluster, 0)

    def flush(self):
        if self.fat_dirty:
            for copy in range(self.fat_count):
                self.write(self.fat_offset + copy * self.fat_sectors * self.bytes_per_sector, self.fat)
        # The FS info sector's free count is only a hint; marked unknown.
        info_sector = struct.unpack_from("<H", self.read(0, 512), 48)[0]
        if info_sector not in (0, 0xFFFF):
            self.write(info_sector * self.bytes_per_sector + 488, struct.pack("<II", 0xFFFFFFFF, 0xFFFFFFFF))
        self.f.close()

    # Folders -------------------------------------------------------------

    def dir_entries(self, first):
        """(name, entry offset in the image, first slot offset, raw entry) for each file or folder."""
        long_parts = []
        first_slot = None
        result = []
        for cluster in self.chain(first):
            base = self.cluster_offset(cluster)
            data = self.read(base, self.cluster_bytes)
            for at in range(0, self.cluster_bytes, 32):
                raw = data[at:at + 32]
                if raw[0] == 0:
                    return result
                if raw[0] == 0xE5:
                    long_parts, first_slot = [], None
                    continue
                if raw[11] == 0x0F:
                    if first_slot is None:
                        first_slot = base + at
                    text = raw[1:11] + raw[14:26] + raw[28:32]
                    long_parts.insert(0, text.decode("utf-16-le", "replace"))
                    continue
                if raw[11] & 0x08:  # the volume label
                    long_parts, first_slot = [], None
                    continue
                if long_parts:
                    name = "".join(long_parts).split("\x00")[0]
                else:
                    stem = raw[0:8].decode("latin-1").rstrip()
                    ext = raw[8:11].decode("latin-1").rstrip()
                    name = stem + ("." + ext if ext else "")
                result.append((name, base + at, first_slot if first_slot is not None else base + at, raw))
                long_parts, first_slot = [], None
        return result

    def find(self, path):
        """The folder's first cluster and the entry for path (None when it is not there)."""
        parts = [p for p in path.split("/") if p]
        folder = self.root_cluster
        for depth, name in enumerate(parts):
            match = None
            for entry in self.dir_entries(folder):
                if entry[0].lower() == name.lower():
                    match = entry
                    break
            if depth == len(parts) - 1:
                return folder, match
            if match is None or not match[3][11] & 0x10:
                raise SystemExit("no folder " + "/".join(parts[:depth + 1]))
            folder = first_cluster(match[3])
        return folder, None

    def free_slots(self, folder, count):
        """Offsets of count free 32-byte slots in a row in the folder, growing it when needed."""
        run = []
        clusters = self.chain(folder)
        for cluster in clusters:
            base = self.cluster_offset(cluster)
            data = self.read(base, self.cluster_bytes)
            for at in range(0, self.cluster_bytes, 32):
                if data[at] in (0, 0xE5):
                    run.append(base + at)
                    if len(run) == count:
                        return run
                else:
                    run = []
        new = self.allocate(1)[0]
        self.set_entry(clusters[-1], new)
        self.write(self.cluster_offset(new), bytes(self.cluster_bytes))
        base = self.cluster_offset(new)
        # A run may continue from the end of the last cluster.
        while len(run) < count:
            run.append(base)
            base += 32
        return run


def first_cluster(raw):
    high, = struct.unpack_from("<H", raw, 20)
    low, = struct.unpack_from("<H", raw, 26)
    return (high << 16) | low


def short_name_for(name, taken):
    stem, _, ext = name.rpartition(".") if "." in name else (name, "", "")
    keep = lambda s: "".join(c for c in s.upper() if c.isalnum() or c in "_-")
    stem, ext = keep(stem), keep(ext)[:3]
    for n in range(1, 1000):
        tail = "~%d" % n
        candidate = (stem[:8 - len(tail)] + tail).ljust(8) + ext.ljust(3)
        if candidate.encode() not in taken:
            return candidate.encode()
    raise SystemExit("no short name left")


def lfn_checksum(short):
    total = 0
    for byte in short:
        total = (((total & 1) << 7) + (total >> 1) + byte) & 0xFF
    return total


def write_file_data(fs, data):
    if not data:
        return 0
    count = (len(data) + fs.cluster_bytes - 1) // fs.cluster_bytes
    clusters = fs.allocate(count)
    for i, cluster in enumerate(clusters):
        piece = data[i * fs.cluster_bytes:(i + 1) * fs.cluster_bytes]
        fs.write(fs.cluster_offset(cluster), piece.ljust(fs.cluster_bytes, b"\x00"))
    return clusters[0]


def put(fs, local, path):
    data = open(local, "rb").read()
    folder, entry = fs.find(path)
    name = path.rstrip("/").split("/")[-1]
    if entry is not None:
        raw = bytearray(entry[3])
        if raw[11] & 0x10:
            raise SystemExit(path + " is a folder")
        old = first_cluster(raw)
        if old:
            fs.free(old)
        start = write_file_data(fs, data)
        struct.pack_into("<H", raw, 20, start >> 16)
        struct.pack_into("<H", raw, 26, start & 0xFFFF)
        struct.pack_into("<I", raw, 28, len(data))
        fs.write(entry[1], bytes(raw))
        return
    taken = {e[3][0:11] for e in fs.dir_entries(folder)}
    short = short_name_for(name, taken)
    units = name.encode("utf-16-le")
    pieces = [units[i:i + 26] for i in range(0, len(units), 26)]
    if len(pieces[-1]) < 26:
        pieces[-1] = (pieces[-1] + b"\x00\x00").ljust(26, b"\xff")
    slots = fs.free_slots(folder, len(pieces) + 1)
    checksum = lfn_checksum(short)
    for index, piece in enumerate(pieces):
        order = index + 1
        if order == len(pieces):
            order |= 0x40
        slot = bytearray(32)
        slot[0] = order
        slot[1:11] = piece[0:10]
        slot[11] = 0x0F
        slot[13] = checksum
        slot[14:26] = piece[10:22]
        slot[28:32] = piece[22:26]
        # The last piece of the name comes first in the folder.
        fs.write(slots[len(pieces) - 1 - index], bytes(slot))
    start = write_file_data(fs, data)
    raw = bytearray(32)
    raw[0:11] = short
    raw[11] = 0x20  # archive
    struct.pack_into("<H", raw, 20, start >> 16)
    struct.pack_into("<H", raw, 26, start & 0xFFFF)
    struct.pack_into("<I", raw, 28, len(data))
    fs.write(slots[-1], bytes(raw))


def remove(fs, path):
    _, entry = fs.find(path)
    if entry is None:
        raise SystemExit("no file " + path)
    if entry[3][11] & 0x10:
        raise SystemExit(path + " is a folder")
    if first_cluster(entry[3]):
        fs.free(first_cluster(entry[3]))
    for at in range(entry[2], entry[1] + 32, 32):
        fs.write(at, b"\xe5")


def make_dir(fs, path):
    folder, entry = fs.find(path.rstrip("/"))
    if entry is not None:
        raise SystemExit(path + " exists")
    name = path.rstrip("/").split("/")[-1]
    if not name:
        raise SystemExit("no folder " + path)
    taken = {e[3][0:11] for e in fs.dir_entries(folder)}
    short = short_name_for(name, taken)
    start = fs.allocate(1)[0]
    dot = bytearray(32)
    dot[0:11] = b".          "
    dot[11] = 0x10
    struct.pack_into("<H", dot, 20, start >> 16)
    struct.pack_into("<H", dot, 26, start & 0xFFFF)
    dotdot = bytearray(32)
    dotdot[0:11] = b"..         "
    dotdot[11] = 0x10
    struct.pack_into("<H", dotdot, 20, folder >> 16)
    struct.pack_into("<H", dotdot, 26, folder & 0xFFFF)
    fs.write(fs.cluster_offset(start), bytes(dot) + bytes(dotdot) + bytes(fs.cluster_bytes - 64))
    units = name.encode("utf-16-le")
    pieces = [units[i:i + 26] for i in range(0, len(units), 26)]
    if len(pieces[-1]) < 26:
        pieces[-1] = (pieces[-1] + b"\x00\x00").ljust(26, b"\xff")
    slots = fs.free_slots(folder, len(pieces) + 1)
    checksum = lfn_checksum(short)
    for index, piece in enumerate(pieces):
        order = index + 1
        if order == len(pieces):
            order |= 0x40
        slot = bytearray(32)
        slot[0] = order
        slot[1:11] = piece[0:10]
        slot[11] = 0x0F
        slot[13] = checksum
        slot[14:26] = piece[10:22]
        slot[28:32] = piece[22:26]
        # The last piece of the name comes first in the folder.
        fs.write(slots[len(pieces) - 1 - index], bytes(slot))
    raw = bytearray(32)
    raw[0:11] = short
    raw[11] = 0x10
    struct.pack_into("<H", raw, 20, start >> 16)
    struct.pack_into("<H", raw, 26, start & 0xFFFF)
    fs.write(slots[-1], bytes(raw))


def main():
    if len(sys.argv) < 4:
        raise SystemExit(__doc__)
    fs = Fat32(sys.argv[1])
    command = sys.argv[2]
    if command == "ls":
        folder = fs.root_cluster
        if sys.argv[3].strip("/"):
            _, entry = fs.find(sys.argv[3])
            if entry is None:
                raise SystemExit("no folder " + sys.argv[3])
            folder = first_cluster(entry[3])
        for name, _, _, raw in fs.dir_entries(folder):
            kind = "<dir>" if raw[11] & 0x10 else str(struct.unpack_from("<I", raw, 28)[0])
            print("%10s  %s" % (kind, name))
    elif command == "get":
        _, entry = fs.find(sys.argv[3])
        if entry is None:
            raise SystemExit("no file " + sys.argv[3])
        size = struct.unpack_from("<I", entry[3], 28)[0]
        data = b"".join(fs.read(fs.cluster_offset(c), fs.cluster_bytes) for c in fs.chain(first_cluster(entry[3])))
        data = data[:size]
        if len(sys.argv) > 4:
            open(sys.argv[4], "wb").write(data)
        else:
            sys.stdout.buffer.write(data)
    elif command == "put":
        put(fs, sys.argv[3], sys.argv[4])
    elif command == "rm":
        remove(fs, sys.argv[3])
    elif command == "mkdir":
        make_dir(fs, sys.argv[3])
    else:
        raise SystemExit(__doc__)
    fs.flush()


if __name__ == "__main__":
    main()
