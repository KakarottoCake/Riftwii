// SPDX-License-Identifier: GPL-3.0-or-later
#include "usbcatalog.hpp"

#include <fat.h>
#include <gccore.h>
#include <ogc/es.h>
#include <ogc/usbstorage.h>
#include <malloc.h>
#include <sdcard/wiisd_io.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>

#include "di.hpp"
#include "ios_reload.hpp"
#include "log.hpp"
#include "riftwii/disc.hpp"

namespace riftwii::wii {
namespace {
Fat32Volume g_usb_volume;
Fat32Volume g_sd_volume;
bool g_raw_mounted = false;
bool g_libfat_mounted = false;
constexpr std::size_t kMaxGames = 128, kMaxPath = 240;

bool usb_read(std::uint64_t sector, std::uint32_t count, std::uint8_t* out) {
    return sector <= 0xFFFFFFFFull && __io_usbstorage.readSectors(static_cast<sec_t>(sector), count, out);
}
bool sd_read(std::uint64_t sector, std::uint32_t count, std::uint8_t* out) {
    return sector <= 0xFFFFFFFFull && __io_wiisd.readSectors(static_cast<sec_t>(sector), count, out);
}
const char* device_name(ImageDevice device) { return device == ImageDevice::Usb ? "USB" : "SD"; }
bool extension(const std::string& name, const char* ext) {
    const std::size_t n = std::strlen(ext); if (name.size() < n) return false;
    for (std::size_t i=0;i<n;++i) if (std::tolower(static_cast<unsigned char>(name[name.size()-n+i])) != ext[i]) return false;
    return true;
}
bool join(const std::string& a, const std::string& b, std::string& out) {
    if (a.size() + 1 + b.size() > kMaxPath) return false;
    out = a + "/" + b;
    return true;
}
bool ensure_usb(std::string& error) {
    if (g_libfat_mounted && g_raw_mounted) return true;
    logf("USB: starting storage\n");
    if (!__io_usbstorage.startup()) { error = "USB storage did not start (use a powered FAT32 USB drive)"; return false; }
    logf("USB: checking for a device\n");
    if (!__io_usbstorage.isInserted()) { error = "no USB mass-storage device is inserted"; return false; }
    // libfat's mount path performs the interface setup that populates the
    // storage capacity/sector-size state. Do this before inspecting it.
    logf("USB: mounting\n");
    if (!fatMountSimple("usb", &__io_usbstorage)) { error = "cannot mount USB storage as usb:"; return false; }
    g_libfat_mounted = true;
    if (__io_usbstorage_sector_size != 512) {
        error = "USB device has " + std::to_string(__io_usbstorage_sector_size) + "-byte sectors; d2x fragment mode requires 512";
        fatUnmount("usb:"); g_libfat_mounted = false;
        return false;
    }
    if (!Fat32Volume::mount(&usb_read, g_usb_volume, error)) {
        error = "USB is not a readable FAT32 volume: " + error;
        fatUnmount("usb:"); g_libfat_mounted = false;
        return false;
    }
    g_raw_mounted = true;
    return true;
}
bool add_piece(const Fat32Volume& volume, const std::string& prefix, const std::string& path, UsbImage& image, std::string& error) {
    if (path.compare(0, prefix.size(), prefix) != 0) { error = "internal image path is invalid"; return false; }
    UsbImagePiece p; p.path=path;
    if (!volume.lookup(path.substr(prefix.size() - 1), p.file, error) || p.file.entry.is_directory) return false;
    std::unique_ptr<FileByteSource> src;
    // The FAT32 entry carries the exact size: stat() cannot represent
    // multi-GB images on 32-bit targets, and the 256 MiB streaming cap
    // must not apply here (d2x reads bulk bytes itself; only headers go
    // through this source).
    if (FileByteSource::open(path, p.file.entry.size, src, error) != OpenStatus::Ok) return false;
    p.source = std::move(src); image.pieces.push_back(std::move(p)); return true;
}
bool make_image(const Fat32Volume& volume, const std::string& prefix, const std::string& primary, const std::vector<std::string>& siblings, UsbImageFormat format,
                UsbImage& image, std::string& error) {
    image = UsbImage{}; image.format = format;
    const std::size_t slash = primary.find_last_of('/');
    if (slash == std::string::npos) { error = "internal image path is invalid"; return false; }
    std::vector<std::string> pieces;
    if (!collect_split_pieces(primary.substr(0, slash), primary.substr(slash + 1), siblings, format, pieces,
                              error)) {
        return false;
    }
    for (const std::string& p : pieces) {
        if (!add_piece(volume, prefix, p, image, error)) return false;
    }
    return true;
}
bool add_game(const Fat32Volume& volume, const std::string& prefix, ImageDevice device, const std::string& path,
              const std::vector<std::string>& siblings, UsbImageFormat fmt, ImageCatalog& catalog, std::string& failure) {
    // Logged before the work, so a scan that never ends names its image.
    logf("%s scan: %s\n", device_name(device), path.c_str());
    const auto skip = [&](const std::string& why) {
        logf("  skipped: %s\n", why.c_str());
        failure = why;
        return false;
    };
    UsbImage image; std::string error;
    if (!make_image(volume, prefix, path, siblings, fmt, image, error)) return skip(error);
    std::unique_ptr<UsbDiscSource> disc;
    if (!UsbDiscSource::open(image, disc, error)) return skip(error);
    DiscHeader header;
    if (!read_disc_header(*disc, header, error) || !header.wii_magic) return skip(error.empty() ? "not a Wii image" : error);
    ImageGame game; game.device=device; game.path=path; game.id=header.game_id; game.title=header.title;
    game.revision=header.version; game.disc_number=header.disc_number; game.format=fmt;
    if (!build_usb_fragments(image, game.fragments, error)) return skip(error);
    logf("  %s \"%s\", %u piece(s)\n", game.id.c_str(), game.title.c_str(), static_cast<unsigned>(image.pieces.size()));
    catalog.games.push_back(std::move(game)); return true;
}
// Lists a catalog directory with the volume's own cycle-capped walker,
// never libfat's readdir: a cross-linked directory chain (e.g. from an
// interrupted multi-GB copy) loops readdir forever on successful reads,
// which looks exactly like a hang and releases the moment the card is
// pulled. Missing or unreadable directories are skipped silently, as
// opendir-NULL was before. Names come back sorted, without "." and ".."
// (which the old listing descended into, adding top-level images twice).
void scan_dir(const Fat32Volume& volume, const std::string& prefix, ImageDevice device, const std::string& dir, bool nested, UsbImageFormat fmt, ImageCatalog& c, std::string& failure) {
    // dir like "usb:/wbfs": the part after the device prefix addresses the volume.
    const std::string sub = dir.substr(prefix.size() - 1);
    std::vector<Fat32Entry> entries;
    std::string error;
    if (!volume.list(sub, entries, error)) {
        // A missing games folder is normal (disc-only users); anything else
        // is recorded. The marker is this codebase's own tested error text.
        if (!error.empty() && error.find("no such directory") == std::string::npos) {
            logf("%s scan: cannot list %s: %s\n", device_name(device), dir.c_str(), error.c_str());
            failure = error;
        }
        return;
    }
    std::sort(entries.begin(), entries.end(),
              [](const Fat32Entry& a, const Fat32Entry& b) { return a.name < b.name; });
    std::vector<std::string> siblings;
    siblings.reserve(entries.size());
    for (const Fat32Entry& e : entries) siblings.push_back(e.name);
    for (const Fat32Entry& e : entries) {
        if (c.games.size() >= kMaxGames) return;
        std::string path;
        if (!join(dir, e.name, path)) continue;
        if (e.is_directory && nested) {
            std::vector<Fat32Entry> subentries;
            if (!volume.list(sub + "/" + e.name, subentries, error)) {
                if (!error.empty() && error.find("no such directory") == std::string::npos) {
                    logf("%s scan: cannot list %s: %s\n", device_name(device), path.c_str(), error.c_str());
                    failure = error;
                }
                continue;
            }
            std::sort(subentries.begin(), subentries.end(),
                      [](const Fat32Entry& a, const Fat32Entry& b) { return a.name < b.name; });
            std::vector<std::string> subnames;
            subnames.reserve(subentries.size());
            for (const Fat32Entry& x : subentries) subnames.push_back(x.name);
            for (const std::string& x : subnames) { if (c.games.size()>=kMaxGames) return; std::string p; if (join(path,x,p) && extension(x,".wbfs")) add_game(volume,prefix,device,p,subnames,fmt,c,failure); }
        } else if (!e.is_directory && ((fmt==UsbImageFormat::Wbfs && extension(e.name,".wbfs")) || (fmt==UsbImageFormat::Iso && extension(e.name,".iso")))) add_game(volume,prefix,device,path,siblings,fmt,c,failure);
    }
}

// This is intentionally non-recursive: all post-reload paths make one
// bounded SD remount attempt and restore the caller-selected append log.
bool restore_sd_and_log(const char* log_path, std::string& error) {
    if (!fatMountSimple("sd", &__io_wiisd)) {
        error += "; additionally could not remount SD after IOS reload";
        return false;
    }
    if (log_path) LogOpen(log_path, true);
    return true;
}

bool post_reload_failure(const char* log_path, std::string& error) {
    restore_sd_and_log(log_path, error);
    return false;
}

// A slot can hold a ticket while the title behind it is missing, stubbed or
// half-installed. ES_LaunchTitleBackground still succeeds for those, and IOS
// then never comes back up -- by which point IPC, SD and USB have already
// been torn down and nothing can report the failure. Reading the installed
// TMD view costs one IPC call on the *current* IOS and tells us the title
// really is there, so a bad slot is skipped instead of stranding the
// console. Only positive evidence of a bad slot rejects it.
tmd_view g_tmd_view[(4096 + sizeof(tmd_view)) / sizeof(tmd_view)] ATTRIBUTE_ALIGN(32);

bool slot_title_is_launchable(int slot, std::string& why) {
    const u64 title = 0x100000000ull | static_cast<u64>(slot);
    u32 views = 0;
    if (ES_GetNumTicketViews(title, &views) < 0 || views < 1) {
        why = "no ticket";
        return false;
    }
    u32 size = 0;
    const s32 sized = ES_GetTMDViewSize(title, &size);
    if (sized < 0) {
        why = "installed ticket but no title (ES " + std::to_string(sized) + ")";
        return false;
    }
    if (size < sizeof(tmd_view) || size > sizeof(g_tmd_view)) {
        // An implausible size is not evidence the title is broken; let the
        // existing post-reload checks judge it.
        return true;
    }
    std::memset(g_tmd_view, 0, sizeof(g_tmd_view));
    const s32 got = ES_GetTMDView(title, g_tmd_view, size);
    if (got < 0) {
        why = "title metadata unreadable (ES " + std::to_string(got) + ")";
        return false;
    }
    if (g_tmd_view[0].num_contents == 0) {
        why = "title has no contents";
        return false;
    }
    if (cios_revision_is_stub(g_tmd_view[0].title_version)) {
        why = "holds a stub, not a cIOS";
        return false;
    }
    return true;
}
}

// Read-only per-slot query: is there a title here that could actually be
// launched, without touching the running IOS. Identity (d2x vs some other
// cIOS) is still decided at launch by the F9/FA probe after the reload;
// this decides whether a missing-cIOS warning is shown while games are
// listed, and it is the same test that gates the reload itself, so the
// warning and the launch never disagree.
bool slot_has_ticket(int slot) {
    std::string why;
    return slot_title_is_launchable(slot, why);
}

bool scan_usb_games(ImageCatalog& out, std::string& error) {
    out = ImageCatalog{}; out.device = ImageDevice::Usb; if (!ensure_usb(error)) return false;
    std::string failure; scan_dir(g_usb_volume, "usb:/", ImageDevice::Usb, "usb:/wbfs", true, UsbImageFormat::Wbfs, out, failure); scan_dir(g_usb_volume, "usb:/", ImageDevice::Usb, "usb:/games", false, UsbImageFormat::Iso, out, failure);
    std::stable_sort(out.games.begin(),out.games.end(),[](const ImageGame&a,const ImageGame&b){ return a.id==b.id ? a.path<b.path : a.id<b.id; });
    out.status = out.games.empty() ? (failure.empty() ? "No valid FAT32 Wii images under usb:/wbfs or usb:/games" : "No valid USB images: " + failure) : "USB: " + std::to_string(out.games.size()) + " valid game(s)";
    logf("%s\n", out.status.c_str());
    // Dolphin has no cIOS slots by design; warning there would be noise.
    if (!running_in_dolphin()) {
        logf("USB: checking cIOS slots\n");
        const CiosSlotState slots[] = {{249, slot_has_ticket(249)}, {250, slot_has_ticket(250)}, {251, slot_has_ticket(251)}};
        out.cios_note = cios_readiness_note(slots, 3);
        if (!out.cios_note.empty()) logf("USB: %s\n", out.cios_note.c_str());
    }
    error.clear(); return true;
}

bool scan_sd_games(ImageCatalog& out, std::string& error) {
    out = ImageCatalog{}; out.device = ImageDevice::Sd;
    logf("SD: scanning for images\n");
    if (!__io_wiisd.isInserted()) { error = "no SD card is inserted"; return false; }
    if (!Fat32Volume::mount(&sd_read, g_sd_volume, error)) { error = "SD is not a readable FAT32 volume: " + error; return false; }
    if (g_sd_volume.geometry().bytes_per_sector != 512) { error = "SD has non-512-byte sectors; d2x fragment mode needs 512"; return false; }
    std::string failure; scan_dir(g_sd_volume, "sd:/", ImageDevice::Sd, "sd:/wbfs", true, UsbImageFormat::Wbfs, out, failure); scan_dir(g_sd_volume, "sd:/", ImageDevice::Sd, "sd:/games", false, UsbImageFormat::Iso, out, failure);
    std::stable_sort(out.games.begin(),out.games.end(),[](const ImageGame&a,const ImageGame&b){ return a.id==b.id ? a.path<b.path : a.id<b.id; });
    out.status = out.games.empty() ? (failure.empty() ? "No valid FAT32 Wii images under sd:/wbfs or sd:/games" : "No valid SD images: " + failure) : "SD: " + std::to_string(out.games.size()) + " valid game(s)";
    logf("%s\n", out.status.c_str());
    if (!running_in_dolphin()) {
        logf("SD: checking cIOS slots\n");
        const CiosSlotState slots[] = {{249, slot_has_ticket(249)}, {250, slot_has_ticket(250)}, {251, slot_has_ticket(251)}};
        out.cios_note = cios_readiness_note(slots, 3);
        if (!out.cios_note.empty()) logf("SD: %s\n", out.cios_note.c_str());
    }
    error.clear(); return true;
}
void unmount_usb_games() { if (g_libfat_mounted) fatUnmount("usb:"); g_libfat_mounted=false; g_raw_mounted=false; }

bool activate_image_game(const ImageGame& game, int cios_slot, void*& storage, std::size_t& storage_bytes,
                         const char* log_path, std::string& error) {
    if (cios_slot < 3 || cios_slot > 255) { error = "cIOS slot must be 3..255"; return false; }
    // Vet the slot while IPC, SD and USB are still up: past the teardown
    // below a title that fails to start cannot be reported or recovered.
    // Dolphin has no cIOS slots and reload_ios() special-cases it there.
    if (!running_in_dolphin()) {
        std::string why;
        if (!slot_title_is_launchable(cios_slot, why)) {
            error = "IOS slot " + std::to_string(cios_slot) + ": " + why +
                    "; " + std::string(device_name(game.device)) +
                    " boot needs d2x (v11 beta3 is the latest) in 249, 250 or 251";
            logf("%s: skipping IOS%d (%s)\n", device_name(game.device), cios_slot, why.c_str());
            return false;
        }
    }
    std::vector<std::uint8_t> bytes; if (!game.fragments.encode(bytes,error)) return false;
    const std::size_t padded = (bytes.size()+31)&~std::size_t(31); void* allocated=memalign(32,padded);
    if (!allocated) { error="out of memory for d2x fragment list"; return false; }
    std::memset(allocated,0,padded); std::memcpy(allocated,bytes.data(),bytes.size());
    if (storage) free(storage);
    storage=allocated;
    storage_bytes=padded;
    // IOS reload makes every libfat descriptor stale. Release both media;
    // d2x then owns the image device and SD is mounted again for XML/saves.
    LogClose();
    logf("%s: reload IOS%d\n", device_name(game.device), cios_slot);
    fatUnmount("sd:");
    __io_wiisd.shutdown();
    unmount_usb_games();
    di::close();
    const ReloadResult r=reload_ios(cios_slot,error);
    if (r == ReloadResult::Terminal) return false;
    if (r==ReloadResult::NotInstalled || r==ReloadResult::Failed) return post_reload_failure(log_path, error);
    const s32 running = IOS_GetVersion();
    const s32 revision = IOS_GetRevision();
    logf("%s: reloaded IOS%d rev %d for slot %d\n", device_name(game.device), running, revision, cios_slot);
    if (revision >= 0 && cios_revision_is_stub(static_cast<std::uint32_t>(revision))) {
        error = "IOS slot " + std::to_string(cios_slot) + " holds a stub, not a cIOS: install d2x (v11 beta3 is the latest) in 249, 250 or 251";
        return post_reload_failure(log_path, error);
    }
    if (!di::open(error)) { error = "after cIOS reload: " + error; return post_reload_failure(log_path, error); }
    std::uint32_t mode=0;
    logf("%s: d2x probe\n", device_name(game.device));
    if (!di::probe_d2x(mode,error)) {
        error = "IOS" + std::to_string(cios_slot) + " is not a d2x cIOS: " + error +
                "; " + std::string(device_name(game.device)) + " boot needs d2x (v11 beta3 is the latest) in 249, 250 or 251";
        return post_reload_failure(log_path, error);
    }
    const std::uint32_t device = game.device == ImageDevice::Usb ? 1 : 2;
    logf("%s: d2x F9 config\n", device_name(game.device));
    if (!di::configure_frag(device,storage,static_cast<std::uint32_t>(bytes.size()),error)) { error = "d2x F9 fragment setup failed: " + error; return post_reload_failure(log_path, error); }
    // Existing physical probes reset the drive. Disable reset after F9 so
    // the following virtual probe cannot clear d2x's emulation state.
    logf("%s: d2x F6 reset-disable\n", device_name(game.device));
    if (!di::disable_reset(error)) { error = "d2x F6 reset-disable failed: " + error; return post_reload_failure(log_path, error); }
    logf("%s: SD remount\n", device_name(game.device));
    if (!fatMountSimple("sd", &__io_wiisd)) {
        error="d2x is configured but SD could not be remounted after IOS reload";
        return post_reload_failure(log_path, error);
    }
    if (log_path) LogOpen(log_path, true);
    return true;
}

}  // namespace riftwii::wii
