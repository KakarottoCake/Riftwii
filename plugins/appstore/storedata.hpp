// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

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

struct App {
    const AppListing* listing = nullptr;
    AppState state = AppState::Get;
    std::uint32_t texture = 0;     // the icon, once fetched
    bool icon_failed = false;      // not fetched again this visit
    std::uint32_t icon_used = 0;   // the frame it was last drawn, for dropping old icons
};

enum class ListState { Loading, Ready, Failed };

enum class InstallStage { Download, Check, Unpack, Done, Failed, Cancelled };
struct InstallProgress {
    volatile InstallStage stage = InstallStage::Download;
    volatile std::uint32_t done = 0;   // bytes (download) or files (unpack)
    volatile std::uint32_t total = 0;
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
    bool StartRemove(std::size_t index);
    bool Busy() const { return job_ != Job::None; }
    bool InstallRunning(std::size_t index) const {
        return (job_ == Job::Install || job_ == Job::Remove) && job_index_ == index;
    }
    void CancelInstall();
    InstallProgress progress;
    // The last install or remove's end: the app, and why it failed.
    std::size_t finished_index = static_cast<std::size_t>(-1);
    bool finished_ok = false;
    bool finished_remove = false;
    std::string finished_error;
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

private:
    enum class Job { None, List, Icon, Install, Remove };
    void Start(Job job, std::size_t index);
    void Finish();

    Job job_ = Job::None;
    std::size_t job_index_ = 0;
    bool job_ok_ = false;
    std::string job_error_;
    volatile bool cancel_ = false;
    bool finished_pending_ = false;
    bool want_list_ = false;

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
