// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui_flowlist.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "i18n.hpp"
#include "skin.hpp"
#include "transition.hpp"
#include "gui_motion.hpp"
#include "video.h"
#include "wiidrc.h"

namespace skin = riftwii::wii::skin;
namespace motion = riftwii::wii::transition;

namespace {

constexpr int kPad = 14;
constexpr int kChipW = 208, kChipH = 30;  // skin::chipOn/chipOff
constexpr int kOpensW = 20;                // an Action's lone ">" (FlowRow::opens)
constexpr int kStep = 34;                 // skin::stepBack/stepForward
constexpr int kGap = 6;
constexpr int kSwitchW = 60;  // skin::switchOn/switchOff
constexpr int kStepperW = kStep + kGap + kChipW + kGap + kStep;
// On cards a stepper's value is plain text between its arrows (no chip
// picture), so the arrows close in on it, down to this much room, and the
// label gets the rest: "Games on the SD card" was cut to "Games on the SD
// c..." beside a short "Look for them". The floor keeps the usual short
// values (On, Off, Auto) at one width, their arrows in line from row to row.
constexpr int kStepperMinMiddle = 140;
constexpr int kDragStart = 8;  // pixels the pointer moves before a press becomes a drag
constexpr int kTrackW = 6;
constexpr int kArrow = 34;       // skin::scrollUp/scrollDown
constexpr int kArrowRepeat = 8;  // frames between rows while an arrow is held
// While the list scrolls, its arrows and track take the right end of the
// list's own width (so a scrolling list and a short one span the same
// space, the page's 40 to 600): the rows end this far in.
constexpr int kScrollColumn = kArrow + 8;
constexpr int kIconSide = 32;       // a row's picture (FlowRow::icon)
constexpr int kIconRoom = kIconSide + 10;  // what it and the gap after it take from the label
constexpr int kCardGap = 3;         // above and below each card, inside its row
constexpr int kCardShadow = 8;      // skin::tile9's margin
constexpr u32 kWpadA = WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A;

bool AnyPointer() {
    for (int i = 0; i < 4; i++)
        if (userInput[i].wpad && userInput[i].wpad->ir.valid) return true;
    return false;
}

bool Pressed(GuiTrigger* t, u32 wpad, u16 pad, u16 drc) {
    return (t->wpad && (t->wpad->btns_d & wpad)) || (t->pad.btns_d & pad) || (t->wiidrcdata.btns_d & drc);
}

// An Action with no value of its own that leads somewhere: just its ">".
bool OpensScreen(const FlowRow& r) {
    return r.kind == FlowRow::Kind::Action && r.value.empty() && r.opens && !r.dim;
}

// On cards, the rows whose value is drawn as plain text at their end
// (everything but a live Option's stepper and a Toggle's switch).
bool EndsInText(const FlowRow& r) {
    if (r.kind == FlowRow::Kind::Toggle) return false;
    return !(r.kind == FlowRow::Kind::Option && !r.dim);
}

// The width a row's control takes at its right.
int ControlWidth(const FlowRow& r) {
    switch (r.kind) {
        case FlowRow::Kind::Option: return r.dim ? kChipW : kStepperW;
        case FlowRow::Kind::Toggle: return kSwitchW + 64;
        case FlowRow::Kind::Action:
            if (r.value.empty()) return r.opens && !r.dim ? kOpensW : 0;
            return kChipW;
        case FlowRow::Kind::Header: return r.value.empty() ? 0 : kChipW;
        case FlowRow::Kind::Info: return r.value.empty() ? 0 : kChipW;
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
        detail[i] = new GuiText(nullptr, 14, skin::kInkDim);
        detail[i]->SetParent(this);
        detail[i]->SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
        tag[i] = new GuiText(nullptr, 13, skin::kAccentInk);
        tag[i]->SetParent(this);
        tag[i]->SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
    }
    soundOver = new GuiSound(button_over_pcm, button_over_pcm_size, SOUND::PCM);
    soundClick = new GuiSound(button_click_pcm, button_click_pcm_size, SOUND::PCM);
}

GuiFlowList::~GuiFlowList() {
    for (int i = 0; i <= kMaxVisible; ++i) {
        delete label[i];
        delete value[i];
        delete detail[i];
        delete tag[i];
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
    const int over = (Count() - visible) * rowH;
    return over > 0 ? over : 0;
}

void GuiFlowList::ScrollTo(float y) {
    const float top = static_cast<float>(MaxScroll());
    aim = y < 0 ? 0 : y > top ? top : y;
}

void GuiFlowList::Select(int index) {
    if (index < 0 || index >= Count()) return;
    focus = index;
    const float top = static_cast<float>(index * rowH);
    const float bottom = top + rowH - visible * rowH;
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
    if (x < x0 || x >= RowsRight() || y < y0 || y >= y0 + visible * rowH) return -1;
    const int index = static_cast<int>((y - y0 + scroll) / rowH);
    return index < Count() ? index : -1;
}

GuiFlowList::Part GuiFlowList::PartAt(int row, int x, int y) const {
    if (row < 0) return Part::None;
    const FlowRow& r = (*rows)[row];
    // Under SetNotesUnder the arrows are on the top line; the explanation
    // under them is the row's (A there opens the choice's list).
    const bool onControlLine = !(notesUnder && !r.detail.empty()) || y < RowTop(row) + kCardGap + LineHeight(r) + 2;
    if (r.kind == FlowRow::Kind::Option && !r.dim && onControlLine) {
        // The arrows' hit areas reach a little past the buttons.
        const int right = ControlsRight();
        const int stepperW = StepperWidth(row);
        if (x >= right - kStep - 4) return Part::Forward;
        if (x >= right - stepperW - 4 && x < right - stepperW + kStep + 4) return Part::Back;
    }
    return Part::Body;
}

// A live Option row's stepper, arrows and all: as measured when its texts
// were set, or the full width for a row not shown then.
int GuiFlowList::StepperWidth(int row) const {
    const int slot = row - textFirst;
    const int middle = slot >= 0 && slot <= visible && stepperMiddle[slot] != 0 ? stepperMiddle[slot] : kChipW;
    return kStep + kGap + middle + kGap + kStep;
}

bool GuiFlowList::Actionable(int row) const {
    return row >= 0 && row < Count() && (*rows)[row].kind != FlowRow::Kind::Info;
}

// Where the rows' controls end: in from the right edge, further while the
// list scrolls, so the arrows on that edge do not touch a switch or chip.
int GuiFlowList::ControlsRight() const { return RowsRight() - kPad; }

// Where the rows end: the list's right edge, or short of its scroll
// column while it scrolls.
int GuiFlowList::RowsRight() const { return x0 + rowWidth - (Count() > visible ? kScrollColumn : 0); }

// A card's edges: indented under its heading as its text is, short of the
// scroll arrows while they show.
int GuiFlowList::CardLeft(const FlowRow& r) const { return x0 + (r.indent ? 22 : 0); }
int GuiFlowList::CardRight() const { return RowsRight(); }

// The arrows end at the list's right edge, at the ends of its track.
int GuiFlowList::ArrowX() const { return x0 + rowWidth - kArrow; }

// The track runs down the middle of the arrows' column.
int GuiFlowList::TrackX() const { return x0 + rowWidth - kArrow / 2 - kTrackW / 2; }

int GuiFlowList::ArrowY(int dir) const { return dir < 0 ? y0 : y0 + visible * rowH - kArrow; }

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
    const int trackX = TrackX();
    return Count() > visible && x >= trackX - 8 && x < trackX + kTrackW + 12 && y >= y0 + kArrow + 4 &&
           y < y0 + visible * rowH - kArrow - 4;
}

void GuiFlowList::ScrollFromTrack(int y) {
    const int top = y0 + kArrow + 4, trackH = visible * rowH - 2 * (kArrow + 4);
    const float at = static_cast<float>(y - top) / trackH;
    const int boxH = visible * rowH;
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
            ScrollTo(aim + motion::Coast(fling, 200.0f));
            if (aim == before) fling = 0.0f;
        } else {
            fling = 0;
        }
        scroll = motion::Follow(scroll, aim, 40.0f);
        if (std::fabs(aim - scroll) < 0.5f) scroll = aim;
    }

    const int first = static_cast<int>(scroll) / rowH;
    if (first != textFirst) dirty = true;
    if (dirty) {
        dirty = false;
        textFirst = first;
        for (int i = 0; i <= visible; ++i) {
            const int index = first + i;
            if (index >= Count()) continue;
            const FlowRow& r = (*rows)[index];
            label[i]->SetText(r.label.c_str());
            const bool note = r.kind == FlowRow::Kind::Info && r.value.empty();
            label[i]->SetFontSize(r.heading ? 20 : note ? 15 : 18);
            label[i]->SetColor(r.dim ? skin::kInkDim : r.indent ? skin::kInkSoft : skin::kInk);
            // GuiText translates what it is given, but an action's value
            // gets the arrow first, and "Rescan  >" is no catalog key: the
            // value is translated here, before the arrow (issue 84). A value
            // already translated with tr() is no key either and stays as it is.
            std::string text = riftwii::wii::tr(r.value.c_str());
            if (r.kind == FlowRow::Kind::Action && !r.dim && !(cards && r.button)) {
                if (!r.value.empty())
                    text += "  \xE2\x80\xBA";
                else if (r.opens)
                    text = "\xE2\x80\xBA";
            }
            value[i]->SetText(text.c_str());
            // A long value (a theme's name, a translation) keeps the
            // list's size and the chip cuts it off: one row in smaller
            // letters than the rest looked out of place.
            value[i]->SetFontSize(15);
            value[i]->SetColor(r.dim ? skin::kInkDim : r.on ? skin::kAccentInk : skin::kInkSoft);
            value[i]->SetMaxWidth(kChipW - 20);
            // On cards a value is plain text at the row's end (endText), so
            // the name may use the room up to it, not a whole chip's: "Get >"
            // left a theme's name only "Dashb..." on a half-width list.
            int control = ControlWidth(r);
            if (cards && EndsInText(r) && !text.empty()) control = std::min(value[i]->GetTextWidth(), kChipW - 20);
            if (cards && r.button && !text.empty()) control += 32;  // its button's padding
            stepperMiddle[i] = kChipW;
            if (cards && r.kind == FlowRow::Kind::Option && !r.dim) {
                const int needed = value[i]->GetTextWidth() + 20;
                stepperMiddle[i] = std::max(kStepperMinMiddle, std::min(needed, kChipW));
                control = kStep + kGap + stepperMiddle[i] + kGap + kStep;
            }
            label[i]->SetMaxWidth(ControlsRight() - x0 - kPad - (r.indent ? 22 : 0) - (r.icon_column ? kIconRoom : 0) -
                                  (control ? control + kPad : 0));
            detail[i]->SetText(r.detail.empty() ? nullptr : r.detail.c_str());
            detail[i]->SetColor(skin::kInkDim);
            tag[i]->SetText(notesUnder && !r.tag.empty() ? r.tag.c_str() : nullptr);
            const int textRoom = ControlsRight() - x0 - kPad - (r.indent ? 22 : 0) - (r.icon_column ? kIconRoom : 0);
            if (notesUnder) {
                // Under the control: the row's whole width, less the tag.
                const int tagRoom = r.tag.empty() ? 0 : tag[i]->GetTextWidth() + kPad;
                detail[i]->SetMaxWidth(textRoom - tagRoom);
            } else {
                detail[i]->SetMaxWidth(textRoom - (control ? control + kPad : 0));
            }
        }
    }

    // Rows are clipped to the list's box while they scroll past its edges.
    const int boxH = visible * rowH;
    // Cards' shadows reach past the box's sides: a little room there.
    if (cards) Menu_Scissor(x0 - kCardShadow, y0, rowWidth + 2 * kCardShadow, boxH);
    else Menu_Scissor(x0, y0, rowWidth + 1, boxH);

    const int lit = dragging ? -1 : hover >= 0 ? hover : (AnyPointer() || !keysActive ? -1 : focus);
    const int right = ControlsRight();
    // The highlight glides to the lit row, and fades in or out when there
    // is none (the pointer off the list).
    const bool litShown = lit >= 0 && lit < Count() && (*rows)[lit].kind != FlowRow::Kind::Info;
    // The lit row's name scrolls when it does not fit (a cheat's long
    // name; the others end in "..."), as Friivolution's lists do.
    for (int i = 0; i <= visible; ++i)
        label[i]->SetScroll(textFirst + i == lit && litShown ? SCROLL::HORIZONTAL : SCROLL::NONE);
    if (litShown) {
        litRow = lit;
        if (litFade <= 0.0f) litPos = static_cast<float>(lit);
        litPos = motion::Follow(litPos, static_cast<float>(lit), 40.0f);
        if (std::fabs(lit - litPos) < 0.01f) litPos = static_cast<float>(lit);
    }
    litFade = motion::Follow(litFade, litShown ? 1.0f : 0.0f, 47.0f);
    if (litFade < 0.02f) litFade = 0.0f;
    if (cards) {
        // What can be pressed looks it: each such row on a card, the lit
        // one with the tiles' ring; what is only read (facts, notes, group
        // names) is plain text on the backdrop. The texts go over them.
        for (int i = 0; i <= visible; ++i) {
            const int index = first + i;
            if (index >= Count()) break;
            const FlowRow& r = (*rows)[index];
            if (r.kind == FlowRow::Kind::Info) continue;
            const float y = y0 + index * rowH - scroll + kCardGap;
            const float left = static_cast<float>(CardLeft(r));
            skin::DrawNine(skin::tile9, left - kCardShadow, y - kCardShadow, CardRight() - left + 2 * kCardShadow,
                           rowH - 2 * kCardGap + 2 * kCardShadow, skin::kTileCorner, r.dim ? alpha * 3 / 4 : alpha);
        }
        if (litFade > 0.0f && litRow >= 0 && litRow < Count()) {
            const float y = y0 + litPos * rowH - scroll + kCardGap;
            const float left = static_cast<float>(CardLeft((*rows)[litRow]));
            skin::DrawNine(skin::tile9Over, left - kCardShadow, y - kCardShadow, CardRight() - left + 2 * kCardShadow,
                           rowH - 2 * kCardGap + 2 * kCardShadow, skin::kTileCorner, static_cast<int>(alpha * litFade * motion::Pulse()));
        }
    } else if (litFade > 0.0f) {
        skin::Draw(skin::rowFocus, x0, y0 + litPos * rowH - scroll, static_cast<int>(alpha * litFade * motion::Pulse()));
    }
    if (static_cast<int>(switchPos.size()) != Count()) {
        switchPos.assign(Count(), -1.0f);
        shownValue.assign(Count(), std::string());
        valueIn.assign(Count(), 1.0f);
    }
    for (int i = 0; i <= visible; ++i) {
        const int index = first + i;
        if (index >= Count()) break;
        const FlowRow& r = (*rows)[index];
        const int y = y0 + index * rowH - static_cast<int>(scroll + 0.5f);
        if (y >= y0 + boxH) break;
        const bool isLit = index == lit && r.kind != FlowRow::Kind::Info;
        const bool fact = r.kind == FlowRow::Kind::Info && !r.value.empty();
        const auto plain = [](const FlowRow& row) { return row.kind == FlowRow::Kind::Info && row.value.empty(); };
        // Lines between rows: on cards, only between facts (the cards part
        // the rest).
        const auto lined = [&](const FlowRow& next) {
            if (cards) return fact && next.kind == FlowRow::Kind::Info && !next.value.empty();
            return !r.heading && !(plain(r) && plain(next)) && !next.heading;
        };
        if (isLit) {
            // (the highlight, drawn above)
        } else if (index + 1 < Count() && !(index + 1 == lit) && lined((*rows)[index + 1])) {
            const int left = x0 + kPad + (r.indent ? 22 : 0);
            const int lineRight = RowsRight() - kPad;
            Menu_DrawRectangle(left, y + rowH - 1, lineRight - left, 1, skin::WithAlpha(skin::kDivider, alpha), 1);
        }

        const int labelH = r.heading ? 20 : (r.kind == FlowRow::Kind::Info && !fact) ? 15 : 18;
        const int labelX = x0 + kPad + (r.indent ? 22 : 0);
        if (r.icon)
            skin::DrawRgb5a3(r.icon, kIconSide, kIconSide, static_cast<float>(labelX), static_cast<float>(y + (rowH - kIconSide) / 2),
                             r.dim ? alpha / 2 : alpha);
        // With a second line (a list made taller with SetRowHeight) the
        // label goes above the row's middle and the detail under it, in the
        // dim ink, as a value under a label is drawn elsewhere.
        const int textLeft = labelX + (r.icon_column ? kIconRoom : 0);
        const int lineH = LineHeight(r);
        if (notesUnder && !r.detail.empty()) {
            // The label and the control on the top line; the explanation
            // under both, and when the change takes effect at its end.
            label[i]->SetPosition(textLeft, y + kCardGap + (lineH - labelH) / 2 - 1);
            label[i]->Draw();
            const int detailY = y + kCardGap + lineH + 1;
            detail[i]->SetPosition(textLeft, detailY);
            detail[i]->Draw();
            if (!r.tag.empty()) {
                tag[i]->SetPosition(right - tag[i]->GetTextWidth(), detailY + 1);
                tag[i]->Draw();
            }
        } else if (r.detail.empty()) {
            label[i]->SetPosition(textLeft, y + (rowH - labelH) / 2 - 2);
            label[i]->Draw();
        } else {
            label[i]->SetPosition(textLeft, y + rowH / 2 - 21);
            label[i]->Draw();
            detail[i]->SetPosition(textLeft, y + rowH / 2 + 3);
            detail[i]->Draw();
        }

        // Controls sit on the top line (the whole row without SetNotesUnder).
        const int lineTop = notesUnder && !r.detail.empty() ? y + kCardGap : y;
        const int cy = lineTop + (lineH - kChipH) / 2;  // controls' top
        const int textY = lineTop + (lineH - 15) / 2 - 2;
        // A value that just changed comes in from the side, fading up.
        if (shownValue[index] != r.value) {
            if (!shownValue[index].empty() || valueIn[index] < 1.0f) valueIn[index] = 0.0f;
            shownValue[index] = r.value;
        }
        valueIn[index] = motion::Follow(valueIn[index], 1.0f, 58.0f);
        if (valueIn[index] > 0.98f) valueIn[index] = 1.0f;
        const auto chipText = [&](int cx, int width = kChipW) {
            const int tw = value[i]->GetTextWidth();
            const int shown = tw > width - 20 ? width - 20 : tw;
            value[i]->SetPosition(cx + (width - shown) / 2 + static_cast<int>(10.0f * (1.0f - valueIn[index])), textY);
            value[i]->SetAlpha(static_cast<int>(255 * valueIn[index]));
            value[i]->Draw();
            value[i]->SetAlpha(255);
        };
        // A value as plain text at the row's end.
        const auto endText = [&]() {
            const int tw = std::min(value[i]->GetTextWidth(), kChipW - 20);
            value[i]->SetPosition(right - tw + static_cast<int>(10.0f * (1.0f - valueIn[index])), textY);
            value[i]->SetAlpha(static_cast<int>(255 * valueIn[index]));
            value[i]->Draw();
            value[i]->SetAlpha(255);
        };
        if (cards && r.kind != FlowRow::Kind::Toggle) {
            if (r.kind == FlowRow::Kind::Option && !r.dim) {
                // The value between its arrows (each a button of its own),
                // which close in on it (kStepperMinMiddle).
                const int middle = stepperMiddle[i] != 0 ? stepperMiddle[i] : kChipW;
                const int backX = right - (kStep + kGap + middle + kGap + kStep);
                const int chipX = backX + kStep + kGap, fwdX = right - kStep;
                {
                    const int by = lineTop + (lineH - kStep) / 2;
                    const bool overBack = index == hover && hoverPart == Part::Back && !dragging;
                    const bool overFwd = index == hover && hoverPart == Part::Forward && !dragging;
                    skin::Draw(overBack ? skin::stepBackOver : skin::stepBack, backX - 4, by - 4, alpha);
                    skin::Draw(overFwd ? skin::stepForwardOver : skin::stepForward, fwdX - 4, by - 4, alpha);
                }
                chipText(chipX, middle);
            } else if (r.button && !r.value.empty()) {
                // A thing to do: its verb on a button of its own, the
                // theme's primary tile, so it does not read as a setting.
                const int tw = std::min(value[i]->GetTextWidth(), kChipW - 20);
                const int bw = tw + 32, bh = kChipH + 2;
                const int bx = right - bw, by = cy - 1;
                const bool over = index == lit && !dragging;
                skin::DrawNine(over ? skin::tile9PrimaryOver : skin::tile9Primary, bx - kCardShadow, by - kCardShadow,
                               bw + 2 * kCardShadow, bh + 2 * kCardShadow, skin::kTileCorner,
                               r.dim ? alpha / 2 : alpha);
                value[i]->SetColor(r.dim ? skin::kInkDim : skin::kAccentInk);
                value[i]->SetPosition(bx + (bw - tw) / 2, textY);
                value[i]->Draw();
            } else if (!r.value.empty() || OpensScreen(r)) {
                endText();
            }
            continue;
        }
        switch (r.kind) {
            case FlowRow::Kind::Option:
                if (r.dim) {
                    skin::Draw(skin::chipOff, right - kChipW - 2, cy - 3, alpha);
                    chipText(right - kChipW);
                } else {
                    const int backX = right - kStepperW, chipX = backX + kStep + kGap, fwdX = right - kStep;
                    const int by = y + (rowH - kStep) / 2;
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
                sw = motion::Follow(sw, r.on ? 1.0f : 0.0f, 47.0f);
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
                } else if (OpensScreen(r)) {
                    endText();
                }
                break;
            case FlowRow::Kind::Info:
                break;
        }
    }
    GX_SetScissor(0, 0, Menu_XfbWidth(), Menu_EfbHeight());

    if (Count() > visible) {
        // Where the list is, along its right edge; A on it (held) drags it.
        const int trackX = TrackX(), trackTop = y0 + kArrow + 4, trackH = boxH - 2 * (kArrow + 4);
        const int total = Count() * rowH;
        int thumb = trackH * boxH / total;
        if (thumb < 16) thumb = 16;
        const int thumbY = trackTop + static_cast<int>((trackH - thumb) * scroll / MaxScroll());
        Menu_DrawRectangle(trackX, trackTop, kTrackW, trackH, skin::WithAlpha(skin::kScrollTrack, alpha), 1);
        Menu_DrawRectangle(trackX, thumbY, kTrackW, thumb,
                           skin::WithAlpha(grabTrack ? skin::kAccent : skin::kScrollThumb, alpha), 1);
        // Each arrow shows while the list can still go its way.
        if (aim > 0) {
            const bool over = hoverArrow < 0 || grabArrow < 0;
            riftwii::wii::DrawInvitingArrow(skin::scrollUp, skin::scrollUpOver, ArrowX() - 4, ArrowY(-1) - 4,
                                          0, -1, over, arrowLift[0], alpha);
        }
        if (aim < MaxScroll()) {
            const bool over = hoverArrow > 0 || grabArrow > 0;
            riftwii::wii::DrawInvitingArrow(skin::scrollDown, skin::scrollDownOver, ArrowX() - 4, ArrowY(1) - 4,
                                          0, 1, over, arrowLift[1], alpha);
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
                    ScrollTo(aim + grabArrow * rowH);
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
                    const float frameMs = motion::FrameMilliseconds();
                    if (frameMs > 0.0f) {
                        const float velocity = (scroll - before) * (1000.0f / 60.0f) / frameMs;
                        fling = motion::Follow(fling, velocity, 24.0f);
                    }
                }
            }
            return;
        }
        // Let go: a press that stayed put acts on its row.
        if (!dragging && !grabTrack && pointing && RowAt(px, py) == grabRow && Actionable(grabRow)) {
            (grabPart == Part::Back ? clickedBack : clicked) = grabRow;
            clickStepped = grabPart == Part::Forward;
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
        if (t->Down()) ScrollTo(aim + rowH);
        else if (t->Up()) ScrollTo(aim - rowH);
        const int arrow = ArrowAt(px, py);
        if (arrow != hoverArrow && arrow != 0) soundOver->Play();
        hoverArrow = arrow;
        if (arrow != 0) {
            hover = -1;
            if (a) {
                // One row per press; held, it repeats.
                ScrollTo(aim + arrow * rowH);
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
        const Part part = PartAt(row, px, py);
        if ((row != hover || part != hoverPart) && Actionable(row)) soundOver->Play();
        hover = row;
        hoverPart = part;
        if (row >= 0) focus = row;
        if (a && px >= x0 && px < RowsRight() && py >= y0 && py < y0 + visible * rowH) {
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
    if (!keysActive) return;
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
        clickStepped = !a;
        soundClick->Play();
    } else if (live && (back || (stepper && t->Left()))) {
        clickedBack = focus;
        soundClick->Play();
    }
}
