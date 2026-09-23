// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/riifs_sync.hpp"

#include <set>

namespace riftwii::riifs {
namespace {

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

std::string folded(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = lower(c);
    return out;
}

std::string normal(const std::string& path) {
    std::string out;
    for (char c : path) out += c == '\\' ? '/' : c;
    if (out.empty() || out[0] != '/') out = "/" + out;
    while (out.size() > 1 && out.back() == '/') out.pop_back();
    return out;
}

std::string parent_of(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos || slash == 0 ? std::string() : path.substr(0, slash);
}

bool ends_with(const std::string& name, const std::string& suffix) {
    if (suffix.empty()) return true;
    if (name.size() < suffix.size()) return false;
    return folded(name.substr(name.size() - suffix.size())) == folded(suffix);
}

// FAT on the card compares names without case; a server on Linux may not.
// Comparing folded keeps a copy from being taken for a stale file.
class Syncer {
public:
    Syncer(Client& client, LocalStore& local, const std::string& root, const SyncOptions& options,
           SyncStats& stats, std::string& error)
        : client_(client), local_(local), root_(root), options_(options), stats_(stats), error_(error) {
        for (const std::string& k : options.keep) keep_.push_back(folded(normal(k)));
    }

    bool file(const std::string& path, const Stat* known) {
        if ((!seeding_ && kept(path)) || !done_.insert(folded(path)).second) return true;
        Stat st;
        if (known) {
            st = *known;
        } else {
            bool missing = false;
            if (!client_.stat(path, st, missing, error_)) {
                if (!missing) return false;
                ++stats_.missing;
                return drop(path);
            }
        }
        if (st.is_directory()) {
            ++stats_.missing;  // a folder where a file was named; the pack reports it
            return true;
        }
        ++stats_.checked;
        const std::string target = root_ + path;
        std::uint64_t size = 0;
        bool exists = false;
        if (!options_.force && local_.file_size(target, size, exists) && exists && size == st.size) {
            return report(path);
        }
        const std::string dir = parent_of(target);
        if (!dir.empty() && !local_.make_dirs(dir)) {
            error_ = "RiiFS: cannot create " + dir;
            return false;
        }
        if (!local_.begin_file(target)) {
            error_ = "RiiFS: cannot write " + target;
            return false;
        }
        std::uint64_t written = 0;
        const bool ok = client_.fetch(path, st.size, [&](const std::uint8_t* data, std::size_t n) {
            written += n;
            return local_.write(data, n);
        }, error_);
        if (!local_.end_file(ok)) {
            if (ok) error_ = "RiiFS: cannot finish writing " + target;
            return false;
        }
        if (!ok) return false;
        ++stats_.copied;
        stats_.bytes += written;
        return report(path);
    }

    bool folder(const std::string& path, bool recursive, const std::string& suffix) {
        if (!seeding_ && kept(path)) {
            // A save folder: the card's copy wins once it exists.
            std::vector<LocalEntry> held;
            bool exists = false;
            if (!local_.list(root_ + path, held, exists)) return false;
            if (exists) return true;
            seeding_ = true;
            const bool ok = folder(path, true, std::string());
            seeding_ = false;
            if (ok && !local_.make_dirs(root_ + path)) {
                error_ = "RiiFS: cannot create " + root_ + path;
                return false;
            }
            return ok;
        }
        std::vector<DirEntry> remote;
        bool missing = false;
        if (!client_.list(path, remote, missing, error_)) {
            if (!missing) return false;
            if (seeding_) return true;  // no save on the PC: the game starts one
            ++stats_.missing;
            return drop(path);
        }
        std::set<std::string> present;
        for (const DirEntry& e : remote) {
            present.insert(folded(e.name));
            const std::string child = join_path(path == "/" ? std::string() : path, e.name);
            if (e.stat.is_directory()) {
                if (recursive && !folder(child, true, suffix)) return false;
            } else if (ends_with(e.name, suffix)) {
                if (!file(child, &e.stat)) return false;
            }
        }
        // What the card holds here that the server no longer has.
        std::vector<LocalEntry> held;
        bool exists = false;
        if (!local_.list(root_ + path, held, exists) || !exists) return true;
        for (const LocalEntry& e : held) {
            if (present.count(folded(e.name))) continue;
            if (e.is_directory ? !recursive : !ends_with(e.name, suffix)) continue;
            if (!drop(join_path(path == "/" ? std::string() : path, e.name))) return false;
        }
        return true;
    }

private:
    bool kept(const std::string& path) const {
        const std::string f = folded(path);
        for (const std::string& k : keep_) {
            if (f == k || (f.size() > k.size() && f.compare(0, k.size(), k) == 0 && f[k.size()] == '/')) return true;
        }
        return false;
    }

    // Removes the local copy of a server path the server does not have.
    bool drop(const std::string& path) {
        if (kept(path)) return true;
        const std::string target = root_ + path;
        std::uint64_t size = 0;
        bool exists = false;
        std::vector<LocalEntry> ignored;
        bool dir_exists = false;
        const bool file_there = local_.file_size(target, size, exists) && exists;
        const bool dir_there = !file_there && local_.list(target, ignored, dir_exists) && dir_exists;
        if (!file_there && !dir_there) return true;
        if (!local_.remove(target)) {
            error_ = "RiiFS: cannot remove the stale copy " + target;
            return false;
        }
        ++stats_.removed;
        return true;
    }

    bool report(const std::string& path) {
        if (options_.progress && !options_.progress(stats_, path)) {
            error_ = "RiiFS: copy stopped";
            return false;
        }
        return true;
    }

    Client& client_;
    LocalStore& local_;
    std::string root_;
    const SyncOptions& options_;
    SyncStats& stats_;
    std::string& error_;
    std::vector<std::string> keep_;
    std::set<std::string> done_;
    bool seeding_ = false;
};

}  // namespace

bool sync(Client& client, LocalStore& local, const std::string& local_root, const std::vector<SyncItem>& items,
          const SyncOptions& options, SyncStats& stats, std::string& error) {
    std::string root = local_root;
    while (!root.empty() && root.back() == '/') root.pop_back();
    Syncer syncer(client, local, root, options, stats, error);
    for (const SyncItem& item : items) {
        const std::string path = normal(item.path);
        const bool ok = item.folder ? syncer.folder(path, item.recursive, item.suffix) : syncer.file(path, nullptr);
        if (!ok) return false;
    }
    return true;
}

}  // namespace riftwii::riifs
