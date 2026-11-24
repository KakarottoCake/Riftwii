// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "riftwii/ctgppackwrite.hpp"
#include "riftwii/fat32.hpp"

// An undo journal for changing sd:/ctgpr/blob.bin in place. If the Wii loses
// power or crashes halfway through an update, the pack would be part old and
// part new. Before the first write to any 32 KiB chunk, the chunk's current
// bytes are appended to a journal file and synced to the card; only then is
// the pack written. At the next start-up, a journal that is still there is
// played back, which puts every touched chunk back as it was. A finished
// update removes the journal after the pack is synced.
//
// A sync alone proves nothing on the Wii (riftwii/ctgppackwrite.hpp,
// CtgpPackIo::read_card). When the pack io has the card readers, every
// journal entry is read back from the card and compared before its chunk is
// changed, and every chunk the update (or a replay) wrote is read back from
// the card and compared before the journal is removed. A mismatch fails that
// step and keeps the journal.
//
// The journal also records the pack's identity (CtgpPackIo::identity) and
// the first 512 bytes of 128 chunks spread over the pack, and is not played
// back into a pack with another identity, or where one of those samples
// outside the journal's own chunks differs: after CTGP-R is installed again,
// the old journal's chunks don't belong in the new pack. The samples are a
// spot check, not proof: a pack rewritten in place with the same directory
// entry that differs only between them still passes. begin sizes all its bookkeeping up front (a few
// bits and a checksum per chunk of the pack), so a write never needs memory
// it might not get; begin fails cleanly when there isn't room.
namespace riftwii {

constexpr std::uint32_t kCtgpJournalChunkBytes = 32 * 1024;

// A pack's identity from its directory entry: its size, first cluster and
// creation time. `with_write_time` adds the last-write time, for a file the
// updater writes in place without touching its entry (jelly.bin, whose key is
// made from those times); libfat changes blob.bin's write time when it
// writes, so blob.bin's leaves it out.
CtgpPackIdentity ctgp_pack_identity(const Fat32Entry& entry, bool with_write_time);

class CtgpPackJournal {
public:
    CtgpPackJournal();
    ~CtgpPackJournal();
    CtgpPackJournal(const CtgpPackJournal&) = delete;
    CtgpPackJournal& operator=(const CtgpPackJournal&) = delete;

    // Creates the journal file. Fails if one already exists at `path` (a
    // previous run's journal must be recovered first). `pack` is kept by copy
    // and must stay valid until commit, rollback or destruction; its
    // identity goes into the journal's header.
    bool begin(const std::string& path, const CtgpPackIo& pack, std::uint64_t pack_size, std::string& error);

    // The pack io to give CtgpPackWriter::open. A write that cannot be
    // journaled returns false without touching the pack, and so does every
    // write after it.
    CtgpPackIo io();

    // Update finished: sync_pack() flushes blob.bin to the card, every chunk
    // the update wrote is read back from the card and compared (when the pack
    // io has read_card), then the journal is removed. Any failure keeps it.
    bool commit(const std::function<bool()>& sync_pack, std::string& error);

    // Update failed: write every journaled chunk back, sync, check them on
    // the card as commit does, remove the journal.
    bool rollback(const std::function<bool()>& sync_pack, std::string& error);

    std::size_t chunks() const;

private:
    struct Impl;
    Impl* impl_;
};

enum class CtgpJournalRecovery { None, RolledBack, Failed };

// Start-up check: if a journal exists at `path`, writes its complete entries
// back into the pack, syncs, checks them on the card (when `pack` can read
// the card), then removes it. A torn last entry (the append a power cut
// stopped, whose chunk was never written) is ignored. A damaged entry with
// more after it, a read error, a journal from another version, or one made
// for a pack of another size or identity fails and leaves the journal for
// another try (Failed). A pack of another size or identity is never written.
CtgpJournalRecovery ctgp_journal_recover(const std::string& path, const CtgpPackIo& pack, std::uint64_t pack_size,
                                         const std::function<bool()>& sync_pack, std::size_t& restored,
                                         std::string& error);

}  // namespace riftwii
