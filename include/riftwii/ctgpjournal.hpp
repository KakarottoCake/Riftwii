// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "riftwii/ctgppackwrite.hpp"

// An undo journal for changing sd:/ctgpr/blob.bin in place. If the Wii loses
// power or crashes halfway through an update, the pack would be part old and
// part new. Before the first write to any 32 KiB chunk, the chunk's current
// bytes are appended to a journal file and synced to the card; only then is
// the pack written. At the next start-up, a journal that is still there is
// played back, which puts every touched chunk back as it was. A finished
// update removes the journal after the pack is synced.
namespace riftwii {

constexpr std::uint32_t kCtgpJournalChunkBytes = 32 * 1024;

class CtgpPackJournal {
public:
    CtgpPackJournal();
    ~CtgpPackJournal();
    CtgpPackJournal(const CtgpPackJournal&) = delete;
    CtgpPackJournal& operator=(const CtgpPackJournal&) = delete;

    // Creates the journal file. Fails if one already exists at `path` (a
    // previous run's journal must be recovered first). `pack` is kept by copy
    // and must stay valid until commit, rollback or destruction.
    bool begin(const std::string& path, const CtgpPackIo& pack, std::uint64_t pack_size, std::string& error);

    // The pack io to give CtgpPackWriter::open. A write that cannot be
    // journaled returns false without touching the pack, and so does every
    // write after it.
    CtgpPackIo io();

    // Update finished: sync_pack() flushes blob.bin to the card, then the
    // journal is removed.
    bool commit(const std::function<bool()>& sync_pack, std::string& error);

    // Update failed: write every journaled chunk back, sync, remove the journal.
    bool rollback(const std::function<bool()>& sync_pack, std::string& error);

    std::size_t chunks() const;

private:
    struct Impl;
    Impl* impl_;
};

enum class CtgpJournalRecovery { None, RolledBack, Failed };

// Start-up check: if a journal exists at `path`, writes its complete entries
// back into the pack, syncs, then removes it. A torn last entry (the append
// a power cut stopped, whose chunk was never written) is ignored. A damaged
// entry with more after it, a read error, or a journal for a pack of another
// size fails and leaves the journal for another try (Failed).
CtgpJournalRecovery ctgp_journal_recover(const std::string& path, const CtgpPackIo& pack, std::uint64_t pack_size,
                                         const std::function<bool()>& sync_pack, std::size_t& restored,
                                         std::string& error);

}  // namespace riftwii
