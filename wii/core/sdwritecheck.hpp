// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "riftwii/sdwrite.hpp"

namespace riftwii::wii {

void ProbeSdWrite(bool sd_mounted);
::riftwii::SdWriteKind SdWriteKindGet();
bool SdWriteNoticeShown();
void SdWriteNoticeMarkShown();
void SdWriteReset();

}  // namespace riftwii::wii
