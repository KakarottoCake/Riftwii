// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/titles.hpp"
#include "riftwii/textlines.hpp"

namespace riftwii {
namespace {

constexpr std::size_t kMaxTitle = 200;

std::string trim(const std::string& s) { return trim_text(s, " \t\r\n"); }

// A cut at `limit` bytes moved back to the start of a UTF-8 character,
// so a long name never ends inside one (as riftwii/profiles.hpp does).
std::size_t utf8_cut(const std::string& text, std::size_t limit) {
    if (text.size() <= limit) return text.size();
    std::size_t at = limit;
    while (at > 0 && (static_cast<unsigned char>(text[at]) & 0xC0) == 0x80) --at;
    return at;
}
bool plausible_id(const std::string& id) {
    if (id.size() != 4 && id.size() != 6) return false;
    for (char c : id) {
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    }
    return true;
}

}  // namespace

void TitleTable::add_text(const std::string& text, const std::set<std::string>* wanted) {
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(at, end - at);
        at = end + 1;
        const std::size_t eq = line.find(" = ");
        if (eq == std::string::npos) continue;
        const std::string id = trim(line.substr(0, eq));
        std::string title = trim(line.substr(eq + 3));
        if (!plausible_id(id) || id == "TITLES" || title.empty()) continue;  // "TITLES = <source>" header
        if (wanted && wanted->count(id) == 0) continue;
        if (title.size() > kMaxTitle) title.resize(utf8_cut(title, kMaxTitle));
        titles_[id] = title;
    }
}

const std::string* TitleTable::find(const std::string& id) const {
    auto it = titles_.find(id);
    if (it == titles_.end() && id.size() == 6) it = titles_.find(id.substr(0, 4));
    return it == titles_.end() ? nullptr : &it->second;
}

const std::string* TitleTable::find_exact(const std::string& id) const {
    const auto it = titles_.find(id);
    return it == titles_.end() ? nullptr : &it->second;
}

const std::string* TitleTable::find_code(const std::string& code) const {
    if (code.size() != 4) return nullptr;
    // The map is sorted: the code's own line, if any, comes first, then
    // the IDs that start with it.
    const auto it = titles_.lower_bound(code);
    if (it == titles_.end() || it->first.compare(0, 4, code) != 0) return nullptr;
    return &it->second;
}

std::string id_from_image_path(const std::string& path) {
    const std::size_t file_slash = path.find_last_of('/');
    const std::string file = file_slash == std::string::npos ? path : path.substr(file_slash + 1);
    const std::size_t dot = file.find_last_of('.');
    const std::string stem = dot == std::string::npos ? file : file.substr(0, dot);
    if (stem.size() == 6 && plausible_id(stem)) return stem;
    const auto bracketed = [](const std::string& name) -> std::string {
        const std::string t = trim(name);
        if (t.size() < 8 || t.back() != ']' || t[t.size() - 8] != '[') return "";
        const std::string id = t.substr(t.size() - 7, 6);
        return plausible_id(id) ? id : "";
    };
    if (file_slash != std::string::npos && file_slash > 0) {
        const std::size_t dir_slash = path.find_last_of('/', file_slash - 1);
        const std::size_t from = dir_slash == std::string::npos ? 0 : dir_slash + 1;
        const std::string id = bracketed(path.substr(from, file_slash - from));
        if (!id.empty()) return id;
    }
    return bracketed(stem);
}

std::size_t sort_name_start(const std::string& title) {
    if (title.size() <= 4) return 0;
    const auto low = [](char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); };
    if (low(title[0]) != 't' || low(title[1]) != 'h' || low(title[2]) != 'e' || title[3] != ' ') return 0;
    std::size_t at = 4;
    while (at < title.size() && title[at] == ' ') ++at;
    return at < title.size() ? at : 0;
}

namespace {

// The plain letters of U+00C0 to U+00FF, one per code point. '*' keeps the
// character as it is (the multiplication and division signs); the others
// that stand for more than one letter: '1' "ae", '2' "th", '3' "ss".
constexpr char kLatin1Letters[] =
    "aaaaaa1ceeeeiiiidnooooo*ouuuuy23"
    "aaaaaa1ceeeeiiiidnooooo*ouuuuy2y";
// The same for U+0100 to U+017F (Latin Extended-A): '4' "ij", '5' "oe".
constexpr char kLatinExtendedLetters[] =
    "aaaaaacccccccc" "dddd" "eeeeeeeeee" "gggggggg" "hhhh" "iiiiiiiiii" "44" "jj" "kkk" "llllllllll"
    "nnnnnnnnn" "oooooo" "55" "rrrrrr" "ssssssss" "tttttt" "uuuuuuuuuuuu" "ww" "yyy" "zzzzzz" "s";
static_assert(sizeof kLatin1Letters == 65, "one letter for each of U+00C0 to U+00FF");
static_assert(sizeof kLatinExtendedLetters == 129, "one letter for each of U+0100 to U+017F");

// What one table letter stands for.
const char* plain_letters(char letter) {
    switch (letter) {
    case '1': return "ae";
    case '2': return "th";
    case '3': return "ss";
    case '4': return "ij";
    case '5': return "oe";
    default: return nullptr;
    }
}

}  // namespace

std::string sort_key(const std::string& name) {
    std::string key;
    key.reserve(name.size());
    for (std::size_t i = 0; i < name.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (c < 0x80) {
            key += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            continue;
        }
        // A two-byte UTF-8 character from U+00C0 to U+017F: lead byte C3,
        // C4 or C5 and a continuation byte.
        const bool two_bytes = (c == 0xC3 || c == 0xC4 || c == 0xC5) && i + 1 < name.size() &&
                               (static_cast<unsigned char>(name[i + 1]) & 0xC0) == 0x80;
        const unsigned code_point =
            two_bytes ? ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(name[i + 1]) & 0x3Fu) : 0;
        char letter = '*';
        if (code_point >= 0xC0 && code_point <= 0xFF) letter = kLatin1Letters[code_point - 0xC0];
        else if (code_point >= 0x100 && code_point <= 0x17F) letter = kLatinExtendedLetters[code_point - 0x100];
        if (letter == '*') {
            key += static_cast<char>(c);
            continue;
        }
        if (const char* letters = plain_letters(letter)) key += letters;
        else key += letter;
        ++i;  // the continuation byte
    }
    return key;
}

bool sort_name_less(const std::string& a, const std::string& b) {
    const std::string key_a = sort_key(a);
    const std::string key_b = sort_key(b);
    if (key_a != key_b) return key_a < key_b;
    return a < b;
}

char sort_letter(const std::string& title) {
    const std::string key = sort_key(title.substr(sort_name_start(title)));
    const char first = key.empty() ? 0 : key[0];
    if (first >= 'a' && first <= 'z') return static_cast<char>(first - 'a' + 'A');
    // Bytes from 0x80 sort after every ASCII letter: another script.
    if (static_cast<unsigned char>(first) >= 0x80) return kOtherScriptLetter;
    return '#';
}

std::string folder_title(const std::string& path, const std::string& id) {
    const std::size_t file_slash = path.find_last_of('/');
    if (file_slash == std::string::npos || file_slash == 0) return "";
    const std::size_t dir_slash = path.find_last_of('/', file_slash - 1);
    if (dir_slash == std::string::npos) return "";
    const std::string folder = path.substr(dir_slash + 1, file_slash - dir_slash - 1);
    const std::string suffix = "[" + id + "]";
    if (folder.size() <= suffix.size() || folder.compare(folder.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return "";
    }
    std::string title = trim(folder.substr(0, folder.size() - suffix.size()));
    if (title.size() > kMaxTitle) title.resize(utf8_cut(title, kMaxTitle));
    return title;
}

namespace {

// The title of a "Title [ID]" folder named for another ID than the disc's
// own: a mod kept under an ID of its own ("Mario Kart Double Dash Plus
// [GM4E05]" holding a GM4E01 disc), whose name the database's entry for
// the original would hide. Empty otherwise.
std::string renamed_folder_title(const std::string& path, const std::string& id) {
    const std::size_t file_slash = path.find_last_of('/');
    if (file_slash == std::string::npos || file_slash == 0) return "";
    const std::size_t dir_slash = path.find_last_of('/', file_slash - 1);
    if (dir_slash == std::string::npos) return "";
    const std::string folder = trim(path.substr(dir_slash + 1, file_slash - dir_slash - 1));
    if (folder.size() < 8 || folder.back() != ']' || folder[folder.size() - 8] != '[') return "";
    const std::string folder_id = folder.substr(folder.size() - 7, 6);
    if (!plausible_id(folder_id) || folder_id == id) return "";
    return folder_title(path, folder_id);
}

}  // namespace

std::string display_title(const TitleTable* table, const std::string& id, const std::string& path,
                          const std::string& internal) {
    const std::string renamed = renamed_folder_title(path, id);
    if (!renamed.empty()) return renamed;
    if (table) {
        if (const std::string* t = table->find(id)) return *t;
    }
    const std::string folder = folder_title(path, id);
    if (!folder.empty()) return folder;
    return internal.empty() ? id : internal;
}

}  // namespace riftwii
