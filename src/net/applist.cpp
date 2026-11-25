// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/applist.hpp"

#include <cctype>
#include <cstring>
#include <vector>

#include "riftwii/jsonreader.hpp"

namespace riftwii {
namespace {

// A path on the card is at most this long, "apps/<id>/..." included.
constexpr std::size_t kMaxCardPath = 255;

// Ids an app may never have: its folder would be RiftWii's own
// (apps/riftwii holds the running boot.dol and the plugins) or the
// channel installer's. Compared without case (FAT).
const char* const kReservedIds[] = {"riftwii", "riftwii_channel", "riftwii-channel"};

// Folders on the card an app may never claim as its data: the Wii's own
// (an emulated NAND's tree at the card's root), RiftWii's, and the ones
// games, mods and other loaders read. Compared without case (FAT).
const char* const kReservedFolders[] = {
    "apps", "riftwii", "private", "title", "ticket", "shared1", "shared2", "sys", "import", "meta",
    "tmp", "wfs", "nands", "wbfs", "games", "wad", "wads", "wiiware", "riivolution", "codes",
    "txtcodes", "gct", "saves", "bootmii", "wiiflow", "usbloader_gx", "nintendont", "roms",
};

bool same_text_nocase(const std::string& a, const char* b) {
    std::size_t i = 0;
    for (; i < a.size() && b[i] != '\0'; ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return i == a.size() && b[i] == '\0';
}

// One part of a path inside a download: not empty or only dots, nothing
// FAT refuses or reads as a separator, no control character, no trailing
// dot or space (FAT drops them, so "boot.dol." would land on boot.dol), at
// most 255 bytes, well-formed UTF-8 (long file names hold it).
bool path_part_ok(const std::string& part) {
    if (part.empty() || part.size() > 255 || part.find_first_not_of('.') == std::string::npos) return false;
    if (part.back() == '.' || part.back() == ' ' || !utf8_ok(part)) return false;
    for (char c : part) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F) return false;
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            return false;
    }
    return true;
}

// Every '/'-separated part of `path` is plain (a trailing '/' allowed when
// `folder`). With `wildcards`, a part may also be "*" (a rule's `from`).
bool relative_path_ok(const std::string& path, bool folder, bool wildcards = false) {
    if (path.empty() || path[0] == '/') return false;
    std::string body = path;
    if (folder) {
        if (body.back() != '/') return false;
        body.pop_back();
    } else if (body.back() == '/') {
        return false;
    }
    std::size_t at = 0;
    while (at <= body.size()) {
        std::size_t slash = body.find('/', at);
        if (slash == std::string::npos) slash = body.size();
        const std::string part = body.substr(at, slash - at);
        if (!(wildcards && part == "*") && !path_part_ok(part)) return false;
        at = slash + 1;
    }
    return true;
}

// Whether `entry` starts with the folder `from` ("" for the top level), a
// "*" part matching any one part (a release's versioned top folder). `rest`
// is what follows it.
bool folder_prefix(const std::string& from, const std::string& entry, std::string& rest) {
    std::size_t f = 0, e = 0;
    while (f < from.size()) {
        const std::size_t f_end = from.find('/', f);
        const std::size_t e_end = entry.find('/', e);
        if (f_end == std::string::npos || e_end == std::string::npos) return false;
        const std::string want = from.substr(f, f_end - f);
        const std::string part = entry.substr(e, e_end - e);
        // A part `*` takes is checked as a written one would be: "../x/"
        // matches no rule, however harmless what follows.
        if (want == "*" ? !path_part_ok(part) : want != part) return false;
        f = f_end + 1;
        e = e_end + 1;
    }
    rest = entry.substr(e);
    return !rest.empty();
}

// scheme://host/path, with a host name of letters, digits, '-' and '.'
// (at least one dot, no IP address), and a path of plain URL characters.
bool web_url_ok(const std::string& url, std::size_t max_bytes, bool http_too) {
    const std::string secure = "https://", plain = "http://";
    std::string scheme;
    if (url.compare(0, secure.size(), secure) == 0) scheme = secure;
    else if (http_too && url.compare(0, plain.size(), plain) == 0) scheme = plain;
    else return false;
    if (url.size() >= max_bytes) return false;
    const std::size_t host_end = url.find('/', scheme.size());
    if (host_end == std::string::npos || host_end == scheme.size()) return false;
    const std::string host = url.substr(scheme.size(), host_end - scheme.size());
    // Labels of letters, digits and '-' (no '@', no ':' for a port, no
    // '['), none empty, and the last one letters only: a name, never an
    // address in any spelling (0x7f.0x0.0x0.1, 2130706433).
    std::size_t label_start = 0;
    bool last_letters = true;
    for (std::size_t i = 0; i <= host.size(); ++i) {
        if (i == host.size() || host[i] == '.') {
            if (i == label_start || host[label_start] == '-' || host[i - 1] == '-') return false;
            label_start = i + 1;
            if (i < host.size()) last_letters = true;
            continue;
        }
        const char c = host[i];
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (!letter) last_letters = false;
        if (!letter && !((c >= '0' && c <= '9') || c == '-')) return false;
    }
    if (host.find('.') == std::string::npos || !last_letters) return false;
    for (std::size_t i = host_end; i < url.size(); ++i) {
        const char c = url[i];
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                           c == '_' || c == '.' || c == '~' || c == '/' || c == '%' || c == '+' || c == '!' ||
                           c == '(' || c == ')' || c == ',' || c == '=' || c == '&' || c == '?';
        if (!plain) return false;
    }
    const std::string path = url.substr(host_end);
    return path.find("/../") == std::string::npos && path.find("/./") == std::string::npos &&
           !(path.size() >= 3 && path.compare(path.size() - 3, 3, "/..") == 0);
}

bool date_ok(const std::string& date) {
    if (date.empty()) return true;
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') return false;
    for (std::size_t i = 0; i < date.size(); ++i) {
        if (i == 4 || i == 7) continue;
        if (date[i] < '0' || date[i] > '9') return false;
    }
    return true;
}

bool format_from(const std::string& text, AppFormat& format) {
    if (text == "zip") format = AppFormat::Zip;
    else if (text == "dol") format = AppFormat::Dol;
    else if (text == "7z") format = AppFormat::SevenZip;
    else if (text == "rar") format = AppFormat::Rar;
    else if (text == "tar") format = AppFormat::Tar;
    else if (text == "tar.bz2") format = AppFormat::TarBz2;
    else if (text == "tar.gz") format = AppFormat::TarGz;
    else return false;
    return true;
}

// The fields of one app object, as read, before they are checked.
struct RawApp {
    std::string id, name, author, version, released, category, description, license, page, icon, newer;
    std::string url, format, sha256;
    std::uint64_t size = 0, files = 0, unpacked_size = 0;
    bool have_size = false, have_files = false, have_unpacked = false, have_download = false;
    std::vector<std::string> platforms, controllers, data_folders;
    std::vector<std::pair<std::string, std::string>> install;
    bool malformed = false;  // a field of the wrong kind, too many entries
};

// A string field; another kind of value, or one too long to keep, marks
// the app malformed.
bool read_text(JsonReader& r, RawApp& a, std::string& into) {
    if (r.peek() != '"') {
        a.malformed = true;
        return r.skip_value(1);
    }
    if (!r.string(into)) return false;
    if (r.last_overlong()) a.malformed = true;
    return true;
}

// A whole number field.
bool read_whole(JsonReader& r, RawApp& a, std::uint64_t& into, bool& have) {
    const char next = r.peek();
    if (next == '"' || next == '{' || next == '[' || next == 't' || next == 'f' || next == 'n') {
        a.malformed = true;
        return r.skip_value(2);
    }
    std::uint64_t value;
    bool ok;
    if (!r.number(value, ok)) return false;
    if (!ok) a.malformed = true;
    into = value;
    have = ok;
    return true;
}

// An array of strings, at most `max` of them.
bool read_strings(JsonReader& r, RawApp& a, std::vector<std::string>& into, std::size_t max) {
    if (!r.take('[')) {
        a.malformed = true;
        return r.skip_value(1);
    }
    if (r.take(']')) return true;
    for (;;) {
        std::string item;
        if (r.peek() != '"') {
            a.malformed = true;
            if (!r.skip_value(2)) return false;
        } else {
            if (!r.string(item)) return false;
            if (r.last_overlong() || into.size() >= max) a.malformed = true;
            else into.push_back(item);
        }
        if (r.take(',')) continue;
        return r.take(']');
    }
}

bool read_download(JsonReader& r, RawApp& a) {
    if (!r.take('{')) {
        a.malformed = true;
        return r.skip_value(1);
    }
    a.have_download = true;
    if (r.take('}')) return true;
    for (;;) {
        std::string key;
        if (!r.string(key) || !r.take(':')) return false;
        bool good;
        if (key == "url") good = read_text(r, a, a.url);
        else if (key == "format") good = read_text(r, a, a.format);
        else if (key == "sha256") good = read_text(r, a, a.sha256);
        else if (key == "size") good = read_whole(r, a, a.size, a.have_size);
        else if (key == "files") good = read_whole(r, a, a.files, a.have_files);
        else if (key == "unpacked_size") good = read_whole(r, a, a.unpacked_size, a.have_unpacked);
        else good = r.skip_value(2);
        if (!good) return false;
        if (r.take(',')) continue;
        return r.take('}');
    }
}

bool read_install(JsonReader& r, RawApp& a) {
    if (!r.take('[')) {
        a.malformed = true;
        return r.skip_value(1);
    }
    if (r.take(']')) return true;
    for (;;) {
        // A rule that is not an object leaves this app out, not the list.
        if (r.peek() != '{') {
            a.malformed = true;
            if (!r.skip_value(2)) return false;
            if (r.take(',')) continue;
            return r.take(']');
        }
        r.take('{');
        std::string from, to;
        if (!r.take('}')) {
            for (;;) {
                std::string key;
                if (!r.string(key) || !r.take(':')) return false;
                bool good;
                if (key == "from") good = read_text(r, a, from);
                else if (key == "to") good = read_text(r, a, to);
                else good = r.skip_value(3);
                if (!good) return false;
                if (r.take(',')) continue;
                if (!r.take('}')) return false;
                break;
            }
        }
        if (a.install.size() >= kAppMaxInstallRules) a.malformed = true;
        else a.install.emplace_back(from, to);
        if (r.take(',')) continue;
        return r.take(']');
    }
}

bool read_app(JsonReader& r, RawApp& a) {
    if (!r.take('{')) return false;
    if (r.take('}')) return true;
    for (;;) {
        std::string key;
        if (!r.string(key) || !r.take(':')) return false;
        bool good;
        if (key == "id") good = read_text(r, a, a.id);
        else if (key == "name") good = read_text(r, a, a.name);
        else if (key == "author") good = read_text(r, a, a.author);
        else if (key == "version") good = read_text(r, a, a.version);
        else if (key == "released") good = read_text(r, a, a.released);
        else if (key == "category") good = read_text(r, a, a.category);
        else if (key == "description") good = read_text(r, a, a.description);
        else if (key == "license") good = read_text(r, a, a.license);
        else if (key == "page") good = read_text(r, a, a.page);
        else if (key == "icon") good = read_text(r, a, a.icon);
        else if (key == "newer") good = read_text(r, a, a.newer);
        else if (key == "platforms") good = read_strings(r, a, a.platforms, 3);
        else if (key == "controllers") {
            // Only pictures and a filter hang on it: a wrong list (more than
            // five names, not strings) leaves them out, not the whole app.
            const bool malformed_before = a.malformed;
            good = read_strings(r, a, a.controllers, 5);
            if (a.malformed && !malformed_before) {
                a.malformed = false;
                a.controllers.clear();
            }
        }
        else if (key == "data_folders") good = read_strings(r, a, a.data_folders, kAppMaxDataFolders);
        else if (key == "download") good = read_download(r, a);
        else if (key == "install") good = read_install(r, a);
        else good = r.skip_value(1);
        if (!good) return false;  // broken text: the reader cannot go on
        if (r.take(',')) continue;
        return r.take('}');
    }
}

void copy_field(char* to, std::size_t room, const std::string& from) {
    const std::size_t n = from.size() < room ? from.size() : room - 1;
    std::memcpy(to, from.data(), n);
    to[n] = '\0';
}

// Whether `to` is a place an install rule may write: "apps/<id>/..." or
// "<data folder>/...", with every part plain. A folder `from` (ending in
// '/', or "" for the download's top level) needs a folder `to`; a file
// `from`, a file `to`.
bool rule_target_ok(const RawApp& a, const std::string& from, const std::string& to) {
    const bool folder = from.empty() || from.back() == '/';
    if ((!from.empty() && !relative_path_ok(from, folder, folder)) || !relative_path_ok(to, folder)) return false;
    if (from.size() >= sizeof(AppInstallRule::from) || to.size() >= sizeof(AppInstallRule::to)) return false;
    const std::string own = "apps/" + a.id + "/";
    if (to.compare(0, own.size(), own) == 0) return true;
    for (const std::string& data : a.data_folders) {
        const std::string mine = data + "/";
        if (to.compare(0, mine.size(), mine) == 0) return true;
    }
    return false;
}

// Checks an app as read; on success, fills `out`.
bool app_from_raw(RawApp& a, AppListing& out) {
    if (a.malformed || !a.have_download) return false;
    std::memset(&out, 0, sizeof(out));
    const bool texts = json_clean_text(a.name, sizeof(out.name)) && json_clean_text(a.author, sizeof(out.author)) &&
                       json_clean_text(a.version, sizeof(out.version)) &&
                       json_clean_text(a.description, sizeof(out.description));
    if (!texts || !app_id_ok(a.id) || !app_category_ok(a.category) || !date_ok(a.released)) return false;
    if (!a.license.empty() && !json_clean_text(a.license, sizeof(out.license))) return false;
    if (!app_page_url_ok(a.page)) return false;
    if (!a.icon.empty() && !app_icon_url_ok(a.icon)) return false;
    if (!a.newer.empty() && (!app_id_ok(a.newer) || a.newer == a.id)) return false;
    AppFormat format;
    if (!app_download_url_ok(a.url) || !format_from(a.format, format) || !sha256_hex_ok(a.sha256)) return false;
    if (!a.have_size || a.size == 0 || a.size > kAppDownloadMaxBytes) return false;
    std::uint8_t platforms = 0;
    for (const std::string& p : a.platforms) {
        if (p == "wii") platforms |= kAppOnWii;
        else if (p == "vwii") platforms |= kAppOnVWii;
        else if (p == "mini") platforms |= kAppOnWiiMini;
    }
    if (platforms == 0) return false;
    // Controllers a later list may name and this RiftWii does not know are
    // passed over: the field only adds pictures and a filter.
    std::uint8_t controllers = 0;
    for (const std::string& c : a.controllers) {
        if (c == "wiimote") controllers |= kAppWiiRemote;
        else if (c == "nunchuk") controllers |= kAppNunchuk;
        else if (c == "classic") controllers |= kAppClassic;
        else if (c == "gamecube") controllers |= kAppGameCube;
        else if (c == "keyboard") controllers |= kAppKeyboard;
    }
    for (const std::string& data : a.data_folders) {
        if (!app_data_folder_ok(data)) return false;
    }
    if (format == AppFormat::Zip) {
        // What the Wii unpacks: counted, sized and placed by its rules.
        if (!a.have_files || !a.have_unpacked || a.files == 0 || a.files > kAppMaxFiles || a.unpacked_size == 0 ||
            a.unpacked_size > kAppMaxUnpackedBytes || a.install.empty())
            return false;
        for (const auto& rule : a.install) {
            if (!rule_target_ok(a, rule.first, rule.second)) return false;
        }
    } else if (!a.install.empty()) {
        // A DOL is placed as apps/<id>/boot.dol; other formats are not
        // installed here at all.
        return false;
    }
    copy_field(out.id, sizeof(out.id), a.id);
    copy_field(out.name, sizeof(out.name), a.name);
    copy_field(out.author, sizeof(out.author), a.author);
    copy_field(out.version, sizeof(out.version), a.version);
    copy_field(out.released, sizeof(out.released), a.released);
    copy_field(out.category, sizeof(out.category), a.category);
    copy_field(out.description, sizeof(out.description), a.description);
    copy_field(out.license, sizeof(out.license), a.license);
    copy_field(out.page, sizeof(out.page), a.page);
    copy_field(out.icon, sizeof(out.icon), a.icon);
    copy_field(out.newer, sizeof(out.newer), a.newer);
    copy_field(out.url, sizeof(out.url), a.url);
    copy_field(out.sha256, sizeof(out.sha256), a.sha256);
    for (std::size_t i = 0; i < a.data_folders.size(); ++i)
        copy_field(out.data_folders[i], sizeof(out.data_folders[i]), a.data_folders[i]);
    out.data_folder_count = static_cast<std::uint8_t>(a.data_folders.size());
    for (std::size_t i = 0; i < a.install.size(); ++i) {
        copy_field(out.install[i].from, sizeof(out.install[i].from), a.install[i].first);
        copy_field(out.install[i].to, sizeof(out.install[i].to), a.install[i].second);
    }
    out.install_count = static_cast<std::uint8_t>(a.install.size());
    out.size = static_cast<std::uint32_t>(a.size);
    out.files = static_cast<std::uint32_t>(a.files);
    out.unpacked_size = static_cast<std::uint32_t>(a.unpacked_size);
    out.platforms = platforms;
    out.controllers = controllers;
    out.format = format;
    return true;
}

}  // namespace

bool app_id_ok(const std::string& id) {
    if (id.empty() || id.size() >= 48 || id[0] == '.' || id[0] == ' ' || id.back() == ' ' || id.back() == '.') return false;
    for (char c : id) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                        c == '_' || c == ' ' || c == '.' || c == '+' || c == '(' || c == ')' || c == '!';
        if (!ok) return false;
    }
    for (const char* reserved : kReservedIds) {
        if (same_text_nocase(id, reserved)) return false;
    }
    return id != "." && id != "..";
}

bool app_category_ok(const std::string& key) {
    if (key.empty() || key.size() > 15) return false;
    for (char c : key) {
        if (c < 'a' || c > 'z') return false;
    }
    return true;
}

bool app_data_folder_ok(const std::string& name) {
    if (name.size() >= 32 || !app_id_ok(name)) return false;
    for (const char* reserved : kReservedFolders) {
        if (same_text_nocase(name, reserved)) return false;
    }
    return true;
}

bool app_download_url_ok(const std::string& url) { return web_url_ok(url, 300, true); }

bool app_page_url_ok(const std::string& url) { return web_url_ok(url, 200, false); }

bool app_icon_url_ok(const std::string& url) {
    const std::string site = kAppSiteUrl;
    return url.size() > site.size() && url.compare(0, site.size(), site) == 0 && web_url_ok(url, 200, false) &&
           url.find('?') == std::string::npos;
}

bool parse_app_list(const char* text, std::size_t text_size, AppListing* out, std::size_t max, std::size_t& count,
                    std::size_t& skipped, std::string& error) {
    count = 0;
    skipped = 0;
    if (text_size > kAppListMaxBytes) {
        error = "the app list is bigger than RiftWii takes (2 MB)";
        return false;
    }
    if (max > kAppListMaxApps) max = kAppListMaxApps;
    const auto bad = [&] {
        error = "the app list is not in a form RiftWii reads";
        return false;
    };
    JsonReader r(text, text_size);
    if (!r.take('{')) return bad();
    bool have_schema = false, have_apps = false;
    if (!r.take('}')) {
        for (;;) {
            std::string key;
            if (!r.string(key) || !r.take(':')) return bad();
            if (key == "schema") {
                std::uint64_t value;
                bool ok;
                if (!r.number(value, ok) || !ok || value != 1) {
                    error = "the app list is from a newer RiftWii than this one (schema is not 1)";
                    return false;
                }
                have_schema = true;
            } else if (key == "apps") {
                if (!r.take('[')) return bad();
                have_apps = true;
                count = 0;
                skipped = 0;
                if (!r.take(']')) {
                    for (;;) {
                        RawApp a;
                        if (r.peek() != '{' || !read_app(r, a)) return bad();
                        AppListing listing;
                        if (!app_from_raw(a, listing) || count >= max) {
                            ++skipped;
                        } else {
                            // One id twice (FAT: whatever the case): the first stands.
                            bool again = false;
                            for (std::size_t i = 0; i < count && !again; ++i) again = same_text_nocase(a.id, out[i].id);
                            if (again) ++skipped;
                            else out[count++] = listing;
                        }
                        if (r.take(',')) continue;
                        if (!r.take(']')) return bad();
                        break;
                    }
                }
            } else if (!r.skip_value(1)) {
                return bad();
            }
            if (r.take(',')) continue;
            if (!r.take('}')) return bad();
            break;
        }
    }
    if (!r.at_end()) return bad();
    if (!have_schema) {
        error = "the app list says nothing about its schema";
        return false;
    }
    if (!have_apps) return bad();
    return true;
}

bool parse_app_list(const std::string& text, AppListing* out, std::size_t max, std::size_t& count,
                    std::size_t& skipped, std::string& error) {
    return parse_app_list(text.data(), text.size(), out, max, count, skipped, error);
}

bool app_install_path(const AppListing& app, const std::string& raw_entry, std::string& card_path) {
    if (app.format != AppFormat::Zip) return false;
    // Some zips made on Windows separate folders with backslashes
    // ("apps\WiiMedic\boot.dol"); they are read as '/', and every part is
    // checked as usual after that.
    std::string zip_entry = raw_entry;
    for (char& c : zip_entry) {
        if (c == '\\') c = '/';
    }
    for (std::size_t i = 0; i < app.install_count; ++i) {
        const std::string from = app.install[i].from;
        const std::string to = app.install[i].to;
        std::string rest;
        if (from.empty() || from.back() == '/') {
            if (!folder_prefix(from, zip_entry, rest)) continue;
            // A folder entry ("dir/") makes nothing by itself; its files do.
            if (rest.back() == '/') return false;
            if (!relative_path_ok(rest, false)) return false;
            card_path = to + rest;
        } else {
            if (zip_entry != from) continue;
            card_path = to;
        }
        return card_path.size() <= kMaxCardPath;
    }
    return false;
}

}  // namespace riftwii
