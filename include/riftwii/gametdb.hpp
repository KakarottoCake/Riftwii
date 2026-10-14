// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

// What GameTDB knows about a game (its wiitdb.xml: genre, release, players,
// controllers, online features, age rating, synopsis), for a game's About
// page. The file is 30 MB of XML for 10,000 games; it is read as it is
// unpacked, a <game> block at a time, and only the games on the player's
// drives are kept, in a small cache file (serialize_game_infos).
//
// Network data: every block, field and list has a size limit, and a game
// ID must look like one (4 or 6 capital letters or digits).
namespace riftwii {

struct GameControl {
    std::string type;       // GameTDB's name: wiimote, nunchuk, classiccontroller, gamecube, wheel...
    bool required = false;  // needed to play, not just supported
};

struct GameInfo {
    std::string id;
    std::string developer;
    std::string publisher;
    std::vector<std::string> genres;   // GameTDB's English words ("racing", "kart racing")
    int year = 0, month = 0, day = 0;  // release; 0 when unknown
    std::string rating_system;         // ESRB, PEGI, CERO, GRB, USK...
    std::string rating;                // E, T, 12, A...
    int players = 0;                   // local, 0 when unknown
    std::vector<GameControl> controls;
    int online_players = 0;            // 0: no online play
    std::vector<std::string> online_features;  // online, download, score, messageboard, wiimmfi...
    std::string synopsis;              // in the wanted language, else English; may be empty
};

// Feeds wiitdb.xml's text in pieces of any size; keeps the games in
// `wanted`. `lang` is GameTDB's locale code for the synopsis (EN, JA, FR,
// DE, ES, IT, NL, PT, KO...), English when it has none.
class WiitdbScanner {
public:
    static constexpr std::size_t kMaxBlock = 64u << 10;  // a <game> block; longer ones are skipped
    WiitdbScanner(std::set<std::string> wanted, std::string lang);
    void feed(const char* data, std::size_t size);
    const std::map<std::string, GameInfo>& games() const { return found_; }
    std::size_t blocks() const { return blocks_; }    // <game> blocks seen
    std::size_t skipped() const { return skipped_; }  // too long or unreadable

private:
    void take_block(const std::string& block);
    std::set<std::string> wanted_;
    std::string lang_;
    std::string buffer_;
    bool in_game_ = false;
    std::map<std::string, GameInfo> found_;
    std::size_t blocks_ = 0;
    std::size_t skipped_ = 0;
};

// Reads one <game> block (pugixml); false when it is not one or its ID is
// not a game ID.
bool parse_wiitdb_game(const std::string& block, const std::string& lang, GameInfo& out);

// The cache (sd:/riftwii/gameinfo-<lang>.txt): one "[ID]" line a game, then
// key=value lines. Reading it back is as careful as reading the network's.
std::string serialize_game_infos(const std::map<std::string, GameInfo>& games);
bool parse_game_infos(const std::string& text, std::map<std::string, GameInfo>& out);

// GameTDB's locale code for a menu language ("en", "ja"...).
std::string gametdb_locale(const std::string& menu_language);

// Typographic punctuation GameTDB's texts use (curly quotes, dashes,
// bullets, the ellipsis, no-break spaces) as the plain characters every
// menu font has.
std::string plain_punctuation(const std::string& text);

// `text` as lines of at most `columns`, a CJK or Hangul character counting
// as two: broken at spaces where there are any, else between characters
// (never inside one). A line break in the text starts a new line, and a
// run of blank lines is kept as one.
std::vector<std::string> wrap_columns(const std::string& text, std::size_t columns);

}  // namespace riftwii
