// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "libwiigui/gui.h"

// A list for the Riftwii screens: tall rows whose title wraps over two
// lines instead of scrolling sideways, a small grey subtitle under it, and
// a tag column on the right (game ID and format, On/Off, the current
// choice). Driven like the template's option browser: point and press A
// (a GameCube pad's stick is a pointer too), or step with the D-pad; Left
// and Right jump a page.
struct RichRow {
	std::string title;
	std::string subtitle;  // optional grey line under the title
	std::string tag;       // right column; may wrap to two lines
	bool dim = false;      // greyed (e.g. a disabled or broken entry)
};

class GuiRichList : public GuiElement {
public:
	// `rowHeight` fits two title lines from 56 up; `tagWidth` is the right
	// column's width.
	GuiRichList(int w, int h, int rowHeight, int tagWidth);
	~GuiRichList();
	void SetRows(const std::vector<RichRow>* rows);  // rows must outlive the list
	void TriggerUpdate();
	// The row A was pressed on since the last call, or -1.
	int GetClickedRow();
	// The highlighted row (pointer hover or D-pad focus), or -1.
	int GetSelectedRow() const;
	// Highlights `index` and scrolls it into view.
	void SelectRow(int index);
	void SetFocus(int f) override;
	void ResetState() override;
	void Draw() override;
	void Update(GuiTrigger * t) override;

private:
	static constexpr int kMaxVisible = 8;
	void Refresh();
	void ScrollTo(int first);
	int RowCount() const { return rows ? static_cast<int>(rows->size()) : 0; }

	const std::vector<RichRow> * rows = nullptr;
	int visible;
	int rowHeight;
	int tagWidth;
	int titleWidth;
	int offset = 0;        // first row shown
	int selectedSlot = 0;  // highlighted slot on screen
	bool changed = true;
	int clicked = -1;

	GuiButton * rowBtn[kMaxVisible];
	GuiText * titleTxt[kMaxVisible];
	GuiText * subTxt[kMaxVisible];
	GuiText * tagTxt[kMaxVisible];

	GuiImageData * arrowUp;
	GuiImageData * arrowUpOver;
	GuiImageData * arrowDown;
	GuiImageData * arrowDownOver;
	GuiImage * arrowUpImg;
	GuiImage * arrowUpOverImg;
	GuiImage * arrowDownImg;
	GuiImage * arrowDownOverImg;
	GuiButton * upBtn;
	GuiButton * downBtn;

	GuiTrigger * trigA;
	GuiSound * soundOver;
	GuiSound * soundClick;
};
