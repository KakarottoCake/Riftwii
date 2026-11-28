// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgpjournal.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#include "riftwii/checkedalloc.hpp"

#if defined(__unix__) || defined(__APPLE__) || defined(__PPC__)
#include <unistd.h>
#define RIFTWII_JOURNAL_FSYNC 1
#endif

namespace riftwii {

namespace {

// Header: "CJRN", version, pack size (u64), chunk size, flags (bit 0: the
// pack's identity is known), the pack's identity (16 bytes), the number of
// content samples (u32) and their checksums (u32 each, see below), checksum
// (u32) over everything before it. Entry: offset (u64), length (u32), data,
// checksum (u32) over offset, length and data. All big-endian. Any other
// version is refused: version 1 had no identity, and development builds
// wrote a version 2 with a shorter header and no samples.
constexpr std::uint32_t kMagic = 0x434A524Eu;
constexpr std::uint32_t kVersion = 3;
constexpr std::uint32_t kFlagIdentity = 1;
constexpr std::size_t kEntryOverhead = 12 + 4;

// The pack's content, sampled when the journal starts: the first 512 bytes
// of kSamples chunks spread evenly over it. The identity comes from the
// directory entry, which a reinstall that rewrites blob.bin in place can keep
// whole (same size, first cluster and creation time). So before a replay
// writes anything, every sample in a chunk no journal entry covers is read
// again and must match. Those chunks are untouched by this update: the
// journal saves a chunk, and reads the entry back from the card, before the
// pack is written there at all. A pack whose sampled bytes differ is another
// pack; one that differs only between the samples is not caught, so this is
// a spot check. 128 sectors are 64 KiB to read, a moment on the Wii. Reading
// more is not cheap: 4 KiB of every chunk of a 1 GiB pack is 128 MiB, some
// 15 to 20 seconds on a Wii's SD card at every update and every replay.
constexpr std::uint32_t kSamples = 128;
constexpr std::uint32_t kSampleBytes = 512;
constexpr std::size_t kSampleCountAt = 40;
constexpr std::size_t kSamplesAt = 44;
constexpr std::size_t kHeaderCrcAt = kSamplesAt + 4 * kSamples;
constexpr std::size_t kHeaderBytes = kHeaderCrcAt + 4;

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
void put16(std::uint8_t* p, std::uint16_t v) { p[0] = std::uint8_t(v >> 8); p[1] = std::uint8_t(v); }

// Room for one entry, or one chunk read back from the card.
bool scratch_room(std::vector<std::uint8_t>& scratch, std::size_t bytes) {
    return scratch.size() >= bytes || try_resize(scratch, bytes);
}

// Whether the card itself holds `expected` at `offset` of the pack.
bool card_holds(const CtgpPackIo& pack, std::uint64_t offset, std::uint32_t length, std::uint32_t expected_crc,
                std::vector<std::uint8_t>& scratch) {
    if (!scratch_room(scratch, length)) return false;
    if (!pack.read_card(offset, length, scratch.data())) return false;
    return crc_update(0, scratch.data(), length) == expected_crc;
}

std::uint64_t chunk_count(std::uint64_t pack_size) {
    return (pack_size + kCtgpJournalChunkBytes - 1) / kCtgpJournalChunkBytes;
}

// Sample `i`'s offset in the pack and length (see kSamples).
void sample_at(std::uint64_t pack_size, std::uint32_t i, std::uint64_t& offset, std::uint32_t& length) {
    const std::uint64_t chunks = chunk_count(pack_size);
    const std::uint64_t chunk = chunks <= 1 ? 0 : i * (chunks - 1) / (kSamples - 1);
    offset = chunk * kCtgpJournalChunkBytes;
    length = std::uint32_t(std::min<std::uint64_t>(kSampleBytes, pack_size - offset));
}

// A set of chunk indices as one bit each, sized when it is made.
struct ChunkBits {
    std::vector<std::uint8_t> bits;
    bool size_for(std::uint64_t chunks) { return try_assign(bits, std::size_t((chunks + 7) / 8), std::uint8_t(0)); }
    bool has(std::uint64_t i) const { return i / 8 < bits.size() && (bits[i / 8] & (1u << (i % 8))) != 0; }
    void add(std::uint64_t i) {
        if (i / 8 < bits.size()) bits[i / 8] |= std::uint8_t(1u << (i % 8));
    }
};

// A chunk written (by the update or a replay) and what it should now hold.
struct Written {
    std::uint64_t offset;
    std::uint32_t length;
    std::uint32_t crc;
};

bool sync_file(std::FILE* f) {
    if (std::fflush(f) != 0) return false;
#ifdef RIFTWII_JOURNAL_FSYNC
    if (fsync(fileno(f)) != 0) return false;
#endif
    return true;
}

// Absent only when the card says the file is not there: any other failure
// (an SD read error) is Unknown, and nothing then goes ahead as if no
// journal were there.
enum class Presence { Absent, Present, Unknown };
Presence presence(const std::string& path) {
    struct stat st;
    errno = 0;
    if (stat(path.c_str(), &st) == 0) return Presence::Present;
    return errno == ENOENT || errno == ENOTDIR ? Presence::Absent : Presence::Unknown;
}

struct Entry {
    std::uint64_t offset;
    std::vector<std::uint8_t> data;
};

enum class EntryRead { Ok, Torn, Bad, BadSum, ReadError };

// Reads the entry at `pos` of a journal `size` bytes long. Torn: the file
// ends inside the entry (a cut-off last append). Bad: the entry's bytes are
// all there but do not check out. ReadError: the card did not give bytes the
// file has. BadSum: a Bad entry whose head reads as one (offset and length
// within the pack) and whose bytes are all there, but whose checksum, given
// in `stored_crc`, does not match them.
EntryRead read_entry(std::FILE* f, std::uint64_t pos, std::uint64_t size, std::uint64_t pack_size, Entry& e,
                     std::uint32_t& length, std::uint32_t& stored_crc) {
    std::uint8_t head[12];
    if (size - pos < sizeof head) return EntryRead::Torn;
    if (std::fread(head, 1, sizeof head, f) != sizeof head) return EntryRead::ReadError;
    e.offset = get64(head);
    length = get32(head + 8);
    if (length == 0 || length > kCtgpJournalChunkBytes || e.offset > pack_size || length > pack_size - e.offset)
        return EntryRead::Bad;
    if (size - pos < sizeof head + length + 4) return EntryRead::Torn;
    if (!try_resize(e.data, length)) return EntryRead::ReadError;
    std::uint8_t tail[4];
    if (std::fread(e.data.data(), 1, length, f) != length || std::fread(tail, 1, 4, f) != 4)
        return EntryRead::ReadError;
    std::uint32_t crc = crc_update(0, head, sizeof head);
    crc = crc_update(crc, e.data.data(), e.data.size());
    stored_crc = get32(tail);
    return crc == stored_crc ? EntryRead::Ok : EntryRead::BadSum;
}

// Whether the pack's chunk under entry `e` (head as read, `length` bytes)
// holds what the entry's checksum `stored_crc` covers: the checksum runs
// over the head and the chunk's bytes as they were before the update. The
// pack is read as the update would see it and, with a card reader, as the
// card holds it: both must match. The entry's own (damaged) bytes are read
// over, so no memory is asked for. False only when a read failed.
bool pack_chunk_is(const CtgpPackIo& pack, Entry& e, std::uint32_t length, std::uint32_t stored_crc,
                   bool& unchanged) {
    unchanged = false;
    if (!pack.read || e.data.size() != length) return false;
    std::uint8_t head[12];
    put64(head, e.offset);
    put32(head + 8, length);
    const std::uint32_t head_crc = crc_update(0, head, sizeof head);
    if (!pack.read(e.offset, length, e.data.data())) return false;
    if (crc_update(head_crc, e.data.data(), length) != stored_crc) return true;
    if (pack.read_card) {
        if (!pack.read_card(e.offset, length, e.data.data())) return false;
        if (crc_update(head_crc, e.data.data(), length) != stored_crc) return true;
    }
    unchanged = true;
    return true;
}

// Writes the entries of the open journal back to the pack, in file order.
// Each entry is synced to the card before its chunk of the pack is changed,
// so only the last append can be cut off, and its chunk was never written:
// a file that ends inside an entry is that, and the replay stops there. An
// entry whose bytes are all there but don't check out is passed over only
// when the pack still holds, by that entry's checksum, the chunk as it was
// (an append that failed and was never cut back); otherwise it, or a read
// error, means the journal itself is damaged: the replay fails and the
// journal is kept.
// `limit`: only the journal's first `limit` bytes count (a rollback in the
// same session passes what was confirmed on the card: an entry past it never
// let its chunk be written, whatever the card holds of it).
CtgpJournalRecovery play_back(std::FILE* f, const CtgpPackIo& pack, std::uint64_t pack_size, std::size_t& restored,
                              std::vector<Written>& put_back, std::string& error,
                              std::uint64_t limit = UINT64_MAX) {
    restored = 0;
    put_back.clear();
    if (std::fseek(f, 0, SEEK_END) != 0) {
        error = "the journal could not be read";
        return CtgpJournalRecovery::Failed;
    }
    const long end = std::ftell(f);
    if (end < 0 || std::fseek(f, 0, SEEK_SET) != 0) {
        error = "the journal could not be read";
        return CtgpJournalRecovery::Failed;
    }
    // A same-session rollback needs every confirmed byte: a journal the card
    // now holds shorter than that would undo only some of the chunks written.
    if (limit != UINT64_MAX && std::uint64_t(end) < limit) {
        error = "the journal on the card is shorter than what was written to it";
        return CtgpJournalRecovery::Failed;
    }
    const std::uint64_t size = std::min<std::uint64_t>(std::uint64_t(end), limit);
    std::uint8_t header[kHeaderBytes];
    // The header is written whole before any pack write, so a journal that
    // ends inside it (its magic and version aside) stopped before any. One
    // of another version is refused whatever its length: its header may be
    // shorter than this version's.
    if (size < 8) return CtgpJournalRecovery::RolledBack;
    if (std::fread(header, 1, 8, f) != 8) {
        error = "the journal could not be read";
        return CtgpJournalRecovery::Failed;
    }
    if (get32(header) != kMagic) {
        error = "journal header is not recognised";
        return CtgpJournalRecovery::Failed;
    }
    if (get32(header + 4) != kVersion) {
        error = "the journal was made by another version of RiftWii, which this one can't play back";
        return CtgpJournalRecovery::Failed;
    }
    if (size < sizeof header) return CtgpJournalRecovery::RolledBack;
    if (std::fread(header + 8, 1, sizeof header - 8, f) != sizeof header - 8) {
        error = "the journal could not be read";
        return CtgpJournalRecovery::Failed;
    }
    if (get32(header + kHeaderCrcAt) != crc_update(0, header, kHeaderCrcAt) ||
        get32(header + 16) != kCtgpJournalChunkBytes) {
        error = "the journal's header is damaged";
        return CtgpJournalRecovery::Failed;
    }
    if (get64(header + 8) != pack_size) {
        error = "journal belongs to a pack of another size";
        return CtgpJournalRecovery::Failed;
    }
    // Nothing goes back into a pack this journal wasn't made for: after
    // CTGP-R was installed again, a pack of the same size holds other bytes
    // (jelly.bin's are even encrypted for other file times).
    if ((get32(header + 20) & kFlagIdentity) != 0) {
        if (!pack.identity.known) {
            error = "the pack file could not be identified, so the journal is not played back into it";
            return CtgpJournalRecovery::Failed;
        }
        if (std::memcmp(header + 24, pack.identity.bytes.data(), pack.identity.bytes.size()) != 0) {
            error = "the journal was made for another copy of the pack file (was CTGP-R installed again?), so "
                    "nothing was put back";
            return CtgpJournalRecovery::Failed;
        }
    }
    // Which chunks the journal covers, from the entries' heads alone, then
    // the content samples outside them: all before the first write.
    if (get32(header + kSampleCountAt) != kSamples) {
        error = "the journal's header is damaged";
        return CtgpJournalRecovery::Failed;
    }
    ChunkBits covered;
    if (!covered.size_for(chunk_count(pack_size))) {
        error = "no memory to read the journal";
        return CtgpJournalRecovery::Failed;
    }
    for (std::uint64_t at = sizeof header; size - at >= 12;) {
        std::uint8_t head[12];
        if (std::fseek(f, long(at), SEEK_SET) != 0 || std::fread(head, 1, sizeof head, f) != sizeof head) {
            error = "the journal could not be read";
            return CtgpJournalRecovery::Failed;
        }
        const std::uint64_t offset = get64(head);
        const std::uint32_t length = get32(head + 8);
        if (length == 0 || length > kCtgpJournalChunkBytes || offset > pack_size || length > pack_size - offset) {
            error = "the journal is damaged";
            return CtgpJournalRecovery::Failed;
        }
        if (size - at < kEntryOverhead + length) break;  // the cut-off last append: its chunk was never written
        for (std::uint64_t c = offset / kCtgpJournalChunkBytes; c <= (offset + length - 1) / kCtgpJournalChunkBytes; c++)
            covered.add(c);
        at += kEntryOverhead + length;
    }
    std::uint8_t sample[kSampleBytes];
    for (std::uint32_t i = 0; i < kSamples; i++) {
        std::uint64_t offset = 0;
        std::uint32_t length = 0;
        sample_at(pack_size, i, offset, length);
        if (covered.has(offset / kCtgpJournalChunkBytes)) continue;
        if (!pack.read || !pack.read(offset, length, sample)) {
            error = "the pack could not be read to check it is the one the journal was made for";
            return CtgpJournalRecovery::Failed;
        }
        if (crc_update(0, sample, length) != get32(header + kSamplesAt + 4 * i)) {
            error = "the pack's contents are not those the journal was made for (was CTGP-R installed again?), "
                    "so nothing was put back";
            return CtgpJournalRecovery::Failed;
        }
    }
    if (std::fseek(f, long(sizeof header), SEEK_SET) != 0) {
        error = "the journal could not be read";
        return CtgpJournalRecovery::Failed;
    }
    std::uint64_t pos = sizeof header;
    Entry e;
    while (pos < size) {
        std::uint32_t length = 0;
        std::uint32_t stored_crc = 0;
        const EntryRead read = read_entry(f, pos, size, pack_size, e, length, stored_crc);
        if (read == EntryRead::ReadError) {
            error = "the journal could not be read";
            return CtgpJournalRecovery::Failed;
        }
        if (read == EntryRead::Torn) break;  // the cut-off last append
        if (read == EntryRead::BadSum) {
            // An entry whose append failed (the card gave other bytes back)
            // never let its chunk be written, but it can stay whole on the
            // card when the power goes before it is cut back, or the cut and
            // the rollback both fail. Its checksum still says what the chunk
            // held before the update: when the pack holds exactly that now,
            // there is nothing to put back, and the replay goes on. Anything
            // else (a chunk that was changed under a damaged entry) is damage.
            bool unchanged = false;
            if (!pack_chunk_is(pack, e, length, stored_crc, unchanged)) {
                error = "the journal could not be checked against the pack";
                return CtgpJournalRecovery::Failed;
            }
            if (!unchanged) {
                error = "the journal is damaged";
                return CtgpJournalRecovery::Failed;
            }
            pos += 12 + std::uint64_t(length) + 4;
            if (std::fseek(f, long(pos), SEEK_SET) != 0) {
                error = "the journal could not be read";
                return CtgpJournalRecovery::Failed;
            }
            continue;
        }
        if (read == EntryRead::Bad) {
            error = "the journal is damaged";
            return CtgpJournalRecovery::Failed;
        }
        if (!pack.write || !pack.write(e.offset, e.data.data(), std::uint32_t(e.data.size()))) {
            error = "writing a journaled chunk back failed";
            return CtgpJournalRecovery::Failed;
        }
        if (pack.read_card) {
            if (put_back.size() == put_back.capacity() && !try_reserve(put_back, put_back.size() * 2 + 64)) {
                error = "no memory to note the chunks put back";
                return CtgpJournalRecovery::Failed;
            }
            put_back.push_back({e.offset, length, crc_update(0, e.data.data(), e.data.size())});
        }
        restored++;
        pos += 12 + std::uint64_t(length) + 4;
    }
    return CtgpJournalRecovery::RolledBack;
}

CtgpJournalRecovery play_back_file(const std::string& path, const CtgpPackIo& pack, std::uint64_t pack_size,
                                   const std::function<bool()>& sync_pack, std::size_t& restored, std::string& error,
                                   std::uint64_t limit = UINT64_MAX,
                                   const std::function<bool(std::string&)>& before_remove = {}) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        error = "cannot open the journal";
        return CtgpJournalRecovery::Failed;
    }
    std::vector<Written> put_back;
    CtgpJournalRecovery result = play_back(f, pack, pack_size, restored, put_back, error, limit);
    std::fclose(f);
    if (result != CtgpJournalRecovery::RolledBack) return result;
    if (sync_pack && !sync_pack()) {
        error = "syncing the pack failed";
        return CtgpJournalRecovery::Failed;
    }
    // The sync may have "worked" with the card refusing the writes: the
    // journal goes only once the card shows every chunk back as it was.
    std::vector<std::uint8_t> scratch;
    for (const Written& w : put_back) {
        if (!card_holds(pack, w.offset, w.length, w.crc, scratch)) {
            error = "the SD card did not keep the chunks put back; the journal is kept";
            return CtgpJournalRecovery::Failed;
        }
    }
    if (before_remove && !before_remove(error)) return CtgpJournalRecovery::Failed;
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
    std::uint64_t file_bytes = 0;  // what has been appended so far
    // What was appended and confirmed on the card: every entry before it let
    // its chunk be written; one after it (an append that failed) never did.
    std::uint64_t confirmed_bytes = 0;
    // All sized by begin, so no write later needs memory it might not get:
    // one bit per chunk of the pack for each set (4 KiB for a 1 GiB pack),
    // and with a card reader one checksum per chunk (128 KiB for 1 GiB).
    ChunkBits done;  // chunks already journaled
    std::size_t done_count = 0;
    // With a card reader: each chunk the update wrote and the checksum of
    // what it should now hold, for commit to compare with the card.
    ChunkBits written;
    std::vector<std::uint32_t> written_crc;
    std::vector<std::uint8_t> entry;    // one entry being appended
    std::vector<std::uint8_t> scratch;  // one entry or chunk read back
    // Set when an entry could not be appended or synced (the file may end in
    // a torn entry), or when a pack write failed (the pack may be half
    // changed): nothing more is written, and commit refuses, until the
    // journal is played back.
    bool failed = false;
    // From begin until the journal is removed: rollback still works after a
    // commit that closed the file but could not remove it.
    bool active = false;

    // Appends `bytes`, syncs, and (with a card reader) reads them back from
    // the card: only then does the caller rely on them.
    bool append(const std::uint8_t* bytes, std::size_t length) {
        if (std::fwrite(bytes, 1, length, file) != length || !sync_file(file)) return false;
        const std::uint64_t at = file_bytes;
        file_bytes += length;
        if (pack.read_card_file) {
            if (!scratch_room(scratch, length)) return false;
            if (!pack.read_card_file(path, at, std::uint32_t(length), scratch.data()) ||
                std::memcmp(scratch.data(), bytes, length) != 0)
                return false;
        }
        confirmed_bytes = file_bytes;
        return true;
    }

    // After an append that failed: the journal is cut back to what was
    // confirmed, so a start-up replay does not meet a whole entry with a bad
    // checksum (which it must take as damage) for a chunk never written. If
    // the card refuses that too, the replay keeps failing safe: the journal
    // stays, and the rollback in this session still stops at the boundary.
    void cut_back_to_confirmed() {
#ifdef RIFTWII_JOURNAL_FSYNC
        std::fflush(file);
        if (ftruncate(fileno(file), off_t(confirmed_bytes)) == 0) {
            sync_file(file);
            file_bytes = confirmed_bytes;
        }
#endif
    }

    bool journal_chunk(std::uint64_t index) {
        std::uint64_t offset = index * kCtgpJournalChunkBytes;
        if (offset >= pack_size) return true;  // beyond the pack: the pack write will fail by itself
        std::uint32_t length = std::uint32_t(std::min<std::uint64_t>(kCtgpJournalChunkBytes, pack_size - offset));
        std::uint8_t* const buf = entry.data();
        put64(buf, offset);
        put32(buf + 8, length);
        if (!pack.read || !pack.read(offset, length, buf + 12)) return false;
        put32(buf + 12 + length, crc_update(0, buf, 12 + length));
        if (!append(buf, kEntryOverhead + length)) {
            failed = true;
            cut_back_to_confirmed();
            return false;
        }
        done.add(index);
        done_count++;
        return true;
    }

    // After a pack write: what each chunk it touched should now hold. A
    // chunk it covers whole is in `data`; another is read back through the
    // pack io, which right after the write is what was meant to be written.
    bool note_written(std::uint64_t offset, const std::uint8_t* data, std::uint32_t length) {
        const std::uint64_t first = offset / kCtgpJournalChunkBytes;
        const std::uint64_t last = (offset + length - 1) / kCtgpJournalChunkBytes;
        for (std::uint64_t i = first; i <= last; i++) {
            const std::uint64_t start = i * kCtgpJournalChunkBytes;
            if (start >= pack_size) break;
            const std::uint64_t end = std::min<std::uint64_t>(start + kCtgpJournalChunkBytes, pack_size);
            const std::uint32_t chunk_length = std::uint32_t(end - start);
            std::uint32_t crc = 0;
            if (offset <= start && offset + length >= end) {
                crc = crc_update(0, data + (start - offset), chunk_length);
            } else {
                if (!scratch_room(scratch, chunk_length) || !pack.read(start, chunk_length, scratch.data()))
                    return false;
                crc = crc_update(0, scratch.data(), chunk_length);
            }
            written.add(i);
            written_crc[std::size_t(i)] = crc;
        }
        return true;
    }

    // Whether the card holds every chunk the update wrote as it was written.
    bool card_has_writes() {
        if (!pack.read_card) return true;
        for (std::uint64_t index = 0; index < written_crc.size(); index++) {
            if (!written.has(index)) continue;
            const std::uint64_t start = index * kCtgpJournalChunkBytes;
            const std::uint32_t length =
                std::uint32_t(std::min<std::uint64_t>(kCtgpJournalChunkBytes, pack_size - start));
            if (!card_holds(pack, start, length, written_crc[std::size_t(index)], scratch)) return false;
        }
        return true;
    }

    bool write(std::uint64_t offset, const std::uint8_t* data, std::uint32_t length) {
        if (!file || !pack.write || failed) return false;
        if (length == 0) return pack.write(offset, data, length);
        std::uint64_t first = offset / kCtgpJournalChunkBytes;
        std::uint64_t last = (offset + length - 1) / kCtgpJournalChunkBytes;
        for (std::uint64_t i = first; i <= last; i++)
            if (!done.has(i) && !journal_chunk(i)) return false;
        // A pack write that fails may have changed part of its chunks, so the
        // update can't be committed: commit refuses and the caller rolls back.
        if (!pack.write(offset, data, length)) {
            failed = true;
            return false;
        }
        if (pack.read_card && !note_written(offset, data, length)) {
            failed = true;
            return false;
        }
        return true;
    }

    void close() {
        if (file) std::fclose(file);
        file = nullptr;
    }
};

CtgpPackIdentity ctgp_pack_identity(const Fat32Entry& entry, bool with_write_time) {
    CtgpPackIdentity id;
    id.known = true;
    std::uint8_t* p = id.bytes.data();
    put32(p, entry.size);
    put32(p + 4, entry.first_cluster);
    put16(p + 8, entry.create_date);
    put16(p + 10, entry.create_time);
    if (with_write_time) {
        put16(p + 12, entry.write_date);
        put16(p + 14, entry.write_time);
    }
    return id;
}

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
    switch (presence(path)) {
    case Presence::Present:
        error = "a journal from an earlier update is still there";
        return false;
    case Presence::Unknown:
        error = "the SD card could not say whether a journal is already there";
        return false;
    case Presence::Absent:
        break;
    }
    // Everything the writes will need, before the file exists: a pack too
    // large for the memory left fails here, cleanly, with nothing written.
    const std::uint64_t chunks = chunk_count(pack_size);
    const bool tracked = bool(pack.read_card);
    if (!within_checked_cap(std::size_t(chunks), sizeof(std::uint32_t)) || !impl_->done.size_for(chunks) ||
        !try_resize(impl_->entry, kEntryOverhead + kCtgpJournalChunkBytes) ||
        !try_resize(impl_->scratch, kEntryOverhead + kCtgpJournalChunkBytes) ||
        !impl_->written.size_for(tracked ? chunks : 0) ||
        !try_assign(impl_->written_crc, std::size_t(tracked ? chunks : 0), std::uint32_t(0))) {
        error = "not enough memory for the journal";
        return false;
    }
    std::uint8_t header[kHeaderBytes] = {};
    put32(header, kMagic);
    put32(header + 4, kVersion);
    put64(header + 8, pack_size);
    put32(header + 16, kCtgpJournalChunkBytes);
    if (pack.identity.known) {
        put32(header + 20, kFlagIdentity);
        std::memcpy(header + 24, pack.identity.bytes.data(), pack.identity.bytes.size());
    }
    put32(header + kSampleCountAt, kSamples);
    for (std::uint32_t i = 0; i < kSamples; i++) {
        std::uint64_t offset = 0;
        std::uint32_t length = 0;
        sample_at(pack_size, i, offset, length);
        std::uint8_t* const sample = impl_->scratch.data();
        if (pack_size == 0 || !pack.read || !pack.read(offset, length, sample)) {
            error = "the pack could not be read";
            return false;
        }
        put32(header + kSamplesAt + 4 * i, crc_update(0, sample, length));
    }
    put32(header + kHeaderCrcAt, crc_update(0, header, kHeaderCrcAt));
    // "x": created only if it is not there, so an existing journal is never
    // emptied, whatever the check above saw.
    std::FILE* f = std::fopen(path.c_str(), "wbx");
    if (!f) {
        error = "cannot create the journal";
        return false;
    }
    impl_->path = path;
    impl_->pack = pack;
    impl_->pack_size = pack_size;
    impl_->file = f;
    impl_->file_bytes = 0;
    impl_->confirmed_bytes = 0;
    impl_->done_count = 0;
    impl_->failed = false;
    if (!impl_->append(header, sizeof header)) {
        impl_->close();
        std::remove(path.c_str());
        error = "cannot write the journal header (or the SD card did not keep it)";
        return false;
    }
    impl_->active = true;
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
    // libfat's sync says yes even when the card refused a write: the journal
    // goes only once the card shows every changed chunk as written.
    if (!impl_->card_has_writes()) {
        error = "the SD card did not keep the update's writes; the journal is kept";
        return false;
    }
    impl_->close();
    if (std::remove(impl_->path.c_str()) != 0) {
        error = "cannot remove the journal";
        return false;
    }
    impl_->active = false;
    return true;
}

bool CtgpPackJournal::rollback(const std::function<bool()>& sync_pack, std::string& error) {
    error.clear();
    if (!impl_->active) {
        error = "no journal is open";
        return false;
    }
    impl_->close();
    std::size_t restored = 0;
    if (play_back_file(impl_->path, impl_->pack, impl_->pack_size, sync_pack, restored, error,
                       impl_->confirmed_bytes) != CtgpJournalRecovery::RolledBack)
        return false;
    impl_->active = false;
    return true;
}

std::size_t CtgpPackJournal::chunks() const { return impl_->done_count; }

CtgpJournalRecovery ctgp_journal_recover(const std::string& path, const CtgpPackIo& pack, std::uint64_t pack_size,
                                         const std::function<bool()>& sync_pack, std::size_t& restored,
                                         std::string& error, const std::function<bool(std::string&)>& before_remove) {
    error.clear();
    restored = 0;
    switch (presence(path)) {
    case Presence::Absent:
        return CtgpJournalRecovery::None;
    case Presence::Unknown:
        error = "the SD card could not say whether the journal is there";
        return CtgpJournalRecovery::Failed;
    case Presence::Present:
        break;
    }
    return play_back_file(path, pack, pack_size, sync_pack, restored, error, UINT64_MAX, before_remove);
}

}  // namespace riftwii
