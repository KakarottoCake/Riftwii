// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Box art for the menu, from GameTDB (https://www.gametdb.com): the
// front cover, shrunk once when it is downloaded into a ready GX texture
// on the SD card (wii/menu/covers.cpp), so drawing a page of covers is a few
// small reads and no image decoding.
namespace riftwii {

// The stored size: half of GameTDB's 160x224 covers.
constexpr int kCoverWidth = 80;
constexpr int kCoverHeight = 112;
// "RWC1", the width and the height (big-endian 16-bit), then the pixels:
// GX RGB5A3 in 4x4 tiles, the corners rounded off with transparency.
constexpr std::size_t kCoverHeaderSize = 8;
constexpr std::size_t kCoverPixelBytes = static_cast<std::size_t>(kCoverWidth) * kCoverHeight * 2;
constexpr std::size_t kCoverFileSize = kCoverHeaderSize + kCoverPixelBytes;

// GameTDB's cover regions to try for a game, best first: its own region
// (for European games, the menu language's when GameTDB has one), then
// EN, US and JA.
std::vector<std::string> cover_regions(const std::string& game_id, const std::string& menu_language);
std::string cover_url(const std::string& region, const std::string& game_id);

// The status a server answered with, read from a failed download's error
// as wii/online/online.cpp's HttpGet words it ("<host> answered 404", or
// "<host> answered 301 (a redirect, ...)"); 0 when the error is not an
// answer at all (no connection, TLS, a timeout).
int http_answered_status(const std::string& error);

// Whether GameTDB answering `status` for a cover's address means it keeps
// no cover there, so the next region is tried: a 404 or any other refusal
// (4xx), or a redirect the download does not follow. Not 408 or 429 (the
// server asks to come back later) nor 5xx (its own trouble): those are no
// answer about the cover.
bool cover_answer_means_missing(int status);

// How one picture's download ended (wii/menu/covers.cpp's FetchCover, and
// the same for the shelf's boxes and the 3D covers).
enum class CoverOutcome {
    Stored,       // downloaded and kept on the SD card
    NotFound,     // GameTDB has none, in any region tried
    NoAnswer,     // no answer about it: no connection, TLS, a timeout, a server in trouble
    CannotStore,  // downloaded, but the SD card could not take it (full?)
};

// The downloads of one kind of GameTDB picture (covers, the shelf's boxes,
// 3D covers) in one menu session, one after the other. A game GameTDB has
// no picture for, or one whose download got no answer, is passed over and
// the rest go on: some titles have no GameTDB entry, and one slow answer is
// not a dead network (issue #130). Downloads stop only once
// kNoAnswerInARow in a row got no answer, or when the card cannot store one.
class CoverDownloads {
public:
    static constexpr unsigned kNoAnswerInARow = 3;

    // `log_name` starts its log lines ("Covers"), `picture` names one of
    // them in the summary ("cover").
    explicit CoverDownloads(std::string log_name = "Covers", std::string picture = "cover")
        : log_name_(std::move(log_name)), picture_(std::move(picture)) {}

    // Counts one download; true when the downloads should stop now.
    bool count(CoverOutcome outcome);

    // A plain sentence on the downloads counted since the last one, for the
    // log once the queue is empty; empty when none were counted.
    std::string take_summary();

    // A fresh start (Settings > Look for games again).
    void reset() { *this = CoverDownloads(log_name_, picture_); }

    const std::string& log_name() const { return log_name_; }

    unsigned stored() const { return stored_; }
    unsigned not_found() const { return not_found_; }
    unsigned no_answer() const { return no_answer_; }

private:
    std::string log_name_;
    std::string picture_;
    unsigned stored_ = 0;
    unsigned not_found_ = 0;
    unsigned no_answer_ = 0;
    unsigned no_answer_in_a_row_ = 0;
    bool counted_since_summary_ = false;
};

// An RGBA image (rows, 4 bytes a pixel) resized to dw x dh, each target
// pixel the average of the source area it covers.
std::vector<std::uint8_t> scale_rgba(const std::uint8_t* src, int sw, int sh, int dw, int dh);

// The stored cover for an RGBA image of any size; empty when the size is
// unusable.
std::vector<std::uint8_t> make_cover_file(const std::uint8_t* rgba, int w, int h);
// True when `header` (kCoverHeaderSize bytes) starts a stored cover.
bool cover_header_valid(const std::uint8_t* header);

// The box for the Home shelf, from GameTDB's full cover (back, spine and
// front in one picture, e.g. 1024x680): the spine and the front, side by
// side in one stored texture, so a game on the shelf is one read.
constexpr int kBoxSpineWidth = 16;
constexpr int kBoxFrontWidth = 128;
constexpr int kBoxBackWidth = 128;
constexpr int kBoxWidth = kBoxSpineWidth + kBoxFrontWidth + kBoxBackWidth;
constexpr int kBoxHeight = 176;
// "RWB2", the width and the height (big-endian 16-bit), then the pixels:
// GX RGB5A3 in 4x4 tiles: the spine in the first kBoxSpineWidth columns,
// then the front, then the back.
constexpr std::size_t kBoxHeaderSize = 8;
constexpr std::size_t kBoxPixelBytes = static_cast<std::size_t>(kBoxWidth) * kBoxHeight * 2;
constexpr std::size_t kBoxFileSize = kBoxHeaderSize + kBoxPixelBytes;

std::string coverfull_url(const std::string& region, const std::string& game_id);

// GameTDB's 3D cover: a picture of the game's case standing turned a
// little, on a see-through ground (176x248 as GameTDB serves it), for the
// Home view of those pictures. Stored as "RW3D", the width and the height
// (big-endian 16-bit), then GX RGB5A3 in 4x4 tiles, its see-through parts
// kept (RGB5A3's 3-bit alpha).
constexpr int kCover3dWidth = 176;
constexpr int kCover3dHeight = 248;
constexpr std::size_t kCover3dHeaderSize = 8;
constexpr std::size_t kCover3dPixelBytes = static_cast<std::size_t>(kCover3dWidth) * kCover3dHeight * 2;
std::string cover3d_url(const std::string& region, const std::string& game_id);
// The stored picture for an RGBA image of any size: scaled to fit
// kCover3dWidth x kCover3dHeight keeping its shape, standing on the
// bottom edge, centred across, the rest see-through. Empty when the size
// is unusable.
std::vector<std::uint8_t> make_cover3d_file(const std::uint8_t* rgba, int w, int h);
bool cover3d_header_valid(const std::uint8_t* header);

// Where the spine is across a full cover `w` x `h`: [x0, x1). A Wii case's
// front and back are 135 mm wide and 190 mm tall, the spine between them
// about 14 mm; a picture whose middle strip is far off that gets a 14 mm
// strip in its centre. False when the picture can't be a full cover.
bool spine_strip(int w, int h, int& x0, int& x1);

// The stored box for a full cover; empty when it can't be one.
std::vector<std::uint8_t> make_box_file(const std::uint8_t* rgba, int w, int h);
bool box_header_valid(const std::uint8_t* header);

// The shelf's backdrop: a small, soft copy of columns [x0, x1) of a w x h
// RGB5A3 texture (4x4 tiles, as stored; with `rgba8`, GX RGBA8 tiles, as
// the menu's painted pictures are), each pixel the average of its area
// and then blurred, opaque, as kBackdropWidth x kBackdropHeight RGB5A3
// tiles into `out` (kBackdropBytes). Drawn stretched across the screen,
// the filtering does the rest of the blur.
constexpr int kBackdropWidth = 32;
constexpr int kBackdropHeight = 44;
constexpr std::size_t kBackdropBytes = static_cast<std::size_t>(kBackdropWidth) * kBackdropHeight * 2;
void make_backdrop(const std::uint8_t* tiles, int w, int h, int x0, int x1, std::uint8_t* out, bool rgba8 = false);

// A game page's box: columns [x0, x1) of a w x h RGB5A3 texture (4x4
// tiles) copied as (x1 - x0) x h tiles into `out`, its four corners
// rounded off with this radius in texels: outside the curve see-through,
// on it partly (RGB5A3's 3-bit alpha), so the edge is smooth. x0, x1 and
// h are multiples of 4; `out` holds (x1 - x0) * h * 2 bytes. False (and
// `out` untouched) when they don't fit.
bool round_corners(const std::uint8_t* tiles, int w, int h, int x0, int x1, float radius, std::uint8_t* out);

}  // namespace riftwii
