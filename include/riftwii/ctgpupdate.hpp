// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// CTGP-R's own update format, as the channel writes it to
// sda:/ctgpr/update.dif (docs/PLUGINS.md, "The ctgpupd plugin"). RiftWii applies an
// update itself so the channel's installer does not have to read base files
// from the game disc, which is where it stops under RiftWii.
//
// The download is not a compressed file and not a patch against one file. It
// is a small byte-code that rebuilds each new file from literals, from short
// back-references into its own output, and from ranges of the files that are
// already installed (in the pack) or on the game disc. The file it produces
// is the *uncompressed* file; the pack stores its files with Yaz0 (below).
namespace riftwii {

// Files in the pack and on the game disc are both stored with Nintendo's
// Yaz0: a 16-byte header ("Yaz0", the uncompressed size big-endian, eight
// zero bytes), then a flag byte for each eight items, most significant bit
// first: 1 is a literal byte, 0 a match. A match's first byte holds the
// length minus 2 in its high nibble and the distance's high four bits in its
// low nibble; the second byte is the distance's low byte (distance =
// value + 1, 4 KiB window). When the high nibble is zero, a third byte
// follows and the length is that byte + 0x12 (18 to 273). A match may not
// reach back before the file's start.
constexpr std::size_t kCtgpYaz0HeaderBytes = 16;
constexpr std::size_t kCtgpYaz0WindowBytes = 4096;
// Bounds the header's size field before any buffer is sized from it. The
// biggest files in a pack are a few megabytes.
constexpr std::uint32_t kCtgpYaz0MaxBytes = 64u * 1024u * 1024u;

// Unpacks one file. False on a malformed header, a size over the bound, or a
// stream that is cut short or reaches before its start.
bool ctgp_yaz0_decode_nintendo(const std::uint8_t* data, std::size_t size,
                               std::vector<std::uint8_t>& out, std::string& error);

// The same, fed in pieces of any size, so neither the packed nor the
// unpacked file has to be in memory: `sink` gets the output in order, up to
// 32 KiB at a time. Bytes after the file's end (padding) are ignored.
class CtgpYaz0StreamDecoder {
public:
    using Sink = std::function<bool(const std::uint8_t* data, std::size_t length)>;
    explicit CtgpYaz0StreamDecoder(Sink sink);
    bool feed(const std::uint8_t* data, std::size_t length, std::string& error);
    // True when the whole file came out.
    bool finish(std::string& error);
    // The header's size (0 until the header has been fed) and the bytes so far.
    std::uint32_t unpacked_size() const { return unpacked_; }
    std::uint64_t produced() const { return produced_; }

private:
    bool run(bool last, std::string& error);
    bool emit(std::uint8_t value);
    bool flush();

    Sink sink_;
    std::vector<std::uint8_t> in_;   // input not used yet (at most one group)
    std::vector<std::uint8_t> out_;  // output not yet given to the sink
    std::array<std::uint8_t, kCtgpYaz0WindowBytes> window_{};
    bool header_ = false;
    std::uint32_t unpacked_ = 0;
    std::uint64_t produced_ = 0;
    bool failed_ = false;
};

// A rebuilt file, kept until it has been checked. In memory it is held in
// 64 KiB pieces, so no single large block is ever asked for (the Wii's heap
// breaks into holes that a few-megabyte block won't fit). With use_file it
// goes to a scratch file instead and only 64 KiB of it is ever in memory: a
// packed course file is several megabytes, more than the Wii's heap can
// spare next to the section being applied.
class CtgpPieces {
public:
    static constexpr std::size_t kPieceBytes = 64 * 1024;
    CtgpPieces() = default;
    ~CtgpPieces();
    CtgpPieces(const CtgpPieces&) = delete;
    CtgpPieces& operator=(const CtgpPieces&) = delete;
    // Keeps what is appended from now on in the file at `path` (created, or
    // emptied); the file is removed by clear() and the destructor.
    bool use_file(const std::string& path, std::string& error);
    bool append(const std::uint8_t* data, std::size_t length);
    std::size_t size() const { return size_; }
    bool read(std::size_t offset, std::uint8_t* out, std::size_t length) const;
    void clear();

private:
    std::vector<std::unique_ptr<std::uint8_t[]>> pieces_;
    std::size_t size_ = 0;
    std::FILE* file_ = nullptr;
    std::string path_;
};

// The DIA0 container: one update for one component. Header fields are at
// fixed offsets (0x00 "DIA0", 0x04 total size, 0x08/0x0C zero, 0x10 the
// eight-byte component name, 0x18 its version, 0x1C the total unpacked size,
// 0x20 the signature length, 0x24 the signature, then at 0x54 the number of
// base records and their offset, and at 0x5C the number of sections and
// their table's offset). Each section is a DIF0 whose table entry is its
// offset and size from the start of the DIA0.
constexpr std::uint32_t kCtgpDiaMaxBaseRecords = 32;
constexpr std::uint32_t kCtgpDiaMaxSections = 64;

struct CtgpDiaSection {
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
};

struct CtgpDiaBase {
    std::string component;  // eight bytes, trailing NULs dropped
    std::uint32_t version = 0;
};

struct CtgpDia {
    std::uint32_t total_size = 0;     // the whole download
    std::uint32_t unpacked_size = 0;  // sum of the sections' outputs
    std::string component;            // the package the update belongs to
    std::uint32_t version = 0;        // the version it installs
    std::vector<std::uint8_t> signature;  // DER, as stored
    // The versions the sections rebuild from: the pack must hold these, or
    // the rebuild would read the wrong bytes.
    std::vector<CtgpDiaBase> bases;
    std::vector<CtgpDiaSection> sections;
};

// Checks the header, both tables and every section's range. `size` is the
// downloaded file's length; the header's total size must match it.
bool ctgp_dia_parse(const std::uint8_t* data, std::size_t size, CtgpDia& out, std::string& error);

// One DIF0 section: the new content of one file, rebuilt from the entries'
// files. The header is at the section's start; the command stream runs from
// `header_size` to `path_table`. The rebuild keeps its own 8 KiB output
// window (larger than the packing window above): back-references and base
// copies write into it, and a later reference reads back from it.
constexpr std::size_t kCtgpUpdateWindowBytes = 8192;
constexpr std::uint32_t kCtgpDifMaxEntries = 8;

struct CtgpDifEntry {
    std::string component;  // "CTGPR", "RMC", ...
    std::uint32_t path_offset = 0;  // from the start of the section
};

struct CtgpDif {
    std::uint32_t size = 0;
    std::uint32_t header_size = 0;
    std::uint32_t path_table = 0;
    std::uint32_t unpacked_size = 0;  // the new file's length
    std::vector<CtgpDifEntry> entries;
};

bool ctgp_dif_parse(const std::uint8_t* data, std::size_t size, CtgpDif& out, std::string& error);

// The NUL-terminated string at `offset` inside a DIF0 section, bounded by
// the section. Empty when the offset is outside it or the string is not
// terminated. The first string (the one at `path_table`) is the new file's
// own path; each entry points at the file it is rebuilt from.
std::string ctgp_dif_path(const std::uint8_t* data, std::size_t size, std::uint32_t offset);

// A file a section rebuilds from, opened in the entries' order and already
// unpacked (the pack and the disc store them with Yaz0). Only the
// ranges the stream asks for are read, so a source may stream from a file.
class CtgpBaseSource {
public:
    virtual ~CtgpBaseSource() = default;
    virtual std::uint32_t size() const = 0;
    virtual bool read(std::uint32_t offset, std::uint8_t* out, std::size_t length) const = 0;
};

// Where the rebuilt bytes go. Called in order with the whole output; a sink
// may write to a file as the rebuild runs.
using CtgpUpdateWrite = bool (*)(void* user, const std::uint8_t* data, std::size_t length);

// Rebuilds one section. `bases` holds the section's entries in order (base
// file 1 first), each already unpacked. Fails on an unknown opcode, a read
// past a base file, a distance or literal that would leave the section, or
// when the output is not exactly the section's unpacked size.
bool ctgp_dif_apply(const std::uint8_t* data, std::size_t size, const CtgpDif& section,
                    const std::vector<const CtgpBaseSource*>& bases,
                    CtgpUpdateWrite write, void* user, std::string& error);

// Bounds a section before its unpacked size is used: the channel's own limit
// is 64 KiB more than the output, and no pack file is over a few megabytes.
constexpr std::uint32_t kCtgpUpdateMaxFileBytes = 64u * 1024u * 1024u;

// What the updater needs from the console: the files the sections rebuild
// from, and somewhere to put the results. The pack side opens each entry's
// file ("CTGPR" in the pack, "RMC" on the game disc), unpacks it, and stores
// the packed result at the section's own path.
// How far a long step has got, `done` of `total` (any unit); for a bar.
using CtgpProgress = std::function<void(std::uint64_t done, std::uint64_t total)>;

class CtgpUpdateTarget {
public:
    virtual ~CtgpUpdateTarget() = default;
    // `component` is the entry's eight-byte name; `path` its file path. The
    // returned source owns the data, so a target may keep it in memory or in
    // a temporary file; it must stay readable until the section is stored.
    // `progress` (may be empty) is told how far the unpacking has got.
    virtual bool load_base(const std::string& component, const std::string& path,
                           std::unique_ptr<CtgpBaseSource>& out, const CtgpProgress& progress,
                           std::string& error) = 0;
    // Called with the SHA-1 of each packed file before it is stored. A file
    // that fails is not stored and the update stops.
    virtual bool check_file(const std::string& path, const std::array<std::uint8_t, 20>& digest,
                            std::string& error) = 0;
    // `progress` (may be empty) is told how much of the file is written.
    virtual bool store_file(const std::string& path, const CtgpPieces& packed, const CtgpProgress& progress,
                            std::string& error) = 0;
    // Where each packed file is kept until it is stored: a scratch file's
    // path, or empty to keep it in memory.
    virtual std::string packed_scratch_path() const { return std::string(); }
};

// Applies every section of a downloaded DIA0, in order. `log` (may be empty)
// gets one line per file. Stops at the first failure; files already stored
// keep their new contents, which a retry writes again harmlessly.
bool ctgp_update_apply(const std::uint8_t* dia, std::size_t size, CtgpUpdateTarget& target,
                       const std::function<void(const std::string&)>& log, std::string& error);

// The same, telling the caller after each file is stored, for a progress bar:
// `index` is 0-based, `total` the section count.
using CtgpUpdateFileDone =
    std::function<void(std::size_t index, std::size_t total, const std::string& path)>;
bool ctgp_update_apply(const std::uint8_t* dia, std::size_t size, CtgpUpdateTarget& target,
                       const std::function<void(const std::string&)>& log,
                       const CtgpUpdateFileDone& file_done, std::string& error);

// Progress inside file `index` (0-based) of `total`: `part` runs from 0 to 1
// while its base files are unpacked, it is rebuilt and packed, and stored.
// Called often enough for a bar to move (a few times a second's work).
using CtgpUpdateStep =
    std::function<void(std::size_t index, std::size_t total, const std::string& path, double part)>;

// The same, reading the download on demand: `read` fills `length` bytes at
// `offset` and `size` is the file's length. Only the header and one section
// are in memory at a time, which is what the Wii needs. `step` may be empty.
using CtgpUpdateRead =
    std::function<bool(std::uint64_t offset, std::uint8_t* out, std::size_t length)>;
bool ctgp_update_apply(const CtgpUpdateRead& read, std::uint64_t size, CtgpUpdateTarget& target,
                       const std::function<void(const std::string&)>& log,
                       const CtgpUpdateFileDone& file_done, std::string& error);
bool ctgp_update_apply(const CtgpUpdateRead& read, std::uint64_t size, CtgpUpdateTarget& target,
                       const std::function<void(const std::string&)>& log,
                       const CtgpUpdateFileDone& file_done, const CtgpUpdateStep& step,
                       std::string& error);

// The header and tables of a DIA0, for a caller that has read the start of
// the file (16 KiB is plenty). `total` is the whole download's length.
constexpr std::size_t kCtgpDiaPrefixBytes = 16 * 1024;
bool ctgp_dia_parse_prefix(const std::uint8_t* data, std::size_t size, std::uint64_t total,
                           CtgpDia& out, std::string& error);

}  // namespace riftwii
