// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "riftwii/contentbin.hpp"

// WiiWare and Virtual Console games the Wii Menu moved to the SD card
// (riftwii/contentbin.hpp): the Wii keeps their TMD, ticket and save, and
// their contents are in sd:/private/wii/title/<code>/content.bin, encrypted
// with this Wii's own key. RiftWii lists them, reads their banner and
// program straight from that file, and, as the Wii Menu does to start
// one, copies the contents back into the title's NAND folder for the
// session, since the game opens them through ES while it runs. The copies
// are removed again at RiftWii's next start.
namespace riftwii::wii {

// sd:/private/wii/title/<code>/content.bin when the card has it, else empty.
std::string moved_channel_file(const std::string& code);

// One content.bin, opened and checked against its title, with the key
// that decrypts it: this Wii's own, or, for a card from another Wii, the
// one in that Wii's BootMii keys.bin on the card.
class MovedChannel {
public:
    MovedChannel() = default;
    ~MovedChannel();
    MovedChannel(const MovedChannel&) = delete;
    MovedChannel& operator=(const MovedChannel&) = delete;

    // False with `error` in a plain clause when the file is not the
    // moved copy of `title_id`, or no key for it is at hand.
    bool open(const std::string& path, std::uint64_t title_id, std::string& error);
    const TmdContents& tmd() const { return tmd_; }
    const std::string& path() const { return path_; }
    // Whether the content with this ID is in the file (shared ones never are).
    bool has(std::uint32_t content_id) const;
    // Its first `bytes` (all of it when `bytes` is its size or more),
    // decrypted; the whole content is checked against the TMD's hash.
    bool read(std::uint32_t content_id, std::uint64_t bytes, std::vector<std::uint8_t>& out, std::string& error);
    // The whole content in pieces of any size, for staging, hash-checked.
    bool stream(std::uint32_t content_id, const std::function<bool(const std::uint8_t*, std::size_t)>& sink,
                std::string& error);

private:
    const WadContentPlace* place_of(std::uint32_t content_id) const;

    FILE* file_ = nullptr;
    std::string path_;
    TmdContents tmd_;
    std::vector<WadContentPlace> places_;
    BackupKey key_;
};

// Copies the contents of a moved channel that its NAND folder lacks into
// it, decrypted, as the Wii Menu does before starting one; each is listed
// in sd:/riftwii/staged_channels.txt before it is written, so
// UnstageMovedChannels removes it again. Nothing to do for a channel that
// was not moved, or one the Wii has in full whatever the card's copy is.
// `progress` gets the fraction done. False with `error` when the list on
// the card or the Wii's memory refused a file (full?), or the card's copy
// is damaged or not the Wii's TMD's; what this call wrote is then removed.
bool stage_moved_channel(std::uint64_t title_id, const std::function<void(double)>& progress, std::string& error);

// At start: removes the contents an earlier session copied to the Wii
// for a moved channel, while that channel's content.bin is still on the
// card (a channel the Wii Menu has since moved back keeps its files).
// Only exact content paths of channel titles are taken from the list, and
// only files staging made (marked by their FS attribute), so a file ES
// installed is never removed. The ones that could not be removed, or whose
// content.bin could not be checked, stay listed for the next start.
void UnstageMovedChannels();

}  // namespace riftwii::wii
