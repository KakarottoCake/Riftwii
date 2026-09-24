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
#include <sys/stat.h>
#include <sstream>

#include "d2xsd.hpp"
#include "loadersettings.hpp"
#include "online.hpp"
#include "di.hpp"
#include "ios_reload.hpp"
#include "log.hpp"
#include "menuios.hpp"
#include "riftwii/disc.hpp"
#include "riftwii/rvz.hpp"
#include "riftwii/titles.hpp"
#include "umsdev.hpp"

namespace riftwii::wii {
namespace {
// USB drives may be FAT32 or NTFS (read through RiftWii's own walkers, the
// same way d2x will read the image: raw 512-byte blocks); SD images stay
// FAT32 or NTFS too. libfat is only asked to mount usb: to bring the
// storage interface up; its answer does not matter.
std::unique_ptr<ImageVolume> g_usb_volume;
std::unique_ptr<ImageVolume> g_sd_volume;
bool g_raw_mounted = false;
bool g_libfat_mounted = false;
bool g_usb_started = false;  // libogc's USB storage driver runs
bool g_sd_back = false;  // SD remounted (and the log reopened) after an IOS reload
// The RVZ game being launched: the loader's partition reads come from it.
std::shared_ptr<const ByteSource> g_rvz_file;
std::unique_ptr<RvzImage> g_rvz;
std::unique_ptr<RvzPartitionSource> g_rvz_partition;
std::size_t g_rvz_partition_index = SIZE_MAX;  // the partition last opened
VolumeFile g_rvz_volume_file;
bool g_rvz_on_usb = false;
// After the reload into the cIOS: the USB drive through d2x's /dev/usb2,
// for an RVZ game on it (libogc's USB driver is shut down by then).
std::unique_ptr<ImageVolume> g_rvz_usb_volume;
constexpr const char* kRvzStubDir = "sd:/riftwii/rvz";
constexpr std::size_t kMaxGames = 4000, kMaxPath = 240;
constexpr u8 kUsbClassMassStorage = 0x08;

bool usb_read(std::uint64_t sector, std::uint32_t count, std::uint8_t* out) {
    return sector <= 0xFFFFFFFFull && __io_usbstorage.readSectors(static_cast<sec_t>(sector), count, out);
}
bool d2x_usb_block_read(std::uint64_t sector, std::uint32_t count, std::uint8_t* out) {
    return ums::Read(sector, count, out);
}
bool sd_read(std::uint64_t sector, std::uint32_t count, std::uint8_t* out) {
    return sector <= 0xFFFFFFFFull && sd_interface()->readSectors(static_cast<sec_t>(sector), count, out);
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
    // libogc's storage driver and d2x each pick one drive, not always the
    // same one when two are plugged in, and the games then go missing or
    // read the wrong disk. Say so instead of failing somewhere later.
    USB_Initialize();
    static usb_device_entry devices[8] ATTRIBUTE_ALIGN(32);
    u8 drives = 0;
    if (USB_GetDeviceList(devices, 8, kUsbClassMassStorage, &drives) >= 0 && drives > 1) {
        logf("USB: %u drives plugged in\n", static_cast<unsigned>(drives));
        error = std::to_string(drives) + " USB drives are plugged in. RiftWii and d2x can use only one: "
                "unplug the others (keep the one with your games) and try again";
        return false;
    }
    logf("USB: starting storage\n");
    if (!__io_usbstorage.startup()) { error = "USB storage did not start (use a powered USB drive)"; return false; }
    g_usb_started = true;
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
// An RVZ game: its headers and what check_rvz says. The stub and fragment
// list d2x needs are made at launch (prepare_rvz_launch).
bool open_rvz_game(const ImageVolume& volume, const std::string& prefix, ImageGame& game, std::string& error) {
    UsbImage image;
    if (!add_piece(volume, prefix, game.path, image, error)) return false;
    RvzHead head;
    if (!read_rvz_head(*image.pieces[0].source, head, error)) return false;
    DiscHeader header;
    if (!parse_disc_header(head.disc_header.data(), head.disc_header.size(), header, error)) return false;
    const RvzVerdict verdict = check_rvz(head, image.pieces[0].file.entry.size);
    game.id = header.game_id;
    game.title = header.title;
    game.revision = header.version;
    game.disc_number = header.disc_number;
    game.rvz_support = verdict.support;
    game.rvz_reasons = verdict.reasons;
    game.rvz_disc_bytes = head.iso_size;
    game.checked = true;
    logf("  %s\n", describe_rvz(head).c_str());
    for (const std::string& r : game.rvz_reasons) logf("  %s\n", r.c_str());
    return true;
}

bool rvz_refused(const ImageGame& game, std::string& error) {
    if (game.format != UsbImageFormat::Rvz || game.rvz_support != RvzSupport::Unsupported) return false;
    error = "This RVZ cannot be played. " + (game.rvz_reasons.empty() ? std::string() : game.rvz_reasons.front());
    return true;
}

// A partition opened on the stub: its data from the RVZ.
const ByteSource* rvz_partition(std::uint64_t partition_offset) {
    if (!g_rvz) return nullptr;
    const RvzRawSource raw(*g_rvz);
    PartitionHeader header;
    std::string error;
    std::size_t index = SIZE_MAX;
    if (read_partition_header(raw, partition_offset, header, error)) index = g_rvz->partition_at(header.data_offset);
    if (index == SIZE_MAX) {
        logf("RVZ: no data for the partition at 0x%llx%s%s\n", static_cast<unsigned long long>(partition_offset),
             error.empty() ? "" : ": ", error.c_str());
        return nullptr;
    }
    g_rvz_partition.reset(new RvzPartitionSource(*g_rvz, index));
    g_rvz_partition_index = index;
    logf("RVZ: partition at 0x%llx read from the image (%llu bytes of data)\n",
         static_cast<unsigned long long>(partition_offset), static_cast<unsigned long long>(g_rvz_partition->size()));
    return g_rvz_partition.get();
}

void close_rvz_reads() {
    di::set_partition_resolver(nullptr);
    g_rvz_partition.reset();
    g_rvz_partition_index = SIZE_MAX;
    g_rvz.reset();
    g_rvz_file.reset();
    g_rvz_volume_file = VolumeFile{};
    g_rvz_on_usb = false;
}

// Opens the RVZ at `path` ("sd:/..." or "usb:/...") on `volume` for the
// loader's partition reads.
bool open_rvz_reads(const ImageVolume* volume, const std::string& path, std::string& error) {
    close_rvz_reads();
    const bool usb = path.compare(0, 5, "usb:/") == 0;
    VolumeFile file;
    if (volume == nullptr || !volume->lookup(path.substr(usb ? 4 : 3), file, error)) {
        error = "cannot find " + path + (error.empty() ? "" : ": " + error);
        return false;
    }
    g_rvz_volume_file = file;
    g_rvz_on_usb = usb;
    g_rvz_file = std::make_shared<VolumeFileSource>(*volume, file);
    if (!RvzImage::open(g_rvz_file, g_rvz, error)) {
        error = path + ": " + error;
        close_rvz_reads();
        return false;
    }
    di::set_partition_resolver(&rvz_partition);
    logf("RVZ: %s, %s\n", path.c_str(), describe_rvz(g_rvz->head()).c_str());
    return true;
}

bool write_if_changed(const std::string& path, const std::vector<std::uint8_t>& bytes, std::string& error) {
    {
        std::ifstream in(path, std::ios::binary);
        if (in) {
            std::vector<std::uint8_t> old((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (old == bytes) return true;
        }
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) {
        error = "cannot write " + path;
        return false;
    }
    return true;
}

// Before the IOS reload: the stub d2x boots from (the disc's headers,
// sd:/riftwii/rvz/<ID>.stub) and the fragment list that places it.
bool prepare_rvz_launch(const ImageGame& game, D2xFragmentList& fragments, std::string& error) {
    if (rvz_refused(game, error)) return false;
    // The stub goes on the SD card whichever drive holds the RVZ: d2x
    // boots the headers from there, and the loader writes nothing to USB.
    if (!g_sd_volume && !mount_image_volume(&sd_read, g_sd_volume, error)) {
        error = "RVZ games need the SD card for their headers: " + error;
        return false;
    }
    const bool usb = game.device == ImageDevice::Usb;
    if (!open_rvz_reads(usb ? g_usb_volume.get() : g_sd_volume.get(), game.path, error)) return false;
    RvzStub stub;
    const bool built = build_rvz_stub(*g_rvz, stub, error);
    close_rvz_reads();
    if (!built) {
        error = "cannot make the RVZ's stub: " + error;
        return false;
    }
    mkdir("sd:/riftwii", 0777);
    mkdir(kRvzStubDir, 0777);
    const std::string path = std::string(kRvzStubDir) + "/" + game.id + ".stub";
    if (!write_if_changed(path, stub.bytes, error)) return false;
    // The raw volume remembers folders and FAT blocks from before libfat
    // wrote the stub.
    if (!mount_image_volume(&sd_read, g_sd_volume, error)) {
        error = "SD volume after writing the RVZ stub: " + error;
        return false;
    }
    UsbImage file;
    if (!add_piece(*g_sd_volume, "sd:/", path, file, error)) return false;
    std::vector<DiscRange> ranges;
    for (const RvzStubRange& r : stub.ranges) ranges.push_back(DiscRange{r.disc_offset, r.stub_offset, r.length});
    if (!build_sparse_fragments(file, ranges, game.rvz_disc_bytes, fragments, error)) {
        error = "RVZ stub fragments: " + error;
        return false;
    }
    logf("RVZ: stub %s, %u bytes in %u range(s), %u fragment(s)\n", path.c_str(),
         static_cast<unsigned>(stub.bytes.size()), static_cast<unsigned>(stub.ranges.size()),
         static_cast<unsigned>(fragments.entries.size()));
    return true;
}

// Opens `game.path`: its pieces, the disc header and the d2x fragment list.
bool open_game(const ImageVolume& volume, const std::string& prefix, const std::vector<std::string>& siblings,
               ImageGame& game, unsigned& pieces, std::string& error) {
    if (game.format == UsbImageFormat::Rvz) {
        pieces = 1;
        return open_rvz_game(volume, prefix, game, error);
    }
    UsbImage image;
    if (!make_image(volume, prefix, game.path, siblings, game.format, image, error)) return false;
    std::unique_ptr<UsbDiscSource> disc;
    if (!UsbDiscSource::open(image, disc, error)) return false;
    DiscHeader header;
    if (!read_disc_header(*disc, header, error) || !header.wii_magic) {
        if (error.empty()) error = "not a Wii image";
        return false;
    }
    if (!build_usb_fragments(image, game.fragments, error)) return false;
    game.id = header.game_id;
    game.title = header.title;
    game.revision = header.version;
    game.disc_number = header.disc_number;
    game.checked = true;
    pieces = static_cast<unsigned>(image.pieces.size());
    return true;
}
bool add_game(const ImageVolume& volume, const std::string& prefix, ImageDevice device, const std::string& path,
              const std::vector<std::string>& siblings, UsbImageFormat fmt, ImageCatalog& catalog, std::string& failure) {
    ImageGame game; game.device=device; game.path=path; game.format=fmt;
    // Named "Title [ID]/ID.wbfs" by a backup manager: listed as it is and
    // opened when picked (check_image_game), so hundreds of games list in
    // seconds.
    game.id = id_from_image_path(path);
    if (!game.id.empty()) { catalog.games.push_back(std::move(game)); return true; }
    // Logged before the work, so a scan that never ends names its image.
    logf("%s scan: %s\n", device_name(device), path.c_str());
    std::string error; unsigned pieces = 0;
    if (!open_game(volume, prefix, siblings, game, pieces, error)) {
        logf("  skipped: %s\n", error.c_str());
        failure = error;
        return false;
    }
    logf("  %s \"%s\", %u piece(s)\n", game.id.c_str(), game.title.c_str(), pieces);
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
        else if (!e.is_directory && fmt==UsbImageFormat::Iso && extension(e.name,".rvz")) add_game(volume,prefix,device,path,siblings,UsbImageFormat::Rvz,c,failure);
    }
}

// Where a GameTDB titles.txt ("ID = Title" per line) may already sit on the
// card: RiftWii's own folder first, then where other loaders keep theirs.
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

// The game names for this session: GameTDB's list in the menu's
// language, downloaded to the card when the Wii is online (at most weekly),
// else a titles.txt another loader left on the card.
TitleTable g_titles;
bool g_titles_loaded = false;
std::string g_titles_from;

const TitleTable* titles() {
    if (g_titles_loaded) return g_titles_from.empty() ? nullptr : &g_titles;
    g_titles_loaded = true;
    const std::string lang = MenuLanguage();
    if (Settings().online) {
        std::string error;
        if (!UpdateTitles(lang, false, error)) logf("Titles: not downloaded: %s\n", error.c_str());
    }
    std::vector<std::string> paths = {TitlesPath(lang)};
    if (lang != "en") paths.push_back(TitlesPath("en"));
    for (const char* p : kTitleFiles) paths.push_back(p);
    for (const std::string& path : paths) {
        std::ifstream in(path, std::ios::binary);
        if (!in) continue;
        std::stringstream text;
        text << in.rdbuf();
        g_titles.add_text(text.str());
        if (g_titles.size() == 0) continue;
        g_titles_from = path;
        logf("Titles: %u game names from %s\n", static_cast<unsigned>(g_titles.size()), path.c_str());
        return &g_titles;
    }
    logf("Titles: no title list on SD; using folder and disc names\n");
    return nullptr;
}

// Gives every game its display name and sorts the list by it, so the list
// reads "Super Mario Galaxy 2", not "SUPER MARIO GALAXY MORE".
void apply_titles(ImageCatalog& c) {
    const TitleTable* table = titles();
    for (ImageGame& g : c.games) g.display = display_title(table, g.id, g.path, g.title);
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
    if (!fatMountSimple("sd", sd_interface())) {
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
bool check_image_game(ImageGame& game, std::string& error) {
    if (game.checked) return !rvz_refused(game, error);
    const ImageVolume* volume = game.device == ImageDevice::Usb ? g_usb_volume.get() : g_sd_volume.get();
    const std::string prefix = game.device == ImageDevice::Usb ? "usb:/" : "sd:/";
    const std::size_t slash = game.path.find_last_of('/');
    if (!volume || game.path.compare(0, prefix.size(), prefix) != 0 || slash == std::string::npos ||
        slash < prefix.size() - 1) {
        error = std::string(device_name(game.device)) + " drive is not mounted any more";
        return false;
    }
    logf("%s: opening %s\n", device_name(game.device), game.path.c_str());
    // The folder's names, for the pieces of a split image (.wbf1, ...).
    std::vector<VolumeEntry> entries;
    std::vector<std::string> siblings;
    if (!volume->list(game.path.substr(prefix.size() - 1, slash - (prefix.size() - 1)), entries, error)) {
        error = "cannot list the game's folder: " + error;
        logf("  %s\n", error.c_str());
        return false;
    }
    for (const VolumeEntry& e : entries) siblings.push_back(e.name);
    const std::string named = game.id;
    ImageGame opened = game;
    unsigned pieces = 0;
    if (!open_game(*volume, prefix, siblings, opened, pieces, error)) {
        logf("  cannot use it: %s\n", error.c_str());
        return false;
    }
    if (opened.id != named) logf("  its name says %s, the disc is %s\n", named.c_str(), opened.id.c_str());
    if (opened.display.empty() || opened.display == named) {
        opened.display = display_title(nullptr, opened.id, opened.path, opened.title);
    }
    logf("  %s \"%s\", %u piece(s)\n", opened.id.c_str(), opened.title.c_str(), pieces);
    game = std::move(opened);
    error.clear();
    return !rvz_refused(game, error);
}

std::string rvz_warning(const ImageGame& game) {
    if (game.format != UsbImageFormat::Rvz || game.rvz_support != RvzSupport::AtOwnRisk) return std::string();
    std::string text = "Play at your own risk:";
    for (const std::string& r : game.rvz_reasons) text += " " + r;
    return text;
}

bool serve_disc_from_rvz(const std::string& sd_path, std::string& error) {
    if (!g_sd_volume && !mount_image_volume(&sd_read, g_sd_volume, error)) {
        error = "SD has no readable volume: " + error;
        return false;
    }
    return open_rvz_reads(g_sd_volume.get(), sd_path, error);
}

namespace {

// A file's pieces on the card for the runtime (absolute sectors).
bool card_extents(const VolumeFile& file, std::vector<rt_rvz_extent>& out, std::string& error) {
    out.clear();
    std::uint64_t file_sector = 0;
    for (const Fragment& f : file.fragments) {
        if (f.sector + f.sector_count > UINT32_MAX || file_sector + f.sector_count > UINT32_MAX) {
            error = "the file lies beyond the card's first 2 TiB";
            return false;
        }
        out.push_back(rt_rvz_extent{static_cast<std::uint32_t>(file_sector), static_cast<std::uint32_t>(f.sector),
                                    static_cast<std::uint32_t>(f.sector_count)});
        file_sector += f.sector_count;
    }
    return true;
}

}  // namespace

bool rvz_resident_options(RvzResidentOptions& out, std::string& error) {
    out = RvzResidentOptions{};
    if (!g_rvz || g_rvz_partition_index == SIZE_MAX) {
        error = "RVZ: the game's partition was never opened from the image";
        return false;
    }
    if (g_rvz_on_usb) {
        out.usb_fd = ums::Fd();
        if (out.usb_fd < 0) {
            error = "RVZ: d2x's USB device is not open";
            return false;
        }
    }
    if (!g_rvz->runtime_table(g_rvz_partition_index, out.table, error)) {
        error = "RVZ: " + error;
        return false;
    }
    const std::string id(reinterpret_cast<const char*>(g_rvz->head().disc_header.data()), 6);
    mkdir("sd:/riftwii", 0777);
    mkdir(kRvzStubDir, 0777);
    const std::string path = std::string(kRvzStubDir) + "/" + id + ".groups";
    if (!write_if_changed(path, out.table.entries, error)) return false;
    out.table.entries = std::vector<std::uint8_t>();
    // A fresh view of the card finds the file libfat just wrote; the
    // RVZ's own file, open on the current one, has not moved.
    std::unique_ptr<ImageVolume> volume;
    VolumeFile groups;
    if (!mount_image_volume(&sd_read, volume, error) || !volume->lookup(path.substr(3), groups, error)) {
        error = "RVZ: cannot find " + path + " on the card: " + error;
        return false;
    }
    if (!card_extents(g_rvz_volume_file, out.extents, error) || !card_extents(groups, out.table_extents, error)) {
        error = "RVZ: " + error;
        return false;
    }
    out.enabled = true;
    logf("RVZ: %u groups for the game, table %s, the RVZ in %u piece(s) on the %s\n", out.table.group_count,
         path.c_str(), static_cast<unsigned>(out.extents.size()), g_rvz_on_usb ? "USB drive" : "SD card");
    error.clear();
    return true;
}

std::string GameDisplayName(const std::string& id, const std::string& internal) {
    return display_title(titles(), id, std::string(), internal);
}

void ReloadTitles() {
    g_titles = TitleTable{};
    g_titles_loaded = false;
    g_titles_from.clear();
}

void RenameGames(ImageCatalog& catalog) { apply_titles(catalog); }

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
void release_usb_driver() {
    unmount_usb_games();
    if (g_usb_started) __io_usbstorage.shutdown();
    g_usb_started = false;
}

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
    if (!game.checked) { error = "internal: the game was not opened before launch"; return false; }
    const bool rvz = game.format == UsbImageFormat::Rvz;
    D2xFragmentList rvz_fragments;
    if (rvz && !prepare_rvz_launch(game, rvz_fragments, error)) return false;
    std::vector<std::uint8_t> bytes; if (!(rvz ? rvz_fragments : game.fragments).encode(bytes,error)) return false;
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
    release_usb_driver();
    di::close();
    const PadPairings pads_before = ReadPadPairings();
    const ReloadResult r=reload_ios(cios_slot,error,true);
    if (r == ReloadResult::Terminal) return false;
    if (r==ReloadResult::NotInstalled || r==ReloadResult::Failed) return post_reload_failure(log_path, error);
    const s32 running = IOS_GetVersion();
    const s32 revision = IOS_GetRevision();
    // The log is closed across the reload. Bring the card back first so
    // every later step, and any failure, lands in boot.log. A game on the
    // SD card is read by d2x through its own SD device, which must then be
    // the card's only driver: the loader uses it too (d2xsd.hpp).
    // An RVZ game's disc is its stub, on the card (prepare_rvz_launch).
    const ImageDevice disc_device = rvz ? ImageDevice::Sd : game.device;
    if (disc_device == ImageDevice::Sd) use_d2x_sd(true);
    g_sd_back = fatMountSimple("sd", sd_interface());
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
    const std::uint32_t device = disc_device == ImageDevice::Usb ? 1 : 2;
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
    if (rvz && game.device == ImageDevice::Usb) {
        // The RVZ itself, through d2x's USB device from now on.
        logf("USB: d2x's /dev/usb2 for the RVZ\n");
        if (!ums::Open(error) || !mount_image_volume(&d2x_usb_block_read, g_rvz_usb_volume, error)) {
            error = "USB drive after the cIOS reload: " + error;
            return post_reload_failure(log_path, error);
        }
    }
    if (rvz && !open_rvz_reads(game.device == ImageDevice::Usb ? g_rvz_usb_volume.get() : g_sd_volume.get(),
                               game.path, error)) {
        return post_reload_failure(log_path, error);
    }
    logf("%s: d2x ready\n", device_name(game.device));
    return true;
}

}  // namespace riftwii::wii
