// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Finds the SDK's IPC entry points in a loaded game without SDK-specific
// byte patterns. The DVD driver issues every disc command as
//     li r4, <DI command>   ...   bl IOS_IoctlAsync
// so the branch target shared by the call sites of several distinct DI
// commands (0x71 DVDLowRead among them) is IOS_IoctlAsync. Likewise the
// open-partition site (`li r4, 0x8B` with `li r5, 3` and `li r6, 2` for
// the vector counts) names IOS_IoctlvAsync. Command numbers are from the
// wiibrew /dev/di table. Verified on Mario Kart Wii (RMCE01).
namespace riftwii {

struct CodeRange {
    std::uint32_t address = 0;     // load address of bytes[0]
    const std::uint8_t* bytes = nullptr;  // big-endian instructions
    std::size_t size = 0;
};

struct IpcSymbols {
    std::uint32_t ioctl_async = 0;
    unsigned ioctl_async_commands = 0;  // distinct DI commands whose sites agree
    std::uint32_t ioctlv_async = 0;     // 0 when not found (optional)
};

// Needs at least `min_commands` distinct DI commands (default 3), always
// including 0x71, to agree on one target inside the ranges.
bool find_ipc_symbols(const std::vector<CodeRange>& text, IpcSymbols& out, std::string& error,
                      unsigned min_commands = 3);

}  // namespace riftwii
