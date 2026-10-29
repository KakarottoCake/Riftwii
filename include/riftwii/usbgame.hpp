// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "riftwii/fat32.hpp"
#include "riftwii/imagevolume.hpp"
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
// only for catalog validation and host tests; d2x
// consumes the corresponding FAT32/NTFS extents after the IOS reload.
struct UsbImagePiece {
    std::shared_ptr<const ByteSource> source;
    VolumeFile file;
    std::string path;
};

// Rvz: Dolphin's compressed format (riftwii/rvz.hpp).
enum class UsbImageFormat { Iso, Wbfs, Rvz };

// The Wii scan's folder rules: games (Iso) takes ISO and RVZ; wbfs
// (Wbfs) and custom folders (Rvz) also take WBFS. Case does not matter.
bool catalog_image_format(const std::string& name, UsbImageFormat folder, UsbImageFormat& out);

// GameCube images use the same names loose or in a game's own folder.
bool is_gc_image_name(const std::string& name);

// The name Nintendont gives a GameCube game's image in a folder of its own
// ("game.iso", "disc2.iso", or .gcm/.rvz), in any case. A folder holding one
// is a GameCube game's folder, wherever the folders around it are.
bool is_gc_game_folder_image_name(const std::string& name);

// Whether the GameCube scan looks into a folder at the top of a drive for
// game folders laid out as Nintendont's (<folder>/<game>/game.iso), when
// players keep them somewhere other than games (a "game1" left by a copy):
// not a hidden name, not games, wbfs or a folder that holds homebrew,
// loader settings or the Wii's own data.
bool gc_scan_probes_top_folder(const std::string& name);

// Preserve Wii lazy listing when the GameCube scan shares its folders:
// skip a known Wii image, or a Wii-style path carrying an R/S ID6.
bool gc_scan_should_open(const std::string& path, bool known_wii);

// Join one untrusted directory entry without allowing another path or
// exceeding the catalog's 240-byte path limit. Hidden entries are skipped.
bool join_game_path(const std::string& directory, const std::string& name, std::string& out);

struct UsbImage {
    UsbImageFormat format = UsbImageFormat::Iso;
    std::vector<UsbImagePiece> pieces;
    // Which disc of a WBFS: a .wbfs file holds one, in slot 0; a WBFS
    // partition (riftwii/wbfspart.hpp) is one piece holding many.
    std::uint32_t wbfs_slot = 0;
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

// A disc made of parts of one file (an RVZ game's stub, riftwii/rvz.hpp):
// each range puts `length` bytes of the file at `disc_offset`, and d2x
// reads the rest of the disc as zeros. The list is sized as a single-layer
// disc unless `disc_bytes` needs two layers, as for WBFS images. Offsets
// and lengths must be multiples of 512.
struct DiscRange {
    std::uint64_t disc_offset = 0;
    std::uint64_t file_offset = 0;
    std::uint64_t length = 0;
};
bool build_sparse_fragments(const UsbImage& file, const std::vector<DiscRange>& ranges, std::uint64_t disc_bytes,
                            D2xFragmentList& out, std::string& error);

// A Wii disc burned as-is to a DVD and read in a USB DVD drive: the
// drive's sectors are the disc's from sector 0, so the list is a single
// fragment. d2x counts the list in the drive's own sectors (2048 bytes for
// a DVD). `disc_end` is where the disc's data ends (disc_data_end); the
// list is sized as a single-layer disc unless that lies past one layer,
// and d2x reads the rest of the size as zeros.
bool build_raw_disc_fragments(std::uint64_t disc_end, std::uint32_t sector_bytes, D2xFragmentList& out,
                              std::string& error);
// Where the data of the disc's last partition ends: all a game can read.
bool disc_data_end(const ByteSource& disc, std::uint64_t& end, std::string& error);

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

// Presence only; the menu supplies translated advice for the chosen drive.
// The guided d2x installer stamps
// revision 65535 whatever the version, so a revision cannot tell v8 from
// v11, and the launch-time F9/FA probe remains the real capability gate.
enum class CiosReadiness { Unknown, Missing, Present };
CiosReadiness cios_readiness(const CiosSlotState* slots, std::size_t count);

// CompactISO (.ciso) GameCube images, which RiftWii does not play and
// will not: the GameCube loader reads .iso, .gcm and .rvz. The SD scan
// notes the ones it passes so Home can say so (and suggest converting
// them with Dolphin) instead of leaving the game silently missing.
// True for a name ending in .ciso in any case; hidden names ("._x.ciso"
// from a Mac) are not images.
bool is_ciso_image_name(const std::string& name);

// The .ciso files one scan found: the first few paths, in scan order, and
// how many in all (a card full of them must not grow the list unbounded).
struct CisoFinds {
    static constexpr std::size_t kPathsKept = 4;
    std::vector<std::string> paths;
    std::size_t total = 0;
    void note(const std::string& path);
    // Adds another scan's finds after these, so the SD card's and the USB
    // drive's are said together.
    void merge(const CisoFinds& other);
};

// Why a file in a GameCube game's own folder (sd:/games/<folder>/ or
// usb:/games/<folder>/) is not listed, for the scan's log: what it is and
// what to do. Empty when it is not worth a line: an image RiftWii plays, a
// hidden name ("._x.iso" from a Mac), a .ciso (Home says so on its own) or
// a file that only sits beside an image (a cover, a text file).
std::string gc_folder_skip_reason(const std::string& name);

// A GameCube image in one game folder, by its disc header.
struct GcFolderImage {
    std::string id;                // the header's game ID
    std::uint8_t disc_number = 0;  // 0 the first disc, 1 the second
};

struct GcCatalogImage {
    std::string path;
    std::string id;
    std::uint8_t disc_number = 0;
};

// For each image, the index that replaces it in the list, or -1 to list
// it. Disc 2 belongs to disc 1 in the same directory. A loose copy in
// games/ or wbfs/ yields to a copy in a game's folder on the same device:
// one inside games/ or wbfs/, or inside one of `folder_roots` (the scan's
// game_folders and the top folders it found game folders in, such as
// "usb:/game1"). A loose pair stays listed instead if the folder copy lacks
// disc 2. Copies in separate game folders remain separate (they may be mods).
std::vector<int> gc_catalog_hidden_by(const std::vector<GcCatalogImage>& images,
                                      const std::vector<std::string>& folder_roots = {});

// Which images of one game folder are a two-disc game's second disc kept
// with its first: for each image, the index of the first disc (same ID,
// disc number 0) it belongs to, or -1 when it is listed as a game itself.
// A second disc alone is listed, as USB Loader GX does: some games start
// from either disc. Nintendont looks for disc2.iso beside game.iso the same
// way when the game asks for the other disc.
std::vector<int> gc_second_disc_of(const std::vector<GcFolderImage>& images);

// The disc a two-disc game started from images[started] changes to
// (runtime/gc/gc_disc.h): the first image of the folder with the same
// game ID and disc number 1, by the same rule as gc_second_disc_of; -1
// when there is none, or when the game was started from a disc other than
// its first. A second disc of another game ID is never changed to: its
// index goes into `other_id` (when given) for the log.
int gc_disc_change_partner(const std::vector<GcFolderImage>& images, std::size_t started,
                           std::vector<std::size_t>* other_id = nullptr);

// A GameCube image container told by the first bytes of the file, whatever
// its name: players rename a .ciso or .gcz to .iso and Home then showed the
// game missing with no reason. NKit is not told apart here: its images keep
// the ISO's own header, so they pass as ISO (a tester's .nkit.gcz is said by
// its name, gc_folder_skip_reason).
enum class GcContainer { None, Ciso, Gcz, Wia, Rvz };
GcContainer gc_container_by_magic(const std::uint8_t* head, std::size_t size);

// For the scan's log, a file named .iso/.gcm that is really `container`:
// what it is and what to do. Empty for None.
std::string gc_container_skip_reason(GcContainer container);

// Home's popup text: `help` (already translated), a blank line, then the
// first file found and, when there are more, `and_more` (translated, with
// {1} for how many more) after it. Empty when nothing was found.
std::string ciso_popup_body(const CisoFinds& finds, const std::string& help, const std::string& and_more);

// The log line for the same, in English: the first file, how many more,
// and what RiftWii plays instead.
std::string ciso_log_line(const CisoFinds& finds);

// What d2x's own USB device (after the reload into the cIOS) reports, said
// for boot.log and the launch's failure screen. A sector size of 0 is d2x
// finding no drive at all in the port it looked at, not a drive with odd
// sectors. `needs` names what needs 512-byte sectors ("GameCube games");
// it is added after a real size only.
std::string d2x_sector_size_problem(std::uint32_t sector_bytes, const char* needs);

// d2x returns the unsigned sector count as a signed IOS result. Small
// negative values are errors, except -1 with 512- or 2048-byte sectors: a drive
// too large for its 32-bit count. The caller must also check init and read
// sector zero before accepting that unknown capacity.
bool d2x_usb_capacity_available(std::int32_t sectors, std::uint32_t sector_bytes);

// Whether the first sector d2x read for a Wii game is the game the menu
// listed: a disc starts with its six-character game ID. True when it
// cannot be judged (the first fragment is not the disc's start, or the
// sector is not 512 bytes, as on a USB DVD drive), so only positive
// evidence of another drive stops a launch.
bool d2x_first_sector_is_game(const std::uint8_t* sector, std::uint32_t sector_bytes,
                              std::uint32_t fragment_offset, const std::string& game_id);

// The launch errors for a Wii game on a USB drive that d2x cannot serve.
// `reason` is the last thing d2x's device said (empty when unknown).
std::string d2x_usb_unreadable_error(const std::string& reason);
std::string d2x_usb_other_drive_error();

// A call into d2x's USB device that never came back (a tester's Wii U
// froze in the first one after the reload): the reason given, and the log
// line for `call` ("open" or "read") after `seconds`.
inline constexpr const char* kD2xStuckReason = "d2x stopped answering";
std::string d2x_usb_stuck_log_line(const char* call, int seconds);

// After every cIOS slot was tried: each USB drive error said once, with
// the slots it happened in ("... (tried with IOS249, IOS251); <another
// reason> (tried with IOS250)"), in the order first seen, then the first
// other slot's error when one failed for another reason.
struct D2xSlotFailure {
    int slot = 0;
    std::string reason;
};
std::string d2x_usb_slots_summary(const std::vector<D2xSlotFailure>& usb_failures, const std::string& other_error);

// Nintendo's stub marker, seen in public SysCheck reports as rev 65280: a
// slot holding this boots but implements nothing, so it can never be d2x.
inline bool cios_revision_is_stub(std::uint32_t revision) { return revision == 65280; }

}  // namespace riftwii
