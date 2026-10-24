// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "wfsdrive.hpp"

#include <gccore.h>

#include <cstring>
#include <utility>
#include <vector>

#include "log.hpp"
#include "otpkey.hpp"
#include "riftwii/wfs.hpp"
#include "riftwii/wfstitle.hpp"

namespace riftwii::wii {
namespace {

// The scan runs in Home's scan thread; a start reads the result in the
// main thread.
mutex_t g_lock = LWP_MUTEX_NULL;
struct Locked {
    Locked() {
        if (g_lock == LWP_MUTEX_NULL) LWP_MutexInit(&g_lock, false);
        LWP_MutexLock(g_lock);
    }
    ~Locked() { LWP_MutexUnlock(g_lock); }
};

bool g_found = false;
std::vector<WfsTitle> g_titles;

}  // namespace

bool ProbeWfsDrive(const BlockReader& reader, std::uint32_t sectors, const std::string& which) {
    std::uint8_t key[16];
    if (!ConsoleWfsKey(key)) {
        logf("WFS: %s may be a WFS drive (Dragon Quest X's), but this Wii's WFS key is not available, so it is "
             "not read\n",
             which.c_str());
        return false;
    }
    WfsVolume volume;
    std::string error;
    const bool opened = volume.open(reader, 0, sectors, key, error);
    std::memset(key, 0, sizeof key);
    if (!opened) {
        logf("WFS: %s is not this Wii's WFS drive: %s\n", which.c_str(), error.c_str());
        return false;
    }
    logf("WFS: %s is this Wii's WFS drive: version %08x, device type %04x, %u sectors, %u-KiB blocks\n",
         which.c_str(), static_cast<unsigned>(volume.version()), static_cast<unsigned>(volume.device_type()),
         static_cast<unsigned>(volume.sectors()), static_cast<unsigned>((1u << volume.root_block_log2()) / 1024));
    std::vector<WfsTitle> titles;
    std::vector<std::string> notes;
    if (!find_wfs_titles(volume, titles, notes, error)) logf("WFS: nothing installed is listed: %s\n", error.c_str());
    for (const std::string& note : notes) logf("WFS: %s\n", note.c_str());
    for (const WfsTitle& t : titles) {
        std::string add_ons;
        for (std::uint64_t id : t.add_ons) add_ons += (add_ons.empty() ? "" : ", ") + wfs_title_text(id);
        logf("WFS: %s at %s: version %u, IOS%u, %s; %s%s%s\n", wfs_title_text(t.title_id).c_str(), t.path.c_str(),
             static_cast<unsigned>(t.version), static_cast<unsigned>(t.ios),
             t.wfs_type ? "a WFS title" : "its TMD is not a WFS title's",
             t.program ? ("its program is there (" + std::to_string(t.program_bytes) + " bytes)").c_str()
                       : "its program (content/default.dol) is missing: the install did not finish",
             add_ons.empty() ? "" : "; add-ons: ", add_ons.c_str());
    }
    Locked lock;
    g_found = true;
    g_titles = std::move(titles);
    return true;
}

void ForgetWfsDrive() {
    Locked lock;
    g_found = false;
    g_titles.clear();
}

bool WfsDriveFound() {
    Locked lock;
    return g_found;
}

WfsInstall WfsInstallOf(std::uint64_t title_id, std::string& detail) {
    Locked lock;
    if (!g_found) {
        detail = "no WFS drive was read at the last USB scan";
        return WfsInstall::Unchecked;
    }
    for (const WfsTitle& t : g_titles) {
        if (t.title_id != title_id) continue;
        if (!t.program) {
            detail = "its install on the WFS drive did not finish (" + t.path + "/content/default.dol is missing)";
            return WfsInstall::Missing;
        }
        detail = "installed on the WFS drive at " + t.path + ", version " + std::to_string(t.version);
        return WfsInstall::Ready;
    }
    detail = "the WFS drive plugged in does not hold it";
    return WfsInstall::Missing;
}

std::uint64_t WfsTitleForDisc(const std::string& disc_id) {
    Locked lock;
    if (!g_found) return 0;
    const int index = wfs_title_for_disc(disc_id, g_titles);
    return index < 0 ? 0 : g_titles[static_cast<std::size_t>(index)].title_id;
}

}  // namespace riftwii::wii
