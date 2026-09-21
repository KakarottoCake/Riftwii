// SPDX-License-Identifier: GPL-3.0-or-later
#include "usbcatalog.hpp"

#include <fat.h>
#include <gccore.h>
#include <ogc/es.h>
#include <ogc/usbstorage.h>
#include <malloc.h>
#include <sdcard/wiisd_io.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <dirent.h>
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
    if (!__io_usbstorage.startup()) { error = "USB storage did not start (use a powered FAT32 USB drive)"; return false; }
    if (!__io_usbstorage.isInserted()) { error = "no USB mass-storage device is inserted"; return false; }
    // libfat's mount path performs the interface setup that populates the
    // storage capacity/sector-size state. Do this before inspecting it.
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
    UsbImage image; std::string error;
    if (!make_image(volume, prefix, path, siblings, fmt, image, error)) { failure = error; return false; }
    std::unique_ptr<UsbDiscSource> disc;
    if (!UsbDiscSource::open(image, disc, error)) { failure = error; return false; }
    DiscHeader header;
    if (!read_disc_header(*disc, header, error) || !header.wii_magic) { failure = error.empty() ? "not a Wii image" : error; return false; }
    ImageGame game; game.device=device; game.path=path; game.id=header.game_id; game.title=header.title;
    game.revision=header.version; game.disc_number=header.disc_number; game.format=fmt;
    if (!build_usb_fragments(image, game.fragments, error)) { failure=error; return false; }
    catalog.games.push_back(std::move(game)); return true;
}
void scan_dir(const Fat32Volume& volume, const std::string& prefix, ImageDevice device, const std::string& dir, bool nested, UsbImageFormat fmt, ImageCatalog& c, std::string& failure) {
    std::unique_ptr<DIR,int(*)(DIR*)> d(opendir(dir.c_str()), closedir); if (!d) return;
    std::vector<std::string> names; while (dirent* e=readdir(d.get())) names.emplace_back(e->d_name); std::sort(names.begin(),names.end());
    for (const std::string& n:names) {
        if (c.games.size() >= kMaxGames) return;
        std::string path;
        if (!join(dir,n,path)) continue;
        struct stat st{}; if (stat(path.c_str(),&st)!=0) continue;
        if (S_ISDIR(st.st_mode) && nested) {
            std::unique_ptr<DIR,int(*)(DIR*)> sub(opendir(path.c_str()), closedir); if (!sub) continue;
            std::vector<std::string> subnames; while (dirent* e=readdir(sub.get())) subnames.emplace_back(e->d_name); std::sort(subnames.begin(),subnames.end());
            for (const std::string& x:subnames) { if (c.games.size()>=kMaxGames) return; std::string p; if (join(path,x,p) && extension(x,".wbfs")) add_game(volume,prefix,device,p,subnames,fmt,c,failure); }
        } else if (S_ISREG(st.st_mode) && ((fmt==UsbImageFormat::Wbfs && extension(n,".wbfs")) || (fmt==UsbImageFormat::Iso && extension(n,".iso")))) add_game(volume,prefix,device,path,names,fmt,c,failure);
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
}

// Read-only ticket query per candidate slot: installed or not, without
// touching the running IOS. Identity (d2x vs stub vs other) is decided at
// launch by the F9/FA probe after the reload; this only decides whether a
// missing-cIOS warning is shown while games are listed.
bool slot_has_ticket(int slot) {
    u32 views = 0;
    const u64 title = 0x100000000ull | static_cast<u64>(slot);
    const s32 res = ES_GetNumTicketViews(title, &views);
    return res >= 0 && views >= 1;
}

bool scan_usb_games(UsbCatalog& out, std::string& error) {
    out = UsbCatalog{}; out.device = ImageDevice::Usb; if (!ensure_usb(error)) return false;
    std::string failure; scan_dir(g_usb_volume, "usb:/", ImageDevice::Usb, "usb:/wbfs", true, UsbImageFormat::Wbfs, out, failure); scan_dir(g_usb_volume, "usb:/", ImageDevice::Usb, "usb:/games", false, UsbImageFormat::Iso, out, failure);
    std::stable_sort(out.games.begin(),out.games.end(),[](const UsbGame&a,const UsbGame&b){ return a.id==b.id ? a.path<b.path : a.id<b.id; });
    out.status = out.games.empty() ? (failure.empty() ? "No valid FAT32 Wii images under usb:/wbfs or usb:/games" : "No valid USB images: " + failure) : "USB: " + std::to_string(out.games.size()) + " valid game(s)";
    // Dolphin has no cIOS slots by design; warning there would be noise.
    if (!running_in_dolphin()) {
        const CiosSlotState slots[] = {{249, slot_has_ticket(249)}, {250, slot_has_ticket(250)}, {251, slot_has_ticket(251)}};
        out.cios_note = cios_readiness_note(slots, 3);
        if (!out.cios_note.empty()) logf("USB: %s\n", out.cios_note.c_str());
    }
    error.clear(); return true;
}

bool scan_sd_games(ImageCatalog& out, std::string& error) {
    out = ImageCatalog{}; out.device = ImageDevice::Sd;
    if (!__io_wiisd.isInserted()) { error = "no SD card is inserted"; return false; }
    if (!Fat32Volume::mount(&sd_read, g_sd_volume, error)) { error = "SD is not a readable FAT32 volume: " + error; return false; }
    if (g_sd_volume.geometry().bytes_per_sector != 512) { error = "SD has non-512-byte sectors; d2x fragment mode needs 512"; return false; }
    std::string failure; scan_dir(g_sd_volume, "sd:/", ImageDevice::Sd, "sd:/wbfs", true, UsbImageFormat::Wbfs, out, failure); scan_dir(g_sd_volume, "sd:/", ImageDevice::Sd, "sd:/games", false, UsbImageFormat::Iso, out, failure);
    std::stable_sort(out.games.begin(),out.games.end(),[](const ImageGame&a,const ImageGame&b){ return a.id==b.id ? a.path<b.path : a.id<b.id; });
    out.status = out.games.empty() ? (failure.empty() ? "No valid FAT32 Wii images under sd:/wbfs or sd:/games" : "No valid SD images: " + failure) : "SD: " + std::to_string(out.games.size()) + " valid game(s)";
    if (!running_in_dolphin()) {
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
    fatUnmount("sd:");
    __io_wiisd.shutdown();
    unmount_usb_games();
    di::close();
    const ReloadResult r=reload_ios(cios_slot,error);
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
    if (!di::probe_d2x(mode,error)) {
        error = "IOS" + std::to_string(cios_slot) + " is not a d2x cIOS: " + error +
                "; " + std::string(device_name(game.device)) + " boot needs d2x (v11 beta3 is the latest) in 249, 250 or 251";
        return post_reload_failure(log_path, error);
    }
    const std::uint32_t device = game.device == ImageDevice::Usb ? 1 : 2;
    if (!di::configure_frag(device,storage,static_cast<std::uint32_t>(bytes.size()),error)) { error = "d2x F9 fragment setup failed: " + error; return post_reload_failure(log_path, error); }
    // Existing physical probes reset the drive. Disable reset after F9 so
    // the following virtual probe cannot clear d2x's emulation state.
    if (!di::disable_reset(error)) { error = "d2x F6 reset-disable failed: " + error; return post_reload_failure(log_path, error); }
    if (!fatMountSimple("sd", &__io_wiisd)) {
        error="d2x is configured but SD could not be remounted after IOS reload";
        return post_reload_failure(log_path, error);
    }
    if (log_path) LogOpen(log_path, true);
    return true;
}

}  // namespace riftwii::wii
