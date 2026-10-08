// SPDX-FileCopyrightText: 2026 RiftWii contributors
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
constexpr int kArrow = 34;       // skin::scrollUp/scrollDown
constexpr int kArrowRepeat = 8;  // frames between rows while an arrow is held
constexpr int kArrowRoom = 12;   // controls move in this far while the scroll arrows show
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
        const int right = ControlsRight();
        if (x >= right - kStep - 4) return Part::Forward;
        if (x >= right - kStepperW - 4 && x < right - kStepperW + kStep + 4) return Part::Back;
    }
    return Part::Body;
}

bool GuiFlowList::Actionable(int row) const {
    return row >= 0 && row < Count() && (*rows)[row].kind != FlowRow::Kind::Info;
}

// Where the rows' controls end: in from the right edge, further while the
// list scrolls, so the arrows on that edge do not touch a switch or chip.
int GuiFlowList::ControlsRight() const { return x0 + rowWidth - kPad - (Count() > visible ? kArrowRoom : 0); }

// The arrows sit on the list's right edge, at the ends of its track.
int GuiFlowList::ArrowX() const { return x0 + rowWidth + 5 - kArrow / 2; }

int GuiFlowList::ArrowY(int dir) const { return dir < 0 ? y0 : y0 + visible * kRowHeight - kArrow; }

int GuiFlowList::ArrowAt(int x, int y) const {
    if (Count() <= visible) return 0;
    for (int dir = -1; dir <= 1; dir += 2) {
        if (dir < 0 ? aim <= 0 : aim >= MaxScroll()) continue;
        // The hit area reaches a little past the button.
        const int ax = ArrowX(), ay = ArrowY(dir);
        if (x >= ax && x < ax + kArrow + 8 && y >= ay - 6 && y < ay + kArrow + 6) return dir;
    }
    return 0;
}

bool GuiFlowList::OnTrack(int x, int y) const {
    const int trackX = x0 + rowWidth + 2;
    return Count() > visible && x >= trackX - 8 && x < trackX + kTrackW + 12 && y >= y0 + kArrow + 4 &&
           y < y0 + visible * kRowHeight - kArrow - 4;
}

void GuiFlowList::ScrollFromTrack(int y) {
    const int top = y0 + kArrow + 4, trackH = visible * kRowHeight - 2 * (kArrow + 4);
    const float at = static_cast<float>(y - top) / trackH;
    const int boxH = visible * kRowHeight;
    ScrollTo(at * (MaxScroll() + boxH) - boxH / 2.0f);
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
            label[i]->SetMaxWidth(ControlsRight() - x0 - kPad - (r.indent ? 22 : 0) - (control ? control + kPad : 0));
            std::string text = r.value;
            if (r.kind == FlowRow::Kind::Action && !r.dim) text += "  \xE2\x80\xBA";
            value[i]->SetText(text.c_str());
            // A long value (a translation, "Remove all (experimental)")
            // steps down to 13 points before the chip cuts it off.
            for (int size = 15; size >= 13; --size) {
                value[i]->SetFontSize(size);
                if (value[i]->GetTextWidth() <= kChipW - 20) break;
            }
            value[i]->SetColor(r.dim ? skin::kInkDim : r.on ? skin::kAccentInk : skin::kInkSoft);
            value[i]->SetMaxWidth(kChipW - 20);
        }
    }

    // Rows are clipped to the list's box while they scroll past its edges.
    const int boxH = visible * kRowHeight;
    Menu_Scissor(x0, y0, rowWidth + 1, boxH);

    const int lit = dragging ? -1 : hover >= 0 ? hover : (AnyPointer() ? -1 : focus);
    const int right = ControlsRight();
    // The highlight glides to the lit row, and fades in or out when there
    // is none (the pointer off the list).
    const bool litShown = lit >= 0 && lit < Count() && (*rows)[lit].kind != FlowRow::Kind::Info;
    if (litShown) {
        if (litFade <= 0.0f) litPos = static_cast<float>(lit);
        litPos += (lit - litPos) * 0.35f;
        if (std::fabs(lit - litPos) < 0.01f) litPos = static_cast<float>(lit);
    }
    litFade += ((litShown ? 1.0f : 0.0f) - litFade) * 0.3f;
    if (litFade < 0.02f) litFade = 0.0f;
    if (litFade > 0.0f)
        skin::Draw(skin::rowFocus, x0, y0 + litPos * kRowHeight - scroll, static_cast<int>(alpha * litFade));
    if (static_cast<int>(switchPos.size()) != Count()) {
        switchPos.assign(Count(), -1.0f);
        shownValue.assign(Count(), std::string());
        valueIn.assign(Count(), 1.0f);
    }
    for (int i = 0; i <= visible; ++i) {
        const int index = first + i;
        if (index >= Count()) break;
        const FlowRow& r = (*rows)[index];
        const int y = y0 + index * kRowHeight - static_cast<int>(scroll + 0.5f);
        if (y >= y0 + boxH) break;
        const bool isLit = index == lit && r.kind != FlowRow::Kind::Info;
        if (isLit) {
            // (the highlight, drawn above)
        } else if (index + 1 < Count() && !(index + 1 == lit) &&
                 !(r.kind == FlowRow::Kind::Info && (*rows)[index + 1].kind == FlowRow::Kind::Info))
            Menu_DrawRectangle(x0 + kPad, y + kRowHeight - 1, rowWidth - 2 * kPad, 1,
                               skin::WithAlpha(skin::kDivider, alpha), 1);

        const int labelH = r.heading ? 20 : r.kind == FlowRow::Kind::Info ? 15 : 18;
        label[i]->SetPosition(x0 + kPad + (r.indent ? 22 : 0), y + (kRowHeight - labelH) / 2 - 2);
        label[i]->Draw();

        const int cy = y + (kRowHeight - kChipH) / 2;  // controls' top
        const int textY = y + (kRowHeight - 15) / 2 - 2;
        // A value that just changed comes in from the side, fading up.
        if (shownValue[index] != r.value) {
            if (!shownValue[index].empty() || valueIn[index] < 1.0f) valueIn[index] = 0.0f;
            shownValue[index] = r.value;
        }
        valueIn[index] += (1.0f - valueIn[index]) * 0.25f;
        if (valueIn[index] > 0.98f) valueIn[index] = 1.0f;
        const auto chipText = [&](int cx) {
            const int tw = value[i]->GetTextWidth();
            const int shown = tw > kChipW - 20 ? kChipW - 20 : tw;
            value[i]->SetPosition(cx + (kChipW - shown) / 2 + static_cast<int>(10.0f * (1.0f - valueIn[index])), textY);
            value[i]->SetAlpha(static_cast<int>(255 * valueIn[index]));
            value[i]->Draw();
            value[i]->SetAlpha(255);
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
                // Switched: one picture fades into the other.
                float& sw = switchPos[index];
                if (sw < 0.0f) sw = r.on ? 1.0f : 0.0f;
                sw += ((r.on ? 1.0f : 0.0f) - sw) * 0.3f;
                if (std::fabs((r.on ? 1.0f : 0.0f) - sw) < 0.02f) sw = r.on ? 1.0f : 0.0f;
                const int swAlpha = r.dim ? alpha / 2 : alpha;
                if (sw < 1.0f) skin::Draw(skin::switchOff, swX - 3, cy - 4, swAlpha);
                if (sw > 0.0f) skin::Draw(skin::switchOn, swX - 3, cy - 4, static_cast<int>(swAlpha * sw));
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
        const int trackX = x0 + rowWidth + 2, trackTop = y0 + kArrow + 4, trackH = boxH - 2 * (kArrow + 4);
        const int total = Count() * kRowHeight;
        int thumb = trackH * boxH / total;
        if (thumb < 16) thumb = 16;
        const int thumbY = trackTop + static_cast<int>((trackH - thumb) * scroll / MaxScroll());
        Menu_DrawRectangle(trackX, trackTop, kTrackW, trackH, skin::WithAlpha(skin::kScrollTrack, alpha), 1);
        Menu_DrawRectangle(trackX, thumbY, kTrackW, thumb,
                           skin::WithAlpha(grabTrack ? skin::kAccent : skin::kScrollThumb, alpha), 1);
        // Each arrow shows while the list can still go its way.
        if (aim > 0) {
            const bool over = hoverArrow < 0 || grabArrow < 0;
            skin::Draw(over ? skin::scrollUpOver : skin::scrollUp, ArrowX() - 4, ArrowY(-1) - 4, alpha);
        }
        if (aim < MaxScroll()) {
            const bool over = hoverArrow > 0 || grabArrow > 0;
            skin::Draw(over ? skin::scrollDownOver : skin::scrollDown, ArrowX() - 4, ArrowY(1) - 4, alpha);
        }
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
        if (grabArrow != 0) {
            // A held arrow keeps going, a row at a time, until A is let go
            // or the list reaches that end.
            if (held && pointing && ArrowAt(px, py) == grabArrow) {
                if (++arrowHeld >= 2 * kArrowRepeat && arrowHeld % kArrowRepeat == 0)
                    ScrollTo(aim + grabArrow * kRowHeight);
                return;
            }
            grabChan = -1;
            grabArrow = 0;
            return;
        }
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
        const int arrow = ArrowAt(px, py);
        if (arrow != hoverArrow && arrow != 0) soundOver->Play();
        hoverArrow = arrow;
        if (arrow != 0) {
            hover = -1;
            if (a) {
                // One row per press; held, it repeats.
                ScrollTo(aim + arrow * kRowHeight);
                soundClick->Play();
                grabChan = t->chan;
                grabArrow = arrow;
                arrowHeld = 0;
                fling = 0;
            }
            return;
        }
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
    hoverArrow = 0;
    int target = focus;
    if (t->Down()) target = focus + 1;
    else if (t->Up()) target = focus - 1;
    // A group's name is passed over; past the first, the list shows its top.
    const int dir = target - focus;
    const auto isHeading = [&](int i) { return (*rows)[i].kind == FlowRow::Kind::Info && (*rows)[i].heading; };
    while (dir != 0 && target >= 0 && target < Count() && isHeading(target)) target += dir;
    if (dir != 0 && (target < 0 || target >= Count())) {
        if (dir < 0) ScrollTo(0);
        target = focus;
    }
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
