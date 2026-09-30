// SPDX-License-Identifier: GPL-3.0-or-later
#include "usbcatalog.hpp"

#include <fat.h>
#include <gccore.h>
#include <ogc/es.h>
#include <ogc/usbstorage.h>
#include <malloc.h>
#include <unistd.h>
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
#include "riftwii/launch.hpp"
#include "riftwii/rvz.hpp"
#include "riftwii/titles.hpp"
#include "riftwii/wbfspart.hpp"
#include "memlimits.hpp"
#include "umsdev.hpp"
#include "gcadapter.hpp"
#include "netsock.hpp"

namespace riftwii::wii {
namespace {
// USB drives may be FAT32 or NTFS, or formatted as WBFS (read through
// RiftWii's own walkers, the same way d2x will read the image: raw 512-byte
// blocks); SD images stay FAT32 or NTFS. libfat is only asked to mount usb:
// to bring the storage interface up; its answer does not matter.
std::unique_ptr<ImageVolume> g_usb_volume;
// A USB drive formatted as WBFS, with no file system: read only through
// wbfspart, its games in slots rather than files.
std::unique_ptr<WbfsPartition> g_usb_wbfs;
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

// libogc's storage driver takes a USB device change reported by IOS as
// the drive's removal and fails every read from then on: bringing up the
// network of a USB LAN adapter does that on IOS 58. isInserted() finds
// the drive again (opened under its new device id), so a failed read does
// that once and is tried again.
bool usb_read_sectors(sec_t sector, sec_t count, void* out) {
    if (__io_usbstorage.readSectors(sector, count, out)) return true;
    const bool back = __io_usbstorage.isInserted();
    logf("USB: a read of sector %lu failed; the drive %s\n", static_cast<unsigned long>(sector),
         back ? "was opened again (a USB device change, such as a network adapter starting?)" : "is gone");
    return back && __io_usbstorage.readSectors(sector, count, out);
}
// libfat's usb: goes through the same retry.
DISC_INTERFACE g_usb_io;

bool usb_read(std::uint64_t sector, std::uint32_t count, std::uint8_t* out) {
    return sector <= 0xFFFFFFFFull && usb_read_sectors(static_cast<sec_t>(sector), count, out);
}
bool d2x_usb_block_read(std::uint64_t sector, std::uint32_t count, std::uint8_t* out) {
    return ums::Read(sector, count, out);
}
// A read of the card can fail for a moment (it did as the menu's GameCube
// adapter started): tried again before the card counts as unreadable.
bool sd_read(std::uint64_t sector, std::uint32_t count, std::uint8_t* out) {
    if (sector > 0xFFFFFFFFull) return false;
    for (int attempt = 1; attempt <= 4; ++attempt) {
        if (sd_interface()->readSectors(static_cast<sec_t>(sector), count, out)) {
            if (attempt > 1) logf("SD: a read of sector %lu worked on try %d\n", static_cast<unsigned long>(sector), attempt);
            return true;
        }
        usleep(50000);
    }
    logf("SD: a read of sector %lu failed 4 times\n", static_cast<unsigned long>(sector));
    return false;
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
// On IOS 58 libogc lists every USB device that is not HID under the
// mass-storage class, a USB LAN adapter too (once IOS has seen it, so not
// always on the first start). Such an entry is asked for its interfaces as
// libogc's storage driver does; opening one there only resumes it and
// closing does not suspend it. Under a cIOS (device id 0) IOS filters the
// class itself. A device that cannot be asked is not counted.
bool is_mass_storage(const usb_device_entry& entry) {
    if (entry.device_id == 0) return true;
    s32 fd = -1;
    if (USB_OpenDevice(entry.device_id, entry.vid, entry.pid, &fd) < 0) return false;
    usb_devdesc desc;
    std::memset(&desc, 0, sizeof(desc));
    bool storage = false;
    if (USB_GetDescriptors(fd, &desc) >= 0) {
        for (u8 c = 0; c < desc.bNumConfigurations && !storage; ++c) {
            const usb_configurationdesc& conf = desc.configurations[c];
            for (u8 i = 0; i < conf.bNumInterfaces; ++i) {
                const usb_interfacedesc& iface = conf.interfaces[i];
                if (iface.bInterfaceClass == kUsbClassMassStorage && iface.bInterfaceProtocol == 0x50) storage = true;
            }
        }
        USB_FreeDescriptors(&desc);
    }
    USB_CloseDevice(&fd);
    if (!storage) logf("USB: %04x:%04x is not a drive (a network adapter or hub?), not counted\n", entry.vid, entry.pid);
    return storage;
}

// A USB DVD drive (2048-byte sectors) holding a Wii disc burned as-is:
// the whole drive is the disc, listed as one game and handed to d2x as a
// single fragment (riftwii/usbgame.hpp, build_raw_disc_fragments).
bool g_usb_raw_disc = false;
ImageGame g_usb_disc_game;

// The drive's sectors as a disc, through libogc's driver.
class UsbRawDisc final : public ByteSource {
public:
    std::uint64_t size() const override { return 0x230480000ull; }  // a dual-layer Wii disc
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override {
        static std::uint8_t bounce[32 * 1024] ATTRIBUTE_ALIGN(32);
        const std::uint32_t bytes = __io_usbstorage_sector_size;
        if (bytes == 0 || bytes > sizeof(bounce)) return false;
        while (length > 0) {
            const std::uint64_t sector = offset / bytes;
            const std::size_t skip = static_cast<std::size_t>(offset % bytes);
            const std::uint32_t count = static_cast<std::uint32_t>(
                std::min<std::uint64_t>((skip + length + bytes - 1) / bytes, sizeof(bounce) / bytes));
            if (sector > 0xFFFFFFFFull || !usb_read_sectors(static_cast<sec_t>(sector), count, bounce)) return false;
            const std::size_t take = std::min<std::size_t>(length, count * bytes - skip);
            std::memcpy(destination, bounce + skip, take);
            destination += take;
            offset += take;
            length -= take;
        }
        return true;
    }
};

// A drive with sectors other than 512 bytes: a Wii disc burned to a DVD in
// a USB DVD drive, or nothing RiftWii can use.
bool open_usb_raw_disc(std::string& error) {
    const std::uint32_t bytes = __io_usbstorage_sector_size;
    const UsbRawDisc disc;
    DiscHeader header;
    std::string why;
    if (!read_disc_header(disc, header, why) || !header.wii_magic) {
        error = "the USB device has " + std::to_string(bytes) +
                "-byte sectors and holds no Wii disc (a USB DVD drive plays a Wii game burned to the DVD as it is; "
                "other drives need 512-byte sectors)";
        return false;
    }
    std::uint64_t end = 0;
    ImageGame game;
    if (!disc_data_end(disc, end, why) || !build_raw_disc_fragments(end, bytes, game.fragments, why)) {
        error = "the Wii disc in the USB DVD drive cannot be read: " + why;
        return false;
    }
    game.device = ImageDevice::Usb;
    game.path = "usb:/";
    game.format = UsbImageFormat::Iso;
    game.id = header.game_id;
    game.title = header.title;
    game.revision = header.version;
    game.disc_number = header.disc_number;
    game.checked = true;
    logf("USB DVD: %s \"%s\", data to %llu MiB, %u sectors of %u bytes\n", game.id.c_str(), game.title.c_str(),
         static_cast<unsigned long long>(end >> 20), static_cast<unsigned>(game.fragments.entries[0].count),
         static_cast<unsigned>(bytes));
    g_usb_disc_game = std::move(game);
    g_usb_raw_disc = true;
    return true;
}

bool ensure_usb(std::string& error) {
    if (g_raw_mounted && (g_usb_volume || g_usb_raw_disc || g_usb_wbfs)) return true;
    // libogc's storage driver and d2x each pick one drive, not always the
    // same one when two are plugged in, and the games then go missing or
    // read the wrong disk. Say so instead of failing somewhere later.
    USB_Initialize();
    static usb_device_entry devices[8] ATTRIBUTE_ALIGN(32);
    u8 listed = 0;
    u8 drives = 0;
    if (USB_GetDeviceList(devices, 8, kUsbClassMassStorage, &listed) >= 0 && listed > 1) {
        for (u8 i = 0; i < listed && i < 8; ++i) drives += is_mass_storage(devices[i]) ? 1 : 0;
    }
    if (drives > 1) {
        logf("USB: %u drives plugged in\n", static_cast<unsigned>(drives));
        error = std::to_string(drives) + " USB drives are plugged in. RiftWii and d2x can use only one: "
                "unplug the others (keep the one with your games) and try again";
        return false;
    }
    logf("USB: starting storage\n");
    if (!__io_usbstorage.startup()) { error = "USB storage did not start (use a powered USB drive)"; return false; }
    g_usb_started = true;
    logf("USB: checking for a device\n");
    if (!__io_usbstorage.isInserted()) {
        // A DVD drive answers only once its disc has spun up.
        // A LAN adapter lists as storage on IOS 58: only a real drive waits.
        u8 any = 0;
        bool drive = false;
        if (USB_GetDeviceList(devices, 8, kUsbClassMassStorage, &any) >= 0) {
            for (u8 i = 0; i < any && i < 8 && !drive; ++i) drive = is_mass_storage(devices[i]);
        }
        if (drive) {
            for (int wait = 0; wait < 10 && !__io_usbstorage.isInserted(); ++wait) {
                if (wait == 0) logf("USB: a device is plugged in but not ready; waiting up to 10 s\n");
                usleep(1000000);
            }
        }
        if (!__io_usbstorage.isInserted()) { error = "no USB mass-storage device is inserted"; return false; }
    }
    // libfat's mount path performs the interface setup that populates the
    // storage capacity/sector-size state. It fails on NTFS, which is fine:
    // the drive is read raw below.
    if (!g_libfat_mounted) {
        logf("USB: mounting\n");
        g_usb_io = __io_usbstorage;
        g_usb_io.readSectors = usb_read_sectors;
        g_libfat_mounted = fatMountSimple("usb", &g_usb_io);
        if (!g_libfat_mounted) logf("USB: libfat cannot mount it (not FAT32); reading it raw\n");
    }
    logf("USB: %u-byte sectors\n", static_cast<unsigned>(__io_usbstorage_sector_size));
    if (__io_usbstorage_sector_size != 512) {
        if (!open_usb_raw_disc(error)) {
            unmount_usb_games();
            return false;
        }
        g_raw_mounted = true;
        return true;
    }
    if (!mount_image_volume(&usb_read, g_usb_volume, error, usb_wanted_folders())) {
        // A drive formatted as WBFS by a USB loader's tools (libfat cannot
        // mount it either, so nothing can write to it).
        WbfsPartition wbfs;
        std::string why;
        if (find_wbfs_partition(&usb_read, wbfs, why)) {
            logf("USB volume: WBFS at block %llu, %u of %u slots used, %u-MiB blocks\n",
                 static_cast<unsigned long long>(wbfs.lba), static_cast<unsigned>(wbfs.layout.used.size()),
                 static_cast<unsigned>(wbfs.layout.slots), static_cast<unsigned>(wbfs.layout.block_sectors / 2048));
            g_usb_wbfs = std::make_unique<WbfsPartition>(std::move(wbfs));
            g_raw_mounted = true;
            error.clear();
            return true;
        }
        // A WBFS it refuses is named; otherwise the drive is simply neither.
        if (why.compare(0, 5, "WBFS ") == 0) {
            error = "USB has a WBFS partition RiftWii cannot use: " + why;
            unmount_usb_games();
            return false;
        }
        error = "USB has no readable FAT32 or NTFS volume: " + error;
        if (error.find("signature missing") != std::string::npos) {
            error += " (a Wii U-formatted drive cannot be read: format it FAT32 on a computer)";
        }
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
    if (!p.file.inline_bytes.empty()) { error = "'" + path + "' is too small to be a disc image"; return false; }
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

// Reads an image's disc header and builds its d2x fragment list, the
// same way for a file and for a slot of a WBFS drive.
bool open_disc(const UsbImage& image, ImageGame& game, std::string& error) {
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
    pieces = static_cast<unsigned>(image.pieces.size());
    return open_disc(image, game, error);
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
// What a file in a catalog folder is read as, if it is a game. USB Loader
// GX and other loaders keep .iso images in wbfs too ("Title [ID]/ID.iso"),
// so wbfs takes both.
// `folder` is what the folder holds: Wbfs (wbfs and iso), Iso (iso and
// rvz), or Rvz for a folder of the user's own, which may hold all three.
bool image_format(const std::string& name, UsbImageFormat folder, UsbImageFormat& out) {
    if (folder != UsbImageFormat::Iso && extension(name, ".wbfs")) { out = UsbImageFormat::Wbfs; return true; }
    if (extension(name, ".iso")) { out = UsbImageFormat::Iso; return true; }
    if (folder != UsbImageFormat::Wbfs && extension(name, ".rvz")) { out = UsbImageFormat::Rvz; return true; }
    return false;
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
            for (const std::string& x : subnames) {
                if (c.games.size() >= kMaxGames) return;
                std::string p;
                UsbImageFormat f;
                const UsbImageFormat inner = fmt == UsbImageFormat::Rvz ? fmt : UsbImageFormat::Wbfs;
                if (join(path, x, p) && image_format(x, inner, f)) add_game(volume, prefix, device, p, subnames, f, c, failure);
            }
        } else if (!e.is_directory) {
            UsbImageFormat f;
            if (image_format(e.name, fmt, f)) add_game(volume, prefix, device, path, siblings, f, c, failure);
        }
    }
}

// A USB drive formatted as WBFS: one game per used slot, listed from the
// slot's copy of the disc header and opened when picked, like a named
// image file. Slots that are not Wii discs (a loader's own settings entry)
// are logged and left out.
void scan_wbfs(ImageCatalog& c, std::string& failure) {
    std::vector<WbfsDisc> discs;
    std::vector<std::string> skipped;
    std::string error;
    if (!list_wbfs_discs(&usb_read, *g_usb_wbfs, discs, skipped, error)) {
        logf("USB scan: %s\n", error.c_str());
        failure = error;
        return;
    }
    for (const std::string& s : skipped) logf("USB scan: WBFS %s: skipped\n", s.c_str());
    for (const WbfsDisc& disc : discs) {
        if (c.games.size() >= kMaxGames) return;
        ImageGame game;
        game.device = ImageDevice::Usb;
        game.path = "usb:/wbfs slot " + std::to_string(disc.slot);
        game.id = disc.id;
        game.title = disc.title;
        game.format = UsbImageFormat::Wbfs;
        game.wbfs_slot = static_cast<int>(disc.slot);
        c.games.push_back(std::move(game));
    }
}

// The folders settings.txt adds (game_folders), each with its own
// subfolders one level down, any image kind.
void scan_own_folders(const ImageVolume& volume, const std::string& device, ImageDevice kind, ImageCatalog& c,
                      std::string& failure) {
    for (const std::string& folder : game_folders_on(Settings(), device)) {
        const std::size_t before = c.games.size();
        scan_dir(volume, device + ":/", kind, device + ":" + folder, true, UsbImageFormat::Rvz, c, failure);
        logf("%s scan: %s:%s (game_folders): %u game(s)\n", device_name(kind), device.c_str(), folder.c_str(),
             static_cast<unsigned>(c.games.size() - before));
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
    if (game.wbfs_slot >= 0) {
        if (game.device != ImageDevice::Usb || !g_usb_wbfs) { error = "USB drive is not mounted any more"; return false; }
        logf("USB: opening WBFS slot %d\n", game.wbfs_slot);
        const std::string named = game.id;
        ImageGame opened = game;
        if (!open_disc(wbfs_slot_image(&usb_read, *g_usb_wbfs, static_cast<std::uint32_t>(game.wbfs_slot)), opened, error)) {
            logf("  cannot use it: %s\n", error.c_str());
            return false;
        }
        if (opened.id != named) logf("  the WBFS table says %s, the disc is %s\n", named.c_str(), opened.id.c_str());
        if (opened.display.empty() || opened.display == named) {
            opened.display = display_title(nullptr, opened.id, opened.path, opened.title);
        }
        logf("  %s \"%s\", %u fragment(s)\n", opened.id.c_str(), opened.title.c_str(),
             static_cast<unsigned>(opened.fragments.entries.size()));
        game = std::move(opened);
        error.clear();
        return true;
    }
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

std::vector<std::string> usb_mod_folders(const std::string& game_id) {
    std::vector<std::string> out;
    if (!g_usb_volume || game_id.empty()) return out;
    const ImageVolume& volume = *g_usb_volume;
    const auto lower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const auto found = [&](const std::string& folder, const std::string& file) {
        logf("USB: mod for %s on the USB drive: usb:%s\n", game_id.c_str(), file.c_str());
        const std::string where = "usb:" + folder;
        if (std::find(out.begin(), out.end(), where) == out.end()) out.push_back(where);
    };
    std::string error;
    // Found the way the SD card's are (codebuilds.cpp).
    const std::string want = lower(game_id) + ".gct";
    std::vector<VolumeEntry> top;
    if (!volume.list("/", top, error)) return out;
    for (const VolumeEntry& t : top) {
        if (!t.is_directory || t.name.empty() || t.name[0] == '.') continue;
        std::vector<std::string> dirs{"/" + t.name};
        if (lower(t.name) != "codes") dirs.push_back("/" + t.name + "/codes");
        for (const std::string& dir : dirs) {
            std::vector<VolumeEntry> entries;
            if (!volume.list(dir, entries, error)) continue;
            for (const VolumeEntry& e : entries) {
                if (e.is_directory || lower(e.name) != want) continue;
                found("/" + t.name, dir + "/" + e.name);
            }
        }
    }
    return out;
}

std::vector<std::string> usb_xml_names(const std::string& folder) {
    std::vector<std::string> out;
    std::vector<VolumeEntry> entries;
    std::string error;
    if (!g_usb_volume || !g_usb_volume->list(folder, entries, error)) return out;
    for (const VolumeEntry& e : entries) {
        if (!e.is_directory && !e.name.empty() && e.name[0] != '.' && extension(e.name, ".xml")) out.push_back(e.name);
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool read_usb_text(const std::string& usb_path, std::string& out) {
    out.clear();
    VolumeFile file;
    std::string error;
    if (!g_usb_volume || usb_path.compare(0, 5, "usb:/") != 0) return false;
    if (!g_usb_volume->lookup(usb_path.substr(4), file, error) || file.entry.is_directory ||
        file.entry.size > (1u << 20)) {
        return false;
    }
    out.assign(static_cast<std::size_t>(file.entry.size), '\0');
    if (!out.empty() && !g_usb_volume->read(file, 0, reinterpret_cast<std::uint8_t*>(&out[0]), out.size())) {
        out.clear();
        return false;
    }
    return true;
}

bool read_usb_range(const std::string& usb_path, std::uint64_t offset, std::uint8_t* out, std::size_t length,
                    std::uint64_t& size) {
    // The last file looked up is kept: a mounted image reads it sector by sector.
    static std::string cached_path;
    static VolumeFile cached;
    static const ImageVolume* cached_volume = nullptr;
    std::string error;
    size = 0;
    if (!g_usb_volume || usb_path.compare(0, 5, "usb:/") != 0) return false;
    if (cached_path != usb_path || cached_volume != g_usb_volume.get()) {
        cached_path.clear();
        if (!g_usb_volume->lookup(usb_path.substr(4), cached, error) || cached.entry.is_directory) return false;
        cached_path = usb_path;
        cached_volume = g_usb_volume.get();
    }
    size = cached.entry.size;
    if (offset > size || length > size - offset) return false;
    return length == 0 || g_usb_volume->read(cached, offset, out, length);
}

bool usb_volume_ready() { return g_usb_volume != nullptr; }

std::string rvz_warning(const ImageGame& game) {
    if (game.format != UsbImageFormat::Rvz) return std::string();
    std::string text = "RVZ is experimental; if it fails, use a WBFS or ISO copy.";
    if (game.rvz_support != RvzSupport::AtOwnRisk) return text;
    text += " Play at your own risk:";
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

bool sd_file_pieces(const std::string& sd_path, std::uint64_t& size, std::vector<Fragment>& out, std::string& error) {
    out.clear();
    size = 0;
    std::unique_ptr<ImageVolume> volume;
    VolumeFile file;
    if (sd_path.compare(0, 4, "sd:/") != 0) {
        error = "not on the SD card";
        return false;
    }
    if (!mount_image_volume(&sd_read, volume, error) || !volume->lookup(sd_path.substr(3), file, error)) return false;
    if (file.entry.is_directory) {
        error = "it is a folder";
        return false;
    }
    size = file.entry.size;
    out = file.fragments;
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
    if (g_usb_raw_disc) {
        out.games.push_back(g_usb_disc_game);
        apply_titles(out);
        out.status = "USB DVD drive: " + g_usb_disc_game.id;
        logf("%s\n", out.status.c_str());
        if (!running_in_dolphin()) {
            const CiosSlotState slots[] = {{249, slot_has_ticket(249)}, {250, slot_has_ticket(250)}, {251, slot_has_ticket(251)}};
            out.cios_note = cios_readiness_note(slots, 3);
        }
        error.clear();
        return true;
    }
    std::string failure;
    if (g_usb_wbfs) {
        scan_wbfs(out, failure);
    } else {
        scan_dir(*g_usb_volume, "usb:/", ImageDevice::Usb, "usb:/wbfs", true, UsbImageFormat::Wbfs, out, failure); scan_dir(*g_usb_volume, "usb:/", ImageDevice::Usb, "usb:/games", false, UsbImageFormat::Iso, out, failure);
        scan_own_folders(*g_usb_volume, "usb", ImageDevice::Usb, out, failure);
    }
    apply_titles(out);
    const std::string empty_drive = g_usb_wbfs ? std::string("No Wii games on the WBFS drive") : std::string("No valid Wii images under usb:/wbfs or usb:/games on the ") + g_usb_volume->kind() + " drive";
    const std::string unreadable = (g_usb_wbfs ? "Cannot read the WBFS drive: " : "No valid USB images: ") + failure;
    out.status = out.games.empty() ? (failure.empty() ? empty_drive : unreadable) : "USB: " + std::to_string(out.games.size()) + " valid game(s)";
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

std::vector<std::string> usb_wanted_folders() {
    std::vector<std::string> wanted = game_folders_on(Settings(), "usb");
    const std::vector<std::string>& defaults = default_wanted_folders();
    wanted.insert(wanted.end(), defaults.begin(), defaults.end());
    return wanted;
}

bool scan_sd_games(ImageCatalog& out, std::string& error) {
    out = ImageCatalog{}; out.device = ImageDevice::Sd;
    logf("SD: scanning for images\n");
    if (!__io_wiisd.isInserted()) { error = "no SD card is inserted"; return false; }
    if (!mount_image_volume(&sd_read, g_sd_volume, error)) { error = "SD has no readable FAT32 or NTFS volume: " + error; return false; }
    std::string failure; scan_dir(*g_sd_volume, "sd:/", ImageDevice::Sd, "sd:/wbfs", true, UsbImageFormat::Wbfs, out, failure); scan_dir(*g_sd_volume, "sd:/", ImageDevice::Sd, "sd:/games", false, UsbImageFormat::Iso, out, failure);
    scan_own_folders(*g_sd_volume, "sd", ImageDevice::Sd, out, failure);
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
void unmount_usb_games() { if (g_libfat_mounted) fatUnmount("usb:"); g_libfat_mounted=false; g_raw_mounted=false; g_usb_volume.reset(); g_usb_wbfs.reset(); g_usb_raw_disc=false; }
void release_usb_driver() {
    unmount_usb_games();
    if (g_usb_started) __io_usbstorage.shutdown();
    g_usb_started = false;
}

namespace {
// Everything the menu has open in IOS, let go of for a reload into a cIOS
// (the log is closed by now), the heap checked after each step: a tester's
// Wii U had it damaged across this sequence and the reload, and only the
// first step that breaks it says which (reload_ios closes the network, the
// adapter, d2x's USB device and the NAND again; they are no-ops by then).
// "" when every step left it whole.
std::string release_for_reload() {
    std::string note;
    const auto step = [&note](const char* after) {
        std::string problem;
        if (note.empty() && !mem::HeapIntact(problem)) note = std::string("broken after ") + after + ": " + problem;
    };
    release_wii_remotes();
    step("releasing the Wii Remotes");
    fatUnmount("sd:");
    __io_wiisd.shutdown();
    g_sd_back = false;
    step("closing the SD card");
    release_usb_driver();
    step("closing the USB drive");
    di::close();
    step("closing the disc drive");
    NetStop();
    step("stopping the network");
    GcAdapterStop();
    step("stopping the GameCube adapter");
    ums::Forget();
    step("closing d2x's USB device");
    ISFS_Deinitialize();
    step("closing the NAND");
    return note;
}

// After the reload, the log open again: what release_for_reload found, and
// the heap as the new IOS left it.
void log_release_heap(const std::string& before_reload) {
    std::string after;
    if (!before_reload.empty()) logf("Heap across the reload: %s\n", before_reload.c_str());
    else if (!mem::HeapIntact(after)) logf("Heap across the reload: whole after releasing, broken by the reload itself: %s\n", after.c_str());
}
}  // namespace

bool activate_disc_cios(int cios_slot, const char* log_path, std::string& error, const char* purpose) {
    const int slots[] = {cios_slot ? cios_slot : 249, cios_slot ? 0 : 250, cios_slot ? 0 : 251};
    std::string skipped;
    for (int slot : slots) {
        if (!slot) continue;
        std::string why;
        // Vetted while this IOS still runs, as for USB games.
        if (!running_in_dolphin() && !slot_title_is_launchable(slot, why)) {
            logf("Disc: skipping IOS%d (%s)\n", slot, why.c_str());
            skipped += (skipped.empty() ? "" : ", ") + std::string("IOS") + std::to_string(slot) + ": " + why;
            continue;
        }
        logf("Disc: reload IOS%d for %s; releasing Wii Remotes, USB, SD and DI\n", slot, purpose);
        mem::CheckHeap("before the cIOS reload");
        LogClose();
        const std::string released = release_for_reload();
        const ReloadResult r = reload_ios(slot, error, true);
        if (r == ReloadResult::Terminal) return false;
        g_sd_back = fatMountSimple("sd", sd_interface());
        if (g_sd_back && log_path) LogOpen(log_path, true);
        log_release_heap(released);
        if (r == ReloadResult::NotInstalled || r == ReloadResult::Failed) {
            logf("Disc: IOS%d: %s\n", slot, error.c_str());
            skipped += (skipped.empty() ? "" : ", ") + std::string("IOS") + std::to_string(slot) + ": " + error;
            continue;
        }
        logf("Disc: reloaded IOS%d rev %d (%s)\n", IOS_GetVersion(), IOS_GetRevision(), last_reload_detail().c_str());
        error.clear();
        return true;
    }
    error = std::string(purpose) + " needs a d2x cIOS (249, 250 or 251), and none could be started" +
            (skipped.empty() ? std::string() : " (" + skipped + ")");
    return false;
}

namespace {
// Before d2x gets the list: the list, what d2x's own USB device sees of
// the drive, and the game's first sector read through it. Some drives
// (a USB 3 HDD on a Wii U) left d2x's first read of the game hanging
// forever after the reload to the cIOS unless something had read the
// drive through d2x first; this read does, and waits for a drive that is
// slow to answer (up to 20 s).
void log_d2x_usb_view(const D2xFragmentList& list) {
    logf("USB: %u fragment(s), %u disc sectors\n", static_cast<unsigned>(list.entries.size()),
         static_cast<unsigned>(list.size));
    for (std::size_t i = 0; i < list.entries.size() && i < 4; ++i) {
        const D2xFragment& f = list.entries[i];
        logf("USB: fragment %u: disc sector %u, %u sectors, at drive sector %u\n", static_cast<unsigned>(i),
             static_cast<unsigned>(f.offset), static_cast<unsigned>(f.count), static_cast<unsigned>(f.sector));
    }
    if (list.entries.empty()) return;
    static std::uint8_t first[32 * 1024] ATTRIBUTE_ALIGN(32);  // one sector of any size
    bool read = false;
    for (int attempt = 1; attempt <= 20 && !read; ++attempt) {
        std::string why;
        if (!ums::Open(why)) {
            logf("USB (d2x): %s\n", why.c_str());
        } else {
            if (attempt == 1) {
                logf("USB (d2x): reading drive sector %u, the game's first\n",
                     static_cast<unsigned>(list.entries[0].sector));
            }
            read = ums::Read(list.entries[0].sector, 1, first);
            if (!read) logf("USB (d2x): that read failed (try %d)\n", attempt);
        }
        if (!read) {
            ums::Forget();  // opened again from scratch on the next try
            usleep(1000000);
        }
    }
    if (!read) {
        logf("USB (d2x): the drive did not answer in 20 s; trying the launch anyway\n");
        return;
    }
    char id[7];
    for (int i = 0; i < 6; ++i) id[i] = first[i] >= 0x20 && first[i] < 0x7F ? static_cast<char>(first[i]) : '.';
    id[6] = 0;
    logf("USB (d2x): it starts with \"%s\" (%02x %02x %02x %02x)\n", id, first[0], first[1], first[2], first[3]);
}
}  // namespace

bool activate_image_game(const ImageGame& game, int cios_slot, void*& storage, std::size_t& storage_bytes,
                         const char* log_path, std::string& error, bool block_ios_reload) {
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
    if (block_ios_reload && rvz) {
        // RiftWii's own resident code reads an RVZ; d2x only has its stub.
        error = "an RVZ game can't be kept as the disc for another program: use an ISO or WBFS";
        return false;
    }
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
    const std::string released = release_for_reload();
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
    log_release_heap(released);
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
    if (!mem::CheckHeap("after the cIOS reload")) {
        error = "RiftWii's memory was damaged during the reload into IOS" + std::to_string(cios_slot) +
                " (the details are in boot.log)";
        return post_reload_failure(log_path, error);
    }
    if (disc_device == ImageDevice::Usb && !rvz) log_d2x_usb_view(game.fragments);
    logf("%s: d2x F9 config\n", device_name(game.device));
    if (!di::configure_frag(device,storage,static_cast<std::uint32_t>(bytes.size()),error)) { error = "d2x F9 fragment setup failed: " + error; return post_reload_failure(log_path, error); }
    // Existing physical probes reset the drive. Disable reset after F9 so
    // the following virtual probe cannot clear d2x's emulation state.
    logf("%s: d2x F6 reset-disable\n", device_name(game.device));
    if (!di::disable_reset(error)) { error = "d2x F6 reset-disable failed: " + error; return post_reload_failure(log_path, error); }
    if (block_ios_reload) {
        // Before the first partition probe: d2x hides its ES commands once
        // ES has identified the disc's title.
        if (!di::set_ios_reload_block(true, static_cast<std::uint32_t>(running), error))
            return post_reload_failure(log_path, error);
        logf("%s: d2x keeps IOS%d across the program's own IOS reloads\n", device_name(game.device), running);
    }
    if (!g_sd_back) {
        error="d2x is configured but SD could not be remounted after IOS reload";
        return post_reload_failure(log_path, error);
    }
    if (rvz && game.device == ImageDevice::Usb) {
        // The RVZ itself, through d2x's USB device from now on.
        logf("USB: d2x's /dev/usb2 for the RVZ\n");
        if (!ums::Open(error) || !mount_image_volume(&d2x_usb_block_read, g_rvz_usb_volume, error, usb_wanted_folders())) {
            error = "USB drive after the cIOS reload: " + error;
            return post_reload_failure(log_path, error);
        }
    }
    if (rvz && !open_rvz_reads(game.device == ImageDevice::Usb ? g_rvz_usb_volume.get() : g_sd_volume.get(),
                               game.path, error)) {
        return post_reload_failure(log_path, error);
    }
    logf("%s: d2x ready\n", device_name(game.device));
    mem::CheckHeap("d2x ready");
    return true;
}

}  // namespace riftwii::wii
