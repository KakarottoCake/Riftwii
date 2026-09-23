// SPDX-License-Identifier: GPL-3.0-or-later
#include "netpacks.hpp"

#include <dirent.h>
#include <fat.h>
#include <ogc/lwp_watchdog.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

#include "d2xsd.hpp"
#include "frontend.hpp"
#include "log.hpp"
#include "netsock.hpp"
#include "riftwii/riifs_sync.hpp"

namespace riftwii::wii {
namespace {

constexpr const char* kSettingPath = "sd:/riftwii/network.txt";
constexpr int kConnectMs = 4000;
constexpr int kDiscoverMs = 1500;
bool g_force = false;
std::string g_synced;  // the selection the last successful copy was for

// The card through libfat, for the copy.
class CardStore final : public riifs::LocalStore {
public:
    ~CardStore() override {
        if (file_) std::fclose(file_);
    }
    bool file_size(const std::string& path, std::uint64_t& size, bool& exists) override {
        struct stat st;
        exists = stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
        size = exists ? static_cast<std::uint64_t>(st.st_size) : 0;
        return true;
    }
    bool list(const std::string& dir, std::vector<riifs::LocalEntry>& out, bool& exists) override {
        out.clear();
        DIR* d = opendir(dir.c_str());
        exists = d != nullptr;
        if (!d) return true;
        while (const dirent* e = readdir(d)) {
            if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
            riifs::LocalEntry x;
            x.name = e->d_name;
            if (e->d_type == DT_UNKNOWN) {
                struct stat st;
                x.is_directory = stat((dir + "/" + x.name).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
            } else {
                x.is_directory = e->d_type == DT_DIR;
            }
            out.push_back(std::move(x));
        }
        closedir(d);
        return true;
    }
    bool make_dirs(const std::string& dir) override {
        std::string::size_type pos = dir.find(":/");
        if (pos == std::string::npos) return false;
        pos += 2;
        for (;;) {
            pos = dir.find('/', pos);
            const std::string part = pos == std::string::npos ? dir : dir.substr(0, pos);
            struct stat st;
            if (stat(part.c_str(), &st) != 0 && mkdir(part.c_str(), 0777) != 0) return false;
            if (pos == std::string::npos) return true;
            ++pos;
        }
    }
    bool begin_file(const std::string& path) override {
        if (file_) std::fclose(file_);
        path_ = path;
        file_ = std::fopen(path.c_str(), "wb");
        if (file_) std::setvbuf(file_, nullptr, _IOFBF, 0x10000);
        return file_ != nullptr;
    }
    bool write(const std::uint8_t* data, std::size_t length) override {
        return file_ && std::fwrite(data, 1, length, file_) == length;
    }
    bool end_file(bool keep) override {
        if (!file_) return false;
        const bool closed = std::fclose(file_) == 0;
        file_ = nullptr;
        if (!keep || !closed) unlink(path_.c_str());
        return closed;
    }
    bool remove(const std::string& path) override {
        struct stat st;
        if (stat(path.c_str(), &st) != 0) return true;
        if (!S_ISDIR(st.st_mode)) return unlink(path.c_str()) == 0;
        std::vector<riifs::LocalEntry> entries;
        bool exists = false;
        list(path, entries, exists);
        for (const riifs::LocalEntry& e : entries) {
            if (!remove(path + "/" + e.name)) return false;
        }
        return rmdir(path.c_str()) == 0;
    }

private:
    FILE* file_ = nullptr;
    std::string path_;
};

std::string read_text(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    if (in) text << in.rdbuf();
    return text.str();
}

// The .xml files directly in `dir`, sorted, hidden ones (macOS "._x.xml")
// and folders left out.
std::vector<std::string> xml_names(const std::string& dir, std::size_t limit, bool& limited) {
    std::vector<std::string> names;
    DIR* d = opendir(dir.c_str());
    if (!d) return names;
    while (const dirent* e = readdir(d)) {
        if (e->d_name[0] == '.' || e->d_type == DT_DIR) continue;
        const char* dot = std::strrchr(e->d_name, '.');
        if (!dot || strcasecmp(dot, ".xml") != 0) continue;
        if (names.size() >= limit) {
            limited = true;
            break;
        }
        names.push_back(e->d_name);
    }
    closedir(d);
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<NetServer> cached_servers() {
    std::vector<NetServer> out;
    DIR* d = opendir(kNetCacheDir);
    if (!d) return out;
    while (const dirent* e = readdir(d)) {
        NetServer s;
        if (ParseServerFolder(e->d_name, s)) out.push_back(s);
    }
    closedir(d);
    return out;
}

std::string megabytes(std::uint64_t bytes) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return buf;
}

// Libfat writes back lazily; the compile reads the card raw. Unmounting
// flushes everything, as prepare_savegame does for its folder.
bool remount_card() {
    LogClose();
    fatUnmount("sd:");
    const bool mounted = fatMountSimple("sd", sd_interface());
    LogReopen();
    return mounted;
}

bool connect_server(SocketTransport& transport, riifs::Client& client, const NetServer& server, std::string& error) {
    if (!transport.connect(server, kConnectMs, error)) return false;
    if (!client.handshake(error)) {
        transport.close();
        return false;
    }
    return true;
}

}  // namespace

std::vector<PackFile> ListPackFiles(std::size_t limit, bool& limited) {
    limited = false;
    std::vector<PackFile> out;
    for (const std::string& name : xml_names(kPackageDir, limit, limited)) {
        const std::string path = std::string(kPackageDir) + "/" + name;
        // An XML that only names a server (Riivolution's way of pointing
        // at one) is not a pack; with no <id> it would show for every game.
        const std::string text = read_text(path);
        if (text.find("<network") != std::string::npos) {
            Package package;
            std::string ignored;
            if (parse_package(text, package, ignored) && !package.networks.empty() && package.options.empty()) continue;
        }
        out.push_back({name, path});
    }
    for (const NetServer& server : cached_servers()) {
        const std::string dir = std::string(kNetCacheDir) + "/" + server.folder() + "/riivolution";
        for (const std::string& name : xml_names(dir, limit - std::min(limit, out.size()), limited)) {
            out.push_back({name + " @ " + server.label(), dir + "/" + name});
        }
    }
    return out;
}

std::string NetworkRootOf(const std::string& xml_sd_path) {
    const std::string prefix = std::string(kNetCacheDir) + "/";
    if (xml_sd_path.compare(0, prefix.size(), prefix) != 0) return "";
    const std::size_t end = xml_sd_path.find('/', prefix.size());
    if (end == std::string::npos) return "";
    return xml_sd_path.substr(3, end - 3);  // without "sd:"
}

void RebasePlan(Plan& plan, const std::string& root) {
    const auto rebase = [&](std::string& path) {
        if (path.empty()) return;
        path = root + (path[0] == '/' ? "" : "/") + path;
    };
    for (FilePatch& f : plan.files) rebase(f.external);
    for (FolderPatch& f : plan.folders) rebase(f.external);
    for (MemoryPatch& m : plan.memory) rebase(m.valuefile);
    for (SavegamePatch& s : plan.savegames) rebase(s.external);
}

bool NetworkPacksEnabled() {
    std::string text = read_text(kSettingPath);
    return text.compare(0, 2, "on") == 0;
}

void SetNetworkPacksEnabled(bool on) {
    mkdir("sd:/riftwii", 0777);
    FILE* f = std::fopen(kSettingPath, "wb");
    if (!f) return;
    std::fputs(on ? "on\n" : "off\n", f);
    std::fclose(f);
}

void ForceNextSync() {
    g_force = true;
    g_synced.clear();
}

std::string RefreshNetworkPacks(const std::function<void(const char*)>& busy) {
    g_synced.clear();
    // Which servers: those the card's XMLs name, and any on the network
    // when the setting is on or an XML leaves the address out.
    bool discover = NetworkPacksEnabled();
    std::vector<std::pair<std::string, std::uint16_t>> named;
    bool limited = false;
    for (const std::string& name : xml_names(kPackageDir, 256, limited)) {
        const std::string text = read_text(std::string(kPackageDir) + "/" + name);
        if (text.find("<network") == std::string::npos) continue;
        Package package;
        std::string error;
        if (!parse_package(text, package, error)) continue;
        for (const NetworkServer& s : package.networks) {
            if (s.address.empty()) {
                discover = true;
            } else {
                named.push_back({s.address, s.port});
            }
        }
    }
    if (named.empty() && !discover) return "";

    if (busy) busy("Looking for network packs...");
    std::string error;
    if (!NetStart(error)) {
        logf("Network packs: %s\n", error.c_str());
        return "Network packs: " + error;
    }
    std::vector<NetServer> servers;
    const auto add = [&](const NetServer& s) {
        for (const NetServer& x : servers) {
            if (x.ip == s.ip && x.port == s.port) return;
        }
        servers.push_back(s);
    };
    for (const auto& n : named) {
        NetServer s;
        if (ResolveServer(n.first, n.second, s, error)) {
            add(s);
        } else {
            logf("Network packs: %s\n", error.c_str());
        }
    }
    if (discover) {
        for (const NetServer& s : DiscoverServers(riifs::kDefaultPort, kDiscoverMs)) add(s);
    }
    std::size_t packs = 0, reached = 0;
    std::string last_error;
    CardStore card;
    for (const NetServer& server : servers) {
        SocketTransport transport;
        riifs::Client client(transport);
        if (!connect_server(transport, client, server, error)) {
            logf("Network packs: %s: %s\n", server.label().c_str(), error.c_str());
            last_error = error;
            continue;
        }
        riifs::SyncItem list;
        list.path = "/riivolution";
        list.folder = true;
        list.recursive = false;
        list.suffix = ".xml";
        riifs::SyncOptions options;
        options.force = true;  // small, and a pack list must never be stale
        riifs::SyncStats stats;
        const std::string root = std::string(kNetCacheDir) + "/" + server.folder();
        if (!riifs::sync(client, card, root, {list}, options, stats, error)) {
            logf("Network packs: %s: %s\n", server.label().c_str(), error.c_str());
            last_error = error;
            continue;
        }
        client.goodbye();
        ++reached;
        packs += stats.checked;
        logf("Network packs: %s: %u pack(s)\n", server.label().c_str(), static_cast<unsigned>(stats.checked));
    }
    NetStop();
    if (servers.empty()) return "Network packs: no RiiFS server answered";
    if (reached == 0) return "Network packs: " + last_error;
    return "Network packs: " + std::to_string(packs) + " from " + std::to_string(reached) + " PC(s)";
}

bool SyncNetworkPackages(const std::vector<PackageChoices>& packages, const DiscProbe& probe,
                         std::vector<std::string>& warnings, std::string& error) {
    // What each server must provide: the chosen options' files, as the
    // server names them. The save folders are the card's, never replaced.
    std::map<std::string, std::vector<riifs::SyncItem>> items;
    std::map<std::string, std::vector<std::string>> keep;
    const DiscIdentity disc = probe.header.identity();
    std::string signature = disc.id;
    for (const PackageChoices& selection : packages) {
        const std::string root = NetworkRootOf(selection.xml_sd_path);
        if (root.empty()) continue;
        signature += "\n" + selection.xml_sd_path;
        for (const auto& c : selection.choices) signature += "\t" + c.first + "=" + c.second;
        Package package;
        if (!parse_package(read_text(selection.xml_sd_path), package, error)) {
            error = selection.xml_sd_path + ": " + error;
            return false;
        }
        for (const auto& c : selection.choices) {
            if (!select_choice(package, c.first, c.second, error)) {
                error = selection.xml_sd_path + ": " + error;
                return false;
            }
        }
        PlanOptions allowed;
        allowed.allow_filename_targets = true;
        allowed.allow_folders = true;
        allowed.allow_memory = true;
        allowed.allow_savegames = true;
        Plan plan;
        if (!plan_package(package, disc, allowed, plan, error)) return false;
        std::vector<riifs::SyncItem>& list = items[root];
        for (const FilePatch& f : plan.files) {
            if (f.external.empty()) continue;
            riifs::SyncItem item;
            item.path = f.external;
            list.push_back(item);
        }
        for (const FolderPatch& f : plan.folders) {
            if (f.external.empty()) continue;
            riifs::SyncItem item;
            item.path = f.external;
            item.folder = true;
            item.recursive = f.recursive;
            list.push_back(item);
        }
        for (const MemoryPatch& m : plan.memory) {
            if (m.valuefile.empty()) continue;
            riifs::SyncItem item;
            item.path = m.valuefile;
            list.push_back(item);
        }
        for (const SavegamePatch& s : plan.savegames) {
            // Copied from the PC once, when the card has no save yet.
            keep[root].push_back(s.external);
            riifs::SyncItem item;
            item.path = s.external;
            item.folder = true;
            list.push_back(item);
        }
    }
    if (items.empty()) return true;
    if (!g_force && signature == g_synced) {
        logf("RiiFS: copied for this selection a moment ago; not again\n");
        return true;
    }
    g_synced.clear();

    const bool force = g_force;
    g_force = false;
    if (!NetStart(error)) {
        warnings.push_back("RiiFS: " + error + "; the files copied last time are used");
        logf("RiiFS: %s; the files copied last time are used\n", error.c_str());
        error.clear();
        return true;
    }
    CardStore card;
    bool ok = true;
    for (const auto& entry : items) {
        NetServer server;
        const std::string folder = entry.first.substr(entry.first.find_last_of('/') + 1);
        if (!ParseServerFolder(folder, server)) continue;
        SocketTransport transport;
        riifs::Client client(transport);
        if (!connect_server(transport, client, server, error)) {
            const std::string note = "RiiFS: " + server.label() + " not reachable (" + error +
                                     "); the files copied last time are used";
            warnings.push_back(note);
            logf("%s\n", note.c_str());
            error.clear();
            continue;
        }
        logf("RiiFS: %s: checking %u item(s)%s\n", server.label().c_str(), static_cast<unsigned>(entry.second.size()),
             force ? ", copying everything again" : "");
        riifs::SyncOptions options;
        options.force = force;
        options.keep = keep[entry.first];
        u64 last = gettime();
        options.progress = [&](const riifs::SyncStats& s, const std::string&) {
            const u64 now = gettime();
            if (ticks_to_millisecs(diff_ticks(last, now)) >= 2000) {
                last = now;
                logf("RiiFS: %u checked, %u copied (%s)\n", s.checked, s.copied, megabytes(s.bytes).c_str());
            }
            return true;
        };
        riifs::SyncStats stats;
        if (!riifs::sync(client, card, "sd:" + entry.first, entry.second, options, stats, error)) {
            ok = false;
            break;
        }
        client.goodbye();
        logf("RiiFS: %s: %u file(s) checked, %u copied (%s), %u removed, %u missing on the PC\n",
             server.label().c_str(), stats.checked, stats.copied, megabytes(stats.bytes).c_str(), stats.removed,
             stats.missing);
    }
    NetStop();
    if (!remount_card() && ok) {
        error = "cannot mount the SD card again after the RiiFS copy";
        return false;
    }
    if (ok) g_synced = signature;
    return ok;
}

}  // namespace riftwii::wii
