// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// The player's own menu music (settings.txt music_file): one song, a
// playlist, or a whole folder played in a shuffled order. The songs are
// the kinds other Wii loaders' music players take: Ogg Vorbis (.ogg) and
// MP3 (.mp3), and a playlist is an .m3u (or .m3u8) list of them, one path
// a line, as a PC's music player writes it.
namespace riftwii {

enum class MusicFormat { None, Ogg, Mp3 };

// Most songs read from one folder (and its folders) or one playlist: each
// is a path kept in memory, and the menu walks the list.
constexpr std::size_t kMaxMusicSongs = 1000;
// How many folders down a folder's songs are looked for: "Music" finds
// "Music/Artist/Album/song.mp3", and no deeper.
constexpr unsigned kMaxMusicDepth = 3;

// The format by the name's extension, any case; None for anything else.
MusicFormat music_format(const std::string& name);
// An .m3u or .m3u8 playlist, by the name's extension.
bool music_is_playlist(const std::string& name);
// A folder pick ends in '/' ("sd:/music/"), so it is told from a file
// without looking at the card.
bool music_choice_is_folder(const std::string& choice);
// What settings.txt's music_file may say: "" (RiftWii's own music), or an
// "sd:/" or "usb:/" path to a song, a playlist or a folder ending in '/',
// with no ".." in it.
bool music_choice_valid(const std::string& choice);
// The name a choice shows in Settings: the file's, or the folder's
// ("sd:/music/" is "music", "usb:/" is "usb:").
std::string music_choice_name(const std::string& choice);

// The songs a playlist names, in its order. `playlist_path` is where the
// playlist is ("usb:/lists/car.m3u"); a line's path is read as a PC wrote
// it: '\' becomes '/', a drive ("E:") or a leading '/' means the top of
// the playlist's own device, and anything else is next to the playlist.
// Lines starting with '#' (#EXTM3U, #EXTINF) and lines that are not .ogg
// or .mp3 are skipped, as are paths that climb out with "..".
std::vector<std::string> music_playlist_songs(const std::string& text, const std::string& playlist_path,
                                              std::size_t max_songs = kMaxMusicSongs);

struct MusicFolderEntry {
    std::string name;
    bool is_directory = false;
};
// Lists one folder ("sd:/music"); false when it cannot be read.
using MusicFolderLister = std::function<bool(const std::string& folder, std::vector<MusicFolderEntry>& out)>;
// Every .ogg and .mp3 in `folder` ("sd:/music/") and its folders down to
// `max_depth`, sorted by path. Names starting with '.' are left out (a
// Mac's "._song.mp3" next to each song, its hidden folders).
std::vector<std::string> music_folder_songs(const std::string& folder, const MusicFolderLister& list,
                                            unsigned max_depth = kMaxMusicDepth,
                                            std::size_t max_songs = kMaxMusicSongs);

// Shuffles `songs` in place from `seed`. With more than one song, the
// first is never `not_first`, so a new round does not start with the
// song the last one ended on.
void music_shuffle(std::vector<std::string>& songs, std::uint32_t seed, const std::string& not_first = "");

}  // namespace riftwii
