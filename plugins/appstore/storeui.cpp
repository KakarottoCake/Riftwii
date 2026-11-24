// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "storeui.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "storedata.hpp"

namespace riftwii::appstore {
namespace {

// Text sizes: nothing smaller than 18 pixels of the 480-line picture, so
// it stays readable on a small TV and on 480i.
constexpr int kTitleSize = 30;
constexpr int kNameSize = 20;
constexpr int kSmallSize = 18;
constexpr int kBodySize = 20;

constexpr int kBand = 72;        // the title band's height
constexpr int kAppBand = 118;    // an app page's band, with its facts
constexpr int kBarTop = 404;     // where the page's bottom band (and its prompts) begins
constexpr int kGap = 14;

constexpr std::uint32_t kWarn = 0xB45F06FFu;  // an update waiting, readable on the light cards
constexpr std::uint32_t kNoteFill = 0xFDF3DCFFu;
constexpr std::uint32_t kNoteInk = 0x6B4A06FFu;
constexpr std::uint32_t kIconPlace = 0xD8E2ECFFu;

// The frame's draw list. Texts are kept until the frame is handed over.
class Frame {
public:
    void Clear() {
        commands_.clear();
        texts_.clear();
    }
    void Card(int x, int y, int w, int h, bool lit, bool primary = false) {
        rwp_draw& d = Add(RWP_DRAW_CARD, x, y, w, h);
        d.flags = static_cast<std::uint8_t>((lit ? RWP_LIT : 0) | (primary ? RWP_PRIMARY : 0));
    }
    void Text(int x, int y, int size, std::uint32_t color, const std::string& text, std::uint8_t align = RWP_ALIGN_LEFT,
              int width = 0, int lines = 0) {
        if (text.empty()) return;
        texts_.push_back(text);
        rwp_draw& d = Add(RWP_DRAW_TEXT, x, y, width, lines);
        d.size = static_cast<std::uint8_t>(size);
        d.color = color;
        d.align = align;
        d.text = texts_.back().c_str();
    }
    void Picture(int x, int y, int w, int h, std::uint32_t texture) {
        rwp_draw& d = Add(RWP_DRAW_TEXTURE, x, y, w, h);
        d.arg = texture;
    }
    void Rect(int x, int y, int w, int h, std::uint32_t color, bool round = false) {
        rwp_draw& d = Add(RWP_DRAW_RECT, x, y, w, h);
        d.color = color;
        d.arg = round ? 1 : 0;
    }
    void Skin(std::uint32_t which, int x, int y, int w, int h, bool lit = false) {
        rwp_draw& d = Add(RWP_DRAW_SKIN, x, y, w, h);
        d.arg = which;
        d.flags = lit ? RWP_LIT : 0;
    }
    void Progress(int x, int y, int w, int h, std::uint32_t permille) {
        rwp_draw& d = Add(RWP_DRAW_PROGRESS, x, y, w, h);
        d.arg = permille;
    }
    void Qr(int x, int y, int side, const std::string& text) {
        texts_.push_back(text);
        rwp_draw& d = Add(RWP_DRAW_QR, x, y, side, side);
        d.text = texts_.back().c_str();
    }
    void Prompt(std::uint32_t key, const std::string& label) {
        texts_.push_back(label);
        rwp_draw& d = Add(RWP_DRAW_PROMPT, 0, 0, 0, 0);
        d.arg = key;
        d.text = texts_.back().c_str();
    }
    void Button(int x, int y, int size, std::uint32_t key) {
        rwp_draw& d = Add(RWP_DRAW_BUTTON, x, y, size, size);
        d.arg = key;
    }
    const rwp_draw* Data() const { return commands_.data(); }
    std::uint32_t Count() const { return static_cast<std::uint32_t>(commands_.size()); }

private:
    rwp_draw& Add(std::uint8_t kind, int x, int y, int w, int h) {
        rwp_draw d;
        std::memset(&d, 0, sizeof(d));
        d.kind = kind;
        d.x = static_cast<std::int16_t>(x);
        d.y = static_cast<std::int16_t>(y);
        d.w = static_cast<std::int16_t>(w);
        d.h = static_cast<std::int16_t>(h);
        d.alpha = 255;
        commands_.push_back(d);
        return commands_.back();
    }
    std::vector<rwp_draw> commands_;
    std::deque<std::string> texts_;  // a deque: its strings never move
};

// Something the pointer can point at, and A press.
struct Hit {
    int x, y, w, h;
    int id;
};

int Width(const std::string& text, int size) { return Host().text_width(text.c_str(), static_cast<std::uint32_t>(size)); }

std::string Plural(std::size_t count, const char* one, const char* many) {
    return count == 1 ? tr(one) : tr(many, std::to_string(count));
}

enum class Page { Front, Grid, App };
enum class GridKind { Category, Search, Mine };

// The ids of the things on a page that are not tiles.
constexpr int kHitPrevious = 1002;
constexpr int kHitNext = 1003;
constexpr int kHitAction = 1004;
constexpr int kHitNewer = 1005;
constexpr int kHitPage = 1006;
constexpr int kHitRetry = 1007;

class StoreScreen {
public:
    explicit StoreScreen(const rwp_screen_info& info) : info_(info) {
        left_ = info.safe_x + 8;
        right_ = info.safe_x + info.safe_w - 8;
        middle_ = info.left + info.width / 2;
    }

    std::int32_t Run() {
        store_.StartList();
        bool leave = false;
        while (!leave) {
            store_.Pump(input_.frame);
            TakeFinishedInstall();
            frame_.Clear();
            hits_.clear();
            prompts_.clear();
            Draw();
            DrawPrompts();
            rwp_input input;
            if (Host().screen_frame(frame_.Data(), frame_.Count(), &input) != 0) break;
            input_ = input;
            leave = Handle();
        }
        // An install still running is stopped (nothing of it is kept); a list,
        // an icon or a removal ends on its own. Its end is waited for with a
        // page that says so: the plugin may not be unloaded under its job.
        store_.Shutdown();
        while (store_.Busy()) {
            store_.Pump(input_.frame);  // takes the last job's answer, starts nothing
            frame_.Clear();
            frame_.Skin(RWP_SKIN_TITLE_BAND, info_.left, 0, info_.width, kBand);
            Message(tr("Finishing..."));
            frame_.Progress(middle_ - 140, 236, 280, 14, (input_.frame * 12) % 1000);
            rwp_input input;
            if (Host().screen_frame(frame_.Data(), frame_.Count(), &input) == 0) input_ = input;
        }
        return RWP_LAUNCH_DONE;
    }

    bool Changed() const { return store_.changed; }

private:
    // ---- Drawing ----

    void Draw() {
        switch (page_) {
            case Page::Front: DrawFront(); break;
            case Page::Grid: DrawGrid(); break;
            case Page::App: DrawApp(); break;
        }
        frame_.Skin(RWP_SKIN_FOOTER, info_.left, 0, info_.width, 0);
    }

    void Band(const std::string& title, int height = kBand) {
        frame_.Skin(RWP_SKIN_TITLE_BAND, info_.left, 0, info_.width, height);
        // Room on the right for a grid's page and its arrows.
        if (!title.empty()) frame_.Text(left_, 20, kTitleSize, RWP_COLOR_ON_BAND, title, RWP_ALIGN_LEFT, right_ - left_ - 280);
    }

    void Prompt(std::uint32_t key, const std::string& label) { prompts_.push_back({key, label}); }

    // The menu's own prompts, from the right edge inwards in the order
    // named: B Back first, at the edge, as on the menu's pages.
    void DrawPrompts() {
        for (const auto& prompt : prompts_) frame_.Prompt(prompt.first, prompt.second);
    }

    void Message(const std::string& text, int y = 190) {
        frame_.Text(middle_, y, 22, RWP_COLOR_INK, text, RWP_ALIGN_CENTER, right_ - left_, 0);
    }

    // The store front: the categories as tiles.
    void DrawFront() {
        Band(tr("Get apps"));
        if (!ListReady()) return;

        CountCategories();
        // Four across on a 16:9 menu; three on a 4:3 one, where four would
        // cut the longer names ("Online services"), in lower rows.
        const int columns = FrontColumns();
        const int rows = (static_cast<int>(shown_categories_.size()) + columns - 1) / columns;
        const int gap = rows > 3 ? 10 : kGap;
        const int h = rows > 3 ? 56 : 76;
        const int w = (right_ - left_ - (columns - 1) * gap) / columns;
        const int name_y = rows > 3 ? 6 : 12, count_y = rows > 3 ? 30 : 44;
        for (std::size_t k = 0; k < shown_categories_.size(); ++k) {
            const int column = static_cast<int>(k) % columns, row = static_cast<int>(k) / columns;
            const int tx = left_ + column * (w + gap), ty = 92 + row * (h + gap);
            const std::size_t category = shown_categories_[k];
            const bool lit = focus_ == static_cast<int>(k);
            frame_.Card(tx, ty, w, h, lit);
            frame_.Text(tx + w / 2, ty + name_y, kNameSize, RWP_COLOR_INK, tr(categories_[category].name), RWP_ALIGN_CENTER,
                        w - 12);
            frame_.Text(tx + w / 2, ty + count_y, kSmallSize, RWP_COLOR_INK_SOFT,
                        Plural(category_counts_[category], "1 app", "{1} apps"), RWP_ALIGN_CENTER, w - 12);
            hits_.push_back({tx, ty, w, h, static_cast<int>(k)});
        }
        std::size_t installed = 0;
        for (const App& app : store_.apps) {
            if (app.state == AppState::Installed || app.state == AppState::Update) ++installed;
        }
        frame_.Text(middle_, 92 + std::max(rows, 3) * (h + gap) - 2, kSmallSize, RWP_COLOR_INK_SOFT,
                    Plural(store_.apps.size(), "1 app", "{1} apps") + "  \xC2\xB7  " +
                        tr("{1} from here on the SD card", std::to_string(installed)),
                    RWP_ALIGN_CENTER);
        // My apps and Search sit with the other buttons on the bottom band,
        // not beside the title, where a longer translation covered it.
        const std::size_t updates = store_.UpdateCount();
        Prompt(RWP_KEY_B, tr("Back"));
        Prompt(RWP_KEY_A, tr("Open"));
        Prompt(RWP_KEY_ONE, tr("Search"));
        Prompt(RWP_KEY_TWO, updates ? tr("My apps ({1})", std::to_string(updates)) : tr("My apps"));
    }

    bool ListReady() {
        if (store_.list_state == ListState::Ready) return true;
        if (store_.list_state == ListState::Loading) {
            Message(tr("Getting the list of apps..."));
            const std::uint32_t sweep = (input_.frame * 12) % 1000;
            frame_.Progress(middle_ - 140, 236, 280, 14, sweep);
        } else {
            Message(tr("Could not get the list of apps: {1}", store_.list_error), 170);
            const int w = Width(tr("Try again"), kNameSize) + 64;
            const int x = middle_ - w / 2;
            frame_.Card(x, 260, w, 48, true);
            frame_.Text(middle_, 272, kNameSize, RWP_COLOR_INK, tr("Try again"), RWP_ALIGN_CENTER);
            hits_.push_back({x, 260, w, 48, kHitRetry});
            Prompt(RWP_KEY_A, tr("Try again"));
        }
        Prompt(RWP_KEY_B, tr("Back"));
        return false;
    }

    void CountCategories() {
        std::size_t count = 0;
        categories_ = app_categories(count);
        category_counts_.assign(count, 0);
        for (const App& app : store_.apps) ++category_counts_[app_category_index(app.listing->category)];
        shown_categories_.clear();
        for (std::size_t i = 0; i < count; ++i) {
            if (category_counts_[i] > 0) shown_categories_.push_back(i);
        }
    }

    int FrontColumns() const { return info_.widescreen ? 4 : 3; }
    int GridColumns() const { return info_.widescreen ? 4 : 3; }
    int PerPage() const { return GridColumns() * 2; }
    int PageCount() const {
        const int count = static_cast<int>(grid_.size());
        return count == 0 ? 1 : (count + PerPage() - 1) / PerPage();
    }

    // A grid of apps: a category, search results or the apps on the card.
    void DrawGrid() {
        Band(grid_title_);
        const int pages = PageCount();
        // The page and its arrows at the band's right.
        if (pages > 1) {
            const std::string text = tr("Page {1} of {2}", std::to_string(grid_page_ + 1), std::to_string(pages));
            const int text_w = Width(text, kNameSize);
            const int next_x = right_ - 44;
            const int previous_x = next_x - 12 - text_w - 12 - 44;
            frame_.Skin(RWP_SKIN_ARROW_LEFT, previous_x, 14, 44, 44, focus_ == kHitPrevious);
            frame_.Text(next_x - 12 - text_w, 26, kNameSize, RWP_COLOR_INK, text);
            frame_.Skin(RWP_SKIN_ARROW_RIGHT, next_x, 14, 44, 44, focus_ == kHitNext);
            hits_.push_back({previous_x, 14, 44, 44, kHitPrevious});
            hits_.push_back({next_x, 14, 44, 44, kHitNext});
        }
        if (grid_.empty()) {
            Message(grid_kind_ == GridKind::Search ? tr("No app matches {1}.", search_)
                                                   : tr("No apps from here are on the SD card yet."));
        }
        const int columns = GridColumns();
        const int w = (right_ - left_ - (columns - 1) * kGap) / columns;
        const int h = 124;
        std::vector<std::size_t> shown;
        const int first = grid_page_ * PerPage();
        for (int k = 0; k < PerPage() && first + k < static_cast<int>(grid_.size()); ++k) {
            const std::size_t index = grid_[first + k];
            const App& app = store_.apps[index];
            shown.push_back(index);
            const int tx = left_ + (k % columns) * (w + kGap), ty = 92 + (k / columns) * (h + kGap);
            frame_.Card(tx, ty, w, h, focus_ == k);
            Icon(app, tx + w / 2 - 64, ty + 12);
            frame_.Text(tx + w / 2, ty + 68, kNameSize, RWP_COLOR_INK, app.listing->name, RWP_ALIGN_CENTER, w - 16);
            std::uint32_t colour = RWP_COLOR_INK_SOFT;
            const std::string note = StateNote(app, colour);
            frame_.Text(tx + w / 2, ty + 94, kSmallSize, colour, note, RWP_ALIGN_CENTER, w - 16);
            hits_.push_back({tx, ty, w, h, k});
        }
        store_.WantIcons(shown, input_.frame);
        Prompt(RWP_KEY_B, tr("Back"));
        if (!grid_.empty()) Prompt(RWP_KEY_A, tr("Open"));
        if (pages > 1) Prompt(RWP_KEY_PLUS, tr("Next page"));
        Prompt(RWP_KEY_ONE, tr("Search"));
    }

    void Icon(const App& app, int x, int y) {
        if (app.texture) {
            frame_.Picture(x, y, 128, 48, app.texture);
        } else {
            frame_.Rect(x, y, 128, 48, kIconPlace);
        }
    }

    std::string StateNote(const App& app, std::uint32_t& colour) const {
        switch (app.state) {
            case AppState::Installed:
                colour = RWP_COLOR_ACCENT;
                return tr("Installed");
            case AppState::Update:
                colour = kWarn;
                return tr("Update");
            case AppState::OnCard:
                colour = RWP_COLOR_ACCENT;
                return tr("On the SD card");
            case AppState::PcOnly:
                colour = RWP_COLOR_INK_SOFT;
                return tr("Get it on a PC");
            case AppState::Get:
                break;
        }
        colour = RWP_COLOR_INK_SOFT;
        return std::string(app.listing->author) + "  \xC2\xB7  " + app.listing->version;
    }

    // An app's page, laid out like a game's.
    void DrawApp() {
        const App& app = store_.apps[app_index_];
        const AppListing& listing = *app.listing;
        std::size_t count = 0;
        const AppCategory* categories = app_categories(count);
        Band("", kAppBand);
        frame_.Text(left_, 14, kSmallSize, RWP_COLOR_INK_SOFT, tr(categories[app_category_index(listing.category)].name));
        frame_.Text(left_, 38, kTitleSize, RWP_COLOR_ON_BAND, listing.name, RWP_ALIGN_LEFT, right_ - left_);
        std::string facts = listing.author;
        if (listing.version[0]) facts += std::string("  \xC2\xB7  ") + listing.version;
        if (listing.released[0]) facts += std::string("  \xC2\xB7  ") + std::string(listing.released, 4);
        facts += "  \xC2\xB7  " + app_size_text(listing.size);
        frame_.Text(left_, 82, kSmallSize, RWP_COLOR_INK_SOFT, facts, RWP_ALIGN_LEFT, right_ - left_);

        // The icon, and the one big action beside it.
        const int row = kAppBand + 18;
        frame_.Card(left_, row, 196, 92, false);
        Icon(app, left_ + 34, row + 22);
        const int ax = left_ + 196 + 18, aw = right_ - ax;
        const bool running = store_.InstallRunning(app_index_);
        frame_.Card(ax, row, aw, 92, focus_ == kHitAction, app.state != AppState::PcOnly);
        hits_.push_back({ax, row, aw, 92, kHitAction});
        if (running) {
            frame_.Text(ax + 24, row + 16, kBodySize, RWP_COLOR_INK, ProgressText(), RWP_ALIGN_LEFT, aw - 48);
            frame_.Progress(ax + 24, row + 56, aw - 48, 16, ProgressPermille());
        } else {
            frame_.Text(ax + 24, row + 14, 26, RWP_COLOR_INK, ActionLabel(app), RWP_ALIGN_LEFT, aw - 48);
            frame_.Text(ax + 24, row + 54, kSmallSize, RWP_COLOR_INK_SOFT, ActionNote(app), RWP_ALIGN_LEFT, aw - 48);
        }

        // What it is, its own page as a QR code, and a newer app that does the same.
        // Big enough for a phone to read off a small 480i TV.
        constexpr int kQr = 132;
        const int text_w = right_ - left_ - kQr - 24;
        const int body_y = row + 92 + 18;
        const std::size_t newer = listing.newer[0] ? store_.Find(listing.newer) : store_.apps.size();
        const bool has_newer = newer < store_.apps.size();
        // The list's descriptions are at most 159 bytes: four lines hold them;
        // two when the note about a newer app needs the room below.
        frame_.Text(left_, body_y, kBodySize, RWP_COLOR_INK, listing.description, RWP_ALIGN_LEFT, text_w, has_newer ? 2 : 4);
        if (listing.page[0]) {
            frame_.Qr(right_ - kQr, body_y - 6, kQr, listing.page);
            frame_.Text(right_ - kQr / 2, body_y + kQr - 4, kSmallSize, RWP_COLOR_INK_SOFT, tr("Its own page"),
                        RWP_ALIGN_CENTER);
        }
        if (has_newer) {
            const int ny = body_y + 64;
            const bool lit = focus_ == kHitNewer;
            frame_.Rect(left_, ny, text_w, 40, lit ? 0xF7E3B0FFu : kNoteFill, true);
            frame_.Text(left_ + 16, ny + 9, kSmallSize, kNoteInk,
                        tr("{1} does the same and is kept up to date.", store_.apps[newer].listing->name), RWP_ALIGN_LEFT,
                        text_w - 32);
            hits_.push_back({left_, ny, text_w, 40, kHitNewer});
        }
        if (!status_.empty()) frame_.Text(left_, body_y + 108, kSmallSize, RWP_COLOR_ACCENT, status_, RWP_ALIGN_LEFT, text_w);
        store_.WantIcons({app_index_}, input_.frame);

        if (running) {
            // Once the files are being put in place there is nothing to stop.
            if (store_.progress.stage != InstallStage::Finish) Prompt(RWP_KEY_B, tr("Stop"));
            return;
        }
        Prompt(RWP_KEY_B, tr("Back"));
        if (app.state != AppState::PcOnly) Prompt(RWP_KEY_A, ActionLabel(app));
        if (newer < store_.apps.size()) Prompt(RWP_KEY_PLUS, tr("Newer app"));
    }

    std::string ActionLabel(const App& app) const {
        switch (app.state) {
            case AppState::Installed: return tr("Remove");
            case AppState::Update: return tr("Update");
            case AppState::OnCard: return tr("Replace");
            case AppState::PcOnly: return tr("Get it on a PC");
            case AppState::Get: break;
        }
        return tr("Install");
    }

    std::string ActionNote(const App& app) const {
        const std::string size = app_size_text(app.listing->size);
        switch (app.state) {
            case AppState::Installed: return tr("Installed, version {1}", app.listing->version);
            case AppState::Update: return tr("Version {1}, {2}", app.listing->version, size);
            case AppState::OnCard: return tr("It is on the SD card already. Put version {1} in its place.", app.listing->version);
            case AppState::PcOnly: return tr("The Wii can't unpack it: scan the code.");
            case AppState::Get: break;
        }
        return tr("To the SD card, {1}", size);
    }

    std::string ProgressText() const {
        const InstallProgress& p = store_.progress;
        switch (p.stage) {
            case InstallStage::Download:
                return tr("Downloading: {1} of {2}", app_size_text(p.done), app_size_text(p.total));
            case InstallStage::Check: return tr("Checking that it is the approved file...");
            case InstallStage::Unpack:
                return tr("Unpacking: {1} of {2} files", std::to_string(p.done), std::to_string(p.total));
            default: return tr("Finishing...");
        }
    }

    std::uint32_t ProgressPermille() const {
        const InstallProgress& p = store_.progress;
        if (p.total == 0) return 0;
        const std::uint64_t part = std::uint64_t(p.done) * 1000u / p.total;
        // Downloading is the first 70%, unpacking the rest.
        if (p.stage == InstallStage::Download) return static_cast<std::uint32_t>(part * 7 / 10);
        if (p.stage == InstallStage::Unpack) return static_cast<std::uint32_t>(700 + part * 3 / 10);
        return 700;
    }

    // ---- Input ----

    bool Pressed(std::uint32_t key) const { return (input_.pressed & key) != 0; }

    int PointedAt() const {
        if (!input_.pointer) return -1;
        for (const Hit& hit : hits_) {
            if (input_.x >= hit.x && input_.x < hit.x + hit.w && input_.y >= hit.y && input_.y < hit.y + hit.h)
                return hit.id;
        }
        return -1;
    }

    void Focus(int id) {
        if (id == focus_) return;
        focus_ = id;
        Host().sound(RWP_SOUND_HOVER);
    }

    // True when the player leaves the store.
    bool Handle() {
        const int pointed = PointedAt();
        if (pointed >= 0) Focus(pointed);
        // A prompt pointed at and pressed comes as its button (the host's).
        const std::uint32_t pressed = input_.pressed;
        if (pressed & RWP_KEY_HOME) return true;
        switch (page_) {
            case Page::Front: return HandleFront(pressed);
            case Page::Grid: HandleGrid(pressed); return false;
            case Page::App: HandleApp(pressed); return false;
        }
        return false;
    }

    bool HandleFront(std::uint32_t pressed) {
        if (pressed & RWP_KEY_B) {
            Host().sound(RWP_SOUND_BACK);
            return true;
        }
        if (store_.list_state == ListState::Failed && (pressed & RWP_KEY_A)) {
            Host().sound(RWP_SOUND_CLICK);
            store_.StartList();
            return false;
        }
        if (store_.list_state != ListState::Ready) return false;
        if (pressed & RWP_KEY_ONE) return Search(), false;
        if (pressed & RWP_KEY_TWO) return OpenMine(), false;
        const int count = static_cast<int>(shown_categories_.size());
        const int columns = FrontColumns();
        if (focus_ < 0 || focus_ >= count) Focus(0);
        MoveInGrid(pressed, count, columns, false);
        if (pressed & RWP_KEY_A) {
            Host().sound(RWP_SOUND_CLICK);
            if (focus_ >= 0 && focus_ < count) OpenCategory(shown_categories_[focus_]);
        }
        return false;
    }

    // The D-pad on a grid of `count` tiles; the edges turn the page when `paged`.
    void MoveInGrid(std::uint32_t pressed, int count, int columns, bool paged) {
        if (count == 0) return;
        int at = std::clamp(focus_, 0, count - 1);
        if (pressed & RWP_KEY_RIGHT) {
            if (at % columns == columns - 1 || at == count - 1) {
                if (paged && TurnPage(1)) at = (at / columns) * columns;
            } else {
                ++at;
            }
        }
        if (pressed & RWP_KEY_LEFT) {
            if (at % columns == 0) {
                if (paged && TurnPage(-1)) at = (at / columns) * columns + columns - 1;
            } else {
                --at;
            }
        }
        if ((pressed & RWP_KEY_DOWN) && at + columns < count) at += columns;
        else if ((pressed & RWP_KEY_DOWN) && at / columns < (count - 1) / columns) at = count - 1;
        if ((pressed & RWP_KEY_UP) && at >= columns) at -= columns;
        const int on_page = paged ? std::min(PerPage(), static_cast<int>(grid_.size()) - grid_page_ * PerPage()) : count;
        Focus(std::clamp(at, 0, std::max(0, on_page - 1)));
    }

    bool TurnPage(int step) {
        const int next = grid_page_ + step;
        if (next < 0 || next >= PageCount()) return false;
        grid_page_ = next;
        return true;
    }

    void HandleGrid(std::uint32_t pressed) {
        if (pressed & RWP_KEY_B) {
            Host().sound(RWP_SOUND_BACK);
            page_ = Page::Front;
            focus_ = front_focus_;
            return;
        }
        if (pressed & RWP_KEY_ONE) return Search();
        const int on_page = std::min(PerPage(), static_cast<int>(grid_.size()) - grid_page_ * PerPage());
        if (pressed & (RWP_KEY_PLUS | RWP_KEY_MINUS)) {
            if (TurnPage((pressed & RWP_KEY_PLUS) ? 1 : -1)) Host().sound(RWP_SOUND_HOVER);
            focus_ = std::min(focus_, std::max(0, std::min(PerPage(), static_cast<int>(grid_.size()) - grid_page_ * PerPage()) - 1));
            return;
        }
        if (focus_ != kHitPrevious && focus_ != kHitNext) MoveInGrid(pressed, on_page, GridColumns(), true);
        else if (pressed & (RWP_KEY_DOWN | RWP_KEY_LEFT | RWP_KEY_RIGHT)) Focus(0);
        if ((pressed & RWP_KEY_UP) && focus_ >= 0 && focus_ < GridColumns() && PageCount() > 1 &&
            !(pressed & RWP_KEY_DOWN))
            Focus(kHitNext);
        if (pressed & RWP_KEY_A) {
            Host().sound(RWP_SOUND_CLICK);
            if (focus_ == kHitPrevious) TurnPage(-1);
            else if (focus_ == kHitNext) TurnPage(1);
            else if (focus_ >= 0 && focus_ < on_page) OpenApp(grid_[grid_page_ * PerPage() + focus_]);
        }
    }

    void HandleApp(std::uint32_t pressed) {
        const App& app = store_.apps[app_index_];
        const bool running = store_.InstallRunning(app_index_);
        if (pressed & RWP_KEY_B) {
            Host().sound(RWP_SOUND_BACK);
            if (running) {
                store_.CancelInstall();
                return;
            }
            page_ = back_page_;
            focus_ = back_focus_;
            status_.clear();
            return;
        }
        if (running) return;
        const std::size_t newer = app.listing->newer[0] ? store_.Find(app.listing->newer) : store_.apps.size();
        const bool has_newer = newer < store_.apps.size();
        if (focus_ != kHitAction && focus_ != kHitNewer) Focus(kHitAction);
        if ((pressed & RWP_KEY_DOWN) && has_newer) Focus(kHitNewer);
        if (pressed & RWP_KEY_UP) Focus(kHitAction);
        if ((pressed & RWP_KEY_PLUS) && has_newer) return ShowNewer(newer);
        if (!(pressed & RWP_KEY_A)) return;
        Host().sound(RWP_SOUND_CLICK);
        if (focus_ == kHitNewer && has_newer) return ShowNewer(newer);
        Act(app);
    }

    void ShowNewer(std::size_t newer) {
        Host().sound(RWP_SOUND_CLICK);
        // Back from the newer app's page comes here again.
        app_index_ = newer;
        focus_ = kHitAction;
        status_.clear();
    }

    void Act(const App& app) {
        const std::string name = app.listing->name;
        status_.clear();
        switch (app.state) {
            case AppState::PcOnly:
                return;
            case AppState::Installed:
                if (Host().ask_choice(tr("Remove {1}?", name).c_str(),
                                      tr("Its folder in sd:/apps goes. Its saves and data folders stay.").c_str(),
                                      tr("Remove").c_str(), tr("Keep it").c_str()) == 1)
                    store_.StartRemove(app_index_);
                return;
            case AppState::OnCard:
                if (Host().ask_choice(tr("Replace {1}?", name).c_str(),
                                      tr("This app is in sd:/apps already, not put there from here. Version {1} replaces its files; its own settings stay.",
                                        app.listing->version)
                                          .c_str(),
                                      tr("Replace").c_str(), tr("Keep it").c_str()) != 1)
                    return;
                break;
            case AppState::Get:
            case AppState::Update:
                break;
        }
        if (!store_.StartInstall(app_index_)) status_ = tr("Another download is still running. Try again in a moment.");
    }

    void TakeFinishedInstall() {
        if (!store_.TakeFinished()) return;
        if (store_.finished_index >= store_.apps.size()) return;
        const std::string name = store_.apps[store_.finished_index].listing->name;
        const bool here = page_ == Page::App && app_index_ == store_.finished_index;
        if (store_.finished_ok) {
            if (here)
                status_ = store_.finished_remove ? tr("{1} is removed.", name)
                                                 : tr("Installed. Find it in Home's Homebrew view.");
            return;
        }
        if (store_.progress.stage == InstallStage::Cancelled) {
            if (here) status_ = tr("Stopped. Nothing was changed.");
            return;
        }
        Host().ask_choice((store_.finished_remove ? tr("{1} was not removed", name) : tr("{1} was not installed", name)).c_str(),
                          store_.finished_error.c_str(), tr("OK").c_str(), nullptr);
    }

    // ---- Moving between pages ----

    void OpenCategory(std::size_t category) {
        grid_.clear();
        for (std::size_t i = 0; i < store_.apps.size(); ++i) {
            if (app_category_index(store_.apps[i].listing->category) == category) grid_.push_back(i);
        }
        grid_kind_ = GridKind::Category;
        grid_title_ = tr(categories_[category].name);
        EnterGrid();
    }

    void OpenMine() {
        grid_.clear();
        // Updates first, then the rest of what is on the card.
        for (int pass = 0; pass < 2; ++pass) {
            for (std::size_t i = 0; i < store_.apps.size(); ++i) {
                const AppState state = store_.apps[i].state;
                const bool update = state == AppState::Update;
                const bool other = state == AppState::Installed || state == AppState::OnCard;
                if ((pass == 0 && update) || (pass == 1 && other)) grid_.push_back(i);
            }
        }
        grid_kind_ = GridKind::Mine;
        grid_title_ = tr("My apps");
        EnterGrid();
    }

    static void Typed(const char* text, char* line, std::uint32_t line_bytes, char* matches, std::uint32_t matches_bytes,
                      char* next, std::uint32_t next_bytes, void* arg) {
        StoreScreen* self = static_cast<StoreScreen*>(arg);
        const std::string typed = text;
        std::size_t found = 0;
        std::string names;
        for (const App& app : self->store_.apps) {
            if (!app_matches(*app.listing, typed)) continue;
            if (found < 5) names += (names.empty() ? "" : ", ") + std::string(app.listing->name);
            ++found;
        }
        if (found > 5) names += " " + tr("and {1} more", std::to_string(found - 5));
        if (typed.empty()) names.clear();
        std::snprintf(line, line_bytes, "%s", typed.empty() ? "" : Plural(found, "1 app", "{1} apps").c_str());
        std::snprintf(matches, matches_bytes, "%s", names.c_str());
        const std::string keys = app_search_next(self->store_.listings.data(), self->store_.listings.size(), typed);
        std::snprintf(next, next_bytes, "%s", keys.c_str());
    }

    void Search() {
        Host().sound(RWP_SOUND_CLICK);
        char text[64];
        std::snprintf(text, sizeof(text), "%s", search_.c_str());
        if (Host().ask_search(text, sizeof(text), Typed, this) != 1) return;
        search_ = text;
        while (!search_.empty() && search_.back() == ' ') search_.pop_back();
        if (search_.empty()) return;
        std::vector<std::pair<int, std::size_t>> ranked;
        for (std::size_t i = 0; i < store_.apps.size(); ++i) {
            const int rank = app_match_rank(*store_.apps[i].listing, search_);
            if (rank > 0) ranked.emplace_back(-rank, i);  // the best first, then by name
        }
        std::stable_sort(ranked.begin(), ranked.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        grid_.clear();
        for (const auto& r : ranked) grid_.push_back(r.second);
        grid_kind_ = GridKind::Search;
        grid_title_ = tr("Search: {1}", search_);
        EnterGrid();
    }

    void EnterGrid() {
        if (page_ == Page::Front) front_focus_ = focus_;
        page_ = Page::Grid;
        grid_page_ = 0;
        focus_ = 0;
    }

    void OpenApp(std::size_t index) {
        back_page_ = page_;
        back_focus_ = focus_;
        app_index_ = index;
        page_ = Page::App;
        focus_ = kHitAction;
        status_.clear();
    }

    rwp_screen_info info_;
    int left_ = 40, right_ = 600, middle_ = 320;
    Store store_;
    Frame frame_;
    std::vector<Hit> hits_;
    std::vector<std::pair<std::uint32_t, std::string>> prompts_;
    rwp_input input_{};

    Page page_ = Page::Front;
    int focus_ = 0;
    int front_focus_ = 0;
    const AppCategory* categories_ = nullptr;
    std::vector<std::size_t> category_counts_;
    std::vector<std::size_t> shown_categories_;

    GridKind grid_kind_ = GridKind::Category;
    std::string grid_title_;
    std::vector<std::size_t> grid_;
    int grid_page_ = 0;
    std::string search_;

    std::size_t app_index_ = 0;
    Page back_page_ = Page::Grid;
    int back_focus_ = 0;
    std::string status_;
};

}  // namespace

std::int32_t RunStore(rwp_appstore_request& request) {
    rwp_screen_info info;
    std::memset(&info, 0, sizeof(info));
    if (Host().screen_open(&info) != 0) {
        std::snprintf(request.error, sizeof(request.error), "%s", "the screen could not be opened");
        return RWP_LAUNCH_FAILED;
    }
    std::int32_t result;
    bool changed;
    {
        StoreScreen screen(info);
        result = screen.Run();
        changed = screen.Changed();
    }
    Host().screen_close();
    request.changed = changed ? 1 : 0;
    return result;
}

}  // namespace riftwii::appstore
