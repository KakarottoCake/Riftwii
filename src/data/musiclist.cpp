// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/musiclist.hpp"

#include <algorithm>
#include <utility>

#include "riftwii/checkedalloc.hpp"
#include "riftwii/textlines.hpp"
#include "riftwii/imagevolume.hpp"

namespace riftwii {

namespace {

std::string lower(std::string text) {
    for (char& c : text)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

bool ends_with(const std::string& text, const char* tail) {
    const std::string end(tail);
    return text.size() >= end.size() && text.compare(text.size() - end.size(), end.size(), end) == 0;
}

// The device a path is on, with its colon ("usb:"); "" without one.
std::string device_of(const std::string& path) {
    const std::size_t colon = path.find(':');
    const std::size_t slash = path.find('/');
    if (colon == std::string::npos || (slash != std::string::npos && slash < colon)) return "";
    return path.substr(0, colon + 1);
}

// Whether any piece of the path between '/' is "..".
bool climbs(const std::string& path) {
    std::size_t at = 0;
    while (at <= path.size()) {
        std::size_t end = path.find('/', at);
        if (end == std::string::npos) end = path.size();
        if (path.compare(at, end - at, "..") == 0 && end - at == 2) return true;
        at = end + 1;
    }
    return false;
}

std::string trim(const std::string& text) {
    std::size_t start = 0;
    std::size_t end = text.size();
    while (start < end && is_stream_space(text[start])) ++start;
    while (end > start && is_stream_space(text[end - 1])) --end;
    return text.substr(start, end - start);
}

// A small generator of its own, so a seed shuffles the same way on the
// Wii and in the host tests (std::shuffle's engines differ by library).
struct ShuffleRandom {
    std::uint32_t state;
    explicit ShuffleRandom(std::uint32_t seed) : state(seed ? seed : 0x9E3779B9u) {}
    std::uint32_t next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    std::size_t below(std::size_t count) { return static_cast<std::size_t>(next() % count); }
};

void collect_songs(const std::string& folder, const MusicFolderLister& list, unsigned depth_left,
                   std::size_t max_songs, std::vector<std::string>& out) {
    // Listed without its last '/' ("sd:/music"), as opendir and the USB
    // drive's view both take it, except a device's top ("sd:/").
    std::string listed = folder;
    if (ends_with(listed, "/") && !ends_with(listed, ":/")) listed.pop_back();
    std::vector<MusicFolderEntry> entries;
    if (!list(listed, entries)) return;
    const std::string base = listed + (ends_with(listed, "/") ? "" : "/");
    for (const MusicFolderEntry& entry : entries) {
        if (out.size() >= max_songs) return;
        if (entry.name.empty() || entry.name[0] == '.') continue;
        if (entry.is_directory) {
            if (depth_left > 0) collect_songs(base + entry.name, list, depth_left - 1, max_songs, out);
        } else if (music_format(entry.name) != MusicFormat::None) {
            out.push_back(base + entry.name);
        }
    }
}

}  // namespace

bool music_read_playlist(std::FILE* file, std::string& text) {
    text.clear();
    char chunk[4096];
    std::size_t got = 0;
    while ((got = std::fread(chunk, 1, sizeof chunk, file)) > 0) {
        // Read through the limit so a file exactly at it still succeeds,
        // but one extra byte rejects the entire playlist.
        if (got > kMaxMusicPlaylistBytes - text.size() || !try_append(text, chunk, got)) {
            text.clear();
            return false;
        }
    }
    if (std::ferror(file)) {
        text.clear();
        return false;
    }
    return true;
}

bool music_try_song_batch(std::size_t& remaining, const std::function<bool()>& play_next) {
    constexpr std::size_t kMaxSongTries = 8;
    for (std::size_t tries = 0; tries < kMaxSongTries && remaining > 0; ++tries) {
        --remaining;
        if (play_next()) {
            remaining = 0;
            return true;
        }
    }
    return false;
}

MusicFormat music_format(const std::string& name) {
    const std::string low = lower(name);
    if (ends_with(low, ".ogg")) return MusicFormat::Ogg;
    if (ends_with(low, ".mp3")) return MusicFormat::Mp3;
    return MusicFormat::None;
}

bool music_is_playlist(const std::string& name) {
    const std::string low = lower(name);
    return ends_with(low, ".m3u") || ends_with(low, ".m3u8");
}

bool music_choice_is_folder(const std::string& choice) { return ends_with(choice, "/"); }

bool music_choice_valid(const std::string& choice) {
    if (choice.empty()) return true;
    if (choice.compare(0, 4, "sd:/") != 0 && !riftwii::is_usb_path(choice)) return false;
    if (climbs(choice)) return false;
    return music_choice_is_folder(choice) || music_format(choice) != MusicFormat::None || music_is_playlist(choice);
}

std::string music_choice_name(const std::string& choice) {
    std::string path = choice;
    if (music_choice_is_folder(path)) path.pop_back();
    const std::size_t slash = path.rfind('/');
    if (slash == std::string::npos) return path;
    return path.substr(slash + 1);
}

std::vector<std::string> music_playlist_songs(const std::string& text, const std::string& playlist_path,
                                              std::size_t max_songs) {
    std::vector<std::string> songs;
    const std::string device = device_of(playlist_path);
    const std::size_t last_slash = playlist_path.rfind('/');
    const std::string folder = last_slash == std::string::npos ? device + "/" : playlist_path.substr(0, last_slash + 1);
    // A PC's player may end its lines with "\r\n" or a lone '\r'.
    std::string unified = text;
    std::replace(unified.begin(), unified.end(), '\r', '\n');
    LineReader lines(unified);
    std::string line;
    while (songs.size() < max_songs && lines.next(line)) {
        line = trim(line);
        // A UTF-8 .m3u8 may start with a byte order mark.
        if (line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
        if (line.empty() || line[0] == '#') continue;
        if (music_format(line) == MusicFormat::None) continue;
        std::replace(line.begin(), line.end(), '\\', '/');
        std::string song;
        const std::size_t colon = line.find(':');
        const std::size_t first_slash = line.find('/');
        if (colon != std::string::npos && (first_slash == std::string::npos || colon < first_slash)) {
            // "E:/Music/song.mp3": the drive's letter means nothing on the
            // Wii, so it is the top of the playlist's own device.
            if (first_slash == std::string::npos) continue;
            song = device + line.substr(first_slash);
        } else if (first_slash == 0) {
            song = device + line;
        } else {
            song = folder + line;
        }
        if (climbs(song)) continue;
        songs.push_back(song);
    }
    return songs;
}

std::vector<std::string> music_folder_songs(const std::string& folder, const MusicFolderLister& list,
                                            unsigned max_depth, std::size_t max_songs) {
    std::vector<std::string> songs;
    collect_songs(folder, list, max_depth, max_songs, songs);
    std::sort(songs.begin(), songs.end());
    return songs;
}

void music_shuffle(std::vector<std::string>& songs, std::uint32_t seed, const std::string& not_first) {
    ShuffleRandom random(seed);
    // Fisher-Yates: every order equally likely.
    for (std::size_t i = songs.size(); i > 1; --i) std::swap(songs[i - 1], songs[random.below(i)]);
    if (songs.size() > 1 && !not_first.empty() && songs.front() == not_first) {
        const std::size_t other = 1 + random.below(songs.size() - 1);
        std::swap(songs.front(), songs[other]);
    }
}

}  // namespace riftwii
