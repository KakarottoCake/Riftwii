// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui_richlist.hpp"

namespace {
const GXColor kRowColor = {56, 58, 70, 255};
const GXColor kRowAltColor = {50, 52, 63, 255};
const GXColor kRowSelected = {74, 104, 170, 255};
const GXColor kTitleColor = {255, 255, 255, 255};
const GXColor kDimColor = {150, 150, 160, 255};
const GXColor kSubColor = {185, 190, 205, 255};
const GXColor kTagColor = {210, 215, 230, 255};
constexpr int kScrollColumn = 36;  // arrows on the right
constexpr int kPad = 12;

// With any pointer live (a Wii Remote aimed at the screen, or a GameCube
// pad's stick pointer), the pointer alone decides the highlighted row.
bool AnyPointer()
{
	for (int i = 0; i < 4; i++)
		if (userInput[i].wpad && userInput[i].wpad->ir.valid) return true;
	return false;
}
}

GuiRichList::GuiRichList(int w, int h, int rh, int tw)
{
	width = w;
	height = h;
	rowHeight = rh;
	tagWidth = tw;
	visible = h / rh;
	if (visible > kMaxVisible) visible = kMaxVisible;
	if (visible < 1) visible = 1;
	selectable = true;
	focus = 0;

	trigA = new GuiTrigger;
	trigA->SetSimpleTrigger(-1, WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A, PAD_BUTTON_A, WIIDRC_BUTTON_A);
	soundOver = new GuiSound(button_over_pcm, button_over_pcm_size, SOUND::PCM);
	soundClick = new GuiSound(button_click_pcm, button_click_pcm_size, SOUND::PCM);

	const int rowWidth = w - kScrollColumn;
	titleWidth = rowWidth - tagWidth - 3 * kPad;
	for (int i = 0; i < kMaxVisible; i++)
	{
		titleTxt[i] = new GuiText(nullptr, 20, kTitleColor);
		titleTxt[i]->SetAlignment(ALIGN_H::LEFT, ALIGN_V::MIDDLE);
		titleTxt[i]->SetPosition(kPad, 0);
		titleTxt[i]->SetWrap(true, titleWidth);

		subTxt[i] = new GuiText(nullptr, 15, kSubColor);
		subTxt[i]->SetAlignment(ALIGN_H::LEFT, ALIGN_V::BOTTOM);
		subTxt[i]->SetPosition(kPad, -5);
		subTxt[i]->SetMaxWidth(titleWidth);

		tagTxt[i] = new GuiText(nullptr, 16, kTagColor);
		tagTxt[i]->SetAlignment(ALIGN_H::LEFT, ALIGN_V::MIDDLE);
		tagTxt[i]->SetPosition(rowWidth - tagWidth - kPad, 0);
		tagTxt[i]->SetWrap(true, tagWidth);

		rowBtn[i] = new GuiButton(rowWidth, rowHeight - 4);
		rowBtn[i]->SetParent(this);
		rowBtn[i]->SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
		rowBtn[i]->SetPosition(0, i * rowHeight + 2);
		rowBtn[i]->SetLabel(titleTxt[i], 0);
		rowBtn[i]->SetLabel(subTxt[i], 1);
		rowBtn[i]->SetLabel(tagTxt[i], 2);
		rowBtn[i]->SetTrigger(trigA);
		rowBtn[i]->SetSoundOver(soundOver);
		rowBtn[i]->SetSoundClick(soundClick);
	}

	arrowUp = new GuiImageData(scrollbar_arrowup_png);
	arrowUpOver = new GuiImageData(scrollbar_arrowup_over_png);
	arrowDown = new GuiImageData(scrollbar_arrowdown_png);
	arrowDownOver = new GuiImageData(scrollbar_arrowdown_over_png);
	arrowUpImg = new GuiImage(arrowUp);
	arrowUpOverImg = new GuiImage(arrowUpOver);
	arrowDownImg = new GuiImage(arrowDown);
	arrowDownOverImg = new GuiImage(arrowDownOver);

	upBtn = new GuiButton(arrowUpImg->GetWidth(), arrowUpImg->GetHeight());
	upBtn->SetParent(this);
	upBtn->SetImage(arrowUpImg);
	upBtn->SetImageOver(arrowUpOverImg);
	upBtn->SetAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
	upBtn->SetSelectable(false);
	upBtn->SetTrigger(trigA);
	upBtn->SetSoundClick(soundClick);

	downBtn = new GuiButton(arrowDownImg->GetWidth(), arrowDownImg->GetHeight());
	downBtn->SetParent(this);
	downBtn->SetImage(arrowDownImg);
	downBtn->SetImageOver(arrowDownOverImg);
	downBtn->SetAlignment(ALIGN_H::RIGHT, ALIGN_V::BOTTOM);
	downBtn->SetSelectable(false);
	downBtn->SetTrigger(trigA);
	downBtn->SetSoundClick(soundClick);
}

GuiRichList::~GuiRichList()
{
	for (int i = 0; i < kMaxVisible; i++)
	{
		delete rowBtn[i];
		delete titleTxt[i];
		delete subTxt[i];
		delete tagTxt[i];
	}
	delete upBtn;
	delete downBtn;
	delete arrowUpImg;
	delete arrowUpOverImg;
	delete arrowDownImg;
	delete arrowDownOverImg;
	delete arrowUp;
	delete arrowUpOver;
	delete arrowDown;
	delete arrowDownOver;
	delete trigA;
	delete soundOver;
	delete soundClick;
}

void GuiRichList::SetRows(const std::vector<RichRow> * r)
{
	rows = r;
	if (offset > RowCount() - visible) offset = RowCount() > visible ? RowCount() - visible : 0;
	if (offset + selectedSlot >= RowCount()) selectedSlot = RowCount() > 0 ? RowCount() - 1 - offset : 0;
	changed = true;
}

void GuiRichList::TriggerUpdate()
{
	SetRows(rows);
}

int GuiRichList::GetClickedRow()
{
	const int c = clicked;
	clicked = -1;
	return c;
}

int GuiRichList::GetSelectedRow() const
{
	const int index = offset + selectedSlot;
	return index < RowCount() ? index : -1;
}

void GuiRichList::ScrollTo(int first)
{
	const int maxFirst = RowCount() > visible ? RowCount() - visible : 0;
	if (first > maxFirst) first = maxFirst;
	if (first < 0) first = 0;
	if (first != offset)
	{
		offset = first;
		changed = true;
	}
}

void GuiRichList::SelectRow(int index)
{
	if (index < 0 || index >= RowCount()) return;
	if (index < offset) ScrollTo(index);
	else if (index >= offset + visible) ScrollTo(index - visible + 1);
	selectedSlot = index - offset;
	for (int i = 0; i < visible; i++) rowBtn[i]->ResetState();
	if (focus) rowBtn[selectedSlot]->SetState(STATE::SELECTED);
}

void GuiRichList::SetFocus(int f)
{
	focus = f;
	for (int i = 0; i < kMaxVisible; i++) rowBtn[i]->ResetState();
	if (f) rowBtn[selectedSlot]->SetState(STATE::SELECTED);
}

void GuiRichList::ResetState()
{
	if (state != STATE::DISABLED)
	{
		state = STATE::DEFAULT;
		stateChan = -1;
	}
	for (int i = 0; i < kMaxVisible; i++) rowBtn[i]->ResetState();
}

void GuiRichList::Refresh()
{
	changed = false;
	for (int i = 0; i < visible; i++)
	{
		const int index = offset + i;
		if (index < RowCount())
		{
			const RichRow & row = (*rows)[index];
			titleTxt[i]->SetText(row.title.c_str());
			titleTxt[i]->SetColor(row.dim ? kDimColor : kTitleColor);
			// Two lines per row: a title that needs both gets both (never
			// cut off) and the subtitle gives way; otherwise the title
			// sits on the upper line with the subtitle under it.
			const bool twoLineTitle = titleTxt[i]->GetTextWidth() > titleWidth;
			const bool withSub = !row.subtitle.empty() && !twoLineTitle;
			titleTxt[i]->SetWrap(true, titleWidth);
			titleTxt[i]->SetPosition(kPad, withSub ? -9 : 0);
			subTxt[i]->SetText(withSub ? row.subtitle.c_str() : "");
			tagTxt[i]->SetText(row.tag.c_str());
			if (rowBtn[i]->GetState() == STATE::DISABLED)
			{
				rowBtn[i]->SetVisible(true);
				rowBtn[i]->SetState(STATE::DEFAULT);
			}
		}
		else
		{
			rowBtn[i]->SetVisible(false);
			rowBtn[i]->SetState(STATE::DISABLED);
		}
	}
	for (int i = visible; i < kMaxVisible; i++)
	{
		rowBtn[i]->SetVisible(false);
		rowBtn[i]->SetState(STATE::DISABLED);
	}
}

void GuiRichList::Draw()
{
	if (!this->IsVisible()) return;
	const int rowWidth = width - kScrollColumn;
	for (int i = 0; i < visible && offset + i < RowCount(); i++)
	{
		const bool on = rowBtn[i]->GetState() == STATE::SELECTED || rowBtn[i]->GetState() == STATE::CLICKED;
		const GXColor c = on ? kRowSelected : ((offset + i) % 2 ? kRowAltColor : kRowColor);
		Menu_DrawRectangle(this->GetLeft(), this->GetTop() + i * rowHeight + 2, rowWidth, rowHeight - 4, c, 1);
		rowBtn[i]->Draw();
	}
	if (RowCount() > visible)
	{
		// Scroll position: a thin bar between the arrows.
		const int trackTop = this->GetTop() + arrowUpImg->GetHeight() + 4;
		const int trackHeight = height - arrowUpImg->GetHeight() - arrowDownImg->GetHeight() - 8;
		const int thumb = trackHeight * visible / RowCount() < 16 ? 16 : trackHeight * visible / RowCount();
		const int maxFirst = RowCount() - visible;
		const int thumbTop = trackTop + (trackHeight - thumb) * offset / maxFirst;
		const int x = this->GetLeft() + width - kScrollColumn / 2 - 3;
		Menu_DrawRectangle(x, trackTop, 6, trackHeight, (GXColor){70, 72, 86, 255}, 1);
		Menu_DrawRectangle(x, thumbTop, 6, thumb, (GXColor){170, 175, 195, 255}, 1);
		upBtn->Draw();
		downBtn->Draw();
	}
	this->UpdateEffects();
}

void GuiRichList::Update(GuiTrigger * t)
{
	if (state == STATE::DISABLED || !t) return;
	if (changed) Refresh();

	if (RowCount() > visible)
	{
		upBtn->Update(t);
		downBtn->Update(t);
		if (upBtn->GetState() == STATE::CLICKED)
		{
			upBtn->ResetState();
			ScrollTo(offset - visible);
		}
		if (downBtn->GetState() == STATE::CLICKED)
		{
			downBtn->ResetState();
			ScrollTo(offset + visible);
		}
		if (changed) Refresh();
	}

	const bool pointers = AnyPointer();
	for (int i = 0; i < visible; i++)
	{
		if (offset + i >= RowCount()) break;
		if (t->wpad->ir.valid)
		{
			// This channel points: hover highlights, A clicks what is under it.
			const bool inside = rowBtn[i]->IsInside(t->wpad->ir.x, t->wpad->ir.y);
			if (!inside && rowBtn[i]->GetState() == STATE::SELECTED &&
			    (rowBtn[i]->GetStateChan() == t->chan || rowBtn[i]->GetStateChan() == -1))
				rowBtn[i]->ResetState();
			if (inside) rowBtn[i]->Update(t);
		}
		else
		{
			// No pointer anywhere: the D-pad focus is the highlight.
			if (!pointers)
			{
				if (i != selectedSlot && rowBtn[i]->GetState() == STATE::SELECTED)
					rowBtn[i]->ResetState();
				else if (focus && i == selectedSlot && rowBtn[i]->GetState() == STATE::DEFAULT)
					rowBtn[i]->SetState(STATE::SELECTED, -1);
			}
			rowBtn[i]->Update(t);
		}

		if (rowBtn[i]->GetState() == STATE::SELECTED) selectedSlot = i;
		if (rowBtn[i]->GetState() == STATE::CLICKED)
		{
			selectedSlot = i;
			clicked = offset + i;
			rowBtn[i]->SetState(STATE::SELECTED, t->chan);
		}
	}

	if (!focus || RowCount() == 0) return;
	const int current = offset + selectedSlot;
	int target = current;
	if (t->Down()) target = current + 1;
	else if (t->Up()) target = current - 1;
	else if (t->Right()) target = current + visible;
	else if (t->Left()) target = current - visible;
	if (target != current)
	{
		if (target >= RowCount()) target = RowCount() - 1;
		if (target < 0) target = 0;
		SelectRow(target);
		if (changed) Refresh();
	}
}
