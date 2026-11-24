// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/appcatalog.hpp"
#include "riftwii/applist.hpp"
#include "riftwii/plugin_api.h"

// The app store's data and its jobs on RiftWii's network thread
// (rwp_host's background_start): the list, the icons and the installs.
// One job runs at a time. The menu thread starts a job and looks at its
// answer once background_running() says it ended; while it runs, the job
// writes only its own fields (and the progress counters the page shows).
namespace riftwii::appstore {

const rwp_host& Host();
// The menu's translation of `english`, with {1}, {2} filled in.
std::string tr(const char* english);
std::string tr(const char* english, const std::string& one);
std::string tr(const char* english, const std::string& one, const std::string& two);
// A size as the player's language writes it ("2.4 MB", "2,4 Mo").
std::string SizeText(std::uint64_t bytes);

struct App {
    const AppListing* listing = nullptr;
    AppState state = AppState::Get;
    std::uint32_t texture = 0;     // the icon, once fetched
    bool icon_failed = false;      // not fetched again this visit
    std::uint32_t icon_used = 0;   // the frame it was last drawn, for dropping old icons
};

enum class ListState { Loading, Ready, Failed };

// Finish: the staged files being put in place, which Stop no longer interrupts.
enum class InstallStage { Download, Check, Unpack, Finish, Done, Failed, Cancelled };
// Written by the job, read by the page while it runs. The three fields go
// together (a byte count is never read against a file total): the writer
// makes the sequence number odd while it changes them, and a reader takes
// a copy only when the number was even and the same before and after
// (a sequence lock). One writer at a time: the menu before the job starts,
// then the job.
class InstallProgress {
public:
    struct Snapshot {
        InstallStage stage = InstallStage::Download;
        std::uint32_t done = 0;   // bytes (download) or files (unpack)
        std::uint32_t total = 0;
    };
    // One thread writes at a time (the menu before an install starts, then
    // the install's own); the writer keeps its own copy of what it wrote,
    // so it never has to read the shared one.
    void Set(InstallStage stage, std::uint32_t done, std::uint32_t total) {
        written_ = Snapshot{stage, done, total};
        sequence_.fetch_add(1);
        stage_.store(stage);
        done_.store(done);
        total_.store(total);
        sequence_.fetch_add(1);
    }
    void SetStage(InstallStage stage) { Set(stage, written_.done, written_.total); }
    void SetDone(std::uint32_t done) { Set(written_.stage, done, written_.total); }

    // The menu's thread reads (only it). It runs at a higher priority than
    // the install's thread: had it caught a write half done and waited for
    // it to end, the writer would never get to finish it. So it tries a few
    // times and otherwise keeps the last whole snapshot it read.
    Snapshot Read() const {
        for (int attempt = 0; attempt < 4; ++attempt) {
            const std::uint32_t before = sequence_.load();
            if ((before & 1u) != 0) continue;
            Snapshot copy;
            copy.stage = stage_.load();
            copy.done = done_.load();
            copy.total = total_.load();
            if (sequence_.load() == before) {
                last_read_ = copy;
                return copy;
            }
        }
        return last_read_;
    }

private:
    std::atomic<std::uint32_t> sequence_{0};
    std::atomic<InstallStage> stage_{InstallStage::Download};
    std::atomic<std::uint32_t> done_{0};
    std::atomic<std::uint32_t> total_{0};
    Snapshot written_;            // the install's thread's own
    mutable Snapshot last_read_;  // the menu's thread's own
};

class Store {
public:
    // The list's records and the store's view of them.
    std::vector<AppListing> listings;
    std::vector<App> apps;  // sorted by name
    ListState list_state = ListState::Loading;
    std::string list_error;
    bool changed = false;   // something was installed or removed this visit

    // Starts the list's download (or another try after it failed).
    void StartList();
    // Called every frame: takes a finished job's answer, starts the next icon.
    void Pump(std::uint32_t frame);
    // The icons of these apps are wanted now (the page shows them).
    void WantIcons(const std::vector<std::size_t>& shown, std::uint32_t frame);

    // An install (or update) of apps[index]: false when a job is running.
    bool StartInstall(std::size_t index);
    // Removes the files the store put there and nobody changed; the app's
    // saves and settings stay (finished_leftovers says where).
    bool StartRemove(std::size_t index);
    // Then, when the player says so: its folder and data folders, whole.
    bool StartRemoveData(std::size_t index);
    bool Busy() const { return job_ != Job::None; }
    // Running, or waiting for an icon's download to end first.
    bool InstallRunning(std::size_t index) const {
        return ((job_ == Job::Install || job_ == Job::Remove || job_ == Job::RemoveData) && job_index_ == index) ||
               queued_install_ == index;
    }
    void CancelInstall();
    // The store is closing: no new job starts (a queued icon stays
    // queued) and an install is stopped. Busy() then says when the last
    // one has ended; Pump() takes its answer.
    void Shutdown();
    InstallProgress progress;
    // The last install or remove's end: the app, and why it failed.
    std::size_t finished_index = static_cast<std::size_t>(-1);
    bool finished_ok = false;
    bool finished_remove = false;
    std::string finished_error;
    // A removal's: the folders of saves and settings it left ("sd:/snes9xgx").
    std::vector<std::string> finished_leftovers;
    // Read by the page once, then cleared.
    bool TakeFinished() {
        const bool any = finished_pending_;
        finished_pending_ = false;
        return any;
    }

    std::size_t Find(const char* id) const;
    // Re-reads what the card has of apps[index].
    void Refresh(std::size_t index);
    // The installed apps (and those with updates) in the store's order.
    std::size_t UpdateCount() const;

    // The job that was started, on the network thread.
    void RunCurrent();
    void RunList();
    void RunIcon();
    void RunInstall();
    void RunRemove();
    void RunRemoveData();

private:
    enum class Job { None, List, Icon, Install, Remove, RemoveData };
    void Start(Job job, std::size_t index);
    void Finish();

    Job job_ = Job::None;
    std::size_t job_index_ = 0;
    bool job_ok_ = false;
    std::string job_error_;
    std::vector<std::string> job_leftovers_;
    // An install's Stop against its move into place: Running, then either
    // StopAsked (the page's Stop won: nothing is moved) or Committing (the
    // job's move won: Stop is no longer taken). One compare-and-swap each
    // way, so the two can never both win.
    enum Gate : int { Running = 0, StopAsked = 1, Committing = 2 };
    std::atomic<int> gate_{Running};
    bool Stopped() const { return gate_.load() == StopAsked; }
    bool shutting_down_ = false;
    bool finished_pending_ = false;
    bool want_list_ = false;
    // An install asked for while an icon (or the list) was downloading:
    // it starts as soon as that job ends, before any further icon.
    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);
    std::size_t queued_install_ = kNone;

    // The jobs' download buffers, taken on the menu thread before they start.
    std::vector<std::uint8_t> list_buffer_;
    std::vector<std::uint8_t> icon_buffer_;
    // The icon job's result: RGBA rows the menu thread turns into a texture.
    std::vector<std::uint8_t> icon_rgba_;
    std::uint32_t icon_width_ = 0, icon_height_ = 0;
    std::vector<std::size_t> icon_queue_;
    std::size_t textures_ = 0;
};

}  // namespace riftwii::appstore
