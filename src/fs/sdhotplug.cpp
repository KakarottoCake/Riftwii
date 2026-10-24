// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/sdhotplug.hpp"

namespace riftwii {
namespace {
constexpr int kConfirmPolls = 2;
}

SdHotplug::SdHotplug(bool mounted) : mounted_(mounted) {}

SdHotplugEvent SdHotplug::update(bool inserted) {
    if (mounted_) {
        if (inserted) {
            away_ = 0;
            return SdHotplugEvent::None;
        }
        ++away_;
        if (away_ < kConfirmPolls) return SdHotplugEvent::None;
        mounted_ = false;
        away_ = 0;
        back_ = 0;
        return SdHotplugEvent::Removed;
    }
    if (!inserted) {
        back_ = 0;
        return SdHotplugEvent::None;
    }
    ++back_;
    if (back_ < kConfirmPolls) return SdHotplugEvent::None;
    mounted_ = true;
    away_ = 0;
    back_ = 0;
    return SdHotplugEvent::Inserted;
}

}  // namespace riftwii
