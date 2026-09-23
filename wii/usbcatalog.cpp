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
#include <fstream>
#include <memory>
#include <set>
#include <sstream>

#include "di.hpp"
#include "ios_reload.hpp"
#include "log.hpp"
#include "menuios.hpp"
#include "riftwii/disc.hpp"
#include "riftwii/titles.hpp"

namespace riftwii::wii {
namespace {
// USB drives may be FAT32 or NTFS (read through Riftwii's own walkers, the
// same way d2x will read the image: raw 512-byte blocks); SD images stay
// FAT32 or NTFS too. libfat is only asked to mount usb: to bring the
// storage interface up; its answer does not matter.
std::unique_ptr<ImageVolume> g_usb_volume;
std::unique_ptr<ImageVolume> g_sd_volume;
bool g_raw_mounted = false;
bool g_libfat_mounted = false;
bool g_sd_back = false;  // SD remounted (and the log reopened) after an IOS reload
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
    if (g_raw_mounted && g_usb_volume) return true;
    logf("USB: starting storage\n");
    if (!__io_usbstorage.startup()) { error = "USB storage did not start (use a powered USB drive)"; return false; }
    logf("USB: checking for a device\n");
    if (!__io_usbstorage.isInserted()) { error = "no USB mass-storage device is inserted"; return false; }
    // libfat's mount path performs the interface setup that populates the
    // storage capacity/sector-size state. It fails on NTFS, which is fine:
    // the drive is read raw below.
    if (!g_libfat_mounted) {
        logf("USB: mounting\n");
        g_libfat_mounted = fatMountSimple("usb", &__io_usbstorage);
        if (!g_libfat_mounted) logf("USB: libfat cannot mount it (not FAT32); reading it raw\n");
    }
    logf("USB: %u-byte sectors\n", static_cast<unsigned>(__io_usbstorage_sector_size));
    if (__io_usbstorage_sector_size != 512) {
        error = "USB device has " + std::to_string(__io_usbstorage_sector_size) + "-byte sectors; d2x fragment mode requires 512";
        unmount_usb_games();
        return false;
    }
    if (!mount_image_volume(&usb_read, g_usb_volume, error)) {
        error = "USB has no readable FAT32 or NTFS volume: " + error;
        unmount_usb_games();
        return false;
    }
    logf("USB volume: %s\n", g_usb_volume->kind());
    g_raw_mounted = true;
    return true;
}
bool add_piece(const ImageVolume& volume, const std::string& prefix, const std::string& path, UsbImage& image, std::string& error) {
    if (path.compare(0, prefix.size(), prefix) != 0) { error = "internal image path is invalid"; return false; }
    UsbImagePiece p; p.path=path;
    if (!volume.lookup(path.substr(prefix.size() - 1), p.file, error)) return false;
    if (p.file.entry.is_directory) { error = "'" + path + "' is a directory"; return false; }
    // Headers are read through the same fragment list d2x will be given,
    // so what the catalog validates is exactly what the game will read.
    p.source = std::make_shared<VolumeFileSource>(volume, p.file);
    image.pieces.push_back(std::move(p)); return true;
}
bool make_image(const ImageVolume& volume, const std::string& prefix, const std::string& primary, const std::vector<std::string>& siblings, UsbImageFormat format,
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
bool add_game(const ImageVolume& volume, const std::string& prefix, ImageDevice device, const std::string& path,
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
// Lists a catalog directory with the volume's own bounded walker,
// never libfat's readdir: a cross-linked directory chain (e.g. from an
// interrupted multi-GB copy) loops readdir forever on successful reads,
// which looks exactly like a hang and releases the moment the card is
// pulled. Missing or unreadable directories are skipped silently, as
// opendir-NULL was before. Names come back sorted, without "." and ".."
// (which the old listing descended into, adding top-level images twice).
void scan_dir(const ImageVolume& volume, const std::string& prefix, ImageDevice device, const std::string& dir, bool nested, UsbImageFormat fmt, ImageCatalog& c, std::string& failure) {
    // dir like "usb:/wbfs": the part after the device prefix addresses the volume.
    const std::string sub = dir.substr(prefix.size() - 1);
    std::vector<VolumeEntry> entries;
    std::string error;
    if (!volume.list(sub, entries, error)) {
        // A missing games folder is normal (disc-only users); anything else
        // is recorded. The marker is this codebase's own tested error text.
        if (!error.empty() && error.find("no such ") == std::string::npos) {
            logf("%s scan: cannot list %s: %s\n", device_name(device), dir.c_str(), error.c_str());
            failure = error;
        }
        return;
    }
    std::sort(entries.begin(), entries.end(),
              [](const VolumeEntry& a, const VolumeEntry& b) { return a.name < b.name; });
    std::vector<std::string> siblings;
    siblings.reserve(entries.size());
    for (const VolumeEntry& e : entries) siblings.push_back(e.name);
    for (const VolumeEntry& e : entries) {
        if (c.games.size() >= kMaxGames) return;
        std::string path;
        if (!join(dir, e.name, path)) continue;
        if (e.is_directory && nested) {
            std::vector<VolumeEntry> subentries;
            if (!volume.list(sub + "/" + e.name, subentries, error)) {
                if (!error.empty() && error.find("no such ") == std::string::npos) {
                    logf("%s scan: cannot list %s: %s\n", device_name(device), path.c_str(), error.c_str());
                    failure = error;
                }
                continue;
            }
            std::sort(subentries.begin(), subentries.end(),
                      [](const VolumeEntry& a, const VolumeEntry& b) { return a.name < b.name; });
            std::vector<std::string> subnames;
            subnames.reserve(subentries.size());
            for (const VolumeEntry& x : subentries) subnames.push_back(x.name);
            for (const std::string& x : subnames) { if (c.games.size()>=kMaxGames) return; std::string p; if (join(path,x,p) && extension(x,".wbfs")) add_game(volume,prefix,device,p,subnames,fmt,c,failure); }
        } else if (!e.is_directory && ((fmt==UsbImageFormat::Wbfs && extension(e.name,".wbfs")) || (fmt==UsbImageFormat::Iso && extension(e.name,".iso")))) add_game(volume,prefix,device,path,siblings,fmt,c,failure);
    }
}

// Where a GameTDB titles.txt ("ID = Title" per line) may already sit on the
// card: Riftwii's own folder first, then where other loaders keep theirs.
const char* const kTitleFiles[] = {
    "sd:/riftwii/titles.txt", "sd:/titles.txt", "sd:/wiitdb.txt",
    "sd:/config/titles.txt", "sd:/apps/usbloader_gx/titles.txt", "sd:/usb-loader/titles.txt",
};

bool less_folded(const std::string& a, const std::string& b) {
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        const int x = std::tolower(static_cast<unsigned char>(a[i]));
        const int y = std::tolower(static_cast<unsigned char>(b[i]));
        if (x != y) return x < y;
    }
    return a.size() < b.size();
}

// Gives every game its display name and sorts the list by it, so the list
// reads "Super Mario Galaxy 2", not "SUPER MARIO GALAXY MORE", when a title
// database is on the card. Only the IDs on the drive are kept from it.
void apply_titles(ImageCatalog& c) {
    std::set<std::string> wanted;
    for (const ImageGame& g : c.games) {
        wanted.insert(g.id);
        wanted.insert(g.id.substr(0, 4));
    }
    TitleTable table;
    const char* used = nullptr;
    for (const char* path : kTitleFiles) {
        std::ifstream in(path, std::ios::binary);
        if (!in) continue;
        std::stringstream text;
        text << in.rdbuf();
        table.add_text(text.str(), &wanted);
        used = path;
        break;
    }
    for (ImageGame& g : c.games) g.display = display_title(used ? &table : nullptr, g.id, g.path, g.title);
    if (used) logf("Titles: %s, %u of %u game(s) named\n", used, static_cast<unsigned>(table.size()),
                   static_cast<unsigned>(c.games.size()));
    else logf("Titles: no titles.txt on SD; using folder and disc names\n");
    std::stable_sort(c.games.begin(), c.games.end(), [](const ImageGame& a, const ImageGame& b) {
        if (less_folded(a.display, b.display)) return true;
        if (less_folded(b.display, a.display)) return false;
        return a.id == b.id ? a.path < b.path : a.id < b.id;
    });
}

// This is intentionally non-recursive: all post-reload paths make one
// bounded SD remount attempt and restore the caller-selected append log.
bool restore_sd_and_log(const char* log_path, std::string& error) {
    if (g_sd_back) return true;  // remounted right after the reload; the log is open
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
    std::string failure; scan_dir(*g_usb_volume, "usb:/", ImageDevice::Usb, "usb:/wbfs", true, UsbImageFormat::Wbfs, out, failure); scan_dir(*g_usb_volume, "usb:/", ImageDevice::Usb, "usb:/games", false, UsbImageFormat::Iso, out, failure);
    apply_titles(out);
    out.status = out.games.empty() ? (failure.empty() ? std::string("No valid Wii images under usb:/wbfs or usb:/games on the ") + g_usb_volume->kind() + " drive" : "No valid USB images: " + failure) : "USB: " + std::to_string(out.games.size()) + " valid game(s)";
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
    if (!mount_image_volume(&sd_read, g_sd_volume, error)) { error = "SD has no readable FAT32 or NTFS volume: " + error; return false; }
    std::string failure; scan_dir(*g_sd_volume, "sd:/", ImageDevice::Sd, "sd:/wbfs", true, UsbImageFormat::Wbfs, out, failure); scan_dir(*g_sd_volume, "sd:/", ImageDevice::Sd, "sd:/games", false, UsbImageFormat::Iso, out, failure);
    apply_titles(out);
    out.status = out.games.empty() ? (failure.empty() ? "No valid Wii images under sd:/wbfs or sd:/games" : "No valid SD images: " + failure) : "SD: " + std::to_string(out.games.size()) + " valid game(s)";
    logf("%s\n", out.status.c_str());
    if (!running_in_dolphin()) {
        logf("SD: checking cIOS slots\n");
        const CiosSlotState slots[] = {{249, slot_has_ticket(249)}, {250, slot_has_ticket(250)}, {251, slot_has_ticket(251)}};
        out.cios_note = cios_readiness_note(slots, 3);
        if (!out.cios_note.empty()) logf("SD: %s\n", out.cios_note.c_str());
    }
    error.clear(); return true;
}
void unmount_usb_games() { if (g_libfat_mounted) fatUnmount("usb:"); g_libfat_mounted=false; g_raw_mounted=false; g_usb_volume.reset(); }

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
    // IOS reload makes every libfat descriptor stale. Release everything
    // that talks to the running IOS first, as other loaders do before
    // IOS_ReloadIOS: the Wii Remote stack (Bluetooth IPC in flight across a
    // reload can keep the new IOS from coming up), USB storage, SD and DI.
    // d2x then owns the image device and SD is mounted again for XML/saves.
    logf("%s: reload IOS%d (fragment list %u bytes); releasing Wii Remotes, USB, SD and DI\n",
         device_name(game.device), cios_slot, static_cast<unsigned>(bytes.size()));
    LogClose();
    release_wii_remotes();
    fatUnmount("sd:");
    __io_wiisd.shutdown();
    g_sd_back = false;
    unmount_usb_games();
    if (game.device == ImageDevice::Usb) __io_usbstorage.shutdown();
    di::close();
    const PadPairings pads_before = ReadPadPairings();
    const ReloadResult r=reload_ios(cios_slot,error,true);
    if (r == ReloadResult::Terminal) return false;
    if (r==ReloadResult::NotInstalled || r==ReloadResult::Failed) return post_reload_failure(log_path, error);
    const s32 running = IOS_GetVersion();
    const s32 revision = IOS_GetRevision();
    // The log is closed across the reload. Bring the card back first so
    // every later step, and any failure, lands in boot.log.
    g_sd_back = fatMountSimple("sd", &__io_wiisd);
    if (g_sd_back && log_path) LogOpen(log_path, true);
    logf("%s: reloaded IOS%d rev %d for slot %d (%s)\n", device_name(game.device), running, revision, cios_slot,
         last_reload_detail().c_str());
    {
        // fakemote writes its pairings when its IOS starts; entries that
        // appear across this reload prove it is in this slot.
        const PadPairings pads = ReadPadPairings();
        logf("%s: %s%s\n", device_name(game.device), DescribePadPairings(pads).c_str(),
             pads.fake > pads_before.fake ? ", added by this IOS just now (fakemote is in it)" : "");
    }
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
    if (!g_sd_back) {
        error="d2x is configured but SD could not be remounted after IOS reload";
        return post_reload_failure(log_path, error);
    }
    logf("%s: d2x ready\n", device_name(game.device));
    return true;
}

}  // namespace riftwii::wii
