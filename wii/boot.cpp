// SPDX-License-Identifier: GPL-3.0-or-later
#include "boot.hpp"

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

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <malloc.h>

#include "di.hpp"
#include "ios_reload.hpp"
#include "log.hpp"
#include "resident.hpp"

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

// Low memory is written through the data cache, like the apploader's own
// stores and libogc's, and flushed once before the jump. libogc's write32
// bypasses the cache; mixing it with dirty cache lines would let the flush
// overwrite it with stale data (Dolphin has no cache, so it would not show
// there).
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

// Picks the VI mode the game will find configured and records it in the
// low-memory global the SDK reads (0x800000CC).
void configure_video_for_game(char region) {
    const bool progressive = CONF_GetProgressiveScan() > 0 && VIDEO_HaveComponentCable();
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

bool probe_disc(DiscProbe& out, std::string& error) {
    if (!di::open(error)) return false;
    bool inserted = false;
    if (!di::cover_status(inserted, error)) return false;
    if (!inserted) {
        logf("No disc: waiting for the cover to close...\n");
        if (!di::wait_for_cover_close(error)) return false;
    }
    if (!di::reset(true, error)) return false;
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
bool boot_after_unmount(const DiscProbe& probe, const BootOptions& options, std::uint32_t required,
                        std::string& error) {
    bool force_ios_fields = false;
    switch (reload_ios(static_cast<int>(required), error)) {
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

    di::PartitionSource data;
    ApploaderHeader apploader;
    if (!read_apploader_header(data, apploader, error)) return false;
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
    for (;;) {
        void* destination = nullptr;
        int length = 0;
        int word_offset = 0;
        if (main(&destination, &length, &word_offset) == 0) break;
        const std::uint32_t dest = reinterpret_cast<std::uint32_t>(destination);
        logf("  load 0x%08x <- %d bytes from word 0x%08x\n", dest, length, word_offset);
        if (length == 0) continue;  // some apploaders emit empty steps (Dolphin skips them too)
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
        bool ok;
        if (aligned32(destination) && (len & 31) == 0) {
            ok = di::read(destination, len, woff, error);
        } else {
            ok = data.read(std::uint64_t(woff) << 2, static_cast<std::uint8_t*>(destination), len);
            if (!ok) error = "unaligned apploader read failed";
        }
        if (!ok) return false;
        DCFlushRange(destination, len);
        ICInvalidateRange(destination, len);
    }
    void (*game_entry)(void) = close();
    if (!game_entry) {
        error = "the apploader returned no entry point";
        return false;
    }
    logf("Game entry 0x%08x\n", reinterpret_cast<std::uint32_t>(game_entry));

    // E2: the DOL is in place, so the runtime can find and hook the game's
    // IPC entry points before anything runs them.
    ResidentInstall resident;
    if (options.install_resident) {
        PartitionDataHeader data_header;
        if (!read_partition_data_header(data, data_header, error)) return false;
        std::uint8_t dol_bytes[kDolHeaderBytes];
        if (!data.read(data_header.dol_offset, dol_bytes, sizeof(dol_bytes))) {
            error = "cannot read the DOL header";
            return false;
        }
        DolHeader dol;
        if (!parse_dol_header(dol_bytes, sizeof(dol_bytes), dol, error)) return false;
        ResidentOptions ro;
        ro.gecko = options.resident_gecko;
        ro.replacements = options.replacements;
        ro.table_tag = static_cast<std::uint64_t>(probe.partition.offset);
        if (!install_resident(dol, ro, resident, error)) return false;
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
    store32(0x80003110, load32(0x80000038));    // FST address, second copy
    std::memcpy(reinterpret_cast<void*>(0x80003180), probe.disc_id, 4);
    store32(0x80003184, 0x80000000);            // where the game id lives
    store32(0x80003194, probe.partition.type);
    store32(0x80003198, static_cast<u32>(probe.partition.offset >> 2));
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
    if (options.install_resident) {
        // IOS's own field, like 0x3140: uncached, after the flush.
        write32(0x80003128, resident.new_arena_end);
    }
    settime(secs_to_ticks(static_cast<u64>(std::time(nullptr)) - kWiiEpochOffset));

    SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);
    game_entry();
    error = "the game entry point returned";
    return false;
}

}  // namespace

bool boot_game(const DiscProbe& probe, const BootOptions& options, std::string& error) {
    const std::uint32_t required = probe.tmd.required_ios();
    if (required == 0) {
        error = "the TMD does not name an IOS";
        return false;
    }
    logf("Booting %s with IOS%u\n", probe.header.game_id.c_str(), required);

    // Everything IOS holds for us dies with the reload: the Wii Remote
    // stack (which also saves its pairings to NAND on shutdown, so it must
    // go while IPC is still alive), the log, DI and the SD card.
    WPAD_Shutdown();
    LogClose();
    std::string ignored;
    di::close_partition(ignored);
    di::close();
    fatUnmount("sd:");
    __io_wiisd.shutdown();

    boot_after_unmount(probe, options, required, error);  // returns only on failure
    logf("Boot failed: %s\n", error.c_str());
    fatInitDefault();  // give the caller its card back so it can log this
    return false;
}

}  // namespace riftwii::wii
