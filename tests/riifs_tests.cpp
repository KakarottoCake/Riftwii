// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/riifs.hpp"
#include "riftwii/riifs_sync.hpp"

#include <cstring>
#include <deque>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using riftwii::riifs::Client;

namespace {

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

void put64(std::vector<std::uint8_t>& out, std::uint64_t v) {
    put32(out, static_cast<std::uint32_t>(v >> 32));
    put32(out, static_cast<std::uint32_t>(v));
}

std::uint32_t get32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

// A RiiFS server in memory, answering the way the published servers do:
// option words buffered until a command, stats as 24 bytes before the
// result word, reads padded to the requested length, directory names in a
// 1024-byte field, "." and ".." listed first. Paths are case-sensitive.
class FakeServer final : public riftwii::riifs::Transport {
public:
    std::map<std::string, std::vector<std::uint8_t>> files;
    std::set<std::string> extra_dirs;  // empty folders
    // A hostile server: names listed in a folder as they are, files of one
    // byte, whatever they hold.
    std::map<std::string, std::vector<std::string>> rogue_names;
    // Every folder under this one lists a folder of its own, as a symlink
    // to a parent does on a PC.
    std::string loop;
    std::size_t listing_extra = 0;  // this many more files in "/big"
    std::int32_t version = 4;
    bool broken = false;  // drop the connection
    std::uint32_t reads = 0;
    std::vector<std::uint32_t> commands;

    bool send(const void* data, std::size_t length) override {
        if (broken) return false;
        const auto* p = static_cast<const std::uint8_t*>(data);
        in_.insert(in_.end(), p, p + length);
        pump();
        return true;
    }
    bool receive(void* data, std::size_t length) override {
        if (broken || out_.size() < length) return false;
        std::copy(out_.begin(), out_.begin() + static_cast<std::ptrdiff_t>(length), static_cast<std::uint8_t*>(data));
        out_.erase(out_.begin(), out_.begin() + static_cast<std::ptrdiff_t>(length));
        return true;
    }
    bool is_dir(const std::string& path) const {
        if (path == "/" || extra_dirs.count(path)) return true;
        if (!loop.empty() && path.compare(0, loop.size(), loop) == 0) return true;
        if (listing_extra && path == "/big") return true;
        const std::string prefix = path + "/";
        for (const auto& f : files) {
            if (f.first.compare(0, prefix.size(), prefix) == 0) return true;
        }
        return false;
    }

private:
    void pump() {
        for (;;) {
            if (in_.size() < 8) return;
            const std::uint32_t action = get32(in_.data());
            if (action == 1) {
                if (in_.size() < 12) return;
                const std::uint32_t option = get32(in_.data() + 4), length = get32(in_.data() + 8);
                if (in_.size() < 12 + length) return;
                options_[option].assign(in_.begin() + 12, in_.begin() + 12 + length);
                in_.erase(in_.begin(), in_.begin() + 12 + length);
            } else {
                const std::uint32_t command = get32(in_.data() + 4);
                in_.erase(in_.begin(), in_.begin() + 8);
                commands.push_back(command);
                run(command);
            }
        }
    }
    std::string text(std::uint32_t option) {
        const auto& v = options_[option];
        return std::string(v.begin(), v.end());
    }
    std::uint32_t word(std::uint32_t option) {
        const auto& v = options_[option];
        return v.size() >= 4 ? get32(v.data()) : 0xFFFFFFFFu;
    }
    void result(std::int32_t v) {
        std::vector<std::uint8_t> b;
        put32(b, static_cast<std::uint32_t>(v));
        out_.insert(out_.end(), b.begin(), b.end());
    }
    void stat_bytes(std::uint64_t size, std::uint32_t mode) {
        std::vector<std::uint8_t> b;
        put64(b, 7);
        put64(b, size);
        put32(b, 0);
        put32(b, mode);
        out_.insert(out_.end(), b.begin(), b.end());
    }
    void run(std::uint32_t command) {
        switch (command) {
        case 0x00:
            result(text(0x00) == "1.03" ? version : -1);
            break;
        case 0x01:
            result(1);
            break;
        case 0x17: {  // stat
            const std::string path = text(0x02);
            if (files.count(path)) {
                stat_bytes(files[path].size(), 0x8000);
                result(0);
            } else if (is_dir(path)) {
                stat_bytes(0, 0xC000);
                result(0);
            } else {
                stat_bytes(0, 0);
                result(-1);
            }
            break;
        }
        case 0x10: {  // open
            const std::string path = text(0x02);
            if (!files.count(path)) {
                result(-1);
                break;
            }
            open_[next_fd_] = {path, 0};
            result(next_fd_++);
            break;
        }
        case 0x11: {  // read
            ++reads;
            const std::uint32_t fd = word(0x01), length = word(0x04);
            std::size_t got = 0;
            if (open_.count(fd)) {
                auto& o = open_[fd];
                const auto& data = files[o.first];
                got = std::min<std::size_t>(length, data.size() - o.second);
                out_.insert(out_.end(), data.begin() + o.second, data.begin() + o.second + got);
                o.second += got;
            }
            out_.insert(out_.end(), length - got, 0);
            result(static_cast<std::int32_t>(got));
            break;
        }
        case 0x16:
            result(open_.erase(word(0x01)) ? 1 : 0);
            break;
        case 0x21: {  // open dir
            const std::string path = text(0x02);
            if (!is_dir(path)) {
                result(-1);
                break;
            }
            std::vector<std::pair<std::string, std::pair<std::uint64_t, std::uint32_t>>> list;
            list.push_back({".", {0, 0xC000}});
            list.push_back({"..", {0, 0xC000}});
            const std::string prefix = path == "/" ? "/" : path + "/";
            std::set<std::string> seen;
            auto add_child = [&](const std::string& full) {
                if (full.compare(0, prefix.size(), prefix) != 0 || full.size() == prefix.size()) return;
                const std::string rest = full.substr(prefix.size());
                const std::size_t slash = rest.find('/');
                const std::string name = rest.substr(0, slash);
                if (!seen.insert(name).second) return;
                if (slash == std::string::npos && files.count(full)) {
                    list.push_back({name, {files[full].size(), 0x8000}});
                } else {
                    list.push_back({name, {0, 0xC000}});
                }
            };
            for (const auto& f : files) add_child(f.first);
            for (const auto& d : extra_dirs) add_child(d);
            if (rogue_names.count(path)) {
                for (const std::string& n : rogue_names[path]) list.push_back({n, {1, 0x8000}});
            }
            if (!loop.empty() && path.compare(0, loop.size(), loop) == 0) list.push_back({"again", {0, 0xC000}});
            if (listing_extra && path == "/big") {
                for (std::size_t i = 0; i < listing_extra; ++i) list.push_back({"f" + std::to_string(i), {0, 0x8000}});
            }
            dirs_[next_fd_] = {list, 0};
            result(next_fd_++);
            break;
        }
        case 0x22:
            result(dirs_.erase(word(0x01)) ? 1 : -1);
            break;
        case 0x23: {  // next name
            std::vector<std::uint8_t> name(1024, 0);
            const std::uint32_t fd = word(0x01);
            if (!dirs_.count(fd) || dirs_[fd].second >= dirs_[fd].first.size()) {
                out_.insert(out_.end(), name.begin(), name.end());
                result(-1);
                break;
            }
            const std::string& n = dirs_[fd].first[dirs_[fd].second].first;
            std::memcpy(name.data(), n.data(), n.size());
            out_.insert(out_.end(), name.begin(), name.end());
            result(static_cast<std::int32_t>(n.size()));
            break;
        }
        case 0x24: {  // next stat
            const std::uint32_t fd = word(0x01);
            auto& d = dirs_[fd];
            const auto& e = d.first[d.second++].second;
            stat_bytes(e.first, e.second);
            result(0);
            break;
        }
        default:
            break;  // unknown commands get no answer, as on the C server
        }
    }

    std::vector<std::uint8_t> in_;
    std::deque<std::uint8_t> out_;
    std::map<std::uint32_t, std::vector<std::uint8_t>> options_;
    std::int32_t next_fd_ = 1;
    std::map<std::int32_t, std::pair<std::string, std::size_t>> open_;
    std::map<std::int32_t, std::pair<std::vector<std::pair<std::string, std::pair<std::uint64_t, std::uint32_t>>>, std::size_t>>
        dirs_;
};

// The card, in memory: FAT-like, names compared without case.
class FakeCard final : public riftwii::riifs::LocalStore {
public:
    std::map<std::string, std::vector<std::uint8_t>> files;  // folded path -> bytes
    std::set<std::string> dirs;                              // folded paths
    std::uint32_t writes = 0;

    static std::string fold(std::string s) {
        for (char& c : s) c = (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
        return s;
    }
    bool file_size(const std::string& path, std::uint64_t& size, bool& exists) override {
        const auto f = files.find(fold(path));
        exists = f != files.end();
        size = exists ? f->second.size() : 0;
        return true;
    }
    bool list(const std::string& dir, std::vector<riftwii::riifs::LocalEntry>& out, bool& exists) override {
        const std::string d = fold(dir);
        exists = dirs.count(d) != 0;
        out.clear();
        if (!exists) return true;
        const std::string prefix = d + "/";
        std::set<std::string> seen;
        auto add = [&](const std::string& full, bool is_dir) {
            if (full.compare(0, prefix.size(), prefix) != 0) return;
            const std::string rest = full.substr(prefix.size());
            if (rest.empty() || rest.find('/') != std::string::npos) return;
            if (seen.insert(rest).second) out.push_back({rest, is_dir});
        };
        for (const auto& f : files) add(f.first, false);
        for (const auto& x : dirs) add(x, true);
        return true;
    }
    bool make_dirs(const std::string& dir) override {
        std::string d = fold(dir);
        while (!d.empty()) {
            dirs.insert(d);
            const std::size_t slash = d.find_last_of('/');
            if (slash == std::string::npos || slash == 0) break;
            d = d.substr(0, slash);
        }
        return true;
    }
    bool begin_file(const std::string& path) override {
        open_ = fold(path);
        files[open_].clear();
        ++writes;
        return true;
    }
    bool write(const std::uint8_t* data, std::size_t length) override {
        files[open_].insert(files[open_].end(), data, data + length);
        return true;
    }
    bool end_file(bool keep) override {
        if (!keep) files.erase(open_);
        return true;
    }
    bool remove(const std::string& path) override {
        const std::string p = fold(path);
        const std::string prefix = p + "/";
        files.erase(p);
        for (auto it = files.begin(); it != files.end();) {
            it = it->first.compare(0, prefix.size(), prefix) == 0 ? files.erase(it) : std::next(it);
        }
        for (auto it = dirs.begin(); it != dirs.end();) {
            it = (*it == p || it->compare(0, prefix.size(), prefix) == 0) ? dirs.erase(it) : std::next(it);
        }
        return true;
    }
    std::string text(const std::string& path) {
        const auto& v = files[fold(path)];
        return std::string(v.begin(), v.end());
    }
    bool has(const std::string& path) { return files.count(fold(path)) != 0; }

private:
    std::string open_;
};

std::vector<std::uint8_t> bytes(const std::string& s) { return std::vector<std::uint8_t>(s.begin(), s.end()); }

}  // namespace

static void TestClient() {
    FakeServer server;
    server.files["/riivolution/mod.xml"] = bytes("<wiidisc/>");
    server.files["/riivolution/Mod/a.arc"] = bytes("0123456789abcdefghij");  // 20 bytes
    server.extra_dirs.insert("/riivolution/Mod/Empty");
    Client client(server);
    std::string error;
    EXPECT_TRUE(client.handshake(error));

    riftwii::riifs::Stat st;
    bool missing = false;
    EXPECT_TRUE(client.stat("/riivolution/Mod/a.arc", st, missing, error));
    EXPECT_EQ(st.size, 20u);
    EXPECT_FALSE(st.is_directory());
    EXPECT_TRUE(client.stat("/riivolution/Mod", st, missing, error));
    EXPECT_TRUE(st.is_directory());
    EXPECT_FALSE(client.stat("/riivolution/nope.arc", st, missing, error));
    EXPECT_TRUE(missing);

    std::vector<riftwii::riifs::DirEntry> list;
    EXPECT_TRUE(client.list("/riivolution", list, missing, error));
    EXPECT_EQ(list.size(), 2u);  // "." and ".." left out
    EXPECT_EQ(list[0].name, "Mod");
    EXPECT_TRUE(list[0].stat.is_directory());
    EXPECT_EQ(list[1].name, "mod.xml");
    EXPECT_EQ(list[1].stat.size, 10u);
    EXPECT_FALSE(client.list("/nothing", list, missing, error));
    EXPECT_TRUE(missing);

    // A read in pieces: 7 + 7 + 6, then the probe past the end.
    std::string got;
    auto sink = [&](const std::uint8_t* d, std::size_t n) {
        got.append(reinterpret_cast<const char*>(d), n);
        return true;
    };
    EXPECT_TRUE(client.fetch("/riivolution/Mod/a.arc", 20, sink, error, 7));
    EXPECT_EQ(got, "0123456789abcdefghij");
    EXPECT_EQ(server.reads, 3u);
    // A file that changed since its stat fails the copy either way.
    got.clear();
    EXPECT_FALSE(client.fetch("/riivolution/Mod/a.arc", 19, sink, error, 7));
    got.clear();
    EXPECT_FALSE(client.fetch("/riivolution/Mod/a.arc", 21, sink, error, 7));
    // The stream stays in step after those: the next request works.
    EXPECT_TRUE(client.stat("/riivolution/mod.xml", st, missing, error));
    EXPECT_EQ(st.size, 10u);
    // An empty file.
    server.files["/e.bin"] = {};
    got.clear();
    EXPECT_TRUE(client.fetch("/e.bin", 0, sink, error));
    EXPECT_EQ(got, "");
    EXPECT_FALSE(client.fetch("/none.bin", 0, sink, error));
    client.goodbye();
    EXPECT_EQ(server.commands.back(), 0x01u);

    // An old or unknown server.
    FakeServer old;
    old.version = -1;
    Client c2(old);
    EXPECT_FALSE(c2.handshake(error));
    FakeServer gone;
    gone.broken = true;
    Client c3(gone);
    EXPECT_FALSE(c3.handshake(error));

    EXPECT_EQ(riftwii::riifs::join_path("/a\\b", "c"), "/a/b/c");
    EXPECT_EQ(riftwii::riifs::join_path("/a/", "c"), "/a/c");
}

static void TestSync() {
    FakeServer server;
    server.files["/riivolution/Pack.xml"] = bytes("<wiidisc/>");
    server.files["/riivolution/readme.txt"] = bytes("hi");
    server.files["/riivolution/Pack/Stage/A.arc"] = bytes("AAAA");
    server.files["/riivolution/Pack/Stage/Deep/B.arc"] = bytes("BBBBBB");
    server.files["/riivolution/Pack/single.bin"] = bytes("S");
    server.files["/riivolution/Pack/SaveGame/data.bin"] = bytes("server save");
    Client client(server);
    std::string error;
    EXPECT_TRUE(client.handshake(error));

    FakeCard card;
    const std::string root = "sd:/riftwii/riifs/pc_1137";
    std::vector<riftwii::riifs::SyncItem> items;
    riftwii::riifs::SyncItem folder;
    folder.path = "/riivolution/Pack/Stage";
    folder.folder = true;
    items.push_back(folder);
    riftwii::riifs::SyncItem one;
    one.path = "riivolution\\Pack\\single.bin";  // normalised
    items.push_back(one);
    riftwii::riifs::SyncOptions options;
    options.keep.push_back("/riivolution/Pack/SaveGame");
    riftwii::riifs::SyncStats stats;
    EXPECT_TRUE(riftwii::riifs::sync(client, card, root, items, options, stats, error));
    EXPECT_EQ(stats.copied, 3u);
    EXPECT_EQ(stats.bytes, 11u);
    EXPECT_EQ(card.text(root + "/riivolution/Pack/Stage/Deep/B.arc"), "BBBBBB");
    EXPECT_EQ(card.text(root + "/riivolution/pack/single.bin"), "S");

    // Unchanged: nothing copied again.
    stats = {};
    EXPECT_TRUE(riftwii::riifs::sync(client, card, root, items, options, stats, error));
    EXPECT_EQ(stats.checked, 3u);
    EXPECT_EQ(stats.copied, 0u);

    // A new size is noticed; the same size is not, unless forced.
    server.files["/riivolution/Pack/Stage/A.arc"] = bytes("AAAAA");
    server.files["/riivolution/Pack/single.bin"] = bytes("T");
    stats = {};
    EXPECT_TRUE(riftwii::riifs::sync(client, card, root, items, options, stats, error));
    EXPECT_EQ(stats.copied, 1u);
    EXPECT_EQ(card.text(root + "/riivolution/Pack/Stage/A.arc"), "AAAAA");
    EXPECT_EQ(card.text(root + "/riivolution/Pack/single.bin"), "S");
    options.force = true;
    stats = {};
    EXPECT_TRUE(riftwii::riifs::sync(client, card, root, items, options, stats, error));
    EXPECT_EQ(stats.copied, 3u);
    EXPECT_EQ(card.text(root + "/riivolution/Pack/single.bin"), "T");
    options.force = false;

    // A file the server dropped leaves the mirrored folder, subfolder too.
    server.files.erase("/riivolution/Pack/Stage/Deep/B.arc");
    stats = {};
    EXPECT_TRUE(riftwii::riifs::sync(client, card, root, items, options, stats, error));
    EXPECT_EQ(stats.removed, 1u);
    EXPECT_FALSE(card.has(root + "/riivolution/Pack/Stage/Deep/B.arc"));
    EXPECT_FALSE(card.dirs.count(FakeCard::fold(root + "/riivolution/Pack/Stage/Deep")));
    // A single file the server dropped is removed too, and counted missing.
    server.files.erase("/riivolution/Pack/single.bin");
    stats = {};
    EXPECT_TRUE(riftwii::riifs::sync(client, card, root, items, options, stats, error));
    EXPECT_EQ(stats.missing, 1u);
    EXPECT_FALSE(card.has(root + "/riivolution/Pack/single.bin"));

    // A save folder the card lacks comes from the PC once...
    std::vector<riftwii::riifs::SyncItem> save(1);
    save[0].path = "/riivolution/Pack/SaveGame";
    save[0].folder = true;
    stats = {};
    EXPECT_TRUE(riftwii::riifs::sync(client, card, root, save, options, stats, error));
    EXPECT_EQ(card.text(root + "/riivolution/Pack/SaveGame/data.bin"), "server save");
    // ...and one the PC lacks is just created (the game starts a save).
    std::vector<riftwii::riifs::SyncItem> none(1);
    none[0].path = "/riivolution/Pack/NoSave";
    none[0].folder = true;
    riftwii::riifs::SyncOptions keep_none = options;
    keep_none.keep.push_back("/riivolution/Pack/NoSave");
    EXPECT_TRUE(riftwii::riifs::sync(client, card, root, none, keep_none, stats, error));
    EXPECT_TRUE(card.dirs.count(FakeCard::fold(root + "/riivolution/Pack/NoSave")) != 0);
    // After that the card's save is never touched, even inside a mirrored folder.
    server.files["/riivolution/Pack/SaveGame/data.bin"] = bytes("newer server save");
    EXPECT_TRUE(riftwii::riifs::sync(client, card, root, save, options, stats, error));
    card.make_dirs(root + "/riivolution/Pack/SaveGame");
    card.begin_file(root + "/riivolution/Pack/SaveGame/data.bin");
    card.write(reinterpret_cast<const std::uint8_t*>("card save!"), 10);
    card.end_file(true);
    std::vector<riftwii::riifs::SyncItem> whole(1);
    whole[0].path = "/riivolution/Pack";
    whole[0].folder = true;
    stats = {};
    EXPECT_TRUE(riftwii::riifs::sync(client, card, root, whole, options, stats, error));
    EXPECT_EQ(card.text(root + "/riivolution/Pack/SaveGame/data.bin"), "card save!");

    // XML only, top level: the pack list.
    std::vector<riftwii::riifs::SyncItem> xmls(1);
    xmls[0].path = "/riivolution";
    xmls[0].folder = true;
    xmls[0].recursive = false;
    xmls[0].suffix = ".XML";
    card.make_dirs(root + "/riivolution");
    card.begin_file(root + "/riivolution/Old.xml");
    card.end_file(true);
    stats = {};
    EXPECT_TRUE(riftwii::riifs::sync(client, card, root, xmls, options, stats, error));
    EXPECT_TRUE(card.has(root + "/riivolution/Pack.xml"));
    EXPECT_FALSE(card.has(root + "/riivolution/readme.txt"));
    EXPECT_FALSE(card.has(root + "/riivolution/Old.xml"));  // gone from the server
    EXPECT_TRUE(card.dirs.count(FakeCard::fold(root + "/riivolution/Pack")) != 0);  // not a listed kind: kept

    // A folder the server does not have is removed from the cache.
    server.files.erase("/riivolution/Pack/Stage/A.arc");
    stats = {};
    EXPECT_TRUE(riftwii::riifs::sync(client, card, root, items, options, stats, error));
    EXPECT_FALSE(card.dirs.count(FakeCard::fold(root + "/riivolution/Pack/Stage")));

    // Progress can stop the copy.
    server.files["/riivolution/Pack/Stage/C.arc"] = bytes("C");
    options.progress = [](const riftwii::riifs::SyncStats&, const std::string&) { return false; };
    stats = {};
    EXPECT_FALSE(riftwii::riifs::sync(client, card, root, items, options, stats, error));

    // A lost connection fails the sync.
    server.broken = true;
    options.progress = nullptr;
    EXPECT_FALSE(riftwii::riifs::sync(client, card, root, items, options, stats, error));
}

// A server on the network must not reach outside the cache folder, nor
// crash the Wii with folders that never end.
static void TestHostileServer() {
    using riftwii::riifs::SyncItem;
    const std::string root = "sd:/riftwii/riifs/pc_1137";
    auto outside = [&](const FakeCard& card) {
        for (const auto& f : card.files) {
            if (f.first.find("..") != std::string::npos || f.first.compare(0, root.size(), FakeCard::fold(root)) != 0) return true;
        }
        return false;
    };
    std::vector<SyncItem> stage(1);
    stage[0].path = "/riivolution/Pack/Stage";
    stage[0].folder = true;
    for (const std::string& bad : std::vector<std::string>{"../../../apps/riftwii/boot.dol", "a/b.arc", "a\\b.arc", "sd:boot.dol",
                                  std::string(256, 'x')}) {
        FakeServer server;
        server.files["/riivolution/Pack/Stage/A.arc"] = bytes("AAAA");
        server.rogue_names["/riivolution/Pack/Stage"].push_back(bad);
        Client client(server);
        std::string error;
        EXPECT_TRUE(client.handshake(error));
        FakeCard card;
        riftwii::riifs::SyncStats stats;
        EXPECT_FALSE(riftwii::riifs::sync(client, card, root, stage, {}, stats, error));
        EXPECT_FALSE(outside(card));
        EXPECT_TRUE(error.find("cannot take") != std::string::npos);
    }
    EXPECT_TRUE(riftwii::riifs::name_ok(std::string(255, 'x')));
    EXPECT_TRUE(riftwii::riifs::name_ok("..hidden"));
    EXPECT_FALSE(riftwii::riifs::name_ok(""));
    EXPECT_FALSE(riftwii::riifs::name_ok(std::string("a\0b", 3)));

    // A pack's own path: ".." is worked out inside the server's root, and
    // one that climbs above it is refused.
    {
        FakeServer server;
        server.files["/riivolution/Pack/single.bin"] = bytes("S");
        server.files["/apps/riftwii/boot.dol"] = bytes("DOL");
        Client client(server);
        std::string error;
        EXPECT_TRUE(client.handshake(error));
        FakeCard card;
        riftwii::riifs::SyncStats stats;
        std::vector<SyncItem> items(1);
        items[0].path = "/riivolution/Pack/./Stage/../single.bin";
        EXPECT_TRUE(riftwii::riifs::sync(client, card, root, items, {}, stats, error));
        EXPECT_EQ(card.text(root + "/riivolution/Pack/single.bin"), "S");
        items[0].path = "/riivolution/../../../apps/riftwii/boot.dol";
        EXPECT_FALSE(riftwii::riifs::sync(client, card, root, items, {}, stats, error));
        items[0].path = "/riivolution/a:b.bin";
        EXPECT_FALSE(riftwii::riifs::sync(client, card, root, items, {}, stats, error));
        EXPECT_FALSE(outside(card));
    }

    // A folder that contains itself stops at the depth limit, cleanly.
    {
        FakeServer server;
        server.loop = "/riivolution/Loop";
        Client client(server);
        std::string error;
        EXPECT_TRUE(client.handshake(error));
        FakeCard card;
        riftwii::riifs::SyncStats stats;
        std::vector<SyncItem> items(1);
        items[0].path = "/riivolution/Loop";
        items[0].folder = true;
        EXPECT_FALSE(riftwii::riifs::sync(client, card, root, items, {}, stats, error));
        EXPECT_TRUE(error.find("too deep") != std::string::npos);
        // The connection is still in step after it.
        riftwii::riifs::Stat st;
        bool missing = false;
        EXPECT_TRUE(client.stat("/riivolution/Loop", st, missing, error));
    }

    // A listing longer than a FAT folder can hold is refused, and the
    // folder is closed so the connection stays usable.
    {
        FakeServer server;
        server.listing_extra = 65537;
        Client client(server);
        std::string error;
        EXPECT_TRUE(client.handshake(error));
        std::vector<riftwii::riifs::DirEntry> list;
        bool missing = false;
        EXPECT_FALSE(client.list("/big", list, missing, error));
        EXPECT_FALSE(missing);
        EXPECT_TRUE(list.empty());
        server.files["/x.bin"] = bytes("x");
        riftwii::riifs::Stat st;
        EXPECT_TRUE(client.stat("/x.bin", st, missing, error));
        server.listing_extra = 65536;
        EXPECT_TRUE(client.list("/big", list, missing, error));
        EXPECT_EQ(list.size(), 65536u);
    }
}

int main() {
    TestClient();
    TestSync();
    TestHostileServer();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "riifs tests passed" << std::endl;
    return 0;
}
