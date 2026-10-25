// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <gctypes.h>

#include <string>

#include "riftwii/riitag.hpp"

// The player's RiiTag picture for Settings > Online > My RiiTag: tag.max.png
// for riitag_id in settings.txt (riftwii/riitag.hpp), downloaded on the
// network's background thread (netsock.hpp) and shrunk to a
// kRiiTagShownWidth x kRiiTagShownHeight RGBA8 texture. The page that shows
// it is MenuRiiTag in rift_menu.cpp.
//
// Memory, MEM2 first as the theme browser does: the download, the PNG
// decoder's rows and the texture live in blocks taken at Open and given
// back at Close (when nothing was taken after them; else kept for the next
// visit). The picture is 1200 x 450, about 8 MB of work while it is read.
namespace riftwii::wii::riitag {

// Takes the page's MEM2. False (with `error`) when there is none.
bool Open(std::string& error);
// Stops the download (waiting for it) and gives the memory back.
void Close();

// Starts the download of `id`'s tag. False when one is still running.
bool Start(const std::string& id);

enum class TagState { Idle, Loading, Ready, Failed };
// Where the download is; `error` says why it failed. Called by the menu
// thread, which takes a finished download's answer here.
TagState Status(std::string& error);

// The tag as an RGBA8 texture, kRiiTagShownWidth x kRiiTagShownHeight,
// once Ready; else nullptr. GUI thread.
const u8* Texture();

}  // namespace riftwii::wii::riitag
