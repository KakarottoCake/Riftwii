// SPDX-License-Identifier: GPL-3.0-or-later
#include "boot.hpp"
#include "progress.hpp"

#include <fat.h>
#include <gccore.h>
#include <ogc/cache.h>
#include <ogc/conf.h>
#include <ogc/ios.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/machine/processor.h>
#include <ogc/system.h>
#include <ogc/video.h>
#include <sdcard/wiisd_io.h>
#include <sys/stat.h>
#include <wiiuse/wpad.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <malloc.h>
#include <utility>
#include <vector>

#include "d2xsd.hpp"
#include "di.hpp"
#include "ios_reload.hpp"
#include "log.hpp"
#include "riftwii/mempatch.hpp"
#include "padhook.hpp"
#include "resident.hpp"
#include "sdfile.hpp"
#include "sdio.hpp"
#include "umsdev.hpp"
#include "usbcatalog.hpp"
#include "wfc.hpp"
#include "codehandleronly_bin.h"
#include "riftwii/codehook.hpp"
#include "riftwii/gamelang.hpp"
#include "riftwii/symsearch.hpp"

namespace riftwii::wii {
namespace {

using ApploaderReport = void (*)(const char* format, ...);
using ApploaderInit = void (*)(ApploaderReport report);
using ApploaderMain = int (*)(void** destination, int* length, int* word_offset);
using ApploaderClose = void (*(*)(void))(void);
using ApploaderEntry = void (*)(ApploaderInit* init, ApploaderMain* main, ApploaderClose* close);

constexpr std::uint32_t kApploaderLoadAddress = 0x81200000;
constexpr std::uint32_t kLoaderStart = 0x80A00000;  // Makefile.wii: --section-start,.init
constexpr std::uint32_t kMem1Start = 0x80000000;
constexpr std::uint32_t kMem1End = 0x81800000;
constexpr std::uint32_t kMem2Start = 0x90000000;
constexpr std::uint32_t kMem2End = 0x94000000;
constexpr std::uint32_t kMaxFstBytes = 8 * 1024 * 1024;
constexpr std::uint64_t kWiiEpochOffset = 946684800;  // 2000-01-01 in Unix seconds

std::uint8_t g_tmd[0x4A00] ATTRIBUTE_ALIGN(32);

void apploader_report(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::printf("  apploader: ");
    std::vprintf(format, args);
    va_end(args);
}

bool aligned32(const void* p) { return (reinterpret_cast<std::uintptr_t>(p) & 31) == 0; }

// What the menu adds to the launch (SetLaunchExtras).
LaunchExtras g_extras;
// Whether the card and boot.log are still up (see release_card_and_log).
bool g_card_live_for_log = false;

// The Gecko code handler and the cheats' GCT (vendor-gecko/), called at
// the end of the game's video retrace handler. Leaves the game untouched
// and says why when that cannot be done.
bool install_cheats(const std::vector<MemoryRegion>& loaded, const std::vector<MemoryPatch>& patches,
                    std::string& why) {
    const std::vector<std::uint8_t>& gct = g_extras.cheat_gct;
    if (gct.size() > kCodeListEnd - kCodeListAddress) {
        why = "the cheats picked need " + std::to_string(gct.size()) + " bytes and the code handler holds " +
              std::to_string(kCodeListEnd - kCodeListAddress) + "; pick fewer";
        return false;
    }
    for (const MemoryPatch& p : patches) {
        if (p.has_offset && p.offset < kCodeListEnd && p.offset + p.value.size() > kCodeHandlerAddress) {
            why = "a pack's memory patch uses the code handler's memory (0x80001800-0x80003000)";
            return false;
        }
    }
    std::vector<CodeRange> text;
    for (const MemoryRegion& r : loaded) {
        text.push_back(CodeRange{r.address, reinterpret_cast<const std::uint8_t*>(r.address), r.length});
    }
    const std::uint32_t hook = find_cheat_hook(text);
    const std::uint32_t branch = hook != 0 ? encode_b(hook, kCodeHandlerEntry) : 0;
    if (branch == 0) {
        why = "the game's video retrace handler was not found, so there is nowhere to run them from";
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(kCodeHandlerAddress), codehandleronly_bin, codehandleronly_bin_size);
    std::memcpy(reinterpret_cast<void*>(kCodeHandlerAddress), g_extras.game_id.c_str(),
                std::min<std::size_t>(6, g_extras.game_id.size()));  // where cheat tools look for it
    std::memset(reinterpret_cast<void*>(kCodeListAddress), 0, kCodeListEnd - kCodeListAddress);
    std::memcpy(reinterpret_cast<void*>(kCodeListAddress), gct.data(), gct.size());
    DCFlushRange(reinterpret_cast<void*>(kCodeHandlerAddress), kCodeListEnd - kCodeHandlerAddress);
    ICInvalidateRange(reinterpret_cast<void*>(kCodeHandlerAddress), kCodeListEnd - kCodeHandlerAddress);
    *reinterpret_cast<volatile std::uint32_t*>(hook) = branch;
    DCFlushRange(reinterpret_cast<void*>(hook & ~31u), 32);
    ICInvalidateRange(reinterpret_cast<void*>(hook & ~31u), 32);
    logf("Cheats: %u code(s), %u bytes, handler at 0x%08x called from 0x%08x\n",
         static_cast<unsigned>(g_extras.cheat_count), static_cast<unsigned>(gct.size()), kCodeHandlerAddress, hook);
    return true;
}

// The render mode tables in what the apploader loaded: patched when the
// menu asks, and whether the game draws borders remembered for the menu
// (sd:/riftwii/choices/<ID>.video).
void apply_video(const std::vector<MemoryRegion>& loaded, bool may_mount) {
    VideoPatchReport report;
    unsigned language_sites = 0;
    for (const MemoryRegion& r : loaded) {
        std::uint8_t* bytes = reinterpret_cast<std::uint8_t*>(r.address);
        patch_video_modes(bytes, r.length, g_extras.video, report);
        language_sites += patch_game_language(bytes, r.length, g_extras.language);
        if (g_extras.video.any() || g_extras.language >= 0) DCFlushRange(bytes, r.length);
    }
    logf("Video: %s (mode %s, width %s, deflicker %s, borders %s)\n", report.describe().c_str(),
         to_string(g_extras.video.mode), to_string(g_extras.video.width), to_string(g_extras.video.deflicker),
         g_extras.video.remove_borders ? "removed" : "kept");
    if (g_extras.language >= 0) {
        logf("Language: %s, %u place(s) patched%s\n", game_language_name(g_extras.language), language_sites,
             language_sites == 0 ? " (the game reads it some other way: it keeps the console's)" : "");
    }
    if (g_extras.game_id.empty() || report.modes == 0) return;
    // After an IOS reload the card is down; with nothing else driving the
    // slot it is mounted just for this note.
    const bool mounted_here = !g_card_live_for_log && may_mount && sd_interface()->startup() &&
                              fatMountSimple("sd", sd_interface());
    if (!g_card_live_for_log && !mounted_here) return;
    const std::string path = "sd:/riftwii/choices/" + g_extras.game_id + ".video";
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fprintf(f, "side_borders = %s\ntop_borders = %s\n", report.side_borders ? "yes" : "no",
                     report.top_borders ? "yes" : "no");
        std::fclose(f);
    }
    if (mounted_here) {
        fatUnmount("sd:");
        sd_interface()->shutdown();
    }
}

// A resident SD reader/writer must keep using an IOS under which this card
// has already mounted successfully. In particular, launch-era IOSes such
// as IOS9 cannot initialise modern SDHC/SDXC cards after an IOS reload.
bool needs_resident_sd(const BootOptions& options) {
    if (!options.savegame_dir.empty() || !options.sd_replacements.empty()) return true;
    for (const VirtualFile& file : options.virtual_files) {
        if (!file.sd_runs.empty()) return true;
    }
    for (const rt_entry& entry : options.table_entries) {
        if (entry.kind == RT_KIND_SD || entry.kind == RT_KIND_USB) return true;  // USB: d2x's /dev/usb2 is open here
    }
    return false;
}

// E4 self-check: reads every SD run of a replacement through the loader's
// own client and logs the checksum of the bytes the game will see (the
// same h = h * 31 + byte the runtime reports).
bool verify_sd_replacement(const sdio::Card& card, const SdReplacement& r, std::string& error) {
    std::uint8_t* sectors = static_cast<std::uint8_t*>(memalign(32, 64 * 512));
    if (!sectors) {
        error = "out of memory";
        return false;
    }
    std::uint32_t checksum = 0;
    std::uint64_t total = 0;
    bool ok = true;
    for (const PlacedRun& run : r.runs) {
        std::uint64_t done = 0;
        while (done < run.length && ok) {
            const std::uint64_t at = run.skip + done;
            const std::uint32_t sector = static_cast<std::uint32_t>(run.source + at / 512);
            const std::uint32_t skip = static_cast<std::uint32_t>(at % 512);
            std::uint32_t count = static_cast<std::uint32_t>(std::min<std::uint64_t>(64, (skip + run.length - done + 511) / 512));
            if (!sdio::read_sectors(card, sector, count, sectors, error)) {
                ok = false;
                break;
            }
            const std::uint32_t take = static_cast<std::uint32_t>(std::min<std::uint64_t>(count * 512 - skip, run.length - done));
            for (std::uint32_t k = 0; k < take; ++k) checksum = checksum * 31u + sectors[skip + k];
            done += take;
        }
        total += run.length;
    }
    free(sectors);
    if (ok) {
        logf("SD check: 0x%llx bytes at partition offset 0x%llx read back, checksum %08x\n",
             static_cast<unsigned long long>(total), static_cast<unsigned long long>(r.virtual_offset), checksum);
    }
    return ok;
}

// Low memory is written through the data cache, like the apploader's own
// stores and libogc's, and flushed once before the jump. libogc's write32
// bypasses the cache; mixing it with dirty cache lines would let the flush
// overwrite it with stale data (Dolphin has no cache, so it would not show
// there).
// When the running IOS is kept for the launch nothing forces the card off
// before the game starts, so libfat and boot.log stay live through the
// apploader and the table build, where a hardware hang is otherwise
// invisible. They go right before the runtime's raw SD handle opens (two
// drivers must not drive the slot at once) or, without one, before the jump.
// (g_card_live_for_log is defined with g_extras, above.)
void release_card_and_log() {
    if (!g_card_live_for_log) return;
    logf("Releasing the SD card (the log ends here; the rest is on screen)\n");
    LogClose();
    fatUnmount("sd:");
    sd_interface()->shutdown();
    g_card_live_for_log = false;
}

void store32(std::uint32_t address, std::uint32_t value) {
    *reinterpret_cast<volatile std::uint32_t*>(address) = value;
}
std::uint32_t load32(std::uint32_t address) { return *reinterpret_cast<volatile std::uint32_t*>(address); }

// The disc id lives at 0x80000000 from the moment the drive reports it:
// the SDK apploader looks at the Wii magic there to decide that the read
// offsets it hands back are in 4-byte words, and the game reads its own
// id from the same place.
void publish_disc_id(const std::uint8_t id[32]) {
    std::memcpy(reinterpret_cast<void*>(kMem1Start), id, 32);
    DCFlushRange(reinterpret_cast<void*>(kMem1Start), 32);
}

bool in_ram(std::uint32_t start, std::uint32_t length) {
    const std::uint64_t end = std::uint64_t(start) + length;
    return (start >= kMem1Start && end <= kMem1End) || (start >= kMem2Start && end <= kMem2End);
}

// This loader's code, data, stack and heap all sit between the link
// address and the top of arena 1; a game whose DOL reaches up there would
// overwrite us while the apploader is still running.
bool overlaps_loader(std::uint32_t start, std::uint32_t length) {
    const std::uint64_t end = std::uint64_t(start) + length;
    const std::uint32_t loader_end = reinterpret_cast<std::uint32_t>(SYS_GetArena1Hi());
    return start < loader_end && end > kLoaderStart;
}

bool make_directories(const std::string& sd_path) {
    // Creates every directory component before the file name.
    std::string::size_type pos = sd_path.find(":/");
    if (pos == std::string::npos) return false;
    pos += 2;
    for (;;) {
        pos = sd_path.find('/', pos);
        if (pos == std::string::npos) return true;
        const std::string dir = sd_path.substr(0, pos);
        struct stat st;
        if (stat(dir.c_str(), &st) != 0 && mkdir(dir.c_str(), 0777) != 0) return false;
        ++pos;
    }
}

bool write_sd_file(const std::string& sd_path, const void* data, std::size_t length, std::string& error) {
    if (!make_directories(sd_path)) {
        error = "cannot create the directories for " + sd_path;
        return false;
    }
    FILE* f = std::fopen(sd_path.c_str(), "wb");
    if (!f) {
        error = "cannot create " + sd_path;
        return false;
    }
    const bool ok = std::fwrite(data, 1, length, f) == length;
    std::fclose(f);
    if (!ok) error = "short write to " + sd_path;
    return ok;
}

bool open_game_partition(const PartitionEntry& partition, Tmd& tmd, std::int32_t& es_result, std::string& error) {
    if ((partition.offset >> 2) > 0xFFFFFFFFull) {
        error = "partition offset beyond the drive's range";
        return false;
    }
    std::memset(g_tmd, 0, sizeof(g_tmd));
    if (!di::open_partition(static_cast<std::uint32_t>(partition.offset >> 2), g_tmd, sizeof(g_tmd), es_result, error)) {
        return false;
    }
    if (es_result < 0) {
        // The drive answered but ES refused the ticket/TMD: the partition
        // key is not set up and every read would return garbage.
        error = "ES refused the partition (ES result " + std::to_string(es_result) + ")";
        return false;
    }
    return parse_tmd(g_tmd, di::kTmdBufferBytes, tmd, error);
}

// Region letter of the game id to the VI standard the game expects; -1
// means "use the console's own setting".
int region_video_standard(char region) {
    switch (region) {
    case 'E': case 'J': case 'K': case 'W': case 'T': return VI_NTSC;
    case 'P': case 'D': case 'F': case 'I': case 'S': case 'H':
    case 'U': case 'V': case 'X': case 'Y': case 'L': case 'M': case 'R': return VI_PAL;
    default: return -1;
    }
}

// What a chosen video mode means on this console, for a game of `region`.
VideoTarget resolve_video_target(VideoMode mode, char region, bool progressive_ok) {
    VideoTarget t;
    switch (mode) {
    case VideoMode::System:
        switch (CONF_GetVideo()) {
        case CONF_VIDEO_PAL: t.format = CONF_GetEuRGB60() > 0 ? kViEurgb60 : kViPal; break;
        case CONF_VIDEO_MPAL: t.format = kViMpal; break;
        default: t.format = kViNtsc; break;
        }
        t.progressive = progressive_ok && t.format != kViPal;
        break;
    case VideoMode::Ntsc: t.format = kViNtsc; break;
    case VideoMode::Pal60: t.format = kViEurgb60; break;
    case VideoMode::Pal50: t.format = kViPal; break;
    case VideoMode::Progressive:
        // 480p: EuRGB60's for PAL games (their 60 Hz mode), NTSC's otherwise.
        t.format = region_video_standard(region) == VI_PAL ? kViEurgb60 : kViNtsc;
        t.progressive = true;
        break;
    default: break;
    }
    return t;
}

// Picks the VI mode the game will find configured and records it in the
// low-memory global the SDK reads (0x800000CC). A chosen video mode
// decides it, and becomes the target the game's tables are converted to.
void configure_video_for_game(char region) {
    const bool progressive_ok = CONF_GetProgressiveScan() > 0 && VIDEO_HaveComponentCable();
    g_extras.video.target = resolve_video_target(g_extras.video.mode, region, progressive_ok);
    const VideoTarget& target = g_extras.video.target;
    if (target.format >= 0) {
        GXRModeObj* forced = &TVNtsc480IntDf;
        if (target.progressive) forced = target.format == kViEurgb60 ? &TVEurgb60Hz480Prog : &TVNtsc480Prog;
        else if (target.format == kViEurgb60) forced = &TVEurgb60Hz480IntDf;
        else if (target.format == kViPal) forced = &TVPal528IntDf;
        else if (target.format == kViMpal) forced = &TVMpal480IntDf;
        logf("Video: forced to TV format %d%s\n", target.format, target.progressive ? ", 480p" : "");
        store32(0x800000CC, static_cast<std::uint32_t>(target.format));
        DCFlushRange(reinterpret_cast<void*>(0x800000CC), 4);
        VIDEO_Configure(forced);
        VIDEO_SetBlack(true);
        VIDEO_Flush();
        VIDEO_WaitVSync();
        return;
    }
    const bool progressive = progressive_ok;
    const bool pal60 = CONF_GetEuRGB60() > 0;
    int standard = region_video_standard(region);
    if (standard < 0) {
        switch (CONF_GetVideo()) {
        case CONF_VIDEO_PAL: standard = VI_PAL; break;
        case CONF_VIDEO_MPAL: standard = VI_MPAL; break;
        default: standard = VI_NTSC; break;
        }
    }
    GXRModeObj* mode = nullptr;
    std::uint32_t reg = VI_NTSC;
    switch (standard) {
    case VI_PAL:
        reg = pal60 ? VI_EURGB60 : VI_PAL;
        mode = progressive ? &TVEurgb60Hz480Prog : (pal60 ? &TVEurgb60Hz480IntDf : &TVPal528IntDf);
        break;
    case VI_MPAL:
        reg = VI_MPAL;
        mode = progressive ? &TVEurgb60Hz480Prog : &TVMpal480IntDf;
        break;
    default:
        reg = VI_NTSC;
        mode = progressive ? &TVNtsc480Prog : &TVNtsc480IntDf;
        break;
    }
    store32(0x800000CC, reg);
    DCFlushRange(reinterpret_cast<void*>(0x800000CC), 4);
    VIDEO_Configure(mode);
    VIDEO_SetBlack(true);
    VIDEO_Flush();
    VIDEO_WaitVSync();
}

}  // namespace

bool probe_disc(DiscProbe& out, std::string& error, const ProbeOptions& options) {
    if (!di::open(error)) return false;
    if (!options.virtual_source) {
        // Bounded wait, never the drive's blocking cover ioctl: with an
        // empty closed drive that call may never return, which used to
        // hang the loader on a black screen before video came up. Poll
        // for a few seconds (covers inserting a disc right now), then
        // report no disc; pressing DISC later probes again from scratch.
        bool inserted = false;
        if (!di::cover_status(inserted, error)) return false;
        for (int waited = 0; !inserted && waited < 20; ++waited) {
            if (waited == 0) logf("No disc: watching for one briefly...\n");
            usleep(250000);
            if (!di::cover_status(inserted, error)) return false;
        }
        if (!inserted) {
            error = "no disc in the drive";
            return false;
        }
        if (!di::reset(true, error)) return false;
    }
    std::uint8_t drive_info[32];
    if (!di::inquiry(drive_info, error)) return false;
    logf("Drive: rev %02x%02x dev %02x%02x fw %02x%02x%02x%02x\n", drive_info[0], drive_info[1], drive_info[2],
         drive_info[3], drive_info[4], drive_info[5], drive_info[6], drive_info[7]);
    if (!di::read_disc_id(out.disc_id, error)) return false;
    publish_disc_id(out.disc_id);

    di::SystemAreaSource system_area;
    if (!read_disc_header(system_area, out.header, error)) return false;
    if (!out.header.wii_magic) {
        error = "not a Wii disc (magic missing)";
        return false;
    }
    logf("Disc: %s  \"%s\"  disc %u version %u\n", out.header.game_id.c_str(), out.header.title.c_str(),
         out.header.disc_number, out.header.version);
    std::vector<PartitionEntry> table;
    if (!read_partition_table(system_area, table, error)) return false;
    if (!find_game_partition(table, out.partition)) {
        error = "no game partition in the partition table";
        return false;
    }
    logf("Partitions: %u listed, game partition at 0x%08llx\n", static_cast<unsigned>(table.size()),
         static_cast<unsigned long long>(out.partition.offset));
    if (!open_game_partition(out.partition, out.tmd, out.es_result, error)) return false;
    out.tmd_bytes.assign(g_tmd, g_tmd + di::kTmdBufferBytes);
    out.running_ios = IOS_GetVersion();
    logf("TMD: title %08x-%08x v%u, needs IOS%u (ES result %d); running IOS%d\n",
         static_cast<unsigned>(out.tmd.title_id >> 32), static_cast<unsigned>(out.tmd.title_id),
         out.tmd.title_version, out.tmd.required_ios(), out.es_result, out.running_ios);
    error.clear();
    return true;
}

bool read_partition_layout(OpenedPartition& out, std::string& error) {
    di::PartitionSource data;
    if (!read_partition_data_header(data, out.data_header, error)) return false;
    if (!read_apploader_header(data, out.apploader, error)) return false;
    logf("Data header: dol 0x%llx fst 0x%llx (%llu bytes, max %llu); apploader %s entry 0x%08x %u+%u bytes\n",
         static_cast<unsigned long long>(out.data_header.dol_offset),
         static_cast<unsigned long long>(out.data_header.fst_offset),
         static_cast<unsigned long long>(out.data_header.fst_size),
         static_cast<unsigned long long>(out.data_header.fst_max_size), out.apploader.date.c_str(),
         out.apploader.entry, out.apploader.size, out.apploader.trailer_size);
    const std::uint64_t fst_size = out.data_header.fst_size;
    if (fst_size == 0 || fst_size > kMaxFstBytes) {
        error = "FST size " + std::to_string(fst_size) + " is not plausible";
        return false;
    }
    out.fst_bytes.assign(static_cast<std::size_t>(fst_size), 0);
    if (!data.read(out.data_header.fst_offset, out.fst_bytes.data(), out.fst_bytes.size())) {
        error = "cannot read the FST";
        return false;
    }
    if (!Fst::parse(out.fst_bytes.data(), out.fst_bytes.size(), true, out.fst, error)) return false;
    logf("FST: %u entries\n", out.fst.count());
    error.clear();
    return true;
}

bool dump_file(const OpenedPartition& partition, const std::string& disc_path, const std::string& sd_path,
               std::string& error) {
    std::uint32_t index = partition.fst.find(disc_path, false);
    if (index == Fst::npos) index = partition.fst.find(disc_path, true);
    if (index == Fst::npos) {
        error = "no such disc file '" + disc_path + "'";
        return false;
    }
    const FstEntry& entry = partition.fst.entries()[index];
    if (entry.is_directory) {
        error = "'" + disc_path + "' is a directory";
        return false;
    }
    if (!make_directories(sd_path)) {
        error = "cannot create the directories for " + sd_path;
        return false;
    }
    FILE* f = std::fopen(sd_path.c_str(), "wb");
    if (!f) {
        error = "cannot create " + sd_path;
        return false;
    }
    logf("Dumping %s (%u bytes at 0x%llx) to %s\n", disc_path.c_str(), entry.size,
         static_cast<unsigned long long>(entry.offset), sd_path.c_str());
    di::PartitionSource data;
    std::vector<std::uint8_t> chunk(64 * 1024);
    std::uint64_t done = 0;
    bool ok = true;
    while (done < entry.size) {
        const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(chunk.size(), entry.size - done));
        if (!data.read(entry.offset + done, chunk.data(), n)) {
            error = "disc read failed at " + std::to_string(done);
            ok = false;
            break;
        }
        if (std::fwrite(chunk.data(), 1, n, f) != n) {
            error = "SD write failed at " + std::to_string(done);
            ok = false;
            break;
        }
        done += n;
        if ((done & 0xFFFFF) == 0) logf("  %llu MiB\n", static_cast<unsigned long long>(done >> 20));
    }
    std::fclose(f);
    if (ok) error.clear();
    return ok;
}

// Streams `length` bytes of the open partition from `offset` into a file.
bool copy_partition_range(std::uint64_t offset, std::uint64_t length, const std::string& sd_path,
                          std::string& error) {
    if (!make_directories(sd_path)) {
        error = "cannot create the directories for " + sd_path;
        return false;
    }
    FILE* f = std::fopen(sd_path.c_str(), "wb");
    if (!f) {
        error = "cannot create " + sd_path;
        return false;
    }
    di::PartitionSource data;
    std::vector<std::uint8_t> chunk(64 * 1024);
    std::uint64_t done = 0;
    bool ok = true;
    while (done < length) {
        const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(chunk.size(), length - done));
        if (!data.read(offset + done, chunk.data(), n)) {
            error = "disc read failed at " + std::to_string(done);
            ok = false;
            break;
        }
        if (std::fwrite(chunk.data(), 1, n, f) != n) {
            error = "SD write failed at " + std::to_string(done);
            ok = false;
            break;
        }
        done += n;
        if ((done & 0xFFFFF) == 0) logf("  %llu MiB\n", static_cast<unsigned long long>(done >> 20));
    }
    std::fclose(f);
    if (ok) error.clear();
    return ok;
}

bool dump_dol(const OpenedPartition& partition, const std::string& sd_path, std::string& error) {
    di::PartitionSource data;
    std::uint8_t header[kDolHeaderBytes];
    if (!data.read(partition.data_header.dol_offset, header, sizeof(header))) {
        error = "cannot read the DOL header";
        return false;
    }
    DolHeader dol;
    if (!parse_dol_header(header, sizeof(header), dol, error)) return false;
    logf("Dumping main.dol (%llu bytes at 0x%llx, entry 0x%08x) to %s\n",
         static_cast<unsigned long long>(dol.image_size()),
         static_cast<unsigned long long>(partition.data_header.dol_offset), dol.entry, sd_path.c_str());
    return copy_partition_range(partition.data_header.dol_offset, dol.image_size(), sd_path, error);
}

bool dump_metadata(const DiscProbe& probe, const OpenedPartition& partition, const std::string& sd_dir,
                   std::string& error) {
    di::SystemAreaSource system_area;
    std::vector<std::uint8_t> buffer(0x100);
    if (!system_area.read(0, buffer.data(), 0x80)) {
        error = "cannot read the disc header";
        return false;
    }
    if (!write_sd_file(sd_dir + "/header.bin", buffer.data(), 0x80, error)) return false;
    if (!system_area.read(kPartitionTableOffset, buffer.data(), 0x100)) {
        error = "cannot read the partition table";
        return false;
    }
    if (!write_sd_file(sd_dir + "/partitions.bin", buffer.data(), 0x100, error)) return false;
    if (!write_sd_file(sd_dir + "/tmd.bin", probe.tmd_bytes.data(), probe.tmd_bytes.size(), error)) return false;
    di::PartitionSource data;
    buffer.assign(static_cast<std::size_t>(kPartitionDataHeaderBytes), 0);
    if (!data.read(0, buffer.data(), buffer.size())) {
        error = "cannot read the partition data header";
        return false;
    }
    if (!write_sd_file(sd_dir + "/datahdr.bin", buffer.data(), buffer.size(), error)) return false;
    if (!write_sd_file(sd_dir + "/fst.bin", partition.fst_bytes.data(), partition.fst_bytes.size(), error)) return false;
    const std::uint64_t app_bytes = kApploaderHeaderBytes + partition.apploader.total_size();
    buffer.assign(static_cast<std::size_t>(app_bytes), 0);
    if (!data.read(kApploaderOffset, buffer.data(), buffer.size())) {
        error = "cannot read the apploader";
        return false;
    }
    if (!write_sd_file(sd_dir + "/apploader.bin", buffer.data(), buffer.size(), error)) return false;
    logf("Metadata written to %s\n", sd_dir.c_str());
    error.clear();
    return true;
}

namespace {

// The part of the boot that runs after the SD card and the log are gone.
// Returns only on failure.
bool boot_after_unmount(const DiscProbe& probe, const BootOptions& options, const SavegameOptions& savegame,
                        const RvzResidentOptions& rvz, std::uint32_t required, std::string& error) {
    bool force_ios_fields = options.preserve_current_ios;
    if (!options.preserve_current_ios) ProgressStage("Starting the game's IOS", 62);
    if (options.preserve_current_ios) {
        logf("Keeping IOS%d for the launch; reporting IOS%u to the game\n", IOS_GetVersion(), required);
    } else switch (reload_ios(static_cast<int>(required), error)) {
    case ReloadResult::Ok:
        logf("IOS%u loaded\n", required);
        break;
    case ReloadResult::AlreadyRunning:
        logf("IOS%u already running\n", required);
        break;
    case ReloadResult::NotInstalled:
        if (!options.allow_ios_fallback) return false;
        logf("Warning: %s; launching under IOS%d and reporting IOS%u to the game\n", error.c_str(),
             IOS_GetVersion(), required);
        force_ios_fields = true;
        break;
    case ReloadResult::Failed:
        return false;
    case ReloadResult::Terminal:
        return false;
    }

    if (!di::open(error)) return false;
    std::uint8_t disc_id[32];
    if (!di::read_disc_id(disc_id, error)) return false;
    publish_disc_id(disc_id);
    if (std::memcmp(disc_id, probe.disc_id, 6) != 0) {
        error = "the disc changed during the IOS reload";
        return false;
    }
    Tmd tmd;
    std::int32_t es_result = 0;
    if (!open_game_partition(probe.partition, tmd, es_result, error)) return false;
    logf("Partition open again (ES result %d)\n", es_result);
    ProgressStage("Loading the game", 70);

    // The partition's layout again, from the reloaded IOS's drive: the
    // apploader header, the data header for the DOL, and the FST in case
    // it has to be rewritten.
    OpenedPartition layout;
    if (!read_partition_layout(layout, error)) return false;
    const ApploaderHeader& apploader = layout.apploader;

    // E5: files with new sizes move into the virtual window. The FST the
    // apploader loads is patched from an override while it goes by, and
    // the same bytes are served from memory should the game read the FST
    // again; the files themselves become MEM replacements in the window.
    // E6: created files add entries, so the table is rebuilt and grows in
    // place; the partition data header the apploader reads first (its FST
    // size field) is overridden the same way.
    PayloadPieces pieces;
    pieces.mem = options.replacements;
    pieces.sd = options.sd_replacements;
    pieces.entries = options.table_entries;
    struct LoadOverride {
        std::uint64_t offset = 0;  // in the partition data
        std::vector<std::uint8_t> bytes;
        const char* what = "";
    };
    std::vector<LoadOverride> overrides;
    // The data header the apploader reads: the FST's size and the DOL's
    // offset may change below.
    PartitionDataHeader header = layout.data_header;
    bool header_changed = false;
    const bool relocates = !options.virtual_files.empty() || !options.relocations.empty();
    if (relocates) {
        if (!options.install_resident) {
            error = "relocated files need the resident runtime";
            return false;
        }
        Fst fst = layout.fst;
        std::uint64_t window_cursor = kVirtualWindowStart;
        unsigned created = 0;
        for (const FstRelocation& r : options.relocations) {
            if (r.create) {
                std::uint32_t index = 0;
                if (!fst.create_file(r.disc_path, r.offset, r.size, index, error)) return false;
                ++created;
            } else {
                std::uint32_t index = fst.find(r.disc_path, false);
                if (index == Fst::npos) index = fst.find(r.disc_path, true);
                if (index == Fst::npos || fst.entries()[index].is_directory) {
                    error = "relocation of '" + r.disc_path + "': not a disc file";
                    return false;
                }
                if (!fst.set_file_extent(index, r.offset, r.size, error)) return false;
            }
            const std::uint64_t end = r.offset + ((static_cast<std::uint64_t>(r.size) + 31) & ~std::uint64_t(31));
            if (end > window_cursor) window_cursor = end;
        }
        if (!plan_virtual_window(fst, options.virtual_files, pieces.mem, pieces.sd, pieces.disc, window_cursor,
                                 error)) {
            return false;
        }
        LoadOverride fst_override;
        fst_override.offset = layout.data_header.fst_offset;
        fst_override.what = "FST";
        if (created == 0) {
            fst_override.bytes = layout.fst_bytes;
            if (!fst.patch_image(fst_override.bytes, error)) return false;
        } else {
            // The table is rebuilt, padded to 32 bytes like the disc's, and
            // stays at its offset: the bytes after the original table must
            // be free (not the DOL, not a file the game still reads there).
            if (!fst.serialize(fst_override.bytes, error)) return false;
            fst_override.bytes.resize((fst_override.bytes.size() + 31) & ~std::size_t(31), 0);
            const std::uint64_t fst_start = layout.data_header.fst_offset;
            const std::uint64_t fst_end = fst_start + fst_override.bytes.size();
            std::uint8_t dol_header[kDolHeaderBytes];
            DolHeader dol;
            di::PartitionSource partition_data;
            if (!partition_data.read(layout.data_header.dol_offset, dol_header, sizeof(dol_header)) ||
                !parse_dol_header(dol_header, sizeof(dol_header), dol, error)) {
                error = "cannot read the DOL header: " + error;
                return false;
            }
            // A replaced DOL is placed after this, clear of the FST.
            if (options.main_dol.empty() && layout.data_header.dol_offset < fst_end &&
                layout.data_header.dol_offset + dol.image_size() > fst_start) {
                error = "the grown FST would overlap the DOL";
                return false;
            }
            for (std::uint32_t i = 0; i < fst.count(); ++i) {
                const FstEntry& e = fst.entries()[i];
                if (e.is_directory || e.size == 0 || e.offset >= kVirtualWindowStart) continue;
                if (e.offset < fst_end && e.offset + e.size > fst_start) {
                    std::string path;
                    fst.path_of(i, path);
                    error = "the grown FST would overlap '" + path + "'";
                    return false;
                }
            }
            header.fst_size = fst_override.bytes.size();
            if (header.fst_max_size < header.fst_size) header.fst_max_size = header.fst_size;
            header_changed = true;
            logf("FST: %u file(s) created, %u -> %u bytes (max %llu -> %llu)\n", created,
                 static_cast<unsigned>(layout.fst_bytes.size()), static_cast<unsigned>(fst_override.bytes.size()),
                 static_cast<unsigned long long>(layout.data_header.fst_max_size),
                 static_cast<unsigned long long>(header.fst_max_size));
        }
        overrides.push_back(std::move(fst_override));
        logf("Virtual window: %u file(s) at 0x%llx-0x%llx, FST rewritten\n",
             static_cast<unsigned>(options.virtual_files.size() + options.relocations.size()),
             static_cast<unsigned long long>(kVirtualWindowStart), static_cast<unsigned long long>(window_cursor));
    }
    // A replaced executable is read only by the apploader, from memory: in
    // the disc DOL's place when it ends before the FST, otherwise right
    // after the FST with the data header pointing there. The game never
    // reads its DOL again, so the runtime does not serve it.
    std::size_t dol_override = SIZE_MAX;
    if (!options.main_dol.empty()) {
        const std::uint64_t size = (options.main_dol.size() + 31) & ~std::uint64_t(31);
        const std::uint64_t fst_start = header.fst_offset;
        const std::uint64_t fst_end = fst_start + ((header.fst_size + 31) & ~std::uint64_t(31));
        const bool in_place = header.dol_offset + size <= fst_start || header.dol_offset >= fst_end;
        if (!in_place) {
            header.dol_offset = (fst_end + 0x7FFF) & ~std::uint64_t(0x7FFF);
            header_changed = true;
        }
        LoadOverride dol;
        dol.offset = header.dol_offset;
        dol.bytes = options.main_dol;
        dol.bytes.resize(static_cast<std::size_t>(size), 0);
        dol.what = "main.dol";
        logf("main.dol: the pack's executable (%u bytes) is loaded %s 0x%llx\n",
             static_cast<unsigned>(options.main_dol.size()), in_place ? "in place at" : "after the FST, at",
             static_cast<unsigned long long>(header.dol_offset));
        dol_override = overrides.size();
        overrides.push_back(std::move(dol));
    }
    if (header_changed) {
        LoadOverride fields;
        fields.offset = kPartitionDataFieldsOffset;
        fields.bytes.resize(kPartitionDataFieldsBytes);
        fields.what = "data header";
        if (!encode_partition_data_fields(header, fields.bytes.data(), error)) return false;
        overrides.push_back(std::move(fields));
    }
    if (relocates) {
        // Should the game read its FST or data header again, the runtime
        // serves the same bytes.
        for (std::size_t i = 0; i < overrides.size(); ++i) {
            if (i == dol_override) continue;
            MemReplacement copy;
            copy.virtual_offset = overrides[i].offset;
            copy.bytes = overrides[i].bytes;
            pieces.mem.push_back(std::move(copy));
        }
    }

    di::PartitionSource data;
    const std::uint64_t app_bytes = apploader.total_size();
    std::uint8_t* app = reinterpret_cast<std::uint8_t*>(kApploaderLoadAddress);
    if (!data.read(apploader.code_offset, app, static_cast<std::size_t>(app_bytes))) {
        error = "cannot read the apploader";
        return false;
    }
    DCFlushRange(app, static_cast<u32>((app_bytes + 31) & ~std::uint64_t(31)));
    ICInvalidateRange(app, static_cast<u32>((app_bytes + 31) & ~std::uint64_t(31)));
    logf("Apploader %s at 0x%08x, entry 0x%08x\n", apploader.date.c_str(), kApploaderLoadAddress, apploader.entry);

    ApploaderInit init = nullptr;
    ApploaderMain main = nullptr;
    ApploaderClose close = nullptr;
    reinterpret_cast<ApploaderEntry>(apploader.entry)(&init, &main, &close);
    if (!init || !main || !close) {
        error = "the apploader returned no functions";
        return false;
    }
    init(apploader_report);
    std::vector<MemoryRegion> loaded;  // what the apploader filled, in load order
    // For the progress bar: the DOL (up to the FST, when it follows) and
    // the FST are most of what the apploader reads.
    const std::uint64_t dol_span = layout.data_header.fst_offset > layout.data_header.dol_offset &&
                                           layout.data_header.fst_offset - layout.data_header.dol_offset < 0x1800000
                                       ? layout.data_header.fst_offset - layout.data_header.dol_offset
                                       : 0x600000;
    const std::uint64_t expected = dol_span + layout.data_header.fst_size;
    std::uint64_t loaded_bytes = 0;
    for (;;) {
        void* destination = nullptr;
        int length = 0;
        int word_offset = 0;
        if (main(&destination, &length, &word_offset) == 0) break;
        const std::uint32_t dest = reinterpret_cast<std::uint32_t>(destination);
        logf("  load 0x%08x <- %d bytes from word 0x%08x\n", dest, length, word_offset);
        if (length == 0) continue;  // some apploaders emit empty steps (Dolphin skips them too)
        if (length > 0) loaded_bytes += static_cast<std::uint32_t>(length);
        ProgressWithin(loaded_bytes, expected, 70, 97);
        if (length < 0 || word_offset < 0 || !in_ram(dest, static_cast<std::uint32_t>(length))) {
            error = "the apploader asked for a load outside RAM";
            return false;
        }
        const std::uint32_t len = static_cast<std::uint32_t>(length);
        const std::uint32_t woff = static_cast<std::uint32_t>(word_offset);
        if (overlaps_loader(dest, len)) {
            char where[96];
            std::snprintf(where, sizeof(where), "0x%08x-0x%08x overlaps the loader at 0x%08x-0x%08x", dest,
                          dest + len, kLoaderStart, reinterpret_cast<std::uint32_t>(SYS_GetArena1Hi()));
            error = std::string("the game's DOL section ") + where + "; relocating the loader is not implemented";
            return false;
        }
        // Only the parts of the load no override covers are read from the
        // disc. A grown FST runs past the original table into space the
        // game never uses, which a WBFS image leaves out: d2x never
        // returned from that read on hardware.
        const std::uint64_t load_start = std::uint64_t(woff) << 2;
        const std::uint64_t load_end = load_start + len;
        std::vector<std::pair<std::uint64_t, std::uint64_t>> covered;
        for (const LoadOverride& o : overrides) {
            const std::uint64_t from = std::max(load_start, o.offset);
            const std::uint64_t to = std::min(load_end, o.offset + o.bytes.size());
            if (from < to) covered.emplace_back(from, to);
        }
        std::sort(covered.begin(), covered.end());
        bool read_any = false;
        const auto read_disc = [&](std::uint64_t from, std::uint64_t to) {
            if (from >= to) return true;
            read_any = true;
            std::uint8_t* at = static_cast<std::uint8_t*>(destination) + (from - load_start);
            const std::uint32_t n = static_cast<std::uint32_t>(to - from);
            if (aligned32(at) && (n & 31) == 0 && (from & 3) == 0) {
                return di::read(at, n, static_cast<std::uint32_t>(from >> 2), error);
            }
            if (!data.read(from, at, n)) {
                error = "unaligned apploader read failed";
                return false;
            }
            return true;
        };
        std::uint64_t cursor = load_start;
        for (const auto& c : covered) {
            if (!read_disc(cursor, c.first)) return false;
            cursor = std::max(cursor, c.second);
        }
        if (!read_disc(cursor, load_end)) return false;
        if (!read_any) {
            logf("  (served from memory, nothing read from the disc)\n");
        }
        for (const LoadOverride& o : overrides) {
            // Whatever part of an override this load covers comes from the
            // rewritten copy instead.
            const std::uint64_t o_end = o.offset + o.bytes.size();
            const std::uint64_t from = std::max(load_start, o.offset);
            const std::uint64_t to = std::min(load_end, o_end);
            if (from < to) {
                std::memcpy(static_cast<std::uint8_t*>(destination) + (from - load_start),
                            o.bytes.data() + (from - o.offset), static_cast<std::size_t>(to - from));
                logf("  %s bytes 0x%llx-0x%llx replaced with the rewritten copy\n", o.what,
                     static_cast<unsigned long long>(from - o.offset), static_cast<unsigned long long>(to - o.offset));
            }
        }
        DCFlushRange(destination, len);
        ICInvalidateRange(destination, len);
        // The game's own image, for search and ocarina patches: the DOL
        // sections, not the FST nor the apploader's buffers above the
        // arena.
        if ((std::uint64_t(woff) << 2) != layout.data_header.fst_offset &&
            dest < reinterpret_cast<std::uint32_t>(SYS_GetArena1Hi())) {
            loaded.push_back(MemoryRegion{dest, len});
        }
    }
    void (*game_entry)(void) = close();
    if (!game_entry) {
        error = "the apploader returned no entry point";
        return false;
    }
    logf("Game entry 0x%08x\n", reinterpret_cast<std::uint32_t>(game_entry));


    // The DOL header for the runtime's search, read now: an RVZ game's
    // partition reads go through the SD card, which is handed on below.
    std::uint8_t dol_bytes[kDolHeaderBytes];
    const bool gc_adapter = g_extras.gc_adapter != GcAdapterMode::Off;
    if (options.install_resident || gc_adapter) {
        if (!options.main_dol.empty()) {
            std::memcpy(dol_bytes, options.main_dol.data(), sizeof(dol_bytes));  // the executable that ran
        } else if (!data.read(layout.data_header.dol_offset, dol_bytes, sizeof(dol_bytes))) {
            error = "cannot read the DOL header";
            return false;
        }
    }

    // E4: the SD card again, with our own fd this time, left open and
    // selected for the runtime.
    sdio::Card card;
    // Until the resident runtime owns this open, selected card, all error
    // exits must deselect and close it before libfat is mounted again.
    bool card_handed_to_runtime = false;
    struct CardCleanup {
        sdio::Card& card;
        bool& handed_to_runtime;
        ~CardCleanup() {
            if (!handed_to_runtime) sdio::close_card(card);
        }
    } card_cleanup{card, card_handed_to_runtime};
    const bool card_required = pieces.needs_sd() || savegame.enabled || rvz.enabled;
    if (pieces.needs_usb() && !options.install_resident) {
        error = "files on the USB drive need the resident runtime";
        return false;
    }
    bool file_device = savegame.file_device && options.install_resident;
    if (card_required || file_device) {
        if (!options.install_resident) {
            error = "SD-backed replacements, savegame redirection and RVZ games need the resident runtime";
            return false;
        }
        release_card_and_log();
        if (!sdio::open_card(card, error)) {
            if (card_required) return false;
            // Only the file device wanted it: the game starts without.
            logf("Riivolution's \"file\" device is off: the SD card: %s\n", error.c_str());
            error.clear();
            file_device = false;
            sdio::close_card(card);  // a half-opened card is not handed on
            card = sdio::Card{};
        }
    }
    if (card.fd >= 0) {
        logf("SD card: fd %d, rca 0x%04x, %s\n", card.fd, card.rca,
             card.d2x ? "through d2x's /dev/sdio/sdhc (the game is on this card)" : card.sdhc ? "SDHC" : "SDSC");
        if (options.verify_sd) {
            for (const SdReplacement& r : pieces.sd) {
                if (!r.runs.empty() && r.runs[0].kind != RT_KIND_SD) continue;  // on the USB drive
                if (!verify_sd_replacement(card, r, error)) return false;
            }
        }
    }

    // E2: the DOL is in place, so the runtime can find and hook the game's
    // IPC entry points before anything runs them.
    ResidentInstall resident;
    DolHeader dol;
    if ((options.install_resident || gc_adapter) && !parse_dol_header(dol_bytes, sizeof(dol_bytes), dol, error)) {
        return false;
    }
    // The runtime's code goes above this loader (which ends at arena 1's
    // top) and the apploader image, both still in use until the game starts.
    const std::uint32_t mem1_floor =
        std::max(reinterpret_cast<std::uint32_t>(SYS_GetArena1Hi()),
                 static_cast<std::uint32_t>(kApploaderLoadAddress + ((app_bytes + 31) & ~31ull)));
    if (options.install_resident) {
        ResidentOptions ro;
        ro.gecko = options.resident_gecko;
        ro.pieces = std::move(pieces);
        ro.table_tag = static_cast<std::uint64_t>(probe.partition.offset);
        ro.virtual_start_words = relocates ? static_cast<std::uint32_t>(kVirtualWindowStart >> 2) : 0;
        ro.sdio_fd = card.fd;
        ro.sdio_sdhc = card.sdhc;
        ro.sdio_d2x = card.d2x;
        if (ro.pieces.needs_usb()) {
            // Opened when the packs were compiled, under this same IOS.
            if (!ums::Open(error)) return false;
            ro.usb_fd = ums::Fd();
            logf("USB drive: d2x's /dev/usb2, fd %d, for the packs on it\n", ro.usb_fd);
        }
        ro.savegame = savegame;
        ro.savegame.file_device = file_device && card.fd >= 0;
        ro.rvz = rvz;
        ro.mem1_floor = mem1_floor;
        if (!install_resident(dol, ro, resident, error)) return false;
    }
    // The GameCube adapter: below the runtime, or on its own.
    PadHook pad;
    if (gc_adapter) {
        std::string why;
        const std::uint32_t arena1_hi = options.install_resident ? resident.new_arena1_hi : game_arena1_hi();
        const std::uint32_t arena2_lo = options.install_resident ? resident.new_arena2_lo : read32(0x80003124);
        if (!plan_pad_hook(dol, arena1_hi, mem1_floor, arena2_lo,
                           options.install_resident ? resident.ioctl_async_original : 0,
                           options.install_resident ? resident.ioctlv_async_original : 0, options.memory_patches,
                           g_extras.gc_adapter == GcAdapterMode::Demo, pad, why)) {
            logf("GameCube adapter: off: %s\n", why.c_str());
        }
    }
    logf("Handing over\n");

    // Low-memory globals the SDK expects from the System Menu (wiibrew
    // memory map; Dolphin's Boot_BS2Emu and Brainslug write the same set).
    // The apploader has just stored the FST fields (0x38/0x3C) and the IOS
    // it expects (0x3188) through the cache; these go the same way.
    std::memcpy(reinterpret_cast<void*>(kMem1Start), probe.disc_id, 32);
    store32(0x80000020, 0x0D15EA5E);            // boot magic
    store32(0x80000024, 1);                     // version
    store32(0x80000028, 0x01800000);            // MEM1 size
    if (!running_in_dolphin()) store32(0x8000002C, 1 + (read32(0xCC00302C) >> 28));  // console type
    store32(0x800000EC, 0x81800000);            // debug monitor location
    store32(0x800000F0, 0x01800000);            // simulated memory size
    store32(0x800000F8, 0x0E7BE2C0);            // bus clock
    store32(0x800000FC, 0x2B73A840);            // CPU clock
    // MEM1 arena end (0x34 from the apploader, 0x3110 as the System Menu
    // sets it): the FST, or the runtime's code just below it.
    const std::uint32_t arena1_end = pad.active                 ? pad.new_arena1_hi
                                     : options.install_resident ? resident.new_arena1_hi
                                                                : load32(0x80000038);
    if (options.install_resident || pad.active) store32(0x80000034, arena1_end);
    store32(0x80003110, arena1_end);
    std::memcpy(reinterpret_cast<void*>(0x80003180), probe.disc_id, 4);
    store32(0x80003184, 0x80000000);            // where the game id lives
    store32(0x80003194, probe.partition.type);
    store32(0x80003198, static_cast<u32>(probe.partition.offset >> 2));
    ProgressStage("Starting the game", 100);
    configure_video_for_game(probe.header.game_id.size() > 3 ? probe.header.game_id[3] : 'E');
    DCFlushRange(reinterpret_cast<void*>(kMem1Start), 0x3400);
    if (force_ios_fields) {
        // Claim to be the IOS the apploader asked for (0x3188), as Brainslug
        // does. Uncached and after the flush: 0x3140 is IOS's own field and
        // never goes through this CPU's cache.
        u32 pretended = read32(0x80003188);
        if ((pretended >> 16) != required) pretended = (required << 16) | (read32(0x80003140) & 0xFFFF);
        write32(0x80003140, pretended);
        write32(0x80003188, pretended);
    }
    if (pad.active) {
        // IOS's own field, like 0x3140: uncached, after the flush. The end
        // (0x3128) is never moved: the top of MEM2 stays the game's.
        write32(0x80003124, pad.new_arena2_lo);
    } else if (options.install_resident && resident.new_arena2_lo != resident.old_arena2_lo) {
        write32(0x80003124, resident.new_arena2_lo);
    }

    // <memory> patches, last of all so they win over the globals above (as
    // in Dolphin, which writes low memory before its patches). Writes may
    // land anywhere in MEM1 except this loader, the runtime's code and its
    // hook stub, and in MEM2 outside the runtime's data and its staging
    // area (the staged bytes are copied down after these patches).
    if (!options.memory_patches.empty()) {
        struct WiiMemory final : MemoryAccess {
            bool read(std::uint32_t address, std::uint8_t* out, std::size_t length) override {
                std::memcpy(out, reinterpret_cast<const void*>(address), length);
                return true;
            }
            bool write(std::uint32_t address, const std::uint8_t* bytes, std::size_t length) override {
                std::memcpy(reinterpret_cast<void*>(address), bytes, length);
                const std::uint32_t start = address & ~31u;
                const std::uint32_t end = (address + static_cast<std::uint32_t>(length) + 31) & ~31u;
                DCFlushRange(reinterpret_cast<void*>(start), end - start);
                ICInvalidateRange(reinterpret_cast<void*>(start), end - start);
                return true;
            }
        } wii_memory;
        const std::uint32_t loader_end = reinterpret_cast<std::uint32_t>(SYS_GetArena1Hi());
        std::vector<MemoryRegion> writable;
        writable.push_back(MemoryRegion{kMem1Start, kLoaderStart - kMem1Start});
        if (options.install_resident && resident.code_base >= loader_end) {
            writable.push_back(MemoryRegion{loader_end, resident.code_base - loader_end});
            const std::uint32_t code_end = resident.code_base + resident.code_bytes;
            writable.push_back(MemoryRegion{code_end, kMem1End - code_end});
        } else {
            writable.push_back(MemoryRegion{loader_end, kMem1End - loader_end});
        }
        std::vector<MemoryRegion> exclusions;
        if (options.install_resident && resident.data_bytes != 0) {
            writable.push_back(MemoryRegion{kMem2Start, kMem2End - kMem2Start});
            exclusions.push_back(MemoryRegion{resident.data_base, resident.data_bytes});
            exclusions.push_back(MemoryRegion{resident.stage_base, kMem2End - resident.stage_base});
        } else {
            const std::uint32_t arena2_end = reinterpret_cast<std::uint32_t>(SYS_GetArena2Hi());
            writable.push_back(MemoryRegion{kMem2Start, arena2_end > kMem2Start ? arena2_end - kMem2Start : 0});
        }
        if (pad.active) {
            exclusions.push_back(MemoryRegion{pad.code_base, pad.code_bytes});
            exclusions.push_back(MemoryRegion{pad.state_base, pad.state_bytes});
        }
        if (options.install_resident) {
            exclusions.reserve(exclusions.size() + resident.hook_site_count);
            for (unsigned i = 0; i < resident.hook_site_count; ++i) {
                exclusions.push_back(MemoryRegion{resident.hook_sites[i], kHookStubBytes});
            }
        }
        writable = subtract_memory_regions(writable, exclusions);
        std::vector<std::string> notes;
        if (!apply_memory_patches(options.memory_patches, loaded, writable, wii_memory, notes, error)) return false;
        for (const std::string& n : notes) logf("  %s\n", n.c_str());
    }
    // The menu's extras, over the game as loaded and patched.
    apply_video(loaded, card.fd < 0);  // not while the runtime's card handle is open
    if (!g_extras.cheat_gct.empty()) {
        std::string why;
        if (!install_cheats(loaded, options.memory_patches, why)) logf("Cheats are off: %s\n", why.c_str());
    }
    if (pad.active) {
        std::string why;
        if (!install_pad_hook(pad, why)) logf("GameCube adapter: off: %s\n", why.c_str());
    }
    // Last, as USB Loader GX does: Wiimmfi's Mario Kart Wii patch goes
    // below everything else in the MEM1 arena. Packs bring their own online
    // setup (and may have replaced the code these patches expect).
    if (g_extras.server != WfcServer::Off) {
        const bool packs = !options.memory_patches.empty() || !options.virtual_files.empty() ||
                           !options.replacements.empty() || !options.sd_replacements.empty();
        if (packs) logf("WFC: not patched; packs are on\n");
        else ApplyWfc(loaded, g_extras.server, g_extras.wfc_domain, g_extras.game_id, probe.header.version);
    }
    settime(secs_to_ticks(static_cast<u64>(std::time(nullptr)) - kWiiEpochOffset));

    release_card_and_log();
    // The resident starts with the game; from here it needs the selected
    // raw-card fd for SD-backed reads and savegame writes.
    card_handed_to_runtime = true;
    SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);
    if (options.install_resident && resident.data_bytes != 0) {
        // The runtime's data to the bottom of the MEM2 arena, over what was
        // this loader's own memory (nothing below needs it any more).
        std::memcpy(reinterpret_cast<void*>(resident.data_base), reinterpret_cast<const void*>(resident.stage_base),
                    resident.data_bytes);
        DCFlushRange(reinterpret_cast<void*>(resident.data_base), resident.data_bytes);
    }
    game_entry();
    // A game entry must never return, but release the card if it does.
    card_handed_to_runtime = false;
    sdio::close_card(card);
    error = "the game entry point returned";
    return false;
}

}  // namespace

// The savegame folder: created when missing, then located on the card
// for the runtime's FAT engine. libfat writes back lazily, so the card is
// unmounted (which flushes) and mounted again before the raw reads, the
// log closed and reopened around that.
bool prepare_savegame(const DiscProbe& probe, const BootOptions& options, SavegameOptions& out, std::string& error) {
    if (!options.install_resident) {
        error = "savegame redirection needs the resident runtime";
        return false;
    }
    // The data directory is named after the title id in the TMD, both
    // halves: disc titles are type 00010000, but one with a channel (Mario
    // Kart Wii, 00010004-524d4345) keeps its save under that type.
    if (probe.tmd.title_id == 0) {
        error = "savegame redirection: the TMD has no title id";
        return false;
    }
    char prefix[64];
    std::snprintf(prefix, sizeof(prefix), "/title/%08x/%08x/data",
                  static_cast<unsigned>(probe.tmd.title_id >> 32), static_cast<unsigned>(probe.tmd.title_id & 0xFFFFFFFFu));
    struct stat existing;
    const bool existed = stat(options.savegame_dir.c_str(), &existing) == 0 && S_ISDIR(existing.st_mode);
    if (!existed && !make_directories(options.savegame_dir + "/.")) {
        error = "cannot create the save folder " + options.savegame_dir;
        return false;
    }
    // The clone marker: a hidden file inside the folder, created when a
    // clone is decided (a new folder, or a marker left by a clone that
    // did not run to its end) and deleted by the runtime when the clone
    // is complete. The game never sees hidden entries (rtfat skips
    // them). Without clone, a stale marker goes.
    const std::string marker = options.savegame_dir + "/riftwii.cln";
    const bool marked = stat(marker.c_str(), &existing) == 0 && !S_ISDIR(existing.st_mode);
    out.clone = options.savegame_clone && (!existed || marked);
    if (out.clone) {
        if (!marked) {
            FILE* f = std::fopen(marker.c_str(), "wb");
            if (f == nullptr) {
                error = "cannot create the clone marker " + marker;
                return false;
            }
            std::fclose(f);
        }
        // A previously-created marker may survive a failed FAT_setAttr.
        // Reassert and read it back every time a clone is pending: a visible
        // marker would otherwise leak into the game's save directory.
        if (FAT_setAttr(marker.c_str(), ATTR_HIDDEN | ATTR_ARCHIVE) != 0) {
            error = "cannot hide the clone marker " + marker;
            return false;
        }
        const int attributes = FAT_getAttr(marker.c_str());
        if (attributes < 0 ||
            (static_cast<unsigned>(attributes) & (ATTR_HIDDEN | ATTR_ARCHIVE)) != (ATTR_HIDDEN | ATTR_ARCHIVE)) {
            error = "cannot verify the hidden clone marker " + marker;
            return false;
        }
    } else if (!out.clone && marked && unlink(marker.c_str()) != 0) {
        error = "cannot remove the stale clone marker " + marker;
        return false;
    }
    LogClose();
    fatUnmount("sd:");
    const bool mounted = fatMountSimple("sd", sd_interface());
    LogReopen();
    if (!mounted) {
        error = "cannot mount the SD card again after creating the save folder";
        return false;
    }
    if (!resolve_sd_directory(options.savegame_dir, out.volume, error)) return false;
    out.prefix = prefix;
    out.enabled = true;
    logf("Savegame: %s served from %s%s\n", prefix, options.savegame_dir.c_str(),
         out.clone ? (existed ? " (marked folder: the NAND save is cloned in again)" : " (new folder: the NAND save is cloned in)")
                   : existed ? " (existing folder)" : " (new folder)");
    return true;
}

void SetLaunchExtras(LaunchExtras extras) { g_extras = std::move(extras); }

bool boot_game(const DiscProbe& probe, const BootOptions& options, std::string& error) {
    const std::uint32_t required = probe.tmd.required_ios();
    if (required == 0) {
        error = "the TMD does not name an IOS";
        return false;
    }
    logf("Booting %s with IOS%u\n", probe.header.game_id.c_str(), required);
    BootOptions effective = options;
    const int running_ios = IOS_GetVersion();
    if (!effective.preserve_current_ios && running_ios != static_cast<int>(required) &&
        needs_resident_sd(effective)) {
        // The selected packages/save mode were resolved through this very
        // card under the running IOS, so it is a proven-good SD driver for
        // the handoff. Keep it and report the title's requested IOS in low
        // memory, as the existing USB-image path already does.
        effective.preserve_current_ios = true;
        logf("Keeping IOS%d for resident SD access; reporting IOS%u to the game\n", running_ios, required);
    }
    if (!effective.preserve_current_ios && running_ios != static_cast<int>(required) &&
        g_extras.gc_adapter != GcAdapterMode::Off && usb_hid_present()) {
        // The adapter needs /dev/usb/hid, which the IOS a game asks for
        // often lacks (IOS36 has none); the running one has it.
        effective.preserve_current_ios = true;
        logf("Keeping IOS%d for the GameCube adapter; reporting IOS%u to the game\n", running_ios, required);
    }
    RvzResidentOptions rvz;
    if (di::has_partition_resolver()) {
        // An RVZ game: the apploader's reads come from the card, which an
        // IOS reload would take away, and the game's from the runtime.
        if (!rvz_resident_options(rvz, error)) return false;
        if (!effective.install_resident) {
            effective.install_resident = true;
            logf("RVZ game: the resident runtime serves its reads\n");
        }
        if (!effective.preserve_current_ios) {
            effective.preserve_current_ios = true;
            logf("Keeping IOS%d for the RVZ on the SD card; reporting IOS%u to the game\n", running_ios, required);
        }
    }
    SavegameOptions savegame;
    ProgressStage(effective.savegame_dir.empty() ? "Getting the game ready" : "Preparing the save", 60);
    if (!effective.savegame_dir.empty() && !prepare_savegame(probe, effective, savegame, error)) return false;
    if (effective.install_resident && effective.file_device) {
        // The save redirect's volume carries the root too; without one,
        // the root's own.
        std::string why;
        if (savegame.enabled || resolve_sd_directory("sd:/", savegame.volume, why)) {
            savegame.file_device = true;
        } else {
            logf("Riivolution's \"file\" device is off: %s\n", why.c_str());
        }
    }

    // Everything IOS holds for us dies with a reload: the Wii Remote stack
    // (which also saves its pairings to NAND on shutdown, so it must go
    // while IPC is still alive), the log, DI and the SD card. With the
    // running IOS kept, the card and the log stay (release_card_and_log).
    release_wii_remotes();
    std::string ignored;
    di::close_partition(ignored);
    di::close();
    if (effective.preserve_current_ios) {
        g_card_live_for_log = true;
    } else {
        LogClose();
        fatUnmount("sd:");
        sd_interface()->shutdown();
    }

    boot_after_unmount(probe, effective, savegame, rvz, required, error);  // returns only on failure
    if (reload_terminal_failure()) return false;
    if (g_card_live_for_log) {
        g_card_live_for_log = false;  // failed before the card was released: it and the log are still up
    } else if (effective.preserve_current_ios) {
        // The preserved IOS may own a USB virtual disc. Reacquiring all
        // default devices could interfere with it, so recover only SD/log.
        if (fatMountSimple("sd", sd_interface())) {
            LogReopen();
        } else {
            error += "; additionally could not remount SD after USB boot failure";
        }
    } else {
        fatInitDefault();  // give the caller its card back so it can log this
    }
    logf("Boot failed: %s\n", error.c_str());
    return false;
}

}  // namespace riftwii::wii
