// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/patch.hpp"
#include "riftwii/mempatch.hpp"
#include <pugixml.hpp>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <istream>
#include <limits>
#include <string>
#include <vector>
#include <map>
namespace riftwii {
namespace {
constexpr std::size_t kMaxXml = 1048576;
constexpr std::size_t kMaxString = 4096;
// Hex payloads (memory value/original) carry binary data as hex text, two
// characters per byte. The budget matches the runtime's 1 MiB memory value
// cap; the 1 MiB XML cap bounds the total anyway. Names and paths stay at
// kMaxString: only these two attributes are exempted in CheckAttrLengths.
constexpr std::size_t kMaxHexChars = kMaxMemoryValueBytes * 2;
constexpr std::size_t kMaxNodes = 8192;
constexpr std::size_t kMaxWarnings = 64;
constexpr int kMaxDepth = 16;
bool IsControl(unsigned char c) {
    return c < 0x20 || c == 0x7F;
}
bool HasControlChars(const std::string& s) {
    for (unsigned char c : s) {
        if (IsControl(c)) return true;
    }
    return false;
}
// Characters that can never appear in a resolved SD or disc path. Braces and
// '$' are excluded here because {$name} placeholders must be substituted
// before a path is used; see substitute_params().
bool HasBadPathChars(const std::string& s) {
    for (unsigned char c : s) {
        if (c == ':' || c == '\\' || c == '{' || c == '}' || c == '$' || IsControl(c)) {
            return true;
        }
    }
    return false;
}
bool HasPlaceholder(const std::string& s) {
    return s.find("{$") != std::string::npos;
}
bool IsAlnumUpper(const std::string& s) {
    if (s.empty()) return false;
    for (char ch : s) {
        bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
        if (!ok) return false;
    }
    return true;
}
bool ParseU64(const std::string& s, std::uint64_t& out) {
    if (s.empty()) return false;
    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        if (s.size() == 2) return false;
        if (s.size() > 18) return false;
        std::uint64_t v = 0;
        for (std::size_t i = 2; i < s.size(); ++i) {
            char c = s[i];
            unsigned digit = 0;
            if (c >= '0' && c <= '9') digit = static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') digit = static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') digit = static_cast<unsigned>(c - 'A' + 10);
            else return false;
            if (v > (std::numeric_limits<std::uint64_t>::max() - digit) / 16) return false;
            v = v * 16 + digit;
        }
        out = v;
        return true;
    }
    if (s.size() > 20) return false;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
    }
    std::uint64_t v = 0;
    for (char c : s) {
        unsigned digit = static_cast<unsigned>(c - '0');
        if (v > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return false;
        v = v * 10 + digit;
    }
    out = v;
    return true;
}
// true/false, yes/no and 1/0 - the spellings found in published patch files.
bool ParseBoolStrict(const std::string& s, bool& out) {
    if (s == "true" || s == "yes" || s == "1") {
        out = true;
        return true;
    }
    if (s == "false" || s == "no" || s == "0") {
        out = false;
        return true;
    }
    return false;
}
// Hex string: optional 0x prefix, even number of hex digits, at least one byte.
bool ParseHex(const std::string& s, std::vector<std::uint8_t>& out) {
    std::size_t start = 0;
    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) start = 2;
    std::size_t digits = s.size() - start;
    if (digits == 0 || (digits % 2) != 0) return false;
    std::vector<std::uint8_t> bytes;
    bytes.reserve(digits / 2);
    for (std::size_t i = start; i < s.size(); i += 2) {
        unsigned v = 0;
        for (std::size_t k = 0; k < 2; ++k) {
            char c = s[i + k];
            unsigned digit = 0;
            if (c >= '0' && c <= '9') digit = static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') digit = static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') digit = static_cast<unsigned>(c - 'A' + 10);
            else return false;
            v = v * 16 + digit;
        }
        bytes.push_back(static_cast<std::uint8_t>(v));
    }
    out = std::move(bytes);
    return true;
}
bool IsWhitespaceOnly(const char* s) {
    if (s == nullptr) return true;
    for (const char* p = s; *p != '\0'; ++p) {
        unsigned char c = static_cast<unsigned char>(*p);
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') return false;
    }
    return true;
}
bool HasDoctype(const std::string& xml) {
    std::size_t n = xml.size();
    std::size_t i = 0;
    while (i < n) {
        if (xml.compare(i, 4, "<!--") == 0) {
            std::size_t e = xml.find("-->", i + 4);
            if (e == std::string::npos) return true;
            i = e + 3;
            continue;
        }
        if (xml.compare(i, 9, "<![CDATA[") == 0) {
            std::size_t e = xml.find("]]>", i + 9);
            if (e == std::string::npos) return true;
            i = e + 3;
            continue;
        }
        if (i + 9 <= n) {
            const char* needle = "<!doctype";
            bool hit = true;
            for (std::size_t k = 0; k < 9; ++k) {
                char a = xml[i + k];
                char b = needle[k];
                if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
                if (a != b) { hit = false; break; }
            }
            if (hit) return true;
        }
        ++i;
    }
    return false;
}
// Rejects processing instructions except a single leading XML declaration
// (optionally preceded by a UTF-8 BOM and whitespace), which every published
// patch file carries.
bool HasForbiddenPi(const std::string& xml, std::string& error) {
    std::size_t n = xml.size();
    std::size_t i = 0;
    if (n >= 3 && static_cast<unsigned char>(xml[0]) == 0xEF &&
        static_cast<unsigned char>(xml[1]) == 0xBB && static_cast<unsigned char>(xml[2]) == 0xBF) {
        i = 3;
    }
    while (i < n && (xml[i] == ' ' || xml[i] == '\t' || xml[i] == '\n' || xml[i] == '\r')) ++i;
    if (xml.compare(i, 5, "<?xml") == 0 && i + 5 < n &&
        (xml[i + 5] == ' ' || xml[i + 5] == '\t' || xml[i + 5] == '\n' || xml[i + 5] == '\r')) {
        std::size_t e = xml.find("?>", i + 5);
        if (e == std::string::npos) {
            error = "unterminated xml declaration";
            return true;
        }
        i = e + 2;
    }
    while (i < n) {
        if (xml.compare(i, 4, "<!--") == 0) {
            std::size_t e = xml.find("-->", i + 4);
            if (e == std::string::npos) {
                error = "unterminated comment";
                return true;
            }
            i = e + 3;
            continue;
        }
        if (xml.compare(i, 9, "<![CDATA[") == 0) {
            std::size_t e = xml.find("]]>", i + 9);
            if (e == std::string::npos) {
                error = "unterminated CDATA";
                return true;
            }
            i = e + 3;
            continue;
        }
        if (i + 2 <= n && xml[i] == '<' && xml[i + 1] == '?') {
            error = "processing instruction not allowed";
            return true;
        }
        ++i;
    }
    return false;
}
bool CheckDuplicateAttrs(const std::string& xml, std::string& error) {
    std::size_t n = xml.size();
    std::size_t i = 0;
    while (i < n) {
        if (xml[i] != '<') { ++i; continue; }
        if (i + 4 <= n && xml.compare(i, 4, "<!--") == 0) {
            std::size_t e = xml.find("-->", i + 4);
            if (e == std::string::npos) return true;
            i = e + 3;
            continue;
        }
        if (i + 9 <= n && xml.compare(i, 9, "<![CDATA[") == 0) {
            std::size_t e = xml.find("]]>", i + 9);
            if (e == std::string::npos) return true;
            i = e + 3;
            continue;
        }
        if (i + 2 <= n && xml[i + 1] == '?') {
            std::size_t e = xml.find("?>", i + 2);
            if (e == std::string::npos) return true;
            i = e + 2;
            continue;
        }
        if (i + 2 <= n && xml[i + 1] == '!') {
            std::size_t j = i + 2;
            char q = 0;
            while (j < n) {
                char c = xml[j];
                if (q != 0) {
                    if (c == q) q = 0;
                } else {
                    if (c == '"' || c == '\'') q = c;
                    else if (c == '>') break;
                }
                ++j;
            }
            if (j >= n) return true;
            i = j + 1;
            continue;
        }
        std::size_t j = i + 1;
        char q = 0;
        while (j < n) {
            char c = xml[j];
            if (q != 0) {
                if (c == q) q = 0;
            } else {
                if (c == '"' || c == '\'') q = c;
                else if (c == '>') break;
            }
            ++j;
        }
        if (j >= n) return true;
        std::string inner = xml.substr(i + 1, j - i - 1);
        i = j + 1;
        std::size_t m = inner.size();
        std::size_t p = 0;
        while (p < m && (inner[p] == ' ' || inner[p] == '\t' || inner[p] == '\n' || inner[p] == '\r')) ++p;
        if (p < m && inner[p] == '/') continue;
        std::size_t ns = p;
        while (p < m && inner[p] != ' ' && inner[p] != '\t' && inner[p] != '\n' && inner[p] != '\r' && inner[p] != '/' && inner[p] != '>') ++p;
        std::string tag = inner.substr(ns, p - ns);
        if (tag.empty()) continue;
        if (!tag.empty() && tag[0] == '/') continue;
        std::vector<std::string> names;
        while (p < m) {
            while (p < m && (inner[p] == ' ' || inner[p] == '\t' || inner[p] == '\n' || inner[p] == '\r')) ++p;
            if (p >= m) break;
            if (inner[p] == '/') { ++p; continue; }
            std::size_t an_s = p;
            while (p < m && inner[p] != '=' && inner[p] != ' ' && inner[p] != '\t' && inner[p] != '\n' && inner[p] != '\r' && inner[p] != '/' && inner[p] != '>') ++p;
            std::string aname = inner.substr(an_s, p - an_s);
            if (aname.empty()) { ++p; continue; }
            std::size_t t = p;
            while (t < m && (inner[t] == ' ' || inner[t] == '\t' || inner[t] == '\n' || inner[t] == '\r')) ++t;
            if (t >= m || inner[t] != '=') continue;
            p = t + 1;
            while (p < m && (inner[p] == ' ' || inner[p] == '\t' || inner[p] == '\n' || inner[p] == '\r')) ++p;
            if (p >= m) break;
            char qq = inner[p];
            if (qq != '"' && qq != '\'') break;
            ++p;
            while (p < m && inner[p] != qq) ++p;
            if (p < m) ++p;
            for (const auto& e : names) {
                if (e == aname) {
                    error = "duplicate attribute '" + aname + "' in element '" + tag + "'";
                    return false;
                }
            }
            names.push_back(aname);
        }
    }
    return true;
}
// Splits on '/' and reports a "." or ".." segment. A name such as "a..b" is
// fine; only whole segments are traversal.
bool HasDotSegment(const std::string& path) {
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        std::size_t len = end - start;
        if (len == 1 && path[start] == '.') return true;
        if (len == 2 && path[start] == '.' && path[start + 1] == '.') return true;
        start = end + 1;
    }
    return false;
}
// Riivolution resolves disc paths component by component, so "/a//b" and
// "/Stage/" name the same thing as "/a/b" and "/Stage". Runs of '/' are
// collapsed and a trailing one dropped (the root stays "/").
std::string TidyDiscPath(std::string path) {
    std::string out;
    out.reserve(path.size());
    for (char c : path) {
        if (c == '/' && !out.empty() && out.back() == '/') continue;
        out += c;
    }
    if (out.size() > 1 && out.back() == '/') out.pop_back();
    return out;
}
// Structural rules for a disc path or bare name. `is_name` reports the bare
// form. When `full` is false (parse time) a string carrying placeholders is
// only checked for length and control characters; the rest waits until the
// planner has substituted them.
bool CheckDiscPath(const std::string& disc, bool allow_empty, bool full, bool& is_name,
                   std::string& error) {
    is_name = false;
    if (disc.size() > kMaxString) { error = "disc path too long"; return false; }
    if (HasControlChars(disc)) { error = "invalid disc path"; return false; }
    if (disc.empty()) {
        if (allow_empty) return true;
        error = "disc path empty";
        return false;
    }
    if (!full && HasPlaceholder(disc)) return true;
    if (HasBadPathChars(disc)) { error = "invalid disc path '" + disc + "'"; return false; }
    if (disc[0] == '/') {
        if (disc.find("//") != std::string::npos) { error = "disc path has empty segment '" + disc + "'"; return false; }
        if (disc.size() > 1 && disc.back() == '/') { error = "disc path must not end with '/' ('" + disc + "')"; return false; }
        if (HasDotSegment(disc)) { error = "disc path traversal not allowed '" + disc + "'"; return false; }
        if (disc.size() == 1 && !allow_empty) { error = "disc path names the root"; return false; }
        return true;
    }
    if (disc.find('/') != std::string::npos) {
        error = "disc path must be absolute or a bare name '" + disc + "'";
        return false;
    }
    if (disc == "." || disc == "..") { error = "invalid disc name '" + disc + "'"; return false; }
    is_name = true;
    return true;
}
struct Ctx {
    Package& pkg;
    std::size_t nodes = 1;
    struct PendingMacro {
        std::string section;
        std::string name;
        std::string id;
        std::vector<Param> params;
    };
    std::vector<PendingMacro> macros;
    // Set when a resource limit (depth, node budget, string length) is hit:
    // those stop the parse. Anything else malformed only drops the element
    // it is in (see Recover), as Riivolution drops what it cannot read.
    bool fatal = false;
    // A second <patch> with an id already defined is kept under a hidden
    // alias; every reference to the id then selects all of them.
    std::map<std::string, std::vector<std::string>> aliases;
};
void Warn(Ctx& ctx, const std::string& message) {
    if (ctx.pkg.warnings.size() < kMaxWarnings) {
        ctx.pkg.warnings.push_back(message);
    } else if (ctx.pkg.warnings.size() == kMaxWarnings) {
        ctx.pkg.warnings.push_back("further warnings suppressed");
    }
}
// Riivolution reads every attribute through one accessor that drops the
// value when it is the empty string, so `foo=""` behaves exactly like an
// omitted `foo` for the whole format: optional attributes keep their
// default, inherited ones keep the inherited value, and required ones are
// reported as missing. Matching that here keeps XMLs written against
// Riivolution loadable instead of failing on a per-attribute check.
// A malformed element is dropped with a warning naming it and the reason;
// the rest of the package still loads. Returns false (stop) only after a
// resource limit.
bool Recover(Ctx& ctx, const std::string& what, std::string& error) {
    if (ctx.fatal) return false;
    Warn(ctx, "ignoring " + what + ": " + error);
    error.clear();
    return true;
}
bool AttrPresent(pugi::xml_node node, const char* name) {
    pugi::xml_attribute a = node.attribute(name);
    return a && a.as_string()[0] != '\0';
}
std::string AttrValue(pugi::xml_node node, const char* name) {
    return std::string(node.attribute(name).as_string());
}
bool CheckAttrLengths(pugi::xml_node node, const std::string& label, std::string& error) {
    for (auto a : node.attributes()) {
        std::string an = a.name();
        // Memory hex payloads are budgeted by character count in ReadHex and
        // by byte count in ParseMemoryNode, not by the name/path cap.
        if (label == "memory" && (an == "value" || an == "original")) continue;
        std::string v = a.as_string();
        if (v.size() > kMaxString) {
            error = std::string("string too long in attribute '") + a.name() + "'";
            return false;
        }
        if (an.size() > kMaxString) {
            error = "attribute name too long";
            return false;
        }
    }
    return true;
}
bool CheckNoText(pugi::xml_node node, std::string& error) {
    for (auto c : node.children()) {
        auto t = c.type();
        if (t == pugi::node_pcdata || t == pugi::node_cdata) {
            std::string v = c.value();
            if (v.size() > kMaxString) {
                error = "string too long";
                return false;
            }
            if (!IsWhitespaceOnly(c.value())) {
                error = std::string("unexpected text in element '") + node.name() + "'";
                return false;
            }
        } else if (t == pugi::node_pi || t == pugi::node_doctype || t == pugi::node_declaration) {
            error = "processing instruction or doctype not allowed";
            return false;
        }
    }
    return true;
}
// Common entry checks for every element: depth, node budget, attribute
// lengths, no text content, and a warning for each attribute outside
// `allowed` (nullptr-terminated). Unknown attributes are ignored, not fatal.
bool EnterElement(pugi::xml_node n, Ctx& ctx, int depth, const char* const* allowed,
                  const std::string& label, std::string& error) {
    if (depth > kMaxDepth) { error = "depth exceeded"; ctx.fatal = true; return false; }
    if (++ctx.nodes > kMaxNodes) { error = "too many nodes"; ctx.fatal = true; return false; }
    if (!CheckAttrLengths(n, label, error)) { ctx.fatal = true; return false; }
    {
        std::string text_error;
        if (!CheckNoText(n, text_error)) {
            if (text_error == "string too long") { error = text_error; ctx.fatal = true; return false; }
            Warn(ctx, label + ": " + text_error + " ignored");
        }
    }
    for (auto a : n.attributes()) {
        std::string an = a.name();
        bool ok = false;
        for (const char* const* p = allowed; *p != nullptr; ++p) {
            if (an == *p) { ok = true; break; }
        }
        if (!ok) Warn(ctx, label + ": ignoring unknown attribute '" + an + "'");
    }
    return true;
}
void WarnUnknownChild(Ctx& ctx, const std::string& label, pugi::xml_node child) {
    Warn(ctx, label + ": ignoring unsupported element '" + child.name() + "'");
}
bool ReadBool(pugi::xml_node n, const char* name, const std::string& label, bool& out, std::string& error) {
    if (!AttrPresent(n, name)) return true;
    std::string v = AttrValue(n, name);
    if (v.empty()) { error = label + " " + name + " empty"; return false; }
    if (!ParseBoolStrict(v, out)) { error = "invalid " + label + " " + name + " '" + v + "'"; return false; }
    return true;
}
bool ReadU64(pugi::xml_node n, const char* name, const std::string& label, std::uint64_t& out, std::string& error) {
    if (!AttrPresent(n, name)) return true;
    std::string v = AttrValue(n, name);
    if (v.empty()) { error = label + " " + name + " empty"; return false; }
    if (!ParseU64(v, out)) { error = "invalid " + label + " " + name + " '" + v + "'"; return false; }
    return true;
}
bool ReadHex(pugi::xml_node n, const char* name, const std::string& label, std::vector<std::uint8_t>& out, std::string& error) {
    if (!AttrPresent(n, name)) return true;
    std::string v = AttrValue(n, name);
    if (v.empty()) { error = label + " " + name + " empty"; return false; }
    if (v.size() > kMaxHexChars) { error = label + " " + name + " too long"; return false; }
    if (!ParseHex(v, out)) { error = "invalid " + label + " " + name + " (hex bytes expected)"; return false; }
    return true;
}
// External-style path attributes (external, valuefile): checked for length
// and control characters only; placeholders are substituted and the path is
// resolved by the planner.
bool ReadExternal(pugi::xml_node n, const char* name, const std::string& label, bool required,
                  std::string& out, std::string& error) {
    if (!AttrPresent(n, name)) {
        if (required) { error = label + " missing " + name; return false; }
        return true;
    }
    std::string v = AttrValue(n, name);
    if (v.empty()) { error = label + " " + name + " empty"; return false; }
    if (v.size() > kMaxString) { error = "string too long in " + label + " " + name; return false; }
    if (HasControlChars(v)) { error = "invalid " + label + " " + name; return false; }
    out = v;
    return true;
}
bool ParseFileNode(pugi::xml_node n, Patch& patch, Ctx& ctx, int depth, std::string& error) {
    static const char* const allowed[] = {"disc", "external", "resize", "create", "offset", "length", "fileoffset", nullptr};
    if (!EnterElement(n, ctx, depth, allowed, "file", error)) return false;
    for (auto c : n.children()) {
        if (c.type() == pugi::node_element) WarnUnknownChild(ctx, "file", c);
    }
    if (!AttrPresent(n, "disc")) { error = "file missing disc"; return false; }
    FilePatch f;
    f.disc = TidyDiscPath(AttrValue(n, "disc"));
    {
        std::string perr;
        if (!CheckDiscPath(f.disc, false, false, f.is_filename, perr)) { error = "file " + perr; return false; }
    }
    if (!ReadExternal(n, "external", "file", true, f.external, error)) return false;
    if (!ReadBool(n, "resize", "file", f.resize, error)) return false;
    if (!ReadBool(n, "create", "file", f.create, error)) return false;
    if (!ReadU64(n, "offset", "file", f.offset, error)) return false;
    if (!ReadU64(n, "length", "file", f.length, error)) return false;
    if (!ReadU64(n, "fileoffset", "file", f.file_offset, error)) return false;
    patch.files.push_back(f);
    patch.order.push_back(PatchStep{PatchKind::File, patch.files.size() - 1});
    return true;
}
bool ParseFolderNode(pugi::xml_node n, Patch& patch, Ctx& ctx, int depth, std::string& error) {
    static const char* const allowed[] = {"disc", "external", "resize", "create", "recursive", "length", nullptr};
    if (!EnterElement(n, ctx, depth, allowed, "folder", error)) return false;
    for (auto c : n.children()) {
        if (c.type() == pugi::node_element) WarnUnknownChild(ctx, "folder", c);
    }
    FolderPatch f;
    f.disc = TidyDiscPath(AttrValue(n, "disc"));
    {
        std::string perr;
        if (!CheckDiscPath(f.disc, true, false, f.is_name, perr)) { error = "folder " + perr; return false; }
    }
    if (!ReadExternal(n, "external", "folder", true, f.external, error)) return false;
    if (!ReadBool(n, "resize", "folder", f.resize, error)) return false;
    if (!ReadBool(n, "create", "folder", f.create, error)) return false;
    if (!ReadBool(n, "recursive", "folder", f.recursive, error)) return false;
    if (!ReadU64(n, "length", "folder", f.length, error)) return false;
    patch.folders.push_back(f);
    patch.order.push_back(PatchStep{PatchKind::Folder, patch.folders.size() - 1});
    return true;
}
bool ParseMemoryNode(pugi::xml_node n, Patch& patch, Ctx& ctx, int depth, std::string& error) {
    static const char* const allowed[] = {"offset", "value", "valuefile", "original", "ocarina", "search", "align", nullptr};
    if (!EnterElement(n, ctx, depth, allowed, "memory", error)) return false;
    for (auto c : n.children()) {
        if (c.type() == pugi::node_element) WarnUnknownChild(ctx, "memory", c);
    }
    MemoryPatch m;
    m.has_offset = AttrPresent(n, "offset");
    if (!ReadU64(n, "offset", "memory", m.offset, error)) return false;
    if (!ReadHex(n, "value", "memory", m.value, error)) return false;
    if (!ReadExternal(n, "valuefile", "memory", false, m.valuefile, error)) return false;
    if (!ReadHex(n, "original", "memory", m.original, error)) return false;
    if (!ReadBool(n, "ocarina", "memory", m.ocarina, error)) return false;
    if (!ReadBool(n, "search", "memory", m.search, error)) return false;
    if (!ReadU64(n, "align", "memory", m.align, error)) return false;
    const bool has_value = !m.value.empty();
    const bool has_file = !m.valuefile.empty();
    if (has_value == has_file) {
        error = has_value ? "memory has both value and valuefile" : "memory needs value or valuefile";
        return false;
    }
    if (m.ocarina && m.search) { error = "memory cannot be both ocarina and search"; return false; }
    if (m.search) {
        if (m.original.empty()) { error = "memory search needs original"; return false; }
        if (has_value && m.value.size() != m.original.size()) {
            error = "memory search value and original differ in length";
            return false;
        }
    } else if (!m.has_offset) {
        error = "memory missing offset";
        return false;
    }
    if (m.ocarina && !has_value) { error = "memory ocarina needs value"; return false; }
    if (m.value.size() > kMaxMemoryValueBytes || m.original.size() > kMaxMemoryValueBytes) {
        error = "memory value too large";
        return false;
    }
    if (m.align == 0) { error = "memory align must be at least 1"; return false; }
    patch.memory.push_back(m);
    patch.order.push_back(PatchStep{PatchKind::Memory, patch.memory.size() - 1});
    return true;
}
bool ParseSavegameNode(pugi::xml_node n, Patch& patch, Ctx& ctx, int depth, std::string& error) {
    static const char* const allowed[] = {"external", "clone", nullptr};
    if (!EnterElement(n, ctx, depth, allowed, "savegame", error)) return false;
    for (auto c : n.children()) {
        if (c.type() == pugi::node_element) WarnUnknownChild(ctx, "savegame", c);
    }
    SavegamePatch s;
    if (!ReadExternal(n, "external", "savegame", true, s.external, error)) return false;
    if (!ReadBool(n, "clone", "savegame", s.clone, error)) return false;
    patch.savegames.push_back(s);
    patch.order.push_back(PatchStep{PatchKind::Savegame, patch.savegames.size() - 1});
    return true;
}
bool ParseNetworkNode(pugi::xml_node n, Ctx& ctx, int depth, std::string& error) {
    static const char* const allowed[] = {"protocol", "address", "port", "log", nullptr};
    if (!EnterElement(n, ctx, depth, allowed, "network", error)) return false;
    for (auto c : n.children()) {
        if (c.type() == pugi::node_element) WarnUnknownChild(ctx, "network", c);
    }
    const std::string protocol = AttrPresent(n, "protocol") ? AttrValue(n, "protocol") : "riifs";
    if (protocol != "riifs") {
        Warn(ctx, "network: ignoring protocol '" + protocol + "' (only riifs is supported)");
        return true;
    }
    NetworkServer s;
    s.address = AttrValue(n, "address");
    std::uint64_t port = s.port;
    if (!ReadU64(n, "port", "network", port, error)) return false;
    if (port == 0 || port > 65535) {
        error = "invalid network port '" + AttrValue(n, "port") + "'";
        return false;
    }
    s.port = static_cast<std::uint16_t>(port);
    ctx.pkg.networks.push_back(s);
    return true;
}
bool ParsePatchDef(pugi::xml_node n, Ctx& ctx, int depth, std::string& error) {
    static const char* const allowed[] = {"id", "root", nullptr};
    if (!EnterElement(n, ctx, depth, allowed, "patch", error)) return false;
    if (!AttrPresent(n, "id")) { error = "patch missing id"; return false; }
    std::string id = AttrValue(n, "id");
    if (id.empty()) { error = "patch id empty"; return false; }
    std::string key = id;
    if (ctx.pkg.patches.find(id) != ctx.pkg.patches.end()) {
        std::vector<std::string>& more = ctx.aliases[id];
        key = id + '\x1f' + std::to_string(more.size() + 1);
        more.push_back(key);
        Warn(ctx, "patch id '" + id + "' is defined more than once; references select every definition");
    }
    Patch p;
    if (AttrPresent(n, "root")) {
        std::string r = AttrValue(n, "root");
        if (HasControlChars(r)) { error = "invalid patch root"; return false; }
        p.root = r;
    }
    const std::string label = "patch '" + id + "'";
    for (auto c : n.children()) {
        if (c.type() != pugi::node_element) continue;
        std::string cn = c.name();
        bool ok = true;
        if (cn == "file") {
            ok = ParseFileNode(c, p, ctx, depth + 1, error);
        } else if (cn == "folder") {
            ok = ParseFolderNode(c, p, ctx, depth + 1, error);
        } else if (cn == "memory") {
            ok = ParseMemoryNode(c, p, ctx, depth + 1, error);
        } else if (cn == "savegame") {
            ok = ParseSavegameNode(c, p, ctx, depth + 1, error);
        } else {
            WarnUnknownChild(ctx, label, c);
        }
        if (!ok && !Recover(ctx, label + " <" + cn + ">", error)) return false;
    }
    ctx.pkg.patches[key] = p;
    return true;
}
bool ParseParam(pugi::xml_node n, std::vector<Param>& out, Ctx& ctx, int depth, std::string& error) {
    static const char* const allowed[] = {"name", "value", nullptr};
    if (!EnterElement(n, ctx, depth, allowed, "param", error)) return false;
    for (auto c : n.children()) {
        if (c.type() == pugi::node_element) WarnUnknownChild(ctx, "param", c);
    }
    if (!AttrPresent(n, "name")) { error = "param missing name"; return false; }
    Param p;
    p.name = AttrValue(n, "name");
    if (p.name.empty()) { error = "param name empty"; return false; }
    p.value = AttrValue(n, "value");
    if (HasControlChars(p.name) || HasControlChars(p.value)) { error = "invalid param"; return false; }
    out.push_back(p);
    return true;
}
bool ParsePatchRef(pugi::xml_node n, Choice& ch, Ctx& ctx, int depth, std::string& error) {
    static const char* const allowed[] = {"id", nullptr};
    if (!EnterElement(n, ctx, depth, allowed, "patch reference", error)) return false;
    for (auto c : n.children()) {
        if (c.type() == pugi::node_element) WarnUnknownChild(ctx, "patch reference", c);
    }
    if (!AttrPresent(n, "id")) { error = "patch reference missing id"; return false; }
    std::string id = AttrValue(n, "id");
    if (id.empty()) { error = "patch reference id empty"; return false; }
    ch.patches.push_back(id);
    return true;
}
bool ParseChoice(pugi::xml_node n, Option& opt, Ctx& ctx, int depth, std::string& error) {
    static const char* const allowed[] = {"name", nullptr};
    if (!EnterElement(n, ctx, depth, allowed, "choice", error)) return false;
    if (!AttrPresent(n, "name")) { error = "choice missing name"; return false; }
    Choice ch;
    ch.name = AttrValue(n, "name");
    if (ch.name.empty()) { error = "choice name empty"; return false; }
    for (auto c : n.children()) {
        if (c.type() != pugi::node_element) continue;
        std::string cn = c.name();
        if (cn == "patch") {
            if (!ParsePatchRef(c, ch, ctx, depth + 1, error) &&
                !Recover(ctx, "a patch reference in choice '" + ch.name + "'", error)) {
                return false;
            }
        } else if (cn == "param") {
            if (!ParseParam(c, ch.params, ctx, depth + 1, error) &&
                !Recover(ctx, "a param in choice '" + ch.name + "'", error)) {
                return false;
            }
        } else {
            WarnUnknownChild(ctx, "choice '" + ch.name + "'", c);
        }
    }
    opt.choices.push_back(ch);
    return true;
}
bool ParseOption(pugi::xml_node n, const std::string& section, Ctx& ctx, int depth, std::string& error) {
    static const char* const allowed[] = {"name", "id", "default", nullptr};
    if (!EnterElement(n, ctx, depth, allowed, "option", error)) return false;
    if (!AttrPresent(n, "name")) { error = "option missing name"; return false; }
    Option opt;
    opt.section = section;
    opt.name = AttrValue(n, "name");
    if (opt.name.empty()) { error = "option name empty"; return false; }
    if (AttrPresent(n, "id")) {
        opt.id = AttrValue(n, "id");
        if (opt.id.empty()) { error = "option id empty"; return false; }
    }
    for (auto c : n.children()) {
        if (c.type() != pugi::node_element) continue;
        std::string cn = c.name();
        if (cn == "choice") {
            if (!ParseChoice(c, opt, ctx, depth + 1, error) &&
                !Recover(ctx, "a choice of option '" + opt.name + "'", error)) {
                return false;
            }
        } else if (cn == "param") {
            if (!ParseParam(c, opt.params, ctx, depth + 1, error) &&
                !Recover(ctx, "a param of option '" + opt.name + "'", error)) {
                return false;
            }
        } else {
            WarnUnknownChild(ctx, "option '" + opt.name + "'", c);
        }
    }
    if (AttrPresent(n, "default")) {
        std::string defStr = AttrValue(n, "default");
        std::uint64_t u = 0;
        if (!ParseU64(defStr, u)) {
            Warn(ctx, "option '" + opt.name + "': default '" + defStr + "' is not a number, treating as disabled");
            u = 0;
        }
        if (u > static_cast<std::uint64_t>(opt.choices.size())) {
            // Documented behaviour is "0 disables"; an index past the last
            // choice cannot select anything, so treat it the same way.
            Warn(ctx, "option '" + opt.name + "': default " + defStr + " is out of range, treating as disabled");
            u = 0;
        }
        opt.selected = static_cast<std::size_t>(u);
    }
    ctx.pkg.options.push_back(opt);
    return true;
}
bool ParseMacro(pugi::xml_node n, const std::string& section, Ctx& ctx, int depth, std::string& error) {
    static const char* const allowed[] = {"name", "id", nullptr};
    if (!EnterElement(n, ctx, depth, allowed, "macro", error)) return false;
    if (!AttrPresent(n, "name")) { error = "macro missing name"; return false; }
    if (!AttrPresent(n, "id")) { error = "macro missing id"; return false; }
    Ctx::PendingMacro m;
    m.section = section;
    m.name = AttrValue(n, "name");
    m.id = AttrValue(n, "id");
    if (m.name.empty()) { error = "macro name empty"; return false; }
    if (m.id.empty()) { error = "macro id empty"; return false; }
    for (auto c : n.children()) {
        if (c.type() != pugi::node_element) continue;
        std::string cn = c.name();
        if (cn == "param") {
            if (!ParseParam(c, m.params, ctx, depth + 1, error)) return false;
        } else {
            WarnUnknownChild(ctx, "macro '" + m.name + "'", c);
        }
    }
    ctx.macros.push_back(m);
    return true;
}
bool ParseSection(pugi::xml_node n, Ctx& ctx, int depth, std::string& error) {
    static const char* const allowed[] = {"name", nullptr};
    if (!EnterElement(n, ctx, depth, allowed, "section", error)) return false;
    if (!AttrPresent(n, "name")) { error = "section missing name"; return false; }
    std::string nm = AttrValue(n, "name");
    if (nm.empty()) { error = "section name empty"; return false; }
    for (auto c : n.children()) {
        if (c.type() != pugi::node_element) continue;
        std::string cn = c.name();
        if (cn == "option") {
            if (!ParseOption(c, nm, ctx, depth + 1, error) && !Recover(ctx, "an option in section '" + nm + "'", error)) {
                return false;
            }
        } else if (cn == "macro") {
            if (!ParseMacro(c, nm, ctx, depth + 1, error) && !Recover(ctx, "a macro in section '" + nm + "'", error)) {
                return false;
            }
        } else {
            WarnUnknownChild(ctx, "section '" + nm + "'", c);
        }
    }
    return true;
}
bool ParseOptions(pugi::xml_node n, Ctx& ctx, int depth, std::string& error) {
    static const char* const allowed[] = {nullptr};
    if (!EnterElement(n, ctx, depth, allowed, "options", error)) return false;
    for (auto c : n.children()) {
        if (c.type() != pugi::node_element) continue;
        std::string cn = c.name();
        if (cn == "section") {
            if (!ParseSection(c, ctx, depth + 1, error) && !Recover(ctx, "a section", error)) return false;
        } else if (cn == "macro") {
            if (!ParseMacro(c, "", ctx, depth + 1, error) && !Recover(ctx, "a macro", error)) return false;
        } else {
            WarnUnknownChild(ctx, "options", c);
        }
    }
    return true;
}
// A macro clones the option carrying its id under a new name; the macro's
// params are appended to every cloned choice so they override the original
// choice params during substitution. The clone joins the macro's section
// (or the original's, for a macro placed directly under <options>).
bool ExpandMacros(Ctx& ctx, std::string& error) {
    for (const auto& m : ctx.macros) {
        const Option* source = nullptr;
        for (const auto& o : ctx.pkg.options) {
            if (!o.id.empty() && o.id == m.id) { source = &o; break; }
        }
        if (source == nullptr) {
            Warn(ctx, "ignoring macro '" + m.name + "': it references unknown option id '" + m.id + "'");
            continue;
        }
        Option clone = *source;
        clone.name = m.name;
        if (!m.section.empty()) clone.section = m.section;
        for (auto& ch : clone.choices) {
            ch.params.insert(ch.params.end(), m.params.begin(), m.params.end());
        }
        ctx.pkg.options.push_back(clone);
        if (ctx.pkg.options.size() > kMaxNodes) { error = "too many options"; return false; }
    }
    return true;
}
bool ParseRegionNode(pugi::xml_node c, DiscFilter& filter, Ctx& ctx, int depth, std::string& error) {
    // <region type="E"/> appears nested in <id> (Newer) or beside it under
    // <wiidisc> (Spectral); both spellings feed the same filter.
    static const char* const region_allowed[] = {"type", nullptr};
    if (!EnterElement(c, ctx, depth, region_allowed, "region", error)) return false;
    for (auto gc : c.children()) {
        if (gc.type() == pugi::node_element) WarnUnknownChild(ctx, "region", gc);
    }
    // A region Riftwii cannot read is ignored with a warning (it then
    // does not narrow the filter), like any other malformed element.
    if (!AttrPresent(c, "type")) {
        Warn(ctx, "ignoring <region> without a type");
        return true;
    }
    std::string t = AttrValue(c, "type");
    if (t.size() != 1 || t[0] < 'A' || t[0] > 'Z') {
        Warn(ctx, "ignoring <region type=\"" + t + "\">: a region is one letter (the game id's fourth character)");
        return true;
    }
    filter.regions.push_back(t);
    return true;
}
bool ParseId(pugi::xml_node n, DiscFilter& filter, Ctx& ctx, int depth, std::string& error) {
    static const char* const allowed[] = {"game", "developer", "disc", "revision", "version", nullptr};
    if (!EnterElement(n, ctx, depth, allowed, "id", error)) return false;
    // `game` is a prefix of the six-character game id: "RMC" for every
    // region, "RMCP" or even "RMCP01" for one. An attribute that cannot be
    // read is ignored with a warning rather than refusing the package; for
    // `game` that would widen the filter to every disc, so it stays fatal.
    if (AttrPresent(n, "game")) {
        std::string g = AttrValue(n, "game");
        if (g.size() > 6) { error = "id game '" + g + "' is longer than a game id"; return false; }
        if (!IsAlnumUpper(g)) { error = "invalid id game '" + g + "'"; return false; }
        filter.game = g;
    }
    if (AttrPresent(n, "developer")) {
        std::string d = AttrValue(n, "developer");
        if (d.size() != 2 || !IsAlnumUpper(d)) {
            Warn(ctx, "ignoring id developer '" + d + "': a developer is two characters");
        } else {
            filter.developer = d;
        }
    }
    if (AttrPresent(n, "disc")) {
        std::string v = AttrValue(n, "disc");
        std::uint64_t u = 0;
        if (!ParseU64(v, u) || u > 255) {
            Warn(ctx, "ignoring id disc '" + v + "'");
        } else {
            filter.number = static_cast<int>(u);
        }
    }
    // `revision` and its older spelling `version`; revision wins.
    for (const char* name : {"version", "revision"}) {
        if (!AttrPresent(n, name)) continue;
        std::string v = AttrValue(n, name);
        std::uint64_t u = 0;
        if (!ParseU64(v, u) || u > 255) {
            Warn(ctx, std::string("ignoring id ") + name + " '" + v + "'");
            continue;
        }
        if (filter.revision >= 0 && filter.revision != static_cast<int>(u)) {
            Warn(ctx, "id revision and version differ; using revision");
        }
        filter.revision = static_cast<int>(u);
    }
    for (auto c : n.children()) {
        if (c.type() != pugi::node_element) continue;
        std::string cn = c.name();
        if (cn != "region") {
            WarnUnknownChild(ctx, "id", c);
            continue;
        }
        if (!ParseRegionNode(c, filter, ctx, depth + 1, error)) return false;
    }
    return true;
}
}  // namespace
bool DiscFilter::matches(const DiscIdentity& disc) const {
    if (disc.id.size() != 6) return false;
    for (char ch : disc.id) {
        bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
        if (!ok) return false;
    }
    if (!game.empty()) {
        if (game.size() > 6) return false;
        if (disc.id.compare(0, game.size(), game) != 0) return false;
    }
    if (!developer.empty()) {
        if (developer.size() != 2) return false;
        if (disc.id.compare(4, 2, developer) != 0) return false;
    }
    if (!regions.empty() && std::find(regions.begin(), regions.end(), disc.id.substr(3, 1)) == regions.end()) return false;
    if (revision < -1 || revision > 255 || number < -1 || number > 255) return false;
    if (revision != -1) {
        if (disc.revision != static_cast<std::uint8_t>(revision)) return false;
    }
    if (number != -1) {
        if (disc.number != static_cast<std::uint8_t>(number)) return false;
    }
    return true;
}
bool resolve_path(const std::string& root, const std::string& path, std::string& output) {
    try {
        std::string tmp;
        if (root.find('\0') != std::string::npos || path.find('\0') != std::string::npos) return false;
        if (root.empty() || path.empty()) return false;
        if (root.size() > kMaxString || path.size() > kMaxString) return false;
        if (root[0] != '/') return false;
        if (HasBadPathChars(root) || HasBadPathChars(path)) return false;
        std::string combined;
        if (!path.empty() && path[0] == '/') {
            combined = path;
        } else {
            combined = root;
            if (!combined.empty() && combined.back() != '/') combined += '/';
            combined += path;
        }
        if (combined.size() > kMaxString * 2) return false;
        if (HasBadPathChars(combined)) return false;
        std::vector<std::string> parts;
        std::string cur;
        for (std::size_t i = 0; i <= combined.size(); ++i) {
            char c = (i < combined.size()) ? combined[i] : '/';
            if (c == '/') {
                if (!cur.empty()) {
                    if (cur == ".") {
                    } else if (cur == "..") {
                        if (parts.empty()) return false;
                        parts.pop_back();
                    } else {
                        parts.push_back(cur);
                    }
                    cur.clear();
                }
            } else {
                cur += c;
            }
        }
        if (parts.empty()) {
            tmp = "/";
        } else {
            tmp = "";
            for (auto& p : parts) {
                tmp += '/';
                tmp += p;
            }
        }
        if (tmp.size() > kMaxString) return false;
        output = tmp;
        return true;
    } catch (...) {
        return false;
    }
}
bool substitute_params(const std::string& input, const std::vector<Param>& params,
                       const DiscIdentity& disc, std::string& output, std::string& error) {
    try {
        std::string out;
        const std::size_t n = input.size();
        std::size_t i = 0;
        while (i < n) {
            if (input[i] == '{' && i + 1 < n && input[i + 1] == '$') {
                std::size_t close = input.find('}', i + 2);
                if (close == std::string::npos) {
                    error = "unterminated placeholder in '" + input + "'";
                    return false;
                }
                std::string name = input.substr(i + 2, close - (i + 2));
                if (name.empty()) {
                    error = "empty placeholder in '" + input + "'";
                    return false;
                }
                std::string value;
                bool found = false;
                if (name == "__gameid" || name == "__region" || name == "__maker") {
                    if (disc.id.size() != 6) {
                        error = "disc id required for {$" + name + "}";
                        return false;
                    }
                    if (name == "__gameid") value = disc.id.substr(0, 3);
                    else if (name == "__region") value = disc.id.substr(3, 1);
                    else value = disc.id.substr(4, 2);
                    found = true;
                } else {
                    for (std::size_t k = params.size(); k > 0; --k) {
                        if (params[k - 1].name == name) {
                            value = params[k - 1].value;
                            found = true;
                            break;
                        }
                    }
                }
                if (!found) {
                    error = "unknown parameter '" + name + "' in '" + input + "'";
                    return false;
                }
                out += value;
                i = close + 1;
            } else {
                out += input[i];
                ++i;
            }
            if (out.size() > kMaxString) {
                error = "substituted path too long";
                return false;
            }
        }
        output = out;
        return true;
    } catch (...) {
        error = "allocation failure";
        return false;
    }
}
bool parse_package(const std::string& xml, Package& output, std::string& error) {
    try {
        if (xml.find('\0') != std::string::npos) {
            error = "embedded null not allowed";
            return false;
        }
        if (xml.size() > kMaxXml) {
            error = "xml too large";
            return false;
        }
        if (HasDoctype(xml)) {
            error = "doctype not allowed";
            return false;
        }
        {
            std::string piErr;
            if (HasForbiddenPi(xml, piErr)) {
                error = piErr;
                return false;
            }
        }
        // pugixml keeps every copy of a repeated attribute and reads the
        // first, as Riivolution's reader does; say so rather than refuse.
        std::string duplicate_attr;
        if (!CheckDuplicateAttrs(xml, duplicate_attr)) duplicate_attr += " (the first is used)";
        else duplicate_attr.clear();
        pugi::xml_document doc;
        pugi::xml_parse_result res = doc.load_buffer(xml.data(), xml.size(), pugi::parse_full);
        if (!res) {
            error = std::string("xml parse error: ") + res.description();
            return false;
        }
        bool seenDecl = false;
        bool seenElement = false;
        for (auto c : doc.children()) {
            auto t = c.type();
            if (t == pugi::node_declaration) {
                if (seenDecl || seenElement) {
                    error = "xml declaration must come first";
                    return false;
                }
                seenDecl = true;
                continue;
            }
            if (t == pugi::node_pi || t == pugi::node_doctype) {
                error = "processing instruction or doctype not allowed";
                return false;
            }
            if (t == pugi::node_pcdata || t == pugi::node_cdata) {
                if (!IsWhitespaceOnly(c.value())) {
                    error = "unexpected text at top level";
                    return false;
                }
            }
            if (t == pugi::node_element) seenElement = true;
        }
        std::vector<pugi::xml_node> roots;
        for (auto c : doc.children()) {
            if (c.type() == pugi::node_element) roots.push_back(c);
        }
        if (roots.empty()) {
            error = "missing root";
            return false;
        }
        if (roots.size() > 1) {
            error = "multiple roots not allowed";
            return false;
        }
        pugi::xml_node r = roots[0];
        std::string rname = r.name();
        if (rname != "wiidisc") {
            error = "unsupported root '" + rname + "'";
            return false;
        }
        Package tmp;
        Ctx ctx{tmp, 1, {}, false, {}};
        if (!duplicate_attr.empty()) Warn(ctx, duplicate_attr);
        static const char* const root_allowed[] = {"version", "root", "shiftfiles", "log", nullptr};
        if (!EnterElement(r, ctx, 1, root_allowed, "wiidisc", error)) return false;
        if (!AttrPresent(r, "version")) {
            error = "wiidisc missing version";
            return false;
        }
        {
            std::string v = AttrValue(r, "version");
            if (v != "1") {
                error = "unsupported wiidisc version '" + v + "'";
                return false;
            }
        }
        // `root=""` is absent (see AttrPresent), so the /riivolution default
        // survives it.
        if (AttrPresent(r, "root")) {
            std::string rt = AttrValue(r, "root");
            if (rt[0] != '/') { error = "wiidisc root must be absolute"; return false; }
            if (HasBadPathChars(rt)) { error = "invalid wiidisc root"; return false; }
            tmp.root = rt;
        }
        if (!ReadBool(r, "shiftfiles", "wiidisc", tmp.shift_files, error)) return false;
        bool seenId = false;
        bool seenOptions = false;
        for (auto c : r.children()) {
            if (c.type() != pugi::node_element) continue;
            std::string cn = c.name();
            if (cn == "id") {
                // The first <id> is the filter; a later one is ignored.
                if (seenId) {
                    Warn(ctx, "ignoring a second <id>");
                    continue;
                }
                seenId = true;
                if (!ParseId(c, tmp.filter, ctx, 2, error)) return false;
            } else if (cn == "region") {
                if (!ParseRegionNode(c, tmp.filter, ctx, 2, error)) return false;
            } else if (cn == "options") {
                // A second <options> block adds its sections to the first.
                if (seenOptions) Warn(ctx, "more than one <options>; their sections are merged");
                seenOptions = true;
                if (!ParseOptions(c, ctx, 2, error)) return false;
            } else if (cn == "patch") {
                if (!ParsePatchDef(c, ctx, 2, error) && !Recover(ctx, "a <patch>", error)) return false;
            } else if (cn == "network") {
                if (!ParseNetworkNode(c, ctx, 2, error)) return false;
            } else {
                WarnUnknownChild(ctx, "wiidisc", c);
            }
        }
        if (!ExpandMacros(ctx, error)) return false;
        if (ctx.nodes > kMaxNodes) { error = "too many nodes"; return false; }
        // References: every definition of a repeated id, and none for an id
        // no <patch> defines (a warning, as Riivolution just skips it).
        for (auto& o : tmp.options) {
            for (auto& ch : o.choices) {
                std::vector<std::string> resolved;
                for (const auto& pid : ch.patches) {
                    if (tmp.patches.find(pid) == tmp.patches.end()) {
                        Warn(ctx, "choice '" + ch.name + "' of option '" + o.name + "' references undefined patch '" +
                                      pid + "'; ignored");
                        continue;
                    }
                    resolved.push_back(pid);
                    const auto more = ctx.aliases.find(pid);
                    if (more != ctx.aliases.end()) {
                        resolved.insert(resolved.end(), more->second.begin(), more->second.end());
                    }
                }
                ch.patches = std::move(resolved);
            }
        }
        output = tmp;
        error.clear();
        return true;
    } catch (const std::bad_alloc&) {
        error = "allocation failure";
        return false;
    } catch (const std::exception& e) {
        try { error = e.what(); } catch (...) { error = "allocation failure"; }
        return false;
    } catch (...) {
        error = "allocation failure";
        return false;
    }
}
bool read_package(std::istream& input, Package& output, std::string& error) {
    try {
        std::string xml;
        char chunk[4096];
        while (input.read(chunk, sizeof(chunk)) || input.gcount() > 0) {
            const auto count = static_cast<std::size_t>(input.gcount());
            if (count > kMaxXml - xml.size()) {
                error = "xml too large";
                return false;
            }
            xml.append(chunk, count);
        }
        if (input.bad() || !input.eof()) {
            error = "read error";
            return false;
        }
        return parse_package(xml, output, error);
    } catch (const std::bad_alloc&) {
        error = "allocation failure";
        return false;
    } catch (...) {
        error = "read error";
        return false;
    }
}
namespace {
struct Selection {
    const Option* option = nullptr;
    const Choice* choice = nullptr;
    const Patch* patch = nullptr;
    std::string patch_id;
    std::vector<Param> params;
};
std::string Where(const Selection& s) {
    return "option '" + s.option->name + "' choice '" + s.choice->name + "' patch '" + s.patch_id + "'";
}
// Substitutes placeholders and resolves an external-style path against
// `base`. `what` names the field for messages.
bool ResolveExternal(const std::string& raw, const std::string& base, const Selection& sel,
                     const DiscIdentity& disc, const char* what, std::string& out, std::string& error) {
    std::string substituted;
    if (!substitute_params(raw, sel.params, disc, substituted, error)) {
        error = Where(sel) + ": " + error;
        return false;
    }
    std::string resolved;
    if (!resolve_path(base, substituted, resolved)) {
        error = Where(sel) + ": invalid " + what + " path '" + substituted + "'";
        return false;
    }
    out = resolved;
    return true;
}
}  // namespace
namespace {

bool SameFolded(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

// Index of the single candidate whose name matches exactly, else the
// single one matching case-insensitively; npos for none or several.
template <typename Match>
std::size_t PickOne(std::size_t count, Match match) {
    for (int folded = 0; folded < 2; ++folded) {
        std::size_t found = std::string::npos;
        std::size_t hits = 0;
        for (std::size_t i = 0; i < count; ++i) {
            if (match(i, folded == 1)) {
                found = i;
                ++hits;
            }
        }
        if (hits == 1) return found;
        if (hits > 1) return std::string::npos;
    }
    return std::string::npos;
}

}  // namespace

bool select_choice(Package& package, const std::string& option, const std::string& choice, std::string& error) {
    if (option.empty()) {
        error = "option name is empty";
        return false;
    }
    // "Section/Option" may split at any '/' (a section name can hold one);
    // the whole string is also tried as a bare option name.
    std::size_t oi = std::string::npos;
    for (std::size_t split = 0; split <= option.size() && oi == std::string::npos; ++split) {
        const bool whole = split == option.size();
        if (!whole && option[split] != '/') continue;
        const std::string section = whole ? std::string() : option.substr(0, split);
        const std::string name = whole ? option : option.substr(split + 1);
        if (name.empty()) continue;
        oi = PickOne(package.options.size(), [&](std::size_t i, bool folded) {
            const Option& o = package.options[i];
            if (!section.empty() && !(folded ? SameFolded(o.section, section) : o.section == section)) return false;
            return folded ? (SameFolded(o.name, name) || SameFolded(o.id, name)) : (o.name == name || o.id == name);
        });
    }
    if (oi == std::string::npos) {
        error = "no single option named '" + option + "'";
        return false;
    }
    Option& o = package.options[oi];
    if (choice.empty() || choice == "0" || SameFolded(choice, "disabled")) {
        o.selected = 0;
        error.clear();
        return true;
    }
    std::size_t ci = PickOne(o.choices.size(), [&](std::size_t i, bool folded) {
        return folded ? SameFolded(o.choices[i].name, choice) : o.choices[i].name == choice;
    });
    if (ci == std::string::npos) {
        // A 1-based number.
        std::size_t n = 0;
        bool numeric = !choice.empty();
        for (char c : choice) {
            if (c < '0' || c > '9' || n > o.choices.size()) {
                numeric = false;
                break;
            }
            n = n * 10 + static_cast<std::size_t>(c - '0');
        }
        if (!numeric || n == 0 || n > o.choices.size()) {
            error = "option '" + o.name + "' has no single choice '" + choice + "'";
            return false;
        }
        ci = n - 1;
    }
    o.selected = ci + 1;
    error.clear();
    return true;
}

bool plan_package(const Package& package, const DiscIdentity& disc, const PlanOptions& options,
                  Plan& output, std::string& error) {
    try {
        if (disc.id.size() != 6 || !IsAlnumUpper(disc.id)) {
            error = "invalid disc id";
            return false;
        }
        if (package.root.empty() || package.root[0] != '/' || package.root.size() > kMaxString || HasBadPathChars(package.root)) {
            error = "invalid package root";
            return false;
        }
        if (!package.filter.matches(disc)) {
            output = Plan();
            error.clear();
            return true;
        }
        std::vector<Selection> selected;
        for (const auto& o : package.options) {
            if (o.selected > o.choices.size()) {
                error = "invalid selection in option '" + o.name + "'";
                return false;
            }
            if (o.selected == 0) continue;
            const Choice& ch = o.choices[o.selected - 1];
            for (const auto& pid : ch.patches) {
                auto it = package.patches.find(pid);
                if (it == package.patches.end()) {
                    error = "unresolved patch reference '" + pid + "'";
                    return false;
                }
                Selection s;
                s.option = &o;
                s.choice = &ch;
                s.patch = &it->second;
                s.patch_id = pid;
                s.params = o.params;
                s.params.insert(s.params.end(), ch.params.begin(), ch.params.end());
                selected.push_back(s);
            }
        }
        Plan tmp;
        std::vector<std::string> unsupported;
        for (const auto& sel : selected) {
            const Patch& p = *sel.patch;
            std::string base = package.root;
            if (!p.root.empty()) {
                if (!ResolveExternal(p.root, package.root, sel, disc, "patch root", base, error)) return false;
            }
            // Patches built in code rather than parsed may leave `order`
            // empty; treat that as kind-by-kind document order so nothing
            // selected is silently skipped.
            std::vector<PatchStep> order = p.order;
            if (order.empty()) {
                for (std::size_t i = 0; i < p.files.size(); ++i) order.push_back(PatchStep{PatchKind::File, i});
                for (std::size_t i = 0; i < p.folders.size(); ++i) order.push_back(PatchStep{PatchKind::Folder, i});
                for (std::size_t i = 0; i < p.memory.size(); ++i) order.push_back(PatchStep{PatchKind::Memory, i});
                for (std::size_t i = 0; i < p.savegames.size(); ++i) order.push_back(PatchStep{PatchKind::Savegame, i});
            }
            for (const auto& step : order) {
                switch (step.kind) {
                case PatchKind::File: {
                    if (step.index >= p.files.size()) { error = "corrupt patch order"; return false; }
                    FilePatch f = p.files[step.index];
                    if (!substitute_params(f.disc, sel.params, disc, f.disc, error)) {
                        error = Where(sel) + ": " + error;
                        return false;
                    }
                    f.disc = TidyDiscPath(f.disc);
                    {
                        std::string perr;
                        if (!CheckDiscPath(f.disc, false, true, f.is_filename, perr)) {
                            error = Where(sel) + ": file " + perr;
                            return false;
                        }
                    }
                    if (!ResolveExternal(f.external, base, sel, disc, "external", f.external, error)) return false;
                    if (f.is_filename && !options.allow_filename_targets) {
                        unsupported.push_back(Where(sel) + ": <file disc=\"" + f.disc + "\"> file-name lookup is not supported yet");
                    }
                    tmp.files.push_back(f);
                    tmp.order.push_back(PatchStep{PatchKind::File, tmp.files.size() - 1});
                    break;
                }
                case PatchKind::Folder: {
                    if (step.index >= p.folders.size()) { error = "corrupt patch order"; return false; }
                    FolderPatch f = p.folders[step.index];
                    if (!f.disc.empty()) {
                        if (!substitute_params(f.disc, sel.params, disc, f.disc, error)) {
                            error = Where(sel) + ": " + error;
                            return false;
                        }
                        f.disc = TidyDiscPath(f.disc);
                    }
                    {
                        std::string perr;
                        if (!CheckDiscPath(f.disc, true, true, f.is_name, perr)) {
                            error = Where(sel) + ": folder " + perr;
                            return false;
                        }
                    }
                    if (!ResolveExternal(f.external, base, sel, disc, "external", f.external, error)) return false;
                    if (!options.allow_folders) {
                        unsupported.push_back(Where(sel) + ": <folder> patches are not supported yet");
                    }
                    tmp.folders.push_back(f);
                    tmp.order.push_back(PatchStep{PatchKind::Folder, tmp.folders.size() - 1});
                    break;
                }
                case PatchKind::Memory: {
                    if (step.index >= p.memory.size()) { error = "corrupt patch order"; return false; }
                    MemoryPatch m = p.memory[step.index];
                    if (!m.valuefile.empty()) {
                        if (!ResolveExternal(m.valuefile, base, sel, disc, "valuefile", m.valuefile, error)) return false;
                    }
                    if (!options.allow_memory) {
                        unsupported.push_back(Where(sel) + ": <memory> patches are not supported yet");
                    }
                    tmp.memory.push_back(m);
                    tmp.order.push_back(PatchStep{PatchKind::Memory, tmp.memory.size() - 1});
                    break;
                }
                case PatchKind::Savegame: {
                    if (step.index >= p.savegames.size()) { error = "corrupt patch order"; return false; }
                    SavegamePatch s = p.savegames[step.index];
                    if (!ResolveExternal(s.external, base, sel, disc, "external", s.external, error)) return false;
                    if (!options.allow_savegames) {
                        unsupported.push_back(Where(sel) + ": <savegame> redirection is not supported yet");
                    }
                    tmp.savegames.push_back(s);
                    tmp.order.push_back(PatchStep{PatchKind::Savegame, tmp.savegames.size() - 1});
                    break;
                }
                }
            }
        }
        if (!unsupported.empty()) {
            error = "cannot launch: ";
            for (std::size_t i = 0; i < unsupported.size(); ++i) {
                if (i > 0) error += "; ";
                error += unsupported[i];
            }
            return false;
        }
        output = tmp;
        error.clear();
        return true;
    } catch (const std::bad_alloc&) {
        error = "allocation failure";
        return false;
    } catch (const std::exception& e) {
        try { error = e.what(); } catch (...) { error = "allocation failure"; }
        return false;
    } catch (...) {
        error = "allocation failure";
        return false;
    }
}
}  // namespace riftwii
