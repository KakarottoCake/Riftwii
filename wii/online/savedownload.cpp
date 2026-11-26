// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "savedownload.hpp"

#include <ogc/lwp_watchdog.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <new>
#include <vector>

#include "httpmem.hpp"
#include "log.hpp"
#include "netsock.hpp"
#include "sdfile.hpp"
#include "skin.hpp"
#include "usbnand.hpp"
#include "riftwii/checkedalloc.hpp"
#include "riftwii/gcbackup.hpp"
#include "riftwii/gcreal.hpp"
#include "riftwii/saveinstall.hpp"
#include "riftwii/saveinflate.hpp"
#include "riftwii/sharednand.hpp"

namespace riftwii::wii::saves {
namespace {

constexpr const char* kWork = "sd:/riftwii/save-download";
constexpr const char* kDownload = "sd:/riftwii/save-download/download.tmp";
constexpr const char* kList = "sd:/riftwii/save-download/list.tmp";
SaveListing* g_list = nullptr;
std::size_t g_count = 0;
bool g_have_list = false;
bool g_install = false;
SaveListing g_pick;
std::string g_destination;
std::uint64_t g_title_id = 0;
std::string g_error, g_backup;
bool g_success = false;
bool g_pending = false;
std::atomic<bool> g_finished{false};
std::atomic<bool> g_stop{false};
std::atomic<unsigned> g_percent{0};
std::atomic<Stage> g_stage{Stage::Download};

bool stopped() { return g_stop.load() || NetCancelRequested(); }

bool room_on_drive(const std::string& path, std::uint64_t need) {
    struct statvfs info;
    if (statvfs(path.c_str(), &info) != 0) return false;
    const std::uint64_t unit = info.f_frsize ? info.f_frsize : info.f_bsize;
    return unit != 0 && static_cast<std::uint64_t>(info.f_bavail) >= (need + unit - 1) / unit;
}

bool read_file(const std::string& path, std::size_t limit, std::vector<std::uint8_t>& bytes) {
    struct stat status;
    if (stat(path.c_str(), &status) != 0 || status.st_size < 0 ||
        static_cast<std::uint64_t>(status.st_size) > limit || !S_ISREG(status.st_mode)) return false;
    if (!try_resize(bytes, static_cast<std::size_t>(status.st_size))) return false;
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    bool ok = std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
    ok = std::fclose(file) == 0 && ok;
    return ok;
}

bool fetch_list() {
    std::vector<std::uint8_t> text;
    bool read = false;
#ifdef RIFTWII_TEST_BUILD
    // The test build only: a list on the card stands in for the published
    // one (Dolphin tests before the catalogue is up, or of records it lacks).
    if (read_file("sd:/riftwii/test_saves.json", kSaveListMaxBytes, text)) {
        logf("Test build: the save list is sd:/riftwii/test_saves.json (%u bytes)\n",
             static_cast<unsigned>(text.size()));
        read = true;
    }
#endif
    if (!read) {
        FileDownload got;
        if (!HttpDownloadToFile(kSaveListUrl, kList, kSaveListMaxBytes, got, g_error, 15000, {},
                                HttpRedirects::SameHost)) return false;
        read = read_file(kList, kSaveListMaxBytes, text);
        std::remove(kList);
    }
    if (!read || stopped()) {
        g_error = "the save list could not be read or was stopped";
        return false;
    }
    std::size_t skipped = 0;
    if (!parse_save_list(reinterpret_cast<const char*>(text.data()), text.size(), g_list,
                         kSaveListMaxEntries, g_count, skipped, g_error)) return false;
    logf("Saves: the list has %u usable saves; %u entries were left out.\n",
          static_cast<unsigned>(g_count), static_cast<unsigned>(skipped));
    return true;
}

bool install_gci() {
    // Only RiftWii's emulated cards, never a physical card or another
    // loader's card. The destination came from the selected game's choices.
    const std::string cards = "sd:/riftwii/gc/cards/";
    if (g_destination.compare(0, cards.size(), cards) != 0 ||
        g_destination.find("..") != std::string::npos) {
        g_error = "the save has no RiftWii SD memory card destination";
        return false;
    }
    const auto recovered = gc_recover_card(g_destination, g_error);
    if (recovered == GcRecovery::Failed || recovered == GcRecovery::BothKept) return false;
    std::vector<std::uint8_t> card, gci;
    struct stat status;
    const bool existed = stat(g_destination.c_str(), &status) == 0;
    FILE* download = std::fopen(kDownload, "rb");
    if (!download) {
        g_error = "the downloaded GameCube save could not be opened";
        return false;
    }
    const ZipReadAt read = [&](std::uint64_t at, std::uint8_t* out, std::size_t bytes) {
        return !stopped() && at <= g_pick.size && bytes <= g_pick.size - at &&
               std::fseek(download, static_cast<long>(at), SEEK_SET) == 0 &&
               std::fread(out, 1, bytes, download) == bytes;
    };
    SaveZipInflater inflater(stopped);
    const bool got_gci = save_read_gci(g_pick, read, &inflater, gci, g_error);
    std::fclose(download);
    if (!got_gci) return false;
    if (existed && !read_file(g_destination, kGcBackupMaxBytes, card)) {
        g_error = "the downloaded save or the memory card could not be read";
        return false;
    }
    if (!existed) {
        // gc_card_format sizes the card with a plain assign; reserve its
        // 2 MiB first so a full heap is an error here, not a crash.
        if (!try_reserve(card, std::size_t{GcCardFormat{}.size_mbits} * (0x100000u / 8u))) {
            g_error = "there is not enough memory for a new memory card";
            return false;
        }
        GcCardFormat format;
        format.shift_jis = gc_card_shift_jis(g_pick.game_id);
        format.format_time = gettime();
        gc_card_format(card, format);
    }
    if (!save_apply_gci(g_pick, gci, card, true, g_error) || stopped()) return false;
    if (!room_on_drive("sd:/", card.size() * 2ull + (1u << 20))) {
        g_error = "the SD card has no room for the new card and its backup";
        return false;
    }
    if (existed) {
        const time_t now = time(nullptr);
        struct tm local;
        localtime_r(&now, &local);
        const std::string folder = gc_card_backup_folder(g_destination);
        const auto before = gc_backup_card(g_destination, folder, gc_backup_stamp(local));
        if (before.what == GcBackupResult::What::Failed) {
            g_error = "the memory card could not be backed up: " + before.error;
            return false;
        }
        g_backup = folder + "/" + before.name;
    }
    mkdir("sd:/riftwii/gc", 0777);
    mkdir("sd:/riftwii/gc/cards", 0777);
    const std::size_t slash = g_destination.rfind('/');
    mkdir(g_destination.substr(0, slash).c_str(), 0777);
    g_stage.store(Stage::Finish);
    return gc_real_commit(g_destination, card, [] { return !stopped(); }, g_error);
}

// `text` with every mention of the mounted folder replaced by the drive's.
std::string on_drive(std::string text, const std::string& mounted, const std::string& drive) {
    if (mounted.empty()) return text;
    for (std::size_t at = text.find(mounted); at != std::string::npos; at = text.find(mounted, at + drive.size())) {
        text.replace(at, mounted.size(), drive);
    }
    return text;
}

struct UsbMount {
    bool mounted = false;
    ~UsbMount() { if (mounted) unmount_usb_nand_folder(); }
};

bool install_wii() {
    std::string root = g_destination;
    UsbMount mount;
    if (shared_nand_on_usb(root)) {
        std::string writable;
        if (!mount_usb_nand_folder(root, writable, g_error)) return false;
        root = writable;
        mount.mounted = true;
    }
    const std::string drive = root.substr(0, root.find('/') + 1);
    if (!room_on_drive(drive, g_pick.unpacked_size + (1u << 20))) {
        g_error = "the save drive has no room to unpack the save beside the current one";
        return false;
    }
    FILE* archive = std::fopen(kDownload, "rb");
    if (!archive) {
        g_error = "the downloaded ZIP could not be read";
        return false;
    }
    const ZipReadAt read = [&](std::uint64_t at, std::uint8_t* out, std::size_t bytes) {
        return !stopped() && at <= g_pick.size && bytes <= g_pick.size - at &&
               std::fseek(archive, static_cast<long>(at), SEEK_SET) == 0 &&
               std::fread(out, 1, bytes, archive) == bytes;
    };
    const time_t now = time(nullptr);
    struct tm local;
    localtime_r(&now, &local);
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S", &local);
    SaveZipInflater inflater(stopped);
    const bool ok = save_install_wii_zip(g_pick, root, read, &inflater, true, timestamp, g_backup, g_error,
        [](std::uint64_t done, std::uint64_t total) {
            g_percent.store(total ? static_cast<unsigned>(done * 100 / total) : 0);
            return !stopped();
        }, g_title_id);
    std::fclose(archive);
    // The temporary USB mount is gone when the menu shows these: name the
    // folder on the drive the player chose instead.
    if (mount.mounted) {
        g_backup = on_drive(g_backup, root, g_destination);
        g_error = on_drive(g_error, root, g_destination);
    }
    return ok;
}

bool install_save() {
    if (!save_listing_ok(g_pick) || g_pick.format == SaveFormat::DataBin) {
        g_error = "this save needs to be extracted on a PC first";
        return false;
    }
    if (!room_on_drive("sd:/", g_pick.size + (1u << 20))) {
        g_error = "the SD card has no room for the save download";
        return false;
    }
    FileDownload got;
    if (!HttpDownloadToFile(g_pick.url, kDownload, g_pick.size, got, g_error, 15000,
        [](std::size_t bytes) {
            g_percent.store(static_cast<unsigned>(std::min<std::uint64_t>(100, bytes * 100ull / g_pick.size)));
        })) return false;
    g_stage.store(Stage::Check);
    if (stopped() || got.length != g_pick.size || !DigestMatches(got.digest, g_pick.sha256)) {
        g_error = "the save download was stopped or did not match its approved size and SHA-256";
        return false;
    }
    g_stage.store(Stage::Install);
    g_percent.store(0);
    return g_pick.format == SaveFormat::Gci ? install_gci() : install_wii();
}

void job() {
    mkdir("sd:/riftwii", 0777);
    mkdir(kWork, 0777);
    g_success = NetStart(g_error, true) && !stopped() && (g_install ? install_save() : fetch_list());
    if (!g_success && g_error.empty()) g_error = "the save download was stopped";
    std::remove(kDownload);
    std::remove(kList);
    if (g_install) {
        logf("Saves: %s for %s %s. %s%s\n", g_pick.id, g_pick.game_id,
              g_success ? "was installed" : "could not be installed", g_error.c_str(),
              g_backup.empty() ? "" : (" Backup: " + g_backup).c_str());
        forget_sd_layout();
    }
    g_finished.store(true, std::memory_order_release);
}

bool start() {
    if (g_pending) return false;
    g_error.clear();
    g_backup.clear();
    g_success = false;
    g_stop.store(false);
    g_percent.store(0);
    g_stage.store(Stage::Download);
    g_finished.store(false);
    if (!NetRunInBackground(job, true, false)) return false;
    g_pending = true;
    return true;
}

}  // namespace

bool HaveList() { return g_have_list; }

const SaveListing* Listing(std::size_t& count) {
    count = g_have_list ? g_count : 0;
    return g_have_list ? g_list : nullptr;
}

bool StartList() {
    if (g_pending) return false;
    if (!g_list) {
        g_list = reinterpret_cast<SaveListing*>(skin::Mem2Alloc(kSaveListMaxEntries * sizeof(SaveListing)));
        if (!g_list) return false;
        for (std::size_t i = 0; i < kSaveListMaxEntries; ++i) new (g_list + i) SaveListing{};
    }
    g_install = false;
    return start();
}

bool StartInstall(const SaveListing& save, const std::string& destination, std::uint64_t title_id) {
    if (g_pending) return false;
    g_install = true;
    g_pick = save;
    g_destination = destination;
    g_title_id = title_id;
    return start();
}

Progress CurrentProgress() { return {g_stage.load(), g_percent.load()}; }

void Cancel() {
    g_stop.store(true);
    NetCancelBackground();
}

bool TakeResult(bool& success, std::string& backup, std::string& error) {
    if (!g_pending || !g_finished.load(std::memory_order_acquire)) return false;
    // Wait for the wrapper to release the shared network worker too.
    if (NetBackgroundBusy()) return false;
    success = g_success;
    error = g_error;
    backup = g_backup;
    if (!g_install && success) g_have_list = true;
    g_pending = false;
    return true;
}

}  // namespace riftwii::wii::saves
