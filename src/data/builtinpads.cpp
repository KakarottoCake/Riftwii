// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/builtinpads.hpp"

namespace riftwii {

const BuiltinPad* find_builtin_pad(std::uint32_t vid_pid) {
    for (std::size_t i = 0; i < kBuiltinPadCount; ++i) {
        if (kBuiltinPads[i].vid_pid == vid_pid) return &kBuiltinPads[i];
    }
    return nullptr;
}

}  // namespace riftwii
