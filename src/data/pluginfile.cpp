// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/pluginfile.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "riftwii/sha256.hpp"

namespace riftwii {
namespace {

constexpr char kMagic[4] = {'R', 'W', 'P', 'L'};
constexpr char kDefaultDirectory[] = "sd:/apps/riftwii/plugins/";
constexpr char kExtension[] = ".rwp";

std::uint32_t Read32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

void Write32(std::uint8_t* p, std::uint32_t value) {
    p[0] = static_cast<std::uint8_t>(value >> 24);
    p[1] = static_cast<std::uint8_t>(value >> 16);
    p[2] = static_cast<std::uint8_t>(value >> 8);
    p[3] = static_cast<std::uint8_t>(value);
}

void Write16(std::uint8_t* p, std::uint32_t value) {
    p[0] = static_cast<std::uint8_t>(value >> 8);
    p[1] = static_cast<std::uint8_t>(value);
}

// In 64 bits: a damaged size near 4 GiB must not wrap round to a small one.
std::uint64_t Align32(std::uint32_t value) { return (std::uint64_t(value) + 31u) & ~std::uint64_t(31); }

bool NameCharacter(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

// A NUL-padded text field: its text, when every byte before the first NUL
// is printable ASCII and every byte after it is NUL. `ok` false otherwise.
std::string PaddedText(const std::uint8_t* field, std::size_t width, bool& ok) {
    std::string text;
    std::size_t i = 0;
    for (; i < width && field[i] != 0; ++i) {
        if (field[i] < 0x20 || field[i] > 0x7E) {
            ok = false;
            return text;
        }
        text += static_cast<char>(field[i]);
    }
    for (; i < width; ++i) {
        if (field[i] != 0) {
            ok = false;
            return text;
        }
    }
    ok = true;
    return text;
}

// The reason for the log, formatted once: plain printf keeps the loader
// small (each string built from pieces costs a few hundred bytes of MEM1).
void SayV(std::string& why, const char* format, va_list args) {
    char text[200];
    std::vsnprintf(text, sizeof(text), format, args);
    why = text;
}

void Say(std::string& why, const char* format, ...) __attribute__((format(printf, 2, 3)));
void Say(std::string& why, const char* format, ...) {
    va_list args;
    va_start(args, format);
    SayV(why, format, args);
    va_end(args);
}

// A header check that failed: Damaged, with `why` saying which.
PluginRefusal Damaged(std::string& why, const char* format, ...) __attribute__((format(printf, 2, 3)));
PluginRefusal Damaged(std::string& why, const char* format, ...) {
    va_list args;
    va_start(args, format);
    SayV(why, format, args);
    va_end(args);
    return PluginRefusal::Damaged;
}

}  // namespace

std::uint32_t PluginHeader::memory_bytes() const {
    // Sizes past the budget are refused before this is used for an
    // allocation; the 64-bit sum keeps the check itself from wrapping.
    const std::uint64_t total = Align32(image_bytes) + Align32(bss_bytes) + Align32(state_bytes);
    return total > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(total);
}

std::uint64_t PluginHeader::file_bytes() const {
    return kPluginHeaderBytes + std::uint64_t(image_bytes) + std::uint64_t(reloc_count) * kPluginRelocBytes;
}

const char* plugin_refusal_name(PluginRefusal refusal) {
    switch (refusal) {
        case PluginRefusal::None: return "loaded";
        case PluginRefusal::Missing: return "missing";
        case PluginRefusal::Damaged: return "damaged";
        case PluginRefusal::Hash: return "wrong hash";
        case PluginRefusal::Api: return "wrong API version";
        case PluginRefusal::Stamp: return "wrong build stamp";
        case PluginRefusal::NoMemory: return "no memory";
        case PluginRefusal::InitFailed: return "init failed";
    }
    return "unknown";
}

bool plugin_name_valid(const std::string& name) {
    if (name.empty() || name.size() > kPluginNameBytes) return false;
    for (char c : name) {
        if (!NameCharacter(c)) return false;
    }
    return true;
}

PluginRefusal plugin_parse_header(const std::uint8_t* bytes, std::size_t available, std::uint64_t file_bytes,
                                  const std::string& expected_name, std::uint32_t budget, PluginHeader& out,
                                  std::string& why) {
    out = PluginHeader{};
    if (bytes == nullptr || available < kPluginHeaderBytes || file_bytes < kPluginHeaderBytes) {
        return Damaged(why, "the file is shorter than a plugin header (%llu bytes)",
                       static_cast<unsigned long long>(file_bytes));
    }
    if (std::memcmp(bytes, kMagic, sizeof(kMagic)) != 0) return Damaged(why, "the file does not start with RWPL");
    if (bytes[4] != kPluginHeaderVersion || bytes[5] != 0 || bytes[6] != 0 || bytes[7] != 0) {
        return Damaged(why, "header version %u, this RiftWii reads %u", static_cast<unsigned>(bytes[4]),
                static_cast<unsigned>(kPluginHeaderVersion));
    }
    bool text_ok = false;
    out.name = PaddedText(bytes + 8, kPluginNameBytes, text_ok);
    if (!text_ok || !plugin_name_valid(out.name)) return Damaged(why, "the name in the header is not a plugin name");
    if (out.name != expected_name) {
        return Damaged(why, "the header names \"%s\", the file is %s's", out.name.c_str(), expected_name.c_str());
    }
    out.plugin_version = Read32(bytes + 24);
    out.api_version = Read32(bytes + 28);
    out.stamp = PaddedText(bytes + 32, kPluginStampBytes, text_ok);
    if (!text_ok || out.stamp.empty()) return Damaged(why, "the build stamp in the header is not text");
    out.init_offset = Read32(bytes + 64);
    out.shutdown_offset = Read32(bytes + 68);
    out.table_offset = Read32(bytes + 72);
    out.image_bytes = Read32(bytes + 76);
    out.bss_bytes = Read32(bytes + 80);
    out.reloc_count = Read32(bytes + 84);
    out.state_bytes = Read32(bytes + 88);
    out.flags = Read32(bytes + 92);
    std::memcpy(out.hash, bytes + kPluginHashOffset, sizeof(out.hash));

    if (out.image_bytes == 0 || (out.image_bytes & 31) != 0) {
        return Damaged(why, "image size %u is not a whole number of 32-byte lines", static_cast<unsigned>(out.image_bytes));
    }
    if (out.reloc_count > kPluginMaxRelocs) {
        return Damaged(why, "%u relocations, more than a plugin can have", static_cast<unsigned>(out.reloc_count));
    }
    if (out.file_bytes() != file_bytes) {
        return Damaged(why, "the header describes %llu bytes, the file has %llu (cut short or added to)",
                static_cast<unsigned long long>(out.file_bytes()), static_cast<unsigned long long>(file_bytes));
    }
    const std::uint64_t memory = Align32(out.image_bytes) + Align32(out.bss_bytes) + Align32(out.state_bytes);
    if (memory > budget) {
        return Damaged(why, "it needs %llu bytes, more than the plugin arena's %u", static_cast<unsigned long long>(memory),
                static_cast<unsigned>(budget));
    }
    const auto entry_ok = [&](std::uint32_t offset, std::uint32_t length) {
        return (offset & 3) == 0 && std::uint64_t(offset) + length <= out.image_bytes;
    };
    if (!entry_ok(out.init_offset, 4)) return Damaged(why, "init offset 0x%x is outside the image", static_cast<unsigned>(out.init_offset));
    // Offset 0 is the reserved word (plugins/plugin.ld), never code: a
    // shutdown there would read as none.
    if (out.init_offset == 0) return Damaged(why, "init is at image offset 0, which is reserved");
    if (out.shutdown_offset != 0 && !entry_ok(out.shutdown_offset, 4)) {
        return Damaged(why, "shutdown offset 0x%x is outside the image", static_cast<unsigned>(out.shutdown_offset));
    }
    // The whole table: the loader reads its probe and shutdown pointers too.
    if (!entry_ok(out.table_offset, kPluginTableBytes)) {
        return Damaged(why, "table offset 0x%x is outside the image", static_cast<unsigned>(out.table_offset));
    }
    return PluginRefusal::None;
}

void plugin_hash_header(const std::uint8_t* header_bytes, std::uint8_t (&out)[kPluginHeaderBytes]) {
    std::memcpy(out, header_bytes, kPluginHeaderBytes);
    std::memset(out + kPluginHashOffset, 0, 32);
}

bool plugin_hash_matches(const std::uint8_t* header_bytes, const std::uint8_t* rest, std::size_t rest_bytes) {
    std::uint8_t zeroed[kPluginHeaderBytes];
    plugin_hash_header(header_bytes, zeroed);
    Sha256 hasher;
    hasher.update(zeroed, sizeof(zeroed));
    if (rest_bytes != 0) hasher.update(rest, rest_bytes);
    const Sha256Digest digest = hasher.finish();
    return std::memcmp(digest.data(), header_bytes + kPluginHashOffset, digest.size()) == 0;
}

PluginRefusal plugin_check_versions(const PluginHeader& header, std::uint32_t api_version, const std::string& stamp,
                                    std::string& why) {
    if (header.api_version != api_version) {
        Say(why, "it is for plugin API %u, this RiftWii has API %u", static_cast<unsigned>(header.api_version),
            static_cast<unsigned>(api_version));
        return PluginRefusal::Api;
    }
    if (header.stamp != stamp) {
        Say(why, "it was built with RiftWii %s, this is %s", header.stamp.c_str(), stamp.c_str());
        return PluginRefusal::Stamp;
    }
    return PluginRefusal::None;
}

bool plugin_relocate(std::uint8_t* image, const PluginHeader& header, const std::uint8_t* relocs, std::uint32_t base,
                     std::string& why) {
    const std::uint64_t target_limit = std::uint64_t(header.image_bytes) + header.bss_bytes;
    // Checked as a whole first, so a bad record leaves the image as it was.
    for (std::uint32_t i = 0; i < header.reloc_count; ++i) {
        const std::uint8_t* record = relocs + std::size_t(i) * kPluginRelocBytes;
        const std::uint32_t offset = Read32(record);
        const std::uint32_t type = Read32(record + 4);
        const std::uint32_t target = Read32(record + 8);
        std::uint32_t width = 0;
        if (type == kRelocAddr32) width = 4;
        else if (type == kRelocAddr16Lo || type == kRelocAddr16Hi || type == kRelocAddr16Ha) width = 2;
        if (width == 0) {
            Say(why, "relocation %u has type %u, which the loader cannot apply", static_cast<unsigned>(i),
                static_cast<unsigned>(type));
            return false;
        }
        if (std::uint64_t(offset) + width > header.image_bytes) {
            Say(why, "relocation %u at 0x%x lies outside the image", static_cast<unsigned>(i), static_cast<unsigned>(offset));
            return false;
        }
        if (target > target_limit) {
            Say(why, "relocation %u points to 0x%x, past the image and .bss", static_cast<unsigned>(i),
                static_cast<unsigned>(target));
            return false;
        }
    }
    for (std::uint32_t i = 0; i < header.reloc_count; ++i) {
        const std::uint8_t* record = relocs + std::size_t(i) * kPluginRelocBytes;
        const std::uint32_t offset = Read32(record);
        const std::uint32_t type = Read32(record + 4);
        const std::uint32_t address = base + Read32(record + 8);
        std::uint8_t* field = image + offset;
        if (type == kRelocAddr32) {
            Write32(field, address);
        } else if (type == kRelocAddr16Lo) {
            Write16(field, address & 0xFFFFu);
        } else if (type == kRelocAddr16Hi) {
            Write16(field, address >> 16);
        } else {
            // The high half adjusted for the signed low half an addi or a
            // load adds to it.
            Write16(field, ((address + 0x8000u) >> 16) & 0xFFFFu);
        }
    }
    return true;
}

PluginRefusal plugin_check_table(const std::uint8_t* image, const PluginHeader& header, std::uint32_t base,
                                 std::uint32_t api_version, std::string& why) {
    const std::uint8_t* table = image + header.table_offset;
    const std::uint32_t size = Read32(table);
    const std::uint32_t table_api = Read32(table + 4);
    if (table_api != api_version || size < kPluginTableBytes) {
        return Damaged(why, "its own table says API %u and %u bytes", static_cast<unsigned>(table_api),
                       static_cast<unsigned>(size));
    }
    // Addresses, relocated: code the loader may call must be a word inside
    // the image, never past it (the .bss, the state) or elsewhere in memory,
    // and never its first word, which is reserved (plugins/plugin.ld).
    const auto code_inside = [&](std::uint32_t address) {
        const std::uint32_t offset = address - base;
        return address > base && (offset & 3) == 0 && offset < header.image_bytes;
    };
    const std::uint32_t probe = Read32(table + 16);
    if (probe != 0 && !code_inside(probe)) {
        return Damaged(why, "its table's probe 0x%08x is not a word of code inside its image",
                       static_cast<unsigned>(probe));
    }
    const std::uint32_t launch = Read32(table + 24);
    if (launch != 0 && !code_inside(launch)) {
        return Damaged(why, "its table's launch 0x%08x is not a word of code inside its image",
                       static_cast<unsigned>(launch));
    }
    const std::uint32_t shutdown = Read32(table + 20);
    const std::uint32_t header_shutdown = header.shutdown_offset != 0 ? base + header.shutdown_offset : 0;
    if (shutdown == base) {
        // Linked without plugins/plugin.ld's reserved word: a shutdown at
        // offset 0, which the header can only call "none".
        return Damaged(why, "its table's shutdown is at image offset 0, which is reserved and the header reads as none");
    }
    if (shutdown != header_shutdown) {
        return Damaged(why, "its table's shutdown 0x%08x is not the header's 0x%08x", static_cast<unsigned>(shutdown),
                       static_cast<unsigned>(header_shutdown));
    }
    return PluginRefusal::None;
}

std::string plugin_file_name(const std::string& name, const std::string& version) {
    return name + "." + version + kExtension;
}

bool plugin_file_parse(const std::string& file, std::string& name, std::string& version) {
    const std::size_t extension_bytes = sizeof(kExtension) - 1;
    if (file.size() <= extension_bytes || file.compare(file.size() - extension_bytes, extension_bytes, kExtension) != 0) {
        return false;
    }
    const std::size_t dot = file.find('.');
    const std::size_t stem_end = file.size() - extension_bytes;
    if (dot == std::string::npos || dot >= stem_end) return false;
    const std::string plugin = file.substr(0, dot);
    const std::string stamp = file.substr(dot + 1, stem_end - dot - 1);
    if (!plugin_name_valid(plugin) || stamp.empty()) return false;
    for (char c : stamp) {
        if (!NameCharacter(c)) return false;  // also refuses a second '.'
    }
    name = plugin;
    version = stamp;
    return true;
}

bool plugin_file_of_other_version(const std::string& file, const std::string& version, std::string& name) {
    std::string plugin;
    std::string its_version;
    if (!plugin_file_parse(file, plugin, its_version) || its_version == version) return false;
    name = plugin;
    return true;
}

std::string plugin_directory_for(const std::string& given) {
    // Older loaders pass "/apps/riftwii/boot.dol", without the drive: the
    // SD card's (as wii/online/online.cpp's RunningDolPath reads it).
    const std::string argv0 = given.rfind("/apps/", 0) == 0 ? "sd:" + given : given;
    // "sd:/apps/riftwii/boot.dol": a drive, a colon, a path with a folder.
    const std::size_t colon = argv0.find(":/");
    const std::size_t slash = argv0.rfind('/');
    if (colon == std::string::npos || colon == 0 || slash == std::string::npos || slash <= colon + 1) {
        return kDefaultDirectory;
    }
    for (std::size_t i = 0; i < colon; ++i) {
        const char c = argv0[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return kDefaultDirectory;
    }
    return argv0.substr(0, slash + 1) + "plugins/";
}

bool plugin_describe_address(std::uint32_t address, const PluginRange* ranges, std::size_t count, char* out,
                             std::size_t out_bytes) {
    for (std::size_t i = 0; i < count; ++i) {
        const PluginRange& range = ranges[i];
        if (range.bytes == 0 || address < range.start || address - range.start >= range.bytes) continue;
        std::snprintf(out, out_bytes, "plugin %s +0x%x", range.name, static_cast<unsigned>(address - range.start));
        return true;
    }
    return false;
}

}  // namespace riftwii
