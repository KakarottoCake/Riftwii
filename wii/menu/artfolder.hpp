// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <sys/stat.h>

#include <string>

// "Is this picture stored?" for many games at once.
//
// Home asks, for every game it lists, whether its cover, its box and its
// 3D cover are on the card, or were missed lately. Each answer was a
// stat(), and libfat's stat walks the folder from its first file: for a
// view of a few hundred games that was over a thousand walks of the same
// folders, seconds during which Home answered nothing (All games after
// the Mods view, and the end of every look at the drives).
//
// While an ArtFolderLook is open, each folder asked about is listed once
// and the questions are answered from its names (riftwii/foldernames.hpp).
// The listing lives only as long as the look, a moment on the menu's
// thread, so it never stands for a folder that has changed since.
namespace riftwii::wii {

// Open one around a run of ArtFileThere / ArtFileStat questions, on the
// thread that asks them. Looks nest; the listings go when the outermost
// one closes.
class ArtFolderLook {
public:
    ArtFolderLook();
    ~ArtFolderLook();
    ArtFolderLook(const ArtFolderLook&) = delete;
    ArtFolderLook& operator=(const ArtFolderLook&) = delete;
};

// Whether the file at `path` is there, as stat() == 0 says. Inside a look
// (on the look's thread) the answer comes from the folder's listing;
// anywhere else, or for a folder that could not be listed, from the card.
bool ArtFileThere(const std::string& path);

// The same with the file's details (its time and size), which only the
// card has: inside a look a file the listing does not name costs nothing,
// and one it names is asked for as before.
bool ArtFileStat(const std::string& path, struct stat& info);

}  // namespace riftwii::wii
