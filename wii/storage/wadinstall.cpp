// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "wadinstall.hpp"

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

#include "log.hpp"
#include "otpkey.hpp"
#include "usbcatalog.hpp"
#include "riftwii/checkedalloc.hpp"
#include "riftwii/imagevolume.hpp"
#include "riftwii/wadfile.hpp"

namespace riftwii::wii {
namespace {

constexpr std::size_t kChunkBytes = 64 * 1024;

struct FileCloser {
    void operator()(FILE* file) const {
        if (file) std::fclose(file);
    }
};
using File = std::unique_ptr<FILE, FileCloser>;

class WadSource final : public ByteSource {
public:
    WadSource(std::string path, FILE* file, std::uint64_t size)
        : path_(std::move(path)), file_(file), size_(size) {}

    std::uint64_t size() const override { return size_; }

    bool read(std::uint64_t offset, std::uint8_t* out, std::size_t length) const override {
        if (offset > size_ || length > size_ - offset) return false;
        if (!file_) {
            std::uint64_t current_size = 0;
            return read_usb_range(path_, offset, out, length, current_size) && current_size == size_;
        }
        return std::fseek(file_.get(), static_cast<long>(offset), SEEK_SET) == 0 &&
               std::fread(out, 1, length, file_.get()) == length;
    }

private:
    std::string path_;
    File file_;
    std::uint64_t size_;
};

bool exists(const std::string& path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0;
}

void make_folders(const std::string& path) {
    for (std::size_t at = path.find('/', 4); at != std::string::npos; at = path.find('/', at + 1)) {
        mkdir(path.substr(0, at).c_str(), 0777);
    }
    mkdir(path.c_str(), 0777);
}

// These are private staging/content folders, never the title's data folder.
// Do not recurse into any unexpected subfolder.
void clear_content_folder(const std::string& folder) {
    DIR* directory = opendir(folder.c_str());
    if (!directory) return;
    while (const dirent* entry = readdir(directory)) {
        const std::string name = entry->d_name;
        if (name == "." || name == ".." || name.find_first_of("/\\:") != std::string::npos) continue;
        std::remove((folder + "/" + name).c_str());
    }
    closedir(directory);
    rmdir(folder.c_str());
}

bool write_file(const std::string& path, const std::vector<std::uint8_t>& bytes, std::string& error) {
    FILE* file = std::fopen(path.c_str(), "wb");
    const bool written = file && std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    const bool closed = file && std::fclose(file) == 0;
    if (!written || !closed) {
        error = "the SD card could not save " + path + " (is it full or locked?)";
        std::remove(path.c_str());
        return false;
    }
    return true;
}

bool copy_content(const ByteSource& wad, const WadContentPlace& place, const std::uint8_t title_key[16],
                  const std::string& to, const std::function<void(std::uint64_t)>& advanced, std::string& error) {
    FILE* file = std::fopen(to.c_str(), "wb");
    if (!file) {
        error = "the SD card could not create a game file (is it full or locked?)";
        return false;
    }
    WadContentDecrypter decrypter(title_key, place.content);
    std::vector<std::uint8_t> chunk;
    if (!try_resize(chunk, kChunkBytes)) error = "not enough memory to prepare the game";
    for (std::uint64_t done = 0; error.empty() && done < place.stored;) {
        const std::size_t piece = static_cast<std::size_t>(std::min<std::uint64_t>(kChunkBytes, place.stored - done));
        if (!wad.read(place.offset + done, chunk.data(), piece)) {
            error = "the WAD could not be read (was the drive removed?)";
            break;
        }
        const std::size_t own = decrypter.decrypt(chunk.data(), piece);
        if (std::fwrite(chunk.data(), 1, own, file) != own) {
            error = "the SD card ran out of space or could not save a game file";
            break;
        }
        done += piece;
        advanced(piece);
    }
    if (std::fclose(file) != 0 && error.empty()) error = "the SD card could not finish saving a game file";
    if (error.empty() && !decrypter.matches()) error = "the WAD is damaged or uses another Wii key";
    if (!error.empty()) {
        std::remove(to.c_str());
        return false;
    }
    return true;
}

// A replacement keeps the previous file/folder until the entire operation
// succeeds. Rollback runs in reverse order, including the shared-content map.
struct Replacement {
    std::string staged;
    std::string target;
    bool had_previous = false;
    bool published = false;
};

bool publish(Replacement& replacement, std::string& error) {
    const std::string backup = replacement.target + ".riftwii-old";
    if (exists(backup)) {
        error = "an interrupted WAD preparation needs its .riftwii-old backup checked before trying again";
        return false;
    }
    replacement.had_previous = exists(replacement.target);
    if (replacement.had_previous && std::rename(replacement.target.c_str(), backup.c_str()) != 0) {
        error = "the SD card could not keep the previous game files safe";
        return false;
    }
    if (std::rename(replacement.staged.c_str(), replacement.target.c_str()) != 0) {
        error = "the SD card could not finish preparing the game (is it full or locked?)";
        if (replacement.had_previous && std::rename(backup.c_str(), replacement.target.c_str()) != 0) {
            error += "; the previous copy is kept in " + backup;
        }
        replacement.had_previous = false;
        return false;
    }
    replacement.published = true;
    return true;
}

}  // namespace

std::unique_ptr<ByteSource> open_wad_source(const std::string& path, std::string& error) {
    File file;
    std::uint64_t size = 0;
    if (is_usb_path(path)) {
        if (!read_usb_range(path, 0, nullptr, 0, size)) {
            error = "the WAD could not be opened on the USB drive";
            return nullptr;
        }
    } else if (path.compare(0, 4, "sd:/") == 0) {
        file.reset(std::fopen(path.c_str(), "rb"));
        if (!file || std::fseek(file.get(), 0, SEEK_END) != 0) {
            error = "the WAD could not be opened on the SD card";
            return nullptr;
        }
        const long length = std::ftell(file.get());
        if (length > 0) size = static_cast<std::uint64_t>(length);
    }
    if (size == 0 || size >= 0x7FFFFFFF) {
        error = "the WAD is empty, too large, or outside the SD and USB drives";
        return nullptr;
    }
    return std::unique_ptr<ByteSource>(new WadSource(path, file.release(), size));
}

std::vector<std::string> list_wad_files() {
    std::vector<std::string> files;
    for (const char* folder : kWadFolders) {
        DIR* directory = opendir(folder);
        if (!directory) continue;
        while (const dirent* entry = readdir(directory)) {
            const std::string name = entry->d_name;
            if (name.empty() || name[0] == '.' || name.size() < 5 ||
                strcasecmp(name.c_str() + name.size() - 4, ".wad") != 0) continue;
            if (files.size() >= 1024 || !try_push_back(files, std::string(folder) + "/" + name)) break;
        }
        closedir(directory);
    }
    std::sort(files.begin(), files.end());
    return files;
}

namespace {

const char* key_source_name(WadKeySource source) {
    if (source == WadKeySource::Ios) return "IOS";
    if (source == WadKeySource::BootMiiKeys) return "sd:/keys.bin";
    return "this Wii's OTP";
}

}  // namespace

bool read_console_wad_banner(const ByteSource& source, const WadMetadata& metadata,
                              std::uint8_t title_key[16], std::vector<std::uint8_t>& banner,
                              std::string& error) {
    WadKeySource selected = WadKeySource::Otp;
    const bool read = select_wad_title_key(source, metadata,
        [&](WadKeySource candidate, std::uint8_t* key) {
            return ConsoleWadTitleKey(candidate, metadata.ticket, key);
        }, title_key, selected, banner, error);
    if (read) {
        logf("WADs: %016llx passed its banner hash check with a title key from %s.\n",
             static_cast<unsigned long long>(metadata.tmd.title_id), key_source_name(selected));
    }
    return read;
}

bool read_console_wad_install_key(const ByteSource& source, const WadMetadata& metadata,
                                  std::uint8_t title_key[16], std::string& error) {
    WadKeySource selected = WadKeySource::Otp;
    const bool read = select_wad_install_key(source, metadata,
        [&](WadKeySource candidate, std::uint8_t* key) {
            return ConsoleWadTitleKey(candidate, metadata.ticket, key);
        }, title_key, selected, error);
    if (read) {
        logf("WADs: %016llx passed its content hash check with a title key from %s.\n",
             static_cast<unsigned long long>(metadata.tmd.title_id), key_source_name(selected));
    }
    return read;
}

bool install_wad(const std::string& path, const std::string& folder, const std::uint8_t common_key[16],
                 const std::function<void(double)>& progress, std::string& code, std::string& error,
                 std::uint64_t expected_title, const Sha1Digest* source_stamp) {
    error.clear();
    // Only a checked, plain SD folder can be a NAND destination. Never let
    // a configured path write outside the card or into the console's NAND.
    if (folder.compare(0, 4, "sd:/") != 0 || folder.size() <= 4 ||
        folder.find("..") != std::string::npos || folder.find_first_of("\\", 4) != std::string::npos ||
        folder.find(':', 3) != std::string::npos) {
        error = "the emulated NAND must be a folder on the SD card";
        return false;
    }
    const auto wad = open_wad_source(path, error);
    WadMetadata metadata;
    if (!wad || !read_wad_metadata(*wad, metadata, error)) return false;
    if (expected_title != 0 && metadata.tmd.title_id != expected_title) {
        error = "the WAD changed to a different game; read the drives again";
        return false;
    }
    code = channel_code(metadata.tmd.title_id);
    std::uint8_t title_key[16] = {};
    bool have_key;
    if (common_key) {
        have_key = decrypt_title_key(metadata.ticket.data(), metadata.ticket.size(), common_key, title_key, error);
    } else {
        // Checked on a content rather than the banner: a channel WAD with no
        // banner, or one over 4 MiB, still installs (as with a common key).
        have_key = read_console_wad_install_key(*wad, metadata, title_key, error);
    }
    if (!have_key) {
        clear_wad_key(title_key, sizeof title_key);
        return false;
    }
    char relative[64];
    std::snprintf(relative, sizeof relative, "/title/%08x/%08x", static_cast<unsigned>(metadata.tmd.title_id >> 32),
                  static_cast<unsigned>(metadata.tmd.title_id));
    const std::string title_dir = folder + relative;
    const std::string stage = title_dir + "/.riftwii-stage";
    const std::string stage_content = stage + "/content";
    const bool new_title = !exists(title_dir + "/content");
    // A power loss before publication can leave staging files. They are
    // private to this title and never include its data/save directory.
    clear_content_folder(stage_content);
    clear_content_folder(stage);
    make_folders(stage_content);
    make_folders(folder + "/ticket/00010001");
    make_folders(folder + "/shared1");
    make_folders(folder + "/sys");

    const std::string map_path = folder + "/shared1/content.map";
    std::vector<std::uint8_t> map_bytes;
    File map(std::fopen(map_path.c_str(), "rb"));
    if (!map && exists(map_path)) {
        error = "the emulated NAND's shared-content list could not be opened";
    }
    if (map) {
        std::fseek(map.get(), 0, SEEK_END);
        const long size = std::ftell(map.get());
        if (size < 0 || size > (1 << 20) || size % 28 != 0 || !try_resize(map_bytes, static_cast<std::size_t>(size)) ||
            std::fseek(map.get(), 0, SEEK_SET) != 0 || std::fread(map_bytes.data(), 1, map_bytes.size(), map.get()) != map_bytes.size()) {
            error = "the emulated NAND's shared-content list is damaged or could not be read";
        }
    }
    map.reset();
    std::vector<SharedContent> shared = parse_content_map(map_bytes.data(), map_bytes.size());
    if (shared.size() * 28 != map_bytes.size()) error = "the emulated NAND's shared-content list is damaged";
    std::vector<Replacement> replacements;
    if (!try_reserve(replacements, metadata.places.size() + 4) || !try_reserve(shared, shared.size() + metadata.places.size())) {
        error = "not enough memory to prepare the game";
    }
    std::uint64_t total = 0;
    std::uint64_t done = 0;
    for (const WadContentPlace& place : metadata.places) total += place.stored;
    const auto advanced = [&](std::uint64_t bytes) {
        done += bytes;
        if (progress && total) progress(static_cast<double>(done) / static_cast<double>(total));
    };
    bool map_changed = false;
    for (const WadContentPlace& place : metadata.places) {
        if (!error.empty()) break;
        if ((place.content.type & 0x8000) != 0) {
            const SharedContent* found = find_shared_content(shared, place.content.sha1);
            // Even a reused shared content is verified from this WAD. A
            // damaged update must fail before any installed file changes.
            const std::string name = found ? found->name : next_shared_name(shared);
            if (name.size() != 8) {
                error = "the emulated NAND has too many shared contents";
                break;
            }
            const std::string target = folder + "/shared1/" + name + ".app";
            const std::string staged = stage + "/" + name + ".app";
            if (!copy_content(*wad, place, title_key, staged, advanced, error)) break;
            const bool already_staged = std::any_of(replacements.begin(), replacements.end(),
                [&](const Replacement& replacement) { return replacement.target == target; });
            if (!already_staged) {
                replacements.push_back({staged, target});
            }
            if (!found) {
                shared.push_back({name, place.content.sha1});
                map_changed = true;
            }
        } else {
            char name[20];
            std::snprintf(name, sizeof name, "/%08x.app", static_cast<unsigned>(place.content.id));
            if (!copy_content(*wad, place, title_key, stage_content + name, advanced, error)) break;
        }
    }
    clear_wad_key(title_key, sizeof title_key);
    // ES uses this list to give the title ownership of its save folder.
    // Publish its update with the ticket and content, never separately.
    std::vector<std::uint8_t> uid_bytes;
    const std::string uid_path = folder + "/sys/uid.sys";
    if (error.empty() && exists(uid_path)) {
        File uid(std::fopen(uid_path.c_str(), "rb"));
        const bool sought = uid && std::fseek(uid.get(), 0, SEEK_END) == 0;
        const long size = sought ? std::ftell(uid.get()) : -1;
        if (size < 0 || size > 12 * 8192 || !try_resize(uid_bytes, static_cast<std::size_t>(size)) ||
            std::fseek(uid.get(), 0, SEEK_SET) != 0 ||
            std::fread(uid_bytes.data(), 1, uid_bytes.size(), uid.get()) != uid_bytes.size()) {
            error = "the emulated NAND's title ownership list could not be read";
        }
    }
    if (error.empty() && add_wad_title_uid(uid_bytes, metadata.tmd.title_id, error) &&
        write_file(stage + "/uid.sys", uid_bytes, error)) {
        replacements.push_back({stage + "/uid.sys", uid_path});
    }
    if (error.empty() && map_changed) {
        const auto encoded = serialize_content_map(shared);
        if (encoded.size() != shared.size() * 28) error = "not enough memory for the shared-content list";
        if (error.empty() && write_file(stage + "/content.map", encoded, error)) {
            replacements.push_back({stage + "/content.map", map_path});
        }
    }
    char ticket_name[32];
    std::snprintf(ticket_name, sizeof ticket_name, "/ticket/00010001/%08x.tik", static_cast<unsigned>(metadata.tmd.title_id));
    if (error.empty() && write_file(stage + "/title.tik", metadata.ticket, error)) {
        replacements.push_back({stage + "/title.tik", folder + ticket_name});
    }
    if (error.empty() && source_stamp != nullptr) {
        std::vector<std::uint8_t> stamp(source_stamp->begin(), source_stamp->end());
        write_file(stage_content + "/.riftwii-wad", stamp, error);
    }
    if (error.empty() && write_file(stage_content + "/title.tmd", metadata.tmd_bytes, error)) {
        replacements.push_back({stage_content, title_dir + "/content"});
    }
    if (error.empty()) {
        for (Replacement& replacement : replacements) {
            if (!publish(replacement, error)) break;
        }
    }
    if (!error.empty()) {
        for (auto it = replacements.rbegin(); it != replacements.rend(); ++it) {
            if (!it->published) continue;
            if (it->target == title_dir + "/content") clear_content_folder(it->target);
            else std::remove(it->target.c_str());
            if (it->had_previous && std::rename((it->target + ".riftwii-old").c_str(), it->target.c_str()) != 0) {
                error += "; the previous copy is kept in " + it->target + ".riftwii-old";
            }
        }
    } else {
        for (const Replacement& replacement : replacements) {
            if (!replacement.had_previous) continue;
            if (replacement.target == title_dir + "/content") clear_content_folder(replacement.target + ".riftwii-old");
            else std::remove((replacement.target + ".riftwii-old").c_str());
        }
        make_folders(title_dir + "/data");
    }
    clear_content_folder(stage_content);
    clear_content_folder(stage);
    if (!error.empty()) {
        if (new_title) rmdir(title_dir.c_str());
        return false;
    }
    logf("WADs: %s is ready in %s (%u contents, %llu bytes).\n", path.c_str(), folder.c_str(),
         static_cast<unsigned>(metadata.places.size()), static_cast<unsigned long long>(total));
    return true;
}

}  // namespace riftwii::wii
