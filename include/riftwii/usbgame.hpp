// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "riftwii/fat32.hpp"
#include "riftwii/overlay.hpp"

namespace riftwii {

// d2x's USB fragment ABI is native endian: three u32 header words followed
// by {logical Wii-sector, physical-sector, sector-count} triples.  The
// physical device and the initial backend both use 512-byte sectors.
constexpr std::uint32_t kD2xFragmentLimit = 20000;
constexpr std::uint32_t kUsbSectorBytes = 512;

struct D2xFragment {
    std::uint32_t offset = 0;
    std::uint32_t sector = 0;
    std::uint32_t count = 0;
};

struct D2xFragmentList {
    std::uint32_t size = 0;       // logical disc sectors, including holes
    std::uint32_t num = 0;
    std::uint32_t maxnum = kD2xFragmentLimit;
    std::vector<D2xFragment> entries;

    // Native-endian bytes suitable for the cIOS F9 command. The caller
    // supplies 32-byte alignment before passing this to IOS.
    bool encode(std::vector<std::uint8_t>& out, std::string& error) const;
};

// A file piece contributes consecutive container bytes.  A split .wbfs is
// represented by its .wbfs then .wbf1, .wbf2 ... pieces.  The source is used
// only while libfat is mounted for catalog validation and host tests; d2x
// consumes the corresponding FAT extents after the IOS reload.
struct UsbImagePiece {
    std::shared_ptr<const ByteSource> source;
    Fat32File file;
    std::string path;
};

enum class UsbImageFormat { Iso, Wbfs };

struct UsbImage {
    UsbImageFormat format = UsbImageFormat::Iso;
    std::vector<UsbImagePiece> pieces;
};

// A ByteSource over a concatenation of files.  It deliberately knows no FAT
// details, so it is also a compact oracle for split-image tests.
class UsbContainerSource final : public ByteSource {
public:
    explicit UsbContainerSource(std::vector<UsbImagePiece> pieces);
    std::uint64_t size() const override;
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override;

private:
    std::vector<UsbImagePiece> pieces_;
    std::vector<std::uint64_t> starts_;
    std::uint64_t size_ = 0;
};

// Parses the file-backed WBFS form and builds mappings from Wii logical
// sectors to physical USB sectors. Raw ISOs are a direct concatenation. The
// image's sources are also used to validate the Wii disc header. No content
// is decrypted here.
bool build_usb_fragments(const UsbImage& image, D2xFragmentList& out, std::string& error);

// Collects a split WBFS set from an in-memory directory listing: the primary
// .wbfs plus consecutive .wbf1, .wbf2, ... pieces. A split set must be
// consecutive, so a later piece may not silently hide a missing earlier one.
// Matching is ASCII case-insensitive (FAT semantics); out_paths holds full
// dir-joined paths with the primary first. Pure logic over the listing the
// scan already holds, so a non-split game costs no extra FAT lookups.
bool collect_split_pieces(const std::string& dir, const std::string& primary_leaf,
                          const std::vector<std::string>& siblings, UsbImageFormat format,
                          std::vector<std::string>& out_paths, std::string& error);

// Presents the unencrypted Wii disc bytes through the same WBFS mapping used
// for fragments. Sparse WBFS blocks fail reads rather than becoming zeroes.
class UsbDiscSource final : public ByteSource {
public:
    static bool open(const UsbImage& image, std::unique_ptr<UsbDiscSource>& out, std::string& error);
    std::uint64_t size() const override;
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override;

private:
    UsbDiscSource(UsbContainerSource container, std::vector<D2xFragment> map, std::uint64_t bytes)
        : container_(std::move(container)), map_(std::move(map)), bytes_(bytes) {}
    UsbContainerSource container_;
    // Here sector is a logical sector in the concatenated container (not a
    // physical device sector); the same logical offsets are used to read the
    // catalog oracle before d2x takes ownership of USB.
    std::vector<D2xFragment> map_;
    std::uint64_t bytes_ = 0;
};

// One candidate cIOS slot and whether the console holds a ticket for it
// (queried without reloading IOS, so this is presence only, not identity).
struct CiosSlotState {
    int slot = 0;
    bool installed = false;
};

// User-visible warning when no candidate slot holds anything: USB games
// cannot boot without a d2x cIOS. Empty when at least one slot is
// installed. Deliberately presence-only: the guided d2x installer stamps
// revision 65535 whatever the version, so a revision cannot tell v8 from
// v11, and the launch-time F9/FA probe remains the real capability gate.
std::string cios_readiness_note(const CiosSlotState* slots, std::size_t count);

// Nintendo's stub marker, seen in public SysCheck reports as rev 65280: a
// slot holding this boots but implements nothing, so it can never be d2x.
inline bool cios_revision_is_stub(std::uint32_t revision) { return revision == 65280; }

}  // namespace riftwii
