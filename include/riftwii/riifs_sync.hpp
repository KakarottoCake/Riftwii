// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "riftwii/riifs.hpp"

// Mirrors files from a RiiFS server into a local folder (on the Wii, a
// cache on the SD card), so a pack served from a PC boots exactly like one
// on the card. A server path "/riivolution/x/a.arc" lands at
// `local_root + "/riivolution/x/a.arc"`.
//
// RiiFS reports a file's size but no modification time, so a local copy of
// the same size is taken as current unless `force` is set. Local files the
// server no longer has are removed from a mirrored folder, so a folder
// patch never picks up a stale file. `keep` protects folders (save
// folders): one the card already has is left alone, one it lacks is copied
// once, so a save starts from the PC's and lives on the card after.
namespace riftwii::riifs {

struct LocalEntry {
    std::string name;
    bool is_directory = false;
};

// The local side, absolute paths ("sd:/riftwii/riifs/…" on the Wii).
class LocalStore {
public:
    virtual ~LocalStore() = default;
    virtual bool file_size(const std::string& path, std::uint64_t& size, bool& exists) = 0;
    virtual bool list(const std::string& dir, std::vector<LocalEntry>& out, bool& exists) = 0;
    virtual bool make_dirs(const std::string& dir) = 0;       // with parents; fine when present
    virtual bool begin_file(const std::string& path) = 0;     // create or truncate
    virtual bool write(const std::uint8_t* data, std::size_t length) = 0;
    virtual bool end_file(bool keep) = 0;                     // close; delete when !keep
    virtual bool remove(const std::string& path) = 0;         // a file, or a folder and all it holds
};

struct SyncItem {
    std::string path;          // absolute server path
    bool folder = false;
    bool recursive = true;     // folders: subfolders too
    std::string suffix;        // folders: only names ending in this (ASCII case-insensitive); empty = all
};

struct SyncStats {
    std::uint32_t checked = 0;     // files compared
    std::uint32_t copied = 0;
    std::uint64_t bytes = 0;       // copied bytes
    std::uint32_t removed = 0;     // local entries the server no longer has
    std::uint32_t missing = 0;     // items the server does not have
};

struct SyncOptions {
    bool force = false;
    std::vector<std::string> keep;  // server folders copied only while the card lacks them
    // After every file; `current` is its server path. Return false to stop.
    std::function<bool(const SyncStats&, const std::string& current)> progress;
};

bool sync(Client& client, LocalStore& local, const std::string& local_root, const std::vector<SyncItem>& items,
          const SyncOptions& options, SyncStats& stats, std::string& error);

}  // namespace riftwii::riifs
