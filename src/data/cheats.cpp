// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/cheats.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace riftwii {
namespace {

std::string trim(const std::string& s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// "XXXXXXXX XXXXXXXX": 1 with the two words, 2 when it has that shape
// with placeholders (letters beyond F) in it, 0 otherwise.
int code_line(const std::string& line, std::uint32_t& a, std::uint32_t& b) {
    if (line.size() != 17 || line[8] != ' ') return 0;
    bool placeholder = false;
    std::uint32_t words[2] = {0, 0};
    for (std::size_t i = 0; i < 17; ++i) {
        if (i == 8) continue;
        const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(line[i])));
        unsigned v = 0;
        if (c >= '0' && c <= '9') {
            v = static_cast<unsigned>(c - '0');
        } else if (c >= 'A' && c <= 'F') {
            v = static_cast<unsigned>(c - 'A' + 10);
        } else if (c >= 'G' && c <= 'Z') {
            placeholder = true;
        } else {
            return 0;
        }
        std::uint32_t& w = words[i < 8 ? 0 : 1];
        w = (w << 4) | v;
    }
    a = words[0];
    b = words[1];
    return placeholder ? 2 : 1;
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

// Whether lines[i] is a cheat file's game ID line: 4 or 6 capitals and
// digits, and a code line never follows it. A file without a header may
// start with a cheat named "Speed2", whose first code line would otherwise
// be taken as the game's name.
bool is_id_line(const std::vector<std::string>& lines, std::size_t i) {
    if (i >= lines.size() || (lines[i].size() != 6 && lines[i].size() != 4)) return false;
    for (char c : lines[i]) {
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    }
    std::uint32_t a, b;
    std::size_t next = i + 1;
    while (next < lines.size() && lines[next].empty()) ++next;
    return !(next < lines.size() && code_line(lines[next], a, b) != 0);
}

// A heading's words, lower case, longest first so "version" is not read
// as "v": the region a release was for, then what its number is called.
const char* const kHeadingRegions[] = {"ntsc-u", "ntsc-j", "ntsc-k", "ntsc", "pal"};
const char* const kHeadingVersionWords[] = {"version", "ver.", "ver", "v"};
const char* const kHeadingRevisionWords[] = {"revision", "rev.", "rev"};
// Longer lines are never headings (a cheat's name or a note).
constexpr std::size_t kMaxHeadingLength = 40;

// Whether `word` is at `at` in `s`, followed by a space, a digit or the
// end; `at` then moves past it and the spaces after it.
bool take_word(const std::string& s, std::size_t& at, const char* word) {
    const std::size_t n = std::char_traits<char>::length(word);
    if (s.compare(at, n, word) != 0) return false;
    const std::size_t after = at + n;
    if (after < s.size() && s[after] != ' ' && !std::isdigit(static_cast<unsigned char>(s[after]))) return false;
    at = after;
    while (at < s.size() && s[at] == ' ') ++at;
    return true;
}

template <std::size_t N>
bool take_any_word(const std::string& s, std::size_t& at, const char* const (&words)[N]) {
    for (const char* word : words) {
        if (take_word(s, at, word)) return true;
    }
    return false;
}

// Digits at `at`, at most `max_digits` of them; -1 when there are none or
// too many.
int take_number(const std::string& s, std::size_t& at, std::size_t max_digits) {
    const std::size_t from = at;
    int value = 0;
    while (at < s.size() && std::isdigit(static_cast<unsigned char>(s[at]))) {
        if (at - from == max_digits) return -1;
        value = value * 10 + (s[at] - '0');
        ++at;
    }
    return at == from ? -1 : value;
}

// The lines between headings carry the revision of the heading above them
// (kAnyRevision before the first one).
bool shown_for(int section, int revision) {
    return revision == kAnyRevision || section == kAnyRevision || section == revision;
}

// A cheat file as text: its header lines, then its blocks (one cheat or
// note each, as the lines between blank lines), trimmed. `section[i]` is
// the revision heading block i is under (a heading is under itself).
struct CheatBlocks {
    std::vector<std::string> header;
    std::vector<std::vector<std::string>> blocks;
    std::vector<int> section;
};

CheatBlocks split_blocks(const std::string& text) {
    CheatBlocks out;
    std::istringstream in(text);
    std::string raw;
    std::vector<std::string> lines;
    while (std::getline(in, raw)) lines.push_back(trim(raw));
    if (!lines.empty() && lines[0].compare(0, 3, "\xEF\xBB\xBF") == 0) lines[0] = lines[0].substr(3);
    std::size_t i = 0;
    // As parse_cheat_text reads the header.
    if (is_id_line(lines, i)) out.header.push_back(lines[i++]);
    if (!out.header.empty() && i < lines.size() && !lines[i].empty()) out.header.push_back(lines[i++]);
    std::vector<std::string> block;
    int section = kAnyRevision;
    for (; i <= lines.size(); ++i) {
        if (i == lines.size() || lines[i].empty()) {
            if (!block.empty()) {
                if (block.size() == 1 && cheat_revision_heading(block[0]) >= 0)
                    section = cheat_revision_heading(block[0]);
                out.blocks.push_back(block);
                out.section.push_back(section);
            }
            block.clear();
        } else {
            block.push_back(lines[i]);
        }
    }
    return out;
}

// A block that is a cheat: a name, then at least one code line.
bool is_cheat(const std::vector<std::string>& block) {
    if (block.size() < 2) return false;
    std::uint32_t a, b;
    if (code_line(block[0], a, b) != 0) return false;
    for (std::size_t k = 1; k < block.size(); ++k) {
        if (code_line(block[k], a, b) != 0) return true;
    }
    return false;
}

bool has_placeholders(const std::vector<std::string>& block) {
    std::uint32_t a, b;
    for (const std::string& line : block) {
        if (code_line(line, a, b) == 2) return true;
    }
    return false;
}

std::string join_blocks(const CheatBlocks& file) {
    std::string out;
    for (const std::string& line : file.header) out += line + "\n";
    for (const std::vector<std::string>& block : file.blocks) {
        if (!out.empty()) out += "\n";
        for (const std::string& line : block) out += line + "\n";
    }
    return out;
}

bool is_template(const std::string& line) {
    const std::size_t n = std::char_traits<char>::length(kCheatTemplateNote);
    return line.compare(0, n, kCheatTemplateNote) == 0;
}

// The block of cheat `name`, named as parse_cheat_text names cheats for
// `revision` ("Name (2)" for a second one of the same name); nullptr when
// none.
std::vector<std::string>* find_cheat(CheatBlocks& file, const std::string& name, int revision) {
    std::vector<std::string> seen;
    for (std::size_t k = 0; k < file.blocks.size(); ++k) {
        std::vector<std::string>& block = file.blocks[k];
        if (!is_cheat(block) || !shown_for(file.section[k], revision)) continue;
        int n = 1;
        for (const std::string& s : seen) {
            if (s == block[0]) ++n;
        }
        seen.push_back(block[0]);
        if ((n > 1 ? block[0] + " (" + std::to_string(n) + ")" : block[0]) == name) return &block;
    }
    return nullptr;
}

bool is_placeholder(char c) {
    const char u = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return u >= 'G' && u <= 'Z';
}

// The codes with their placeholders: the template lines when RiftWii
// filled them in, else the code lines themselves.
std::vector<std::string> template_of(const std::vector<std::string>& block) {
    std::vector<std::string> codes, kept;
    const std::size_t n = std::char_traits<char>::length(kCheatTemplateNote);
    std::uint32_t a, b;
    for (std::size_t k = 1; k < block.size(); ++k) {
        if (is_template(block[k])) kept.push_back(trim(block[k].substr(n)));
        else if (code_line(block[k], a, b) != 0) codes.push_back(block[k]);
    }
    return kept.empty() ? codes : kept;
}

std::vector<std::string> codes_of(const std::vector<std::string>& block) {
    std::vector<std::string> codes;
    std::uint32_t a, b;
    for (std::size_t k = 1; k < block.size(); ++k) {
        if (!is_template(block[k]) && code_line(block[k], a, b) != 0) codes.push_back(block[k]);
    }
    return codes;
}

// Puts `from`'s block `m` (a player's own cheat) at the end of the same
// revision's part of `into`, so it stays under its heading: the cheats
// before any heading stay before the first one, and a revision `into` has
// no heading for gets `from`'s heading at the end.
void insert_in_section(CheatBlocks& into, const CheatBlocks& from, std::size_t m) {
    const int section = from.section[m];
    std::size_t at = into.blocks.size();
    bool has_section = false;
    for (std::size_t k = 0; k < into.blocks.size(); ++k) {
        if (into.section[k] == section) {
            has_section = true;
            at = k + 1;
        }
    }
    if (section == kAnyRevision && !has_section) at = 0;  // every block is under a heading
    if (!has_section && section != kAnyRevision) {
        for (std::size_t k = 0; k < from.blocks.size(); ++k) {
            const bool its_heading = from.section[k] == section && from.blocks[k].size() == 1 &&
                                     cheat_revision_heading(from.blocks[k][0]) == section;
            if (!its_heading) continue;
            into.blocks.push_back(from.blocks[k]);
            into.section.push_back(section);
            break;
        }
        at = into.blocks.size();
    }
    into.blocks.insert(into.blocks.begin() + static_cast<std::ptrdiff_t>(at), from.blocks[m]);
    into.section.insert(into.section.begin() + static_cast<std::ptrdiff_t>(at), section);
}

}  // namespace

int cheat_revision_heading(const std::string& line) {
    if (line.size() > kMaxHeadingLength) return -1;
    // Decoration around it ("-- Version 1.0 --", "[v1.1]", "Version 1.0:").
    static const char kDecoration[] = " -=*#[]():";
    const std::size_t first = line.find_first_not_of(kDecoration);
    if (first == std::string::npos) return -1;
    const std::size_t last = line.find_last_not_of(kDecoration);
    std::string s = line.substr(first, last - first + 1);
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    std::size_t at = 0;
    const bool region = take_any_word(s, at, kHeadingRegions);
    const bool version_word = take_any_word(s, at, kHeadingVersionWords);
    const bool revision_word = !version_word && take_any_word(s, at, kHeadingRevisionWords);
    if (!region && !version_word && !revision_word) return -1;  // a bare number is a note

    const int major = take_number(s, at, 3);
    if (major < 0) return -1;
    int revision = -1;
    if (at < s.size() && s[at] == '.') {
        // A release number: 1.0 and 1.00 are the first release (revision
        // 0), 1.1 and 1.01 the next, 1.02 the one after.
        ++at;
        const int minor = take_number(s, at, 2);
        if (major != 1 || minor < 0) return -1;
        revision = minor;
    } else if (revision_word) {
        revision = major;  // "Rev 1": the revision byte itself
    } else {
        return -1;  // "v1" alone could be 1.0 or revision 1
    }
    if (at != s.size() || revision > 255) return -1;
    return revision;
}

std::vector<CheatField> cheat_fields(const std::string& text, const std::string& name, int revision) {
    std::vector<CheatField> fields;
    CheatBlocks file = split_blocks(text);
    const std::vector<std::string>* block = find_cheat(file, name, revision);
    if (!block) return fields;
    const std::vector<std::string> tmpl = template_of(*block);
    const std::vector<std::string> codes = codes_of(*block);
    // Values now: read where the placeholders are, once they are filled.
    const bool filled = codes.size() == tmpl.size() && !has_placeholders(codes);
    for (std::size_t l = 0; l < tmpl.size(); ++l) {
        const std::string& line = tmpl[l];
        for (std::size_t i = 0; i < line.size();) {
            if (!is_placeholder(line[i])) {
                ++i;
                continue;
            }
            const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(line[i])));
            std::size_t j = i;
            while (j < line.size() && std::toupper(static_cast<unsigned char>(line[j])) == letter) ++j;
            CheatField* field = nullptr;
            for (CheatField& f : fields) {
                if (f.letter == letter) field = &f;
            }
            if (!field) {
                fields.push_back(CheatField{letter, 0, ""});
                field = &fields.back();
                if (filled && codes[l].size() >= j) field->value = codes[l].substr(i, j - i);
            }
            field->digits = std::max(field->digits, j - i);
            i = j;
        }
    }
    return fields;
}

bool fill_cheat_values(std::string& text, const std::string& name, const std::vector<CheatField>& fields,
                       std::string& error, int revision) {
    CheatBlocks file = split_blocks(text);
    std::vector<std::string>* block = find_cheat(file, name, revision);
    if (!block) {
        error = "no cheat called " + name;
        return false;
    }
    const std::vector<std::string> tmpl = template_of(*block);
    // Each letter's value, as many digits as its longest run.
    std::vector<CheatField> want = cheat_fields(text, name, revision);
    for (CheatField& w : want) {
        w.value.clear();
        for (const CheatField& f : fields) {
            if (f.letter == w.letter) w.value = f.value;
        }
        if (w.value.empty() || w.value.size() > w.digits) {
            error = std::string("a value for ") + w.letter + " of 1 to " + std::to_string(w.digits) + " digits";
            return false;
        }
        for (char& c : w.value) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (!std::isxdigit(static_cast<unsigned char>(c))) {
                error = std::string("the value for ") + w.letter + " is not hex";
                return false;
            }
        }
        w.value = std::string(w.digits - w.value.size(), '0') + w.value;
    }
    std::vector<std::string> out{(*block)[0]};
    for (const std::string& line : tmpl) {
        std::string code = line;
        for (std::size_t i = 0; i < code.size();) {
            if (!is_placeholder(code[i])) {
                ++i;
                continue;
            }
            const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(code[i])));
            std::size_t j = i;
            while (j < code.size() && std::toupper(static_cast<unsigned char>(code[j])) == letter) ++j;
            for (const CheatField& w : want) {
                // A shorter run of a letter takes the value's last digits.
                if (w.letter == letter) code.replace(i, j - i, w.value.substr(w.value.size() - (j - i)));
            }
            i = j;
        }
        out.push_back(code);
    }
    std::uint32_t a, b;
    for (std::size_t k = 1; k < block->size(); ++k) {
        const std::string& line = (*block)[k];
        if (!is_template(line) && code_line(line, a, b) == 0) out.push_back(line);  // its notes
    }
    for (const std::string& line : tmpl) out.push_back(kCheatTemplateNote + line);
    *block = out;
    text = join_blocks(file);
    return true;
}

std::string merge_cheat_text(const std::string& fresh, const std::string& old, std::size_t& kept) {
    kept = 0;
    CheatBlocks merged = split_blocks(fresh);
    const CheatBlocks before = split_blocks(old);
    std::vector<std::size_t> added;  // indexes into before.blocks
    for (std::size_t m = 0; m < before.blocks.size(); ++m) {
        const std::vector<std::string>& mine = before.blocks[m];
        if (!is_cheat(mine)) continue;
        bool found = false;
        for (std::size_t t = 0; t < merged.blocks.size(); ++t) {
            std::vector<std::string>& theirs = merged.blocks[t];
            // Two releases' cheats of the same name are different codes.
            if (!is_cheat(theirs) || theirs[0] != mine[0] || merged.section[t] != before.section[m]) continue;
            found = true;
            // Values filled in by hand stay filled in.
            if (has_placeholders(theirs) && !has_placeholders(mine)) {
                theirs = mine;
                ++kept;
            }
            break;
        }
        if (!found) {
            added.push_back(m);
            ++kept;
        }
    }
    for (std::size_t m : added) insert_in_section(merged, before, m);
    return join_blocks(merged);
}

bool parse_cheat_text(const std::string& text, CheatFile& out, std::string& error, int revision) {
    out = CheatFile{};
    std::istringstream in(text);
    std::string raw;
    std::vector<std::string> lines;
    while (std::getline(in, raw)) lines.push_back(trim(raw));
    // A UTF-8 byte order mark from a PC editor.
    if (!lines.empty() && lines[0].compare(0, 3, "\xEF\xBB\xBF") == 0) lines[0] = lines[0].substr(3);
    std::size_t i = 0;
    // The header: the ID line and the name line, when the file has them.
    if (is_id_line(lines, i)) out.game_id = lines[i++];
    if (!out.game_id.empty() && i < lines.size() && !lines[i].empty()) out.title = lines[i++];
    Cheat current;
    bool open = false;
    // The revision heading the lines are under, and whether they are read.
    int section = kAnyRevision;
    bool shown = true;
    const auto close = [&]() {
        if (open && (!current.words.empty() || current.needs_values)) {
            if (shown) out.cheats.push_back(current);
            else ++out.other_revision_cheats;
        }
        current = Cheat{};
        open = false;
    };
    for (; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (line.empty()) {
            close();
            continue;
        }
        std::uint32_t a = 0, b = 0;
        const int kind = code_line(line, a, b);
        if (!open) {
            if (kind != 0) continue;  // code lines with no name: nothing to call them
            // A heading is a line alone between blank lines.
            const bool alone = i + 1 == lines.size() || lines[i + 1].empty();
            const int heading = alone ? cheat_revision_heading(line) : -1;
            if (heading >= 0) {
                section = heading;
                shown = shown_for(section, revision);
                out.revisions.push_back(static_cast<std::uint8_t>(heading));
                continue;
            }
            current.name = line;
            open = true;
            continue;
        }
        if (kind == 1) {
            current.words.push_back(a);
            current.words.push_back(b);
        } else if (kind == 2) {
            current.needs_values = true;
        } else if (is_template(line)) {
            current.has_template = true;
        } else {
            current.notes.push_back(line);
        }
    }
    close();
    // Names must tell cheats apart: they are how choices are saved.
    std::vector<std::string> original;
    for (const Cheat& c : out.cheats) original.push_back(c.name);
    for (std::size_t k = 0; k < out.cheats.size(); ++k) {
        int n = 1;
        for (std::size_t j = 0; j < k; ++j) {
            if (original[j] == original[k]) ++n;
        }
        if (n > 1) out.cheats[k].name += " (" + std::to_string(n) + ")";
    }
    if (out.cheats.empty()) {
        error = "no cheats in the file";
        return false;
    }
    return true;
}

std::vector<std::uint8_t> build_gct(const CheatFile& file, const std::set<std::string>& enabled, std::size_t& count) {
    std::vector<std::uint8_t> out;
    count = 0;
    put32(out, 0x00D0C0DE);
    put32(out, 0x00D0C0DE);
    for (const Cheat& c : file.cheats) {
        if (c.needs_values || c.words.empty() || enabled.count(c.name) == 0) continue;
        for (std::uint32_t w : c.words) put32(out, w);
        ++count;
    }
    put32(out, 0xF0000000);
    put32(out, 0x00000000);
    return out;
}

namespace {

std::uint32_t get32(const std::vector<std::uint8_t>& b, std::size_t at) {
    return (std::uint32_t(b[at]) << 24) | (std::uint32_t(b[at + 1]) << 16) | (std::uint32_t(b[at + 2]) << 8) | b[at + 3];
}

}  // namespace

bool valid_gct(const std::vector<std::uint8_t>& gct) {
    if (gct.size() < 16 || gct.size() % 8 != 0) return false;
    const std::size_t end = gct.size() - 8;
    return get32(gct, 0) == 0x00D0C0DE && get32(gct, 4) == 0x00D0C0DE && get32(gct, end) == 0xF0000000 &&
           get32(gct, end + 4) == 0;
}

std::vector<std::uint8_t> join_gct(const std::vector<std::uint8_t>& first, const std::vector<std::uint8_t>& second) {
    if (first.empty()) return second;
    if (second.empty()) return first;
    std::vector<std::uint8_t> out(first.begin(), first.end() - 8);  // without its end
    out.insert(out.end(), second.begin() + 8, second.end());       // without its header
    return out;
}

}  // namespace riftwii
