// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "usbcatalog.hpp"
#include "riftwii/gamepage.hpp"

namespace riftwii::wii {

bool PlanGameDeletion(const ImageGame& game, GameDeletion& plan, std::string& error);
bool HasOtherGameCopy(const ImageGame& game, const GameDeletion& plan, const ImageCatalog& catalog);
// False also covers partial deletion. The caller keeps the page open so it
// can retry, and shows the paths that could not be removed.
bool DeleteGameFiles(const ImageGame& game, const GameDeletion& plan, bool saves,
                     std::vector<std::string>& remaining, std::string& error);

}  // namespace riftwii::wii
