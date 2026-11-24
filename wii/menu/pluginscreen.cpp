// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "pluginscreen.hpp"

#include <malloc.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "gui_busy.hpp"
#include "gui_prompts.hpp"
#include "i18n.hpp"
#include "libwiigui/gui.h"
#include "log.hpp"
#include "menutheme.hpp"
#include "riftwii/canvas.hpp"
#include "riftwii/checkedalloc.hpp"
#include "riftwii/qrcode.hpp"
#include "skin.hpp"
#include "transition.hpp"
#include "video.h"
#include "wiidrc.h"
#include "wiimenusounds.hpp"

namespace riftwii::wii::pluginscreen {
namespace {

namespace skinns = riftwii::wii::skin;

Hooks g_hooks;

// The most commands and texts one frame takes: a page of the store is a
// few hundred. More is a plugin's mistake, cut off.
constexpr uint32_t kMaxCommands = 1024;
constexpr std::size_t kMaxTextBytes = 2048;
constexpr std::size_t kMaxTextures = 256;
// And in bytes: from the heap (MEM1, then MEM2), so a plugin's pictures
// never crowd out the menu's. The store's 40 icons take about 1 MB.
constexpr std::size_t kMaxTextureBytes = 4u << 20;
std::size_t g_textureBytes = 0;
constexpr std::size_t kMaxWidthCache = 1024;
// The bottom bar's flat ends and body, as Home's (rift_menu.cpp's HomeBar).
constexpr int kBarFlat = 140;
constexpr int kBarBody = 8;
// A page's bottom band, as rift_menu.cpp's PageControlsBand.
constexpr int kFooterTop = 410;

struct Texture {
    u8* data = nullptr;  // GX_TF_RGBA8, padded to multiples of 4
    int width = 0, height = 0;
    int paddedWidth = 0, paddedHeight = 0;
};
std::vector<Texture> g_textures;  // handle = index + 1

// A text command's settings, to know when its GuiText must change.
struct TextLook {
    std::string text;
    int size = 0;
    int width = 0;
    int lines = 0;
};

const Hooks& H() { return g_hooks; }

GXColor Colour(uint32_t value, int alpha) {
    GXColor c;
    switch (value) {
        case RWP_COLOR_INK: c = skinns::kInk; break;
        case RWP_COLOR_INK_SOFT: c = skinns::kInkSoft; break;
        case RWP_COLOR_ACCENT: c = skinns::kAccent; break;
        case RWP_COLOR_ON_BAND: c = skinns::kInk; break;
        case RWP_COLOR_CARD: c = skinns::kCard; break;
        case RWP_COLOR_WARN: c = skinns::kWarn; break;
        case RWP_COLOR_FIELD: c = skinns::kChipOff; break;
        default:
            c = GXColor{static_cast<u8>(value >> 24), static_cast<u8>(value >> 16), static_cast<u8>(value >> 8),
                        static_cast<u8>(value)};
            break;
    }
    c.a = static_cast<u8>(c.a * alpha / 255);
    return c;
}

GXColor Scaled(GXColor c, float k) {
    const auto s = [k](u8 v) { return static_cast<u8>(std::clamp(v * k, 0.0f, 255.0f)); };
    return GXColor{s(c.r), s(c.g), s(c.b), c.a};
}

const char* ButtonName(uint32_t key) {
    switch (key) {
        case RWP_KEY_A: return "A";
        case RWP_KEY_B: return "B";
        case RWP_KEY_PLUS: return "+";
        case RWP_KEY_MINUS: return "-";
        case RWP_KEY_ONE: return "1";
        case RWP_KEY_TWO: return "2";
        case RWP_KEY_HOME: return "HOME";
        default: return nullptr;
    }
}

// The buttons a controller pressed or holds, as RWP_KEY_* bits.
uint32_t Keys(GuiTrigger* t, bool held) {
    const u32 wpad = t->wpad ? (held ? t->wpad->btns_h : t->wpad->btns_d) : 0;
    const u16 pad = held ? t->pad.btns_h : t->pad.btns_d;
    const u32 drc = held ? t->wiidrcdata.btns_h : t->wiidrcdata.btns_d;
    uint32_t keys = 0;
    const auto map = [&](uint32_t key, u32 w, u16 p, u32 d) {
        if ((wpad & w) || (pad & p) || (drc & d)) keys |= key;
    };
    map(RWP_KEY_A, WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A, PAD_BUTTON_A, WIIDRC_BUTTON_A);
    map(RWP_KEY_B, WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B, WIIDRC_BUTTON_B);
    map(RWP_KEY_PLUS, WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_FULL_R, PAD_BUTTON_START | PAD_TRIGGER_R,
        WIIDRC_BUTTON_PLUS | WIIDRC_BUTTON_R);
    map(RWP_KEY_MINUS, WPAD_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_FULL_L, PAD_TRIGGER_L,
        WIIDRC_BUTTON_MINUS | WIIDRC_BUTTON_L);
    map(RWP_KEY_ONE, WPAD_BUTTON_1 | WPAD_CLASSIC_BUTTON_X, PAD_BUTTON_X, WIIDRC_BUTTON_X);
    map(RWP_KEY_TWO, WPAD_BUTTON_2 | WPAD_CLASSIC_BUTTON_Y, PAD_BUTTON_Y, WIIDRC_BUTTON_Y);
    map(RWP_KEY_HOME, WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME, 0, WIIDRC_BUTTON_HOME);
    return keys;
}

class Canvas : public GuiElement {
public:
    Canvas() {
        width = screenwidth;
        height = screenheight;
        selectable = true;
    }
    ~Canvas() override {
        for (GuiText* text : texts_) delete text;
        delete prompts_;
    }

    // Where the prompts end on the right: the safe area's edge.
    void SetPromptEdge(int right) { promptEdge_ = right; }

    // Menu thread, GUI halted.
    void SetCommands(const rwp_draw* commands, uint32_t count) {
        count = std::min(count, kMaxCommands);
        commands_.resize(count);
        pictures_.assign(count, nullptr);
        std::size_t textIndex = 0;
        std::vector<std::pair<uint32_t, std::string>> prompts;
        for (uint32_t i = 0; i < count; ++i) {
            commands_[i] = commands[i];
            commands_[i].text = nullptr;
            const rwp_draw& c = commands[i];
            if (c.kind == RWP_DRAW_TEXT && c.text != nullptr) {
                Text(textIndex, c);
                commands_[i].arg = static_cast<uint32_t>(textIndex++);
            } else if (c.kind == RWP_DRAW_QR && c.text != nullptr) {
                const std::string wanted(c.text, strnlen(c.text, 78));
                if (wanted != qrText_) {
                    qrText_ = wanted;
                    qr_ = QrCode{};
                    if (!make_qr(qrText_, qr_)) qr_ = QrCode{};
                }
            } else if (c.kind == RWP_DRAW_SKIN && c.arg == RWP_SKIN_PICTURE && c.text != nullptr) {
                // Looked up now: the plugin's string is gone once the frame is handed over.
                const std::string name(c.text, strnlen(c.text, 40));
                // "control_<type>": the controller picture a game's page shows
                // for that GameTDB type (Kenney's, in the theme's glyph colour).
                const bool control = name.compare(0, 8, "control_") == 0;
                const skinns::Tex* picture = control ? &skinns::ControlIcon(name.substr(8)) : &skinns::Picture(name);
                if (picture->data) pictures_[i] = picture;
            } else if (c.kind == RWP_DRAW_PROMPT) {
                if (c.text != nullptr && ButtonName(c.arg) != nullptr)
                    prompts.emplace_back(c.arg, std::string(c.text, strnlen(c.text, 120)));
                commands_[i].kind = 0;  // drawn by the prompts widget
            } else if (c.kind == RWP_DRAW_TEXT) {
                commands_[i].kind = 0;  // no text: nothing to draw
            }
        }
        textsUsed_ = textIndex;
        // The menu's own prompts widget, made again only when they change.
        if (prompts != promptList_) {
            promptList_ = prompts;
            delete prompts_;
            prompts_ = nullptr;
            if (!promptList_.empty()) {
                prompts_ = new GuiPrompts();
                prompts_->SetParent(this);
                prompts_->SetRightEdge(promptEdge_);
                // From the right edge inwards, in the plugin's order.
                for (const auto& prompt : promptList_) prompts_->Add(ButtonName(prompt.first), prompt.second);
            }
        }
    }

    rwp_input TakeInput() {
        rwp_input input = input_;
        input.frame = frames_;
        input_.pressed = 0;
        return input;
    }
    void ForgetInput() {
        input_.pressed = 0;
        input_.held = 0;
    }
    uint32_t Frames() const { return frames_; }

    void Draw() override {
        if (!IsVisible()) return;
        const int alpha = GetAlpha();
        for (std::size_t i = 0; i < commands_.size(); ++i) DrawOne(commands_[i], pictures_[i], alpha);
        if (prompts_) prompts_->Draw();
        ++frames_;
    }

    void Update(GuiTrigger* t) override {
        if (state == STATE::DISABLED || t == nullptr) return;
        if (t->chan == 0) {
            // A new frame's input starts with the first controller.
            input_.held = 0;
            input_.pointer = 0;
        }
        if (t->wpad && t->wpad->ir.valid && !input_.pointer) {
            input_.pointer = 1;
            input_.x = static_cast<int32_t>(t->wpad->ir.x);
            input_.y = static_cast<int32_t>(t->wpad->ir.y);
        }
        input_.pressed |= Keys(t, false);
        input_.held |= Keys(t, true);
        // The menu's own repeat for the directions (and the sticks).
        if (t->Up()) input_.pressed |= RWP_KEY_UP;
        if (t->Down()) input_.pressed |= RWP_KEY_DOWN;
        if (t->Left()) input_.pressed |= RWP_KEY_LEFT;
        if (t->Right()) input_.pressed |= RWP_KEY_RIGHT;
        if (prompts_) {
            prompts_->Update(t);
            // A prompt pointed at and pressed is its button, A's press not.
            const int clicked = prompts_->GetClicked();
            if (clicked >= 0 && static_cast<std::size_t>(clicked) < promptList_.size()) {
                input_.pressed &= ~RWP_KEY_A;
                input_.pressed |= promptList_[clicked].first;
            }
        }
    }

private:
    void Text(std::size_t index, const rwp_draw& c) {
        if (index >= texts_.size()) {
            GuiText* made = new GuiText(nullptr, 18, skinns::kInk);
            made->SetParent(this);
            made->SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
            texts_.push_back(made);
            looks_.emplace_back();
        }
        GuiText* text = texts_[index];
        TextLook& look = looks_[index];
        const std::string wanted(c.text, strnlen(c.text, kMaxTextBytes));
        const int size = std::clamp<int>(c.size, 12, 60);
        const int lines = c.w > 0 && c.h > 0 ? c.h : 0;
        if (look.text != wanted || look.size != size || look.width != c.w || look.lines != lines) {
            look.text = wanted;
            look.size = size;
            look.width = c.w;
            look.lines = lines;
            text->SetText(wanted.c_str());
            text->SetFontSize(size);
            if (lines > 0) {
                text->SetWrap(true, c.w, lines);
            } else {
                text->SetWrap(false);
                text->SetMaxWidth(c.w > 0 ? c.w : 0);
            }
            // Measured now, on the menu thread while the GUI is halted:
            // the font's glyphs are loaded by whoever asks first.
            textWidths_.resize(texts_.size());
            const int full = text->GetTextWidth();
            textWidths_[index] = c.w > 0 ? std::min(full, static_cast<int>(c.w)) : full;
        }
    }

    void DrawOne(const rwp_draw& c, const skinns::Tex* picture, int pageAlpha) {
        const int alpha = pageAlpha * c.alpha / 255;
        if (alpha <= 0) return;
        const float x = c.x, y = c.y, w = c.w, h = c.h;
        switch (c.kind) {
            case RWP_DRAW_CARD: {
                const bool primary = (c.flags & RWP_PRIMARY) != 0;
                const skinns::Tex& base = primary ? skinns::tile9Primary : skinns::tile9;
                const skinns::Tex& lit = primary ? skinns::tile9PrimaryOver : skinns::tile9Over;
                skinns::DrawNine(base, x - 8, y - 8, w + 16, h + 16, skinns::kTileCorner, alpha);
                if (c.flags & RWP_LIT)
                    skinns::DrawNine(lit, x - 8, y - 8, w + 16, h + 16, skinns::kTileCorner,
                                     static_cast<int>(alpha * riftwii::wii::transition::Pulse()));
                break;
            }
            case RWP_DRAW_TEXT: {
                if (c.arg >= textsUsed_) break;
                GuiText* text = texts_[c.arg];
                const int width = c.arg < textWidths_.size() ? textWidths_[c.arg] : 0;
                int left = c.x;
                // Wrapped text is drawn from its left; one line can be centred or right-aligned.
                if (looks_[c.arg].lines == 0) {
                    if (c.align == RWP_ALIGN_CENTER) left -= width / 2;
                    else if (c.align == RWP_ALIGN_RIGHT) left -= width;
                }
                text->SetColor(Colour(c.color, 255));
                text->SetAlpha(alpha);
                text->SetPosition(left, c.y);
                text->Draw();
                break;
            }
            case RWP_DRAW_TEXTURE: {
                if (c.arg == 0 || c.arg > g_textures.size()) break;
                const Texture& t = g_textures[c.arg - 1];
                if (!t.data) break;
                Menu_DrawImgPart(x, y, w, h, static_cast<u16>(t.paddedWidth), static_cast<u16>(t.paddedHeight), t.data, 0, 0,
                                 static_cast<f32>(t.width) / t.paddedWidth, static_cast<f32>(t.height) / t.paddedHeight,
                                 static_cast<u8>(alpha));
                break;
            }
            case RWP_DRAW_RECT:
                if (c.arg > 0)
                    riftwii::wii::DrawPill(x, y, w, c.h, Colour(c.color, alpha));
                else
                    Menu_DrawRectangle(x, y, w, h, Colour(c.color, alpha), 1);
                break;
            case RWP_DRAW_SKIN:
                DrawSkin(c, picture, alpha);
                break;
            case RWP_DRAW_PROGRESS: {
                const int height = c.h > 0 ? c.h : 14;
                riftwii::wii::DrawPill(x, y, w, height, skinns::WithAlpha(skinns::kScrollTrack, alpha));
                const float filled = w * std::min<uint32_t>(c.arg, 1000) / 1000.0f;
                if (filled > 0) riftwii::wii::DrawPill(x, y, filled, height, skinns::WithAlpha(skinns::kAccent, alpha));
                break;
            }
            case RWP_DRAW_QR: {
                if (qr_.size == 0) break;
                const int module = std::max(1, c.w / (qr_.size + 4));
                const int side = (qr_.size + 4) * module;
                // Whole pixels a module: the code is centred in the box asked for.
                const float left = x + (c.w - side) / 2, top = y + (c.w - side) / 2;
                Menu_DrawRectangle(left, top, side, side, skinns::WithAlpha(skinns::kWhite, alpha), 1);
                const GXColor dark{0, 0, 0, static_cast<u8>(alpha)};
                for (int r = 0; r < qr_.size; ++r) {
                    for (int col = 0; col < qr_.size;) {
                        if (!qr_.at(col, r)) {
                            ++col;
                            continue;
                        }
                        int end = col;
                        while (end < qr_.size && qr_.at(end, r)) ++end;
                        Menu_DrawRectangle(left + 2 * module + col * module, top + 2 * module + r * module, (end - col) * module,
                                           module, dark, 1);
                        col = end;
                    }
                }
                break;
            }
            case RWP_DRAW_BUTTON: {
                const char* name = ButtonName(c.arg);
                if (!name) break;
                const int size = c.h > 0 ? c.h : 24;
                skinns::Draw(skinns::ButtonIcon(name, size), x, y, alpha);
                break;
            }
            default:
                break;  // RWP_DRAW_BACKGROUND: the menu's own is already under the page
        }
    }

    void DrawSkin(const rwp_draw& c, const skinns::Tex* picture, int alpha) {
        const float x = c.x, y = c.y, w = c.w, h = c.h;
        const bool lit = (c.flags & RWP_LIT) != 0;
        switch (c.arg) {
            case RWP_SKIN_BAR: {
                f32 vx, vy, vw, vh;
                Menu_VisibleArea(&vx, &vy, &vw, &vh);
                const skinns::Tex& picture = skinns::WideMenu() && skinns::barWide.data ? skinns::barWide : skinns::bar;
                skinns::DrawExtended(picture, 320.0f - picture.w / 2.0f, y, vx, y, vx + vw, vy + vh, kBarFlat, kBarBody);
                break;
            }
            case RWP_SKIN_ROUND:
            case RWP_SKIN_ROUND_LIT: {
                const skinns::Tex& t = c.arg == RWP_SKIN_ROUND_LIT || lit ? skinns::roundBtnOver : skinns::roundBtn;
                const float scale = w > 0 && t.w > 4 ? w / (t.w - 4) : 1.0f;
                skinns::Draw(t, x - 2 * scale, y - 2 * scale, alpha, scale);
                break;
            }
            case RWP_SKIN_PILL:
            case RWP_SKIN_PILL_LIT: {
                const skinns::Tex& t = c.arg == RWP_SKIN_PILL_LIT || lit ? skinns::pillOver : skinns::pill;
                skinns::DrawNine(t, x - 4, y - 4, w + 8, h + 8, 26, alpha);
                break;
            }
            case RWP_SKIN_ARROW_LEFT:
                skinns::Draw(lit ? skinns::arrowLeftOver : skinns::arrowLeft, x - 2, y - 2, alpha);
                break;
            case RWP_SKIN_ARROW_RIGHT:
                skinns::Draw(lit ? skinns::arrowRightOver : skinns::arrowRight, x - 2, y - 2, alpha);
                break;
            case RWP_SKIN_FOOTER: {
                // As a menu page's (rift_menu.cpp's PageControlsBand).
                Menu_FillScreen(kFooterTop, 1000, skinns::WithAlpha(skinns::kBar, alpha));
                for (int at = kFooterTop; at < 480; at += 8)
                    Menu_FillScreen(at, 8, skinns::WithAlpha(Scaled(skinns::kBar, 1.0f - 0.05f * (at - kFooterTop) / 70.0f), alpha));
                Menu_FillScreen(kFooterTop - 3, 3, skinns::WithAlpha(skinns::kDivider, 45 * alpha / 255));
                Menu_FillScreen(kFooterTop, 2, skinns::WithAlpha(skinns::kAccent, alpha));
                Menu_FillScreen(kFooterTop + 2, 1, skinns::WithAlpha(skinns::kCard, 180 * alpha / 255));
                break;
            }
            case RWP_SKIN_PICTURE: {
                if (picture) {
                    const float dw = w > 0 ? w : picture->w, dh = h > 0 ? h : picture->h;
                    Menu_DrawImgPart(x, y, dw, dh, static_cast<u16>(picture->w), static_cast<u16>(picture->h),
                                     picture->data, 0, 0, 1, 1, static_cast<u8>(alpha));
                    break;
                }
                // The theme has no such PNG: the plugin's own.
                if (c.color == 0 || c.color > g_textures.size()) break;
                const Texture& t = g_textures[c.color - 1];
                if (!t.data) break;
                Menu_DrawImgPart(x, y, w > 0 ? w : t.width, h > 0 ? h : t.height, static_cast<u16>(t.paddedWidth),
                                 static_cast<u16>(t.paddedHeight), t.data, 0, 0, static_cast<f32>(t.width) / t.paddedWidth,
                                 static_cast<f32>(t.height) / t.paddedHeight, static_cast<u8>(alpha));
                break;
            }
            case RWP_SKIN_TITLE_BAND: {
                // As a menu page's (rift_menu.cpp's TitleBand), `h` high.
                const int band = c.h > 8 ? c.h : 70;
                Menu_FillScreen(-1000, 1000 + band, skinns::WithAlpha(skinns::kBar, alpha));
                for (int at = 0; at < band - 4; at += 6)
                    Menu_FillScreen(at, 6, skinns::WithAlpha(Scaled(skinns::kBar, 1.0f - 0.05f * at / band), alpha));
                Menu_FillScreen(band - 5, 2, skinns::WithAlpha(skinns::kCard, 180 * alpha / 255));
                Menu_FillScreen(band - 3, 3, skinns::WithAlpha(skinns::kAccent, alpha));
                Menu_FillScreen(band, 2, GXColor{0, 0, 0, static_cast<u8>(26 * alpha / 255)});
                Menu_FillScreen(band + 2, 3, GXColor{0, 0, 0, static_cast<u8>(12 * alpha / 255)});
                break;
            }
            default:
                break;
        }
    }

    std::vector<rwp_draw> commands_;
    std::vector<const skinns::Tex*> pictures_;  // RWP_SKIN_PICTURE's, by command
    std::vector<GuiText*> texts_;
    std::vector<TextLook> looks_;
    std::vector<int> textWidths_;
    std::size_t textsUsed_ = 0;
    std::string qrText_;
    QrCode qr_;
    GuiPrompts* prompts_ = nullptr;
    std::vector<std::pair<uint32_t, std::string>> promptList_;
    int promptEdge_ = 600;
    rwp_input input_{};
    volatile uint32_t frames_ = 0;
};

Canvas* g_canvas = nullptr;
std::unordered_map<std::string, int> g_widths;  // "<size>:<text>"

void FreeTextures() {
    for (Texture& t : g_textures) std::free(t.data);
    g_textures.clear();
    g_textureBytes = 0;
}

}  // namespace

void SetHooks(const Hooks& hooks) { g_hooks = hooks; }

int32_t Open(rwp_screen_info* out) {
    if (g_canvas != nullptr || !H().halt || !H().resume || !H().window || !H().window()) return -1;
    H().halt();
    g_canvas = new Canvas();
    {
        f32 safeX = 0, safeW = 640;
        Menu_SafeArea(&safeX, &safeW);
        g_canvas->SetPromptEdge(static_cast<int>(safeX + safeW * 0.95f));
    }
    H().window()->Append(g_canvas);
    H().resume();
    if (out != nullptr) {
        f32 safeX = 0, safeW = 640;
        Menu_SafeArea(&safeX, &safeW);
        f32 vx = 0, vy = 0, vw = 640, vh = 480;
        Menu_VisibleArea(&vx, &vy, &vw, &vh);
        out->left = static_cast<int32_t>(safeX);
        out->width = static_cast<int32_t>(safeW);
        // The 5% overscan margin the menu keeps its own pages inside.
        out->safe_x = static_cast<int32_t>(safeX + safeW * 0.05f);
        out->safe_w = static_cast<int32_t>(safeW * 0.90f);
        out->widescreen = skinns::WideMenu() ? 1u : 0u;
        const ThemeColor& glyph = MenuTheme().colors.glyph;
        out->glyph = static_cast<uint32_t>(glyph.r) << 24 | static_cast<uint32_t>(glyph.g) << 16 |
                     static_cast<uint32_t>(glyph.b) << 8 | glyph.a;
    }
    logf("Plugin screen: opened (%s menu)\n", skinns::WideMenu() ? "16:9" : "4:3");
    return 0;
}

int32_t Frame(const rwp_draw* commands, uint32_t count, rwp_input* input) {
    if (g_canvas == nullptr) return -1;
    H().halt();
    g_canvas->SetCommands(commands, commands ? count : 0);
    const rwp_input taken = g_canvas->TakeInput();
    const uint32_t before = g_canvas->Frames();
    H().resume();
    // One frame drawn with these commands before the plugin goes on.
    for (int waited = 0; waited < 10 && g_canvas->Frames() == before; ++waited) usleep(5000);
    if (input != nullptr) *input = taken;
    return 0;
}

void Close() {
    if (g_canvas == nullptr) return;
    H().halt();
    H().window()->Remove(g_canvas);
    delete g_canvas;
    g_canvas = nullptr;
    FreeTextures();
    g_widths.clear();
    H().resume();
    logf("Plugin screen: closed\n");
}

uint32_t TextureCreate(const uint8_t* rgba, uint32_t width, uint32_t height) {
    if (rgba == nullptr || width == 0 || height == 0 || width > 640 || height > 480) return 0;
    const int paddedW = static_cast<int>((width + 3) & ~3u), paddedH = static_cast<int>((height + 3) & ~3u);
    const std::size_t bytes = static_cast<std::size_t>(paddedW) * paddedH * 4;
    if (bytes > kMaxTextureBytes - g_textureBytes) return 0;
    std::vector<std::uint8_t> padded;
    if (!try_resize(padded, bytes)) return 0;
    for (uint32_t row = 0; row < height; ++row)
        std::memcpy(padded.data() + static_cast<std::size_t>(row) * paddedW * 4, rgba + static_cast<std::size_t>(row) * width * 4,
                    width * 4);
    u8* data = static_cast<u8*>(memalign(32, bytes));
    if (data == nullptr) return 0;
    to_gx_rgba8(padded.data(), paddedW, paddedH, data);
    DCFlushRange(data, bytes);
    Texture made{data, static_cast<int>(width), static_cast<int>(height), paddedW, paddedH};
    // The GUI thread reads the list while it draws.
    if (g_canvas) H().halt();
    uint32_t handle = 0;
    for (std::size_t i = 0; i < g_textures.size(); ++i) {
        if (!g_textures[i].data) {
            g_textures[i] = made;
            handle = static_cast<uint32_t>(i + 1);
            break;
        }
    }
    if (handle == 0 && g_textures.size() < kMaxTextures) {
        g_textures.push_back(made);
        handle = static_cast<uint32_t>(g_textures.size());
    }
    if (g_canvas) H().resume();
    if (handle == 0) std::free(data);
    else g_textureBytes += bytes;
    return handle;
}

void TextureFree(uint32_t handle) {
    if (handle == 0 || handle > g_textures.size()) return;
    if (g_canvas) H().halt();
    const Texture& gone = g_textures[handle - 1];
    if (gone.data) g_textureBytes -= static_cast<std::size_t>(gone.paddedWidth) * gone.paddedHeight * 4;
    std::free(gone.data);
    g_textures[handle - 1] = Texture{};
    if (g_canvas) H().resume();
}

int32_t TextWidth(const char* text, uint32_t size) {
    if (text == nullptr || *text == '\0') return 0;
    const int clamped = std::clamp<int>(static_cast<int>(size), 12, 60);
    const std::string key = std::to_string(clamped) + ":" + std::string(text, strnlen(text, kMaxTextBytes));
    const auto found = g_widths.find(key);
    if (found != g_widths.end()) return found->second;
    // The font is shared with the GUI thread, which must not draw meanwhile.
    if (g_canvas) H().halt();
    GuiText probe(nullptr, clamped, skinns::kInk);
    probe.SetText(key.c_str() + key.find(':') + 1);
    const int width = probe.GetTextWidth();
    if (g_canvas) H().resume();
    if (g_widths.size() >= kMaxWidthCache) g_widths.clear();
    g_widths.emplace(key, width);
    return width;
}

const char* Translate(const char* english) { return english ? tr(english) : ""; }

void Sound(uint32_t which) {
    switch (which) {
        case RWP_SOUND_HOVER: PlayMenuSound(MenuSoundEvent::Hover); break;
        case RWP_SOUND_CLICK: PlayMenuSound(MenuSoundEvent::Click); break;
        case RWP_SOUND_BACK: PlayMenuSound(MenuSoundEvent::Back); break;
        default: break;
    }
}

int32_t AskSearch(char* text, uint32_t bytes,
                  void (*typed)(const char* text, char* line, uint32_t line_bytes, char* matches, uint32_t matches_bytes,
                                char* next, uint32_t next_bytes, void* arg),
                  void* arg) {
    if (text == nullptr || bytes == 0 || !H().search) return 0;
    std::string entry(text, strnlen(text, bytes - 1));
    const auto filter = [&](const std::string& now, std::string& matches, std::string& next) -> std::string {
        if (typed == nullptr) return "";
        char line[96] = "";
        char names[1024] = "";
        char keys[128] = "";
        typed(now.c_str(), line, sizeof(line), names, sizeof(names), keys, sizeof(keys), arg);
        matches = names;
        next = keys;
        return line;
    };
    H().halt();
    if (g_canvas) g_canvas->SetState(STATE::DISABLED);
    const bool confirmed = H().search(entry, filter);
    if (g_canvas) {
        g_canvas->SetState(STATE::DEFAULT);
        g_canvas->ForgetInput();
    }
    H().resume();
    if (!confirmed) return 0;
    std::snprintf(text, bytes, "%s", entry.c_str());
    return 1;
}

int32_t AskChoice(const char* title, const char* text, const char* yes, const char* no) {
    if (!H().popup) return 0;
    H().halt();
    if (g_canvas) g_canvas->SetState(STATE::DISABLED);
    const int answer = H().popup(title ? title : "", text ? text : "", yes ? yes : tr("OK"), no ? no : "");
    if (g_canvas) {
        g_canvas->SetState(STATE::DEFAULT);
        g_canvas->ForgetInput();
    }
    H().resume();
    // ShowPopup: 0 the first button, 1 the second.
    return answer == 0 ? 1 : 0;
}

int32_t AskRisky(const char* title, const char* text, const char* safe, const char* risky) {
    if (!H().popup_safe_first || !safe || !risky) return 0;
    H().halt();
    if (g_canvas) g_canvas->SetState(STATE::DISABLED);
    const int answer = H().popup_safe_first(title ? title : "", text ? text : "", safe, risky);
    if (g_canvas) {
        g_canvas->SetState(STATE::DEFAULT);
        g_canvas->ForgetInput();
    }
    H().resume();
    return answer == 1 ? 1 : 0;
}

}  // namespace riftwii::wii::pluginscreen
