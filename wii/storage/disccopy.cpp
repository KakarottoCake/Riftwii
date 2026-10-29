// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "disccopy.hpp"

#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include "di.hpp"
#include "ios_reload.hpp"
#include "launchrun.hpp"
#include "log.hpp"
#include "menuios.hpp"
#include "otpkey.hpp"
#include "usbcatalog.hpp"

namespace riftwii::wii {
namespace {

const char* partition_kind(std::uint32_t type) {
    switch (type) {
    case 0: return "game";
    case 1: return "update";
    case 2: return "channel installer";
    default: return "other";
    }
}

// The image's bytes, in pieces of at most `split` bytes each: x.wbfs,
// x.wbf1, x.wbf2 ... (riftwii/discdump.hpp).
class SplitFileSink final : public WbfsSink {
public:
    SplitFileSink(std::string primary, std::uint64_t split) : primary_(std::move(primary)), split_(split) {}
    ~SplitFileSink() override { close_piece(); }

    bool write(const std::uint8_t* data, std::size_t length, std::string& error) override {
        while (length > 0) {
            if (file_ == nullptr || in_piece_ == split_) {
                if (!close_piece()) {
                    error = "writing " + pieces_.back() + " failed (is the drive full?)";
                    return false;
                }
                const std::string path = wbfs_piece_path(primary_, static_cast<unsigned>(pieces_.size()));
                file_ = std::fopen(path.c_str(), "wb");
                if (file_ == nullptr) {
                    error = "cannot create " + path + ": " + std::strerror(errno);
                    return false;
                }
                pieces_.push_back(path);
                in_piece_ = 0;
            }
            const std::size_t take = static_cast<std::size_t>(std::min<std::uint64_t>(length, split_ - in_piece_));
            if (std::fwrite(data, 1, take, file_) != take) {
                error = "writing " + pieces_.back() + " failed (is the drive full?)";
                return false;
            }
            in_piece_ += take;
            data += take;
            length -= take;
        }
        return true;
    }

    // False when the last piece could not be written out in full.
    bool close_piece() {
        if (file_ == nullptr) return true;
        const bool ok = std::fclose(file_) == 0;
        file_ = nullptr;
        return ok;
    }

    // Removes every piece written, after a copy that did not finish.
    void remove_all() {
        close_piece();
        for (const std::string& path : pieces_) std::remove(path.c_str());
    }

    std::size_t piece_count() const { return pieces_.size(); }

private:
    std::string primary_;
    std::uint64_t split_;
    std::FILE* file_ = nullptr;
    std::uint64_t in_piece_ = 0;
    std::vector<std::string> pieces_;
};

bool make_folder(const std::string& path) {
    return mkdir(path.c_str(), 0777) == 0 || errno == EEXIST;
}

}  // namespace

std::vector<DiscCopyPlace> DiscCopyPlaces() {
    std::vector<DiscCopyPlace> places;
    for (const char* root : {"usb:/", "sd:/"}) {
        if (root[0] == 'u' && !usb_fat32_mounted()) continue;
        struct statvfs st;
        if (statvfs(root, &st) != 0) continue;
        DiscCopyPlace place;
        place.root = root;
        place.usb = root[0] == 'u';
        place.free_bytes = std::uint64_t(st.f_bavail) * st.f_bsize;
        places.push_back(place);
    }
    return places;
}

bool DiscCopyUsbNotFat32() { return usb_volume_ready() && !usb_fat32_mounted(); }

bool DiscCopyCanRead() {
    if (running_in_dolphin()) return false;
    const int slot = MenuCiosSlot();
    // d2x_base reads the cIOS's own information block: 0 for a cIOS that
    // is not d2x, whose disc reads stop at the start as the Wii's IOS's do.
    return slot != 0 && d2x_base(slot) != 0;
}

int DiscCopySlot() {
    const int slots[] = {251, 249, 250, 248, 252};
    for (int slot : slots) {
        if (slot_has_ticket(slot) && d2x_base(slot) == 58) return slot;
    }
    for (int slot : slots) {
        if (slot_has_ticket(slot) && d2x_base(slot) != 0) return slot;
    }
    return 0;
}

bool PrepareDiscCopy(DiscCopyJob& job, std::string& error) {
    DiscCopyJob prepared;
    if (!ProbeInserted(prepared.id, prepared.title, error)) return false;
    logf("Disc copy: %s \"%s\" in the drive; finding what every partition uses\n", prepared.id.c_str(),
         prepared.title.c_str());
    std::uint8_t key[16];
    const bool have_key = ConsoleCommonKey(key);
    if (!have_key) logf("Disc copy: this Wii's disc key could not be read; every partition is kept whole\n");
    const di::RawDiscSource disc;
    const bool mapped = map_disc_usage(disc, have_key ? key : nullptr, prepared.usage, error);
    std::memset(key, 0, sizeof(key));
    if (!mapped) {
        logf("Disc copy: the disc cannot be copied: %s\n", error.c_str());
        return false;
    }
    for (std::size_t i = 0; i < prepared.usage.partitions.size(); ++i) {
        const DiscUsagePartition& p = prepared.usage.partitions[i];
        if (p.whole) {
            ++prepared.whole_partitions;
            logf("Disc copy: partition %u (%s, group %u) at 0x%09llx is kept whole: %s\n", static_cast<unsigned>(i),
                 partition_kind(p.type), static_cast<unsigned>(p.group), static_cast<unsigned long long>(p.offset),
                 p.why.c_str());
        } else {
            logf("Disc copy: partition %u (%s, group %u) at 0x%09llx: its files are kept\n", static_cast<unsigned>(i),
                 partition_kind(p.type), static_cast<unsigned>(p.group), static_cast<unsigned long long>(p.offset));
        }
    }
    if (!plan_wbfs_image(prepared.usage, prepared.plan, error)) return false;
    logf("Disc copy: %u of the disc's 32 KiB sectors are used; the image is %llu MiB\n",
         static_cast<unsigned>(prepared.usage.used_sectors()),
         static_cast<unsigned long long>(prepared.plan.file_bytes >> 20));
    job = std::move(prepared);
    error.clear();
    return true;
}

std::string DiscCopyPath(const DiscCopyJob& job, const DiscCopyPlace& place) {
    return place.root + "wbfs/" + wbfs_game_folder_name(job.title, job.id) + "/" + job.id + ".wbfs";
}

bool RunDiscCopy(const DiscCopyJob& job, const DiscCopyPlace& place, const DiscCopyProgress& progress, bool& exists,
                 std::string& error) {
    exists = false;
    const std::string path = DiscCopyPath(job, place);
    const std::string folder = path.substr(0, path.rfind('/'));
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        exists = true;
        error = path + " is already there";
        return false;
    }
    if (!make_folder(place.root + "wbfs") || !make_folder(folder)) {
        error = "cannot make the folder " + folder + ": " + std::strerror(errno);
        logf("Disc copy: %s\n", error.c_str());
        return false;
    }
    logf("Disc copy: writing %s (%llu MiB in %u piece(s))\n", path.c_str(),
         static_cast<unsigned long long>(job.plan.file_bytes >> 20), wbfs_piece_count(job.plan.file_bytes));
    const di::RawDiscSource disc;
    SplitFileSink sink(path, kWbfsSplitBytes);
    bool ok = write_wbfs_image(disc, job.plan, job.usage, sink, progress, error);
    if (ok && !sink.close_piece()) {
        error = "writing " + path + " failed (is the drive full?)";
        ok = false;
    }
    if (!ok) {
        logf("Disc copy: not finished: %s. What was written is removed.\n", error.c_str());
        sink.remove_all();
        rmdir(folder.c_str());  // only when empty: another copy may share it
        return false;
    }
    logf("Disc copy: %s is complete, %u piece(s)\n", path.c_str(), static_cast<unsigned>(sink.piece_count()));
    error.clear();
    return true;
}

}  // namespace riftwii::wii
