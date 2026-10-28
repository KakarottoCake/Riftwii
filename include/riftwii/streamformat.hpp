// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdarg>
#include <cstdio>

namespace riftwii {

// FILE output through the same formatter as snprintf. Short messages need
// no heap; longer ones are kept whole. Returns the byte count, or -1 on a
// formatting, allocation or write error, as vfprintf does. Does not flush.
int format_to_stream(FILE* stream, const char* format, va_list args);

}  // namespace riftwii
