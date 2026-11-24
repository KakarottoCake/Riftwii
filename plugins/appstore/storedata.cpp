// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "storedata.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

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

// A staged file and where it goes.
struct Move {
    std::string staged, target;
};

// The staged files put in place. Each old file is kept as <file>.bak until
// every new one is in, and put back if one cannot be moved, so the app is
// the old version or the new one, never a mix. A power cut in the middle
// leaves the .bak files, and no file lost. A fresh install that fails
// leaves no folder behind.
bool Commit(const std::vector<Move>& moves, const std::string& own) {
    const bool fresh = !IsFolder(own);
    std::vector<std::string> backups;
    bool ok = true;
    for (const Move& move : moves) {
        std::string backup;
        if (!MakeParents(move.target)) {
            ok = false;
            break;
        }
        if (Exists(move.target)) {
            backup = move.target + ".bak";
            Host().remove_path(backup.c_str());
            if (Host().rename_path(move.target.c_str(), backup.c_str()) != 0) {
                ok = false;
                break;
            }
        }
        if (Host().rename_path(move.staged.c_str(), move.target.c_str()) != 0) {
            if (!backup.empty()) Host().rename_path(backup.c_str(), move.target.c_str());
            ok = false;
            break;
        }
        backups.push_back(backup);
    }
    if (!ok) {
        for (std::size_t i = backups.size(); i-- > 0;) {
            Host().remove_path(moves[i].target.c_str());
            if (!backups[i].empty()) Host().rename_path(backups[i].c_str(), moves[i].target.c_str());
        }
        if (fresh && IsFolder(own)) RemoveTree(own);
        return false;
    }
    for (const std::string& backup : backups) {
        if (!backup.empty()) Host().remove_path(backup.c_str());
    }
    return true;
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
    cancel_ = false;
    if (Host().background_start(RunJob, this) != 0) job_ = Job::None;  // asked again next frame
}

void Store::Pump(std::uint32_t frame) {
    if (job_ != Job::None && !Host().background_running()) Finish();
    if (job_ != Job::None) return;
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
            Refresh(index);
            finished_index = index;
            finished_ok = job_ok_;
            finished_remove = job == Job::Remove;
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
    const bool folder = IsFolder(std::string("sd:/apps/") + app.id);
    char text[512];
    const std::string record_path = std::string(kStoreFolder) + "/" + app.id + ".txt";
    const std::int32_t read = Host().file_read(record_path.c_str(), 0, text, sizeof(text) - 1);
    AppRecord record;
    bool has_record = false;
    if (read > 0) {
        text[read] = '\0';
        has_record = app_record_parse(text, record);
    }
    apps[index].state = app_state(app, folder, has_record ? &record : nullptr);
}

std::size_t Store::UpdateCount() const {
    std::size_t count = 0;
    for (const App& app : apps) {
        if (app.state == AppState::Update) ++count;
    }
    return count;
}

bool Store::StartInstall(std::size_t index) {
    if (job_ != Job::None || index >= apps.size() || !app_installable(*apps[index].listing)) return false;
    progress.stage = InstallStage::Download;
    progress.done = 0;
    progress.total = apps[index].listing->size;
    Start(Job::Install, index);
    return job_ == Job::Install;
}

bool Store::StartRemove(std::size_t index) {
    if (job_ != Job::None || index >= apps.size()) return false;
    Start(Job::Remove, index);
    return job_ == Job::Remove;
}

void Store::CancelInstall() {
    if (job_ != Job::Install) return;
    cancel_ = true;
    Host().background_cancel();
}

// ---- The jobs, on the network's thread ----

void Store::RunList() {
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
    const auto fail = [&](const std::string& why) {
        job_error_ = why;
        progress.stage = cancel_ ? InstallStage::Cancelled : InstallStage::Failed;
        clean_up();
        Host().log("%s was not installed: %s", app.id, why.c_str());
    };
    if (!Host().net_ready(error, sizeof(error))) return fail(error);
    clean_up();
    if (!MakeParents(staging + "/")) return fail("the SD card's riftwii folder could not be written");

    // Download, hashed on the way, and no longer than the list says it is.
    progress.stage = InstallStage::Download;
    std::uint8_t digest[32];
    const auto downloaded = [](std::uint32_t done, void* arg) { static_cast<Store*>(arg)->progress.done = done; };
    if (!Host().http_download(app.url, kDownloadFile, app.size, digest, downloaded, this, error, sizeof(error)))
        return fail(cancel_ ? "stopped" : error);
    progress.stage = InstallStage::Check;
    if (Hex(digest, sizeof(digest)) != app.sha256)
        return fail("the download is not the file the app list approved (it may have been changed)");

    // Everything is first written into the staging folder, where a failure,
    // a Stop or a full card changes nothing of the app. Only then are the
    // files put in place (Commit, below).
    std::vector<Move> moves;
    if (app.format == AppFormat::Dol) {
        progress.stage = InstallStage::Unpack;
        progress.total = 1;
        moves.push_back({kDownloadFile, own + "/boot.dol"});
        progress.done = 1;
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
        progress.stage = InstallStage::Unpack;
        progress.done = 0;
        progress.total = static_cast<std::uint32_t>(plan.size());
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
                if (cancel_) return false;
                write_failed = std::fwrite(data, 1, size, out) != size;
                return !write_failed;
            };
            const bool unpacked = zip_extract_at(read, entries[plan[i].entry], &inflater, write, why);
            const bool closed = std::fclose(out) == 0;
            if (cancel_ || !unpacked || !closed) {
                std::fclose(zip);
                if (cancel_) return fail("stopped");
                return fail(write_failed || !closed ? "the SD card could not be written (is it full?)" : why);
            }
            moves.push_back({staged, "sd:/" + plan[i].path});
            progress.done = progress.done + 1;
        }
        std::fclose(zip);
    }

    // The store's note, staged with the files: which file this is, for updates.
    const std::string note_staged = staging + "/note.part";
    const std::string record = app_record_text(app);
    FILE* note = std::fopen(note_staged.c_str(), "wb");
    const bool noted = note && std::fwrite(record.data(), 1, record.size(), note) == record.size();
    if ((note && std::fclose(note) != 0) || !noted) return fail("the SD card could not be written (is it full?)");
    moves.push_back({note_staged, std::string(kStoreFolder) + "/" + app.id + ".txt"});

    progress.stage = InstallStage::Finish;
    if (!Commit(moves, own)) return fail("the app's files could not be put in place; the old ones were put back");
    clean_up();
    progress.stage = InstallStage::Done;
    Host().log("%s %s installed (%u files)", app.id, app.version, static_cast<unsigned>(moves.size() - 1));
    job_ok_ = true;
}

void Store::RunRemove() {
    const AppListing& app = *apps[job_index_].listing;
    const std::string own = std::string("sd:/apps/") + app.id;
    // The app's own folder only: its data folders hold its saves.
    if (IsFolder(own) && !RemoveTree(own)) {
        job_error_ = "some of its files could not be removed";
        return;
    }
    Host().remove_path((std::string(kStoreFolder) + "/" + app.id + ".txt").c_str());
    Host().log("%s removed", app.id);
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
        case Job::None: break;
    }
}

}  // namespace riftwii::appstore
