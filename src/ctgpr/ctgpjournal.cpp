// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgpjournal.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>

#if defined(__unix__) || defined(__APPLE__) || defined(__PPC__)
#include <unistd.h>
#define RIFTWII_JOURNAL_FSYNC 1
#endif

namespace riftwii {

namespace {

// Header: "CJRN", version, pack size (u64), chunk size. Entry: offset (u64),
// length (u32), data, checksum (u32) over offset, length and data. All big-endian.
constexpr std::uint32_t kMagic = 0x434A524Eu;
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kHeaderBytes = 20;

std::uint32_t crc_update(std::uint32_t crc, const std::uint8_t* data, std::size_t size) {
    crc = ~crc;
    for (std::size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void put32(std::uint8_t* p, std::uint32_t v) {
    p[0] = std::uint8_t(v >> 24); p[1] = std::uint8_t(v >> 16); p[2] = std::uint8_t(v >> 8); p[3] = std::uint8_t(v);
}
void put64(std::uint8_t* p, std::uint64_t v) { put32(p, std::uint32_t(v >> 32)); put32(p + 4, std::uint32_t(v)); }
std::uint32_t get32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}
std::uint64_t get64(const std::uint8_t* p) { return (std::uint64_t(get32(p)) << 32) | get32(p + 4); }

bool sync_file(std::FILE* f) {
    if (std::fflush(f) != 0) return false;
#ifdef RIFTWII_JOURNAL_FSYNC
    if (fsync(fileno(f)) != 0) return false;
#endif
    return true;
}

bool file_exists(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

struct Entry {
    std::uint64_t offset;
    std::vector<std::uint8_t> data;
};

enum class EntryRead { Ok, Bad, ReadError };

// Reads the entry at `pos` of a journal `size` bytes long. Bad: the entry is
// torn or damaged (its bytes are there but do not check out, or the file
// ends inside it). ReadError: the card did not give bytes the file has.
EntryRead read_entry(std::FILE* f, std::uint64_t pos, std::uint64_t size, std::uint64_t pack_size, Entry& e,
                     std::uint32_t& length) {
    std::uint8_t head[12];
    if (size - pos < sizeof head) return EntryRead::Bad;
    if (std::fread(head, 1, sizeof head, f) != sizeof head) return EntryRead::ReadError;
    e.offset = get64(head);
    length = get32(head + 8);
    if (length == 0 || length > kCtgpJournalChunkBytes || e.offset > pack_size || length > pack_size - e.offset)
        return EntryRead::Bad;
    if (size - pos < sizeof head + length + 4) return EntryRead::Bad;
    e.data.resize(length);
    std::uint8_t tail[4];
    if (std::fread(e.data.data(), 1, length, f) != length || std::fread(tail, 1, 4, f) != 4)
        return EntryRead::ReadError;
    std::uint32_t crc = crc_update(0, head, sizeof head);
    crc = crc_update(crc, e.data.data(), e.data.size());
    return crc == get32(tail) ? EntryRead::Ok : EntryRead::Bad;
}

// Writes the entries of the open journal back to the pack, in file order.
// Each entry is synced to the card before its chunk of the pack is changed,
// so only the last append can be torn, and its chunk was never written: a
// bad entry within one entry's length of the end is that, and the replay
// stops there. A bad entry with more after it, or a read error, means the
// journal itself is damaged: the replay fails and the journal is kept.
CtgpJournalRecovery play_back(std::FILE* f, const CtgpPackIo& pack, std::uint64_t pack_size, std::size_t& restored,
                              std::string& error) {
    restored = 0;
    if (std::fseek(f, 0, SEEK_END) != 0) {
        error = "the journal could not be read";
        return CtgpJournalRecovery::Failed;
    }
    const long end = std::ftell(f);
    if (end < 0 || std::fseek(f, 0, SEEK_SET) != 0) {
        error = "the journal could not be read";
        return CtgpJournalRecovery::Failed;
    }
    const std::uint64_t size = std::uint64_t(end);
    std::uint8_t header[kHeaderBytes];
    if (size < sizeof header) {
        // Never got as far as a header: no pack write can have happened.
        return CtgpJournalRecovery::RolledBack;
    }
    if (std::fread(header, 1, sizeof header, f) != sizeof header) {
        error = "the journal could not be read";
        return CtgpJournalRecovery::Failed;
    }
    if (get32(header) != kMagic || get32(header + 4) != kVersion || get32(header + 16) != kCtgpJournalChunkBytes) {
        error = "journal header is not recognised";
        return CtgpJournalRecovery::Failed;
    }
    if (get64(header + 8) != pack_size) {
        error = "journal belongs to a pack of another size";
        return CtgpJournalRecovery::Failed;
    }
    constexpr std::uint64_t kLongestEntry = 12 + kCtgpJournalChunkBytes + 4;
    std::uint64_t pos = sizeof header;
    Entry e;
    while (pos < size) {
        std::uint32_t length = 0;
        const EntryRead read = read_entry(f, pos, size, pack_size, e, length);
        if (read == EntryRead::ReadError) {
            error = "the journal could not be read";
            return CtgpJournalRecovery::Failed;
        }
        if (read == EntryRead::Bad) {
            if (size - pos <= kLongestEntry) break;  // the torn last append
            error = "the journal is damaged";
            return CtgpJournalRecovery::Failed;
        }
        if (!pack.write || !pack.write(e.offset, e.data.data(), std::uint32_t(e.data.size()))) {
            error = "writing a journaled chunk back failed";
            return CtgpJournalRecovery::Failed;
        }
        restored++;
        pos += 12 + std::uint64_t(length) + 4;
    }
    return CtgpJournalRecovery::RolledBack;
}

CtgpJournalRecovery play_back_file(const std::string& path, const CtgpPackIo& pack, std::uint64_t pack_size,
                                   const std::function<bool()>& sync_pack, std::size_t& restored, std::string& error) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        error = "cannot open the journal";
        return CtgpJournalRecovery::Failed;
    }
    CtgpJournalRecovery result = play_back(f, pack, pack_size, restored, error);
    std::fclose(f);
    if (result != CtgpJournalRecovery::RolledBack) return result;
    if (sync_pack && !sync_pack()) {
        error = "syncing the pack failed";
        return CtgpJournalRecovery::Failed;
    }
    if (std::remove(path.c_str()) != 0) {
        error = "cannot remove the journal";
        return CtgpJournalRecovery::Failed;
    }
    return CtgpJournalRecovery::RolledBack;
}

}  // namespace

struct CtgpPackJournal::Impl {
    std::string path;
    CtgpPackIo pack;
    std::uint64_t pack_size = 0;
    std::FILE* file = nullptr;
    std::set<std::uint64_t> done;  // chunk indices already journaled
    // Set when an entry could not be appended or synced: the file may end in
    // a torn entry, so nothing more goes after it and the pack is not
    // written again until the journal is played back.
    bool failed = false;

    bool journal_chunk(std::uint64_t index) {
        std::uint64_t offset = index * kCtgpJournalChunkBytes;
        if (offset >= pack_size) return true;  // beyond the pack: the pack write will fail by itself
        std::uint32_t length = std::uint32_t(std::min<std::uint64_t>(kCtgpJournalChunkBytes, pack_size - offset));
        std::vector<std::uint8_t> buf(12 + length + 4);
        put64(buf.data(), offset);
        put32(buf.data() + 8, length);
        if (!pack.read || !pack.read(offset, length, buf.data() + 12)) return false;
        put32(buf.data() + 12 + length, crc_update(0, buf.data(), 12 + length));
        if (std::fwrite(buf.data(), 1, buf.size(), file) != buf.size() || !sync_file(file)) {
            failed = true;
            return false;
        }
        done.insert(index);
        return true;
    }

    bool write(std::uint64_t offset, const std::uint8_t* data, std::uint32_t length) {
        if (!file || !pack.write || failed) return false;
        if (length == 0) return pack.write(offset, data, length);
        std::uint64_t first = offset / kCtgpJournalChunkBytes;
        std::uint64_t last = (offset + length - 1) / kCtgpJournalChunkBytes;
        for (std::uint64_t i = first; i <= last; i++)
            if (!done.count(i) && !journal_chunk(i)) return false;
        return pack.write(offset, data, length);
    }

    void close() {
        if (file) std::fclose(file);
        file = nullptr;
    }
};

CtgpPackJournal::CtgpPackJournal() : impl_(new Impl) {}
CtgpPackJournal::~CtgpPackJournal() {
    impl_->close();
    delete impl_;
}

bool CtgpPackJournal::begin(const std::string& path, const CtgpPackIo& pack, std::uint64_t pack_size, std::string& error) {
    error.clear();
    if (impl_->file) {
        error = "journal already started";
        return false;
    }
    if (file_exists(path)) {
        error = "a journal from an earlier update is still there";
        return false;
    }
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        error = "cannot create the journal";
        return false;
    }
    std::uint8_t header[kHeaderBytes];
    put32(header, kMagic);
    put32(header + 4, kVersion);
    put64(header + 8, pack_size);
    put32(header + 16, kCtgpJournalChunkBytes);
    if (std::fwrite(header, 1, sizeof header, f) != sizeof header || !sync_file(f)) {
        std::fclose(f);
        std::remove(path.c_str());
        error = "cannot write the journal header";
        return false;
    }
    impl_->path = path;
    impl_->pack = pack;
    impl_->pack_size = pack_size;
    impl_->file = f;
    impl_->done.clear();
    impl_->failed = false;
    return true;
}

CtgpPackIo CtgpPackJournal::io() {
    CtgpPackIo out;
    Impl* impl = impl_;
    out.read = [impl](std::uint64_t offset, std::uint32_t length, std::uint8_t* buf) {
        return impl->pack.read && impl->pack.read(offset, length, buf);
    };
    out.write = [impl](std::uint64_t offset, const std::uint8_t* data, std::uint32_t length) {
        return impl->write(offset, data, length);
    };
    return out;
}

bool CtgpPackJournal::commit(const std::function<bool()>& sync_pack, std::string& error) {
    error.clear();
    if (!impl_->file) {
        error = "no journal is open";
        return false;
    }
    if (impl_->failed) {
        error = "the journal could not be written; the update must be rolled back";
        return false;
    }
    if (sync_pack && !sync_pack()) {
        error = "syncing the pack failed; the journal is kept";
        return false;
    }
    impl_->close();
    if (std::remove(impl_->path.c_str()) != 0) {
        error = "cannot remove the journal";
        return false;
    }
    return true;
}

bool CtgpPackJournal::rollback(const std::function<bool()>& sync_pack, std::string& error) {
    error.clear();
    if (!impl_->file) {
        error = "no journal is open";
        return false;
    }
    impl_->close();
    std::size_t restored = 0;
    return play_back_file(impl_->path, impl_->pack, impl_->pack_size, sync_pack, restored, error) ==
           CtgpJournalRecovery::RolledBack;
}

std::size_t CtgpPackJournal::chunks() const { return impl_->done.size(); }

CtgpJournalRecovery ctgp_journal_recover(const std::string& path, const CtgpPackIo& pack, std::uint64_t pack_size,
                                         const std::function<bool()>& sync_pack, std::size_t& restored,
                                         std::string& error) {
    error.clear();
    restored = 0;
    if (!file_exists(path)) return CtgpJournalRecovery::None;
    return play_back_file(path, pack, pack_size, sync_pack, restored, error);
}

}  // namespace riftwii
