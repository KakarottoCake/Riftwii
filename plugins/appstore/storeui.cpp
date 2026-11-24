// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "storeui.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "riftwii/storeicons.hpp"
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
constexpr int kLine = 26;     // a line of body text
constexpr int kFactRow = 28;  // a row of an app's facts

// The theme's colours, so a dark theme's tiles read as well as a light one's.
constexpr std::uint32_t kWarn = RWP_COLOR_WARN;        // an update waiting, Wii only
constexpr std::uint32_t kIconPlace = RWP_COLOR_FIELD;  // where an icon goes until it is in

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
    void Texture(int x, int y, int w, int h, std::uint32_t texture) {
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
    // The theme's PNG `name`, else `own` (a texture of the plugin's).
    void Picture(const std::string& name, std::uint32_t own, int x, int y, int w, int h) {
        texts_.push_back(name);
        rwp_draw& d = Add(RWP_DRAW_SKIN, x, y, w, h);
        d.arg = RWP_SKIN_PICTURE;
        d.color = own;
        d.text = texts_.back().c_str();
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
constexpr int kHitFilter = 1008;

class StoreScreen {
public:
    StoreScreen(const rwp_screen_info& info, std::uint32_t console) : info_(info), console_(console) {
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

    // The page's band and title; `picture` (a theme picture's name) before
    // it, and `right` pixels kept free at the band's right end (for what a
    // page puts there).
    void Band(const std::string& title, int height = kBand, const std::string& picture = "", int right = 0) {
        frame_.Skin(RWP_SKIN_TITLE_BAND, info_.left, 0, info_.width, height);
        int x = left_;
        if (!picture.empty()) {
            Picture(picture, x, 16, 40);
            x += 52;
        }
        if (!title.empty()) frame_.Text(x, 20, kTitleSize, RWP_COLOR_ON_BAND, title, RWP_ALIGN_LEFT, right_ - x - right);
    }

    // A theme picture (docs/THEMES.md), `size` pixels square: the theme's
    // PNG, else the store's own, painted here once in the theme's icon
    // colour (riftwii/storeicons.hpp; the menu does not carry them).
    void Picture(const std::string& name, int x, int y, int size) {
        // Painted once: a failure too is kept (0), not tried each frame.
        const bool painted = own_pictures_.count(name) != 0;
        std::uint32_t& own = own_pictures_[name];
        if (!painted) {
            const Rgba ink{static_cast<std::uint8_t>(info_.glyph >> 24), static_cast<std::uint8_t>(info_.glyph >> 16),
                           static_cast<std::uint8_t>(info_.glyph >> 8), static_cast<std::uint8_t>(info_.glyph)};
            Canvas canvas(0, 0);
            if (paint_store_icon(name, ink, canvas)) {
                own = Host().texture_create(canvas.pixels().data(), static_cast<std::uint32_t>(canvas.width()),
                                            static_cast<std::uint32_t>(canvas.height()));
            }
        }
        frame_.Picture(name, own, x, y, size, size);
    }

    static std::string CategoryPicture(const AppCategory& category) { return std::string("store_") + category.key; }

    void Prompt(std::uint32_t key, const std::string& label) { prompts_.push_back({key, label}); }

    // The menu's own prompts, from the right edge inwards in the order
    // named: B Back first, at the edge, as on the menu's pages.
    void DrawPrompts() {
        for (const auto& prompt : prompts_) frame_.Prompt(prompt.first, prompt.second);
    }

    void Message(const std::string& text, int y = 190) {
        frame_.Text(middle_, y, 22, RWP_COLOR_INK, text, RWP_ALIGN_CENTER, right_ - left_, 0);
    }

    // The store front: the categories as tiles, each with its icon, and a
    // line under them of what the lit one holds.
    void DrawFront() {
        Band(tr("Rift Store"), kBand, "store_logo", 280);  // the counts on the right
        if (!ListReady()) return;

        CountCategories();
        std::size_t installed = 0;
        for (const App& app : store_.apps) {
            if (app.state == AppState::Installed || app.state == AppState::Update) ++installed;
        }
        std::size_t shown = 0;
        for (const App& app : store_.apps) shown += Shown(app) ? 1 : 0;
        const std::string counts = Plural(shown, "1 app", "{1} apps") + "  \xC2\xB7  " +
                                   tr("{1} installed", std::to_string(installed));
        // The filter: a button at the band's right (- or pointed at and A),
        // filled while it is on; the counts under it are what it lets through.
        // Not a prompt: a fifth one did not fit a 4:3 menu's bottom band.
        const std::string filter = tr("Without a Wii Remote");
        const int filter_w = 10 + 20 + 8 + Width(filter, kSmallSize) + 14;
        const int filter_x = right_ - filter_w;
        // Level with the title: higher would be in the TV's overscan.
        frame_.Card(filter_x, 14, filter_w, 28, focus_ == kHitFilter, no_remote_);
        frame_.Button(filter_x + 10, 18, 20, RWP_KEY_MINUS);
        frame_.Text(filter_x + 38, 16, kSmallSize, RWP_COLOR_INK, filter);
        hits_.push_back({filter_x, 14, filter_w, 28, kHitFilter});
        // Clear of the band's lower edge (Bookshelf's is a thick plank).
        frame_.Text(right_, 44, kSmallSize, RWP_COLOR_INK_SOFT, counts, RWP_ALIGN_RIGHT, 270);

        // Four across on a 16:9 menu; three on a 4:3 one, where four would
        // cut the longer names ("Online services"). The tiles share the room
        // above the line; the icon is as big as their height lets it be.
        constexpr int kTop = 88;
        constexpr int kAboutY = 368;
        const int columns = FrontColumns();
        const int rows = (static_cast<int>(shown_categories_.size()) + columns - 1) / columns;
        const int gap = rows > 3 ? 8 : 12;
        const int h = std::min(84, (kAboutY - 8 - kTop - (rows - 1) * gap) / std::max(rows, 1));
        const int w = (right_ - left_ - (columns - 1) * gap) / columns;
        const int icon = h >= 80 ? 40 : h >= 58 ? 28 : 0;
        const int block = icon > 0 ? icon + 4 + 22 : 22;  // the icon over the name
        for (std::size_t k = 0; k < shown_categories_.size(); ++k) {
            const int column = static_cast<int>(k) % columns, row = static_cast<int>(k) / columns;
            const int tx = left_ + column * (w + gap), ty = kTop + row * (h + gap);
            const AppCategory& category = categories_[shown_categories_[k]];
            const bool lit = focus_ == static_cast<int>(k);
            frame_.Card(tx, ty, w, h, lit);
            const int top = ty + (h - block) / 2;
            if (icon > 0) Picture(CategoryPicture(category), tx + (w - icon) / 2, top, icon);
            frame_.Text(tx + w / 2, top + block - 24, kNameSize, RWP_COLOR_INK, tr(category.name), RWP_ALIGN_CENTER,
                        w - 12);
            hits_.push_back({tx, ty, w, h, static_cast<int>(k)});
        }
        // What the lit category holds, and how many; its count is left out
        // when the line would not fit.
        if (focus_ >= 0 && focus_ < static_cast<int>(shown_categories_.size())) {
            const std::size_t index = shown_categories_[focus_];
            std::string about = tr(categories_[index].about);
            const std::string counted =
                about + "  \xC2\xB7  " + Plural(category_counts_[index], "1 app", "{1} apps");
            if (Width(counted, kSmallSize) <= right_ - left_) about = counted;
            frame_.Text(middle_, kAboutY, kSmallSize, RWP_COLOR_INK_SOFT, about, RWP_ALIGN_CENTER, right_ - left_);
        }
        // My apps and Search sit with the other buttons on the bottom band,
        // not beside the title, where a longer translation covered it.
        const std::size_t updates = store_.UpdateCount();
        Prompt(RWP_KEY_B, tr("Back"));
        Prompt(RWP_KEY_A, tr("Open"));
        Prompt(RWP_KEY_ONE, tr("Search"));
        Prompt(RWP_KEY_TWO, updates ? tr("My apps ({1})", std::to_string(updates)) : tr("My apps"));
    }

    void ToggleFilter() {
        Host().sound(RWP_SOUND_CLICK);
        no_remote_ = !no_remote_;
        CountCategories();
    }

    // Whether the filter lets `app` through: with it on, only apps the
    // list says work with a Classic or GameCube controller.
    bool Shown(const App& app) const { return !no_remote_ || app_needs_no_wii_remote(app.listing->controllers); }

    // Listed for the Wii only, on a Wii U in Wii mode.
    bool OtherConsole(const AppListing& listing) const {
        return console_ == RWP_CONSOLE_VWII && (listing.platforms & kAppOnVWii) == 0;
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
        for (const App& app : store_.apps) {
            if (Shown(app)) ++category_counts_[app_category_index(app.listing->category)];
        }
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
        std::string picture;
        if (grid_kind_ == GridKind::Category) picture = CategoryPicture(categories_[grid_category_]);
        const int pages = PageCount();
        // Room on the right for the page and its arrows, when there are pages.
        Band(grid_title_, kBand, picture, pages > 1 ? 280 : 0);
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
        // What the lit app is, under the tiles.
        if (focus_ >= 0 && focus_ < static_cast<int>(shown.size())) {
            frame_.Text(middle_, 92 + 2 * (h + kGap) + 2, kSmallSize, RWP_COLOR_INK_SOFT,
                        store_.apps[shown[focus_]].listing->description, RWP_ALIGN_CENTER, right_ - left_);
        }
        store_.WantIcons(shown, input_.frame);
        Prompt(RWP_KEY_B, tr("Back"));
        if (!grid_.empty()) Prompt(RWP_KEY_A, tr("Open"));
        if (pages > 1) Prompt(RWP_KEY_PLUS, tr("Next page"));
        Prompt(RWP_KEY_ONE, tr("Search"));
    }

    void Icon(const App& app, int x, int y) {
        if (app.texture) {
            frame_.Texture(x, y, 128, 48, app.texture);
        } else {
            // No icon (or not downloaded yet): its category's, on a plain field.
            std::size_t count = 0;
            const AppCategory* categories = app_categories(count);
            frame_.Rect(x, y, 128, 48, kIconPlace);
            Picture(CategoryPicture(categories[app_category_index(app.listing->category)]), x + 44, y + 4, 40);
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
                if (OtherConsole(*app.listing)) {
                    colour = kWarn;
                    return tr("Wii only");
                }
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
        std::string byline = listing.author;
        if (listing.version[0]) byline += std::string("  \xC2\xB7  ") + listing.version;
        // The controllers it works with, at the band's right, as the list
        // says: the pictures a game's page shows for GameTDB's controllers,
        // drawn by the menu (a USB keyboard has none, and is left out here).
        static const struct {
            std::uint8_t bit;
            const char* picture;
        } kControllers[] = {{kAppWiiRemote, "control_wiimote"},
                            {kAppNunchuk, "control_nunchuk"},
                            {kAppClassic, "control_classiccontroller"},
                            {kAppGameCube, "control_gamecube"}};
        int icons_x = right_;
        for (int i = 3; i >= 0; --i) {
            if ((listing.controllers & kControllers[i].bit) == 0) continue;
            icons_x -= 32;
            frame_.Picture(kControllers[i].picture, 0, icons_x, 74, 32, 32);
            icons_x -= 6;
        }
        int byline_end = icons_x < right_ ? icons_x - 16 : right_;
        if (icons_x < right_) {
            // Said what the pictures are, before them, when the byline
            // still fits whole beside the words (a 4:3 menu's often doesn't).
            const std::string works = tr("Works with");
            const int label_left = icons_x - 4 - Width(works, kSmallSize);
            if (left_ + Width(byline, kSmallSize) + 24 <= label_left) {
                frame_.Text(icons_x - 4, 82, kSmallSize, RWP_COLOR_INK_SOFT, works, RWP_ALIGN_RIGHT);
                byline_end = label_left - 24;
            }
        }
        frame_.Text(left_, 82, kSmallSize, RWP_COLOR_INK_SOFT, byline, RWP_ALIGN_LEFT, byline_end - left_);

        // The icon, and the one big action beside it.
        const int row = kAppBand + 18;
        frame_.Card(left_, row, 196, 92, false);
        Icon(app, left_ + 34, row + 22);
        const int ax = left_ + 196 + 18, aw = right_ - ax;
        const bool running = store_.InstallRunning(app_index_);
        frame_.Card(ax, row, aw, 92, focus_ == kHitAction, app.state != AppState::PcOnly);
        hits_.push_back({ax, row, aw, 92, kHitAction});
        if (running) {
            frame_.Text(ax + 24, row + 10, kBodySize, RWP_COLOR_INK, ProgressTitle(), RWP_ALIGN_LEFT, aw - 48);
            frame_.Text(ax + 24, row + 38, kSmallSize, RWP_COLOR_INK_SOFT, ProgressDetail(), RWP_ALIGN_LEFT, aw - 48);
            frame_.Progress(ax + 24, row + 66, aw - 48, 12, ProgressPermille());
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
        // two when the note about a newer app needs the room below. Most
        // are one line, and the facts follow right under them.
        const int description_lines = DescriptionLines(listing.description, text_w, has_newer ? 2 : 4);
        frame_.Text(left_, body_y, kBodySize, RWP_COLOR_INK, listing.description, RWP_ALIGN_LEFT, text_w, has_newer ? 2 : 4);
        if (listing.page[0]) {
            frame_.Qr(right_ - kQr, body_y - 6, kQr, listing.page);
            frame_.Text(right_ - kQr / 2, body_y + kQr - 4, kSmallSize, RWP_COLOR_INK_SOFT, tr("Its own page"),
                        RWP_ALIGN_CENTER);
        }
        int below = body_y + description_lines * kLine + 12;
        if (has_newer) {
            const int ny = below;
            below = ny + 40 + 12;
            const bool lit = focus_ == kHitNewer;
            // Pressed to open that app: a card, as every button is.
            frame_.Card(left_, ny, text_w, 40, lit);
            frame_.Text(left_ + 16, ny + 9, kSmallSize, RWP_COLOR_INK,
                        tr("{1} does the same and is kept up to date.", store_.apps[newer].listing->name), RWP_ALIGN_LEFT,
                        text_w - 32);
            hits_.push_back({left_, ny, text_w, 40, kHitNewer});
        }
        // What an install or removal said, on the last line until the page
        // is left; the facts in the room above it.
        constexpr int kStatusY = 368;
        int facts_end = kBarTop;
        if (!status_.empty()) {
            frame_.Text(left_, kStatusY, kSmallSize, RWP_COLOR_ACCENT, status_, RWP_ALIGN_LEFT, text_w);
            facts_end = kStatusY - 4;
        }
        DrawFacts(listing, left_, below, text_w, facts_end);
        store_.WantIcons({app_index_}, input_.frame);

        if (running) {
            // Once the files are being put in place there is nothing to stop.
            if (CanStop()) Prompt(RWP_KEY_B, tr("Stop"));
            return;
        }
        Prompt(RWP_KEY_B, tr("Back"));
        if (app.state != AppState::PcOnly) Prompt(RWP_KEY_A, ActionLabel(app));
        if (newer < store_.apps.size()) Prompt(RWP_KEY_PLUS, tr("Newer app"));
    }

    // How many lines `text` takes at the body size in `width`, at most
    // `most`: wrapped at spaces the way the menu's text wraps (libwiigui's
    // GuiText measures a line with the space after it, and starts a new
    // line with the word that made it too wide). Kept for the last text, as
    // the page asks every frame.
    int DescriptionLines(const std::string& text, int width, int most) {
        if (text.empty()) return 0;
        if (text != wrapped_text_ || width != wrapped_width_) {
            wrapped_text_ = text;
            wrapped_width_ = width;
            int lines = 1;
            std::string line;
            std::size_t start = 0;
            while (start < text.size()) {
                std::size_t end = text.find(' ', start);
                const bool last = end == std::string::npos;
                if (last) end = text.size();
                const std::string word = text.substr(start, end - start);
                std::string candidate = line.empty() ? word : line + " " + word;
                if (!line.empty() && Width(last ? candidate : candidate + " ", kBodySize) > width) {
                    ++lines;
                    candidate = word;
                }
                line = candidate;
                start = end + 1;
            }
            wrapped_lines_ = lines;
        }
        return std::min(wrapped_lines_, most);
    }

    // Its size, date, consoles and license as a table: each fact's icon, its
    // name and its value, one a row from `y`, as many as end above `end`.
    // Plain text: nothing here is pressed.
    void DrawFacts(const AppListing& listing, int x, int y, int width, int end) {
        std::string consoles;
        const auto add_console = [&consoles](const char* name) {
            if (!consoles.empty()) consoles += "  \xC2\xB7  ";
            consoles += name;
        };
        if (listing.platforms & kAppOnWii) add_console("Wii");
        if (listing.platforms & kAppOnVWii) add_console("vWii");
        if (listing.platforms & kAppOnWiiMini) add_console("Wii mini");
        struct Fact {
            const char* picture;
            std::string name, value;
        };
        const Fact all[] = {
            {"store_size", tr("Size"), SizeText(listing.size)},
            {"store_date", tr("Released"), listing.released},
            {"store_console", tr("Works on"), consoles},
            {"store_license", tr("License"), listing.license},
        };
        std::vector<const Fact*> facts;
        int name_w = 0;
        for (const Fact& fact : all) {
            if (fact.value.empty()) continue;
            facts.push_back(&fact);
            name_w = std::max(name_w, Width(fact.name, kSmallSize));
        }
        const int value_x = x + 34 + name_w + 18;
        for (const Fact* fact : facts) {
            if (y + kFactRow > end) break;
            Picture(fact->picture, x, y + 1, 24);
            frame_.Text(x + 34, y + 2, kSmallSize, RWP_COLOR_INK_SOFT, fact->name);
            frame_.Text(value_x, y + 2, kSmallSize, RWP_COLOR_INK, fact->value, RWP_ALIGN_LEFT, x + width - value_x);
            y += kFactRow;
        }
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
        const std::string size = SizeText(app.listing->size);
        switch (app.state) {
            case AppState::Installed: return tr("Installed, version {1}", app.listing->version);
            case AppState::Update: return tr("Version {1}, {2}", app.listing->version, size);
            case AppState::OnCard: return tr("It is on the SD card already. Put version {1} in its place.", app.listing->version);
            case AppState::PcOnly: return tr("The Wii can't unpack it: scan the code.");
            case AppState::Get: break;
        }
        // Short, as the card has one line; the warning is asked before installing.
        if (OtherConsole(*app.listing)) return tr("Listed for the Wii only, {1}", size);
        return tr("To the SD card, {1}", size);
    }

    // The step, on the card's first line, and its numbers on the second:
    // one line held both, and was cut on a 4:3 menu.
    std::string ProgressTitle() const {
        switch (store_.progress.Read().stage) {
            case InstallStage::Download: return tr("Downloading...");
            case InstallStage::Check: return tr("Checking the file...");
            case InstallStage::Unpack: return tr("Unpacking...");
            case InstallStage::Cancelled: return tr("Stopping...");
            default: return tr("Finishing...");
        }
    }
    std::string ProgressDetail() const {
        const InstallProgress::Snapshot p = store_.progress.Read();
        if (p.stage == InstallStage::Download) return tr("{1} of {2}", SizeText(p.done), SizeText(p.total));
        if (p.stage == InstallStage::Unpack)
            return tr("{1} of {2} files", std::to_string(p.done), std::to_string(p.total));
        return "";
    }
    // Whether Stop is still taken: until the files start moving in.
    bool CanStop() const {
        const InstallStage stage = store_.progress.Read().stage;
        return stage == InstallStage::Download || stage == InstallStage::Check || stage == InstallStage::Unpack;
    }

    std::uint32_t ProgressPermille() const {
        const InstallProgress::Snapshot p = store_.progress.Read();
        if (p.total == 0) return 0;
        const std::uint64_t part = std::uint64_t(p.done) * 1000u / p.total;
        // Downloading is the first 70%, unpacking the rest.
        if (p.stage == InstallStage::Download) return static_cast<std::uint32_t>(part * 7 / 10);
        if (p.stage == InstallStage::Unpack) return static_cast<std::uint32_t>(700 + part * 3 / 10);
        if (p.stage == InstallStage::Check) return 700;
        return 1000;  // everything unpacked: the moves in, or the end
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
        if (pressed & RWP_KEY_MINUS) {
            ToggleFilter();
            if (focus_ != kHitFilter) focus_ = std::min(focus_, std::max(0, static_cast<int>(shown_categories_.size()) - 1));
            return false;
        }
        // The filter's button sits above the tiles: up from the first row,
        // down back to them.
        if (focus_ == kHitFilter) {
            if (pressed & RWP_KEY_A) ToggleFilter();
            else if (pressed & (RWP_KEY_DOWN | RWP_KEY_LEFT)) Focus(0);
            return false;
        }
        if ((pressed & RWP_KEY_UP) && focus_ >= 0 && focus_ < columns) {
            Focus(kHitFilter);
            return false;
        }
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
                                      tr("Its program goes. Its saves and settings stay until you say otherwise.").c_str(),
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
        // Every install of a Wii-only app on a Wii U asks: Install, Update and Replace.
        if (OtherConsole(*app.listing) &&
            Host().ask_choice(tr("Install {1}?", name).c_str(),
                              tr("It is listed for the Wii only and may not work on a Wii U.").c_str(),
                              tr("Install anyway").c_str(), tr("Cancel").c_str()) != 1)
            return;
        if (!store_.StartInstall(app_index_)) status_ = tr("Another app is being installed. Try again when it is done.");
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
            // What the app wrote stays unless the player says so; B keeps it.
            if (store_.finished_remove && !store_.finished_leftovers.empty()) {
                std::string folders;
                for (const std::string& folder : store_.finished_leftovers)
                    folders += (folders.empty() ? "" : ", ") + folder;
                if (Host().ask_choice(tr("Remove its saves too?").c_str(),
                                      tr("{1} left its saves and settings in {2}. Kept, they are there if you install it again.", name, folders)
                                          .c_str(),
                                      tr("Remove them").c_str(), tr("Keep them").c_str()) == 1 &&
                    store_.StartRemoveData(store_.finished_index) && here)
                    status_.clear();
                else if (here)
                    status_ = tr("Removed. Its saves are kept.");
            }
            return;
        }
        if (store_.progress.Read().stage == InstallStage::Cancelled) {
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
            if (app_category_index(store_.apps[i].listing->category) == category && Shown(store_.apps[i]))
                grid_.push_back(i);
        }
        grid_kind_ = GridKind::Category;
        grid_title_ = tr(categories_[category].name);
        grid_category_ = category;
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
            if (!self->Shown(app) || !app_matches(*app.listing, typed)) continue;
            if (found < 5) names += (names.empty() ? "" : ", ") + std::string(app.listing->name);
            ++found;
        }
        if (found > 5) names += " " + tr("and {1} more", std::to_string(found - 5));
        if (typed.empty()) names.clear();
        std::snprintf(line, line_bytes, "%s", typed.empty() ? "" : Plural(found, "1 app", "{1} apps").c_str());
        std::snprintf(matches, matches_bytes, "%s", names.c_str());
        const auto no_remote = [](const AppListing& listing) { return app_needs_no_wii_remote(listing.controllers); };
        const std::string keys = app_search_next(self->store_.listings.data(), self->store_.listings.size(), typed,
                                                 self->no_remote_ ? +no_remote : nullptr);
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
            if (!Shown(store_.apps[i])) continue;
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
    std::uint32_t console_ = RWP_CONSOLE_WII;
    bool no_remote_ = false;  // the "Without a Wii Remote" filter
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
    std::size_t grid_category_ = 0;  // a category grid's
    std::map<std::string, std::uint32_t> own_pictures_;  // name: texture (0: none)
    std::string wrapped_text_;  // DescriptionLines' last text, width and count
    int wrapped_width_ = 0;
    int wrapped_lines_ = 0;
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
        StoreScreen screen(info, request.console);
        result = screen.Run();
        changed = screen.Changed();
    }
    Host().screen_close();
    request.changed = changed ? 1 : 0;
    return result;
}

}  // namespace riftwii::appstore
