// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// GameCube RVZ: a synthetic GameCube disc written as an RVZ here (groups
// of zeros, stored as they are, Zstandard frames, and RVZ-packed padding,
// both plain and compressed), then read back through check_rvz,
// RvzDiscSource and the GameCube blob's reader (runtime/gc/gc_rvz.c, built
// for the host) and compared byte for byte with the disc it came from.
//
// The Zstandard frames are written by hand from raw and RLE blocks: the
// repository carries Zstandard's decoder only, and the frame format is
// what the readers have to get right.
#include "riftwii/rvz.hpp"
#include "riftwii/sha1.hpp"
#include "riftwii/source.hpp"

#include "gc_rvz.h"
#include "rtrvz.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

namespace {

using Bytes = std::vector<std::uint8_t>;

void put32(Bytes& b, std::size_t at, std::uint32_t v) {
    b[at] = static_cast<std::uint8_t>(v >> 24);
    b[at + 1] = static_cast<std::uint8_t>(v >> 16);
    b[at + 2] = static_cast<std::uint8_t>(v >> 8);
    b[at + 3] = static_cast<std::uint8_t>(v);
}

void put64(Bytes& b, std::size_t at, std::uint64_t v) {
    put32(b, at, static_cast<std::uint32_t>(v >> 32));
    put32(b, at + 4, static_cast<std::uint32_t>(v));
}

void append32(Bytes& b, std::uint32_t v) {
    b.resize(b.size() + 4);
    put32(b, b.size() - 4, v);
}

// A Zstandard frame (RFC 8878) holding `data` in raw blocks, and RLE
// blocks where a block is one byte repeated: single segment, the content
// size in 4 bytes, no checksum.
Bytes zstd_frame(const Bytes& data) {
    Bytes f = {0x28, 0xB5, 0x2F, 0xFD, 0xA0};
    const auto size = static_cast<std::uint32_t>(data.size());
    for (int i = 0; i < 4; ++i) f.push_back(static_cast<std::uint8_t>(size >> (8 * i)));
    constexpr std::size_t kBlock = 0x10000;
    std::size_t at = 0;
    do {
        const std::size_t n = std::min(kBlock, data.size() - at);
        const bool last = at + n == data.size();
        const bool rle = n > 0 && std::all_of(data.begin() + at, data.begin() + at + n,
                                              [&](std::uint8_t c) { return c == data[at]; });
        const std::uint32_t header = (last ? 1u : 0u) | ((rle ? 1u : 0u) << 1) | (static_cast<std::uint32_t>(n) << 3);
        for (int i = 0; i < 3; ++i) f.push_back(static_cast<std::uint8_t>(header >> (8 * i)));
        if (rle) {
            f.push_back(data[at]);
        } else {
            f.insert(f.end(), data.begin() + at, data.begin() + at + n);
        }
        at += n;
    } while (at < data.size());
    return f;
}

// How one chunk of the disc is made and stored.
enum class Kind {
    Zero,        // all zeros: a group of size 0
    Stored,      // incompressible bytes, stored as they are
    Zstd,        // compressible bytes in a Zstandard frame
    Packed,      // data, a run of padding, data: RVZ-packed, stored as it is
    PackedZstd,  // the same, the packed stream in a Zstandard frame
};

struct Seed {
    std::uint8_t bytes[RTRVZ_SEED_BYTES];
};

// The padding generator's bytes for a run at `disc_offset`, seeded as RVZ
// packing records it: from the 0x8000 boundary at or before the run.
Bytes junk(const Seed& seed, std::uint64_t disc_offset, std::uint32_t length) {
    rtrvz_junk j;
    rtrvz_junk_seed(&j, seed.bytes);
    rtrvz_junk_skip(&j, static_cast<std::uint32_t>(disc_offset % 0x8000));
    Bytes out(length);
    rtrvz_junk_out(&j, out.data(), length);
    return out;
}

struct Disc {
    Bytes iso;
    Bytes rvz;
};

std::uint32_t g_noise = 0x12345678;
std::uint8_t noise() {
    g_noise = g_noise * 1103515245u + 12345u;
    return static_cast<std::uint8_t>(g_noise >> 16);
}

// One chunk's disc bytes and, for a packed one, its packed stream.
void make_chunk(Kind kind, std::uint64_t start, std::uint32_t length, Bytes& disc, Bytes& packed) {
    disc.assign(length, 0);
    packed.clear();
    if (kind == Kind::Zero) return;
    if (kind == Kind::Stored) {
        for (auto& c : disc) c = noise();
        return;
    }
    if (kind == Kind::Zstd) {
        // Long runs of one byte (RLE blocks) and some noise (raw ones).
        for (std::uint32_t i = 0; i < length; ++i) disc[i] = (i / 0x9000) % 2 ? noise() : static_cast<std::uint8_t>(i >> 12);
        return;
    }
    // Packed: data, then padding from an odd place that crosses a 0x8000
    // boundary, then data to the chunk's end.
    const std::uint32_t head = std::min<std::uint32_t>(length, 0x1234 + static_cast<std::uint32_t>(start % 0x777));
    const std::uint32_t pad = std::min<std::uint32_t>(length - head, 0x9000);
    const std::uint32_t tail = length - head - pad;
    for (std::uint32_t i = 0; i < head; ++i) disc[i] = noise();
    Seed seed;
    for (auto& c : seed.bytes) c = noise();
    const Bytes padding = junk(seed, start + head, pad);
    std::copy(padding.begin(), padding.end(), disc.begin() + head);
    for (std::uint32_t i = 0; i < tail; ++i) disc[head + pad + i] = noise();
    append32(packed, head);
    packed.insert(packed.end(), disc.begin(), disc.begin() + head);
    if (pad != 0) {
        append32(packed, 0x80000000u | pad);
        packed.insert(packed.end(), seed.bytes, seed.bytes + RTRVZ_SEED_BYTES);
    }
    if (tail != 0) {
        append32(packed, tail);
        packed.insert(packed.end(), disc.begin() + head + pad, disc.end());
    }
}

// A GameCube disc of `size` bytes in `chunk`-byte groups, the kinds in
// turn, written as an RVZ with Zstandard (`zstd`) or no compression.
Disc make_disc(std::uint32_t size, std::uint32_t chunk, bool zstd, const std::vector<Kind>& kinds) {
    Disc d;
    const std::uint32_t groups = (size + chunk - 1) / chunk;
    Bytes group_table, group_data;
    for (std::uint32_t g = 0; g < groups; ++g) {
        const std::uint64_t start = std::uint64_t(g) * chunk;
        const auto length = static_cast<std::uint32_t>(std::min<std::uint64_t>(chunk, size - start));
        Kind kind = kinds[g % kinds.size()];
        if (!zstd && kind == Kind::Zstd) kind = Kind::Stored;
        if (!zstd && kind == Kind::PackedZstd) kind = Kind::Packed;
        Bytes disc, packed;
        make_chunk(kind, start, length, disc, packed);
        d.iso.insert(d.iso.end(), disc.begin(), disc.end());
        Bytes stored;
        std::uint32_t flags = 0;
        std::uint32_t packed_size = 0;
        if (kind == Kind::Stored) {
            stored = disc;
        } else if (kind == Kind::Zstd) {
            stored = zstd_frame(disc);
            flags = 0x80000000u;
        } else if (kind == Kind::Packed) {
            stored = packed;
            packed_size = static_cast<std::uint32_t>(packed.size());
        } else if (kind == Kind::PackedZstd) {
            stored = zstd_frame(packed);
            flags = 0x80000000u;
            packed_size = static_cast<std::uint32_t>(packed.size());
        }
        while (group_data.size() % 4 != 0) group_data.push_back(0);
        const std::size_t at = group_data.size();
        group_data.insert(group_data.end(), stored.begin(), stored.end());
        group_table.resize(group_table.size() + 12);
        // The offset is filled in below, once the tables' size is known.
        put32(group_table, group_table.size() - 12, static_cast<std::uint32_t>(at));
        put32(group_table, group_table.size() - 8, static_cast<std::uint32_t>(stored.size()) | flags);
        put32(group_table, group_table.size() - 4, packed_size);
    }
    // The disc's header: an ID, the GameCube magic and a title.
    std::memcpy(d.iso.data(), "GRVZ01", 6);
    put32(d.iso, 0x1C, 0xC2339F3Du);
    std::memcpy(d.iso.data() + 0x20, "RVZ test disc", 13);

    Bytes raw_table(24);
    put64(raw_table, 0, 0x80);
    put64(raw_table, 8, size - 0x80);
    put32(raw_table, 16, 0);
    put32(raw_table, 20, groups);
    const Bytes raw_stored = zstd ? zstd_frame(raw_table) : raw_table;

    constexpr std::size_t kHead = 0x48;
    constexpr std::size_t kDisc = 0xDC;
    const std::size_t raw_at = kHead + kDisc;
    const std::size_t group_table_at = raw_at + raw_stored.size();
    // The groups follow the group table, whose entries hold their offsets:
    // its stored size is the same whatever offsets it holds (one raw block,
    // or the table itself), so it is measured first.
    const std::size_t data_at =
        (group_table_at + (zstd ? zstd_frame(group_table) : group_table).size() + 3) & ~std::size_t(3);
    for (std::uint32_t g = 0; g < groups; ++g) {
        const std::uint32_t local = (std::uint32_t(group_table[12 * g]) << 24) |
                                    (std::uint32_t(group_table[12 * g + 1]) << 16) |
                                    (std::uint32_t(group_table[12 * g + 2]) << 8) | group_table[12 * g + 3];
        put32(group_table, 12 * g, static_cast<std::uint32_t>((data_at + local) / 4));
    }
    const Bytes group_stored = zstd ? zstd_frame(group_table) : group_table;
    Bytes& f = d.rvz;
    f.assign(data_at, 0);
    f.insert(f.end(), group_data.begin(), group_data.end());
    std::copy(raw_stored.begin(), raw_stored.end(), f.begin() + raw_at);
    std::copy(group_stored.begin(), group_stored.end(), f.begin() + group_table_at);

    Bytes disc(kDisc, 0);
    put32(disc, 0, 1);  // GameCube
    put32(disc, 4, zstd ? 5 : 0);
    put32(disc, 8, zstd ? 19 : 0);
    put32(disc, 0x0C, chunk);
    std::copy(d.iso.begin(), d.iso.begin() + 0x80, disc.begin() + 0x10);
    put32(disc, 0xB4, 1);
    put64(disc, 0xB8, raw_at);
    put32(disc, 0xC0, static_cast<std::uint32_t>(raw_stored.size()));
    put32(disc, 0xC4, groups);
    put64(disc, 0xC8, group_table_at);
    put32(disc, 0xD0, static_cast<std::uint32_t>(group_stored.size()));
    std::copy(disc.begin(), disc.end(), f.begin() + kHead);

    std::memcpy(f.data(), "RVZ\x01", 4);
    put32(f, 4, 0x01000000);
    put32(f, 8, 0x00030000);
    put32(f, 0x0C, kDisc);
    const Sha1Digest disc_hash = sha1(disc.data(), disc.size());
    std::copy(disc_hash.begin(), disc_hash.end(), f.begin() + 0x10);
    put64(f, 0x24, size);
    put64(f, 0x2C, f.size());
    const Sha1Digest head_hash = sha1(f.data(), 0x34);
    std::copy(head_hash.begin(), head_hash.end(), f.begin() + 0x34);
    return d;
}

const std::vector<Kind> kAllKinds = {Kind::Stored, Kind::Zstd, Kind::Zero, Kind::Packed, Kind::PackedZstd,
                                     Kind::Zstd, Kind::Packed};

void TestVerdicts() {
    const Disc d = make_disc(40 * 0x8000 + 0x1800, 0x8000, true, kAllKinds);
    RvzHead head;
    std::string error;
    EXPECT_TRUE(read_rvz_head(MemorySource(d.rvz), head, error));
    EXPECT_EQ(head.disc_type, 1u);
    EXPECT_TRUE(head.partitions.empty());
    RvzVerdict v = check_rvz(head, d.rvz.size());
    EXPECT_TRUE(v.support == RvzSupport::Supported);
    EXPECT_TRUE(v.reasons.empty());
    // The same limits as a Wii game's.
    RvzHead h = head;
    h.chunk_size = 256 * 1024;
    EXPECT_TRUE(check_rvz(h, d.rvz.size()).support == RvzSupport::AtOwnRisk);
    h.chunk_size = 1024 * 1024;
    EXPECT_TRUE(check_rvz(h, d.rvz.size()).support == RvzSupport::Unsupported);
    h = head;
    h.method = 3;
    EXPECT_TRUE(check_rvz(h, d.rvz.size()).support == RvzSupport::Unsupported);
    h = head;
    h.partitions.resize(1);
    v = check_rvz(h, d.rvz.size());
    EXPECT_TRUE(v.support == RvzSupport::Unsupported);
    EXPECT_TRUE(!v.reasons.empty() && v.reasons[0].find("GameCube") != std::string::npos);
    h = head;
    h.disc_type = 3;
    EXPECT_TRUE(check_rvz(h, d.rvz.size()).support == RvzSupport::Unsupported);
    EXPECT_TRUE(check_rvz(head, d.rvz.size() - 1).support == RvzSupport::Unsupported);
}

// The whole disc through RvzDiscSource, in pieces of awkward sizes.
void CheckDiscSource(const Disc& d) {
    std::unique_ptr<RvzDiscSource> disc;
    std::string error;
    EXPECT_TRUE(RvzDiscSource::open(std::make_shared<MemorySource>(d.rvz), disc, error));
    if (!disc) {
        std::cerr << "  " << error << std::endl;
        return;
    }
    EXPECT_EQ(disc->size(), d.iso.size());
    Bytes out(d.iso.size(), 0xEE);
    for (std::size_t at = 0; at < out.size();) {
        const std::size_t n = std::min<std::size_t>(out.size() - at, 0x3001 + at % 0x1777);
        EXPECT_TRUE(disc->read(at, out.data() + at, n));
        at += n;
    }
    EXPECT_TRUE(out == d.iso);
    std::uint8_t byte = 0;
    EXPECT_FALSE(disc->read(d.iso.size(), &byte, 1));
}

// The card for gc_rvz.c: 512-byte sectors, the files laid out in pieces.
struct Card {
    Bytes sectors;
    Bytes usb_sectors;  // the USB drive, for a game on it (gc_rvz's extents_on_usb)
    std::uint32_t requests = 0;
    std::uint32_t usb_requests = 0;
    std::uint32_t largest = 0;
    bool fail = false;
};

// Puts `file` on a device (the card's or the USB drive's sectors) in
// pieces of `piece` sectors with a gap of one sector between them, and
// returns the pieces.
std::vector<gc_rvz_extent> place(Bytes& device, const Bytes& file, std::uint32_t piece) {
    std::vector<gc_rvz_extent> out;
    const std::uint32_t sectors = static_cast<std::uint32_t>((file.size() + 511) / 512);
    for (std::uint32_t s = 0; s < sectors; s += piece) {
        const std::uint32_t n = std::min(piece, sectors - s);
        const std::uint32_t at = static_cast<std::uint32_t>(device.size() / 512) + 1;
        device.resize((std::size_t(at) + n) * 512, 0xCC);
        const std::size_t bytes = std::min<std::size_t>(std::size_t(n) * 512, file.size() - std::size_t(s) * 512);
        std::copy(file.begin() + std::size_t(s) * 512, file.begin() + std::size_t(s) * 512 + bytes,
                  device.begin() + std::size_t(at) * 512);
        out.push_back(gc_rvz_extent{s, at, n});
    }
    return out;
}

struct Aligned {
    explicit Aligned(std::size_t bytes) : storage(bytes + 64) {}
    std::uintptr_t address() {
        const auto p = reinterpret_cast<std::uintptr_t>(storage.data());
        return (p + 63) & ~std::uintptr_t(63);
    }
    Bytes storage;
};

}  // namespace

extern "C" int gc_rvz_device_read(void* io, std::uint32_t usb, std::uint32_t sector, std::uint32_t count,
                                  std::uintptr_t target) {
    Card& card = *static_cast<Card*>(io);
    const Bytes& device = usb ? card.usb_sectors : card.sectors;
    card.requests++;
    if (usb) card.usb_requests++;
    card.largest = std::max(card.largest, count);
    if (card.fail || target % 32 != 0 || (std::size_t(sector) + count) * 512 > device.size()) return 0;
    std::memcpy(reinterpret_cast<void*>(target), device.data() + std::size_t(sector) * 512, std::size_t(count) * 512);
    return 1;
}

namespace {

// The disc through the GameCube blob's reader, set up as the loader does
// (plugins/gamecube/gcnative.cpp): the group table file on the card, and
// the RVZ on the card or, for `on_usb`, on the USB drive.
void CheckBlobReader(const Disc& d, const char* name, bool on_usb) {
    std::unique_ptr<RvzDiscSource> disc;
    std::string error;
    EXPECT_TRUE(RvzDiscSource::open(std::make_shared<MemorySource>(d.rvz), disc, error));
    if (!disc) return;
    RvzRuntimeTable table;
    EXPECT_TRUE(disc->image().disc_runtime_table(table, error));
    const std::uint32_t chunk = disc->image().head().chunk_size;
    EXPECT_EQ(table.group_count, static_cast<std::uint32_t>((d.iso.size() + chunk - 1) / chunk));
    EXPECT_EQ(table.entries.size(), std::size_t(table.group_count) * 8);
    const RvzBufferSizes sizes = rvz_buffer_sizes(table);

    Card card;
    card.sectors.resize(512 * 3, 0xCC);
    card.usb_sectors.resize(512 * 5, 0xDD);
    const std::vector<gc_rvz_extent> table_pieces = place(card.sectors, table.entries, 1);
    const std::vector<gc_rvz_extent> file_pieces = place(on_usb ? card.usb_sectors : card.sectors, d.rvz, 37);
    EXPECT_TRUE(table_pieces.size() <= GC_RVZ_TABLE_EXTENTS);
    EXPECT_TRUE(file_pieces.size() <= GC_RVZ_MAX_EXTENTS);

    std::unique_ptr<gc_rvz> rvz(new gc_rvz());
    std::memset(rvz.get(), 0, sizeof(gc_rvz));
    Aligned stored(sizes.stored), slot0(sizes.group), slot1(sizes.group), dctx(1u << 20);
    rvz->magic = GC_RVZ_MAGIC;
    rvz->disc_bytes = static_cast<std::uint32_t>(d.iso.size());
    rvz->chunk_bytes = chunk;
    rvz->group_count = table.group_count;
    rvz->stored_buffer = sizes.stored ? stored.address() : 0;
    rvz->stored_bytes = sizes.stored;
    rvz->dctx_buffer = sizes.decoder ? dctx.address() : 0;
    rvz->dctx_bytes = sizes.decoder ? 1u << 20 : 0;
    rvz->slots[0].buffer = slot0.address();
    rvz->slots[1].buffer = slot1.address();
    rvz->slots[0].buffer_bytes = rvz->slots[1].buffer_bytes = sizes.group;
    rvz->slots[0].group = rvz->slots[1].group = GC_RVZ_NO_GROUP;
    rvz->entry_sector = GC_RVZ_NO_GROUP;
    std::memcpy(rvz->header, disc->image().head().disc_header.data(), GC_RVZ_HEADER_BYTES);
    rvz->table_extent_count = static_cast<std::uint32_t>(table_pieces.size());
    std::copy(table_pieces.begin(), table_pieces.end(), rvz->table_extents);
    rvz->extent_count = static_cast<std::uint32_t>(file_pieces.size());
    rvz->extents_on_usb = on_usb ? 1 : 0;
    std::copy(file_pieces.begin(), file_pieces.end(), rvz->extents);

    // The whole disc in the blob's pieces: never across a group's end, at
    // most 64 KiB (gc_read_step), with the stream's slot reading elsewhere
    // in between.
    Bytes out(d.iso.size(), 0xEE);
    Bytes music(0x2000);
    std::uint32_t stream_at = static_cast<std::uint32_t>(d.iso.size() / 2);
    bool all_read = true;
    for (std::uint32_t at = 0; at < out.size();) {
        std::uint32_t n = std::min<std::uint32_t>(static_cast<std::uint32_t>(out.size()) - at, 0x10000);
        n = std::min(n, chunk - at % chunk);
        if (!gc_rvz_read(rvz.get(), &card, GC_RVZ_SLOT_GAME, out.data() + at, n, at)) all_read = false;
        at += n;
        if (stream_at + music.size() > d.iso.size()) stream_at = 0x8000;
        if (!gc_rvz_read(rvz.get(), &card, GC_RVZ_SLOT_STREAM, music.data(), static_cast<std::uint32_t>(music.size()),
                         stream_at)) {
            all_read = false;
        }
        if (!std::equal(music.begin(), music.end(), d.iso.begin() + stream_at)) all_read = false;
        stream_at += static_cast<std::uint32_t>(music.size());
    }
    EXPECT_TRUE(all_read);
    EXPECT_TRUE(out == d.iso);
    EXPECT_EQ(rvz->failures, 0u);
    EXPECT_TRUE(card.largest <= GC_RVZ_REQUEST_SECTORS);
    // On USB the groups come from the drive and only the table's sectors
    // from the card.
    if (on_usb) EXPECT_EQ(card.requests - card.usb_requests, rvz->entry_loads);
    else EXPECT_EQ(card.usb_requests, 0u);
    std::cout << "  " << name << (on_usb ? " (on USB)" : "") << ": " << table.group_count << " groups, "
              << rvz->groups_loaded << " decoded, " << rvz->entry_loads << " table sectors, " << card.requests
              << " requests (" << card.usb_requests << " to USB)" << std::endl;

    // A read inside the group held costs nothing more.
    const std::uint32_t loaded = rvz->groups_loaded;
    const std::uint32_t requests = card.requests;
    Bytes small(0x40);
    EXPECT_TRUE(gc_rvz_read(rvz.get(), &card, GC_RVZ_SLOT_GAME, small.data(), 0x40, static_cast<std::uint32_t>(d.iso.size()) - 0x40));
    EXPECT_TRUE(gc_rvz_read(rvz.get(), &card, GC_RVZ_SLOT_GAME, small.data(), 0x40, static_cast<std::uint32_t>(d.iso.size()) - 0x80));
    EXPECT_TRUE(std::equal(small.begin(), small.end(), d.iso.end() - 0x80));
    EXPECT_EQ(rvz->groups_loaded, loaded);
    EXPECT_EQ(card.requests, requests);

    // The header's bytes come from the RVZ's header, even when read alone
    // or across its end.
    Bytes head(0x90);
    EXPECT_TRUE(gc_rvz_read(rvz.get(), &card, GC_RVZ_SLOT_GAME, head.data(), 0x88, 0x08));
    EXPECT_TRUE(std::equal(head.begin(), head.begin() + 0x88, d.iso.begin() + 0x08));

    // Past the end, as on a disc.
    EXPECT_FALSE(gc_rvz_read(rvz.get(), &card, GC_RVZ_SLOT_GAME, small.data(), 0x40, static_cast<std::uint32_t>(d.iso.size()) - 0x20));
    EXPECT_EQ(rvz->last_error, GC_RVZ_ERR_PAST_END);

    // A failed card request fails the read, and the next one recovers.
    rvz->slots[0].group = GC_RVZ_NO_GROUP;
    rvz->entry_sector = GC_RVZ_NO_GROUP;
    card.fail = true;
    EXPECT_FALSE(gc_rvz_read(rvz.get(), &card, GC_RVZ_SLOT_GAME, small.data(), 0x40, 0x9000));
    EXPECT_EQ(rvz->last_error, GC_RVZ_ERR_TABLE);
    card.fail = false;
    EXPECT_TRUE(gc_rvz_read(rvz.get(), &card, GC_RVZ_SLOT_GAME, small.data(), 0x40, 0x9000));
    EXPECT_TRUE(std::equal(small.begin(), small.end(), d.iso.begin() + 0x9000));

    // A file sector outside the pieces given is refused, not read: the
    // second group (stored or compressed, never zeros here) lies past the
    // first piece of the file.
    rvz->slots[0].group = GC_RVZ_NO_GROUP;
    rvz->extent_count = 1;
    EXPECT_FALSE(gc_rvz_read(rvz.get(), &card, GC_RVZ_SLOT_GAME, small.data(), 0x40, chunk + 0x10));
    EXPECT_EQ(rvz->last_error, GC_RVZ_ERR_EXTENT);
    rvz->extent_count = static_cast<std::uint32_t>(file_pieces.size());

    // A buffer too small for a group is refused, not overrun.
    rvz->slots[0].group = GC_RVZ_NO_GROUP;
    rvz->slots[0].buffer_bytes = 512;
    rvz->stored_bytes = std::min<std::uint32_t>(rvz->stored_bytes, 512);
    EXPECT_FALSE(gc_rvz_read(rvz.get(), &card, GC_RVZ_SLOT_GAME, small.data(), 0x40, 0x9000));
}

void TestDiscs() {
    struct Case {
        const char* name;
        std::uint32_t size;
        std::uint32_t chunk;
        bool zstd;
    };
    const Case cases[] = {
        {"Zstandard, 32 KiB chunks", 40 * 0x8000 + 0x1800, 0x8000, true},
        {"Zstandard, 128 KiB chunks", 9 * 0x20000 + 0x6000, 0x20000, true},
        {"no compression, 64 KiB chunks", 12 * 0x10000 + 0x200, 0x10000, false},
    };
    for (const Case& c : cases) {
        const Disc d = make_disc(c.size, c.chunk, c.zstd, kAllKinds);
        CheckDiscSource(d);
        CheckBlobReader(d, c.name, false);
        CheckBlobReader(d, c.name, true);
    }
}

// A Wii disc is not a GameCube one: RvzDiscSource refuses it, and a
// GameCube one has no partition table to describe.
void TestRefusals() {
    Disc d = make_disc(4 * 0x8000, 0x8000, true, kAllKinds);
    std::unique_ptr<RvzImage> image;
    std::string error;
    EXPECT_TRUE(RvzImage::open(std::make_shared<MemorySource>(d.rvz), image, error));
    RvzRuntimeTable table;
    if (image) EXPECT_FALSE(image->runtime_table(0, table, error));
    d.rvz[0x48 + 0x20] ^= 1;  // the disc struct's SHA-1 no longer matches
    std::unique_ptr<RvzDiscSource> disc;
    EXPECT_FALSE(RvzDiscSource::open(std::make_shared<MemorySource>(d.rvz), disc, error));
}

}  // namespace

int main() {
    TestVerdicts();
    TestDiscs();
    TestRefusals();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "gcrvz: all tests passed" << std::endl;
    return 0;
}
