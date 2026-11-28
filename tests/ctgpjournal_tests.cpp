// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The undo journal for in-place pack updates (riftwii/ctgpjournal.hpp), with
// a synthetic in-memory pack.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "riftwii/ctgpjournal.hpp"

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kChunk = kCtgpJournalChunkBytes;
constexpr std::size_t kHeader = 560;  // the journal's header (version 3, with 128 content samples)

// A pack behind a write cache, as blob.bin is on the Wii behind libfat's
// (libdvm's): writes land in `cache`, and sync() copies them to `card`, except
// that the card drops the blocks `lose` names while sync() still says yes,
// as libdvm's flush does when a sector write fails. read_card sees the card.
struct CachedPack {
    std::vector<std::uint8_t> cache, card;
    std::set<std::uint64_t> pending;  // 512-byte blocks written since the last sync
    std::set<std::uint64_t> lose;     // blocks the card refuses (sync still says yes)
    CtgpPackIdentity identity;
    CtgpPackIo io() {
        CtgpPackIo io;
        io.read = [this](std::uint64_t o, std::uint32_t n, std::uint8_t* out) {
            if (o + n > cache.size()) return false;
            std::memcpy(out, cache.data() + o, n);
            return true;
        };
        io.write = [this](std::uint64_t o, const std::uint8_t* in, std::uint32_t n) {
            if (o + n > cache.size()) return false;
            std::memcpy(cache.data() + o, in, n);
            for (std::uint64_t b = o / 512; b <= (o + n - 1) / 512; b++) pending.insert(b);
            return true;
        };
        io.read_card = [this](std::uint64_t o, std::uint32_t n, std::uint8_t* out) {
            if (o + n > card.size()) return false;
            std::memcpy(out, card.data() + o, n);
            return true;
        };
        io.identity = identity;
        return io;
    }
    std::function<bool()> sync() {
        return [this] {
            for (std::uint64_t b : pending)
                if (!lose.count(b)) std::memcpy(card.data() + b * 512, cache.data() + b * 512, 512);
            pending.clear();
            return true;  // whatever the card did, as libdvm's sync
        };
    }
};

CachedPack make_cached(std::size_t size, std::uint8_t seed) {
    CachedPack p;
    p.cache.resize(size);
    for (std::size_t i = 0; i < size; i++) p.cache[i] = std::uint8_t((i * 7 + (i >> 8) + seed) & 0xFF);
    p.card = p.cache;
    return p;
}

CtgpPackIdentity identity_of(std::uint32_t first_cluster, std::uint16_t create_time) {
    Fat32Entry e;
    e.size = 6 * kChunk;
    e.first_cluster = first_cluster;
    e.create_time = create_time;
    e.create_date = 0x5B78;
    e.write_time = 0x1234;
    e.write_date = 0x5B79;
    return ctgp_pack_identity(e, true);
}

// The journal file as the card holds it: the real file, except that reads
// of the bytes from `lose_from` on come back changed while `losing` is set
// (the card refused them, and libfat's sync said yes).
struct CardFile {
    bool losing = false;
    std::uint64_t lose_from = 0;
    std::function<bool(const std::string&, std::uint64_t, std::uint32_t, std::uint8_t*)> reader() {
        return [this](const std::string& path, std::uint64_t o, std::uint32_t n, std::uint8_t* out) {
            std::FILE* f = std::fopen(path.c_str(), "rb");
            if (!f) return false;
            const bool ok = std::fseek(f, long(o), SEEK_SET) == 0 && std::fread(out, 1, n, f) == n;
            std::fclose(f);
            if (ok && losing)
                for (std::uint32_t i = 0; i < n; i++)
                    if (o + i >= lose_from) out[i] ^= 0x5A;
            return ok;
        };
    }
};

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
    // A folder of this run's own: its process ID and the clock, emptied first.
    fs::path dir = fs::temp_directory_path() /
                   ("ctgpjournal_test_" + std::to_string(::getpid()) + "_" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string jp = (dir / "j.bin").string();
    const std::size_t size = 6 * kChunk;
    std::string err;

    {   // writes, then recovery restores the original
        MemPack p = make_pack(size);
        auto orig = p.data;
        {
            CtgpPackJournal j;
            EXPECT_TRUE(j.begin(jp, p.io(), size, err));
            do_writes(j.io());
            EXPECT_EQ(j.chunks(), std::size_t(4));
        }  // a crash: the journal's file is closed (Windows can't remove an open file)
        EXPECT_TRUE(p.data != orig);
        std::size_t n = 0;
        // Simulate a crash: journal object just stops; recovery runs at next start.
        EXPECT_TRUE(ctgp_journal_recover(jp, p.io(), size, p.sync(), n, err) == CtgpJournalRecovery::RolledBack);
        EXPECT_EQ(n, std::size_t(4));
        EXPECT_TRUE(p.data == orig);
        EXPECT_TRUE(!exists(jp));
        EXPECT_TRUE(ctgp_journal_recover(jp, p.io(), size, p.sync(), n, err) == CtgpJournalRecovery::None);
    }
    {   // a check before the journal goes (jelly.bin's entry) that fails keeps
        // the journal, so the next start checks again
        MemPack p = make_pack(size);
        auto orig = p.data;
        {
            CtgpPackJournal j;
            EXPECT_TRUE(j.begin(jp, p.io(), size, err));
            do_writes(j.io());
        }
        std::size_t n = 0;
        const auto refuse = [](std::string& why) {
            why = "the entry changed";
            return false;
        };
        EXPECT_TRUE(ctgp_journal_recover(jp, p.io(), size, p.sync(), n, err, refuse) == CtgpJournalRecovery::Failed);
        EXPECT_TRUE(err == "the entry changed");
        EXPECT_TRUE(exists(jp));
        EXPECT_TRUE(p.data == orig);  // put back; only the removal waits
        const auto accept = [](std::string&) { return true; };
        EXPECT_TRUE(ctgp_journal_recover(jp, p.io(), size, p.sync(), n, err, accept) ==
                    CtgpJournalRecovery::RolledBack);
        EXPECT_TRUE(!exists(jp));
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
    {   // a commit whose pack sync fails keeps the journal, and rollback still works after it
        MemPack p = make_pack(size);
        auto orig = p.data;
        CtgpPackJournal j;
        EXPECT_TRUE(j.begin(jp, p.io(), size, err));
        do_writes(j.io());
        EXPECT_TRUE(!j.commit([]() { return false; }, err));
        EXPECT_TRUE(exists(jp));
        EXPECT_TRUE(j.rollback(p.sync(), err));
        EXPECT_TRUE(p.data == orig);
        EXPECT_TRUE(!exists(jp));
        EXPECT_TRUE(!j.rollback(p.sync(), err));  // nothing left to roll back
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
        {
            CtgpPackJournal j;
            EXPECT_TRUE(j.begin(jp, p.io(), size, err));
            do_writes(j.io());
        }  // a crash, the journal's file closed
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

        // Bad checksum on a whole last entry: that entry may have been synced
        // before its chunk changed, so the replay fails and keeps the journal.
        auto bad = jd;
        bad[bad.size() - 1] ^= 0xFF;
        spit(jp, bad);
        MemPack r = p;
        EXPECT_TRUE(ctgp_journal_recover(jp, r.io(), size, r.sync(), n, err) == CtgpJournalRecovery::Failed);
        EXPECT_TRUE(fs::exists(jp));
        fs::remove(jp);
    }
    {   // a damaged entry with more after it: the journal itself is damaged,
        // so the replay fails and the journal is kept for another try
        MemPack p = make_pack(size);
        {
            CtgpPackJournal j;
            EXPECT_TRUE(j.begin(jp, p.io(), size, err));
            do_writes(j.io());
        }  // a crash, the journal's file closed
        auto jd = slurp(jp);
        jd[kHeader + 12 + 100] ^= 0xFF;  // inside the first entry's data
        spit(jp, jd);
        std::size_t n = 0;
        EXPECT_TRUE(ctgp_journal_recover(jp, p.io(), size, p.sync(), n, err) == CtgpJournalRecovery::Failed);
        EXPECT_TRUE(!err.empty());
        EXPECT_TRUE(exists(jp));
        std::remove(jp.c_str());
    }
    {   // pack size mismatch: nothing written, journal kept
        MemPack p = make_pack(size);
        {
            CtgpPackJournal j;
            EXPECT_TRUE(j.begin(jp, p.io(), size, err));
            do_writes(j.io());
        }  // a crash, the journal's file closed
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
        // a failed pack write leaves the update uncommittable, and refuses
        // later writes, until it is rolled back
        EXPECT_TRUE(!j.io().write(20, a.data(), 64));
        EXPECT_TRUE(!j.commit(p.sync(), err));
        EXPECT_TRUE(exists(jp));
        EXPECT_TRUE(j.rollback(p.sync(), err));
        EXPECT_TRUE(p.data == orig);

        CtgpPackJournal k;
        EXPECT_TRUE(!k.begin((dir / "nodir" / "j.bin").string(), p.io(), size, err));
        EXPECT_TRUE(!k.io().write(0, a.data(), 64));  // not begun: refused, pack untouched
        EXPECT_TRUE(p.data == orig);

        // Journaling fails (pack read fails): the pack write must not happen.
        bool reads_fail = false;
        CtgpPackIo bad = p.io();
        const auto good_read = bad.read;
        bad.read = [&reads_fail, good_read](std::uint64_t o, std::uint32_t n, std::uint8_t* out) {
            return !reads_fail && good_read(o, n, out);
        };
        CtgpPackJournal m;
        EXPECT_TRUE(m.begin(jp, bad, size, err));
        reads_fail = true;
        EXPECT_TRUE(!m.io().write(0, a.data(), 64));
        EXPECT_TRUE(p.data == orig);
        std::remove(jp.c_str());
    }

    {   // a journal that is already there is never emptied by a new begin
        MemPack p = make_pack(size);
        const auto old = filled(100, 0x5A);
        spit(jp, old);
        CtgpPackJournal j;
        EXPECT_TRUE(!j.begin(jp, p.io(), size, err));
        EXPECT_TRUE(slurp(jp) == old);
        std::remove(jp.c_str());
    }
#ifndef _WIN32
    {   // a lookup that fails for another reason than "not there" (here a name
        // too long for the file system) is not taken as "no journal"
        MemPack p = make_pack(size);
        const std::string odd = (dir / std::string(300, 'j')).string();
        std::size_t n = 0;
        EXPECT_TRUE(ctgp_journal_recover(odd, p.io(), size, p.sync(), n, err) == CtgpJournalRecovery::Failed);
        EXPECT_TRUE(!err.empty());
        CtgpPackJournal j;
        EXPECT_TRUE(!j.begin(odd, p.io(), size, err));
        // whereas a missing folder on the way is "not there"
        EXPECT_TRUE(ctgp_journal_recover((dir / "nodir" / "j.bin").string(), p.io(), size, p.sync(), n, err) ==
                    CtgpJournalRecovery::None);
    }
#endif

    {   // a journal entry the card did not keep: the pack is not written, the
        // update can't be committed, and the journal stays for the undo
        CachedPack p = make_cached(size, 0);
        const auto orig = p.cache;
        CardFile card;
        CtgpPackIo io = p.io();
        io.read_card_file = card.reader();
        CtgpPackJournal j;
        EXPECT_TRUE(j.begin(jp, io, size, err));
        auto a = filled(kChunk, 0xA1);
        EXPECT_TRUE(j.io().write(0, a.data(), kChunk));
        card.losing = true;
        const auto confirmed = fs::file_size(jp);
        card.lose_from = confirmed;  // the next entry never reaches the card
        auto b = filled(1024, 0xB2);
        EXPECT_TRUE(!j.io().write(2 * kChunk, b.data(), 1024));
#if defined(__unix__) || defined(__APPLE__)
        // Cut back to the confirmed entries, so a start-up replay never meets
        // the failed one (whole, with a bad checksum on a real card).
        EXPECT_TRUE(fs::file_size(jp) == confirmed);
#endif
        EXPECT_TRUE(std::equal(orig.begin() + 2 * kChunk, orig.begin() + 3 * kChunk, p.cache.begin() + 2 * kChunk));
        EXPECT_TRUE(!j.io().write(4 * kChunk, b.data(), 1024));  // nothing after it either
        EXPECT_TRUE(std::equal(orig.begin() + 4 * kChunk, orig.end(), p.cache.begin() + 4 * kChunk));
        EXPECT_TRUE(!j.commit(p.sync(), err));
        EXPECT_TRUE(exists(jp));
        card.losing = false;
        EXPECT_TRUE(j.rollback(p.sync(), err));
        EXPECT_TRUE(p.cache == orig && p.card == orig);
        EXPECT_TRUE(!exists(jp));

        // A header the card did not keep: no journal, nothing written.
        card.losing = true;
        card.lose_from = 0;
        CtgpPackJournal k;
        EXPECT_TRUE(!k.begin(jp, io, size, err));
        EXPECT_TRUE(!err.empty());
        EXPECT_TRUE(!exists(jp));
        EXPECT_TRUE(!k.io().write(0, a.data(), kChunk));
        EXPECT_TRUE(p.cache == orig);
    }
    {   // the pack's sync says yes but the card lost a block: commit keeps the
        // journal; a rollback whose own writes are lost keeps it too; the
        // start-up replay then puts the card back
        CachedPack p = make_cached(size, 0);
        const auto orig = p.cache;
        CtgpPackJournal j;
        EXPECT_TRUE(j.begin(jp, p.io(), size, err));
        do_writes(j.io());
        p.lose = {(2 * kChunk - 512) / 512};  // inside the write that spans chunks 1 and 2
        EXPECT_TRUE(!j.commit(p.sync(), err));
        EXPECT_TRUE(!err.empty());
        EXPECT_TRUE(exists(jp));
        // Chunk 3's new bytes did reach the card; putting them back is lost.
        p.lose = {(3 * kChunk + 77) / 512};
        EXPECT_TRUE(!j.rollback(p.sync(), err));
        EXPECT_TRUE(exists(jp));
        EXPECT_TRUE(p.card != orig);
        p.lose.clear();
        std::size_t n = 0;
        EXPECT_TRUE(ctgp_journal_recover(jp, p.io(), size, p.sync(), n, err) == CtgpJournalRecovery::RolledBack);
        EXPECT_TRUE(p.card == orig && p.cache == orig);
        EXPECT_TRUE(!exists(jp));

        // A start-up replay the card does not keep fails and keeps the journal.
        {
            CtgpPackJournal k;
            EXPECT_TRUE(k.begin(jp, p.io(), size, err));
            do_writes(k.io());
        }  // a crash, the journal's file closed
        EXPECT_TRUE(p.sync()());
        p.lose = {0};
        EXPECT_TRUE(ctgp_journal_recover(jp, p.io(), size, p.sync(), n, err) == CtgpJournalRecovery::Failed);
        EXPECT_TRUE(exists(jp));
        p.lose.clear();
        EXPECT_TRUE(ctgp_journal_recover(jp, p.io(), size, p.sync(), n, err) == CtgpJournalRecovery::RolledBack);
        EXPECT_TRUE(p.card == orig);

        // And when the card takes everything, commit checks it and goes through.
        CtgpPackJournal m;
        EXPECT_TRUE(m.begin(jp, p.io(), size, err));
        do_writes(m.io());
        const auto changed = p.cache;
        EXPECT_TRUE(m.commit(p.sync(), err));
        EXPECT_TRUE(p.card == changed && changed != orig);
        EXPECT_TRUE(!exists(jp));
    }
    {   // CTGP-R installed again over a cut-off update: the new pack has the
        // same size but other bytes and another identity. The old journal is
        // refused, the new pack untouched, and the journal kept.
        CachedPack old_pack = make_cached(size, 0);
        old_pack.identity = identity_of(100, 0x1000);
        const auto old_orig = old_pack.cache;
        {
            CtgpPackJournal j;
            EXPECT_TRUE(j.begin(jp, old_pack.io(), size, err));
            do_writes(j.io());
            // power cut: the journal object just stops
        }
        CachedPack fresh = make_cached(size, 99);
        fresh.identity = identity_of(200, 0x2000);
        const auto fresh_orig = fresh.cache;
        std::size_t n = 0;
        EXPECT_TRUE(ctgp_journal_recover(jp, fresh.io(), size, fresh.sync(), n, err) == CtgpJournalRecovery::Failed);
        EXPECT_TRUE(!err.empty());
        EXPECT_EQ(n, std::size_t(0));
        EXPECT_TRUE(fresh.cache == fresh_orig && fresh.card == fresh_orig);
        EXPECT_TRUE(exists(jp));
        // Same size, same bytes as the old pack, but a new copy (other
        // creation time): refused too.
        CachedPack copy = old_pack;
        copy.identity = identity_of(100, 0x1002);
        const auto copy_bytes = copy.cache;
        EXPECT_TRUE(ctgp_journal_recover(jp, copy.io(), size, copy.sync(), n, err) == CtgpJournalRecovery::Failed);
        EXPECT_TRUE(copy.cache == copy_bytes);
        // A pack that can't be identified is not written either.
        CachedPack unknown = fresh;
        unknown.identity = CtgpPackIdentity();
        EXPECT_TRUE(ctgp_journal_recover(jp, unknown.io(), size, unknown.sync(), n, err) == CtgpJournalRecovery::Failed);
        EXPECT_TRUE(unknown.cache == fresh_orig);
        EXPECT_TRUE(exists(jp));
        // The pack it was made for: played back.
        EXPECT_TRUE(ctgp_journal_recover(jp, old_pack.io(), size, old_pack.sync(), n, err) ==
                    CtgpJournalRecovery::RolledBack);
        EXPECT_TRUE(old_pack.cache == old_orig && old_pack.card == old_orig);
        EXPECT_TRUE(!exists(jp));
    }
    {   // the identity: write time counts only when asked for
        Fat32Entry e;
        e.size = 1234;
        e.first_cluster = 77;
        e.create_time = 1;
        e.create_date = 2;
        e.write_time = 3;
        e.write_date = 4;
        Fat32Entry later = e;
        later.write_time = 9;
        EXPECT_TRUE(ctgp_pack_identity(e, false).bytes == ctgp_pack_identity(later, false).bytes);
        EXPECT_TRUE(ctgp_pack_identity(e, true).bytes != ctgp_pack_identity(later, true).bytes);
        Fat32Entry moved = e;
        moved.first_cluster = 78;
        EXPECT_TRUE(ctgp_pack_identity(e, false).bytes != ctgp_pack_identity(moved, false).bytes);
        EXPECT_TRUE(ctgp_pack_identity(e, false).known);
    }
    {   // a version 1 journal (no identity) and a damaged header: refused, kept
        MemPack p = make_pack(size);
        const auto orig = p.data;
        std::vector<std::uint8_t> v1 = {'C', 'J', 'R', 'N', 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x80, 0};
        v1[8 + 4] = std::uint8_t(size >> 24);
        v1[8 + 5] = std::uint8_t(size >> 16);
        v1[8 + 6] = std::uint8_t(size >> 8);
        v1[8 + 7] = std::uint8_t(size);
        v1.resize(v1.size() + 12 + 512 + 4, 0x11);
        spit(jp, v1);
        std::size_t n = 0;
        EXPECT_TRUE(ctgp_journal_recover(jp, p.io(), size, p.sync(), n, err) == CtgpJournalRecovery::Failed);
        EXPECT_TRUE(p.data == orig);
        EXPECT_TRUE(exists(jp));
        // The development builds' version 2 (a 44-byte header, no samples):
        // refused too, however long the file.
        v1[7] = 2;
        v1.resize(4096, 0x22);
        spit(jp, v1);
        EXPECT_TRUE(ctgp_journal_recover(jp, p.io(), size, p.sync(), n, err) == CtgpJournalRecovery::Failed);
        EXPECT_TRUE(p.data == orig);
        EXPECT_TRUE(exists(jp));
        std::remove(jp.c_str());

        CtgpPackJournal j;
        EXPECT_TRUE(j.begin(jp, p.io(), size, err));
        do_writes(j.io());
        auto jd = slurp(jp);
        jd[30] ^= 0x01;  // inside the identity
        spit(jp, jd);
        const auto changed = p.data;
        EXPECT_TRUE(ctgp_journal_recover(jp, p.io(), size, p.sync(), n, err) == CtgpJournalRecovery::Failed);
        EXPECT_TRUE(p.data == changed);
        EXPECT_TRUE(exists(jp));
        std::remove(jp.c_str());
    }

    {   // CTGP-R rewritten in place over a cut-off update: the same size, first
        // cluster and creation time (one identity), other contents. The
        // samples outside the journal's chunks differ: refused, untouched.
        CachedPack old_pack = make_cached(size, 0);
        old_pack.identity = identity_of(100, 0x1000);
        const auto old_orig = old_pack.cache;
        {
            CtgpPackJournal j;
            EXPECT_TRUE(j.begin(jp, old_pack.io(), size, err));
            do_writes(j.io());  // chunks 0 to 3; 4 and 5 untouched
        }
        CachedPack rewritten = make_cached(size, 42);
        rewritten.identity = old_pack.identity;
        const auto rewritten_orig = rewritten.cache;
        std::size_t n = 0;
        EXPECT_TRUE(ctgp_journal_recover(jp, rewritten.io(), size, rewritten.sync(), n, err) ==
                    CtgpJournalRecovery::Failed);
        EXPECT_TRUE(!err.empty());
        EXPECT_EQ(n, std::size_t(0));
        EXPECT_TRUE(rewritten.cache == rewritten_orig && rewritten.card == rewritten_orig);
        EXPECT_TRUE(exists(jp));
        // One byte in an untouched chunk's sampled sector is enough.
        CachedPack nearly = old_pack;
        nearly.cache[5 * kChunk + 3] ^= 1;
        nearly.card = nearly.cache;
        const auto nearly_orig = nearly.cache;
        EXPECT_TRUE(ctgp_journal_recover(jp, nearly.io(), size, nearly.sync(), n, err) == CtgpJournalRecovery::Failed);
        EXPECT_TRUE(nearly.cache == nearly_orig);
        // Bytes the update itself changed are not compared: the pack it was
        // made for, half updated, is played back.
        EXPECT_TRUE(ctgp_journal_recover(jp, old_pack.io(), size, old_pack.sync(), n, err) ==
                    CtgpJournalRecovery::RolledBack);
        EXPECT_TRUE(old_pack.cache == old_orig);
        EXPECT_TRUE(!exists(jp));
    }
    {   // begin fails cleanly, with no journal made, when the pack can't be
        // read for its samples or the bookkeeping would not fit in memory
        MemPack p = make_pack(size);
        CtgpPackIo io = p.io();
        io.read = [](std::uint64_t, std::uint32_t, std::uint8_t*) { return false; };
        CtgpPackJournal j;
        EXPECT_TRUE(!j.begin(jp, io, size, err));
        EXPECT_TRUE(!exists(jp));
        CtgpPackJournal k;
        EXPECT_TRUE(!k.begin(jp, p.io(), std::uint64_t(1) << 60, err));
        EXPECT_TRUE(!err.empty());
        EXPECT_TRUE(!exists(jp));
    }

    fs::remove_all(dir);
    if (g_failures == 0) std::cout << "ctgpjournal: all tests passed" << std::endl;
    return g_failures ? 1 : 0;
}
