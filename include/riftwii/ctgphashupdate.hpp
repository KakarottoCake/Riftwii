// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "riftwii/ctgpdsa.hpp"
#include "riftwii/ctgppackwrite.hpp"
#include "riftwii/ctgpupdate.hpp"

// CTGP-R's HASH updates: new signed hash lists for jelly.bin ("efb:"), the
// package disk that holds them. A HASH update is a DIA0 like a CTGPR one,
// but each section rebuilds a whole list from its own commands (DIF0
// version 0, no base files): "/core.hsh", "/ctgpr.hsh". Each list goes to
// jelly's /PACKAGES/HASH/<NAME>.HSH, and jelly's /PACKAGES.BIN then names
// the HASH package's new version, as CTGP-R's channel leaves them.
//
// Every list is rebuilt and checked in memory before anything is written:
// the DIA0's own signature (checked by the caller), then each list's own
// DSA signature against CTGP-R's public key, its size, its package name and
// its body's SHA-1 (ctgp_load_hash_list). RiftWii only verifies; it never
// signs anything. `key` is for tests only: CTGP-R's public key when null.
namespace riftwii {

struct CtgpHashUpdateList {
    std::string section_path;  // as the update names it: "/ctgpr.hsh"
    std::string package;       // "CTGPR", from the section's name and the list's own
    std::string disk_path;     // "/PACKAGES/HASH/CTGPR.HSH" on jelly.bin
    std::uint32_t label = 0;   // the list's own version label (0x28)
    std::vector<std::uint8_t> bytes;
};

// The lists are small (tens of KB); one larger than this is refused.
constexpr std::uint32_t kCtgpHashListMaxBytes = 1024u * 1024u;

// Rebuilds and checks every list of the HASH update `dia` (already parsed,
// its signature already checked), reading the file through `read`. Fails,
// with `failure` set to BadUpdate, when a section is not a whole-file
// section, is not named "/<name>.hsh", or its list does not check out; and
// with Temporary when the file could not be read.
bool ctgp_hash_update_build(const CtgpUpdateRead& read, std::uint64_t size, const CtgpDia& dia,
                            std::vector<CtgpHashUpdateList>& out, CtgpUpdateFailure& failure, std::string& error,
                            const CtgpDsaKey* key = nullptr);

// Whether every list can be written in place: jelly.bin holds a file at
// each list's path already, and the disk's free space covers what the lists
// grow by. Reads only; run before the journal starts, so a list that can't
// go in stops the step with nothing written.
// `needs_growth` (may be null) is set when it can't go into jelly.bin as it
// lies (a list it doesn't hold yet, or not enough free room): a genuine
// update that CTGP-R's own channel installs by making jelly.bin larger, which
// RiftWii never does. Not set when the card or the file failed.
bool ctgp_hash_update_fits(CtgpPackWriter& jelly, const std::vector<CtgpHashUpdateList>& lists, std::string& error,
                           bool* needs_growth = nullptr);

// Writes the lists into jelly.bin, each over its existing file, then sets
// the HASH record of jelly's /PACKAGES.BIN to `version`. `jelly` must be
// open on the undo journal's io: a failure partway leaves the journal to
// undo it.
bool ctgp_hash_update_write(CtgpPackWriter& jelly, const std::vector<CtgpHashUpdateList>& lists,
                            std::uint32_t version, std::string& error);

// Reads back what ctgp_hash_update_write left: each list byte for byte and
// checked again as a signed list, and the HASH record. Run on a writer
// opened afresh over the card, so it reads what is really there.
bool ctgp_hash_update_verify(CtgpPackWriter& jelly, const std::vector<CtgpHashUpdateList>& lists,
                             std::uint32_t version, std::string& error, const CtgpDsaKey* key = nullptr);

// Sets the version of record `component` in a PACKAGES.BIN ("PAK0", the
// count at 12, 16-byte records from 16: name[8], version, flags), and clears
// the record's 0x40000000 flag, as CTGP-R's channel does. False when the
// list is malformed or has no such record.
bool ctgp_packages_set_version(std::vector<std::uint8_t>& list, const std::string& component, std::uint32_t version,
                               std::string& error);

// The whole install of a HASH update into jelly.bin, as the plugin runs it:
//  1. reads jelly.bin (`raw`, its bytes as they lie on the card, `size` a
//     multiple of 32 KiB) with its key and checks the lists fit
//     (ctgp_hash_update_fits); nothing is written if they don't;
//  2. writes jelly's first 512 bytes back unchanged, so a card that refuses
//     writes (locked) stops the step before anything is journalled;
//  3. starts the undo journal at `journal_path` and writes through it: each
//     32 KiB chunk is copied to the journal and synced before the first raw
//     write to it;
//  4. reads everything back through `raw` with a fresh reader and checks it
//     (ctgp_hash_update_verify), and only then commits (removes the journal);
//     any failure rolls jelly.bin back from the journal.
// `raw.write` must write the bytes where jelly.bin already lies and nothing
// else (riftwii/fat32.hpp's fat32_write_in_place): jelly.bin's directory
// entry, and with it the key, never changes. `sync` makes raw writes
// durable. `rolled_back` tells a failure that left jelly.bin as it was from
// one that could not be undone now (the journal stays for the next start).
// `needs_growth` (may be null) is set when nothing was written because the
// update only goes in with jelly.bin made larger (ctgp_hash_update_fits).
bool ctgp_hash_update_install(const CtgpPackIo& raw, std::uint64_t size, const std::array<std::uint8_t, 16>& key,
                              const CtgpChunkIv& iv, const std::vector<CtgpHashUpdateList>& lists,
                              std::uint32_t version, const std::string& journal_path,
                              const std::function<bool()>& sync, bool& rolled_back, std::string& error,
                              const CtgpDsaKey* dsa_key = nullptr, bool* needs_growth = nullptr);

}  // namespace riftwii
