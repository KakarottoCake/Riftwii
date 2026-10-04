// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/problemreport.hpp"

#include <algorithm>
#include <sstream>

namespace riftwii {
namespace {

// Room for a part's header line and the note where its middle was cut.
constexpr std::size_t kPartOverhead = 160;

std::string header(const ReportPart& part, std::size_t cut) {
    std::string line = "\n===== " + part.name;
    if (!part.found) return line + " (not there) =====\n";
    line += " (" + std::to_string(part.text.size()) + " bytes";
    if (cut != 0) line += ", " + std::to_string(cut) + " cut from the middle";
    return line + ") =====\n";
}

// The part in at most `keep` bytes: a third from the start, the rest from
// the end, each on whole lines where there are any.
std::string shorten(const std::string& text, std::size_t keep, std::size_t& cut) {
    cut = 0;
    if (text.size() <= keep) return text;
    std::size_t head = keep / 3;
    std::size_t tail = text.size() - (keep - head);
    const std::size_t head_line = text.rfind('\n', head);
    if (head_line != std::string::npos && head_line > head / 2) head = head_line + 1;
    const std::size_t tail_line = text.find('\n', tail);
    if (tail_line != std::string::npos && tail_line < tail + (keep - head) / 2) tail = tail_line + 1;
    cut = tail - head;
    return text.substr(0, head) + "\n[... " + std::to_string(cut) + " bytes cut ...]\n" + text.substr(tail);
}

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

}  // namespace

std::string printable_utf8(const std::string& text) {
    std::string out = text;
    const std::size_t n = text.size();
    std::size_t i = 0;
    while (i < n) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            if ((c < 0x20 && c != '\t' && c != '\n' && c != '\r') || c == 0x7F) out[i] = '?';
            ++i;
            continue;
        }
        // The sequence's length and the range its second byte must be in
        // (which rules out overlong forms, surrogates and > U+10FFFF).
        std::size_t len = 0;
        unsigned char lo = 0x80, hi = 0xBF;
        if (c >= 0xC2 && c <= 0xDF) len = 2;
        else if (c == 0xE0) len = 3, lo = 0xA0;
        else if (c >= 0xE1 && c <= 0xEC) len = 3;
        else if (c == 0xED) len = 3, hi = 0x9F;
        else if (c >= 0xEE && c <= 0xEF) len = 3;
        else if (c == 0xF0) len = 4, lo = 0x90;
        else if (c >= 0xF1 && c <= 0xF3) len = 4;
        else if (c == 0xF4) len = 4, hi = 0x8F;
        bool ok = len != 0 && i + len <= n;
        for (std::size_t k = 1; ok && k < len; ++k) {
            const unsigned char b = static_cast<unsigned char>(text[i + k]);
            ok = k == 1 ? (b >= lo && b <= hi) : (b >= 0x80 && b <= 0xBF);
        }
        if (!ok) {
            out[i] = '?';
            ++i;
            continue;
        }
        i += len;
    }
    return out;
}

std::string assemble_report(const std::string& raw_summary, const std::vector<ReportPart>& raw_parts,
                            std::size_t limit) {
    const std::string summary = printable_utf8(raw_summary);
    std::vector<ReportPart> parts = raw_parts;
    for (ReportPart& p : parts) {
        p.name = printable_utf8(p.name);
        p.text = printable_utf8(p.text);
    }
    std::size_t fixed = summary.size() + 64;
    std::size_t largest = 0;
    for (const ReportPart& p : parts) {
        fixed += kPartOverhead + p.name.size() * 2;
        largest = std::max(largest, p.text.size());
    }
    const std::size_t budget = limit > fixed ? limit - fixed : 0;
    // The largest share every part may keep: parts under it stay whole.
    const auto total_at = [&](std::size_t cap) {
        std::size_t total = 0;
        for (const ReportPart& p : parts) total += std::min(p.text.size(), cap);
        return total;
    };
    std::size_t cap = largest;
    if (total_at(cap) > budget) {
        std::size_t low = 0, high = largest;
        while (low < high) {
            const std::size_t mid = low + (high - low + 1) / 2;
            if (total_at(mid) <= budget) low = mid;
            else high = mid - 1;
        }
        cap = low;
    }

    std::string out = summary;
    if (!out.empty() && out.back() != '\n') out += '\n';
    out += "\nIn this report:\n";
    for (const ReportPart& p : parts) {
        out += "  " + p.name + (p.found ? " (" + std::to_string(p.text.size()) + " bytes)" : " (not there)") + "\n";
    }
    for (const ReportPart& p : parts) {
        std::size_t cut = 0;
        const std::string body = p.found ? shorten(p.text, cap, cut) : std::string();
        out += header(p, cut);
        out += body;
        if (!body.empty() && body.back() != '\n') out += '\n';
    }
    out += "\n===== end of report =====\n";
    return out;
}

namespace {

// The game ID that starts `text` (up to the next space), or "".
std::string game_id_at(const std::string& text) {
    const std::string id = text.substr(0, text.find(' '));
    if (id.size() != 4 && id.size() != 6) return "";
    for (char c : id) {
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return "";
    }
    return id;
}

}  // namespace

std::string hide_settings_secrets(const std::string& settings) {
    std::string out;
    std::size_t at = 0;
    while (at < settings.size()) {
        std::size_t end = settings.find('\n', at);
        if (end == std::string::npos) end = settings.size();
        std::string line = settings.substr(at, end - at);
        std::size_t key = line.find_first_not_of(" \t");
        if (key != std::string::npos && line.compare(key, 10, "riitag_key") == 0) {
            const std::size_t eq = line.find('=', key);
            if (eq != std::string::npos) {
                const bool cr = !line.empty() && line.back() == '\r';
                line = line.substr(0, eq + 1) + " (hidden)" + (cr ? "\r" : "");
            }
        }
        out += line;
        if (end < settings.size()) out += '\n';
        at = end + 1;
    }
    return out;
}

std::string launched_game_id(const std::string& boot_log) {
    const std::string first = boot_log.substr(0, boot_log.find('\n'));
    const std::size_t at = first.find(": launch ");
    if (at != std::string::npos) return game_id_at(first.substr(at + 9));
    // A boot without packs: the disc line, the first one only.
    std::istringstream lines(boot_log);
    std::string line;
    while (std::getline(lines, line)) {
        const std::size_t disc = line.find("] Disc: ");
        if (disc != std::string::npos) return game_id_at(line.substr(disc + 8));
        if (line.compare(0, 6, "Disc: ") == 0) return game_id_at(line.substr(6));
    }
    return "";
}

std::vector<std::string> enabled_pack_files(const std::string& choices) {
    std::vector<std::string> out;
    std::istringstream lines(choices);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t t1 = line.find('\t');
        if (t1 == std::string::npos || line.find('\t', t1 + 1) != std::string::npos) continue;
        const std::string file = line.substr(0, t1);
        if (file == "*riftwii*" || line.substr(t1 + 1) != "on") continue;
        if (std::find(out.begin(), out.end(), file) == out.end()) out.push_back(file);
    }
    return out;
}

bool paste_link(int status, const std::string& answer, std::string& link, bool& partial, std::string& error,
                const std::string& site) {
    partial = status == 206;
    const std::string text = trim(answer);
    if ((status == 201 || status == 206) && text.compare(0, 8, "https://") == 0 &&
        text.find_first_of(" \t\r\n") == std::string::npos) {
        link = text;
        return true;
    }
    if (status == 429) {
        error = site + " is busy (too many uploads); try again in a few minutes";
    } else {
        error = site + " answered " + std::to_string(status);
        if (!text.empty()) error += ": " + text.substr(0, 120);
    }
    return false;
}

}  // namespace riftwii
