// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgpupdater.hpp"

#include "riftwii/ctgphashlookup.hpp"
#include "riftwii/sha1.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>

namespace riftwii {

namespace {

// The update names a file inside the pack's own package ("/Race/Course/...").
// The pack holds it under /PACKAGES/<package>/.
std::string package_path(const std::string& path) {
    return "/PACKAGES/CTGPR" + path;
}

// A base file kept in a scratch file on the card. The rebuild reads it in
// many small ranges, so the last 32 KiB block read is kept.
class ScratchBase final : public CtgpBaseSource {
public:
    static constexpr std::size_t kBlock = 32 * 1024;

    ScratchBase(std::FILE* file, std::uint32_t size, std::string name)
        : file_(file), size_(size), name_(std::move(name)) {}
    ~ScratchBase() override {
        if (file_ != nullptr) {
            std::fclose(file_);
        }
        if (!name_.empty()) {
            std::remove(name_.c_str());
        }
    }

    std::uint32_t size() const override { return size_; }
    bool read(std::uint32_t offset, std::uint8_t* out, std::size_t length) const override {
        if (std::uint64_t(offset) + length > size_) {
            return false;
        }
        if (block_.empty()) {
            block_.resize(kBlock);
        }
        while (length > 0) {
            const std::uint32_t index = offset / kBlock;
            if (index != cached_) {
                const std::uint64_t start = std::uint64_t(index) * kBlock;
                const std::size_t bytes = std::size_t(std::min<std::uint64_t>(kBlock, size_ - start));
                if (fseeko(file_, static_cast<off_t>(start), SEEK_SET) != 0 ||
                    std::fread(block_.data(), 1, bytes, file_) != bytes) {
                    cached_ = kNone;
                    return false;
                }
                cached_ = index;
            }
            const std::size_t within = offset % kBlock;
            const std::size_t take = std::min<std::size_t>(length, kBlock - within);
            std::memcpy(out, block_.data() + within, take);
            out += take;
            offset += std::uint32_t(take);
            length -= take;
        }
        return true;
    }

private:
    static constexpr std::uint32_t kNone = 0xffffffffu;
    std::FILE* file_;
    std::uint32_t size_;
    std::string name_;
    mutable std::vector<std::uint8_t> block_;
    mutable std::uint32_t cached_ = kNone;
};

}  // namespace

CtgpPackUpdateTarget::CtgpPackUpdateTarget(CtgpPackWriter& pack, CtgpDiscBase disc,
                                           std::vector<std::uint8_t> hash_list, std::string temp_prefix)
    : pack_(pack), disc_(std::move(disc)), hash_list_(std::move(hash_list)), temp_prefix_(std::move(temp_prefix)) {}

bool CtgpPackUpdateTarget::load_base(const std::string& component, const std::string& path, bool packed,
                                     std::unique_ptr<CtgpBaseSource>& out, const CtgpProgress& progress,
                                     CtgpUpdateFailure& failure, std::string& error) {
    const bool from_pack = component == "CTGPR";
    if (!from_pack) disc_base_ = true;
    if (!from_pack && component != "RMC") {
        // Signed by CTGP-R, but asking for files RiftWii has no way to give:
        // trying again can never work, but CTGP-R's channel can install it.
        error = "the update needs files from a package RiftWii can't read (" + component + ")";
        failure = CtgpUpdateFailure::Unsupported;
        return false;
    }
    if (!from_pack && !disc_) {
        error = "the game disc is needed to update " + path;
        return false;
    }
    const std::string scratch = temp_prefix_ + std::to_string(temp_number_++) + ".tmp";
    std::FILE* file = std::fopen(scratch.c_str(), "w+b");
    if (file == nullptr) {
        error = "no room for a scratch file on the card";
        return false;
    }
    const auto fail = [&](const std::string& why) {
        std::fclose(file);
        std::remove(scratch.c_str());
        error = why;
        return false;
    };

    // The unpacked file goes straight to the scratch file as it is made.
    std::uint64_t written = 0;
    bool write_failed = false;
    const auto put = [&](const std::uint8_t* data, std::size_t length) {
        if (length != 0 && std::fwrite(data, 1, length, file) != length) {
            write_failed = true;
            return false;
        }
        written += length;
        return true;
    };
    CtgpYaz0StreamDecoder decoder(put);
    // A pack file the entry says is stored unpacked is used as it is. A disc
    // file that is not compressed at all is too; its first four bytes say
    // which.
    std::vector<std::uint8_t> head;
    bool decided = false;
    bool plain = false;
    std::uint32_t stored = 0;
    std::uint64_t fed = 0;
    std::string why;
    const auto use = [&](const std::uint8_t* data, std::size_t length) {
        return plain ? put(data, length) : decoder.feed(data, length, why);
    };
    bool mismatch = false;
    const auto decide = [&]() {
        decided = true;
        const bool yaz0 = head.size() >= 4 && std::memcmp(head.data(), "Yaz0", 4) == 0;
        if (from_pack && !packed && yaz0) {
            // The update expects the pack's file unpacked and it is packed:
            // the pack is not what the update was made for. Rebuilding from
            // the packed bytes would make a wrong file.
            mismatch = true;
            return false;
        }
        plain = from_pack ? !packed : !yaz0;
        const bool ok = use(head.data(), head.size());
        head.clear();
        head.shrink_to_fit();
        return ok;
    };
    const auto take = [&](const std::uint8_t* data, std::size_t length) {
        fed += length;
        bool ok = true;
        if (!decided) {
            head.insert(head.end(), data, data + length);
            if (head.size() < 4 && fed < stored) {
                return true;
            }
            ok = decide();
        } else {
            ok = use(data, length);
        }
        if (ok && progress) {
            progress(fed, stored);
        }
        return ok;
    };
    std::string read_error;
    const bool read = from_pack ? pack_.read_file(package_path(path), take, &stored, read_error)
                                : disc_(path, take, &stored, read_error);
    bool ok = read && (decided || decide());
    if (ok && !plain) {
        ok = decoder.finish(why);
    }
    if (!ok) {
        std::string reason = mismatch       ? "the pack stores it packed, and the update expects it unpacked"
                             : !why.empty() ? why
                             : write_failed ? "a scratch file could not be written"
                                            : read_error;
        if (reason.empty()) {
            reason = "it could not be read";
        }
        return fail(path + " (" + std::to_string(stored) + " bytes): " + reason);
    }
    if (std::fflush(file) != 0) {
        return fail("a scratch file could not be written");
    }
    if (written > 0xffffffffull) {
        return fail(path + " is too large");
    }
    out = std::make_unique<ScratchBase>(file, std::uint32_t(written), scratch);
    return true;
}

bool CtgpPackUpdateTarget::check_file(const std::string& path, bool packed,
                                      const std::array<std::uint8_t, 20>& digest, std::string& error) {
    std::array<std::uint8_t, 20> expected{};
    std::string why;
    bool unlisted = false;
    // Each file's bases are loaded before it is checked; the flag covers
    // this file only (reset below on every path).
    struct Reset {
        bool& flag;
        ~Reset() { flag = false; }
    } reset{disc_base_};
    if (!ctgp_hash_list_digest(hash_list_.data(), hash_list_.size(), "CTGPR", path, expected, why, &unlisted)) {
        // CTGP-R's list names only the files it checks itself (the packed
        // courses); its small unpacked files (cup lists, names, tables) have
        // no entry. Such a file is taken without a content check, when it was
        // rebuilt only from the update's signed commands and the pack's own
        // files (the pack is the version the update names, checked before
        // the install starts): a base from the game disc could be another
        // revision of the disc. A packed file always needs its entry.
        if (unlisted && !packed && !disc_base_) {
            return true;
        }
        if (unlisted && !packed) {
            error = path + " cannot be checked: CTGP-R's hash list does not name it, and it is rebuilt from the game "
                           "disc";
            return false;
        }
        error = path + " cannot be checked: " + why;
        return false;
    }
    if (digest != expected) {
        error = path + " was rebuilt, but does not match CTGP-R's hash list; nothing was written for it";
        return false;
    }
    return true;
}

bool CtgpPackUpdateTarget::store_file(const std::string& path, const CtgpPieces& packed,
                                      const CtgpProgress& progress, std::string& error) {
    return pack_.replace_file(
        package_path(path), packed.size(),
        [&](std::size_t offset, std::uint8_t* out, std::size_t length) {
            if (progress) {
                progress(offset, packed.size());
            }
            return packed.read(offset, out, length);
        },
        error);
}

}  // namespace riftwii
