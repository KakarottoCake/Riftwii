// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "vsdmake.hpp"

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <new>

#include "i18n.hpp"
#include "log.hpp"
#include "riftwii/vsdparts.hpp"

namespace riftwii::wii {
namespace {

constexpr const char* kFolder = "sd:/riftwii/";
constexpr std::uint64_t kPartBytes = 4000ull << 20;  // as 7-Zip splits them
constexpr std::size_t kMaxItems = 60000;
// Room left for what the game writes (replays, custom stages, settings).
constexpr std::uint64_t kSpareMin = 128ull << 20, kSpareMax = 512ull << 20;

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool stat_of(const std::string& path, struct stat& st) { return stat(path.c_str(), &st) == 0; }

// The card's top folders and files, by name.
std::vector<std::string> top_names() {
    std::vector<std::string> out;
    if (DIR* d = opendir("sd:/")) {
        while (dirent* e = readdir(d)) {
            const std::string name = e->d_name;
            if (name != "." && name != "..") out.push_back(name);
        }
        closedir(d);
    }
    return out;
}

// What a Mac or Windows leaves beside a build's files.
bool junk(const std::string& name) {
    return name.compare(0, 2, "._") == 0 || name == ".DS_Store" || lower(name) == "thumbs.db" ||
           lower(name) == "desktop.ini";
}

// Everything under the card path `path` ("/Project+"), folders and files.
bool walk(const std::string& path, int depth, std::vector<riftwii::VsdItem>& items, std::string& error) {
    if (depth > 32) {
        error = tr("{1} has folders too deep to copy.", {path});
        return false;
    }
    DIR* d = opendir(("sd:" + path).c_str());
    if (!d) {
        error = tr("Cannot read sd:{1}.", {path});
        return false;
    }
    std::vector<std::string> names;
    while (dirent* e = readdir(d)) {
        const std::string name = e->d_name;
        if (name != "." && name != ".." && !junk(name)) names.push_back(name);
    }
    closedir(d);
    for (const std::string& name : names) {
        if (items.size() >= kMaxItems) {
            error = tr("The build has more than {1} files.", {std::to_string(kMaxItems)});
            return false;
        }
        const std::string sub = path + "/" + name;
        struct stat st;
        if (!stat_of("sd:" + sub, st)) continue;
        if (S_ISDIR(st.st_mode)) {
            items.push_back({sub, true, 0});
            if (!walk(sub, depth + 1, items, error)) return false;
        } else {
            items.push_back({sub, false, static_cast<std::uint64_t>(st.st_size)});
        }
    }
    return true;
}

// A top folder or file of the card in the image, under its own name.
bool add_top(const std::string& name, std::vector<std::string>& tops, std::vector<riftwii::VsdItem>& items,
             std::string& error) {
    for (const std::string& t : tops)
        if (strcasecmp(t.c_str(), name.c_str()) == 0) return true;
    struct stat st;
    if (!stat_of("sd:/" + name, st)) return true;
    tops.push_back(name);
    if (!S_ISDIR(st.st_mode)) {
        items.push_back({"/" + name, false, static_cast<std::uint64_t>(st.st_size)});
        return true;
    }
    items.push_back({"/" + name, true, 0});
    return walk("/" + name, 1, items, error);
}

// "Project+" -> "Project+.raw": a name libfat and a PC both take.
std::string image_name(const std::string& top) {
    std::string out;
    for (char c : top) {
        if (std::strchr("\\/:*?\"<>|", c) || static_cast<unsigned char>(c) < 0x20) c = '_';
        out.push_back(c);
    }
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    if (out.empty() || lower(out) == "sd") out = "build";  // sd.raw is the card of old
    return out + ".raw";
}

std::uint64_t file_size(const std::string& path) {
    struct stat st;
    return stat_of(path, st) && !S_ISDIR(st.st_mode) ? static_cast<std::uint64_t>(st.st_size) : 0;
}

// The image's files on the card now: name.raw and its parts.
std::vector<std::string> image_files(const std::string& image) {
    std::vector<std::string> out;
    if (access((kFolder + image).c_str(), F_OK) == 0) out.push_back(kFolder + image);
    for (unsigned n = 1; n < 1000; ++n) {
        const std::string part = kFolder + riftwii::vsd_part_name(image, n);
        if (access(part.c_str(), F_OK) != 0) break;
        out.push_back(part);
    }
    return out;
}

// Writes the image to its file, or parts of kPartBytes.
class CardSink final : public riftwii::VsdSink {
public:
    CardSink(const std::string& image, bool split) : image_(image), split_(split) {}
    ~CardSink() override { close(); }
    bool write(const std::uint8_t* data, std::size_t length) override {
        while (length) {
            if (!f_ || (split_ && in_part_ == kPartBytes)) {
                if (!next()) return false;
            }
            std::size_t n = length;
            if (split_) n = static_cast<std::size_t>(std::min<std::uint64_t>(n, kPartBytes - in_part_));
            if (std::fwrite(data, 1, n, f_) != n) return false;
            in_part_ += n;
            data += n;
            length -= n;
        }
        return true;
    }
    bool close() {
        if (!f_) return true;
        const bool ok = std::fclose(f_) == 0;
        f_ = nullptr;
        return ok;
    }
    const std::vector<std::string>& written() const { return written_; }

private:
    bool next() {
        if (!close()) return false;
        const std::string path = kFolder + (split_ ? riftwii::vsd_part_name(image_, ++part_) : image_);
        f_ = std::fopen(path.c_str(), "wb");
        if (!f_) return false;
        // Its chunks go straight to the card: no second copy of each.
        std::setvbuf(f_, nullptr, _IONBF, 0);
        written_.push_back(path);
        in_part_ = 0;
        return true;
    }

    std::string image_;
    bool split_;
    FILE* f_ = nullptr;
    unsigned part_ = 0;
    std::uint64_t in_part_ = 0;
    std::vector<std::string> written_;
};

// The build's files, read in the order written: one open at a time.
class CardReader {
public:
    ~CardReader() {
        if (f_) std::fclose(f_);
    }
    bool read(const std::string& path, std::uint64_t offset, std::uint8_t* out, std::size_t length) {
        if (path != path_ || offset != at_) {
            if (f_) std::fclose(f_);
            f_ = std::fopen(("sd:" + path).c_str(), "rb");
            path_ = path;
            at_ = 0;
            if (!f_) return false;
            std::setvbuf(f_, nullptr, _IONBF, 0);
            if (offset && fseeko(f_, static_cast<off_t>(offset), SEEK_SET) != 0) return false;
            at_ = offset;
        }
        if (!f_ || std::fread(out, 1, length, f_) != length) return false;
        at_ += length;
        return true;
    }

private:
    FILE* f_ = nullptr;
    std::string path_;
    std::uint64_t at_ = 0;
};

}  // namespace

namespace {

bool plan_make(const std::string& key, const std::string& game_id, VsdMakePlan& out, std::string& error) {
    out = VsdMakePlan();
    out.key = key;
    const std::string top = key.substr(0, key.find('/'));
    if (top.empty() || top == key) {
        error = tr("Only a build in a folder can go into an image.");
        return false;
    }
    std::vector<riftwii::VsdItem> items;
    if (!add_top(top, out.tops, items, error)) return false;

    // The other top folders its codes load files from ("/projectm/" for a
    // build whose codes are in sd:/codes).
    std::vector<std::uint8_t> gct;
    if (FILE* f = std::fopen(("sd:/" + key).c_str(), "rb")) {
        std::uint8_t buf[4096];
        std::size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0 && gct.size() < (1u << 20)) gct.insert(gct.end(), buf, buf + n);
        std::fclose(f);
    }
    const std::vector<std::string> names = top_names();
    std::string named;  // the first folder named that is not the build's own
    for (const std::string& folder : riftwii::vsd_gct_folders(gct)) {
        const std::string l = lower(folder);
        // Never the card's own folders, nor another game's whole save folder.
        if (l == "riftwii" || l == "private" || l == "apps" || l == "wbfs" || l == "games" || l == "riivolution" ||
            l == "wad")
            continue;
        for (const std::string& n : names) {
            if (strcasecmp(n.c_str(), folder.c_str()) != 0) continue;
            struct stat st;
            if (!stat_of("sd:/" + n, st) || !S_ISDIR(st.st_mode)) break;
            if (strcasecmp(n.c_str(), top.c_str()) != 0 && named.empty()) named = n;
            if (!add_top(n, out.tops, items, error)) return false;
            break;
        }
    }
    // gameconfig.txt or gc.txt at the top of the card places the code list
    // when the build has none of its own.
    for (const char* config : {"gameconfig.txt", "gc.txt"})
        if (!add_top(config, out.tops, items, error)) return false;
    // A custom stage or the like where the game keeps its own files.
    if (game_id.size() >= 4) {
        const std::string app = "private/wii/app/" + game_id.substr(0, 4);
        struct stat st;
        if (stat_of("sd:/" + app, st) && S_ISDIR(st.st_mode)) {
            items.push_back({"/" + app, true, 0});
            if (!walk("/" + app, 4, items, error)) return false;
            out.tops.push_back(app);
        }
    }

    // Free room: 5 %, from 128 to 512 MiB.
    std::uint64_t bytes = 0;
    for (const riftwii::VsdItem& i : items) bytes += i.size;
    const std::uint64_t spare = std::min(kSpareMax, std::max(kSpareMin, bytes / 20));
    const std::time_t now = std::time(nullptr);
    if (const std::tm* t = std::localtime(&now)) {
        if (t->tm_year >= 80) {
            out.plan.fat_date = static_cast<std::uint16_t>(((t->tm_year - 80) << 9) | ((t->tm_mon + 1) << 5) | t->tm_mday);
            out.plan.fat_time = static_cast<std::uint16_t>((t->tm_hour << 11) | (t->tm_min << 5) | (t->tm_sec / 2));
        }
    }
    out.plan.serial = static_cast<std::uint32_t>(now);
    std::string why;
    if (!riftwii::plan_vsd_image(items, spare, out.plan, why)) {
        error = tr("The build cannot go into an image: {1}.", {why});
        return false;
    }

    // A root build ("codes") takes the name of the folder its codes name.
    out.image = image_name(lower(top) == "codes" && !named.empty() ? named : top);
    out.parts = out.plan.image_bytes > riftwii::kVsdMaxFileBytes
                    ? static_cast<unsigned>((out.plan.image_bytes + kPartBytes - 1) / kPartBytes)
                    : 0;
    if (out.parts > riftwii::kMaxVsdParts) {
        error = tr("The build cannot go into an image: {1}.", {"it would need more than " +
                                                                std::to_string(riftwii::kMaxVsdParts) + " parts"});
        return false;
    }
    std::uint64_t replaced = 0;
    for (const std::string& f : image_files(out.image)) replaced += file_size(f);
    out.replaces = !image_files(out.image).empty();
    struct statvfs vfs;
    if (statvfs("sd:/", &vfs) == 0) {
        out.free_bytes = std::uint64_t(vfs.f_bavail) * vfs.f_frsize + replaced;
        // Room for the new image beside the old one: the old one is only
        // let go once the new one is whole (MakeVsdImage).
        out.beside = out.free_bytes - replaced >= out.plan.image_bytes + (8ull << 20);
    }
    logf("Image: %s from %s: %u file(s), %u folder(s), %llu MiB, cluster %u, %llu MiB free\n", out.image.c_str(),
         key.c_str(), out.plan.files, out.plan.folders, static_cast<unsigned long long>(out.plan.image_bytes >> 20),
         out.plan.cluster_bytes, static_cast<unsigned long long>(out.free_bytes >> 20));
    // A little room for the card's own FAT to grow.
    if (out.free_bytes < out.plan.image_bytes + (8ull << 20)) {
        const auto mb = [](std::uint64_t b) { return std::to_string((b + (1 << 20) - 1) >> 20) + " MB"; };
        error = tr("The image needs {1} on the SD card and {2} is free. Make room (or use a bigger card) and try again.",
                   {mb(out.plan.image_bytes + (8ull << 20)), mb(out.free_bytes)});
        return false;
    }
    return true;
}

}  // namespace

// The list of a big build (tens of thousands of files) and its plan take
// megabytes: running out is a message, not a crash.
bool PlanVsdMake(const std::string& key, const std::string& game_id, VsdMakePlan& out, std::string& error) {
    try {
        return plan_make(key, game_id, out, error);
    } catch (const std::bad_alloc&) {
        out = VsdMakePlan();
        error = tr("The build has too many files to make an image of here (out of memory).");
        logf("Image: out of memory planning %s\n", key.c_str());
        return false;
    }
}

bool MakeVsdImage(const VsdMakePlan& plan,
                  const std::function<bool(std::uint64_t written, const std::string& path)>& progress,
                  std::string& error) {
    mkdir("sd:/riftwii", 0777);
    // Written under a name of its own when the card has room for both, and
    // swapped in once whole: a power cut or a full card then leaves the
    // old image as it was. Without that room the old one goes first.
    const std::string fresh = plan.image + ".new";
    for (const std::string& f : image_files(fresh)) std::remove(f.c_str());  // a cut-short earlier try
    if (!plan.beside) {
        for (const std::string& f : image_files(plan.image)) {
            if (std::remove(f.c_str()) != 0) {
                error = tr("Cannot replace {1}.", {f});
                return false;
            }
        }
    }
    CardSink sink(plan.beside ? fresh : plan.image, plan.parts != 0);
    CardReader reader;
    std::vector<std::uint8_t> buffer(1 << 20);
    std::string why;
    bool ok = riftwii::write_vsd_image(
        plan.plan, sink,
        [&reader](const std::string& path, std::uint64_t offset, std::uint8_t* out, std::size_t length) {
            return reader.read(path, offset, out, length);
        },
        progress, buffer, why);
    if (ok && !sink.close()) {
        ok = false;
        why = "could not finish the image (is the card full?)";
    }
    if (!ok) {
        sink.close();
        for (const std::string& f : sink.written()) std::remove(f.c_str());
        logf("Image: %s not made: %s\n", plan.image.c_str(), why.c_str());
        error = why;
        return false;
    }
    if (plan.beside) {
        for (const std::string& f : image_files(plan.image)) {
            if (std::remove(f.c_str()) != 0) {
                for (const std::string& w : sink.written()) std::remove(w.c_str());
                error = tr("Cannot replace {1}.", {f});
                return false;
            }
        }
        for (const std::string& w : sink.written()) {
            // "X.raw.new" -> "X.raw", "X.raw.new.001" -> "X.raw.001".
            std::string to = w;
            to.erase(std::strlen(kFolder) + plan.image.size(), 4);
            if (std::rename(w.c_str(), to.c_str()) != 0) {
                error = tr("Cannot replace {1}.", {to});
                logf("Image: cannot rename %s to %s\n", w.c_str(), to.c_str());
                return false;
            }
        }
    }
    logf("Image: %s made (%u file(s))\n", plan.image.c_str(), static_cast<unsigned>(sink.written().size()));
    return true;
}

}  // namespace riftwii::wii
