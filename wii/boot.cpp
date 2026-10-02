// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2014 Alex Chadwick (Brainslug) <https://github.com/Chadderz121/brainslug-wii>
// SPDX-FileCopyrightText: 2020 Florian Bach (Brainslug)
// SPDX-FileCopyrightText: USB Loader GX contributors <https://github.com/wiidev/usbloadergx>
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
#include "menuios.hpp"
#include "log.hpp"
#include "memlimits.hpp"
#include "riftwii/mempatch.hpp"
#include "padhook.hpp"
#include "resident.hpp"
#include "faulthook.hpp"
#include "shothook.hpp"
#include "vsdhook.hpp"
#include "sdfile.hpp"
#include "sdio.hpp"
#include "umsdev.hpp"
#include "usbcatalog.hpp"
#include "wfc.hpp"
#include "codehandleronly_bin.h"
#include "riftwii/cardlog.hpp"
#include "riftwii/codehook.hpp"
#include "riftwii/gamelang.hpp"
#include "riftwii/gxpatches.hpp"
#include "riftwii/playhistory.hpp"
#include "riftwii/returnto.hpp"
#include "channel.hpp"
#include "loadersettings.hpp"
#include "riftwii/symsearch.hpp"
#include "dolboot.h"

namespace riftwii::wii {
namespace {

using ApploaderReport = void (*)(const char* format, ...);
using ApploaderInit = void (*)(ApploaderReport report);
using ApploaderMain = int (*)(void** destination, int* length, int* word_offset);
using ApploaderClose = void (*(*)(void))(void);
using ApploaderEntry = void (*)(ApploaderInit* init, ApploaderMain* main, ApploaderClose* close);

constexpr std::uint32_t kApploaderLoadAddress = 0x81200000;
constexpr std::uint32_t kLoaderStart = 0x80A00000;  // Makefile.wii: --section-start,.init
constexpr std::uint32_t kGameStart = 0x80004000;    // where games' executables start
constexpr std::uint32_t kCodeVeneers = 0x80002300;  // past the code handler (0x800022B0), before 0x80003000
constexpr std::uint32_t kFaultVeneers = 0x80002F00;  // the crash blob's two, after the runtime's and the card's
constexpr std::uint32_t kMem1Start = 0x80000000;
constexpr std::uint32_t kMem1End = 0x81800000;
constexpr std::uint32_t kMem2Start = 0x90000000;
constexpr std::uint32_t kMem2End = 0x94000000;
constexpr std::uint32_t kMaxFstBytes = 8 * 1024 * 1024;
constexpr std::uint64_t kWiiEpochOffset = 946684800;  // 2000-01-01 in Unix seconds

// Test switches for a game that only fails on a console: settings.txt
// "debug_off = bca, fault, dolswitch, 480p, returnto, consoletype, prerun"
// turns those extras off one by one (PMEX Remix's SD files fail under
// RiftWii on a Wii but not under USB Loader GX with the same cIOS).
bool debug_off(const char* what) {
    const auto it = Settings().other.find("debug_off");
    if (it == Settings().other.end()) return false;
    const std::string& list = it->second;
    const std::size_t n = std::strlen(what);
    for (std::size_t at = 0; (at = list.find(what, at)) != std::string::npos; at += n) {
        const bool start = at == 0 || list[at - 1] == ',' || list[at - 1] == ' ';
        const bool end = at + n == list.size() || list[at + n] == ',' || list[at + n] == ' ';
        if (start && end) return true;
    }
    return false;
}

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
// A code build's virtual SD card (wii/vsdhook.hpp), found before the
// card is unmounted, served in-game when the image is usable.
VsdImage g_vsd;

extern "C" const std::uint8_t riftwii_dolswitch_stub[];
extern "C" const std::uint8_t riftwii_dolswitch_stub_end[];

// A game that starts another executable: wii/dolswitch_stub.S hooks the
// code handler into it. Logged; the first executable's cheats work
// without it.
void install_dol_switch(const std::vector<CodeRange>& text, const std::uint32_t* pattern) {
    const std::vector<std::uint32_t> jumps = find_dol_jumps(text);
    if (jumps.empty()) return;
    const std::size_t size = static_cast<std::size_t>(riftwii_dolswitch_stub_end - riftwii_dolswitch_stub);
    if (size > kDolSwitchStubEnd - kDolSwitchStub) {
        logf("Codes: the next executable's hook is too big (%u bytes); its cheats stay off\n",
             static_cast<unsigned>(size));
        return;
    }
    std::uint8_t* stub = reinterpret_cast<std::uint8_t*>(kDolSwitchStub);
    std::memcpy(stub, riftwii_dolswitch_stub, size);
    std::uint32_t* words = reinterpret_cast<std::uint32_t*>(stub);
    words[1] = kCodeHandlerEntry;
    for (int i = 0; i < 4; ++i) words[2 + i] = pattern[i];
    words[6] = 0x80004000;
    words[7] = kMem1End - 16;
    DCFlushRange(stub, size);
    ICInvalidateRange(stub, size);
    for (std::uint32_t at : jumps) {
        const std::uint32_t branch = encode_b(at, kDolSwitchStub);
        if (branch == 0) continue;
        *reinterpret_cast<volatile std::uint32_t*>(at) = branch;
        DCFlushRange(reinterpret_cast<void*>(at & ~31u), 32);
        ICInvalidateRange(reinterpret_cast<void*>(at & ~31u), 32);
        logf("Codes: the jump to another executable at 0x%08x goes through 0x%08x, which hooks it too\n",
             static_cast<unsigned>(at), kDolSwitchStub);
    }
}

// The Gecko code handler and the cheats' GCT (vendor-gecko/), called at
// the end of the game's video retrace handler. Leaves the game untouched
// and says why when that cannot be done.
bool install_cheats(const std::vector<MemoryRegion>& loaded, const std::vector<MemoryPatch>& patches,
                    const std::vector<MemoryRegion>& keep_out, std::string& why) {
    const std::vector<std::uint8_t>& gct = g_extras.cheat_gct;
    // A code build's gameconfig.txt may move the list out of the handler's
    // own room, to memory the game leaves free.
    const bool moved = g_extras.code_list_start != 0;
    const std::uint32_t list = moved ? g_extras.code_list_start : kCodeListAddress;
    const std::uint32_t list_end = moved ? g_extras.code_list_end : kCodeListEnd;
    const auto overlap = [](std::uint32_t a, std::uint32_t a_end, std::uint32_t b, std::uint32_t b_end) {
        return a < b_end && b < a_end;
    };
    char where[96];
    std::snprintf(where, sizeof(where), "the code list (0x%08x-0x%08x)", static_cast<unsigned>(list),
                  static_cast<unsigned>(list_end));
    if (moved) {
        if (list < kCodeListEnd || list_end <= list || list_end > kLoaderStart) {
            why = std::string(where) + " is outside the memory a game can give it";
            return false;
        }
        for (const MemoryRegion& r : loaded) {
            if (overlap(list, list_end, r.address, r.address + r.length)) {
                why = std::string(where) + " is over the game's own code or data";
                return false;
            }
        }
        for (const MemoryRegion& r : keep_out) {
            if (r.length != 0 && overlap(list, list_end, r.address, r.address + r.length)) {
                why = std::string(where) + " is over RiftWii's own code for this launch";
                return false;
            }
        }
    }
    if (gct.size() > list_end - list) {
        why = "the codes need " + std::to_string(gct.size()) + " bytes and " + where + " holds " +
              std::to_string(list_end - list) + "; pick fewer cheats";
        return false;
    }
    for (const MemoryPatch& p : patches) {
        if (!p.has_offset) continue;
        const std::uint32_t end = p.offset + static_cast<std::uint32_t>(p.value.size());
        if (overlap(p.offset, end, kCodeHandlerAddress, kCodeListEnd) || (moved && overlap(p.offset, end, list, list_end))) {
            why = "a pack's memory patch uses the code handler's memory or " + std::string(where);
            return false;
        }
    }
    std::vector<CodeRange> text;
    for (const MemoryRegion& r : loaded) {
        text.push_back(CodeRange{r.address, reinterpret_cast<const std::uint8_t*>(r.address), r.length});
    }
    const bool audio = g_extras.code_hooktype == 7;
    const std::uint32_t hook = find_code_hook(text, audio ? CodeHook::AudioFrame : CodeHook::Retrace);
    const std::uint32_t branch = hook != 0 ? encode_b(hook, kCodeHandlerEntry) : 0;
    if (branch == 0) {
        why = audio ? "the game's AXNextFrame (gameconfig.txt's hook type 7) was not found, so there is nowhere to run them from"
                    : "the game's video retrace handler was not found, so there is nowhere to run them from";
        return false;
    }
    std::uint8_t* handler = reinterpret_cast<std::uint8_t*>(kCodeHandlerAddress);
    std::memcpy(handler, codehandleronly_bin, codehandleronly_bin_size);
    if (moved && !relocate_code_list(handler, codehandleronly_bin_size, list)) {
        why = "the code handler cannot be pointed at " + std::string(where);
        return false;
    }
    std::memcpy(handler, g_extras.game_id.c_str(),
                std::min<std::size_t>(6, g_extras.game_id.size()));  // where cheat tools look for it
    std::memset(reinterpret_cast<void*>(list), 0, list_end - list);
    std::memcpy(reinterpret_cast<void*>(list), gct.data(), gct.size());
    DCFlushRange(reinterpret_cast<void*>(kCodeHandlerAddress), kCodeListEnd - kCodeHandlerAddress);
    ICInvalidateRange(reinterpret_cast<void*>(kCodeHandlerAddress), kCodeListEnd - kCodeHandlerAddress);
    if (moved) {
        DCFlushRange(reinterpret_cast<void*>(list & ~31u), ((list_end + 31) & ~31u) - (list & ~31u));
        ICInvalidateRange(reinterpret_cast<void*>(list & ~31u), ((list_end + 31) & ~31u) - (list & ~31u));
    }
    *reinterpret_cast<volatile std::uint32_t*>(hook) = branch;
    DCFlushRange(reinterpret_cast<void*>(hook & ~31u), 32);
    ICInvalidateRange(reinterpret_cast<void*>(hook & ~31u), 32);
    if (!debug_off("dolswitch")) install_dol_switch(text, code_hook_pattern(audio ? CodeHook::AudioFrame : CodeHook::Retrace));
    else logf("Codes: debug_off: no hook on jumps to another executable\n");
    logf("Codes: %u cheat(s)%s%s, %u bytes at 0x%08x, handler at 0x%08x called from %s 0x%08x\n",
         static_cast<unsigned>(g_extras.cheat_count), g_extras.code_builds.empty() ? "" : " and ",
         g_extras.code_builds.c_str(), static_cast<unsigned>(gct.size()), static_cast<unsigned>(list),
         kCodeHandlerAddress, audio ? "AXNextFrame" : "the retrace handler", hook);
    // gameconfig.txt's words, last: they may turn off what would undo the
    // codes (a Project+ build stops the game clearing the memory its list is in).
    for (const GamePoke& p : g_extras.pokes) {
        if (p.address < kMem1Start || p.address + 4 > kLoaderStart || (p.address & 3) != 0) {
            logf("  poke 0x%08x skipped: outside the game's memory\n", static_cast<unsigned>(p.address));
            continue;
        }
        if (p.conditional) {
            if (p.check_address < kMem1Start || p.check_address + 4 > kMem1End || (p.check_address & 3) != 0 ||
                *reinterpret_cast<volatile std::uint32_t*>(p.check_address) != p.check_value) {
                continue;
            }
        }
        *reinterpret_cast<volatile std::uint32_t*>(p.address) = p.value;
        DCFlushRange(reinterpret_cast<void*>(p.address & ~31u), 32);
        ICInvalidateRange(reinterpret_cast<void*>(p.address & ~31u), 32);
        logf("  poke 0x%08x = 0x%08x\n", static_cast<unsigned>(p.address), static_cast<unsigned>(p.value));
    }
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
// `progressive_ok`: 480p is on in the Wii's settings and a component
// cable is in; `component`: the cable alone.
VideoTarget resolve_video_target(VideoMode mode, char region, bool progressive_ok, bool component) {
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
        // Only through a component cable: on the AV cable a 480p signal is
        // a green or scrambled screen, so the same 60 Hz mode interlaced.
        t.format = region_video_standard(region) == VI_PAL ? kViEurgb60 : kViNtsc;
        t.progressive = component;
        break;
    default: break;
    }
    return t;
}

// Picks the VI mode the game will find configured and records it in the
// low-memory global the SDK reads (0x800000CC). A chosen video mode
// decides it, and becomes the target the game's tables are converted to.
void configure_video_for_game(char region) {
    const bool component = VIDEO_HaveComponentCable();
    const bool progressive_ok = CONF_GetProgressiveScan() > 0 && component;
    g_extras.video.target = resolve_video_target(g_extras.video.mode, region, progressive_ok, component);
    const VideoTarget& target = g_extras.video.target;
    if (g_extras.video.mode == VideoMode::Progressive && !component)
        logf("Video: 480p needs a component cable and none is in; 480i instead\n");
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
    if (options.header_only) {
        error.clear();
        return true;
    }
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

// Return to RiftWii, below with the play log.
u64 return_title();
void apply_return_to(const std::vector<MemoryRegion>& loaded, u64 title, std::uint32_t stub);

namespace {

// The part of the boot that runs after the SD card and the log are gone.
// Returns only on failure.
bool boot_after_unmount(const DiscProbe& probe, BootOptions& options, const SavegameOptions& savegame,
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
    // Moved, not copied: boot_game's options are this launch's own.
    const bool file_replacements = !options.replacements.empty() || !options.sd_replacements.empty();
    PayloadPieces pieces;
    pieces.mem = std::move(options.replacements);
    pieces.sd = std::move(options.sd_replacements);
    pieces.entries = std::move(options.table_entries);
    struct LoadOverride {
        std::uint64_t offset = 0;  // in the partition data
        std::vector<std::uint8_t> bytes;
        const char* what = "";
        // A pack's main.dol is served from the options' own copy, not a
        // fifth one (RiiMajor's is 5.9 MB): `length` bytes, zeros past it.
        const std::vector<std::uint8_t>* borrowed = nullptr;
        std::uint64_t length = 0;
        std::uint64_t size() const { return borrowed ? length : bytes.size(); }
        void copy(std::uint8_t* to, std::uint64_t from, std::size_t n) const {
            if (!borrowed) {
                std::memcpy(to, bytes.data() + from, n);
                return;
            }
            const std::uint64_t have = from < borrowed->size() ? borrowed->size() - from : 0;
            const std::size_t real = static_cast<std::size_t>(std::min<std::uint64_t>(n, have));
            if (real) std::memcpy(to, borrowed->data() + from, real);
            if (real < n) std::memset(to + real, 0, n - real);
        }
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
        // Created files first, all in one rebuild of the table (a pack can
        // create thousands); the other relocations then find them too.
        std::vector<FstNewFile> new_files;
        for (const FstRelocation& r : options.relocations) {
            if (!r.create) continue;
            FstNewFile f;
            f.path = r.disc_path;
            f.offset = r.offset;
            f.size = r.size;
            new_files.push_back(std::move(f));
        }
        const unsigned created = static_cast<unsigned>(new_files.size());
        if (created != 0) {
            std::vector<std::uint32_t> indices;
            if (!fst.create_files(new_files, indices, error)) return false;
        }
        for (const FstRelocation& r : options.relocations) {
            if (!r.create) {
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
            // The table is rebuilt, padded to 32 bytes like the disc's. It
            // stays at its offset when the bytes after the original table
            // are free (not the DOL, not a file the game still reads
            // there); on an image packed tight (a file right after the
            // table) it moves to the virtual window, past the disc's end,
            // with the data header pointing there.
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
            std::string in_the_way;
            if (options.main_dol.empty() && layout.data_header.dol_offset < fst_end &&
                layout.data_header.dol_offset + dol.image_size() > fst_start) {
                in_the_way = "the DOL";
            }
            for (std::uint32_t i = 0; in_the_way.empty() && i < fst.count(); ++i) {
                const FstEntry& e = fst.entries()[i];
                if (e.is_directory || e.size == 0 || e.offset >= kVirtualWindowStart) continue;
                if (e.offset < fst_end && e.offset + e.size > fst_start) {
                    fst.path_of(i, in_the_way);
                    in_the_way = "'" + in_the_way + "'";
                }
            }
            if (!in_the_way.empty()) {
                const std::uint64_t moved = (window_cursor + 0x7FFF) & ~std::uint64_t(0x7FFF);
                if (((moved + fst_override.bytes.size()) >> 2) > 0xFFFFFFFFull) {
                    error = "the grown FST would overlap " + in_the_way + " and the virtual window is full";
                    return false;
                }
                fst_override.offset = moved;
                header.fst_offset = moved;
                window_cursor = moved + fst_override.bytes.size();
                logf("FST: the grown table would overlap %s; moved to 0x%llx\n", in_the_way.c_str(),
                     static_cast<unsigned long long>(moved));
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
        // Padded 32 bytes past its 32-byte-rounded end: an apploader rounds a
        // section's length up to 32 bytes, so a last section ending at the
        // file's end is read up to 31 bytes past it. Those bytes came from
        // the disc, where after the FST a WBFS image may keep nothing: d2x
        // never answered (an older CT-code DOL, stuck at 93%).
        const std::uint64_t size = ((options.main_dol.size() + 31) & ~std::uint64_t(31)) + 32;
        const std::uint64_t fst_start = header.fst_offset;
        const std::uint64_t fst_end = fst_start + ((header.fst_size + 31) & ~std::uint64_t(31));
        const bool in_place = header.dol_offset + size <= fst_start || header.dol_offset >= fst_end;
        if (!in_place) {
            header.dol_offset = (fst_end + 0x7FFF) & ~std::uint64_t(0x7FFF);
            header_changed = true;
        }
        LoadOverride dol;
        dol.offset = header.dol_offset;
        dol.borrowed = &options.main_dol;
        dol.length = size;
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

    // A code build that keeps the game from clearing its own memory at
    // start (Project+'s poke over the clearing call, so its code list
    // survives) would leave the game whatever this memory held: clear it
    // as a fresh console would have it, before the game goes in.
    if (!g_extras.pokes.empty()) {
        std::memset(reinterpret_cast<void*>(kGameStart), 0, kLoaderStart - kGameStart);
        DCFlushRange(reinterpret_cast<void*>(kGameStart), kLoaderStart - kGameStart);
        logf("Code builds: game memory 0x%08x-0x%08x cleared\n", static_cast<unsigned>(kGameStart),
             static_cast<unsigned>(kLoaderStart));
    }
    mem::LogUsage("before loading the game");  // the headroom a big pack leaves
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
        // The word offset is unsigned: the virtual window starts at word 0x80000000.
        if (length < 0 || !in_ram(dest, static_cast<std::uint32_t>(length))) {
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
            const std::uint64_t to = std::min(load_end, o.offset + o.size());
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
            const std::uint64_t o_end = o.offset + o.size();
            const std::uint64_t from = std::max(load_start, o.offset);
            const std::uint64_t to = std::min(load_end, o_end);
            if (from < to) {
                o.copy(static_cast<std::uint8_t*>(destination) + (from - load_start), from - o.offset,
                       static_cast<std::size_t>(to - from));
                logf("  %s bytes 0x%llx-0x%llx replaced with the rewritten copy\n", o.what,
                     static_cast<unsigned long long>(from - o.offset), static_cast<unsigned long long>(to - o.offset));
            }
        }
        DCFlushRange(destination, len);
        ICInvalidateRange(destination, len);
        // The game's own image, for search and ocarina patches: the DOL
        // sections, not the FST nor the apploader's buffers above the
        // arena.
        if ((std::uint64_t(woff) << 2) != header.fst_offset &&
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
    // The apploader set the arena, FST and BI2 fields (0x34-0x3C, 0xF4)
    // through the cache; the runtime and the hooks read them uncached
    // (read32), which saw what was there before the apploader (0x34 at the
    // top of MEM1, so a hook went over the FST): to memory with them first.
    DCFlushRange(reinterpret_cast<void*>(kMem1Start), 0x100);
    logf("Apploader: arena top 0x%08x, FST 0x%08x (%u bytes), BI2 0x%08x\n", load32(0x80000034),
         load32(0x80000038), load32(0x8000003C), load32(0x800000F4));


    // The DOL header for the runtime's search, read now: an RVZ game's
    // partition reads go through the SD card, which is handed on below.
    std::uint8_t dol_bytes[kDolHeaderBytes];
    // USB Loader GX's exclude_game list: games that check their own code
    // (MetaFortress). RiftWii's hooks and code patches stay out of them.
    const bool gx_protected = gx_protected_game(probe.header.game_id);
    bool gc_adapter = g_extras.gc_adapter != GcAdapterMode::Off && !gx_protected;
    if (gc_adapter && g_extras.gc_adapter != GcAdapterMode::Demo && (rvz.usb_fd >= 0 || pieces.needs_usb())) {
        // The runtime reads the USB drive through d2x while the game runs.
        // On (not Automatic) tries anyway: an experiment the player chose.
        const bool forced = g_extras.gc_adapter_forced;
        logf("GameCube adapter: %s: %s the USB drive, which the adapter broke on a Wii\n",
             forced ? "on anyway (the setting is On)" : "off", rvz.usb_fd >= 0 ? "the RVZ is read from" : "packs are read from");
        gc_adapter = forced;
    }
    // Every launch needs it: the game crash hook (wii/faulthook.hpp) is
    // always on.
    DolHeader dol;
    {
        if (!options.main_dol.empty()) {
            std::memcpy(dol_bytes, options.main_dol.data(), sizeof(dol_bytes));  // the executable that ran
        } else if (!data.read(layout.data_header.dol_offset, dol_bytes, sizeof(dol_bytes))) {
            error = "cannot read the DOL header";
            return false;
        }
        if (!parse_dol_header(dol_bytes, sizeof(dol_bytes), dol, error)) return false;
    }
    // The adapter's PAD search while boot.log is open; the rest of its
    // setup comes after the runtime's, when the log has ended.
    PadHook pad;
    if (gc_adapter) {
        std::string why;
        if (!find_pad_functions(dol, g_extras.gc_adapter == GcAdapterMode::Demo, pad, why)) {
            logf("GameCube adapter: off: %s\n", why.c_str());
            gc_adapter = false;
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
    if (g_vsd.enabled && !g_vsd.on_usb && !options.install_resident) {
        release_card_and_log();
        if (!sdio::open_card(card, error)) {
            error = "the virtual SD card needs the SD card: " + error;
            return false;
        }
    }
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
        ro.sdio_rca = card.rca;
        if (ro.pieces.needs_usb()) {
            // Opened when the packs were compiled, under this same IOS.
            if (!ums::Open(error)) return false;
            ro.usb_fd = ums::Fd();
            logf("USB drive: d2x's /dev/usb2, fd %d, for the packs on it\n", ro.usb_fd);
        }
        ro.savegame = savegame;
        ro.savegame.file_device = file_device && card.fd >= 0;
        ro.rvz = rvz;
        ro.retail_bca = options.retail_bca;
        ro.mem1_floor = mem1_floor;
        // A code build (Project+) sizes the game's heaps to all of MEM1;
        // its list is placed elsewhere, so the handler's own list room is
        // free for the veneers and the runtime's code goes to MEM2.
        if (g_extras.code_list_start != 0 && !g_extras.cheat_gct.empty()) ro.mem1_veneers = kCodeVeneers;
        if (!install_resident(dol, ro, resident, error)) return false;
    }
    // The virtual SD card: in the resident runtime's place (never with it).
    VsdHook vsd;
    if (g_vsd.enabled && !options.install_resident) {
        std::string why;
        VsdDevice device;
        if (g_vsd.on_usb) {
            device.backend = VSD_BACKEND_USB;
            device.fd = ums::Fd();
        } else {
            device.backend = card.d2x ? VSD_BACKEND_D2X_SD : VSD_BACKEND_SLOT0;
            device.fd = card.fd;
            device.sdhc = card.sdhc;
            device.rca = card.rca;
        }
        const bool veneers = g_extras.code_list_start != 0 && !g_extras.cheat_gct.empty();
        if (!plan_vsd_hook(dol, g_vsd, device, game_arena1_hi(), mem1_floor, read32(0x80003124),
                           veneers ? kCodeVeneers : 0, options.memory_patches, vsd, why)) {
            // The build's files are only in the image: without it the game
            // would look for them on a card that does not have them.
            error = "the virtual SD card: " + why;
            return false;
        }
    }
    const auto base_arena1_hi = [&]() {
        return vsd.active ? vsd.new_arena1_hi : options.install_resident ? resident.new_arena1_hi : game_arena1_hi();
    };
    const auto base_arena2_lo = [&]() {
        return vsd.active ? vsd.new_arena2_lo : options.install_resident ? resident.new_arena2_lo : read32(0x80003124);
    };
    // The GameCube adapter: below the runtime, or on its own.
    if (gc_adapter) {
        std::string why;
        const std::uint32_t arena1_hi = base_arena1_hi();
        const std::uint32_t arena2_lo = base_arena2_lo();
        if (!plan_pad_hook(dol, arena1_hi, mem1_floor, arena2_lo,
                           options.install_resident ? resident.ioctl_async_original : 0,
                           options.install_resident ? resident.ioctlv_async_original : 0, options.memory_patches,
                           pad, why)) {
            logf("GameCube adapter: off: %s\n", why.c_str());
        }
    }
    // In-game screenshots: below the adapter, the runtime, or alone.
    ShotHook shot;
    if (g_extras.screenshots && !gx_protected) {
        std::string why;
        const std::uint32_t arena1_hi = pad.active                 ? pad.new_arena1_hi
                                                                   : base_arena1_hi();
        const std::uint32_t arena2_lo = pad.active                 ? pad.new_arena2_lo
                                                                   : base_arena2_lo();
        ShotIpc known;
        if (options.install_resident) {
            known.open_async = resident.originals[RT_IPC_ASYNC(1)];
            known.close_async = resident.originals[RT_IPC_ASYNC(2)];
            known.write_async = resident.originals[RT_IPC_ASYNC(4)];
            known.ioctl_async = resident.originals[RT_IPC_ASYNC(6)];
            known.ioctlv_async = resident.ioctlv_async;
        }
        // Never a frame copy: its 0.8 MB of MEM2 is more than games spare.
        // Newer Super Mario Bros. Wii crashed on its title screen, and
        // HAL's Kirby games (Return to Dream Land, Dream Collection)
        // without packs failed to make a heap and stayed black. Direct
        // shots read the frame buffer itself.
        const bool direct = true;
        if (!plan_shot_hook(dol, arena1_hi, mem1_floor, arena2_lo, known, pad.read, g_extras.screenshots_demo,
                            direct, options.memory_patches, shot, why)) {
            logf("Screenshots: off: %s\n", why.c_str());
        }
    }
    // A crash in the game is saved for the next start: below all of the
    // above, always.
    FaultHook fault;
    if (gx_protected) {
        logf("Game crashes: not recorded: the game checks its own code (MetaFortress); RiftWii's hooks stay out\n");
    } else if (debug_off("fault")) {
        logf("Game crashes: not recorded: debug_off (and the BCA read not answered)\n");
    } else {
        std::string why;
        const std::uint32_t arena1_hi = shot.active                ? shot.new_arena1_hi
                                        : pad.active               ? pad.new_arena1_hi
                                                                   : base_arena1_hi();
        const std::uint32_t arena2_lo = shot.active                ? shot.new_arena2_lo
                                        : pad.active               ? pad.new_arena2_lo
                                                                   : base_arena2_lo();
        const bool answer_bca = options.retail_bca && !options.install_resident && !debug_off("bca");
        if (options.retail_bca && debug_off("bca")) logf("BCA: debug_off: not answered\n");
        // A code build that moved its code list sizes its heaps to all of
        // MEM1 (PMEX Remix wrote over the blob below the arena top): the
        // blob goes to MEM2, past the veneers the others may use.
        const std::uint32_t veneers =
            g_extras.code_list_start != 0 && !g_extras.cheat_gct.empty() ? kFaultVeneers : 0;
        if (!plan_fault_hook(dol, arena1_hi, mem1_floor, arena2_lo, veneers, answer_bca, options.memory_patches, fault,
                             why)) {
            logf("Game crashes: not recorded%s: %s\n", answer_bca ? " and the BCA read not answered" : "",
                 why.c_str());
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
    if (!running_in_dolphin() && !debug_off("consoletype"))
        store32(0x8000002C, 1 + (read32(0xCC00302C) >> 28));  // console type
    store32(0x800000EC, 0x81800000);            // debug monitor location
    store32(0x800000F0, 0x01800000);            // simulated memory size
    store32(0x800000F8, 0x0E7BE2C0);            // bus clock
    store32(0x800000FC, 0x2B73A840);            // CPU clock
    // MEM1 arena end (0x34 from the apploader, 0x3110 as the System Menu
    // sets it): the FST, or the runtime's code just below it, or Return to
    // RiftWii's stub below all of that. The stub goes where nothing of the
    // game's can be: a pack's loader may put its code anywhere the game
    // doesn't use, and CTGP's writes its own over the SDK's spare room as
    // it runs.
    const std::uint32_t arena1_top = fault.active               ? fault.new_arena1_hi
                                     : shot.active              ? shot.new_arena1_hi
                                     : pad.active               ? pad.new_arena1_hi
                                     : options.install_resident ? resident.new_arena1_hi
                                     : vsd.active && !vsd.code_in_mem2 ? vsd.new_arena1_hi
                                                                : load32(0x80000038);
    const u64 return_to = return_title();
    const std::uint32_t return_stub = return_to != 0 ? (arena1_top - 32) & ~31u : 0;
    const std::uint32_t arena1_end = return_stub != 0 ? return_stub : arena1_top;
    if (options.install_resident || vsd.active || pad.active || shot.active || fault.active || return_stub != 0)
        store32(0x80000034, arena1_end);
    store32(0x80003110, arena1_end);
    std::memcpy(reinterpret_cast<void*>(0x80003180), probe.disc_id, 4);
    store32(0x80003184, 0x80000000);            // where the game id lives
    store32(0x80003194, probe.partition.type);
    store32(0x80003198, static_cast<u32>(probe.partition.offset >> 2));
    ProgressStage("Starting the game", 100);
    configure_video_for_game(probe.header.game_id.size() > 3 ? probe.header.game_id[3] : 'E');
    // A Japanese game: the VI configuration bit the System Menu sets on
    // Japanese consoles (USB Loader GX's gamepatches()). Hollywood's
    // registers need AHBPROT, as the Homebrew Channel leaves it.
    if (probe.header.game_id.size() > 3 && probe.header.game_id[3] == 'J' && !running_in_dolphin() &&
        read32(0x0D800064) == 0xFFFFFFFF) {
        write32(0xCD800018, read32(0xCD800018) | (1u << 17));
    }
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
    if (fault.active) {
        write32(0x80003124, fault.new_arena2_lo);  // above all the others
    } else if (shot.active) {
        write32(0x80003124, shot.new_arena2_lo);  // above the adapter's and the runtime's
    } else if (pad.active) {
        // IOS's own field, like 0x3140: uncached, after the flush. The end
        // (0x3128) is never moved: the top of MEM2 stays the game's.
        write32(0x80003124, pad.new_arena2_lo);
    } else if (options.install_resident && resident.new_arena2_lo != resident.old_arena2_lo) {
        write32(0x80003124, resident.new_arena2_lo);
    } else if (vsd.active) {
        write32(0x80003124, vsd.new_arena2_lo);
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
        if (options.install_resident && resident.code_base >= loader_end && resident.code_base < kMem1End) {
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
        if (return_stub != 0) exclusions.push_back(MemoryRegion{return_stub, 32});
        if (vsd.active) {
            exclusions.push_back(MemoryRegion{vsd.code_base, vsd.code_bytes});
            exclusions.push_back(MemoryRegion{vsd.data_base, vsd.data_bytes});
            exclusions.push_back(MemoryRegion{vsd.stage_base, vsd.data_bytes});
        }
        if (pad.active) {
            exclusions.push_back(MemoryRegion{pad.code_base, pad.code_bytes});
            exclusions.push_back(MemoryRegion{pad.state_base, pad.state_bytes});
        }
        if (shot.active) {
            exclusions.push_back(MemoryRegion{shot.code_base, shot.code_bytes});
            exclusions.push_back(MemoryRegion{shot.state_base, shot.state_bytes});
        }
        if (fault.active) {
            exclusions.push_back(MemoryRegion{fault.code_base, fault.code_bytes});
            exclusions.push_back(MemoryRegion{fault.state_base, fault.state_bytes});
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
    // USB Loader GX's fixes for particular games (riftwii/gxpatches.hpp),
    // over the game as loaded and patched, before the menu's extras.
    std::vector<CodeSpan> loaded_spans;
    for (const MemoryRegion& r : loaded) {
        loaded_spans.push_back(CodeSpan{reinterpret_cast<std::uint8_t*>(r.address), r.length, r.address});
    }
    const auto sync_loaded = [&loaded]() {
        for (const MemoryRegion& r : loaded) {
            DCFlushRange(reinterpret_cast<void*>(r.address), r.length);
            ICInvalidateRange(reinterpret_cast<void*>(r.address), r.length);
        }
    };
    {
        GxReport gx;
        const bool own_executable = !options.main_dol.empty();
        unsigned changed = gx_game_patches(probe.header.game_id, dol, loaded_spans, gx, own_executable);
        if (di::frag_device() == 2 && !own_executable) changed += gx_sd_card_patches(probe.header.game_id, loaded_spans, gx);
        if (g_extras.region_video && !gx_protected && probe.header.game_id.size() >= 4)
            changed += gx_region_video_fix(probe.header.game_id[3], loaded_spans, gx);
        for (const std::string& note : gx.notes) logf("Game fixes: %s\n", note.c_str());
        if (changed) sync_loaded();
    }
    if (gx_protected && (g_extras.video.width != VideoWidth::Game || g_extras.video.deflicker != Deflicker::Game ||
                         g_extras.video.remove_borders)) {
        // GX leaves width and deflicker out of these games too.
        logf("Video: width, deflicker and borders as the game has them: it checks its own code (MetaFortress)\n");
        g_extras.video.width = VideoWidth::Game;
        g_extras.video.deflicker = Deflicker::Game;
        g_extras.video.remove_borders = false;
    }
    // The menu's extras, over the game as loaded and patched.
    apply_video(loaded, card.fd < 0);  // not while the runtime's card handle is open
    bool codes_first = false;  // run the codes once before the game starts
    if (!g_extras.cheat_gct.empty()) {
        std::string why;
        std::vector<MemoryRegion> keep_out;
        if (options.install_resident) keep_out.push_back(MemoryRegion{resident.code_base, resident.code_bytes});
        if (vsd.active) {
            keep_out.push_back(MemoryRegion{vsd.code_base, vsd.code_bytes});
            keep_out.push_back(MemoryRegion{vsd.stage_base, vsd.data_bytes});
        }
        if (pad.active) keep_out.push_back(MemoryRegion{pad.code_base, pad.code_bytes});
        if (shot.active) keep_out.push_back(MemoryRegion{shot.code_base, shot.code_bytes});
        if (fault.active) keep_out.push_back(MemoryRegion{fault.code_base, fault.code_bytes});
        if (!install_cheats(loaded, options.memory_patches, keep_out, why)) logf("Codes are off: %s\n", why.c_str());
        else codes_first = !g_extras.code_builds.empty();
    }
    if (!gx_protected) {
        GxReport gx;
        if (debug_off("480p")) logf("Game fixes: debug_off: no 480p fix\n");
        else if (gx_fix_480p(loaded_spans, gx)) sync_loaded();
        for (const std::string& note : gx.notes) logf("Game fixes: %s\n", note.c_str());
    }
    if (vsd.active) {
        std::string why;
        if (!install_vsd_hook(vsd, why)) {
            error = "the virtual SD card: " + why;
            return false;
        }
    }
    if (pad.active) {
        std::string why;
        if (!install_pad_hook(pad, why)) logf("GameCube adapter: off: %s\n", why.c_str());
    }
    if (shot.active) {
        std::string why;
        if (!install_shot_hook(shot, why)) logf("Screenshots: off: %s\n", why.c_str());
    }
    if (fault.active) {
        std::string why;
        if (!install_fault_hook(fault, why)) logf("Game crashes: not recorded: %s\n", why.c_str());
    }
    // Last, in gamepatches.c's order: Wiimmfi's Mario Kart Wii patch goes
    // below everything else in the MEM1 arena. Packs bring their own online
    // setup (and may have replaced the code these patches expect).
    if (g_extras.server != WfcServer::Off) {
        const bool packs = !options.memory_patches.empty() || !options.virtual_files.empty() ||
                           file_replacements;
        if (packs) logf("WFC: not patched; packs are on\n");
        else ApplyWfc(loaded, g_extras.server, g_extras.wfc_domain, g_extras.game_id, probe.header.version);
    }
    if (debug_off("returnto")) logf("Return to: debug_off: the game is not patched\n");
    else apply_return_to(loaded, return_to, return_stub);
    settime(secs_to_ticks(static_cast<u64>(std::time(nullptr)) - kWiiEpochOffset));

    release_card_and_log();
    // The resident starts with the game; from here it needs the selected
    // raw-card fd for SD-backed reads and savegame writes.
    card_handed_to_runtime = true;
    // libogc's shutdown clears the BI2 pointer (0x800000F4) with its own
    // exception fields. The apploader set it, and a game's start-up code
    // may read it: Other M Redux's finds the top of memory through it and
    // jumped to 0 - its payload's size without it.
    const std::uint32_t bi2 = load32(0x800000F4);
    SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);
    store32(0x800000F4, bi2);
    if (options.install_resident && resident.data_bytes != 0) {
        // The runtime's data to the bottom of the MEM2 arena, over what was
        // this loader's own memory (nothing below needs it any more).
        std::memcpy(reinterpret_cast<void*>(resident.data_base), reinterpret_cast<const void*>(resident.stage_base),
                    resident.data_bytes);
        DCFlushRange(reinterpret_cast<void*>(resident.data_base), resident.data_bytes);
        ICInvalidateRange(reinterpret_cast<void*>(resident.data_base), resident.data_bytes);  // the code, when it is here
    }
    place_vsd_hook(vsd);  // to the bottom of the MEM2 arena, as the runtime's data above
    place_fault_hook(fault);  // the crash blob, when a code build pushed it to MEM2
    dolboot_system_call_vector();  // what the system software leaves for a game (channel/common/dolboot.h)
    if (codes_first && !debug_off("prerun")) {
        // As Gecko loaders do: the handler runs once before the game, so a
        // code build's writes to the game's tables (Project+ resizes its
        // heaps) are in place before the game reads them at start. The
        // handler returns to its caller like the hooked function would.
        reinterpret_cast<void (*)()>(kCodeHandlerEntry)();
    }
    game_entry();
    // A game entry must never return, but release the card if it does.
    card_handed_to_runtime = false;
    sdio::close_card(card);
    error = "the game entry point returned";
    return false;
}

}  // namespace

// Whether a pack's replacement executable has the SDK's IPC functions the
// resident runtime hooks (find_ipc_symbols over its text sections).
static bool executable_has_ipc(const std::vector<std::uint8_t>& bytes, std::string& why) {
    DolHeader dol;
    if (!parse_dol_header(bytes.data(), bytes.size(), dol, why)) return false;
    std::vector<CodeRange> text;
    for (std::size_t i = 0; i < kDolTextSections; ++i) {
        const DolSection& s = dol.sections[i];
        if (!s.used()) continue;
        if (static_cast<std::uint64_t>(s.offset) + s.size > bytes.size()) {
            why = "a text section lies past the end of the file";
            return false;
        }
        text.push_back({s.address, bytes.data() + s.offset, s.size});
    }
    IpcSymbols symbols;
    return find_ipc_symbols(text, symbols, why);
}

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
    // A blank card log for this game (the menu has reported the last one).
    // Without it the game runs all the same, unlogged.
    bool cardlog = false;
    if (FILE* f = std::fopen(kCardLogPath, "wb")) {
        static const std::uint8_t blank[riftwii::kCardLogBytes] = {};
        cardlog = std::fwrite(blank, 1, sizeof(blank), f) == sizeof(blank);
        cardlog = std::fclose(f) == 0 && cardlog;
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
    if (cardlog) {
        forget_sd_layout();
        Fat32File file;
        std::string why;
        if (resolve_sd_file(kCardLogPath, file, why) && !file.fragments.empty() &&
            file.fragments.front().sector_count != 0 && file.fragments.front().sector <= 0xFFFFFFFFu) {
            out.cardlog_lba = static_cast<std::uint32_t>(file.fragments.front().sector);
        } else {
            logf("Card log: off (%s)\n", why.empty() ? "no sector" : why.c_str());
        }
    }
    out.prefix = prefix;
    out.enabled = true;
    logf("Savegame: %s served from %s%s\n", prefix, options.savegame_dir.c_str(),
         out.clone ? (existed ? " (marked folder: the NAND save is cloned in again)" : " (new folder: the NAND save is cloned in)")
                   : existed ? " (existing folder)" : " (new folder)");
    return true;
}

void SetLaunchExtras(LaunchExtras extras) { g_extras = std::move(extras); }

std::vector<std::string> TakeCardLog() {
    std::uint8_t bytes[riftwii::kCardLogBytes] = {};
    FILE* f = std::fopen(kCardLogPath, "rb");
    if (f == nullptr) return {};
    const bool read = std::fread(bytes, 1, sizeof(bytes), f) == sizeof(bytes);
    std::fclose(f);
    riftwii::CardLog log;
    if (!read || !riftwii::parse_card_log(bytes, sizeof(bytes), log) || log.events == 0) return {};
    if (FILE* blank = std::fopen(kCardLogPath, "wb")) {
        static const std::uint8_t zeros[riftwii::kCardLogBytes] = {};
        std::fwrite(zeros, 1, sizeof(zeros), blank);
        std::fclose(blank);
    }
    return riftwii::describe_card_log(log);
}

bool is_wii_u() {
    static int known = -1;
    if (known < 0) {
        u32 contents = 0;
        known = ES_GetTitleContentsCount(0x0000000100000200ULL, &contents) >= 0 && contents > 0;
    }
    return known == 1;
}

// Return to RiftWii (Settings): a game's HOME Menu "Wii Menu" starts the
// RiftWii channel, which starts RiftWii. Two ways, both tried: d2x's own
// "return to" (ES ioctl 0xA1, as PatchNewReturnTo asks it) for a game under
// d2x, and the game's __OSLaunchMenu patched to load the channel's title
// (src/returnto.cpp) for one under any IOS.
u64 return_title() {
    if (g_extras.return_to_menu) return 0;
    if (g_extras.return_to == 0 && Settings().return_to != "riftwii") return 0;
    const u64 title = g_extras.return_to != 0 ? g_extras.return_to : ChannelTitle();
    if (title == 0) {
        logf("Return to RiftWii: the RiftWii channel is not installed; the Wii Menu stays\n");
        return 0;
    }
    if ((title >> 32) != 0x00010001) {
        logf("Return to %08x-%08x: only 00010001 titles can be patched in; the Wii Menu stays\n",
             static_cast<u32>(title >> 32), static_cast<u32>(title));
        return 0;
    }
    return title;
}

// `stub`: 32 bytes of MEM1 above the game's arena.
void apply_return_to(const std::vector<MemoryRegion>& loaded, u64 title, std::uint32_t stub) {
    if (title == 0) return;
    std::vector<CodeSpan> spans;
    for (const MemoryRegion& r : loaded) spans.push_back(CodeSpan{reinterpret_cast<std::uint8_t*>(r.address), r.length, r.address});
    const ReturnToReport report =
        patch_return_to(spans, static_cast<std::uint32_t>(title), reinterpret_cast<std::uint8_t*>(stub), stub);
    if (report.patched) {
        DCFlushRange(reinterpret_cast<void*>(stub), 32);
        ICInvalidateRange(reinterpret_cast<void*>(stub), 32);
        for (const MemoryRegion& r : loaded) {
            DCFlushRange(reinterpret_cast<void*>(r.address), r.length);
            ICInvalidateRange(reinterpret_cast<void*>(r.address), r.length);
        }
    }
    alignas(32) static u64 target;
    target = title;
    s32 d2x = -1;
    const s32 es = IOS_Open("/dev/es", 0);
    if (es >= 0) {
        alignas(32) static ioctlv vector[1];
        vector[0].data = &target;
        vector[0].len = sizeof target;
        d2x = IOS_Ioctlv(es, 0xA1, 1, 0, vector);
        IOS_Close(es);
    }
    logf("Return to %08x-%08x: game %s; d2x %s\n", static_cast<u32>(title >> 32), static_cast<u32>(title),
         report.describe().c_str(), d2x >= 0 ? "set" : "not available");
}

// IOS's file system keeps the Wii Menu's files to the Wii Menu. With
// AHBPROT off (the Homebrew Channel starts apps that way) the PPC can
// write IOS's memory in MEM2, so the permission check can be opened for
// the rest of this IOS's life: in its Thumb code, "cmp r3, r1; beq" before
// "movs r5, #0x66" becomes an unconditional branch past the refusal. IOS's
// memory is written only there, and only after IOS refused. An IOS
// reload (the game's) brings the check back.
bool open_nand_permissions(const char* who) {
    if (read32(0x0D800064) != 0xFFFFFFFF) {
        logf("%s: no AHBPROT access, IOS%d left as it is\n", who, IOS_GetVersion());
        return false;
    }
    static const u8 kCheck[] = {0x42, 0x8B, 0xD0, 0x01, 0x25, 0x66};
    const u16 protection = read16(0x0D8B420A);
    write16(0x0D8B420A, 2);  // MEM2 protection off while IOS's code is written
    int patched = 0;
    // IOS lives at the top of MEM2; read and written uncached.
    for (u32 at = 0xD3400000; at + sizeof kCheck <= 0xD4000000; at += 2) {
        volatile u8* p = reinterpret_cast<volatile u8*>(at);
        bool same = true;
        for (std::size_t i = 0; same && i < sizeof kCheck; ++i) same = p[i] == kCheck[i];
        if (!same) continue;
        *reinterpret_cast<volatile u16*>(at + 2) = 0xE001;  // beq +2 -> b +2
        ++patched;
    }
    write16(0x0D8B420A, protection);
    logf("%s: IOS%d's NAND permission check %s\n", who, IOS_GetVersion(), patched ? "opened" : "not found");
    return patched != 0;
}

// ES gives a title it starts hardware access only when the title's TMD asks
// for it (the access rights at TMD offset 0x1D8, "movs r2, #0xEC; lsls r2,
// r2, #1" in this sequence of ES's Thumb code), and an IOS's TMD does not.
// Writing 1 to the byte 25 bytes into the sequence makes that test pass for
// every title. This is the byte libruntimeiospatch's IosPatch_AHBPROT
// writes (USB Loader GX, WiiFlow); the code here is RiftWii's own. Same
// MEM2 handling as open_nand_permissions above.
bool keep_hardware_access(const char* who) {
    if (read32(0x0D800064) != 0xFFFFFFFF) {
        logf("%s: no AHBPROT access, IOS%d's ES left as it is\n", who, IOS_GetVersion());
        return false;
    }
    static const u8 kRights[] = {0x68, 0x5B, 0x22, 0xEC, 0x00, 0x52, 0x18, 0x9B,
                                 0x68, 0x1B, 0x46, 0x98, 0x07, 0xDB};
    const u16 protection = read16(0x0D8B420A);
    write16(0x0D8B420A, 2);
    int patched = 0;
    for (u32 at = 0xD3400000; at + 26 <= 0xD4000000; at += 2) {
        volatile u8* p = reinterpret_cast<volatile u8*>(at);
        bool same = true;
        for (std::size_t i = 0; same && i < sizeof kRights; ++i) same = p[i] == kRights[i];
        if (!same) continue;
        p[25] = 0x01;
        ++patched;
    }
    write16(0x0D8B420A, protection);
    logf("%s: IOS%d's ES %s\n", who, IOS_GetVersion(),
         patched ? "keeps hardware access on for the next IOS" : "check for hardware access not found");
    return patched != 0;
}

// The Wii Menu's play log, so the Message Board shows the game and how
// long it was played, as it does for a disc started from the Wii Menu.
// Written under the IOS running now (the game's may lack the patch); a
// failure only costs the entry. message_board = off in settings.txt skips
// it.
s32 write_play_file(const u8* buffer, u32 bytes) {
    alignas(32) static const char kPath[] = "/title/00000001/00000002/data/play_rec.dat";
    s32 fd = ISFS_Open(kPath, ISFS_OPEN_WRITE);
    if (fd == -106) {  // not there yet
        const s32 made = ISFS_CreateFile(kPath, 0, 3, 3, 3);
        fd = made < 0 ? made : ISFS_Open(kPath, ISFS_OPEN_WRITE);
    }
    if (fd < 0) return fd;
    const s32 r = ISFS_Write(fd, buffer, bytes);
    ISFS_Close(fd);
    return r;
}

void write_play_log(const DiscProbe& probe) {
    const auto off = Settings().other.find("message_board");
    if (off != Settings().other.end() && off->second == "off") return;
    const std::string name = GameDisplayName(probe.header.game_id,
                                             probe.header.title.empty() ? probe.header.game_id : probe.header.title);
    const u64 ticks = secs_to_ticks(static_cast<u64>(std::time(nullptr)) - kWiiEpochOffset);
    const std::vector<std::uint8_t> record = play_log_record(name, probe.header.game_id, ticks);
    alignas(32) static u8 buffer[kPlayLogBytes];
    std::memcpy(buffer, record.data(), sizeof buffer);
    s32 r = ISFS_Initialize();
    if (r >= 0) r = write_play_file(buffer, sizeof buffer);
    if (r == -102 && open_nand_permissions("Message Board")) r = write_play_file(buffer, sizeof buffer);
    if (r == static_cast<s32>(sizeof buffer)) {
        logf("Message Board: play log written (%s)\n", name.c_str());
    } else {
        logf("Message Board: play log not written (error %d under IOS%d)\n", r, IOS_GetVersion());
    }
    ISFS_Deinitialize();  // not left open for the game's IOS reload
}

bool boot_game(const DiscProbe& probe, BootOptions options, std::string& error) {
    const std::uint32_t required = probe.tmd.required_ios();
    if (required == 0) {
        error = "the TMD does not name an IOS";
        return false;
    }
    logf("Booting %s with IOS%u\n", probe.header.game_id.c_str(), required);
    write_play_log(probe);
    BootOptions& effective = options;  // the caller's, moved in: no second copy
    const int running_ios = IOS_GetVersion();
    // A pack whose main.dol is another program, not the game: the CTGP-R
    // Channel's launcher (CTGP Revolution 1.03's Riivolution XML), which
    // loads Mario Kart Wii itself later. The resident runtime hooks the
    // SDK's IPC functions in the executable the apploader loads; this one
    // has none, and what it loads next would not carry the hooks anyway.
    // A save redirect (and the file device) is dropped for it and the
    // program starts; file replacements cannot be served, so those fail.
    if (effective.install_resident && !effective.main_dol.empty()) {
        std::string why;
        if (!executable_has_ipc(effective.main_dol, why)) {
            const bool files = !effective.table_entries.empty() || !effective.relocations.empty() ||
                               !effective.replacements.empty() || !effective.virtual_files.empty() ||
                               !effective.sd_replacements.empty() || di::has_partition_resolver();
            if (files) {
                error = "this pack replaces main.dol with a program RiftWii can't hook (" + why +
                        "), so its file replacements can't be served" +
                        (di::has_partition_resolver() ? " (and an RVZ game needs them)" : "");
                return false;
            }
            logf("main.dol: the pack's executable is not a game RiftWii can hook (%s); it starts without the "
                 "resident runtime%s\n",
                 why.c_str(),
                 effective.savegame_dir.empty() ? ""
                                                : ", and the save stays on the console (NAND) instead of the SD card");
            effective.install_resident = false;
            effective.resident_gecko = false;
            effective.savegame_dir.clear();
            effective.savegame_clone = false;
            effective.file_device = false;
        }
    }
    if (g_extras.gc_adapter != GcAdapterMode::Off && di::has_partition_resolver()) {
        // RVZ games and the adapter conflict (GitHub issue #4): off for
        // every RVZ launch, even with the setting On, until that is solved.
        logf("GameCube adapter: off: RVZ games and the adapter do not work together yet (issue #4)\n");
        g_extras.gc_adapter = GcAdapterMode::Off;
        g_extras.gc_adapter_forced = false;
    }
    if (g_extras.gc_adapter == GcAdapterMode::Auto || g_extras.gc_adapter == GcAdapterMode::On) {
        // Where the adapter broke the launch on hardware, it stays off and
        // USB is not touched for it: on a Wii U's d2x cIOS /dev/usb/hid
        // never answered; with the game read from the USB drive through
        // d2x, the game's disc reads failed. A Wii U's IOS 58 refused
        // RiftWii's handle only while the menu's USB held /dev/usb/hid;
        // the game's is opened after libogc's USB is shut down.
        const char* off = is_wii_u() && running_ios != 58 ? "on a Wii U it needs IOS 58 (the Menu IOS)"
                          : di::frag_device() == 1          ? "the game is read from the USB drive, which the adapter breaks"
                                                            : nullptr;
        // On (not Automatic) tries anyway: an experiment the player chose.
        // Every /dev/usb/hid call at launch has a time limit, so a module
        // that never answers turns the adapter off rather than hang.
        g_extras.gc_adapter_forced = g_extras.gc_adapter == GcAdapterMode::On;  // the player's choice, not Automatic's
        if (off && g_extras.gc_adapter_forced) {
            logf("GameCube adapter: on anyway (the setting is On), although %s\n", off);
        } else if (off) {
            logf("GameCube adapter: off: %s\n", off);
            g_extras.gc_adapter = GcAdapterMode::Off;
        }
    }
    if (g_extras.gc_adapter == GcAdapterMode::Auto || g_extras.gc_adapter == GcAdapterMode::On) {
        // Auto: on only when an adapter is there now, so games that use
        // USB input of their own (and games with their own adapter code)
        // are left alone otherwise.
        std::string how;
        const AdapterSeen seen = look_for_gc_adapter(how);
        logf("GameCube adapter (%s): %s\n", g_extras.gc_adapter == GcAdapterMode::Auto ? "auto" : "on", how.c_str());
        if (g_extras.gc_adapter == GcAdapterMode::Auto) {
            g_extras.gc_adapter = seen == AdapterSeen::Missing ? GcAdapterMode::Off : GcAdapterMode::On;
        }
    }
    // Games on IOS57 start under it even when the runtime needs the card:
    // Just Dance 2014 goes black under IOS58 (on a tester's Wii U, with a
    // save-only pack and with a files-only one) and runs under IOS57, which
    // reads the card too. Older IOSes keep the rule above (they may not
    // read SDHC cards). `sd_launch_ios` in settings.txt overrides it:
    // `game` reloads for every game, `menu` never does.
    const auto sd_ios = Settings().other.find("sd_launch_ios");
    const std::string sd_ios_setting = sd_ios != Settings().other.end() ? sd_ios->second : std::string();
    const bool reload_for_sd = sd_ios_setting == "game" || (sd_ios_setting != "menu" && required == 57);
    if (!effective.preserve_current_ios && running_ios != static_cast<int>(required) &&
        needs_resident_sd(effective) && reload_for_sd) {
        logf("Starting IOS%u for the game although the runtime needs the SD card (%s)\n", required,
             sd_ios_setting == "game" ? "sd_launch_ios = game" : "IOS57 games run under it");
    } else if (!effective.preserve_current_ios && running_ios != static_cast<int>(required) &&
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
    // A burned disc read through d2x (its DVD-ROM mode, bit 0): the burn
    // has no BCA either, and d2x answers the same way.
    std::uint32_t d2x_mode = 0;
    std::string no_d2x;
    const bool burned = di::frag_device() == 0 && !di::has_partition_resolver() && MenuCiosSlot() != 0 &&
                        di::probe_d2x(d2x_mode, no_d2x) && (d2x_mode & 1u) != 0;
    if (burned) logf("Disc: a burned disc, read through d2x's DVD-ROM mode\n");
    if (di::frag_device() != 0 || di::has_partition_resolver() || burned) {
        // An image has no drive to answer the BCA read; d2x answers from
        // its bytes 0x100-0x13F, usually zero in images made from a dump.
        // A game that checks it (New Super Mario Bros. Wii) stops minutes
        // into play, so the runtime answers as a retail disc does.
        std::uint8_t bca[RT_BCA_BYTES];
        std::string why;
        bool retail = di::read_bca(bca, why);
        // Only the start is fixed; the last bytes hold disc data.
        for (std::uint32_t i = 0; retail && i <= RT_BCA_MARK; ++i) {
            if (bca[i] != (i == RT_BCA_MARK ? 1u : 0u)) retail = false;
        }
        if (retail) {
            logf("BCA: the image has a retail one\n");
        } else {
            // Answered by the resident runtime when it goes in anyway,
            // else by the small crash blob (wii/faulthook.hpp): the
            // runtime alone takes some 100 KB of the game's memory, which
            // Super Smash Bros. Brawl does not have to spare.
            logf("BCA: %s; answered with a retail one\n", why.empty() ? "the image has none" : why.c_str());
            effective.retail_bca = true;
        }
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
    if (effective.install_resident && effective.file_device && !g_extras.code_builds.empty()) {
        // A code build (Project+) reads the SD card itself, through the
        // game's own SD driver; the runtime holding the card open for the
        // file device (a Riivolution pack's) would stand in its way.
        logf("Riivolution's \"file\" device is off: %s reads the SD card itself\n", g_extras.code_builds.c_str());
    } else if (effective.install_resident && effective.file_device) {
        // The save redirect's volume carries the root too; without one,
        // the root's own.
        std::string why;
        if (savegame.enabled || resolve_sd_directory("sd:/", savegame.volume, why)) {
            savegame.file_device = true;
        } else {
            logf("Riivolution's \"file\" device is off: %s\n", why.c_str());
        }
    }

    // Code builds inside the virtual SD card (sd.raw): the image served to
    // the game as its SD card by the virtual SD card blob, which needs the
    // card or d2x's USB device (and so the running IOS) at the handoff.
    // Not with packs yet: the resident runtime reads the same card.
    g_vsd = VsdImage{};
    if (g_extras.code_build_in_image) {
        std::string why;
        if (effective.install_resident) {
            error = "code builds inside sd.raw don't work together with Riivolution packs yet";
            return false;
        }
        if (!find_vsd_image(g_vsd, why)) {
            error = "the virtual SD card: " + why;
            return false;
        } else {
            logf("Virtual SD card: %s (%u sectors, %u piece(s)) for %s\n", g_vsd.path.c_str(), g_vsd.sectors,
                 static_cast<unsigned>(g_vsd.extents.size()), g_extras.code_builds.c_str());
            if (!effective.preserve_current_ios && running_ios != static_cast<int>(required)) {
                effective.preserve_current_ios = true;
                logf("Keeping IOS%d for the virtual SD card; reporting IOS%u to the game\n", running_ios, required);
            }
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
