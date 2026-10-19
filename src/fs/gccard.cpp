// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// GameCube memory card images. The layout is the card's own, as public
// documentation describes it (Yet Another GameCube Documentation, the
// memory card chapter); no other project's code is used.
//
// Block 0, the header: the serial (12 bytes), the format time (8), the
// SRAM's clock bias and language (4 each), the DTV register (4), the
// device ID, the size in megabits and the encoding (2 each), then the
// checksum pair at 0x1FC over everything before it.
// Blocks 1 and 2, the directory: 127 entries of 64 bytes, the update
// counter at 0x1FFA and the checksum pair at 0x1FFC over what precedes it.
// Blocks 3 and 4, the BAT: the checksum pair at 0 over the rest of the
// block, the update counter at 4, the free block count at 6, the last
// allocated block at 8, then one word per block from block 5 on: 0 for a
// free block, 0xFFFF for a save's last, else the save's next block.
#include "riftwii/gccard.hpp"

#include <algorithm>
#include <cstring>

namespace riftwii {

namespace {

constexpr std::uint32_t kHeaderChecksum = 0x1FC;
constexpr std::uint32_t kDirCounter = 0x1FFA;
constexpr std::uint32_t kDirChecksum = 0x1FFC;
constexpr std::uint32_t kBatChecksum = 0x0;
constexpr std::uint32_t kBatCounter = 0x4;
constexpr std::uint32_t kBatFree = 0x6;
constexpr std::uint32_t kBatLast = 0x8;
constexpr std::uint32_t kBatMap = 0xA;
constexpr std::uint16_t kChainEnd = 0xFFFF;
// The BAT block holds this many block words, which caps the card's size.
constexpr std::uint32_t kBatMapWords = (kGcCardBlockBytes - kBatMap) / 2;

// Directory entry fields.
constexpr std::uint32_t kEntryGame = 0x00;
constexpr std::uint32_t kEntryMaker = 0x04;
constexpr std::uint32_t kEntryBanner = 0x07;
constexpr std::uint32_t kEntryName = 0x08;
constexpr std::uint32_t kEntryNameBytes = 32;
constexpr std::uint32_t kEntryModified = 0x28;
constexpr std::uint32_t kEntryImage = 0x2C;
constexpr std::uint32_t kEntryIconFormat = 0x30;
constexpr std::uint32_t kEntryIconSpeed = 0x32;
constexpr std::uint32_t kEntryPermissions = 0x34;
constexpr std::uint32_t kEntryCopies = 0x35;
constexpr std::uint32_t kEntryFirstBlock = 0x36;
constexpr std::uint32_t kEntryBlockCount = 0x38;
constexpr std::uint32_t kEntryComment = 0x3C;
constexpr std::uint32_t kCommentLineBytes = 32;

// The banner and icon pictures (2 bits each in the entry).
constexpr unsigned kPictureNone = 0;
constexpr unsigned kPictureCi8 = 1;     // icons: CI8, one palette after the last frame
constexpr unsigned kPictureRgb5a3 = 2;
constexpr unsigned kPictureCi8Own = 3;  // icons only: CI8 with its own palette right after it
constexpr std::uint32_t kPaletteBytes = 256 * 2;
constexpr std::uint32_t kBannerPixels = 96 * 32;
constexpr std::uint32_t kIconPixels = kGcIconSide * kGcIconSide;
constexpr unsigned kIconFrames = 8;

std::uint16_t be16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] << 8 | p[1]); }

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

void put_be16(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 8);
    p[1] = static_cast<std::uint8_t>(v);
}

void put_be32(std::uint8_t* p, std::uint32_t v) {
    put_be16(p, v >> 16);
    put_be16(p + 2, v);
}

bool checksum_holds(const std::uint8_t* data, std::uint32_t bytes, const std::uint8_t* stored) {
    std::uint8_t computed[4];
    gc_card_checksum(data, bytes, computed);
    return std::memcmp(computed, stored, 4) == 0;
}

// The card sizes the GameCube made: 4 Mbit (59 save blocks) to 128 Mbit
// (2043).
bool known_size(std::uint32_t mbits) {
    return mbits == 4 || mbits == 8 || mbits == 16 || mbits == 32 || mbits == 64 || mbits == 128;
}

std::uint32_t card_bytes(std::uint32_t mbits) { return mbits * (0x100000u / 8u); }

// A fixed-size text field, up to its first NUL.
std::string text_field(const std::uint8_t* p, std::uint32_t bytes) {
    std::uint32_t length = 0;
    while (length < bytes && p[length] != 0) ++length;
    return std::string(reinterpret_cast<const char*>(p), length);
}

// Of a pair of copies, the one in use: the one whose checksum holds, or
// when both do the newer by update counter. The counter wraps, so it is
// compared as the SDK does, by the sign of the 16-bit difference.
bool pick_copy(bool valid0, bool valid1, std::uint16_t counter0, std::uint16_t counter1, unsigned& out) {
    if (valid0 && valid1) {
        const std::int16_t ahead = static_cast<std::int16_t>(counter1 - counter0);
        out = ahead > 0 ? 1 : 0;
        return true;
    }
    if (valid0 || valid1) {
        out = valid1 ? 1 : 0;
        return true;
    }
    return false;
}

struct Layout {
    std::uint32_t blocks = 0;
    unsigned dir = 0;
    unsigned bat = 0;
    bool shift_jis = false;
};

std::uint8_t* dir_block(std::vector<std::uint8_t>& card, unsigned copy) {
    return card.data() + (1 + copy) * kGcCardBlockBytes;
}
const std::uint8_t* dir_block(const std::vector<std::uint8_t>& card, unsigned copy) {
    return card.data() + (1 + copy) * kGcCardBlockBytes;
}
std::uint8_t* bat_block(std::vector<std::uint8_t>& card, unsigned copy) {
    return card.data() + (3 + copy) * kGcCardBlockBytes;
}
const std::uint8_t* bat_block(const std::vector<std::uint8_t>& card, unsigned copy) {
    return card.data() + (3 + copy) * kGcCardBlockBytes;
}

// The card's header and which copies of the directory and the BAT hold,
// from its first `have` bytes (at least the five system blocks) of a card
// `card_size` bytes long.
bool open_system(const std::uint8_t* data, std::size_t have, std::uint64_t card_size, Layout& layout,
                 std::string& error) {
    if (have < kGcCardSystemBlocks * kGcCardBlockBytes) {
        error = "The memory card image is too small to hold a card's header, directory and block table.";
        return false;
    }
    const std::uint8_t* header = data;
    if (!checksum_holds(header, kHeaderChecksum, header + kHeaderChecksum)) {
        error = "The memory card's header is damaged (its checksum does not match), or the card is not formatted.";
        return false;
    }
    const std::uint32_t mbits = be16(header + 0x22);
    if (!known_size(mbits)) {
        error = "The memory card's header gives a size no GameCube card has.";
        return false;
    }
    if (card_bytes(mbits) != card_size) {
        error = "The memory card image's length does not match the size its header gives.";
        return false;
    }
    layout.blocks = static_cast<std::uint32_t>(card_size / kGcCardBlockBytes);
    if (layout.blocks - kGcCardSystemBlocks > kBatMapWords) {
        error = "The memory card is larger than its block table can describe.";
        return false;
    }
    layout.shift_jis = be16(header + 0x24) == 1;

    const std::uint8_t* dir0 = data + 1 * kGcCardBlockBytes;
    const std::uint8_t* dir1 = data + 2 * kGcCardBlockBytes;
    const bool dir0_valid = checksum_holds(dir0, kDirChecksum, dir0 + kDirChecksum);
    const bool dir1_valid = checksum_holds(dir1, kDirChecksum, dir1 + kDirChecksum);
    if (!pick_copy(dir0_valid, dir1_valid, be16(dir0 + kDirCounter), be16(dir1 + kDirCounter), layout.dir)) {
        error = "Both copies of the memory card's directory are damaged.";
        return false;
    }
    const std::uint8_t* bat0 = data + 3 * kGcCardBlockBytes;
    const std::uint8_t* bat1 = data + 4 * kGcCardBlockBytes;
    const bool bat0_valid = checksum_holds(bat0 + kBatCounter, kGcCardBlockBytes - kBatCounter, bat0 + kBatChecksum);
    const bool bat1_valid = checksum_holds(bat1 + kBatCounter, kGcCardBlockBytes - kBatCounter, bat1 + kBatChecksum);
    if (!pick_copy(bat0_valid, bat1_valid, be16(bat0 + kBatCounter), be16(bat1 + kBatCounter), layout.bat)) {
        error = "Both copies of the memory card's block table are damaged.";
        return false;
    }
    return true;
}

bool open_card(const std::vector<std::uint8_t>& card, Layout& layout, std::string& error) {
    return open_system(card.data(), card.size(), card.size(), layout, error);
}

bool block_in_range(const Layout& layout, std::uint32_t block) {
    return block >= kGcCardSystemBlocks && block < layout.blocks;
}

std::uint32_t map_offset(std::uint32_t block) { return kBatMap + (block - kGcCardSystemBlocks) * 2; }

std::uint16_t next_block(const std::uint8_t* bat, std::uint32_t block) { return be16(bat + map_offset(block)); }

bool entry_used(const std::uint8_t* entry) { return entry[kEntryGame] != 0xFF; }

// Follows a save's chain through the BAT. `chain` gets every block walked
// before anything went wrong, so a damaged save can still be freed.
bool follow_chain(const Layout& layout, const std::uint8_t* bat, std::uint32_t first, std::uint32_t count,
                  std::vector<std::uint16_t>& chain) {
    chain.clear();
    if (count == 0 || count > layout.blocks - kGcCardSystemBlocks) return false;
    std::vector<bool> visited(layout.blocks, false);
    std::uint32_t block = first;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (!block_in_range(layout, block) || visited[block]) return false;
        visited[block] = true;
        chain.push_back(static_cast<std::uint16_t>(block));
        const std::uint16_t next = next_block(bat, block);
        const bool last = i + 1 == count;
        if (last) return next == kChainEnd;
        if (next == kChainEnd || next == 0) return false;  // ends early, or runs into a free block
        block = next;
    }
    return false;
}

// Copies `length` bytes at `offset` into the save, through its chain.
bool save_bytes(const std::vector<std::uint8_t>& card, const std::vector<std::uint16_t>& chain,
                std::uint32_t offset, std::uint32_t length, std::uint8_t* out) {
    const std::uint64_t save_length = std::uint64_t(chain.size()) * kGcCardBlockBytes;
    if (std::uint64_t(offset) + length > save_length) return false;
    for (std::uint32_t done = 0; done < length;) {
        const std::uint32_t at = offset + done;
        const std::uint32_t block = chain[at / kGcCardBlockBytes];
        const std::uint32_t inside = at % kGcCardBlockBytes;
        const std::uint32_t piece = std::min(length - done, kGcCardBlockBytes - inside);
        std::memcpy(out + done, card.data() + std::size_t(block) * kGcCardBlockBytes + inside, piece);
        done += piece;
    }
    return true;
}

// The card as read: its layout, the saves, and each save's chain as far
// as it could be followed.
struct Scan {
    Layout layout;
    GcCardInfo info;
    std::vector<std::vector<std::uint16_t>> chains;  // by index in info.saves
    // By index in info.saves: whether the chain followed cleanly to its
    // end at the save's block count (it may still share blocks).
    std::vector<bool> followed;
};

const GcSave* find_save(const Scan& scan, unsigned slot, std::size_t& index) {
    for (std::size_t i = 0; i < scan.info.saves.size(); ++i) {
        if (scan.info.saves[i].slot == slot) {
            index = i;
            return &scan.info.saves[i];
        }
    }
    return nullptr;
}

GcSave save_from_entry(const std::uint8_t* entry, unsigned slot) {
    GcSave save;
    save.slot = slot;
    save.game_code = text_field(entry + kEntryGame, 4);
    save.maker = text_field(entry + kEntryMaker, 2);
    save.file_name = text_field(entry + kEntryName, kEntryNameBytes);
    save.banner_format = entry[kEntryBanner];
    save.modified = be32(entry + kEntryModified);
    save.image_offset = be32(entry + kEntryImage);
    save.icon_format = be16(entry + kEntryIconFormat);
    save.icon_speed = be16(entry + kEntryIconSpeed);
    save.permissions = entry[kEntryPermissions];
    save.copy_count = entry[kEntryCopies];
    save.first_block = be16(entry + kEntryFirstBlock);
    save.block_count = be16(entry + kEntryBlockCount);
    save.comment_offset = be32(entry + kEntryComment);
    std::memcpy(save.entry.data(), entry, kGcCardEntryBytes);
    return save;
}

bool scan_card(const std::vector<std::uint8_t>& card, Scan& scan, std::string& error) {
    if (!open_card(card, scan.layout, error)) return false;
    const Layout& layout = scan.layout;
    const std::uint8_t* dir = dir_block(card, layout.dir);
    const std::uint8_t* bat = bat_block(card, layout.bat);

    GcCardInfo& info = scan.info;
    info.blocks = layout.blocks;
    info.shift_jis = layout.shift_jis;
    info.directory_copy = layout.dir;
    info.bat_copy = layout.bat;
    for (std::uint32_t block = kGcCardSystemBlocks; block < layout.blocks; ++block)
        if (next_block(bat, block) == 0) ++info.free_blocks;

    for (unsigned slot = 0; slot < kGcCardMaxEntries; ++slot) {
        const std::uint8_t* entry = dir + slot * kGcCardEntryBytes;
        if (!entry_used(entry)) {
            ++info.free_entries;
            continue;
        }
        GcSave save = save_from_entry(entry, slot);
        std::vector<std::uint16_t> chain;
        save.intact = follow_chain(layout, bat, save.first_block, save.block_count, chain);
        info.saves.push_back(save);
        scan.chains.push_back(chain);
        scan.followed.push_back(save.intact);
    }

    // A block two saves claim belongs to neither: both are marked damaged.
    std::vector<unsigned> claims(layout.blocks, 0);
    for (const auto& chain : scan.chains)
        for (std::uint16_t block : chain) ++claims[block];
    for (std::size_t i = 0; i < info.saves.size(); ++i) {
        GcSave& save = info.saves[i];
        for (std::uint16_t block : scan.chains[i])
            if (claims[block] > 1) save.intact = false;
        if (!save.intact) continue;

        std::uint8_t comment[2 * kCommentLineBytes];
        if (save_bytes(card, scan.chains[i], save.comment_offset, sizeof comment, comment)) {
            save.comment_title = text_field(comment, kCommentLineBytes);
            save.comment_detail = text_field(comment + kCommentLineBytes, kCommentLineBytes);
        }
    }
    return true;
}

// Writes `block` (a changed copy of the directory or BAT in use) into the
// copy not in use, its update counter one past the current one.
void commit_directory(std::vector<std::uint8_t>& card, const Layout& layout, std::vector<std::uint8_t>& block) {
    const std::uint16_t counter = be16(block.data() + kDirCounter);
    put_be16(block.data() + kDirCounter, static_cast<std::uint16_t>(counter + 1));
    gc_card_checksum(block.data(), kDirChecksum, block.data() + kDirChecksum);
    std::memcpy(dir_block(card, layout.dir ^ 1), block.data(), kGcCardBlockBytes);
}

void commit_bat(std::vector<std::uint8_t>& card, const Layout& layout, std::vector<std::uint8_t>& block) {
    // The free count is counted again rather than adjusted, so a card
    // whose count had drifted comes out right.
    std::uint32_t free_blocks = 0;
    for (std::uint32_t b = kGcCardSystemBlocks; b < layout.blocks; ++b)
        if (next_block(block.data(), b) == 0) ++free_blocks;
    put_be16(block.data() + kBatFree, free_blocks);
    const std::uint16_t counter = be16(block.data() + kBatCounter);
    put_be16(block.data() + kBatCounter, static_cast<std::uint16_t>(counter + 1));
    gc_card_checksum(block.data() + kBatCounter, kGcCardBlockBytes - kBatCounter, block.data() + kBatChecksum);
    std::memcpy(bat_block(card, layout.bat ^ 1), block.data(), kGcCardBlockBytes);
}

std::vector<std::uint8_t> copy_block(const std::uint8_t* block) {
    return std::vector<std::uint8_t>(block, block + kGcCardBlockBytes);
}

// RGB5A3 to RGBA8: with the top bit set 5 bits each of red, green and
// blue, opaque; without it 3 bits of alpha and 4 bits each of colour.
void rgb5a3_to_rgba(std::uint16_t v, std::uint8_t* out) {
    if (v & 0x8000) {
        const unsigned r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
        out[0] = static_cast<std::uint8_t>(r << 3 | r >> 2);
        out[1] = static_cast<std::uint8_t>(g << 3 | g >> 2);
        out[2] = static_cast<std::uint8_t>(b << 3 | b >> 2);
        out[3] = 0xFF;
    } else {
        const unsigned a = (v >> 12) & 7, r = (v >> 8) & 15, g = (v >> 4) & 15, b = v & 15;
        out[0] = static_cast<std::uint8_t>(r * 17);
        out[1] = static_cast<std::uint8_t>(g * 17);
        out[2] = static_cast<std::uint8_t>(b * 17);
        out[3] = static_cast<std::uint8_t>(a << 5 | a << 2 | a >> 1);
    }
}

std::uint32_t banner_bytes(unsigned format) {
    if (format == kPictureCi8) return kBannerPixels + kPaletteBytes;
    if (format == kPictureRgb5a3) return kBannerPixels * 2;
    return 0;
}

std::uint32_t icon_bytes(unsigned format) {
    if (format == kPictureCi8) return kIconPixels;  // its palette comes after the last frame
    if (format == kPictureCi8Own) return kIconPixels + kPaletteBytes;
    if (format == kPictureRgb5a3) return kIconPixels * 2;
    return 0;
}

}  // namespace

void gc_card_checksum(const std::uint8_t* data, std::uint32_t bytes, std::uint8_t* out) {
    std::uint16_t sum = 0;
    std::uint16_t inverse = 0;
    for (std::uint32_t i = 0; i + 1 < bytes; i += 2) {
        const std::uint16_t word = be16(data + i);
        sum = static_cast<std::uint16_t>(sum + word);
        inverse = static_cast<std::uint16_t>(inverse + (word ^ 0xFFFF));
    }
    if (sum == 0xFFFF) sum = 0;
    if (inverse == 0xFFFF) inverse = 0;
    put_be16(out, sum);
    put_be16(out + 2, inverse);
}

void gc_card_format(std::vector<std::uint8_t>& card, const GcCardFormat& format) {
    const std::uint32_t mbits = known_size(format.size_mbits) ? format.size_mbits : 16;
    card.assign(card_bytes(mbits), 0xFF);
    const std::uint32_t blocks = card_bytes(mbits) / kGcCardBlockBytes;

    // The serial: the flash ID, each byte offset by the next step of a
    // linear congruential sequence seeded with the format time.
    std::uint8_t* header = card.data();
    std::uint64_t rand = format.format_time;
    for (unsigned i = 0; i < 12; ++i) {
        rand = (rand * 0x41C64E6Dull + 0x3039ull) >> 16;
        header[i] = static_cast<std::uint8_t>(format.flash_id[i] + static_cast<std::uint32_t>(rand));
        rand = ((rand * 0x41C64E6Dull + 0x3039ull) >> 16) & 0x7FFF;
    }
    put_be32(header + 12, static_cast<std::uint32_t>(format.format_time >> 32));
    put_be32(header + 16, static_cast<std::uint32_t>(format.format_time));
    put_be32(header + 20, format.counter_bias);
    put_be32(header + 24, format.language);
    put_be32(header + 28, format.dtv);
    put_be16(header + 0x20, 0);                         // device ID
    put_be16(header + 0x22, mbits);                     // size, megabits
    put_be16(header + 0x24, format.shift_jis ? 1 : 0);  // the encoding
    gc_card_checksum(header, kHeaderChecksum, header + kHeaderChecksum);

    // Both directories stay erased (every entry unused, update counter 0xFFFF).
    for (unsigned copy = 0; copy < 2; ++copy) {
        std::uint8_t* dir = dir_block(card, copy);
        gc_card_checksum(dir, kDirChecksum, dir + kDirChecksum);
    }
    // Both BATs every block free; the second one newer, so in use.
    for (unsigned copy = 0; copy < 2; ++copy) {
        std::uint8_t* bat = bat_block(card, copy);
        std::fill(bat, bat + kGcCardBlockBytes, 0);
        put_be16(bat + kBatCounter, copy);
        put_be16(bat + kBatFree, blocks - kGcCardSystemBlocks);
        put_be16(bat + kBatLast, kGcCardSystemBlocks - 1);
        gc_card_checksum(bat + kBatCounter, kGcCardBlockBytes - kBatCounter, bat + kBatChecksum);
    }
}

bool gc_card_read(const std::vector<std::uint8_t>& card, GcCardInfo& out, std::string& error) {
    Scan scan;
    if (!scan_card(card, scan, error)) return false;
    out = scan.info;
    return true;
}

bool gc_card_space(const std::vector<std::uint8_t>& system_blocks, std::uint64_t card_bytes, GcCardSpace& out,
                   std::string& error) {
    Layout layout;
    if (!open_system(system_blocks.data(), system_blocks.size(), card_bytes, layout, error)) return false;
    const std::uint8_t* dir = system_blocks.data() + (1 + layout.dir) * kGcCardBlockBytes;
    const std::uint8_t* bat = system_blocks.data() + (3 + layout.bat) * kGcCardBlockBytes;
    out = GcCardSpace{};
    out.blocks = layout.blocks;
    // Counted as gc_card_read counts them, from the same copies.
    for (std::uint32_t block = kGcCardSystemBlocks; block < layout.blocks; ++block)
        if (next_block(bat, block) == 0) ++out.free_blocks;
    for (unsigned slot = 0; slot < kGcCardMaxEntries; ++slot)
        if (!entry_used(dir + slot * kGcCardEntryBytes)) ++out.free_entries;
    return true;
}

bool gc_card_delete(std::vector<std::uint8_t>& card, unsigned slot, std::string& error) {
    Scan scan;
    if (!scan_card(card, scan, error)) return false;
    std::size_t index = 0;
    if (!find_save(scan, slot, index)) {
        error = "There is no save in that slot of the memory card.";
        return false;
    }
    std::vector<std::uint8_t> dir = copy_block(dir_block(card, scan.layout.dir));
    std::vector<std::uint8_t> bat = copy_block(bat_block(card, scan.layout.bat));
    std::fill_n(dir.begin() + slot * kGcCardEntryBytes, kGcCardEntryBytes, 0xFF);
    // A block on another save's chain that followed cleanly stays taken,
    // even when that save is listed as damaged for sharing it: a damaged
    // save's walk can run into a good save's blocks, and freeing them
    // would let the next import overwrite that save.
    std::vector<bool> kept(scan.layout.blocks, false);
    for (std::size_t i = 0; i < scan.chains.size(); ++i) {
        if (i == index || !scan.followed[i]) continue;
        for (std::uint16_t block : scan.chains[i]) kept[block] = true;
    }
    for (std::uint16_t block : scan.chains[index])
        if (!kept[block]) put_be16(bat.data() + map_offset(block), 0);
    // The directory first: cut off after it, the card has blocks nobody
    // uses rather than a save whose blocks are free.
    commit_directory(card, scan.layout, dir);
    commit_bat(card, scan.layout, bat);
    return true;
}

bool gc_card_export(const std::vector<std::uint8_t>& card, unsigned slot, std::vector<std::uint8_t>& gci,
                    std::string& error) {
    Scan scan;
    if (!scan_card(card, scan, error)) return false;
    std::size_t index = 0;
    const GcSave* save = find_save(scan, slot, index);
    if (!save) {
        error = "There is no save in that slot of the memory card.";
        return false;
    }
    if (!save->intact) {
        error = "The save's blocks on the memory card are damaged, so it cannot be copied.";
        return false;
    }
    const std::vector<std::uint16_t>& chain = scan.chains[index];
    gci.assign(save->entry.begin(), save->entry.end());
    gci.resize(kGcCardEntryBytes + chain.size() * kGcCardBlockBytes);
    for (std::size_t i = 0; i < chain.size(); ++i) {
        const std::uint8_t* block = card.data() + std::size_t(chain[i]) * kGcCardBlockBytes;
        std::memcpy(gci.data() + kGcCardEntryBytes + i * kGcCardBlockBytes, block, kGcCardBlockBytes);
    }
    return true;
}

bool gc_card_import(std::vector<std::uint8_t>& card, const std::vector<std::uint8_t>& gci, std::string& error,
                    unsigned* slot_out) {
    Scan scan;
    if (!scan_card(card, scan, error)) return false;
    const Layout& layout = scan.layout;
    if (gci.size() < kGcCardEntryBytes) {
        error = "The save file is too short to be a GameCube save (.gci).";
        return false;
    }
    const std::uint8_t* entry = gci.data();
    if (!entry_used(entry)) {
        error = "The save file's header is blank, so it is not a GameCube save (.gci).";
        return false;
    }
    const std::uint32_t count = be16(entry + kEntryBlockCount);
    if (count == 0 || gci.size() != kGcCardEntryBytes + std::size_t(count) * kGcCardBlockBytes) {
        error = "The save file's length does not match the block count in its header.";
        return false;
    }
    const GcSave incoming = save_from_entry(entry, 0);
    for (const GcSave& save : scan.info.saves) {
        if (save.game_code == incoming.game_code && save.maker == incoming.maker &&
            save.file_name == incoming.file_name) {
            error = "The memory card already has this save; delete the one on the card first.";
            return false;
        }
    }
    const std::uint8_t* dir_in_use = dir_block(card, layout.dir);
    unsigned slot = kGcCardMaxEntries;
    for (unsigned s = 0; s < kGcCardMaxEntries; ++s) {
        if (!entry_used(dir_in_use + s * kGcCardEntryBytes)) {
            slot = s;
            break;
        }
    }
    if (slot == kGcCardMaxEntries) {
        error = "The memory card has no room left in its directory for another save.";
        return false;
    }
    if (count > scan.info.free_blocks) {
        error = "The memory card does not have enough free blocks for this save.";
        return false;
    }

    // Blocks are taken in order after the last one allocated, wrapping to
    // the first save block, as the SDK takes them.
    std::vector<std::uint8_t> bat = copy_block(bat_block(card, layout.bat));
    std::uint32_t cursor = be16(bat.data() + kBatLast);
    if (!block_in_range(layout, cursor)) cursor = kGcCardSystemBlocks - 1;
    std::vector<std::uint16_t> taken;
    for (std::uint32_t step = kGcCardSystemBlocks; step < layout.blocks && taken.size() < count; ++step) {
        ++cursor;
        if (cursor >= layout.blocks) cursor = kGcCardSystemBlocks;
        if (next_block(bat.data(), cursor) == 0) taken.push_back(static_cast<std::uint16_t>(cursor));
    }
    if (taken.size() < count) {  // the count above held, so this would be a bug
        error = "The memory card does not have enough free blocks for this save.";
        return false;
    }
    for (std::size_t i = 0; i < taken.size(); ++i) {
        const bool last = i + 1 == taken.size();
        put_be16(bat.data() + map_offset(taken[i]), last ? kChainEnd : taken[i + 1]);
        std::memcpy(card.data() + std::size_t(taken[i]) * kGcCardBlockBytes,
                    gci.data() + kGcCardEntryBytes + i * kGcCardBlockBytes, kGcCardBlockBytes);
    }
    put_be16(bat.data() + kBatLast, taken.back());

    std::vector<std::uint8_t> dir = copy_block(dir_in_use);
    std::uint8_t* new_entry = dir.data() + slot * kGcCardEntryBytes;
    std::memcpy(new_entry, entry, kGcCardEntryBytes);
    put_be16(new_entry + kEntryFirstBlock, taken.front());
    // The data and the BAT first: cut off before the directory, the card
    // has blocks nobody uses rather than an entry pointing at free blocks.
    commit_bat(card, layout, bat);
    commit_directory(card, layout, dir);
    if (slot_out) *slot_out = slot;
    return true;
}

bool gc_card_icon(const std::vector<std::uint8_t>& card, unsigned slot, std::vector<std::uint8_t>& rgba,
                  std::string& error) {
    Scan scan;
    if (!scan_card(card, scan, error)) return false;
    std::size_t index = 0;
    const GcSave* save = find_save(scan, slot, index);
    if (!save) {
        error = "There is no save in that slot of the memory card.";
        return false;
    }
    if (!save->intact) {
        error = "The save's blocks on the memory card are damaged, so its icon cannot be read.";
        return false;
    }
    const unsigned banner = save->banner_format & 3;
    if (banner == 3) {
        error = "The save's banner is in a format the memory card does not define.";
        return false;
    }
    const unsigned first_format = save->icon_format & 3;
    if (first_format == kPictureNone) {
        error = "The save has no icon.";
        return false;
    }

    // The pictures follow one another from the image offset: the banner,
    // then each frame that has one, then the frames' shared CI8 palette.
    std::uint64_t cursor = std::uint64_t(save->image_offset) + banner_bytes(banner);
    const std::uint64_t first_icon = cursor;
    for (unsigned frame = 0; frame < kIconFrames; ++frame) cursor += icon_bytes((save->icon_format >> (2 * frame)) & 3);
    const std::uint64_t shared_palette = cursor;
    if (shared_palette + kPaletteBytes > 0xFFFFFFFFull) {
        error = "The save's icon lies past the end of the save.";
        return false;
    }

    const std::vector<std::uint16_t>& chain = scan.chains[index];
    const std::uint32_t icon_at = static_cast<std::uint32_t>(first_icon);
    std::vector<std::uint8_t> pixels(icon_bytes(first_format));
    if (!save_bytes(card, chain, icon_at, static_cast<std::uint32_t>(pixels.size()), pixels.data())) {
        error = "The save's icon lies past the end of the save.";
        return false;
    }
    std::uint8_t palette[kPaletteBytes];
    if (first_format == kPictureCi8) {
        if (!save_bytes(card, chain, static_cast<std::uint32_t>(shared_palette), kPaletteBytes, palette)) {
            error = "The save's icon palette lies past the end of the save.";
            return false;
        }
    } else if (first_format == kPictureCi8Own) {
        std::memcpy(palette, pixels.data() + kIconPixels, kPaletteBytes);
    }

    // Both formats are stored in tiles: CI8 in tiles of 8x4 pixels, RGB5A3
    // in tiles of 4x4, each tile's pixels row by row, tiles left to right.
    rgba.assign(kIconPixels * 4, 0);
    for (std::uint32_t y = 0; y < kGcIconSide; ++y) {
        for (std::uint32_t x = 0; x < kGcIconSide; ++x) {
            std::uint8_t* out = rgba.data() + (y * kGcIconSide + x) * 4;
            if (first_format == kPictureRgb5a3) {
                const std::uint32_t tile = (y / 4) * (kGcIconSide / 4) + x / 4;
                const std::uint32_t inside = (y % 4) * 4 + x % 4;
                rgb5a3_to_rgba(be16(pixels.data() + (tile * 16 + inside) * 2), out);
            } else {
                const std::uint32_t tile = (y / 4) * (kGcIconSide / 8) + x / 8;
                const std::uint32_t inside = (y % 4) * 8 + x % 8;
                const std::uint8_t colour = pixels[tile * 32 + inside];
                rgb5a3_to_rgba(be16(palette + colour * 2), out);
            }
        }
    }
    return true;
}

std::vector<std::uint8_t> gc_icon_rgb5a3(const std::vector<std::uint8_t>& rgba) {
    constexpr unsigned kSide = kGcIconSide;
    std::vector<std::uint8_t> tiles;
    if (rgba.size() != static_cast<std::size_t>(kSide) * kSide * 4) return tiles;
    tiles.reserve(static_cast<std::size_t>(kSide) * kSide * 2);
    for (unsigned tile_y = 0; tile_y < kSide; tile_y += 4) {
        for (unsigned tile_x = 0; tile_x < kSide; tile_x += 4) {
            for (unsigned y = tile_y; y < tile_y + 4; ++y) {
                for (unsigned x = tile_x; x < tile_x + 4; ++x) {
                    const std::uint8_t* p = &rgba[(static_cast<std::size_t>(y) * kSide + x) * 4];
                    const unsigned r = p[0], g = p[1], b = p[2], a = p[3];
                    // Opaque pixels keep 5 bits a colour; see-through ones
                    // trade a bit each for a 3-bit alpha.
                    const unsigned v = a >= 0xE0 ? 0x8000u | (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3)
                                                 : (a >> 5) << 12 | (r >> 4) << 8 | (g >> 4) << 4 | (b >> 4);
                    tiles.push_back(static_cast<std::uint8_t>(v >> 8));
                    tiles.push_back(static_cast<std::uint8_t>(v));
                }
            }
        }
    }
    return tiles;
}

bool gc_card_shift_jis(const std::string& game_id) {
    return game_id.size() > 3 && game_id[3] == 'J';
}

std::string gc_card_path(const std::string& game_id, bool shared) {
    std::string file;
    if (shared) {
        file = gc_card_shift_jis(game_id) ? "shared-jp" : "shared";
    } else {
        // A disc's ID as it is, six capitals and digits; anything else (a
        // homebrew disc, a damaged header) by its bytes in hex: keeping
        // only the letters made "...." an empty name, ".raw", and IDs that
        // differed elsewhere one shared card (MIN-04).
        bool plain = game_id.size() == 6;
        for (char c : game_id)
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) plain = false;
        if (plain) {
            file = game_id;
        } else {
            static const char kHex[] = "0123456789abcdef";
            file = "x";
            for (char c : game_id) {
                const unsigned char u = static_cast<unsigned char>(c);
                file += kHex[u >> 4];
                file += kHex[u & 15];
            }
        }
    }
    return "sd:/riftwii/gc/cards/" + file + ".raw";
}

std::string gc_card_legacy_path(const std::string& game_id, bool shared) {
    if (shared) return std::string();
    // The name before MIN-04: the ID's capitals and digits only.
    std::string file;
    for (char c : game_id)
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) file += c;
    const std::string legacy = "sd:/riftwii/gc/cards/" + file + ".raw";
    if (file.empty() || legacy == gc_card_path(game_id, shared)) return std::string();
    return legacy;
}

}  // namespace riftwii
