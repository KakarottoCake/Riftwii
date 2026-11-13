// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The undo journal for in-place pack updates (riftwii/ctgpjournal.hpp), with
// a synthetic in-memory pack.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/ctgpjournal.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kChunk = kCtgpJournalChunkBytes;

struct MemPack {
    std::vector<std::uint8_t> data;
    bool fail_writes = false;
    int syncs = 0;
    CtgpPackIo io() {
        CtgpPackIo io;
        io.read = [this](std::uint64_t o, std::uint32_t n, std::uint8_t* out) {
            if (o + n > data.size()) return false;
            std::memcpy(out, data.data() + o, n);
            return true;
        };
        io.write = [this](std::uint64_t o, const std::uint8_t* in, std::uint32_t n) {
            if (fail_writes || o + n > data.size()) return false;
            std::memcpy(data.data() + o, in, n);
            return true;
        };
        return io;
    }
    std::function<bool()> sync() { return [this] { syncs++; return true; }; }
};

MemPack make_pack(std::size_t size) {
    MemPack p;
    p.data.resize(size);
    for (std::size_t i = 0; i < size; i++) p.data[i] = std::uint8_t((i * 7 + (i >> 8)) & 0xFF);
    return p;
}

std::vector<std::uint8_t> filled(std::size_t n, std::uint8_t v) { return std::vector<std::uint8_t>(n, v); }

std::vector<std::uint8_t> slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
void spit(const std::string& path, const std::vector<std::uint8_t>& d) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
}
bool exists(const std::string& p) { return fs::exists(p); }

// Some writes: chunk 0 aligned, one spanning chunks 1-2, one unaligned in chunk 3 (twice).
void do_writes(CtgpPackIo io) {
    auto a = filled(kChunk, 0xA1);
    EXPECT_TRUE(io.write(0, a.data(), kChunk));
    auto b = filled(1024, 0xB2);
    EXPECT_TRUE(io.write(2 * kChunk - 512, b.data(), 1024));
    auto c = filled(100, 0xC3);
    EXPECT_TRUE(io.write(3 * kChunk + 77, c.data(), 100));
    EXPECT_TRUE(io.write(3 * kChunk + 200, c.data(), 100));
}

}  // namespace

int main() {
    fs::path dir = fs::temp_directory_path() / ("ctgpjournal_test_" + std::to_string(std::rand()));
    fs::create_directories(dir);
    const std::string jp = (dir / "j.bin").string();
    const std::size_t size = 6 * kChunk;
    std::string err;

    {   // writes, then recovery restores the original
        MemPack p = make_pack(size);
        auto orig = p.data;
        CtgpPackJournal j;
        EXPECT_TRUE(j.begin(jp, p.io(), size, err));
        do_writes(j.io());
        EXPECT_EQ(j.chunks(), std::size_t(4));
        EXPECT_TRUE(p.data != orig);
        std::size_t n = 0;
        // Simulate a crash: journal object just stops; recovery runs at next start.
        EXPECT_TRUE(ctgp_journal_recover(jp, p.io(), size, p.sync(), n, err) == CtgpJournalRecovery::RolledBack);
        EXPECT_EQ(n, std::size_t(4));
        EXPECT_TRUE(p.data == orig);
        EXPECT_TRUE(!exists(jp));
        EXPECT_TRUE(ctgp_journal_recover(jp, p.io(), size, p.sync(), n, err) == CtgpJournalRecovery::None);
    }
    {   // commit
        MemPack p = make_pack(size);
        auto orig = p.data;
        CtgpPackJournal j;
        EXPECT_TRUE(j.begin(jp, p.io(), size, err));
        do_writes(j.io());
        auto changed = p.data;
        EXPECT_TRUE(j.commit(p.sync(), err));
        EXPECT_EQ(p.syncs, 1);
        EXPECT_TRUE(!exists(jp));
        EXPECT_TRUE(p.data == changed && p.data != orig);
        std::size_t n = 0;
        EXPECT_TRUE(ctgp_journal_recover(jp, p.io(), size, p.sync(), n, err) == CtgpJournalRecovery::None);
        EXPECT_TRUE(p.data == changed);
    }
    {   // rollback
        MemPack p = make_pack(size);
        auto orig = p.data;
        CtgpPackJournal j;
        EXPECT_TRUE(j.begin(jp, p.io(), size, err));
        do_writes(j.io());
        EXPECT_TRUE(j.rollback(p.sync(), err));
        EXPECT_TRUE(p.data == orig);
        EXPECT_TRUE(!exists(jp));
    }
    {   // torn last entry, and recovery twice from a copy of the journal
        MemPack p = make_pack(size);
        auto orig = p.data;
        CtgpPackJournal j;
        EXPECT_TRUE(j.begin(jp, p.io(), size, err));
        do_writes(j.io());
        auto jd = slurp(jp);
        // Chunk 3's pack write "never started": undo it by hand, tear its entry.
        auto torn = jd;
        torn.resize(torn.size() - 1000);
        spit(jp, torn);
        MemPack q = p;
        std::copy(orig.begin() + 3 * kChunk, orig.begin() + 4 * kChunk, q.data.begin() + 3 * kChunk);
        std::size_t n = 0;
        std::string jcopy = (dir / "copy.bin").string();
        fs::copy_file(jp, jcopy);
        EXPECT_TRUE(ctgp_journal_recover(jp, q.io(), size, q.sync(), n, err) == CtgpJournalRecovery::RolledBack);
        EXPECT_EQ(n, std::size_t(3));
        EXPECT_TRUE(q.data == orig);
        // Second recovery of the same journal (crash during recovery): same result.
        EXPECT_TRUE(ctgp_journal_recover(jcopy, q.io(), size, q.sync(), n, err) == CtgpJournalRecovery::RolledBack);
        EXPECT_TRUE(q.data == orig);

        // Bad checksum on the last entry.
        auto bad = jd;
        bad[bad.size() - 1] ^= 0xFF;
        spit(jp, bad);
        MemPack r = p;
        std::copy(orig.begin() + 3 * kChunk, orig.begin() + 4 * kChunk, r.data.begin() + 3 * kChunk);
        EXPECT_TRUE(ctgp_journal_recover(jp, r.io(), size, r.sync(), n, err) == CtgpJournalRecovery::RolledBack);
        EXPECT_EQ(n, std::size_t(3));
        EXPECT_TRUE(r.data == orig);
    }
    {   // a damaged entry with more after it: the journal itself is damaged,
        // so the replay fails and the journal is kept for another try
        MemPack p = make_pack(size);
        CtgpPackJournal j;
        EXPECT_TRUE(j.begin(jp, p.io(), size, err));
        do_writes(j.io());
        auto jd = slurp(jp);
        jd[20 + 12 + 100] ^= 0xFF;  // inside the first entry's data
        spit(jp, jd);
        std::size_t n = 0;
        EXPECT_TRUE(ctgp_journal_recover(jp, p.io(), size, p.sync(), n, err) == CtgpJournalRecovery::Failed);
        EXPECT_TRUE(!err.empty());
        EXPECT_TRUE(exists(jp));
        std::remove(jp.c_str());
    }
    {   // pack size mismatch: nothing written, journal kept
        MemPack p = make_pack(size);
        CtgpPackJournal j;
        EXPECT_TRUE(j.begin(jp, p.io(), size, err));
        do_writes(j.io());
        auto changed = p.data;
        std::size_t n = 0;
        EXPECT_TRUE(ctgp_journal_recover(jp, p.io(), size + 512, p.sync(), n, err) == CtgpJournalRecovery::Failed);
        EXPECT_TRUE(!err.empty());
        EXPECT_TRUE(p.data == changed);
        EXPECT_TRUE(exists(jp));
        // begin refuses while it exists
        CtgpPackJournal j2;
        EXPECT_TRUE(!j2.begin(jp, p.io(), size, err));
        EXPECT_TRUE(!err.empty());
        EXPECT_TRUE(ctgp_journal_recover(jp, p.io(), size, p.sync(), n, err) == CtgpJournalRecovery::RolledBack);
    }
    {   // pack write fails: error is passed on; journal in a missing directory: begin fails
        MemPack p = make_pack(size);
        auto orig = p.data;
        CtgpPackJournal j;
        EXPECT_TRUE(j.begin(jp, p.io(), size, err));
        p.fail_writes = true;
        auto a = filled(64, 1);
        EXPECT_TRUE(!j.io().write(10, a.data(), 64));
        p.fail_writes = false;
        EXPECT_TRUE(j.rollback(p.sync(), err));
        EXPECT_TRUE(p.data == orig);

        CtgpPackJournal k;
        EXPECT_TRUE(!k.begin((dir / "nodir" / "j.bin").string(), p.io(), size, err));
        EXPECT_TRUE(!k.io().write(0, a.data(), 64));  // not begun: refused, pack untouched
        EXPECT_TRUE(p.data == orig);

        // Journaling fails (pack read fails): the pack write must not happen.
        CtgpPackIo bad = p.io();
        bad.read = [](std::uint64_t, std::uint32_t, std::uint8_t*) { return false; };
        CtgpPackJournal m;
        EXPECT_TRUE(m.begin(jp, bad, size, err));
        EXPECT_TRUE(!m.io().write(0, a.data(), 64));
        EXPECT_TRUE(p.data == orig);
        std::remove(jp.c_str());
    }

    fs::remove_all(dir);
    if (g_failures == 0) std::cout << "ctgpjournal: all tests passed" << std::endl;
    return g_failures ? 1 : 0;
}
