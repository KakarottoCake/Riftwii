// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui_flowlist.hpp"

#include <cmath>
#include <cstdlib>

#include "skin.hpp"
#include "video.h"
#include "wiidrc.h"

namespace skin = riftwii::wii::skin;

namespace {

constexpr int kPad = 14;
constexpr int kChipW = 208, kChipH = 30;  // skin::chipOn/chipOff
constexpr int kStep = 34;                 // skin::stepBack/stepForward
constexpr int kGap = 6;
constexpr int kSwitchW = 60;  // skin::switchOn/switchOff
constexpr int kStepperW = kStep + kGap + kChipW + kGap + kStep;
constexpr int kDragStart = 8;  // pixels the pointer moves before a press becomes a drag
constexpr int kTrackW = 6;
constexpr u32 kWpadA = WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A;

bool AnyPointer() {
    for (int i = 0; i < 4; i++)
        if (userInput[i].wpad && userInput[i].wpad->ir.valid) return true;
    return false;
}

bool Pressed(GuiTrigger* t, u32 wpad, u16 pad, u16 drc) {
    return (t->wpad && (t->wpad->btns_d & wpad)) || (t->pad.btns_d & pad) || (t->wiidrcdata.btns_d & drc);
}

// The width a row's control takes at its right.
int ControlWidth(const FlowRow& r) {
    switch (r.kind) {
        case FlowRow::Kind::Option: return r.dim ? kChipW : kStepperW;
        case FlowRow::Kind::Toggle: return kSwitchW + 64;
        case FlowRow::Kind::Action:
        case FlowRow::Kind::Header: return r.value.empty() ? 0 : kChipW;
        case FlowRow::Kind::Info: return 0;
    }
    return 0;
}

}  // namespace

GuiFlowList::GuiFlowList(int x, int y, int w, int n)
    : x0(x), y0(y), rowWidth(w), visible(n > kMaxVisible ? kMaxVisible : n < 1 ? 1 : n) {
    width = screenwidth;
    height = screenheight;
    selectable = true;
    for (int i = 0; i <= kMaxVisible; ++i) {
        label[i] = new GuiText(nullptr, 18, skin::kInk);
        label[i]->SetParent(this);
        label[i]->SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
        value[i] = new GuiText(nullptr, 16, skin::kInkSoft);
        value[i]->SetParent(this);
        value[i]->SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
    }
    soundOver = new GuiSound(button_over_pcm, button_over_pcm_size, SOUND::PCM);
    soundClick = new GuiSound(button_click_pcm, button_click_pcm_size, SOUND::PCM);
}

GuiFlowList::~GuiFlowList() {
    for (int i = 0; i <= kMaxVisible; ++i) {
        delete label[i];
        delete value[i];
    }
    delete soundOver;
    delete soundClick;
}

void GuiFlowList::SetRows(const std::vector<FlowRow>* r) {
    rows = r;
    Refresh();
}

void GuiFlowList::Refresh() {
    if (focus >= Count()) focus = Count() > 0 ? Count() - 1 : 0;
    ScrollTo(aim);
    dirty = true;
}

int GuiFlowList::MaxScroll() const {
    const int over = (Count() - visible) * kRowHeight;
    return over > 0 ? over : 0;
}

void GuiFlowList::ScrollTo(float y) {
    const float top = static_cast<float>(MaxScroll());
    aim = y < 0 ? 0 : y > top ? top : y;
}

void GuiFlowList::Select(int index) {
    if (index < 0 || index >= Count()) return;
    focus = index;
    const float top = static_cast<float>(index * kRowHeight);
    const float bottom = top + kRowHeight - visible * kRowHeight;
    if (aim > top) ScrollTo(top);
    else if (aim < bottom) ScrollTo(bottom);
}

int GuiFlowList::GetClicked() {
    const int c = clicked;
    clicked = -1;
    return c;
}

int GuiFlowList::GetClickedBack() {
    const int c = clickedBack;
    clickedBack = -1;
    return c;
}

int GuiFlowList::RowAt(int x, int y) const {
    if (x < x0 || x >= x0 + rowWidth || y < y0 || y >= y0 + visible * kRowHeight) return -1;
    const int index = static_cast<int>((y - y0 + scroll) / kRowHeight);
    return index < Count() ? index : -1;
}

GuiFlowList::Part GuiFlowList::PartAt(int row, int x) const {
    if (row < 0) return Part::None;
    const FlowRow& r = (*rows)[row];
    if (r.kind == FlowRow::Kind::Option && !r.dim) {
        // The arrows' hit areas reach a little past the buttons.
        const int right = x0 + rowWidth - kPad;
        if (x >= right - kStep - 4) return Part::Forward;
        if (x >= right - kStepperW - 4 && x < right - kStepperW + kStep + 4) return Part::Back;
    }
    return Part::Body;
}

bool GuiFlowList::Actionable(int row) const {
    return row >= 0 && row < Count() && (*rows)[row].kind != FlowRow::Kind::Info;
}

bool GuiFlowList::OnTrack(int x, int y) const {
    const int trackX = x0 + rowWidth + 2;
    return Count() > visible && x >= trackX - 8 && x < trackX + kTrackW + 12 && y >= y0 && y < y0 + visible * kRowHeight;
}

void GuiFlowList::ScrollFromTrack(int y) {
    const int trackH = visible * kRowHeight;
    const float at = static_cast<float>(y - y0) / trackH;
    ScrollTo(at * (MaxScroll() + trackH) - trackH / 2.0f);
    scroll = aim;
}

void GuiFlowList::Draw() {
    if (!IsVisible() || !rows) return;
    const int alpha = GetAlpha();

    // Motion: a flung list slows down; otherwise it eases to where it is sent.
    if (grabChan < 0) {
        if (std::fabs(fling) > 0.3f) {
            const float before = aim;
            ScrollTo(aim + fling);
            fling = aim == before ? 0.0f : fling * 0.92f;
        } else {
            fling = 0;
        }
        scroll += (aim - scroll) * 0.35f;
        if (std::fabs(aim - scroll) < 0.5f) scroll = aim;
    }

    const int first = static_cast<int>(scroll) / kRowHeight;
    if (first != textFirst) dirty = true;
    if (dirty) {
        dirty = false;
        textFirst = first;
        for (int i = 0; i <= visible; ++i) {
            const int index = first + i;
            if (index >= Count()) continue;
            const FlowRow& r = (*rows)[index];
            label[i]->SetText(r.label.c_str());
            label[i]->SetFontSize(r.heading ? 20 : r.kind == FlowRow::Kind::Info ? 15 : 18);
            label[i]->SetColor(r.dim ? skin::kInkDim : r.indent ? skin::kInkSoft : skin::kInk);
            const int control = ControlWidth(r);
            label[i]->SetMaxWidth(rowWidth - 2 * kPad - (r.indent ? 22 : 0) - (control ? control + kPad : 0));
            std::string text = r.value;
            if (r.kind == FlowRow::Kind::Action && !r.dim) text += "  \xE2\x80\xBA";
            value[i]->SetText(text.c_str());
            value[i]->SetFontSize(15);
            value[i]->SetColor(r.dim ? skin::kInkDim : r.on ? skin::kAccentInk : skin::kInkSoft);
            value[i]->SetMaxWidth(kChipW - 20);
        }
    }

    // Rows are clipped to the list's box while they scroll past its edges.
    const int boxH = visible * kRowHeight;
    const float sy = Menu_EfbHeight() / 480.0f, sx = Menu_XfbWidth() / static_cast<float>(screenwidth);
    GX_SetScissor(static_cast<u32>(x0 * sx), static_cast<u32>(y0 * sy), static_cast<u32>((rowWidth + 1) * sx),
                  static_cast<u32>(boxH * sy));

    const int lit = dragging ? -1 : hover >= 0 ? hover : (AnyPointer() ? -1 : focus);
    const int right = x0 + rowWidth - kPad;
    for (int i = 0; i <= visible; ++i) {
        const int index = first + i;
        if (index >= Count()) break;
        const FlowRow& r = (*rows)[index];
        const int y = y0 + index * kRowHeight - static_cast<int>(scroll + 0.5f);
        if (y >= y0 + boxH) break;
        const bool isLit = index == lit && r.kind != FlowRow::Kind::Info;
        if (isLit) skin::Draw(skin::rowFocus, x0, y, alpha);
        else if (index + 1 < Count() && !(index + 1 == lit))
            Menu_DrawRectangle(x0 + kPad, y + kRowHeight - 1, rowWidth - 2 * kPad, 1,
                               skin::WithAlpha((GXColor){232, 232, 238, 255}, alpha), 1);

        const int labelH = r.heading ? 20 : r.kind == FlowRow::Kind::Info ? 15 : 18;
        label[i]->SetPosition(x0 + kPad + (r.indent ? 22 : 0), y + (kRowHeight - labelH) / 2 - 2);
        label[i]->Draw();

        const int cy = y + (kRowHeight - kChipH) / 2;  // controls' top
        const int textY = y + (kRowHeight - 15) / 2 - 2;
        const auto chipText = [&](int cx) {
            const int tw = value[i]->GetTextWidth();
            const int shown = tw > kChipW - 20 ? kChipW - 20 : tw;
            value[i]->SetPosition(cx + (kChipW - shown) / 2, textY);
            value[i]->Draw();
        };
        switch (r.kind) {
            case FlowRow::Kind::Option:
                if (r.dim) {
                    skin::Draw(skin::chipOff, right - kChipW - 2, cy - 3, alpha);
                    chipText(right - kChipW);
                } else {
                    const int backX = right - kStepperW, chipX = backX + kStep + kGap, fwdX = right - kStep;
                    const int by = y + (kRowHeight - kStep) / 2;
                    const bool overBack = index == hover && hoverPart == Part::Back && !dragging;
                    const bool overFwd = index == hover && hoverPart == Part::Forward && !dragging;
                    skin::Draw(overBack ? skin::stepBackOver : skin::stepBack, backX - 4, by - 4, alpha);
                    skin::Draw(r.on ? skin::chipOn : skin::chipOff, chipX - 2, cy - 3, alpha);
                    skin::Draw(overFwd ? skin::stepForwardOver : skin::stepForward, fwdX - 4, by - 4, alpha);
                    chipText(chipX);
                }
                break;
            case FlowRow::Kind::Toggle: {
                const int swX = right - kSwitchW;
                skin::Draw(r.on ? skin::switchOn : skin::switchOff, swX - 3, cy - 4, r.dim ? alpha / 2 : alpha);
                const int tw = value[i]->GetTextWidth();
                value[i]->SetPosition(swX - 12 - tw, textY);
                value[i]->Draw();
                break;
            }
            case FlowRow::Kind::Action:
            case FlowRow::Kind::Header:
                if (!r.value.empty()) {
                    skin::Draw(r.on ? skin::chipOn : skin::chipOff, right - kChipW - 2, cy - 3, r.dim ? alpha / 2 : alpha);
                    chipText(right - kChipW);
                }
                break;
            case FlowRow::Kind::Info:
                break;
        }
    }
    GX_SetScissor(0, 0, Menu_XfbWidth(), Menu_EfbHeight());

    if (Count() > visible) {
        // Where the list is, along its right edge; A on it (held) drags it.
        const int trackX = x0 + rowWidth + 2, trackH = boxH - 8;
        const int total = Count() * kRowHeight;
        int thumb = trackH * boxH / total;
        if (thumb < 24) thumb = 24;
        const int thumbY = y0 + 4 + static_cast<int>((trackH - thumb) * scroll / MaxScroll());
        Menu_DrawRectangle(trackX, y0 + 4, kTrackW, trackH, skin::WithAlpha((GXColor){230, 230, 236, 255}, alpha), 1);
        Menu_DrawRectangle(trackX, thumbY, kTrackW, thumb,
                           skin::WithAlpha(grabTrack ? skin::kAccent : (GXColor){168, 168, 180, 255}, alpha), 1);
    }
    UpdateEffects();
}

void GuiFlowList::Update(GuiTrigger* t) {
    if (state == STATE::DISABLED || !t || Count() == 0) return;
    const bool pointing = t->wpad && t->wpad->ir.valid;
    const int px = pointing ? static_cast<int>(t->wpad->ir.x) : 0;
    const int py = pointing ? static_cast<int>(t->wpad->ir.y) : 0;

    // A held on the list by this Wii Remote: follow it until A is let go.
    if (grabChan >= 0 && t->chan == grabChan) {
        const bool held = t->wpad && (t->wpad->btns_h & kWpadA);
        if (held && pointing) {
            if (grabTrack) {
                ScrollFromTrack(py);
            } else {
                if (!dragging && std::abs(py - grabY) > kDragStart) {
                    dragging = true;
                    hover = -1;
                }
                if (dragging) {
                    const float before = scroll;
                    ScrollTo(grabScroll - (py - grabY));
                    scroll = aim;
                    fling = fling * 0.5f + (scroll - before) * 0.5f;
                }
            }
            return;
        }
        // Let go: a press that stayed put acts on its row.
        if (!dragging && !grabTrack && pointing && RowAt(px, py) == grabRow && Actionable(grabRow)) {
            (grabPart == Part::Back ? clickedBack : clicked) = grabRow;
            soundClick->Play();
        } else if (!dragging) {
            fling = 0;
        }
        grabChan = -1;
        dragging = grabTrack = false;
        return;
    }
    if (grabChan >= 0) return;  // another Wii Remote holds the list

    const bool a = Pressed(t, kWpadA, PAD_BUTTON_A, WIIDRC_BUTTON_A);
    const bool back = Pressed(t, WPAD_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_MINUS, PAD_BUTTON_Y, WIIDRC_BUTTON_MINUS);
    if (pointing) {
        // While pointing, the D-pad scrolls by a row.
        if (t->Down()) ScrollTo(aim + kRowHeight);
        else if (t->Up()) ScrollTo(aim - kRowHeight);
        if (a && OnTrack(px, py)) {
            grabChan = t->chan;
            grabTrack = true;
            fling = 0;
            ScrollFromTrack(py);
            return;
        }
        const int row = RowAt(px, py);
        const Part part = PartAt(row, px);
        if ((row != hover || part != hoverPart) && Actionable(row)) soundOver->Play();
        hover = row;
        hoverPart = part;
        if (row >= 0) focus = row;
        if (a && px >= x0 && px < x0 + rowWidth && py >= y0 && py < y0 + visible * kRowHeight) {
            grabChan = t->chan;
            grabY = py;
            grabScroll = aim;
            grabRow = row;
            grabPart = part;
            dragging = false;
            fling = 0;
            return;
        }
        if (back && Actionable(row)) {
            clickedBack = row;
            soundClick->Play();
        }
        return;
    }
    if (AnyPointer()) return;
    hover = -1;
    int target = focus;
    if (t->Down()) target = focus + 1;
    else if (t->Up()) target = focus - 1;
    if (target >= Count()) target = Count() - 1;
    if (target < 0) target = 0;
    if (target != focus) {
        Select(target);
        soundOver->Play();
    }
    const FlowRow& r = (*rows)[focus];
    const bool live = r.kind != FlowRow::Kind::Info;
    // Left and Right step an option, as its arrows do.
    const bool stepper = r.kind == FlowRow::Kind::Option && !r.dim;
    if (live && (a || (stepper && t->Right()))) {
        clicked = focus;
        soundClick->Play();
    } else if (live && (back || (stepper && t->Left()))) {
        clickedBack = focus;
        soundClick->Play();
    }
}
