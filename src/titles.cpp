// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/titles.hpp"

namespace riftwii {
namespace {

constexpr std::size_t kMaxTitle = 200;

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
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
        if (title.size() > kMaxTitle) title.resize(kMaxTitle);
        titles_[id] = title;
    }
}

const std::string* TitleTable::find(const std::string& id) const {
    auto it = titles_.find(id);
    if (it == titles_.end() && id.size() == 6) it = titles_.find(id.substr(0, 4));
    return it == titles_.end() ? nullptr : &it->second;
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
    if (title.size() > kMaxTitle) title.resize(kMaxTitle);
    return title;
}

std::string display_title(const TitleTable* table, const std::string& id, const std::string& path,
                          const std::string& internal) {
    if (table) {
        if (const std::string* t = table->find(id)) return *t;
    }
    const std::string folder = folder_title(path, id);
    if (!folder.empty()) return folder;
    return internal.empty() ? id : internal;
}

}  // namespace riftwii
