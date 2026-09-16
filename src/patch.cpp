#include "riftwii/patch.hpp"
#include <pugixml.hpp>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
#include <map>
namespace riftwii {
namespace {
constexpr std::size_t kMaxXml = 1048576;
constexpr std::size_t kMaxString = 4096;
constexpr std::size_t kMaxNodes = 8192;
constexpr int kMaxDepth = 16;
bool IsControl(unsigned char c) {
    return c < 0x20 || c == 0x7F;
}
bool HasBadPathChars(const std::string& s) {
    for (unsigned char c : s) {
        if (c == ':' || c == '\\' || c == '{' || c == '}' || c == '$' || IsControl(c)) {
            return true;
        }
    }
    return false;
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
bool ParseBoolStrict(const std::string& s, bool& out) {
    if (s == "true" || s == "yes") {
        out = true;
        return true;
    }
    if (s == "false" || s == "no") {
        out = false;
        return true;
    }
    return false;
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
    const char* needle = "<!doctype";
    std::size_t m = 9;
    for (std::size_t i = 0; i + m <= n; ++i) {
        if (xml[i] != '<' && xml[i] != '<') continue;
        bool hit = true;
        if (xml[i] != '<' || i + 1 >= n || xml[i + 1] != '!') continue;
        for (std::size_t k = 0; k < m; ++k) {
            if (i + k >= n) { hit = false; break; }
            char a = xml[i + k];
            char b = needle[k];
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
            if (a != b) { hit = false; break; }
        }
        if (hit) return true;
    }
    return false;
}
bool HasForbiddenPi(const std::string& xml, std::string& error) {
    std::size_t n = xml.size();
    std::size_t pos = 0;
    bool firstChecked = false;
    std::size_t declEnd = 0;
    bool hasDecl = false;
    while (true) {
        std::size_t f = xml.find("<?", pos);
        if (f == std::string::npos) return false;
        if (!firstChecked) {
            firstChecked = true;
            std::size_t s = 0;
            while (s < n && (xml[s] == ' ' || xml[s] == '\t' || xml[s] == '\n' || xml[s] == '\r')) ++s;
            if (s + 3 < n && xml[s] == '\xEF' && n >= s + 3 && static_cast<unsigned char>(xml[s]) == 0xEF && static_cast<unsigned char>(xml[s+1]) == 0xBB && static_cast<unsigned char>(xml[s+2]) == 0xBF) {
                s += 3;
                while (s < n && (xml[s] == ' ' || xml[s] == '\t' || xml[s] == '\n' || xml[s] == '\r')) ++s;
            }
            if (f == s && f + 5 <= n && xml.compare(f, 5, "<?xml") == 0) {
                std::size_t e = xml.find("?>", f + 2);
                if (e == std::string::npos) {
                    error = "invalid processing instruction";
                    return true;
                }
                hasDecl = true;
                declEnd = e + 2;
                pos = declEnd;
                continue;
            } else {
                error = "processing instruction not allowed";
                return true;
            }
        } else {
            (void)hasDecl;
            (void)declEnd;
            error = "processing instruction not allowed";
            return true;
        }
    }
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
struct Counter {
    std::size_t nodes = 0;
};
bool AttrPresent(pugi::xml_node node, const char* name) {
    return static_cast<bool>(node.attribute(name));
}
std::string AttrValue(pugi::xml_node node, const char* name) {
    return std::string(node.attribute(name).as_string());
}
bool CheckAttrLengths(pugi::xml_node node, std::string& error) {
    for (auto a : node.attributes()) {
        std::string v = a.as_string();
        if (v.size() > kMaxString) {
            error = std::string("string too long in attribute '") + a.name() + "'";
            return false;
        }
        std::string an = a.name();
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
bool ParseFileNode(pugi::xml_node n, Patch& patch, Counter& cnt, int depth, std::string& error) {
    if (depth > kMaxDepth) { error = "depth exceeded"; return false; }
    if (++cnt.nodes > kMaxNodes) { error = "too many nodes"; return false; }
    if (!CheckAttrLengths(n, error)) return false;
    if (!CheckNoText(n, error)) return false;
    for (auto c : n.children()) {
        if (c.type() == pugi::node_element) {
            error = std::string("unsupported tag '") + c.name() + "' in file";
            return false;
        }
    }
    const char* allowed[] = {"disc", "external", "resize", "create", "offset", "length", "fileoffset"};
    for (auto a : n.attributes()) {
        std::string an = a.name();
        bool ok = false;
        for (auto al : allowed) if (an == al) ok = true;
        if (!ok) {
            error = "unsupported attribute '" + an + "' in file";
            return false;
        }
    }
    if (!AttrPresent(n, "disc")) { error = "file missing disc"; return false; }
    if (!AttrPresent(n, "external")) { error = "file missing external"; return false; }
    std::string disc = AttrValue(n, "disc");
    std::string external = AttrValue(n, "external");
    if (disc.empty()) { error = "file disc empty"; return false; }
    if (external.empty()) { error = "file external empty"; return false; }
    if (disc.size() > kMaxString || external.size() > kMaxString) { error = "string too long in file"; return false; }
    if (disc[0] != '/') { error = "file disc must be absolute"; return false; }
    FilePatch f;
    f.disc = disc;
    f.external = external;
    f.resize = true;
    f.create = false;
    f.offset = 0;
    f.file_offset = 0;
    f.length = 0;
    if (AttrPresent(n, "resize")) {
        std::string v = AttrValue(n, "resize");
        if (v.empty()) { error = "file resize empty"; return false; }
        bool b = false;
        if (!ParseBoolStrict(v, b)) { error = "invalid file resize '" + v + "'"; return false; }
        f.resize = b;
    }
    if (AttrPresent(n, "create")) {
        std::string v = AttrValue(n, "create");
        if (v.empty()) { error = "file create empty"; return false; }
        bool b = false;
        if (!ParseBoolStrict(v, b)) { error = "invalid file create '" + v + "'"; return false; }
        f.create = b;
    }
    if (AttrPresent(n, "offset")) {
        std::string v = AttrValue(n, "offset");
        if (v.empty()) { error = "file offset empty"; return false; }
        std::uint64_t u = 0;
        if (!ParseU64(v, u)) { error = "invalid file offset '" + v + "'"; return false; }
        f.offset = u;
    }
    if (AttrPresent(n, "length")) {
        std::string v = AttrValue(n, "length");
        if (v.empty()) { error = "file length empty"; return false; }
        std::uint64_t u = 0;
        if (!ParseU64(v, u)) { error = "invalid file length '" + v + "'"; return false; }
        f.length = u;
    }
    if (AttrPresent(n, "fileoffset")) {
        std::string v = AttrValue(n, "fileoffset");
        if (v.empty()) { error = "file fileoffset empty"; return false; }
        std::uint64_t u = 0;
        if (!ParseU64(v, u)) { error = "invalid file fileoffset '" + v + "'"; return false; }
        f.file_offset = u;
    }
    patch.files.push_back(f);
    return true;
}
bool ParsePatchDef(pugi::xml_node n, Package& pkg, Counter& cnt, int depth, std::string& error) {
    if (depth > kMaxDepth) { error = "depth exceeded"; return false; }
    if (++cnt.nodes > kMaxNodes) { error = "too many nodes"; return false; }
    if (!CheckAttrLengths(n, error)) return false;
    if (!CheckNoText(n, error)) return false;
    for (auto a : n.attributes()) {
        std::string an = a.name();
        if (an != "id" && an != "root") {
            error = "unsupported attribute '" + an + "' in patch";
            return false;
        }
    }
    if (!AttrPresent(n, "id")) { error = "patch missing id"; return false; }
    std::string id = AttrValue(n, "id");
    if (id.empty()) { error = "patch id empty"; return false; }
    if (id.size() > kMaxString) { error = "string too long in patch id"; return false; }
    if (pkg.patches.find(id) != pkg.patches.end()) { error = "duplicate patch id '" + id + "'"; return false; }
    Patch p;
    if (AttrPresent(n, "root")) {
        std::string r = AttrValue(n, "root");
        if (r.empty()) { error = "patch root empty"; return false; }
        if (r.size() > kMaxString) { error = "string too long in patch root"; return false; }
        p.root = r;
    } else {
        p.root = "";
    }
    for (auto c : n.children()) {
        if (c.type() != pugi::node_element) continue;
        std::string cn = c.name();
        if (cn != "file") {
            error = "unsupported tag '" + cn + "' in patch";
            return false;
        }
        if (!ParseFileNode(c, p, cnt, depth + 1, error)) return false;
    }
    pkg.patches[id] = p;
    return true;
}
bool ParsePatchRef(pugi::xml_node n, Choice& ch, Counter& cnt, int depth, std::string& error) {
    if (depth > kMaxDepth) { error = "depth exceeded"; return false; }
    if (++cnt.nodes > kMaxNodes) { error = "too many nodes"; return false; }
    if (!CheckAttrLengths(n, error)) return false;
    if (!CheckNoText(n, error)) return false;
    for (auto a : n.attributes()) {
        std::string an = a.name();
        if (an != "id") {
            error = "unsupported attribute '" + an + "' in patch reference";
            return false;
        }
    }
    for (auto c : n.children()) {
        if (c.type() == pugi::node_element) {
            error = std::string("unsupported tag '") + c.name() + "' in patch reference";
            return false;
        }
    }
    if (!AttrPresent(n, "id")) { error = "patch reference missing id"; return false; }
    std::string id = AttrValue(n, "id");
    if (id.empty()) { error = "patch reference id empty"; return false; }
    if (id.size() > kMaxString) { error = "string too long in patch reference"; return false; }
    ch.patches.push_back(id);
    return true;
}
bool ParseChoice(pugi::xml_node n, Option& opt, Counter& cnt, int depth, std::string& error) {
    if (depth > kMaxDepth) { error = "depth exceeded"; return false; }
    if (++cnt.nodes > kMaxNodes) { error = "too many nodes"; return false; }
    if (!CheckAttrLengths(n, error)) return false;
    if (!CheckNoText(n, error)) return false;
    for (auto a : n.attributes()) {
        std::string an = a.name();
        if (an != "name") {
            error = "unsupported attribute '" + an + "' in choice";
            return false;
        }
    }
    if (!AttrPresent(n, "name")) { error = "choice missing name"; return false; }
    std::string nm = AttrValue(n, "name");
    if (nm.empty()) { error = "choice name empty"; return false; }
    Choice ch;
    ch.name = nm;
    for (auto c : n.children()) {
        if (c.type() != pugi::node_element) continue;
        std::string cn = c.name();
        if (cn != "patch") {
            error = "unsupported tag '" + cn + "' in choice";
            return false;
        }
        if (!ParsePatchRef(c, ch, cnt, depth + 1, error)) return false;
    }
    opt.choices.push_back(ch);
    return true;
}
bool ParseOption(pugi::xml_node n, const std::string& section, Package& pkg, Counter& cnt, int depth, std::string& error) {
    if (depth > kMaxDepth) { error = "depth exceeded"; return false; }
    if (++cnt.nodes > kMaxNodes) { error = "too many nodes"; return false; }
    if (!CheckAttrLengths(n, error)) return false;
    if (!CheckNoText(n, error)) return false;
    for (auto a : n.attributes()) {
        std::string an = a.name();
        if (an != "name" && an != "id" && an != "default") {
            error = "unsupported attribute '" + an + "' in option";
            return false;
        }
    }
    if (!AttrPresent(n, "name")) { error = "option missing name"; return false; }
    std::string nm = AttrValue(n, "name");
    if (nm.empty()) { error = "option name empty"; return false; }
    Option opt;
    opt.section = section;
    opt.name = nm;
    opt.selected = 0;
    if (AttrPresent(n, "id")) {
        std::string oid = AttrValue(n, "id");
        if (oid.empty()) { error = "option id empty"; return false; }
        opt.id = oid;
    }
    std::string defStr;
    bool hasDef = AttrPresent(n, "default");
    if (hasDef) defStr = AttrValue(n, "default");
    for (auto c : n.children()) {
        if (c.type() != pugi::node_element) continue;
        std::string cn = c.name();
        if (cn != "choice") {
            error = "unsupported tag '" + cn + "' in option";
            return false;
        }
        if (!ParseChoice(c, opt, cnt, depth + 1, error)) return false;
    }
    if (hasDef) {
        if (defStr.empty()) { error = "option default empty"; return false; }
        std::uint64_t u = 0;
        if (!ParseU64(defStr, u)) { error = "invalid option default '" + defStr + "'"; return false; }
        if (u > static_cast<std::uint64_t>(opt.choices.size())) { error = "invalid option default out of range"; return false; }
        opt.selected = static_cast<std::size_t>(u);
    } else {
        opt.selected = 0;
    }
    pkg.options.push_back(opt);
    return true;
}
bool ParseSection(pugi::xml_node n, Package& pkg, Counter& cnt, int depth, std::string& error) {
    if (depth > kMaxDepth) { error = "depth exceeded"; return false; }
    if (++cnt.nodes > kMaxNodes) { error = "too many nodes"; return false; }
    if (!CheckAttrLengths(n, error)) return false;
    if (!CheckNoText(n, error)) return false;
    for (auto a : n.attributes()) {
        std::string an = a.name();
        if (an != "name") {
            error = "unsupported attribute '" + an + "' in section";
            return false;
        }
    }
    if (!AttrPresent(n, "name")) { error = "section missing name"; return false; }
    std::string nm = AttrValue(n, "name");
    if (nm.empty()) { error = "section name empty"; return false; }
    for (auto c : n.children()) {
        if (c.type() != pugi::node_element) continue;
        std::string cn = c.name();
        if (cn != "option") {
            error = "unsupported tag '" + cn + "' in section";
            return false;
        }
        if (!ParseOption(c, nm, pkg, cnt, depth + 1, error)) return false;
    }
    return true;
}
bool ParseOptions(pugi::xml_node n, Package& pkg, Counter& cnt, int depth, std::string& error) {
    if (depth > kMaxDepth) { error = "depth exceeded"; return false; }
    if (++cnt.nodes > kMaxNodes) { error = "too many nodes"; return false; }
    if (!CheckAttrLengths(n, error)) return false;
    if (!CheckNoText(n, error)) return false;
    for (auto a : n.attributes()) {
        error = std::string("unsupported attribute '") + a.name() + "' in options";
        return false;
    }
    for (auto c : n.children()) {
        if (c.type() != pugi::node_element) continue;
        std::string cn = c.name();
        if (cn != "section") {
            error = "unsupported tag '" + cn + "' in options";
            return false;
        }
        if (!ParseSection(c, pkg, cnt, depth + 1, error)) return false;
    }
    return true;
}
bool ParseId(pugi::xml_node n, DiscFilter& filter, Counter& cnt, int depth, std::string& error) {
    if (depth > kMaxDepth) { error = "depth exceeded"; return false; }
    if (++cnt.nodes > kMaxNodes) { error = "too many nodes"; return false; }
    if (!CheckAttrLengths(n, error)) return false;
    if (!CheckNoText(n, error)) return false;
    for (auto a : n.attributes()) {
        std::string an = a.name();
        if (an != "game" && an != "developer" && an != "disc" && an != "revision" && an != "version") {
            error = "unsupported attribute '" + an + "' in id";
            return false;
        }
    }
    if (AttrPresent(n, "game")) {
        std::string g = AttrValue(n, "game");
        if (g.empty()) { error = "id game empty"; return false; }
        if (g.size() > 4) { error = "id game too long"; return false; }
        if (!IsAlnumUpper(g)) { error = "invalid id game"; return false; }
        filter.game = g;
    }
    if (AttrPresent(n, "developer")) {
        std::string d = AttrValue(n, "developer");
        if (d.empty()) { error = "id developer empty"; return false; }
        if (d.size() != 2) { error = "id developer must be 2 characters"; return false; }
        if (!IsAlnumUpper(d)) { error = "invalid id developer"; return false; }
        filter.developer = d;
    }
    if (AttrPresent(n, "disc")) {
        std::string v = AttrValue(n, "disc");
        if (v.empty()) { error = "id disc empty"; return false; }
        std::uint64_t u = 0;
        if (!ParseU64(v, u) || u > 255) { error = "invalid id disc '" + v + "'"; return false; }
        filter.number = static_cast<int>(u);
    }
    bool hasRev = AttrPresent(n, "revision");
    bool hasVer = AttrPresent(n, "version");
    if (hasRev && hasVer) {
        std::string a = AttrValue(n, "revision");
        std::string b = AttrValue(n, "version");
        if (a.empty() || b.empty()) { error = "id revision empty"; return false; }
        std::uint64_t ua = 0, ub = 0;
        if (!ParseU64(a, ua) || ua > 255) { error = "invalid id revision '" + a + "'"; return false; }
        if (!ParseU64(b, ub) || ub > 255) { error = "invalid id version '" + b + "'"; return false; }
        if (ua != ub) { error = "id revision/version mismatch"; return false; }
        filter.revision = static_cast<int>(ua);
    } else if (hasRev) {
        std::string v = AttrValue(n, "revision");
        if (v.empty()) { error = "id revision empty"; return false; }
        std::uint64_t u = 0;
        if (!ParseU64(v, u) || u > 255) { error = "invalid id revision '" + v + "'"; return false; }
        filter.revision = static_cast<int>(u);
    } else if (hasVer) {
        std::string v = AttrValue(n, "version");
        if (v.empty()) { error = "id version empty"; return false; }
        std::uint64_t u = 0;
        if (!ParseU64(v, u) || u > 255) { error = "invalid id version '" + v + "'"; return false; }
        filter.revision = static_cast<int>(u);
    }
    for (auto c : n.children()) {
        if (c.type() != pugi::node_element) continue;
        std::string cn = c.name();
        if (cn != "region") {
            error = "unsupported tag '" + cn + "' in id";
            return false;
        }
        if (++cnt.nodes > kMaxNodes) { error = "too many nodes"; return false; }
        if (depth + 1 > kMaxDepth) { error = "depth exceeded"; return false; }
        if (!CheckAttrLengths(c, error)) return false;
        if (!CheckNoText(c, error)) return false;
        for (auto a : c.attributes()) {
            std::string an = a.name();
            if (an != "type") {
                error = "unsupported attribute '" + an + "' in region";
                return false;
            }
        }
        for (auto gc : c.children()) {
            if (gc.type() == pugi::node_element) {
                error = std::string("unsupported tag '") + gc.name() + "' in region";
                return false;
            }
        }
        if (!AttrPresent(c, "type")) { error = "region missing type"; return false; }
        std::string t = AttrValue(c, "type");
        if (t.empty()) { error = "region type empty"; return false; }
        if (t.size() != 1) { error = "region type must be 1 character"; return false; }
        filter.regions.push_back(t);
    }
    return true;
}
}
bool DiscFilter::matches(const DiscIdentity& disc) const {
    if (disc.id.size() != 6) return false;
    for (char ch : disc.id) {
        bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
        if (!ok) return false;
    }
    if (!game.empty()) {
        if (game.size() > 4) return false;
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
        {
            std::string dupErr;
            if (!CheckDuplicateAttrs(xml, dupErr)) {
                error = dupErr;
                return false;
            }
        }
        pugi::xml_document doc;
        pugi::xml_parse_result res = doc.load_buffer(xml.data(), xml.size(), pugi::parse_full);
        if (!res) {
            error = std::string("xml parse error: ") + res.description();
            return false;
        }
        for (auto c : doc.children()) {
            auto t = c.type();
            if (t == pugi::node_pi || t == pugi::node_doctype || t == pugi::node_declaration) {
                if (t == pugi::node_declaration) {
                    error = "xml declaration not allowed";
                    return false;
                }
                error = "processing instruction or doctype not allowed";
                return false;
            }
            if (t == pugi::node_pcdata || t == pugi::node_cdata) {
                if (!IsWhitespaceOnly(c.value())) {
                    error = "unexpected text at top level";
                    return false;
                }
            }
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
        Counter cnt;
        cnt.nodes = 1;
        Package tmp;
        tmp.root = "/riivolution";
        tmp.filter.game = "";
        tmp.filter.developer = "";
        tmp.filter.revision = -1;
        tmp.filter.number = -1;
        if (!CheckAttrLengths(r, error)) return false;
        if (!CheckNoText(r, error)) return false;
        for (auto a : r.attributes()) {
            std::string an = a.name();
            if (an != "version" && an != "root") {
                error = "unsupported attribute '" + an + "' in wiidisc";
                return false;
            }
        }
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
        if (AttrPresent(r, "root")) {
            std::string rt = AttrValue(r, "root");
            if (rt.empty()) { error = "wiidisc root empty"; return false; }
            if (rt.size() > kMaxString) { error = "string too long in wiidisc root"; return false; }
            if (rt[0] != '/') { error = "wiidisc root must be absolute"; return false; }
            if (HasBadPathChars(rt)) { error = "invalid wiidisc root"; return false; }
            tmp.root = rt;
        }
        bool seenId = false;
        bool seenOptions = false;
        for (auto c : r.children()) {
            if (c.type() != pugi::node_element) continue;
            std::string cn = c.name();
            if (cn == "id") {
                if (seenId) { error = "duplicate id"; return false; }
                seenId = true;
                if (!ParseId(c, tmp.filter, cnt, 2, error)) return false;
            } else if (cn == "options") {
                if (seenOptions) { error = "duplicate options"; return false; }
                seenOptions = true;
                if (!ParseOptions(c, tmp, cnt, 2, error)) return false;
            } else if (cn == "patch") {
                if (!ParsePatchDef(c, tmp, cnt, 2, error)) return false;
            } else {
                error = "unsupported tag '" + cn + "' in wiidisc";
                return false;
            }
        }
        if (cnt.nodes > kMaxNodes) { error = "too many nodes"; return false; }
        for (const auto& o : tmp.options) {
            for (const auto& ch : o.choices) {
                for (const auto& pid : ch.patches) {
                    if (tmp.patches.find(pid) == tmp.patches.end()) {
                        error = "unresolved patch reference '" + pid + "'";
                        return false;
                    }
                }
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
bool plan_files(const Package& package, const DiscIdentity& disc, std::vector<FilePatch>& output, std::string& error) {
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
            std::vector<FilePatch> empty;
            output = empty;
            error.clear();
            return true;
        }
        std::vector<const Patch*> selected;
        std::vector<std::string> selIds;
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
                selected.push_back(&it->second);
                selIds.push_back(pid);
            }
        }
        std::vector<FilePatch> tmp;
        for (std::size_t si = 0; si < selected.size(); ++si) {
            const Patch* p = selected[si];
            std::string patchBase = package.root;
            if (!p->root.empty()) {
                std::string pb;
                if (!resolve_path(package.root, p->root, pb)) {
                    error = "invalid patch root '" + p->root + "'";
                    return false;
                }
                patchBase = pb;
            }
            for (const auto& f : p->files) {
                if (f.disc.empty() || f.disc[0] != '/') {
                    error = "file disc must be absolute";
                    return false;
                }
                if (f.disc.size() > kMaxString || f.external.size() > kMaxString) {
                    error = "string too long in file";
                    return false;
                }
                if (f.external.empty()) {
                    error = "file external empty";
                    return false;
                }
                std::string resolved;
                if (!resolve_path(patchBase, f.external, resolved)) {
                    error = "invalid external path '" + f.external + "'";
                    return false;
                }
                FilePatch nf = f;
                nf.external = resolved;
                tmp.push_back(nf);
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
}
