// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "storedata.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

#include "riftwii/checkedalloc.hpp"
#include "riftwii/pngdecode.hpp"
#include "riftwii/zipread.hpp"
#include "rwpruntime.hpp"

namespace riftwii::appstore {
namespace {

// Where the store keeps its notes and its download while it unpacks it.
constexpr const char* kStoreFolder = "sd:/riftwii/apps";
constexpr const char* kDownloadFile = "sd:/riftwii/apps/download.tmp";
// The list's answer: the list and its HTTP headers.
constexpr std::size_t kListBufferBytes = kAppListMaxBytes + (64u << 10);
constexpr std::size_t kIconBufferBytes = kAppIconMaxBytes + (64u << 10);
// Icons are 128 x 48 (the Homebrew Channel's); a bigger one is refused.
constexpr std::uint32_t kIconMaxSide = 256;
// Icon textures kept at once; the ones not drawn longest go first.
constexpr std::size_t kMaxTextures = 40;
// A zip's directory may also name its folders.
constexpr std::size_t kMaxZipEntries = 2 * kAppMaxFiles;

void ReplaceAll(std::string& text, const char* mark, const std::string& with) {
    const std::size_t length = std::strlen(mark);
    for (std::size_t at = text.find(mark); at != std::string::npos; at = text.find(mark, at + with.size()))
        text.replace(at, length, with);
}

int CompareNames(const AppListing& a, const AppListing& b) {
    for (std::size_t i = 0;; ++i) {
        char x = a.name[i], y = b.name[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x + 32);
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y + 32);
        if (x != y || x == '\0') return static_cast<unsigned char>(x) - static_cast<unsigned char>(y);
    }
}

std::string Hex(const std::uint8_t* bytes, std::size_t size) {
    static const char kDigits[] = "0123456789abcdef";
    std::string out;
    for (std::size_t i = 0; i < size; ++i) {
        out += kDigits[bytes[i] >> 4];
        out += kDigits[bytes[i] & 15];
    }
    return out;
}

bool IsFolder(const std::string& path) {
    rwp_path_info info;
    return Host().path_info(path.c_str(), &info) == 0 && info.is_directory;
}

bool Exists(const std::string& path) {
    rwp_path_info info;
    return Host().path_info(path.c_str(), &info) == 0;
}

// Every folder of `path` (a file's, "sd:/apps/x/data/a.txt") made.
bool MakeParents(const std::string& path) {
    const std::size_t root = path.find(":/");
    if (root == std::string::npos) return false;
    for (std::size_t at = path.find('/', root + 2); at != std::string::npos; at = path.find('/', at + 1)) {
        if (Host().make_directory(path.substr(0, at).c_str()) != 0) return false;
    }
    return true;
}

struct Listing {
    std::vector<std::pair<std::string, bool>> entries;
};

void Collect(const char* name, std::uint32_t is_directory, void* arg) {
    static_cast<Listing*>(arg)->entries.emplace_back(name, is_directory != 0);
}

// A folder and everything in it. Depth is bounded: an app's folder is not
// deep, and a loop of folders cannot exist on FAT.
bool RemoveTree(const std::string& folder, int depth = 0) {
    if (depth > 16) return false;
    Listing listing;
    if (Host().list_directory(folder.c_str(), Collect, &listing) < 0) return false;
    for (const auto& entry : listing.entries) {
        const std::string path = folder + "/" + entry.first;
        if (entry.second ? !RemoveTree(path, depth + 1) : Host().remove_path(path.c_str()) != 0) return false;
    }
    return Host().remove_path(folder.c_str()) == 0;
}

std::string RecordPath(const AppListing& app) { return std::string(kStoreFolder) + "/" + app.id + ".txt"; }

// The store's note of `app`, read whole. Unreadable is kept apart from
// Absent: a note that is there but cannot be read (a short read, no
// memory, damaged text) must not make the app's files look like no one's.
enum class Note { Absent, Read, Unreadable };
Note ReadRecord(const AppListing& app, AppRecord& record) {
    const std::string path = RecordPath(app);
    rwp_path_info info;
    if (Host().path_info(path.c_str(), &info) != 0) return Note::Absent;
    if (info.is_directory || info.size == 0 || info.size > kAppRecordMaxBytes) return Note::Unreadable;
    // The text and the list made of it.
    if (!can_allocate(static_cast<std::size_t>(info.size) * 3)) return Note::Unreadable;
    std::string text(static_cast<std::size_t>(info.size), '\0');
    const std::int32_t read = Host().file_read(path.c_str(), 0, &text[0], static_cast<std::uint32_t>(text.size()));
    if (read < 0 || static_cast<std::uint64_t>(read) != info.size) return Note::Unreadable;
    return app_record_parse(text, record) ? Note::Read : Note::Unreadable;
}

bool WriteText(const std::string& path, const std::string& text) {
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) return false;
    const bool written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    return std::fclose(file) == 0 && written;
}

// `path` replaced by `text` only once the new text is whole on the card:
// written beside it first, then swapped in, the old one put back if the
// swap fails (rename_path does not overwrite).
bool ReplaceText(const std::string& path, const std::string& text) {
    const std::string fresh = path + ".new", old = path + ".old";
    Host().remove_path(fresh.c_str());
    if (!WriteText(fresh, text)) {
        Host().remove_path(fresh.c_str());
        return false;
    }
    Host().remove_path(old.c_str());
    const bool had_old = Exists(path);
    if (had_old && Host().rename_path(path.c_str(), old.c_str()) != 0) {
        Host().remove_path(fresh.c_str());
        return false;
    }
    if (Host().rename_path(fresh.c_str(), path.c_str()) != 0) {
        if (had_old) Host().rename_path(old.c_str(), path.c_str());
        Host().remove_path(fresh.c_str());
        return false;
    }
    Host().remove_path(old.c_str());
    return true;
}

// The CRC-32 of a file on the card, as a zip lists it.
// Whole only when every byte the card says it has was read (the plugin
// has no ferror to tell a failed read from the end).
bool FileCrc(const std::string& path, std::uint32_t& crc) {
    rwp_path_info info;
    if (Host().path_info(path.c_str(), &info) != 0 || info.is_directory) return false;
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    std::uint8_t chunk[4096];
    crc = 0;
    std::uint64_t total = 0;
    std::size_t got;
    while ((got = std::fread(chunk, 1, sizeof(chunk), file)) > 0) {
        crc = zip_crc32_update(crc, chunk, got);
        total += got;
    }
    std::fclose(file);
    return total == info.size;
}

// What is at `card_path` ("apps/x/boot.dol") against the note's `recorded`.
AppFileOnCard OnCard(const std::string& card_path, const AppRecordFile* recorded) {
    const std::string full = "sd:/" + card_path;
    rwp_path_info info;
    if (Host().path_info(full.c_str(), &info) != 0) return AppFileOnCard::Absent;
    if (info.is_directory || recorded == nullptr) return AppFileOnCard::NotOurs;
    if (info.size != recorded->size) return AppFileOnCard::Changed;
    std::uint32_t crc = 0;
    // A file that cannot be read back counts as changed: kept, never lost.
    if (!FileCrc(full, crc)) return AppFileOnCard::Changed;
    return crc == recorded->crc ? AppFileOnCard::AsInstalled : AppFileOnCard::Changed;
}

// The top folder of `card_path` that is the app's: "apps/<id>" or one of
// its data folders; "" for a path that is neither (a damaged note's).
std::string AppTop(const AppListing& app, const std::string& card_path) {
    const std::string own = std::string("apps/") + app.id;
    if (card_path.compare(0, own.size() + 1, own + "/") == 0) return own;
    for (std::uint8_t i = 0; i < app.data_folder_count; ++i) {
        const std::string data = app.data_folders[i];
        if (card_path.compare(0, data.size() + 1, data + "/") == 0) return data;
    }
    return "";
}

// The folders above a removed file, as far as the app's top folder, each
// gone if it is now empty (remove_path takes only an empty one).
void PruneEmpty(const std::string& card_path, const std::string& top) {
    std::string folder = card_path;
    while (true) {
        const std::size_t slash = folder.rfind('/');
        if (slash == std::string::npos) return;
        folder.resize(slash);
        if (folder.size() < top.size()) return;
        if (Host().remove_path(("sd:/" + folder).c_str()) != 0) return;
    }
}

bool HasEntries(const std::string& folder) {
    Listing listing;
    return Host().list_directory(folder.c_str(), Collect, &listing) >= 0 && !listing.entries.empty();
}

// A staged file and where it goes.
struct Move {
    std::string staged, target;
};

// Where the old files wait while the new ones go in: the store's own
// folder, under names no app uses, never next to the player's files.
constexpr const char* kBackupFolder = "sd:/riftwii/apps/backup";

// A name in the backup folder that is not taken: a backup left by an
// earlier failed install is someone's only copy, so it is never replaced.
std::string FreeBackupName(const std::string& id, std::size_t& next) {
    for (;; ++next) {
        const std::string name = std::string(kBackupFolder) + "/" + id + "-" + std::to_string(next) + ".bak";
        if (!Exists(name)) {
            ++next;
            return name;
        }
    }
}

// The staged files put in place. Each old file waits in the backup folder
// until every new one is in, and goes back if one cannot be moved, so the
// app is the old version or the new one, never a mix. A fresh install that
// fails leaves no folder behind. Returns "" or why it failed; when an old
// file could not be put back, the reason says where it is.
std::string Commit(const std::vector<Move>& moves, const std::string& own, const std::string& id) {
    const bool fresh = !IsFolder(own);
    std::vector<std::string> backups;
    std::size_t next = 0;
    bool ok = MakeParents(std::string(kBackupFolder) + "/");
    for (std::size_t i = 0; ok && i < moves.size(); ++i) {
        const Move& move = moves[i];
        std::string backup;
        if (!MakeParents(move.target)) {
            ok = false;
            break;
        }
        if (Exists(move.target)) {
            backup = FreeBackupName(id, next);
            if (Host().rename_path(move.target.c_str(), backup.c_str()) != 0) {
                ok = false;
                break;
            }
        }
        if (Host().rename_path(move.staged.c_str(), move.target.c_str()) != 0) {
            // This one's old file is put back with the others below.
            backups.push_back(backup);
            Host().remove_path(move.target.c_str());
            ok = false;
            break;
        }
        backups.push_back(backup);
    }
    if (ok) {
        for (const std::string& backup : backups) {
            if (!backup.empty()) Host().remove_path(backup.c_str());
        }
        return "";
    }
    // Back as it was, last first. Two things can stay wrong, and the player
    // is told each apart: a new file that could not be removed (named), and
    // an old file that could not go back (it stays in the backup folder).
    std::string new_left;
    bool old_left = false;
    for (std::size_t i = backups.size(); i-- > 0;) {
        if (Exists(moves[i].target) && Host().remove_path(moves[i].target.c_str()) != 0) {
            Host().log("%s: the new %s could not be removed", id.c_str(), moves[i].target.c_str());
            if (new_left.empty()) new_left = moves[i].target;
            if (!backups[i].empty()) {
                // Its old file cannot go where the new one still is.
                old_left = true;
                Host().log("%s: the old %s stays %s", id.c_str(), moves[i].target.c_str(), backups[i].c_str());
            }
            continue;
        }
        if (!backups[i].empty() && Host().rename_path(backups[i].c_str(), moves[i].target.c_str()) != 0) {
            old_left = true;
            Host().log("%s: %s could not be put back; it is %s", id.c_str(), moves[i].target.c_str(), backups[i].c_str());
        }
    }
    if (fresh && new_left.empty() && !old_left && IsFolder(own)) RemoveTree(own);
    std::string why = "the app's files could not be put in place";
    if (new_left.empty() && !old_left) return why + "; the old ones were put back";
    if (!new_left.empty()) why += "; some new files could not be removed, such as " + new_left;
    if (old_left) why += std::string("; some old files could not be put back: they are in ") + kBackupFolder;
    return why;
}

bool PngInflate(const std::uint8_t* data, std::size_t size, std::uint8_t* out, std::size_t out_size) {
    z_stream z;
    std::memset(&z, 0, sizeof(z));
    if (inflateInit(&z) != Z_OK) return false;
    z.next_in = const_cast<Bytef*>(data);
    z.avail_in = static_cast<uInt>(size);
    z.next_out = out;
    z.avail_out = static_cast<uInt>(out_size);
    const int result = inflate(&z, Z_FINISH);
    const bool whole = (result == Z_STREAM_END || result == Z_BUF_ERROR || result == Z_OK) && z.avail_out == 0;
    inflateEnd(&z);
    return whole;
}

// zlib's raw inflater through RiftWii's copy (rwp_host's zlib_*).
class ZlibStream final : public ZipInflateStream {
public:
    bool begin() override {
        std::memset(&z_, 0, sizeof(z_));
        return inflateInit2(&z_, -MAX_WBITS) == Z_OK;
    }
    Step feed(const std::uint8_t* in, std::size_t size, const ZipSink& sink) override {
        z_.next_in = const_cast<Bytef*>(in);
        z_.avail_in = static_cast<uInt>(size);
        for (;;) {
            z_.next_out = out_;
            z_.avail_out = sizeof(out_);
            const int result = inflate(&z_, Z_NO_FLUSH);
            const std::size_t made = sizeof(out_) - z_.avail_out;
            if (made > 0 && !sink(out_, made)) return Step::Bad;
            if (result == Z_STREAM_END) return Step::Done;
            if (result != Z_OK && result != Z_BUF_ERROR) return Step::Bad;
            if (z_.avail_in == 0 && z_.avail_out != 0) return Step::More;
        }
    }
    void end() override { inflateEnd(&z_); }

private:
    z_stream z_;
    std::uint8_t out_[16384];
};

void RunJob(void* arg);

}  // namespace

const rwp_host& Host() { return riftwii::plugin::host(); }

std::string tr(const char* english) {
    const char* text = Host().translate(english);
    return text ? text : english;
}

std::string tr(const char* english, const std::string& one) {
    std::string text = tr(english);
    ReplaceAll(text, "{1}", one);
    return text;
}

std::string tr(const char* english, const std::string& one, const std::string& two) {
    std::string text = tr(english);
    ReplaceAll(text, "{1}", one);
    ReplaceAll(text, "{2}", two);
    return text;
}

std::string SizeText(std::uint64_t bytes) { return app_size_text(bytes, tr("{1}.{2} MB").c_str(), tr("{1} KB").c_str()); }

void Store::StartList() {
    list_state = ListState::Loading;
    list_error.clear();
    want_list_ = true;
}

void Store::Start(Job job, std::size_t index) {
    job_ = job;
    job_index_ = index;
    job_ok_ = false;
    job_error_.clear();
    gate_.store(Running);
    if (Host().background_start(RunJob, this) != 0) job_ = Job::None;  // asked again next frame
}

void Store::Pump(std::uint32_t frame) {
    if (job_ != Job::None && !Host().background_running()) Finish();
    if (job_ != Job::None || shutting_down_) return;
    if (queued_install_ != kNone) {
        const std::size_t index = queued_install_;
        Start(Job::Install, index);
        if (job_ == Job::Install) queued_install_ = kNone;  // else the network thread is busy: next frame
        return;
    }
    if (want_list_) {
        // Its memory is taken here, on the menu's thread: a job never
        // allocates much on the network's.
        if (list_buffer_.size() != kListBufferBytes) list_buffer_.resize(kListBufferBytes);
        listings.assign(kAppListMaxApps, AppListing{});
        Start(Job::List, 0);
        if (job_ == Job::List) want_list_ = false;
        return;
    }
    while (!icon_queue_.empty()) {
        const std::size_t index = icon_queue_.back();
        icon_queue_.pop_back();
        if (index >= apps.size() || apps[index].texture || apps[index].icon_failed) continue;
        if (apps[index].listing->icon[0] == '\0') {
            apps[index].icon_failed = true;
            continue;
        }
        if (icon_buffer_.size() != kIconBufferBytes) icon_buffer_.resize(kIconBufferBytes);
        Start(Job::Icon, index);
        if (job_ == Job::None) icon_queue_.push_back(index);  // the network's thread is busy: later
        break;
    }
    (void)frame;
}

void Store::WantIcons(const std::vector<std::size_t>& shown, std::uint32_t frame) {
    icon_queue_.clear();
    // Fetched from the last to the first, so the first shown comes first.
    for (std::size_t k = shown.size(); k-- > 0;) {
        const std::size_t index = shown[k];
        if (index >= apps.size()) continue;
        apps[index].icon_used = frame;
        if (!apps[index].texture && !apps[index].icon_failed) icon_queue_.push_back(index);
    }
}

void Store::Finish() {
    const Job job = job_;
    const std::size_t index = job_index_;
    job_ = Job::None;
    switch (job) {
        case Job::List:
            list_buffer_.clear();
            list_buffer_.shrink_to_fit();
            if (job_ok_) {
                list_state = ListState::Ready;
            } else {
                listings.clear();
                apps.clear();
                list_error = job_error_;
                list_state = ListState::Failed;
            }
            break;
        case Job::Icon: {
            if (index >= apps.size()) break;
            if (!job_ok_) {
                apps[index].icon_failed = true;
                break;
            }
            // Room for one more: the icon drawn longest ago goes.
            if (textures_ >= kMaxTextures) {
                std::size_t oldest = apps.size();
                for (std::size_t i = 0; i < apps.size(); ++i) {
                    if (apps[i].texture && (oldest == apps.size() || apps[i].icon_used < apps[oldest].icon_used))
                        oldest = i;
                }
                if (oldest < apps.size()) {
                    Host().texture_free(apps[oldest].texture);
                    apps[oldest].texture = 0;
                    --textures_;
                }
            }
            apps[index].texture = Host().texture_create(icon_rgba_.data(), icon_width_, icon_height_);
            if (apps[index].texture) ++textures_;
            else apps[index].icon_failed = true;
            icon_rgba_.clear();
            break;
        }
        case Job::Install:
        case Job::Remove:
        case Job::RemoveData:
            Refresh(index);
            finished_index = index;
            finished_ok = job_ok_;
            finished_remove = job != Job::Install;
            finished_leftovers = job == Job::Remove ? job_leftovers_ : std::vector<std::string>{};
            job_leftovers_.clear();
            finished_error = job_error_;
            finished_pending_ = true;
            if (job_ok_) changed = true;
            break;
        case Job::None:
            break;
    }
}

std::size_t Store::Find(const char* id) const {
    for (std::size_t i = 0; i < apps.size(); ++i) {
        if (std::strcmp(apps[i].listing->id, id) == 0) return i;
    }
    return apps.size();
}

void Store::Refresh(std::size_t index) {
    if (index >= apps.size()) return;
    const AppListing& app = *apps[index].listing;
    // Installed means its program is there: a folder left with only its
    // settings (a removal that kept them) is not.
    const std::string own = std::string("sd:/apps/") + app.id;
    const bool program = Exists(own + "/boot.dol") || Exists(own + "/boot.elf");
    // The note's start is enough here: its version and checksum come first
    // (a file line cut off at the end is left out).
    char text[512];
    const std::string record_path = RecordPath(app);
    const std::int32_t read = Host().file_read(record_path.c_str(), 0, text, sizeof(text) - 1);
    AppRecord record;
    bool has_record = false;
    if (read > 0) {
        text[read] = '\0';
        has_record = app_record_parse(text, record);
    }
    apps[index].state = app_state(app, program, has_record ? &record : nullptr);
}

std::size_t Store::UpdateCount() const {
    std::size_t count = 0;
    for (const App& app : apps) {
        if (app.state == AppState::Update) ++count;
    }
    return count;
}

bool Store::StartInstall(std::size_t index) {
    if (index >= apps.size() || !app_installable(*apps[index].listing)) return false;
    // Another install or a removal runs: one at a time, the player waits.
    if (job_ == Job::Install || job_ == Job::Remove || job_ == Job::RemoveData || queued_install_ != kNone) return false;
    progress.Set(InstallStage::Download, 0, apps[index].listing->size);
    // An icon or the list may still be downloading (or RiftWii's own
    // network work): the install waits for it and starts next (Pump).
    queued_install_ = index;
    Pump(0);
    return true;
}

bool Store::StartRemove(std::size_t index) {
    if (job_ != Job::None || index >= apps.size()) return false;
    Start(Job::Remove, index);
    return job_ == Job::Remove;
}

bool Store::StartRemoveData(std::size_t index) {
    if (job_ != Job::None || index >= apps.size()) return false;
    Start(Job::RemoveData, index);
    return job_ == Job::RemoveData;
}

void Store::CancelInstall() {
    if (queued_install_ != kNone) {
        // Not begun: nothing to stop but the wait.
        finished_index = queued_install_;
        finished_ok = false;
        finished_remove = false;
        finished_error = "stopped";
        finished_pending_ = true;
        progress.SetStage(InstallStage::Cancelled);
        queued_install_ = kNone;
        return;
    }
    if (job_ != Job::Install) return;
    // Only while the job has not begun moving files in: then it stops and
    // nothing of the app changes. Once moving, it finishes.
    int expected = Running;
    if (gate_.compare_exchange_strong(expected, StopAsked)) Host().background_cancel();
}

void Store::Shutdown() {
    shutting_down_ = true;
    want_list_ = false;
    icon_queue_.clear();
    CancelInstall();
}

// ---- The jobs, on the network's thread ----

void Store::RunList() {
    // What a power cut in the middle of an install left: its download and
    // its staged files. Backups are kept: they may be someone's only copy.
    Host().remove_path(kDownloadFile);
    const std::string staging = std::string(kStoreFolder) + "/staging";
    if (IsFolder(staging)) {
        RemoveTree(staging);
        Host().log("removed the staged files of an install that did not finish");
    }
    char error[200] = "";
    if (!Host().net_ready(error, sizeof(error))) {
        job_error_ = error;
        return;
    }
    std::uint32_t length = 0;
    if (!Host().http_get(kAppListUrl, list_buffer_.data(), static_cast<std::uint32_t>(list_buffer_.size()), &length, error,
                         sizeof(error))) {
        job_error_ = error;
        return;
    }
    std::size_t count = 0, skipped = 0;
    if (!parse_app_list(reinterpret_cast<const char*>(list_buffer_.data()), length, listings.data(), listings.size(), count,
                        skipped, job_error_))
        return;
    listings.resize(count);
    apps.assign(count, App{});
    for (std::size_t i = 0; i < count; ++i) apps[i].listing = &listings[i];
    std::sort(apps.begin(), apps.end(),
              [](const App& a, const App& b) { return CompareNames(*a.listing, *b.listing) < 0; });
    for (std::size_t i = 0; i < count; ++i) Refresh(i);
    Host().log("the list has %u apps (%u left out for breaking a rule)", static_cast<unsigned>(count),
               static_cast<unsigned>(skipped));
    job_ok_ = true;
}

void Store::RunIcon() {
    const AppListing& app = *apps[job_index_].listing;
    char error[200] = "";
    std::uint32_t length = 0;
    if (!Host().http_get(app.icon, icon_buffer_.data(), static_cast<std::uint32_t>(icon_buffer_.size()), &length, error,
                         sizeof(error))) {
        Host().log("no icon for %s: %s", app.id, error);
        return;
    }
    if (length > kAppIconMaxBytes) return;
    if (!decode_png(icon_buffer_.data(), length, PngInflate, kIconMaxSide, icon_rgba_, icon_width_, icon_height_,
                    job_error_)) {
        Host().log("the icon of %s is not a picture RiftWii reads: %s", app.id, job_error_.c_str());
        return;
    }
    job_ok_ = true;
}

void Store::RunInstall() {
    const AppListing& app = *apps[job_index_].listing;
    char error[200] = "";
    const std::string own = std::string("sd:/apps/") + app.id;
    const std::string staging = std::string(kStoreFolder) + "/staging";
    const auto clean_up = [&] {
        Host().remove_path(kDownloadFile);
        if (IsFolder(staging)) RemoveTree(staging);
    };
    // `why` is shown to the player; `logged`, when given, goes to the log
    // in English instead of a translated `why`.
    const auto fail = [&](const std::string& why, const char* logged = nullptr) {
        job_error_ = why;
        // Stopped only when Stop won before anything moved: a failure while
        // moving is reported as one, whatever was pressed.
        progress.SetStage(Stopped() ? InstallStage::Cancelled : InstallStage::Failed);
        clean_up();
        Host().log("%s was not installed: %s", app.id, logged ? logged : why.c_str());
    };
    if (!Host().net_ready(error, sizeof(error))) return fail(error);
    clean_up();
    if (!MakeParents(staging + "/")) return fail("the SD card's riftwii folder could not be written");

    // Room for the download and its unpacked files side by side (an
    // update's old files are moved, not copied), and a megabyte to spare:
    // said now, not as a failed write halfway through.
    const std::uint64_t needed = static_cast<std::uint64_t>(app.size) + app.unpacked_size + 1024u * 1024u;
    std::uint32_t free_kib = 0;
    if (RWP_HOST_HAS(&Host(), sd_free_kib) && Host().sd_free_kib(&free_kib) == 0) {
        Host().log("%s needs %u KiB on the SD card, %u KiB are free", app.id, static_cast<unsigned>(needed / 1024u),
                   static_cast<unsigned>(free_kib));
        if (static_cast<std::uint64_t>(free_kib) * 1024u < needed) {
            const std::uint64_t missing = needed - static_cast<std::uint64_t>(free_kib) * 1024u;
            return fail(tr("The SD card is too full. Make {1} of room and try again.", SizeText(missing)),
                        "the SD card is too full");
        }
    }

    // Download, hashed on the way, and no longer than the list says it is.
    progress.SetStage(InstallStage::Download);
    std::uint8_t digest[32];
    const auto downloaded = [](std::uint32_t done, void* arg) { static_cast<Store*>(arg)->progress.SetDone(done); };
    if (!Host().http_download(app.url, kDownloadFile, app.size, digest, downloaded, this, error, sizeof(error)))
        return fail(Stopped() ? "stopped" : error);
    progress.SetStage(InstallStage::Check);
    if (Hex(digest, sizeof(digest)) != app.sha256)
        return fail("the download is not the file the app list approved (it may have been changed)");

    // Everything is first written into the staging folder, where a failure,
    // a Stop or a full card changes nothing of the app. Only then are the
    // files put in place (Commit, below).
    std::vector<Move> moves;
    std::vector<AppRecordFile> new_files;  // what each move puts on the card
    if (app.format == AppFormat::Dol) {
        progress.Set(InstallStage::Unpack, 0, 1);
        std::uint32_t crc = 0;
        if (!FileCrc(kDownloadFile, crc)) return fail("the download could not be read back");
        moves.push_back({kDownloadFile, own + "/boot.dol"});
        new_files.push_back({std::string("apps/") + app.id + "/boot.dol", app.size, crc});
        progress.Set(InstallStage::Unpack, 1, 1);
    } else {
        FILE* zip = std::fopen(kDownloadFile, "rb");
        if (!zip) return fail("the download could not be read back");
        rwp_path_info info;
        Host().path_info(kDownloadFile, &info);
        const ZipReadAt read = [&](std::uint64_t offset, std::uint8_t* out, std::size_t bytes) {
            return fseeko(zip, static_cast<off_t>(offset), SEEK_SET) == 0 && std::fread(out, 1, bytes, zip) == bytes;
        };
        std::vector<ZipEntry> entries;
        std::vector<AppInstallStep> plan;
        std::string why;
        if (!zip_read_directory_at(read, info.size, entries, why, kMaxZipEntries) ||
            !app_plan_install(app, entries, plan, why)) {
            std::fclose(zip);
            return fail(why);
        }
        progress.Set(InstallStage::Unpack, 0, static_cast<std::uint32_t>(plan.size()));
        ZlibStream inflater;
        for (std::size_t i = 0; i < plan.size(); ++i) {
            const std::string staged = staging + "/" + std::to_string(i) + ".part";
            FILE* out = std::fopen(staged.c_str(), "wb");
            if (!out) {
                std::fclose(zip);
                return fail("a file could not be written to the SD card");
            }
            bool write_failed = false;
            // Stop is heard inside a file too, so a big one does not hold it up.
            const ZipSink write = [&](const std::uint8_t* data, std::size_t size) {
                if (Stopped()) return false;
                write_failed = std::fwrite(data, 1, size, out) != size;
                return !write_failed;
            };
            const bool unpacked = zip_extract_at(read, entries[plan[i].entry], &inflater, write, why);
            const bool closed = std::fclose(out) == 0;
            if (Stopped() || !unpacked || !closed) {
                std::fclose(zip);
                if (Stopped()) return fail("stopped");
                return fail(write_failed || !closed ? "the SD card could not be written (is it full?)" : why);
            }
            moves.push_back({staged, "sd:/" + plan[i].path});
            new_files.push_back({plan[i].path, entries[plan[i].entry].unpacked_size, entries[plan[i].entry].crc});
            progress.SetDone(static_cast<std::uint32_t>(i + 1));
        }
        std::fclose(zip);
    }

    // What is on the card now decides what goes in: a file the app or the
    // player changed since the store put it there (settings, saves), or one
    // in a data folder the store never put there, is kept
    // (app_install_replaces). The note lists what the store put there.
    AppRecord old_record;
    const Note old_note = ReadRecord(app, old_record);
    if (old_note == Note::Unreadable)
        return fail("the store's note of this app could not be read, so nothing was changed",
                    "its note in sd:/riftwii/apps could not be read");
    const bool has_old = old_note == Note::Read;
    std::vector<Move> placed;
    std::vector<AppRecordFile> listed;
    for (std::size_t i = 0; i < moves.size(); ++i) {
        const std::string& path = new_files[i].path;
        const AppRecordFile* before = has_old ? app_record_find(old_record, path) : nullptr;
        const AppFileOnCard on_card = OnCard(path, before);
        if (!app_install_replaces(app, path, on_card, has_old ? &old_record : nullptr)) {
            Host().log("%s: kept sd:/%s (%s)", app.id, path.c_str(),
                       on_card == AppFileOnCard::Changed ? "changed since it was installed" : "not put there by the store");
            if (before != nullptr) listed.push_back(*before);
            continue;
        }
        placed.push_back(moves[i]);
        listed.push_back(new_files[i]);
    }
    // The old install's files the new one does not have are still the store's.
    if (has_old) {
        for (const AppRecordFile& file : old_record.files) {
            bool there = false;
            for (const AppRecordFile& now : listed) there = there || now.path == file.path;
            if (!there) listed.push_back(file);
        }
    }
    // The note must hold all of it: an entry the reader would drop would
    // leave that file behind at a removal.
    const std::string record = app_record_text(app, listed);
    if (listed.size() > kAppMaxFiles || record.size() > kAppRecordMaxBytes)
        return fail("the app has more files than the store can keep track of", "its note would be too long");
    moves.swap(placed);

    // The store's note, staged with the files: which file this is, for
    // updates, and what it put on the card.
    const std::string note_staged = staging + "/note.part";
    FILE* note = std::fopen(note_staged.c_str(), "wb");
    const bool noted = note && std::fwrite(record.data(), 1, record.size(), note) == record.size();
    if ((note && std::fclose(note) != 0) || !noted) return fail("the SD card could not be written (is it full?)");
    moves.push_back({note_staged, RecordPath(app)});

    // Stop and the move in race here; whichever takes the gate first wins.
    int expected = Running;
    if (!gate_.compare_exchange_strong(expected, Committing)) return fail("stopped");
    progress.SetStage(InstallStage::Finish);
    const std::string not_placed = Commit(moves, own, app.id);
    if (!not_placed.empty()) return fail(not_placed);
    clean_up();
    progress.SetStage(InstallStage::Done);
    Host().log("%s %s installed (%u files placed)", app.id, app.version, static_cast<unsigned>(moves.size() - 1));
    job_ok_ = true;
}

void Store::RunRemove() {
    const AppListing& app = *apps[job_index_].listing;
    // The files the store put on the card and nobody changed since; what
    // the app wrote (its saves, its settings) stays, and the player is
    // asked about it after (job_leftovers_).
    AppRecord record;
    const Note note = ReadRecord(app, record);
    if (note == Note::Unreadable) {
        // Without it the store cannot tell its files from the app's saves.
        job_error_ = "the store's note of this app could not be read, so nothing was removed";
        Host().log("%s was not removed: its note in sd:/riftwii/apps could not be read", app.id);
        return;
    }
    const bool has_record = note == Note::Read;
    std::vector<AppFileOnCard> on_card;
    std::vector<AppRecordFile> kept;
    if (has_record) {
        for (const AppRecordFile& file : record.files) {
            on_card.push_back(OnCard(file.path, &file));
            if (on_card.back() == AppFileOnCard::Changed) kept.push_back(file);
        }
    }
    std::string not_removed;
    for (const std::string& path : app_remove_files(app, has_record ? &record : nullptr, on_card)) {
        const std::string top = AppTop(app, path);
        if (top.empty() || !Exists("sd:/" + path)) continue;
        if (Host().remove_path(("sd:/" + path).c_str()) != 0) {
            if (not_removed.empty()) not_removed = path;
            continue;
        }
        PruneEmpty(path, top);
    }
    if (!not_removed.empty()) {
        Host().log("%s: sd:/%s could not be removed", app.id, not_removed.c_str());
        job_error_ = "some of its files could not be removed";
        return;
    }
    // What is left of the app: its own folder (settings it wrote there),
    // its data folders.
    job_leftovers_.clear();
    if (IsFolder(std::string("sd:/apps/") + app.id)) job_leftovers_.push_back(std::string("sd:/apps/") + app.id);
    for (std::uint8_t i = 0; i < app.data_folder_count; ++i) {
        const std::string data = std::string("sd:/") + app.data_folders[i];
        if (IsFolder(data) && HasEntries(data)) job_leftovers_.push_back(data);
    }
    if (job_leftovers_.empty()) {
        Host().remove_path(RecordPath(app).c_str());
    } else if (!ReplaceText(RecordPath(app), app_record_text(app, kept, true))) {
        // The old note stays whole: a later install still tells the kept
        // files from its own by it.
        Host().log("%s: the note of its kept files could not be written; the old one stays", app.id);
    }
    Host().log("%s removed (%u folders of saves and settings left)", app.id, static_cast<unsigned>(job_leftovers_.size()));
    job_ok_ = true;
}

void Store::RunRemoveData() {
    const AppListing& app = *apps[job_index_].listing;
    std::vector<std::string> folders{std::string("sd:/apps/") + app.id};
    for (std::uint8_t i = 0; i < app.data_folder_count; ++i) folders.push_back(std::string("sd:/") + app.data_folders[i]);
    for (const std::string& folder : folders) {
        if (IsFolder(folder) && !RemoveTree(folder)) {
            job_error_ = "some of its files could not be removed";
            Host().log("%s: %s could not be removed whole", app.id, folder.c_str());
            return;
        }
    }
    Host().remove_path(RecordPath(app).c_str());
    Host().log("%s: its saves and settings were removed", app.id);
    job_ok_ = true;
}

namespace {

void RunJob(void* arg) {
    Store* store = static_cast<Store*>(arg);
    store->RunCurrent();
}

}  // namespace

void Store::RunCurrent() {
    switch (job_) {
        case Job::List: RunList(); break;
        case Job::Icon: RunIcon(); break;
        case Job::Install: RunInstall(); break;
        case Job::Remove: RunRemove(); break;
        case Job::RemoveData: RunRemoveData(); break;
        case Job::None: break;
    }
}

}  // namespace riftwii::appstore
