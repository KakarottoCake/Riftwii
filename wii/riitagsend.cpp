// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2011 Dimok
// SPDX-FileCopyrightText: USB Loader GX contributors <https://github.com/wiidev/usbloadergx>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riitagsend.hpp"

#include <cstdio>
#include <vector>

#include "loadersettings.hpp"
#include "log.hpp"
#include "netsock.hpp"
#include "online.hpp"
#include "riftwii/riitag.hpp"

namespace riftwii::wii {
namespace {

// Where a Wiinnertag.xml may already be: RiftWii's folder, then USB
// Loader GX's (its default WiinnertagPath), so a tag set up for GX works
// here as it is.
const char* const kTagFiles[] = {"sd:/riftwii/Wiinnertag.xml", "sd:/apps/usbloader_gx/Wiinnertag.xml"};

std::vector<std::string> g_urls;  // for Send, on the network's thread

bool ReadSmall(const char* path, std::string& text) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    char buf[1024];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0 && text.size() < 16384) text.append(buf, n);
    std::fclose(f);
    return true;
}

// "https://riitag.t0g3pii.de/...": the host, for the log (never the key).
std::string HostOf(const std::string& url) {
    const std::size_t from = url.find("://");
    if (from == std::string::npos) return url;
    const std::size_t end = url.find('/', from + 3);
    return url.substr(from + 3, end == std::string::npos ? std::string::npos : end - from - 3);
}

void Send() {
    for (const std::string& url : g_urls) {
        std::vector<std::uint8_t> answer;
        std::string error;
        if (HttpGet(url, answer, error, 4096, 6000)) {
            logf("RiiTag: %s updated\n", HostOf(url).c_str());
        } else {
            logf("RiiTag: %s: %s\n", HostOf(url).c_str(), error.c_str());
        }
    }
}

}  // namespace

void TagGame(const std::string& game_id, bool background) {
    if (!Settings().online) return;
    std::vector<RiiTagEntry> entries;
    for (const char* path : kTagFiles) {
        std::string text;
        if (!ReadSmall(path, text)) continue;
        const std::vector<RiiTagEntry> found = parse_wiinnertag(text);
        if (found.empty()) logf("RiiTag: %s has no <Tag URL=... Key=...>\n", path);
        entries.insert(entries.end(), found.begin(), found.end());
    }
    if (!Settings().riitag_key.empty()) entries.push_back({kRiiTagUrl, Settings().riitag_key});
    g_urls.clear();
    for (const RiiTagEntry& e : entries) {
        const std::string url = riitag_url(e, game_id);
        bool seen = url.empty();
        for (const std::string& u : g_urls) seen = seen || u == url;
        if (!seen) g_urls.push_back(url);
    }
    if (g_urls.empty()) return;
    logf("RiiTag: %s to %u server(s)\n", game_id.c_str(), static_cast<unsigned>(g_urls.size()));
    // On the network's thread: Start answers at once, and leaving the menu
    // waits for it (wii/main.cpp) while the network is still up.
    if (background) {
        NetWaitForBackground();  // a cover download, say: seconds at most
        if (NetRunInBackground(Send)) return;
    }
    Send();
}

}  // namespace riftwii::wii
