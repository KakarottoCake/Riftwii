#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace riftwii {

struct DiscIdentity {
    std::string id;
    std::uint8_t revision = 0;
    std::uint8_t number = 0;
};

struct DiscFilter {
    std::string game;
    std::string developer;
    std::vector<std::string> regions;
    int revision = -1;
    int number = -1;
    bool matches(const DiscIdentity& disc) const;
};

struct FilePatch {
    std::string disc;
    std::string external;
    std::uint64_t offset = 0;
    std::uint64_t file_offset = 0;
    std::uint64_t length = 0;
    bool resize = true;
    bool create = false;
};

struct Choice {
    std::string name;
    std::vector<std::string> patches;
};

struct Option {
    std::string section;
    std::string id;
    std::string name;
    std::size_t selected = 0;
    std::vector<Choice> choices;
};

struct Patch {
    std::string root;
    std::vector<FilePatch> files;
};

struct Package {
    DiscFilter filter;
    std::string root = "/riivolution";
    std::vector<Option> options;
    std::map<std::string, Patch> patches;
};

bool parse_package(const std::string& xml, Package& output, std::string& error);
bool resolve_path(const std::string& root, const std::string& path, std::string& output);
bool plan_files(const Package& package, const DiscIdentity& disc,
                std::vector<FilePatch>& output, std::string& error);

}
