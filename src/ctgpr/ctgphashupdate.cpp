// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgphashupdate.hpp"

#include "riftwii/ctgphash.hpp"
#include "riftwii/ctgpjournal.hpp"

#include <cstring>

namespace riftwii {
namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}

bool collect(void* user, const std::uint8_t* data, std::size_t length) {
    auto* out = static_cast<std::vector<std::uint8_t>*>(user);
    if (out->size() + length > kCtgpHashListMaxBytes) return false;
    out->insert(out->end(), data, data + length);
    return true;
}

// "/ctgpr.hsh" -> "CTGPR": one level, a name of one to eight letters,
// digits or underscores, and the ".hsh" ending (any case).
bool package_of(const std::string& path, std::string& package) {
    if (path.size() < 6 || path[0] != '/') return false;
    const std::size_t dot = path.size() - 4;
    const std::string ending = path.substr(dot);
    if (ending != ".hsh" && ending != ".HSH") return false;
    const std::string stem = path.substr(1, dot - 1);
    if (stem.empty() || stem.size() > 8) return false;
    package.clear();
    for (char c : stem) {
        if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
        package.push_back(c);
    }
    return true;
}

std::uint64_t clusters_for(std::uint64_t bytes, std::uint32_t cluster) {
    return cluster == 0 ? 0 : (bytes + cluster - 1) / cluster;
}

// The list checked as CTGP-R's: its signature, size, name and body digest,
// under its own label. The label is not tied to the HASH update's version or
// to blob.bin's CTGPR version: CTGP-R's own lists run ahead of the pack (HASH
// 0x01031374's CTGPR.HSH is labelled 1194 while the pack is still at 1192 or
// 1193), and the channel installs them as they come. The native launch's
// label rule (ctgp_load_hash_list against PACKAGES.BIN) is separate.
bool check_list(const std::vector<std::uint8_t>& bytes, const std::string& package, std::string& error,
                const CtgpDsaKey* key) {
    if (bytes.size() < 0x2C) {
        error = "the list is too short";
        return false;
    }
    const CtgpBytes copy(bytes.begin(), bytes.end());
    CtgpHashList list;
    return ctgp_load_hash_list(copy, package, be32(bytes.data() + 0x28), list, error, key);
}

}  // namespace

bool ctgp_packages_set_version(std::vector<std::uint8_t>& list, const std::string& component, std::uint32_t version,
                               std::string& error) {
    if (list.size() < 16 || std::memcmp(list.data(), "PAK0", 4) != 0 || component.empty() || component.size() > 8) {
        error = "PACKAGES.BIN is not a package list";
        return false;
    }
    const std::uint32_t count = be32(list.data() + 12);
    if (count > (list.size() - 16) / 16) {
        error = "PACKAGES.BIN is shorter than its record count";
        return false;
    }
    char name[8] = {};
    std::memcpy(name, component.data(), component.size());
    bool found = false;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint8_t* record = list.data() + 16 + i * 16;
        if (std::memcmp(record, name, 8) != 0) continue;
        record[8] = std::uint8_t(version >> 24);
        record[9] = std::uint8_t(version >> 16);
        record[10] = std::uint8_t(version >> 8);
        record[11] = std::uint8_t(version);
        // As CTGP-R's channel does when it installs an update: the record's
        // 0x40000000 flag goes (no card seen so far has it set).
        record[12] = std::uint8_t(record[12] & ~0x40u);
        found = true;
    }
    if (!found) error = "PACKAGES.BIN has no " + component + " record";
    return found;
}

bool ctgp_hash_update_build(const CtgpUpdateRead& read, std::uint64_t size, const CtgpDia& dia,
                            std::vector<CtgpHashUpdateList>& out, CtgpUpdateFailure& failure, std::string& error,
                            const CtgpDsaKey* key) {
    out.clear();
    failure = CtgpUpdateFailure::Temporary;
    if (dia.component != "HASH") {
        failure = CtgpUpdateFailure::BadUpdate;
        error = "not a HASH update";
        return false;
    }
    for (const CtgpDiaSection& place : dia.sections) {
        if (place.size > kCtgpHashListMaxBytes + 0x10000u || std::uint64_t(place.offset) + place.size > size) {
            failure = CtgpUpdateFailure::BadUpdate;
            error = "a HASH update section is too large";
            return false;
        }
        std::vector<std::uint8_t> section(place.size);
        if (!read(place.offset, section.data(), section.size())) {
            error = "the update file could not be read";
            return false;
        }
        CtgpDif dif;
        if (!ctgp_dif_parse(section.data(), section.size(), dif, error)) {
            failure = CtgpUpdateFailure::BadUpdate;
            return false;
        }
        CtgpHashUpdateList list;
        list.section_path = ctgp_dif_path(section.data(), section.size(), dif.path_table);
        if (dif.version != 0 || !dif.entries.empty() || dif.unpacked_size > kCtgpHashListMaxBytes) {
            failure = CtgpUpdateFailure::BadUpdate;
            error = "the HASH update's " + list.section_path + " is not a whole list";
            return false;
        }
        if (!package_of(list.section_path, list.package)) {
            failure = CtgpUpdateFailure::BadUpdate;
            error = "the HASH update names a file that is not a hash list (" + list.section_path + ")";
            return false;
        }
        for (const CtgpHashUpdateList& earlier : out) {
            if (earlier.package == list.package) {
                failure = CtgpUpdateFailure::BadUpdate;
                error = "the HASH update has " + list.section_path + " twice";
                return false;
            }
        }
        list.bytes.reserve(dif.unpacked_size);
        std::string apply_error;
        if (!ctgp_dif_apply(section.data(), section.size(), dif, {}, collect, &list.bytes, apply_error)) {
            failure = CtgpUpdateFailure::BadUpdate;
            error = list.section_path + ": " + apply_error;
            return false;
        }
        std::string check_error;
        if (!check_list(list.bytes, list.package, check_error, key)) {
            failure = CtgpUpdateFailure::BadUpdate;
            error = "the HASH update's " + list.section_path + " is not a list CTGP-R signed: " + check_error;
            return false;
        }
        list.label = be32(list.bytes.data() + 0x28);
        list.disk_path = "/PACKAGES/HASH/" + list.package + ".HSH";
        out.push_back(std::move(list));
    }
    if (out.empty()) {
        failure = CtgpUpdateFailure::BadUpdate;
        error = "the HASH update has no lists";
        return false;
    }
    failure = CtgpUpdateFailure::None;
    return true;
}

bool ctgp_hash_update_fits(CtgpPackWriter& jelly, const std::vector<CtgpHashUpdateList>& lists, std::string& error,
                           bool* needs_growth) {
    if (needs_growth != nullptr) *needs_growth = false;
    const std::uint32_t cluster = jelly.cluster_bytes();
    if (cluster == 0) {
        error = "jelly.bin is not open";
        return false;
    }
    std::uint64_t more = 0;
    for (const CtgpHashUpdateList& list : lists) {
        std::uint32_t old_size = 0;
        bool present = false;
        std::string read_error;
        if (!jelly.stat_file(list.disk_path, present, old_size, read_error)) {
            // The card or the disk, not the update: it may go in next time.
            error = "jelly.bin's " + list.disk_path + " could not be looked up: " + read_error;
            return false;
        }
        if (!present) {
            // CTGP-R's channel creates it; RiftWii never adds to jelly.bin.
            error = "jelly.bin has no " + list.disk_path + " to replace";
            if (needs_growth != nullptr) *needs_growth = true;
            return false;
        }
        // The whole old list must read, or the replace could fail part-way.
        if (!jelly.read_file(list.disk_path, [](const std::uint8_t*, std::size_t) { return true; }, nullptr,
                             read_error)) {
            error = "jelly.bin's " + list.disk_path + " could not be read: " + read_error;
            return false;
        }
        const std::uint64_t now = clusters_for(old_size, cluster);
        const std::uint64_t then = clusters_for(list.bytes.size(), cluster);
        if (then > now) more += then - now;
    }
    if (more > jelly.free_clusters()) {
        error = "the new hash lists need " + std::to_string(more) + " more clusters than jelly.bin has free (" +
                std::to_string(jelly.free_clusters()) + ")";
        if (needs_growth != nullptr) *needs_growth = true;
        return false;
    }
    std::vector<std::uint8_t> packages;
    if (!jelly.read_file("/PACKAGES.BIN", packages, error)) {
        error = "jelly.bin's version list could not be read: " + error;
        return false;
    }
    return ctgp_packages_set_version(packages, "HASH", 0, error);
}

bool ctgp_hash_update_write(CtgpPackWriter& jelly, const std::vector<CtgpHashUpdateList>& lists,
                            std::uint32_t version, std::string& error) {
    for (const CtgpHashUpdateList& list : lists) {
        if (!jelly.replace_file(list.disk_path, list.bytes.data(), list.bytes.size(), error)) {
            error = list.disk_path + " was not written: " + error;
            return false;
        }
    }
    std::vector<std::uint8_t> packages;
    if (!jelly.read_file("/PACKAGES.BIN", packages, error)) {
        error = "jelly.bin's version list could not be read: " + error;
        return false;
    }
    if (!ctgp_packages_set_version(packages, "HASH", version, error)) return false;
    if (!jelly.replace_file("/PACKAGES.BIN", packages.data(), packages.size(), error)) {
        error = "jelly.bin's version list was not updated: " + error;
        return false;
    }
    return true;
}

bool ctgp_hash_update_verify(CtgpPackWriter& jelly, const std::vector<CtgpHashUpdateList>& lists,
                             std::uint32_t version, std::string& error, const CtgpDsaKey* key) {
    std::vector<std::uint8_t> bytes;
    for (const CtgpHashUpdateList& list : lists) {
        if (!jelly.read_file(list.disk_path, bytes, error)) {
            error = list.disk_path + " could not be read back: " + error;
            return false;
        }
        if (bytes != list.bytes) {
            error = list.disk_path + " reads back different from what was written";
            return false;
        }
        std::string check_error;
        if (!check_list(bytes, list.package, check_error, key)) {
            error = list.disk_path + " does not check out after writing: " + check_error;
            return false;
        }
    }
    if (!jelly.read_file("/PACKAGES.BIN", bytes, error)) {
        error = "jelly.bin's version list could not be read back: " + error;
        return false;
    }
    const std::uint32_t count = bytes.size() >= 16 ? be32(bytes.data() + 12) : 0;
    for (std::uint32_t i = 0; i < count && 16 + i * 16 + 16 <= bytes.size(); ++i) {
        const std::uint8_t* record = bytes.data() + 16 + i * 16;
        if (std::memcmp(record, "HASH\0\0\0\0", 8) == 0) {
            if (be32(record + 8) == version) return true;
            error = "jelly.bin's HASH record reads back wrong";
            return false;
        }
    }
    error = "jelly.bin's version list has no HASH record after writing";
    return false;
}

bool ctgp_hash_update_install(const CtgpPackIo& raw, std::uint64_t size, const std::array<std::uint8_t, 16>& key,
                              const CtgpChunkIv& iv, const std::vector<CtgpHashUpdateList>& lists,
                              std::uint32_t version, const std::string& journal_path,
                              const std::function<bool()>& sync, bool& rolled_back, std::string& error,
                              const CtgpDsaKey* dsa_key, bool* needs_growth) {
    rolled_back = true;  // nothing written yet
    if (needs_growth != nullptr) *needs_growth = false;
    CtgpPackIo read_only;
    read_only.read = raw.read;
    read_only.write = [](std::uint64_t, const std::uint8_t*, std::uint32_t) { return false; };
    {
        CtgpPackWriter look;
        const bool fits =
            look.open(read_only, size, key, iv, error) && ctgp_hash_update_fits(look, lists, error, needs_growth);
        look.close();
        if (!fits) {
            error = "jelly.bin: " + error;
            return false;
        }
    }
    // No write to jelly.bin happens outside the journal: a locked card stops
    // at the journal's creation, and a raw write the card refuses is the
    // first journaled write (its chunk already saved), so it rolls back.
    CtgpPackJournal journal;
    if (!journal.begin(journal_path, raw, size, error)) {
        error = "the undo journal for jelly.bin could not be started: " + error;
        return false;
    }
    rolled_back = false;  // from here a failure is undone through the journal
    CtgpPackWriter jelly;
    bool ok = jelly.open(journal.io(), size, key, iv, error) && ctgp_hash_update_write(jelly, lists, version, error);
    // The writer keeps the last chunk it changed: out before anything is read back.
    jelly.close();
    if (ok && !sync()) {
        ok = false;
        error = "jelly.bin could not be synced";
    }
    if (ok) {
        CtgpPackWriter check;
        ok = check.open(read_only, size, key, iv, error) && ctgp_hash_update_verify(check, lists, version, error, dsa_key);
        check.close();
        if (!ok) error = "jelly.bin did not read back right: " + error;
    }
    std::string journal_error;
    if (ok && !journal.commit(sync, journal_error)) {
        ok = false;
        error = "the jelly.bin update could not be finished: " + journal_error;
    }
    if (ok) return true;
    rolled_back = journal.rollback(sync, journal_error);
    if (!rolled_back) error += "; jelly.bin could not be put back now (" + journal_error + "), the next start tries again";
    return false;
}

}  // namespace riftwii
