// SPDX-License-Identifier: GPL-3.0-or-later
// The resumable FAT32 engine against an image built in memory: names,
// lookups, reads across clusters, writes that grow files and allocate
// clusters, creation with short and long names, deletion, renaming,
// listing, transfer failures and corrupt chains. Every mutation is
// cross-checked with the host resolver (riftwii/fat32.hpp), which reads
// the same image independently.
#include "rtfat.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "fat32_image.hpp"
#include "riftwii/fat32.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
// Each side is evaluated once: several checks call the engine, whose result changes state.
#define EXPECT_EQ(a, b) do { const auto va_ = (a); const auto vb_ = (b); if (va_ != vb_) { std::cerr << "FAILED: " #a " == " #b " (" << va_ << " != " << vb_ << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using Bytes = std::vector<std::uint8_t>;
using fatimg::Image;
using fatimg::lfn_entries;
using fatimg::short_entry;
using fatimg::ucs;

// The engine addresses buffers with 32-bit fields (it is a 32-bit
// target), so driving it on a 64-bit host needs memory below 4 GiB.
#if defined(_WIN32) || defined(__CYGWIN__)
#include <windows.h>
static std::uint8_t* LowBuffer(std::size_t bytes) {
    for (std::uintptr_t hint = 0x10000000; hint < 0x70000000; hint += 0x10000000) {
        void* p = VirtualAlloc(reinterpret_cast<void*>(hint), bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (p) return static_cast<std::uint8_t*>(p);
    }
    return nullptr;
}
#elif defined(__linux__)
#include <sys/mman.h>
static std::uint8_t* LowBuffer(std::size_t bytes) {
    void* p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    return p == MAP_FAILED ? nullptr : static_cast<std::uint8_t*>(p);
}
#else
static std::uint8_t* LowBuffer(std::size_t) { return nullptr; }
#endif

namespace {

std::uint32_t Addr(const void* p) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)); }

Bytes pattern(std::size_t n, std::uint8_t seed) {
    Bytes b(n);
    for (std::size_t i = 0; i < n; ++i) b[i] = static_cast<std::uint8_t>(seed + i * 13 + (i >> 8));
    return b;
}

void cat(Bytes& to, const Bytes& from) { to.insert(to.end(), from.begin(), from.end()); }

// Low memory shared by the tests: the operation record, a bounce buffer
// and a data buffer.
struct Low {
    std::uint8_t* base = nullptr;
    rtfat_op* op = nullptr;
    std::uint8_t* bounce = nullptr;
    std::uint8_t* data = nullptr;
    static constexpr std::size_t kBounce = 2048;
    static constexpr std::size_t kData = 1 << 20;
    Low() {
        base = LowBuffer(sizeof(rtfat_op) + 64 + kBounce + kData);
        if (!base) return;
        std::uintptr_t p = (reinterpret_cast<std::uintptr_t>(base) + 31) & ~std::uintptr_t(31);
        op = reinterpret_cast<rtfat_op*>(p);
        p = (p + sizeof(rtfat_op) + 31) & ~std::uintptr_t(31);
        bounce = reinterpret_cast<std::uint8_t*>(p);
        data = bounce + kBounce;
    }
};

// A device over the image: performs the engine's transfers, counts them,
// and fails the n-th one when asked.
struct Device {
    struct Attempt { std::uint32_t lba, write; };
    Image& img;
    std::uint32_t reads = 0, writes = 0, attempts = 0;
    std::uint32_t fail_at = 0;  // 1-based transfer index to fail, 0 = never
    std::uint32_t fail_at2 = 0; // optional second one-shot failure (recovery tests)
    std::vector<Attempt> history;
    explicit Device(Image& i) : img(i) {}
    void reset() { reads = writes = attempts = 0; fail_at = fail_at2 = 0; history.clear(); }
    int transfer(const rtfat_op& op) {
        const std::uint64_t start = std::uint64_t(op.io_lba) * 512;
        const std::uint64_t bytes = std::uint64_t(op.io_count) * 512;
        ++attempts;
        history.push_back({op.io_lba, op.io_write});
        if (attempts == fail_at || attempts == fail_at2) return -1;
        if (start + bytes > img.bytes.size() || op.io_count == 0) return -2;
        std::uint8_t* mem = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(op.io_buffer));
        if (op.io_write) {
            std::memcpy(img.bytes.data() + start, mem, static_cast<std::size_t>(bytes));
            ++writes;
        } else {
            std::memcpy(mem, img.bytes.data() + start, static_cast<std::size_t>(bytes));
            ++reads;
        }
        return 0;
    }
};

// Runs an operation to completion the synchronous way: step, transfer, step...
std::int32_t Run(rtfat_volume& vol, rtfat_op& op, Device& dev, std::uint32_t kind) {
    rtfat_begin(&op, kind);
    for (int guard = 0; guard < 100000; ++guard) {
        const int r = rtfat_step(&vol, &op);
        if (r == RTFAT_DONE) return op.result;
        op.io_status = dev.transfer(op);
    }
    std::cerr << "FAILED: operation did not finish" << std::endl;
    g_failures++;
    return -999;
}

void SetName(char* field, const char* name) {
    std::memset(field, 0, RTFAT_NAME_MAX + 1);
    std::strncpy(field, name, RTFAT_NAME_MAX);
}

// The image: 512-byte sectors, small clusters so chains matter, the save
// folder at cluster 3 ("save" in the root), its files spread about.
struct Fixture {
    Image img;
    Device dev;
    rtfat_volume vol{};
    Bytes banner, rksys, wiimote;
    explicit Fixture(std::uint32_t spc = 2) : img(512, spc, 0), dev(img) {
        const std::uint32_t cb = img.cluster_bytes();
        banner = pattern(cb + 300, 0x21);         // two clusters, second partly used
        rksys = pattern(3 * cb, 0x33);            // exactly three clusters
        wiimote = pattern(77, 0x55);              // one cluster, mostly empty
        img.write_data({10, 11}, banner);
        img.write_data({12, 14, 13}, rksys);      // out of order on purpose
        img.write_data({20}, wiimote);
        img.write_data({30}, pattern(cb, 0));     // the deleted entry's old cluster, now free
        img.set_fat(30, 0);
        img.chain({40, 41});
        img.set_fat(41, 40);                      // a loop for the corrupt-chain test

        Bytes root;
        cat(root, short_entry("RIFTWII    ", 0x08, 0, 0));
        cat(root, short_entry("SAVE       ", 0x10, 3, 0));
        if (!img.write_dir({2}, root)) g_failures++;

        Bytes save;
        cat(save, short_entry(".          ", 0x10, 3, 0));
        cat(save, short_entry("..         ", 0x10, 0, 0));
        cat(save, short_entry("BANNER  BIN", 0x20, 10, static_cast<std::uint32_t>(banner.size()), 0x18));
        cat(save, short_entry("RKSYS   DAT", 0x20, 12, static_cast<std::uint32_t>(rksys.size())));
        cat(save, lfn_entries(ucs(u"wiimote.data"), "WIIMOT~1DAT", 0x20, 20, static_cast<std::uint32_t>(wiimote.size())));
        cat(save, short_entry("\xE5LD     SAV", 0x20, 30, 100));
        cat(save, short_entry("LOOP    BIN", 0x20, 40, (img.clusters + 10) * cb));  // longer than the volume
        cat(save, lfn_entries(ucs(u"Too Long A Name For ISFS.txt"), "TOOLON~1TXT", 0x20, 0, 0));
        cat(save, short_entry("SUB        ", 0x10, 50, 0));
        if (!img.write_dir({3, 4}, save)) g_failures++;

        vol.sectors_per_cluster = spc;
        vol.fat_lba = img.reserved;
        vol.fat_count = img.fats;
        vol.fat_sectors = img.fat_sectors;
        vol.data_lba = static_cast<std::uint32_t>(img.data_start_sector());
        vol.cluster_count = img.clusters;
        vol.dir_cluster = 3;
        vol.alloc_hint = 2;
    }
    std::uint32_t fat(std::uint32_t c, std::uint32_t copy = 0) const {
        const std::uint8_t* p = img.bytes.data() + (std::uint64_t(img.reserved) + std::uint64_t(copy) * img.fat_sectors) * 512 + std::uint64_t(c) * 4;
        return (std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24)) & 0x0FFFFFFF;
    }
    // The host resolver's view of the same image.
    bool host_lookup(const std::string& name, riftwii::Fat32File& out) {
        riftwii::Fat32Volume v;
        std::string err;
        if (!riftwii::Fat32Volume::mount(img.reader(), v, err)) {
            std::cerr << "host mount failed: " << err << std::endl;
            return false;
        }
        return v.lookup("/save/" + name, out, err);
    }
    Bytes host_read(const std::string& name) {
        riftwii::Fat32Volume v;
        riftwii::Fat32File f;
        std::string err;
        Bytes out;
        if (!riftwii::Fat32Volume::mount(img.reader(), v, err) || !v.lookup("/save/" + name, f, err)) return out;
        out.resize(f.entry.size);
        if (!out.empty() && !v.read(f, 0, out.data(), out.size())) out.clear();
        return out;
    }
    std::vector<std::string> host_names() {
        riftwii::Fat32Volume v;
        std::vector<riftwii::Fat32Entry> entries;
        std::string err;
        std::vector<std::string> names;
        if (riftwii::Fat32Volume::mount(img.reader(), v, err) && v.list("/save", entries, err)) {
            for (const auto& e : entries) names.push_back(e.name);
        }
        return names;
    }
};

bool FatCopiesAgree(const Fixture& fx) {
    for (std::uint32_t c = 0; c < fx.img.clusters + 2; ++c) {
        for (std::uint32_t copy = 1; copy < fx.vol.fat_count; ++copy) {
            if (fx.fat(c, copy) != fx.fat(c)) return false;
        }
    }
    return true;
}

void FillDirectoryForGrowth(Fixture& fx, rtfat_op& op) {
    std::memset(&op, 0, sizeof(op));
    SetName(op.name, "data.bin");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_CREATE), RTFAT_OK);  // the deleted slot
    for (int i = 0; i < 51; ++i) {  // 13 used entries plus these fill the two clusters
        char n[16];
        std::snprintf(n, sizeof n, "F%03d.SAV", i);
        SetName(op.name, n);
        EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_CREATE), RTFAT_OK);
    }
    EXPECT_EQ(fx.fat(4), 0x0FFFFFFFu);
    EXPECT_TRUE(FatCopiesAgree(fx));
}

}  // namespace

// ---- names ----------------------------------------------------------------

static void TestNames() {
    EXPECT_TRUE(rtfat_valid_name("banner.bin"));
    EXPECT_TRUE(rtfat_valid_name("wiimote.data"));
    EXPECT_FALSE(rtfat_valid_name("wiimote01.dat"));  // 13 characters
    EXPECT_TRUE(rtfat_valid_name("a"));
    EXPECT_FALSE(rtfat_valid_name(""));
    EXPECT_FALSE(rtfat_valid_name("thirteenchars"));
    EXPECT_FALSE(rtfat_valid_name("a/b"));
    EXPECT_FALSE(rtfat_valid_name("a b"));
    EXPECT_FALSE(rtfat_valid_name("."));
    EXPECT_FALSE(rtfat_valid_name(".."));
    EXPECT_FALSE(rtfat_valid_name("a:b"));
    EXPECT_FALSE(rtfat_valid_name(nullptr));

    std::uint8_t s[11];
    std::uint8_t flags = 0xFF;
    EXPECT_TRUE(rtfat_fits_short("banner.bin", s, &flags));
    EXPECT_EQ(std::string(reinterpret_cast<char*>(s), 11), "BANNER  BIN");
    EXPECT_EQ(int(flags), 0x18);
    EXPECT_TRUE(rtfat_fits_short("RKSYS.DAT", s, &flags));
    EXPECT_EQ(std::string(reinterpret_cast<char*>(s), 11), "RKSYS   DAT");
    EXPECT_EQ(int(flags), 0);
    EXPECT_TRUE(rtfat_fits_short("save", s, &flags));
    EXPECT_EQ(std::string(reinterpret_cast<char*>(s), 11), "SAVE       ");
    EXPECT_EQ(int(flags), 0x08);
    EXPECT_TRUE(rtfat_fits_short("DATA.bin", s, &flags));
    EXPECT_EQ(int(flags), 0x10);
    EXPECT_FALSE(rtfat_fits_short("Banner.bin", s, &flags));  // mixed case: long name
    EXPECT_EQ(std::string(reinterpret_cast<char*>(s), 11), "BANNER~1BIN");
    EXPECT_FALSE(rtfat_fits_short("wiimote.data", s, &flags));  // a four-character extension
    EXPECT_EQ(std::string(reinterpret_cast<char*>(s), 11), "WIIMOT~1DAT");
    EXPECT_FALSE(rtfat_fits_short("a.b.c", s, &flags));
    EXPECT_EQ(std::string(reinterpret_cast<char*>(s), 11), "AB~1    C  ");
    EXPECT_FALSE(rtfat_fits_short("save.", s, &flags));  // trailing dot
    EXPECT_FALSE(rtfat_fits_short(".hidden", s, &flags));
    EXPECT_EQ(std::string(reinterpret_cast<char*>(s), 11), "_~1     HID");
    EXPECT_FALSE(rtfat_fits_short("x+y.txt", s, &flags));
    EXPECT_EQ(std::string(reinterpret_cast<char*>(s), 11), "XY~1    TXT");
    EXPECT_FALSE(rtfat_fits_short("longbasename", s, &flags));
    EXPECT_EQ(std::string(reinterpret_cast<char*>(s), 11), "LONGBA~1   ");

    const std::uint8_t n[11] = {'W', 'I', 'I', 'M', 'O', 'T', '~', '1', 'D', 'A', 'T'};
    EXPECT_EQ(rtfat_short_checksum(n), std::uint32_t(fatimg::checksum11("WIIMOT~1DAT")));
}

// ---- lookup ----------------------------------------------------------------

static void TestLookup(Low& low) {
    Fixture fx;
    rtfat_op& op = *low.op;
    std::memset(&op, 0, sizeof(op));
    SetName(op.name, "banner.bin");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_LOOKUP), RTFAT_OK);
    EXPECT_EQ(op.found.first_cluster, 10u);
    EXPECT_EQ(op.found.size, std::uint32_t(fx.banner.size()));
    EXPECT_EQ(std::string(op.found.name), "banner.bin");
    EXPECT_EQ(op.found.lfn_count, 0u);
    EXPECT_EQ(op.found.entry_index, 2u);
    EXPECT_EQ(fx.dev.reads, 1u);  // the first directory sector

    SetName(op.name, "WIIMOTE.DATA");  // long name, case-insensitive
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_LOOKUP), RTFAT_OK);
    EXPECT_EQ(op.found.first_cluster, 20u);
    EXPECT_EQ(std::string(op.found.name), "wiimote.data");
    EXPECT_EQ(op.found.lfn_count, 1u);
    EXPECT_EQ(op.found.lfn_index, 4u);
    EXPECT_EQ(op.found.entry_index, 5u);

    SetName(op.name, "rksys.dat");  // stored upper case
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_LOOKUP), RTFAT_OK);
    EXPECT_EQ(std::string(op.found.name), "RKSYS.DAT");

    SetName(op.name, "old.sav");  // deleted
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_LOOKUP), RTFAT_ENOENT);
    SetName(op.name, "sub");  // a directory is not a file
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_LOOKUP), RTFAT_ENOENT);
    SetName(op.name, "missing.bin");
    fx.dev.reset();
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_LOOKUP), RTFAT_ENOENT);
    EXPECT_EQ(fx.dev.reads, 1u);  // the end-of-directory marker sits in the first sector
    SetName(op.name, "bad/name");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_LOOKUP), RTFAT_EINVAL);
}

// ---- read --------------------------------------------------------------------

static void TestRead(Low& low) {
    Fixture fx;
    rtfat_op& op = *low.op;
    std::memset(&op, 0, sizeof(op));
    SetName(op.name, "rksys.dat");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_LOOKUP), RTFAT_OK);
    rtfat_file file{};
    file.first_cluster = op.found.first_cluster;
    file.size = op.found.size;
    file.entry_lba = op.found.entry_lba;
    file.entry_index = op.found.entry_index;

    // The whole file in one call, across three out-of-order clusters.
    op.file = &file;
    op.buffer = Addr(low.data);
    op.length = 100000;
    op.bounce = Addr(low.bounce);
    op.bounce_bytes = Low::kBounce;
    fx.dev.reset();
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_READ), std::int32_t(fx.rksys.size()));
    EXPECT_EQ(std::memcmp(low.data, fx.rksys.data(), fx.rksys.size()), 0);
    EXPECT_EQ(file.position, std::uint32_t(fx.rksys.size()));
    EXPECT_EQ(fx.dev.reads, 4u);  // one FAT sector, three clusters
    EXPECT_EQ(fx.dev.writes, 0u);

    // At the end: nothing more.
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_READ), 0);

    // An unaligned window in the middle, through the cluster cache.
    file.position = 700;
    op.length = 900;
    std::memset(low.data, 0xAA, 2000);
    fx.dev.reset();
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_READ), 900);
    EXPECT_EQ(std::memcmp(low.data, fx.rksys.data() + 700, 900), 0);
    EXPECT_EQ(low.data[900], 0xAA);
    EXPECT_EQ(file.position, 1600u);

    // Clamped at the end.
    file.position = std::uint32_t(fx.rksys.size()) - 10;
    op.length = 100;
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_READ), 10);
    EXPECT_EQ(std::memcmp(low.data, fx.rksys.data() + fx.rksys.size() - 10, 10), 0);

    // Zero-length and a position past the end.
    op.length = 0;
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_READ), 0);
    file.position = std::uint32_t(fx.rksys.size()) + 5;
    op.length = 10;
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_READ), 0);

    // A tiny bounce buffer still works, one sector at a time.
    file.position = 0;
    file.cache_cluster = 0;
    op.length = std::uint32_t(fx.banner.size());
    SetName(op.name, "banner.bin");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_LOOKUP), RTFAT_OK);
    file.first_cluster = op.found.first_cluster;
    file.size = op.found.size;
    op.bounce_bytes = 512;
    fx.dev.reset();
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_READ), std::int32_t(fx.banner.size()));
    EXPECT_EQ(std::memcmp(low.data, fx.banner.data(), fx.banner.size()), 0);
    EXPECT_EQ(fx.dev.reads, 1u + 3u);  // FAT once, then 2 + 1 sectors

    // A looping chain (the file claims more clusters than the volume has) is refused, not followed forever.
    SetName(op.name, "loop.bin");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_LOOKUP), RTFAT_OK);
    file.first_cluster = op.found.first_cluster;
    file.size = op.found.size;
    file.position = 0;
    file.cache_cluster = 0;
    op.length = file.size;
    op.bounce_bytes = Low::kBounce;
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_READ), RTFAT_ECORRUPT);

    // A failing transfer ends the operation with EIO.
    SetName(op.name, "rksys.dat");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_LOOKUP), RTFAT_OK);
    file.first_cluster = op.found.first_cluster;
    file.size = op.found.size;
    file.position = 0;
    file.cache_cluster = 0;
    op.length = file.size;
    fx.dev.reset();
    fx.dev.fail_at = 3;
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_READ), RTFAT_EIO);
    fx.dev.reset();
}

// ---- write -------------------------------------------------------------------

static void TestWrite(Low& low) {
    Fixture fx;
    rtfat_op& op = *low.op;
    std::memset(&op, 0, sizeof(op));
    const std::uint32_t cb = fx.img.cluster_bytes();

    // Append within the last cluster: a partial sector is read, merged, written.
    SetName(op.name, "banner.bin");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_LOOKUP), RTFAT_OK);
    rtfat_file file{};
    file.first_cluster = op.found.first_cluster;
    file.size = op.found.size;
    file.entry_lba = op.found.entry_lba;
    file.entry_index = op.found.entry_index;
    file.position = file.size;
    Bytes model = fx.banner;
    Bytes add = pattern(100, 0x77);
    std::memcpy(low.data, add.data(), add.size());
    op.file = &file;
    op.buffer = Addr(low.data);
    op.length = 100;
    op.bounce = Addr(low.bounce);
    op.bounce_bytes = Low::kBounce;
    fx.dev.reset();
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_WRITE), 100);
    cat(model, add);
    EXPECT_EQ(file.size, std::uint32_t(model.size()));
    EXPECT_EQ(file.position, std::uint32_t(model.size()));
    EXPECT_EQ(fx.dev.writes, 2u);  // the data sector and the directory entry
    EXPECT_TRUE(fx.host_read("banner.bin") == model);

    // Grow past the cluster: allocation from the hint, both FATs, entry updated.
    Bytes more = pattern(cb + 50, 0x99);
    std::memcpy(low.data, more.data(), more.size());
    op.length = std::uint32_t(more.size());
    fx.vol.alloc_hint = 2;
    fx.dev.reset();
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_WRITE), std::int32_t(more.size()));
    cat(model, more);
    EXPECT_TRUE(fx.host_read("banner.bin") == model);
    EXPECT_EQ(fx.fat(11), 5u);        // the first free cluster after the directory's 3 and 4
    EXPECT_EQ(fx.fat(5), 0x0FFFFFFFu); // 624 bytes fit in cluster 11, the remaining 450 in one more
    EXPECT_EQ(fx.fat(11, 1), 5u);     // second copy too
    EXPECT_EQ(fx.fat(5, 1), 0x0FFFFFFFu);
    EXPECT_EQ(fx.fat(6), 0u);
    EXPECT_EQ(fx.vol.alloc_hint, 6u);

    // Overwrite in the middle without changing the size: no entry write.
    file.position = 600;
    Bytes mid = pattern(1000, 0xCC);
    std::memcpy(low.data, mid.data(), mid.size());
    op.length = 1000;
    fx.dev.reset();
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_WRITE), 1000);
    std::memcpy(model.data() + 600, mid.data(), mid.size());
    EXPECT_TRUE(fx.host_read("banner.bin") == model);
    EXPECT_EQ(file.size, std::uint32_t(model.size()));
    EXPECT_EQ(fx.dev.writes, 2u);  // 1000 bytes from 600 span two clusters of 1024: two data writes
    EXPECT_EQ(file.position, 1600u);

    // Into an empty file: the first cluster is allocated and the entry points at it.
    SetName(op.name, "fresh.sav");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_CREATE), RTFAT_OK);
    rtfat_file fresh{};
    fresh.entry_lba = op.found.entry_lba;
    fresh.entry_index = op.found.entry_index;
    Bytes content = pattern(3 * cb + 7, 0xEE);
    std::memcpy(low.data, content.data(), content.size());
    op.file = &fresh;
    op.length = std::uint32_t(content.size());
    fx.dev.reset();
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_WRITE), std::int32_t(content.size()));
    EXPECT_EQ(fresh.first_cluster, 6u);
    EXPECT_EQ(fresh.size, std::uint32_t(content.size()));
    EXPECT_TRUE(fx.host_read("fresh.sav") == content);
    riftwii::Fat32File hf;
    EXPECT_TRUE(fx.host_lookup("fresh.sav", hf));
    EXPECT_EQ(hf.entry.first_cluster, 6u);
    EXPECT_EQ(hf.fragments.size(), 1u);  // 6, 7, 8, 9 were free
    EXPECT_EQ(fx.fat(9), 0x0FFFFFFFu);
    // Four clusters: each marked in both FATs, three of them linked in both; four data writes; the entry.
    EXPECT_EQ(fx.dev.writes, 4u * fx.vol.fat_count + 3u * fx.vol.fat_count + 4u + 1u);

    // Reading it back through the engine.
    fresh.position = 0;
    fresh.cache_cluster = 0;
    std::memset(low.data, 0, content.size());
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_READ), std::int32_t(content.size()));
    EXPECT_EQ(std::memcmp(low.data, content.data(), content.size()), 0);

    // Writing beyond the end is refused; zero length does nothing.
    fresh.position = fresh.size + 1;
    op.length = 1;
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_WRITE), RTFAT_EINVAL);
    fresh.position = fresh.size;
    op.length = 0;
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_WRITE), 0);

    // A full FAT: every free cluster taken, the next allocation fails and
    // what was written before stays.
    for (std::uint32_t c = 2; c < fx.img.clusters + 2; ++c) {
        if (fx.fat(c) == 0) fx.img.set_fat(c, 0x0FFFFFFF);
    }
    fresh.position = fresh.size;
    op.length = cb;
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_WRITE), RTFAT_ENOSPC);
    EXPECT_TRUE(fx.host_read("fresh.sav") == content);
}

static bool IsFatWrite(const Fixture& fx, const Device::Attempt& a) {
    return a.write != 0 && a.lba >= fx.vol.fat_lba &&
           a.lba < fx.vol.fat_lba + fx.vol.fat_count * fx.vol.fat_sectors;
}

static void BeginBannerAppend(Fixture& fx, rtfat_op& op, Low& low, rtfat_file& file) {
    std::memset(&op, 0, sizeof(op));
    SetName(op.name, "banner.bin");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_LOOKUP), RTFAT_OK);
    file = rtfat_file{};
    file.first_cluster = op.found.first_cluster;
    file.size = op.found.size;
    file.position = file.size;
    file.entry_lba = op.found.entry_lba;
    file.entry_index = op.found.entry_index;
    const Bytes append = pattern(fx.img.cluster_bytes() + 800, 0x8A);
    std::memcpy(low.data, append.data(), append.size());
    op.file = &file;
    op.buffer = Addr(low.data);
    op.length = std::uint32_t(append.size());
    op.bounce = Addr(low.bounce);
    op.bounce_bytes = Low::kBounce;
}

// A directory extension has an explicit rollback.  Ordinary file chain
// mutations do not, so any failed mirrored FAT write poisons the volume and
// the caller must remount before trying another mutation.
static void TestOrdinaryFatMutationPoison(Low& low) {
    Fixture grow_ok;
    rtfat_op& probe = *low.op;
    rtfat_file probe_file{};
    BeginBannerAppend(grow_ok, probe, low, probe_file);
    grow_ok.dev.reset();
    EXPECT_TRUE(Run(grow_ok.vol, probe, grow_ok.dev, RTFAT_OP_WRITE) > 0);

    int grew_cases = 0;
    for (std::size_t i = 0; i < grow_ok.dev.history.size(); ++i) {
        if (!IsFatWrite(grow_ok, grow_ok.dev.history[i])) continue;
        Fixture fx;
        rtfat_op& op = *low.op;
        rtfat_file file{};
        BeginBannerAppend(fx, op, low, file);
        fx.dev.reset();
        fx.dev.fail_at = std::uint32_t(i + 1);
        EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_WRITE), RTFAT_EIO);
        EXPECT_EQ(fx.vol.mutation_uncertain, 1u);
        SetName(op.name, "later.sav");
        EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_CREATE), RTFAT_EIO);
        SetName(op.name, "rksys.dat");
        EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_LOOKUP), RTFAT_OK);
        ++grew_cases;
    }
    EXPECT_TRUE(grew_cases >= 4);  // candidate marks and parent links, both copies

    Fixture delete_ok;
    rtfat_op& delete_probe = *low.op;
    std::memset(&delete_probe, 0, sizeof(delete_probe));
    SetName(delete_probe.name, "wiimote.data");
    delete_ok.dev.reset();
    EXPECT_EQ(Run(delete_ok.vol, delete_probe, delete_ok.dev, RTFAT_OP_DELETE), RTFAT_OK);

    int delete_cases = 0;
    for (std::size_t i = 0; i < delete_ok.dev.history.size(); ++i) {
        if (!IsFatWrite(delete_ok, delete_ok.dev.history[i])) continue;
        Fixture fx;
        rtfat_op& op = *low.op;
        std::memset(&op, 0, sizeof(op));
        SetName(op.name, "wiimote.data");
        fx.dev.reset();
        fx.dev.fail_at = std::uint32_t(i + 1);
        EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_DELETE), RTFAT_EIO);
        EXPECT_EQ(fx.vol.mutation_uncertain, 1u);
        SetName(op.name, "later.sav");
        EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_CREATE), RTFAT_EIO);
        SetName(op.name, "rksys.dat");
        EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_LOOKUP), RTFAT_OK);
        ++delete_cases;
    }
    EXPECT_EQ(delete_cases, int(delete_ok.vol.fat_count));
}

// ---- create ------------------------------------------------------------------

static void TestCreate(Low& low) {
    Fixture fx;
    rtfat_op& op = *low.op;
    std::memset(&op, 0, sizeof(op));

    // An 8.3 name in lower case: one entry with the NT flags.
    SetName(op.name, "data.bin");
    fx.dev.reset();
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_CREATE), RTFAT_OK);
    EXPECT_EQ(op.found.lfn_count, 0u);
    EXPECT_EQ(op.found.first_cluster, 0u);
    EXPECT_EQ(op.found.size, 0u);
    EXPECT_EQ(op.found.entry_index, 6u);  // the deleted entry's slot, reused
    riftwii::Fat32File hf;
    EXPECT_TRUE(fx.host_lookup("data.bin", hf));
    EXPECT_EQ(hf.entry.name, "data.bin");
    EXPECT_EQ(hf.entry.short_name, "data.bin");
    EXPECT_EQ(fx.dev.writes, 1u);

    // A long name: two entries, alias ~1, checksum right (the host parses it).
    SetName(op.name, "wiimote.dat2");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_CREATE), RTFAT_OK);
    EXPECT_EQ(op.found.lfn_count, 1u);
    EXPECT_EQ(op.found.lfn_index, 13u);  // after the fixture's 13 entries
    EXPECT_EQ(op.found.entry_index, 14u);
    EXPECT_TRUE(fx.host_lookup("wiimote.dat2", hf));
    EXPECT_EQ(hf.entry.name, "wiimote.dat2");
    EXPECT_EQ(hf.entry.short_name, "WIIMOT~2.DAT");  // ~1 belongs to wiimote.data

    // Existing names, in any case, are refused.
    SetName(op.name, "BANNER.BIN");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_CREATE), RTFAT_EEXIST);
    SetName(op.name, "Wiimote.Data");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_CREATE), RTFAT_EEXIST);
    SetName(op.name, "");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_CREATE), RTFAT_EINVAL);

    // The engine's own lookup finds them, and lists them.
    SetName(op.name, "wiimote.dat2");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_LOOKUP), RTFAT_OK);
    EXPECT_EQ(op.found.entry_index, 14u);

    // Fill the directory past its two clusters (64 entries, 15 in use):
    // it grows by a cluster each time the entries run out, the new
    // cluster zeroed and chained after cluster 4, and every file is
    // found afterwards, by the engine and by the host.
    EXPECT_EQ(fx.fat(4), 0x0FFFFFFFu);
    for (int i = 0; i < 100; ++i) {
        char n[16];
        std::snprintf(n, sizeof n, "F%03d.SAV", i);
        SetName(op.name, n);
        EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_CREATE), RTFAT_OK);
    }
    const std::uint32_t third = fx.fat(4);
    EXPECT_TRUE(third >= 2 && third < 0x0FFFFFF8u);
    const std::uint32_t fourth = fx.fat(third);
    EXPECT_TRUE(fourth >= 2 && fourth < 0x0FFFFFF8u);
    EXPECT_EQ(fx.fat(fourth), 0x0FFFFFFFu);  // 115 entries: four clusters of 32
    EXPECT_EQ(fx.fat(third, 1), fourth);      // the second FAT copy too
    EXPECT_EQ(fx.host_names().size(), std::size_t(6 + 2 + 100));  // the host lists the too-long name and the directory too
    const auto cluster_lba = [&](std::uint32_t c) { return fx.vol.data_lba + (c - 2) * fx.vol.sectors_per_cluster; };
    SetName(op.name, "F099.SAV");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_LOOKUP), RTFAT_OK);
    EXPECT_EQ(op.found.entry_lba, cluster_lba(fourth) + 1);  // entry 114: the fourth cluster's second sector
    EXPECT_EQ(op.found.entry_index, 2u);
    SetName(op.name, "F049.SAV");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_LOOKUP), RTFAT_OK);
    EXPECT_EQ(op.found.entry_lba, cluster_lba(third));  // entry 64: the first of the grown cluster
    EXPECT_EQ(op.found.entry_index, 0u);
    // A long name (two entries) with one entry left in the last sector:
    // never split across sectors, so the directory grows once more and
    // the name starts the fifth cluster.
    for (int i = 100; i < 112; ++i) {
        char n[16];
        std::snprintf(n, sizeof n, "F%03d.SAV", i);
        SetName(op.name, n);
        EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_CREATE), RTFAT_OK);
    }
    EXPECT_EQ(fx.fat(fourth), 0x0FFFFFFFu);  // 127 entries used, one free
    SetName(op.name, "longname9.sv");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_CREATE), RTFAT_OK);
    const std::uint32_t fifth = fx.fat(fourth);
    EXPECT_TRUE(fifth >= 2 && fifth < 0x0FFFFFF8u);
    EXPECT_EQ(op.found.lfn_lba, cluster_lba(fifth));
    EXPECT_EQ(op.found.lfn_index, 0u);
    EXPECT_EQ(op.found.entry_index, 1u);
    riftwii::Fat32File grown;
    EXPECT_TRUE(fx.host_lookup("longname9.sv", grown));
    EXPECT_EQ(grown.entry.name, "longname9.sv");
    // The same with room left in the cluster: the name starts the next
    // sector of the fifth cluster, and the entry it skipped is marked
    // deleted so scans read on. (F112 takes the entry marked deleted
    // in the fourth cluster; 13 more fill the fifth's first sector but one.)
    for (int i = 112; i < 126; ++i) {
        char n[16];
        std::snprintf(n, sizeof n, "F%03d.SAV", i);
        SetName(op.name, n);
        EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_CREATE), RTFAT_OK);
    }
    SetName(op.name, "longname8.sv");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_CREATE), RTFAT_OK);
    EXPECT_EQ(fx.fat(fifth), 0x0FFFFFFFu);
    EXPECT_EQ(op.found.lfn_lba, cluster_lba(fifth) + 1);
    EXPECT_EQ(op.found.lfn_index, 0u);
    EXPECT_TRUE(fx.host_lookup("longname8.sv", grown));
    EXPECT_EQ(fx.img.bytes[(cluster_lba(fifth) * 512) + 15 * 32], 0xE5);  // the skipped entry
    // The engine lists them all.
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_COUNT), 5 + 2 + 112 + 1 + 14 + 1);
    EXPECT_EQ(fx.host_names().size(), std::size_t(6 + 2 + 112 + 1 + 14 + 1));

    // Each possible one-shot failure while extending a full directory is
    // unwound: every FAT copy agrees, the old tail is EOC, the candidate
    // is free, and the directory bytes match the pre-operation image.
    // That includes candidate mark/zero/link and the first dirent write.
    Fixture success;
    rtfat_op& os = *low.op;
    FillDirectoryForGrowth(success, os);
    success.dev.reset();
    SetName(os.name, "GROWN.SAV");
    EXPECT_EQ(Run(success.vol, os, success.dev, RTFAT_OP_CREATE), RTFAT_OK);
    const std::uint32_t growth_transfers = success.dev.attempts;
    EXPECT_TRUE(growth_transfers > 8);
    EXPECT_TRUE(FatCopiesAgree(success));

    for (std::uint32_t fail_at = 1; fail_at <= growth_transfers; ++fail_at) {
        Fixture fy;
        rtfat_op& oy = *low.op;
        FillDirectoryForGrowth(fy, oy);
        const Bytes before = fy.img.bytes;
        fy.dev.reset();
        fy.dev.fail_at = fail_at;
        SetName(oy.name, "GROWN.SAV");
        EXPECT_EQ(Run(fy.vol, oy, fy.dev, RTFAT_OP_CREATE), RTFAT_EIO);
        EXPECT_EQ(fy.vol.mutation_uncertain, 0u);
        EXPECT_TRUE(FatCopiesAgree(fy));
        EXPECT_EQ(fy.fat(4), 0x0FFFFFFFu);
        if (oy.alloc_cluster != 0) {
            // A retry may start at the old hint or at the restored candidate,
            // but it must never skip that known-free candidate.
            EXPECT_TRUE(fy.vol.alloc_hint <= oy.alloc_cluster);
        }
        EXPECT_TRUE(fy.img.bytes == before);
        riftwii::Fat32File missing;
        EXPECT_FALSE(fy.host_lookup("GROWN.SAV", missing));
        EXPECT_TRUE(fy.host_read("rksys.dat") == fy.rksys);
    }

    // A second failure during the recovery sweep leaves consistency
    // unproven.  The volume is poisoned, later mutations fail EIO, and
    // lookups can still safely read the surviving directory.
    int saw_poison = 0;
    for (std::uint32_t fail_at = 1; fail_at <= growth_transfers && !saw_poison; ++fail_at) {
        Fixture fy;
        rtfat_op& oy = *low.op;
        FillDirectoryForGrowth(fy, oy);
        fy.dev.reset();
        fy.dev.fail_at = fail_at;
        fy.dev.fail_at2 = fail_at + 1;
        SetName(oy.name, "GROWN.SAV");
        EXPECT_EQ(Run(fy.vol, oy, fy.dev, RTFAT_OP_CREATE), RTFAT_EIO);
        if (fy.vol.mutation_uncertain == 0) continue;  // first failure preceded the extension
        saw_poison = 1;
        EXPECT_TRUE(fy.dev.attempts > fail_at + 1);  // the remaining recovery copies were still attempted
        SetName(oy.name, "LATER.SAV");
        EXPECT_EQ(Run(fy.vol, oy, fy.dev, RTFAT_OP_CREATE), RTFAT_EIO);
        SetName(oy.name, "rksys.dat");
        EXPECT_EQ(Run(fy.vol, oy, fy.dev, RTFAT_OP_LOOKUP), RTFAT_OK);
    }
    EXPECT_TRUE(saw_poison);
}

// ---- delete and rename ---------------------------------------------------------

static void TestDeleteRename(Low& low) {
    Fixture fx;
    rtfat_op& op = *low.op;
    std::memset(&op, 0, sizeof(op));

    // Delete a long-named file: both entries gone, the chain released in both FATs.
    SetName(op.name, "wiimote.data");
    fx.dev.reset();
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_DELETE), RTFAT_OK);
    riftwii::Fat32File hf;
    EXPECT_FALSE(fx.host_lookup("wiimote.data", hf));
    EXPECT_EQ(fx.fat(20), 0u);
    EXPECT_EQ(fx.fat(20, 1), 0u);
    EXPECT_EQ(fx.img.bytes[fx.img.cluster_lba(3) * 512 + 4 * 32], 0xE5);
    EXPECT_EQ(fx.img.bytes[fx.img.cluster_lba(3) * 512 + 5 * 32], 0xE5);
    EXPECT_EQ(fx.dev.writes, 1u + 1u * fx.vol.fat_count);
    SetName(op.name, "wiimote.data");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_DELETE), RTFAT_ENOENT);

    // Delete a three-cluster file: all three released.
    SetName(op.name, "rksys.dat");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_DELETE), RTFAT_OK);
    EXPECT_EQ(fx.fat(12), 0u);
    EXPECT_EQ(fx.fat(13), 0u);
    EXPECT_EQ(fx.fat(14), 0u);
    EXPECT_FALSE(fx.host_lookup("rksys.dat", hf));

    // Rename to a long name: same clusters and size, old entry gone.
    SetName(op.name, "banner.bin");
    SetName(op.name2, "Banner O.bin");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_RENAME), RTFAT_EINVAL);  // a space is not allowed
    SetName(op.name2, "Banner1.bin");  // mixed case: a long name with a short alias
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_RENAME), RTFAT_OK);
    EXPECT_TRUE(fx.host_lookup("Banner1.bin", hf));
    EXPECT_EQ(hf.entry.first_cluster, 10u);
    EXPECT_EQ(hf.entry.size, std::uint32_t(fx.banner.size()));
    EXPECT_EQ(hf.entry.short_name, "BANNER~1.BIN");
    EXPECT_FALSE(fx.host_lookup("banner.bin", hf));
    EXPECT_TRUE(fx.host_read("Banner1.bin") == fx.banner);

    // Rename onto an existing name, or a missing source, is refused.
    SetName(op.name, "Banner1.bin");
    SetName(op.name2, "loop.bin");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_RENAME), RTFAT_EEXIST);
    SetName(op.name, "nothere.bin");
    SetName(op.name2, "x.bin");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_RENAME), RTFAT_ENOENT);
    SetName(op.name, "Banner1.bin");
    SetName(op.name2, "banner1.bin");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_RENAME), RTFAT_EEXIST);  // the same name to FAT

    // And back to a short one.
    SetName(op.name, "Banner1.bin");
    SetName(op.name2, "banner.bin");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_RENAME), RTFAT_OK);
    EXPECT_TRUE(fx.host_lookup("banner.bin", hf));
    EXPECT_EQ(hf.entry.short_name, "banner.bin");
    EXPECT_FALSE(fx.host_lookup("Banner1.bin", hf));
}

// ---- a long name straddling two sectors ------------------------------------------

static void TestStraddle(Low& low) {
    Fixture fx;
    rtfat_op& op = *low.op;
    std::memset(&op, 0, sizeof(op));
    // Entries 13 and 14 of the first sector filled, then a long name whose
    // long entry is the sector's last (15) and whose short entry opens the
    // next sector.
    std::uint8_t* dir = fx.img.cluster(3);
    Bytes fill;
    cat(fill, short_entry("PAD0    BIN", 0x20, 0, 0));
    cat(fill, short_entry("PAD1    BIN", 0x20, 0, 0));
    cat(fill, lfn_entries(ucs(u"strad.dle1"), "STRAD~1 DLE", 0x20, 20, 77));
    std::memcpy(dir + 13 * 32, fill.data(), fill.size());

    SetName(op.name, "strad.dle1");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_LOOKUP), RTFAT_OK);
    EXPECT_EQ(op.found.lfn_index, 15u);
    EXPECT_EQ(op.found.lfn_lba, std::uint32_t(fx.img.cluster_lba(3)));
    EXPECT_EQ(op.found.entry_lba, std::uint32_t(fx.img.cluster_lba(3)) + 1);
    EXPECT_EQ(op.found.entry_index, 0u);
    riftwii::Fat32File hf;
    EXPECT_TRUE(fx.host_lookup("strad.dle1", hf));

    fx.dev.reset();
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_DELETE), RTFAT_OK);
    EXPECT_FALSE(fx.host_lookup("strad.dle1", hf));
    EXPECT_TRUE(fx.host_lookup("pad1.bin", hf));  // the neighbour survives
    EXPECT_EQ(dir[15 * 32], 0xE5);
    EXPECT_EQ(dir[16 * 32], 0xE5);
    EXPECT_EQ(fx.dev.writes, 2u + 1u * fx.vol.fat_count);  // two directory sectors, the FAT copies
    EXPECT_EQ(fx.fat(20), 0u);

    // The freed slots (15 in sector 0, 0 in sector 1) are not one run: a
    // two-entry name goes to the first sector holding two free entries in a
    // row, the second sector's 0 (freed) and 1 (never used).
    SetName(op.name, "Mixed.cas");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_CREATE), RTFAT_OK);
    EXPECT_EQ(op.found.lfn_lba, std::uint32_t(fx.img.cluster_lba(3)) + 1);
    EXPECT_EQ(op.found.lfn_index, 0u);
    EXPECT_EQ(op.found.entry_index, 1u);
    EXPECT_TRUE(fx.host_lookup("Mixed.cas", hf));
    EXPECT_EQ(hf.entry.short_name, "MIXED~1.CAS");
}

// ---- list, count, usage -----------------------------------------------------------

static void TestList(Low& low) {
    Fixture fx;
    rtfat_op& op = *low.op;
    std::memset(&op, 0, sizeof(op));
    const std::uint32_t cb = fx.img.cluster_bytes();

    // Files only: no directories, no label, no deleted; a long name ISFS
    // cannot hold appears under its short alias (and opens by it).
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_COUNT), 5);
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_USAGE), 5);
    const std::uint32_t blocks = (std::uint32_t(fx.banner.size()) + 16383) / 16384 + (3 * cb + 16383) / 16384 +
                                 (77 + 16383) / 16384 + ((fx.img.clusters + 10) * cb + 16383) / 16384;
    EXPECT_EQ(op.usage_blocks, blocks);

    // The names are packed as IOS packs them: one after another, each
    // NUL-terminated, in a buffer of 13 bytes per name asked for.
    std::memset(low.data, 0xAA, 13 * 8);
    op.buffer = Addr(low.data);
    op.length = 8;
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_LIST), 5);
    {
        const char* p = reinterpret_cast<const char*>(low.data);
        const char* expect[5] = {"banner.bin", "RKSYS.DAT", "wiimote.data", "LOOP.BIN", "TOOLON~1.TXT"};
        std::size_t at = 0;
        for (const char* e : expect) {
            EXPECT_EQ(std::string(p + at), e);
            at += std::string(e).size() + 1;
        }
        EXPECT_EQ(at, std::size_t(11 + 10 + 13 + 9 + 13));
        EXPECT_EQ(low.data[at], 0xAA);  // untouched beyond the names
    }
    SetName(op.name, "toolon~1.txt");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_LOOKUP), RTFAT_OK);

    op.length = 2;
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_LIST), 2);
    op.length = 0;
    op.buffer = 0;
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_LIST), 0);
    op.length = 1;
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_LIST), RTFAT_EINVAL);

    // A hidden entry (the runtime's clone marker): skipped by lookups,
    // listings and usage unless the operation asks for hidden entries;
    // a creation of its name still collides. The host sees it as any file.
    SetName(op.name, "riftwii.cln");
    op.want_hidden = 1;
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_CREATE), RTFAT_OK);
    EXPECT_EQ(op.found.attributes, 0x22u);
    riftwii::Fat32File marker;
    EXPECT_TRUE(fx.host_lookup("riftwii.cln", marker));
    op.want_hidden = 0;
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_LOOKUP), RTFAT_ENOENT);
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_COUNT), 5);
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_USAGE), 5);
    op.buffer = Addr(low.data);
    op.length = 8;
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_LIST), 5);
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_CREATE), RTFAT_EEXIST);
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_DELETE), RTFAT_ENOENT);
    op.want_hidden = 1;
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_LOOKUP), RTFAT_OK);
    EXPECT_EQ(op.found.attributes, 0x22u);
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_COUNT), 6);
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_DELETE), RTFAT_OK);
    EXPECT_FALSE(fx.host_lookup("riftwii.cln", marker));
    op.want_hidden = 0;
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_CREATE), RTFAT_OK);  // the name is free again

    // Internal staging names can be promoted without exposing the stage:
    // force-hidden CREATE, then force-visible RENAME.
    SetName(op.name, "stage.sav");
    op.want_hidden = 1;
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_CREATE), RTFAT_OK);
    SetName(op.name, "stage.sav");
    SetName(op.name2, "dest.sav");
    op.want_hidden = 2;
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_RENAME), RTFAT_OK);
    op.want_hidden = 0;
    SetName(op.name, "dest.sav");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_LOOKUP), RTFAT_OK);
    EXPECT_EQ(op.found.attributes, 0x20u);
}

// ---- a bigger geometry: 8 sectors per cluster, the FAT crossing sectors -----------------

static void TestGeometry(Low& low) {
    Fixture fx(8);
    rtfat_op& op = *low.op;
    std::memset(&op, 0, sizeof(op));
    // 256 clusters of 4 KiB: the FAT spans two sectors (128 entries each);
    // allocate past cluster 128 to cross them.
    for (std::uint32_t c = 2; c < 130; ++c) {
        if (fx.fat(c) == 0) fx.img.set_fat(c, 0x0FFFFFFF);
    }
    SetName(op.name, "big.sav");
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_CREATE), RTFAT_OK);
    rtfat_file f{};
    f.entry_lba = op.found.entry_lba;
    f.entry_index = op.found.entry_index;
    const Bytes content = pattern(5 * fx.img.cluster_bytes() + 123, 0x5A);
    std::memcpy(low.data, content.data(), content.size());
    op.file = &f;
    op.buffer = Addr(low.data);
    op.length = std::uint32_t(content.size());
    op.bounce = Addr(low.bounce);
    op.bounce_bytes = Low::kBounce;
    fx.vol.alloc_hint = 100;
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_WRITE), std::int32_t(content.size()));
    EXPECT_EQ(f.first_cluster, 130u);
    EXPECT_EQ(fx.fat(130), 131u);
    EXPECT_EQ(fx.fat(134), 135u);
    EXPECT_EQ(fx.fat(135), 0x0FFFFFFFu);  // five full clusters and 123 bytes: six
    EXPECT_TRUE(fx.host_read("big.sav") == content);
    f.position = 0;
    f.cache_cluster = 0;
    std::memset(low.data, 0, content.size());
    EXPECT_EQ(Run(fx.vol, op, fx.dev, RTFAT_OP_READ), std::int32_t(content.size()));
    EXPECT_EQ(std::memcmp(low.data, content.data(), content.size()), 0);
    EXPECT_EQ(fx.vol.alloc_hint, 136u);
}

// ---- atomic-commit ghost: hidden stage renamed visible, old must die ----
static std::uint32_t ReadFat(const fatimg::Image& img, std::uint32_t c) {
    const std::uint8_t* p = img.bytes.data() + (std::uint64_t(img.reserved) * 512) + std::uint64_t(c) * 4;
    return (std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24)) & 0x0FFFFFFFu;
}

static void TestCommitGhost(Low& low) {
    // Single-sector clusters like the SD card; the save folder grows past
    // one sector during the run, like the 37-entry folder on the card.
    // Each round mimics the atomic import commit: hidden stage created,
    // written, renamed visible. Afterwards the stage name must resolve
    // to nothing (even hidden-aware), and no two live files may share a
    // cluster (the card showed a stage sharing banner.bin's chain).
    fatimg::Image img(512, 1, 0, 1024);
    Device dev(img);
    rtfat_volume vol{};
    vol.sectors_per_cluster = 1;
    vol.fat_lba = img.reserved;
    vol.fat_count = img.fats;
    vol.fat_sectors = img.fat_sectors;
    vol.data_lba = static_cast<std::uint32_t>(img.data_start_sector());
    vol.cluster_count = img.clusters;
    vol.dir_cluster = 3;
    vol.alloc_hint = 4;
    Bytes root;
    cat(root, short_entry("SAVE       ", 0x10, 3, 0));
    EXPECT_TRUE(img.write_dir({2}, root));
    Bytes dot;
    cat(dot, short_entry(".          ", 0x10, 3, 0));
    cat(dot, short_entry("..         ", 0x10, 2, 0));
    EXPECT_TRUE(img.write_dir({3}, dot));
    rtfat_op& op = *low.op;
    Bytes payload = pattern(3 * 512 + 100, 0x5A);
    std::vector<std::uint32_t> dst_first;
    for (int i = 0; i < 30; ++i) {
        char dst[16];
        std::snprintf(dst, sizeof dst, "F%03d.BIN", i);
        std::memset(&op, 0, sizeof(op));
        SetName(op.name, ".rwstage.tmp");
        op.want_hidden = 1;
        EXPECT_EQ(Run(vol, op, dev, RTFAT_OP_CREATE), RTFAT_OK);
        std::memset(&op, 0, sizeof(op));
        SetName(op.name, ".rwstage.tmp");
        op.want_hidden = 1;
        EXPECT_EQ(Run(vol, op, dev, RTFAT_OP_LOOKUP), RTFAT_OK);
        rtfat_file file{};
        file.first_cluster = op.found.first_cluster;
        file.size = 0;
        file.position = 0;
        file.entry_lba = op.found.entry_lba;
        file.entry_index = op.found.entry_index;
        std::memcpy(low.data, payload.data(), payload.size());
        op.file = &file;
        op.buffer = Addr(low.data);
        op.length = static_cast<std::uint32_t>(payload.size());
        op.bounce = Addr(low.bounce);
        op.bounce_bytes = Low::kBounce;
        EXPECT_EQ(Run(vol, op, dev, RTFAT_OP_WRITE), std::int32_t(payload.size()));
        std::memset(&op, 0, sizeof(op));
        SetName(op.name, ".rwstage.tmp");
        SetName(op.name2, dst);
        op.want_hidden = 2;
        EXPECT_EQ(Run(vol, op, dev, RTFAT_OP_RENAME), RTFAT_OK);
        std::memset(&op, 0, sizeof(op));
        SetName(op.name, ".rwstage.tmp");
        op.want_hidden = 1;
        EXPECT_EQ(Run(vol, op, dev, RTFAT_OP_LOOKUP), RTFAT_ENOENT);
        std::memset(&op, 0, sizeof(op));
        SetName(op.name, dst);
        EXPECT_EQ(Run(vol, op, dev, RTFAT_OP_LOOKUP), RTFAT_OK);
        dst_first.push_back(op.found.first_cluster);
    }
    EXPECT_EQ(dst_first.size(), std::size_t(30));
    // Every live chain exclusive: walk each file, no cluster twice.
    std::vector<char> seen(img.clusters + 2, 0);
    seen[3] = 1;
    for (std::uint32_t first : dst_first) {
        std::uint32_t c = first;
        std::uint32_t n = 0;
        while (c >= 2 && c < 0x0FFFFFF8u && n < 100) {
            EXPECT_FALSE(seen[c]);
            seen[c] = 1;
            c = ReadFat(img, c);
            ++n;
        }
        EXPECT_TRUE(c >= 0x0FFFFFF8u);
    }
    // The host reader agrees: every file present with its bytes, the
    // stage name resolving to nothing.
    riftwii::Fat32Volume v;
    std::string err;
    EXPECT_TRUE(riftwii::Fat32Volume::mount(img.reader(), v, err));
    std::vector<riftwii::Fat32Entry> entries;
    EXPECT_TRUE(v.list("/save", entries, err));
    std::size_t files = 0;
    for (const auto& e : entries) {
        if (e.name != "." && e.name != "..") ++files;
    }
    EXPECT_EQ(files, std::size_t(30));
    riftwii::Fat32File f;
    EXPECT_FALSE(v.lookup("/save/.rwstage.tmp", f, err));
    for (int i = 0; i < 30; ++i) {
        char dst[16];
        std::snprintf(dst, sizeof dst, "/save/F%03d.BIN", i);
        EXPECT_TRUE(v.lookup(dst, f, err));
        EXPECT_EQ(f.entry.size, std::uint32_t(payload.size()));
    }
    for (int i = 0; i < 30; i += 7) {
        char dst[16];
        std::snprintf(dst, sizeof dst, "/save/F%03d.BIN", i);
        EXPECT_TRUE(v.lookup(dst, f, err));
        Bytes out(f.entry.size);
        EXPECT_TRUE(v.read(f, 0, out.data(), out.size()));
        EXPECT_TRUE(out == payload);
    }
}

int main() {
    Low low;
    if (!low.op) {
        std::cerr << "no memory below 4 GiB on this host; skipping the engine tests" << std::endl;
        TestNames();
        return g_failures == 0 ? 0 : 1;
    }
    TestNames();
    TestLookup(low);
    TestRead(low);
    TestWrite(low);
    TestOrdinaryFatMutationPoison(low);
    TestCreate(low);
    TestDeleteRename(low);
    TestCommitGhost(low);
    TestStraddle(low);
    TestList(low);
    TestGeometry(low);
    if (g_failures == 0) std::cout << "rtfat tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
