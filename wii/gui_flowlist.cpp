// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui_flowlist.hpp"

#include "skin.hpp"
#include "wiidrc.h"

namespace skin = riftwii::wii::skin;

namespace {

constexpr int kChipW = 176;
constexpr int kPad = 12;

bool AnyPointer() {
    for (int i = 0; i < 4; i++)
        if (userInput[i].wpad && userInput[i].wpad->ir.valid) return true;
    return false;
}

bool Pressed(GuiTrigger* t, u32 wpad, u16 pad, u16 drc) {
    return (t->wpad && (t->wpad->btns_d & wpad)) || (t->pad.btns_d & pad) || (t->wiidrcdata.btns_d & drc);
}

}  // namespace

GuiFlowList::GuiFlowList(int x, int y, int w, int n)
    : x0(x), y0(y), rowWidth(w), visible(n > kMaxVisible ? kMaxVisible : n < 1 ? 1 : n) {
    width = screenwidth;
    height = screenheight;
    selectable = true;
    for (int i = 0; i < kMaxVisible; ++i) {
        label[i] = new GuiText(nullptr, 15, skin::kInk);
        label[i]->SetParent(this);
        label[i]->SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
        value[i] = new GuiText(nullptr, 13, skin::kInkSoft);
        value[i]->SetParent(this);
        value[i]->SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
    }
    soundOver = new GuiSound(button_over_pcm, button_over_pcm_size, SOUND::PCM);
    soundClick = new GuiSound(button_click_pcm, button_click_pcm_size, SOUND::PCM);
}

GuiFlowList::~GuiFlowList() {
    for (int i = 0; i < kMaxVisible; ++i) {
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
    ScrollTo(offset);
    dirty = true;
}

void GuiFlowList::ScrollTo(int first) {
    const int maxFirst = Count() > visible ? Count() - visible : 0;
    if (first > maxFirst) first = maxFirst;
    if (first < 0) first = 0;
    offset = first;
    dirty = true;
}

void GuiFlowList::Select(int index) {
    if (index < 0 || index >= Count()) return;
    focus = index;
    if (focus < offset) ScrollTo(focus);
    else if (focus >= offset + visible) ScrollTo(focus - visible + 1);
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
    const int index = offset + (y - y0) / kRowHeight;
    return index < Count() ? index : -1;
}

void GuiFlowList::Draw() {
    if (!IsVisible() || !rows) return;
    const int alpha = GetAlpha();
    if (dirty) {
        dirty = false;
        for (int i = 0; i < visible; ++i) {
            const int index = offset + i;
            if (index >= Count()) continue;
            const FlowRow& r = (*rows)[index];
            label[i]->SetText(r.label.c_str());
            const bool header = r.kind == FlowRow::Kind::Header;
            label[i]->SetFontSize(header ? 17 : r.kind == FlowRow::Kind::Info ? 13 : 15);
            label[i]->SetColor(r.dim ? skin::kInkDim : header ? skin::kInk : skin::kInkSoft);
            label[i]->SetMaxWidth(rowWidth - (r.value.empty() ? 2 * kPad : kChipW + 3 * kPad));
            std::string chip = r.value;
            if (r.kind == FlowRow::Kind::Option) chip = "\xE2\x80\xB9  " + chip + "  \xE2\x80\xBA";
            value[i]->SetText(chip.c_str());
            value[i]->SetColor(r.on ? skin::kAccentInk : skin::kInkSoft);
            value[i]->SetMaxWidth(kChipW - 16);
        }
    }
    const int lit = hover >= 0 ? hover : (AnyPointer() ? -1 : focus);
    for (int i = 0; i < visible; ++i) {
        const int index = offset + i;
        if (index >= Count()) break;
        const FlowRow& r = (*rows)[index];
        const int y = y0 + i * kRowHeight;
        if (index == lit && r.kind != FlowRow::Kind::Info) skin::Draw(skin::rowFocus, x0 + (rowWidth - 548) / 2, y - 1, alpha);
        const int indent = r.kind == FlowRow::Kind::Option ? 18 : 0;
        label[i]->SetPosition(x0 + kPad + indent, y + (r.kind == FlowRow::Kind::Header ? 7 : 9));
        label[i]->Draw();
        if (!r.value.empty()) {
            const int cx = x0 + rowWidth - kPad - kChipW;
            skin::Draw(r.on ? skin::chipOn : skin::chipOff, cx - 2, y + 2 - 3, alpha);
            const int tw = value[i]->GetTextWidth();
            const int shown = tw > kChipW - 16 ? kChipW - 16 : tw;
            value[i]->SetPosition(cx + (kChipW - shown) / 2, y + 9);
            value[i]->Draw();
        }
    }
    if (Count() > visible) {
        // Scroll position along the right edge.
        const int trackX = x0 + rowWidth + 4, trackH = visible * kRowHeight - 8;
        const int thumb = trackH * visible / Count() < 18 ? 18 : trackH * visible / Count();
        const int thumbY = y0 + 4 + (trackH - thumb) * offset / (Count() - visible);
        Menu_DrawRectangle(trackX, y0 + 4, 4, trackH, skin::WithAlpha((GXColor){226, 226, 232, 255}, alpha), 1);
        Menu_DrawRectangle(trackX, thumbY, 4, thumb, skin::WithAlpha((GXColor){170, 170, 182, 255}, alpha), 1);
    }
    UpdateEffects();
}

void GuiFlowList::Update(GuiTrigger* t) {
    if (state == STATE::DISABLED || !t || Count() == 0) return;
    const bool a = Pressed(t, WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A, PAD_BUTTON_A, WIIDRC_BUTTON_A);
    const bool back = Pressed(t, WPAD_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_MINUS, PAD_BUTTON_Y, WIIDRC_BUTTON_MINUS);
    if (t->wpad && t->wpad->ir.valid) {
        const int px = static_cast<int>(t->wpad->ir.x), py = static_cast<int>(t->wpad->ir.y);
        // While pointing, the D-pad scrolls, and A on the scroll bar pages.
        if (t->Down()) ScrollTo(offset + 1);
        else if (t->Up()) ScrollTo(offset - 1);
        const int trackX = x0 + rowWidth + 4;
        if (a && Count() > visible && px >= trackX - 10 && px < trackX + 16 && py >= y0 &&
            py < y0 + visible * kRowHeight) {
            ScrollTo(py < y0 + visible * kRowHeight / 2 ? offset - visible : offset + visible);
            soundClick->Play();
            return;
        }
        const int row = RowAt(px, py);
        if (row != hover && row >= 0 && (*rows)[row].kind != FlowRow::Kind::Info) soundOver->Play();
        hover = row;
        if (row >= 0) focus = row;
        if (row >= 0 && (a || back) && (*rows)[row].kind != FlowRow::Kind::Info) {
            (a ? clicked : clickedBack) = row;
            soundClick->Play();
        }
        return;
    }
    if (AnyPointer()) return;
    hover = -1;
    int target = focus;
    if (t->Down()) target = focus + 1;
    else if (t->Up()) target = focus - 1;
    else if (t->Right()) target = focus + visible;
    else if (t->Left()) target = focus - visible;
    if (target >= Count()) target = Count() - 1;
    if (target < 0) target = 0;
    if (target != focus) {
        Select(target);
        soundOver->Play();
    }
    if ((a || back) && (*rows)[focus].kind != FlowRow::Kind::Info) {
        (a ? clicked : clickedBack) = focus;
        soundClick->Play();
    }
}
