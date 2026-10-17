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
#include "riftwii/nandchannel.hpp"
#include "riftwii/wadfile.hpp"

namespace riftwii::wii {
namespace {

// Read and decrypted this much at a time.
constexpr std::size_t kChunkBytes = 64 * 1024;
// Over this a file is not a channel's WAD (the Shop's biggest were a few
// hundred MB), and its offsets would not fit the card's file API.
constexpr long kMaxWadBytes = 0x7FFFFFFF;

struct FileCloser {
    void operator()(FILE* f) const {
        if (f) std::fclose(f);
    }
};
using File = std::unique_ptr<FILE, FileCloser>;

bool read_at(FILE* file, std::uint64_t offset, std::uint8_t* out, std::size_t length) {
    return std::fseek(file, static_cast<long>(offset), SEEK_SET) == 0 && std::fread(out, 1, length, file) == length;
}

bool read_part(FILE* file, std::uint64_t offset, std::uint64_t size, std::vector<std::uint8_t>& out) {
    out.resize(static_cast<std::size_t>(size));
    return read_at(file, offset, out.data(), out.size());
}

// Every folder of `path` (sd:/a/b/c makes sd:/a, sd:/a/b and sd:/a/b/c).
void make_folders(const std::string& path) {
    for (std::size_t at = path.find('/', 4); at != std::string::npos; at = path.find('/', at + 1))
        mkdir(path.substr(0, at).c_str(), 0777);
    mkdir(path.c_str(), 0777);
}

bool write_file(const std::string& path, const std::vector<std::uint8_t>& bytes, std::string& error) {
    File file(std::fopen(path.c_str(), "wb"));
    if (!file || std::fwrite(bytes.data(), 1, bytes.size(), file.get()) != bytes.size()) {
        error = path + " could not be written";
        return false;
    }
    return true;
}

// Copies one content out of the WAD, decrypted, into `to`; the file is
// removed again when its bytes do not match the TMD's hash.
bool copy_content(FILE* wad, const WadContentPlace& place, const std::uint8_t title_key[16], const std::string& to,
                  const std::function<void(std::uint64_t)>& advanced, std::string& error) {
    File out(std::fopen(to.c_str(), "wb"));
    if (!out) {
        error = to + " could not be written";
        return false;
    }
    WadContentDecrypter decrypter(title_key, place.content);
    std::vector<std::uint8_t> chunk(kChunkBytes);
    for (std::uint64_t done = 0; done < place.stored;) {
        const std::size_t piece = static_cast<std::size_t>(std::min<std::uint64_t>(kChunkBytes, place.stored - done));
        if (!read_at(wad, place.offset + done, chunk.data(), piece)) {
            error = "the WAD could not be read";
            break;
        }
        const std::size_t own = decrypter.decrypt(chunk.data(), piece);
        if (own != 0 && std::fwrite(chunk.data(), 1, own, out.get()) != own) {
            error = to + " could not be written (is the card full?)";
            break;
        }
        done += piece;
        advanced(piece);
    }
    out.reset();
    if (error.empty() && !decrypter.matches()) {
        char text[96];
        std::snprintf(text, sizeof text, "content %08x does not match its hash (a damaged WAD, or one for another key)",
                      static_cast<unsigned>(place.content.id));
        error = text;
    }
    if (!error.empty()) {
        std::remove(to.c_str());
        return false;
    }
    return true;
}

}  // namespace

std::vector<std::string> list_wad_files() {
    std::vector<std::string> files;
    DIR* dir = opendir(kWadFolder);
    if (dir == nullptr) return files;
    while (const dirent* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        // Hidden files are never WADs: macOS leaves "._name.wad" beside each.
        if (name.empty() || name[0] == '.' || name.size() < 5) continue;
        if (strcasecmp(name.c_str() + name.size() - 4, ".wad") != 0) continue;
        files.push_back(std::string(kWadFolder) + "/" + name);
    }
    closedir(dir);
    std::sort(files.begin(), files.end());
    return files;
}

bool install_wad(const std::string& path, const std::string& folder, const std::uint8_t common_key[16],
                 const std::function<void(double)>& progress, std::string& code, std::string& error) {
    File wad(std::fopen(path.c_str(), "rb"));
    if (!wad) {
        error = "it could not be opened";
        return false;
    }
    std::fseek(wad.get(), 0, SEEK_END);
    const long file_size = std::ftell(wad.get());
    if (file_size <= 0 || file_size >= kMaxWadBytes) {
        error = "it is not a size a WAD has";
        return false;
    }
    std::uint8_t header[0x20];
    WadLayout layout;
    if (!read_at(wad.get(), 0, header, sizeof header) ||
        !parse_wad_header(header, sizeof header, static_cast<std::uint64_t>(file_size), layout, error))
        return false;
    std::vector<std::uint8_t> ticket, tmd_bytes;
    if (!read_part(wad.get(), layout.ticket_offset, layout.ticket_size, ticket) ||
        !read_part(wad.get(), layout.tmd_offset, layout.tmd_size, tmd_bytes)) {
        error = "its ticket and TMD could not be read";
        return false;
    }
    TmdContents tmd;
    if (!parse_tmd_contents(tmd_bytes.data(), tmd_bytes.size(), tmd, error)) return false;
    // Only channels: a WAD of an IOS or a system title would go where
    // nothing of RiftWii's should write.
    if (static_cast<std::uint32_t>(tmd.title_id >> 32) != kDownloadedChannels) {
        char text[96];
        std::snprintf(text, sizeof text, "it is title %08x-%08x, not a channel", static_cast<unsigned>(tmd.title_id >> 32),
                      static_cast<unsigned>(tmd.title_id));
        error = text;
        return false;
    }
    code = channel_code(tmd.title_id);
    std::uint8_t title_key[16];
    if (!decrypt_title_key(ticket.data(), ticket.size(), common_key, title_key, error)) return false;
    std::vector<WadContentPlace> places;
    if (!wad_content_places(layout, tmd, places, error)) return false;

    char title_part[48];
    std::snprintf(title_part, sizeof title_part, "/title/%08x/%08x", static_cast<unsigned>(tmd.title_id >> 32),
                  static_cast<unsigned>(tmd.title_id));
    const std::string title_dir = folder + title_part;
    make_folders(title_dir + "/content");
    make_folders(title_dir + "/data");
    make_folders(folder + "/ticket/00010001");
    make_folders(folder + "/shared1");

    // Shared contents go to /shared1 once, known by their hash.
    std::vector<std::uint8_t> map_bytes;
    {
        File map(std::fopen((folder + "/shared1/content.map").c_str(), "rb"));
        if (map) {
            std::fseek(map.get(), 0, SEEK_END);
            const long size = std::ftell(map.get());
            const bool read = size > 0 && size < (1 << 20) &&
                              read_part(map.get(), 0, static_cast<std::uint64_t>(size), map_bytes);
            if (!read) map_bytes.clear();
        }
    }
    std::vector<SharedContent> shared = parse_content_map(map_bytes.data(), map_bytes.size());
    bool shared_added = false;
    // On a failure the files written so far go again, and the title's
    // folders when they are left empty: a title folder without its TMD
    // would be listed as unreadable at every start.
    std::vector<std::string> written;
    const auto undo = [&]() {
        for (const std::string& file : written) std::remove(file.c_str());
        rmdir((title_dir + "/content").c_str());
        rmdir((title_dir + "/data").c_str());
        rmdir(title_dir.c_str());
    };

    std::uint64_t total = 0, done = 0;
    for (const WadContentPlace& place : places) total += place.stored;
    const auto advanced = [&](std::uint64_t bytes) {
        done += bytes;
        if (progress && total) progress(static_cast<double>(done) / static_cast<double>(total));
    };
    for (const WadContentPlace& place : places) {
        const bool is_shared = (place.content.type & 0x8000) != 0;
        std::string to;
        if (is_shared) {
            if (find_shared_content(shared, place.content.sha1)) {
                advanced(place.stored);
                continue;
            }
            SharedContent entry;
            entry.name = next_shared_name(shared);
            entry.sha1 = place.content.sha1;
            to = folder + "/shared1/" + entry.name + ".app";
            if (!copy_content(wad.get(), place, title_key, to, advanced, error)) {
                undo();
                return false;
            }
            written.push_back(to);
            shared.push_back(entry);
            shared_added = true;
            continue;
        }
        to = folder + nand_content_path(tmd.title_id, place.content.id);
        if (!copy_content(wad.get(), place, title_key, to, advanced, error)) {
            undo();
            return false;
        }
        written.push_back(to);
    }
    if (shared_added && !write_file(folder + "/shared1/content.map", serialize_content_map(shared), error)) {
        undo();
        return false;
    }
    // Last, so a title only counts as installed once every content is there.
    if (!write_file(title_dir + "/content/title.tmd", tmd_bytes, error)) {
        undo();
        return false;
    }
    char ticket_part[48];
    std::snprintf(ticket_part, sizeof ticket_part, "/ticket/%08x/%08x.tik", static_cast<unsigned>(tmd.title_id >> 32),
                  static_cast<unsigned>(tmd.title_id));
    if (!write_file(folder + ticket_part, ticket, error)) return false;
    logf("WADs: %s installed into %s (%u content(s), %llu bytes)\n", path.c_str(), folder.c_str(),
         static_cast<unsigned>(places.size()), static_cast<unsigned long long>(total));
    return true;
}

}  // namespace riftwii::wii
