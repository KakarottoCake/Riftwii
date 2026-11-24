// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <string>
#include <vector>

#include "riftwii/ctgppackwrite.hpp"
#include "riftwii/ctgpupdate.hpp"

// Applying a downloaded CTGP-R update to the pack RiftWii already has open
// (docs/PLUGINS.md, "The ctgpupd plugin"). The sections rebuild each file from the
// pack's own copy ("CTGPR") and from the game disc ("RMC"); the pack side is
// here, the disc side is one callback so the menu can supply the selected
// game image and the console's own disc key.
namespace riftwii {

// Reads a file from the game disc as it is stored there (Nintendo Yaz0 or
// plain), handing it to `sink` in pieces; the target unpacks it as it comes.
// `size` (may be null) gets the file's length before the first piece.
// `path` is the game path the update names ("/Race/Course/..."); the menu
// opens the selected image, decrypts its game partition and reads the file.
using CtgpDiscSink = std::function<bool(const std::uint8_t* data, std::size_t length)>;
using CtgpDiscBase = std::function<bool(const std::string& path, const CtgpDiscSink& sink, std::uint32_t* size,
                                        std::string& error)>;

class CtgpPackUpdateTarget : public CtgpUpdateTarget {
public:
    // `disc` may be empty when the game image is not available; a section
    // that needs a disc file then fails with a clear error instead of
    // writing a wrong file. Each base is unpacked to a scratch file named
    // `temp_prefix` + a number + ".tmp" as it is read, so no base file is
    // ever in memory whole, packed or unpacked.
    // `hash_list` is jelly.bin's /PACKAGES/HASH/CTGPR.HSH: every rebuilt
    // file's stored bytes must match its digest there, or nothing is written.
    // The list's CTGP-R signature is checked once, at the first file, before
    // any of its checksums are trusted (ctgp_hash_list_signature_ok, the same
    // rule native launch and the HASH installer use); a list that fails it
    // refuses every file, so nothing is written.
    // A file stored unpacked that the list doesn't name (CTGP-R lists only
    // the courses) is taken without a content check when it was rebuilt only
    // from the update's signed commands and the pack's own files; one rebuilt
    // from the game disc is refused.
    CtgpPackUpdateTarget(CtgpPackWriter& pack, CtgpDiscBase disc, std::vector<std::uint8_t> hash_list,
                         std::string temp_prefix = "sd:/ctgpr/update-base-");

    bool load_base(const std::string& component, const std::string& path, bool packed,
                   std::unique_ptr<CtgpBaseSource>& out, const CtgpProgress& progress,
                   CtgpUpdateFailure& failure, std::string& error) override;
    bool check_file(const std::string& path, bool packed, const std::array<std::uint8_t, 20>& digest,
                    std::string& error) override;
    bool store_file(const std::string& path, const CtgpPieces& packed, const CtgpProgress& progress,
                    std::string& error) override;
    // The version the update brings. The list in jelly.bin must be labelled
    // that version or newer (its label, at 0x28, is bound by the checks of
    // ctgp_load_hash_list for native launch: the same list must not be an
    // older signed one). 0: not checked (tests).
    void set_required_list_version(std::uint32_t version) { required_version_ = version; }
    // True once the list could not vouch for the update in a way a newer list
    // may fix: it is not signed by CTGP-R, it is older than the update, or it
    // is newer than the update and no longer matches (or names) a file the
    // update rebuilds (the update is out of date). A digest that differs
    // from a list of the update's own version is NOT this: that update or
    // its base files are damaged, and another list would not help
    // (CtgpUpdRequest::hash_list_trouble).
    bool list_did_not_vouch() const { return list_refused_; }
    // True once a file's rebuilt digest differed from a list of the update's
    // own version: the update (or the base files it was rebuilt from) is
    // bad, so the install treats it as a bad update, and it is set aside
    // rather than tried again at every start.
    bool update_mismatched() const { return mismatched_; }
    // For tests only: lists made here carry no CTGP-R signature.
    void skip_list_signature_for_tests() { signature_skipped_ = true; }
    // Reads only a base file's first bytes (pack or disc), for the bar.
    bool base_unpacked_size(const std::string& component, const std::string& path, bool packed,
                            std::uint32_t& size) override;
    // The packed file waits in a scratch file beside the bases' ones.
    std::string packed_scratch_path() const override { return temp_prefix_ + "packed.tmp"; }

private:
    CtgpPackWriter& pack_;
    CtgpDiscBase disc_;
    std::vector<std::uint8_t> hash_list_;
    std::string temp_prefix_;
    unsigned temp_number_ = 0;
    bool list_refused_ = false;
    bool mismatched_ = false;
    bool signature_skipped_ = false;       // tests only
    std::uint32_t required_version_ = 0;
    std::uint32_t list_label_ = 0;     // the list's version label, once read
    bool list_signature_checked_ = false;  // the list passed (or a test skipped) the signature check
    std::string list_signature_error_;     // why it did not; every file is then refused
    bool disc_base_ = false;  // the file being rebuilt has a base from the disc
};

}  // namespace riftwii
