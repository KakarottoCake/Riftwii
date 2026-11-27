/****************************************************************************
 * libwiigui
 *
 * Tantric 2009
 *
 * gui_text.cpp
 *
 * GUI class definitions
 ***************************************************************************/
/* Changed for RiftWii (September and October 2026), under
 * GPL-3.0-or-later: wrapped text can stop after a number of lines, ending in "..."; UTF-8 and layout fixes;
 * Japanese wraps at a good place rather than mid-word (November 2026).
 * Every change is in RiftWii's git history; NOTICE.md lists the origin. */

#include "gui.h"
#include "../gettext.h"

static GXColor presetColor = (GXColor){255, 255, 255, 255};
static int currentSize = 0;
static int presetSize = 0;
static int presetMaxWidth = 0;
static ALIGN_H presetAlignmentHor = ALIGN_H::LEFT;
static ALIGN_V presetAlignmentVert = ALIGN_V::TOP;
static u16 presetStyle = 0;

#define TEXT_SCROLL_DELAY			8
#define	TEXT_SCROLL_INITIAL_DELAY	6

/**
 * Constructor for the GuiText class.
 */
GuiText::GuiText(const char * t, int s, GXColor c)
{
	origText = nullptr;
	text = nullptr;
	size = s;
	color = c;
	alpha = c.a;
	style = FTGX_JUSTIFY_CENTER | FTGX_ALIGN_MIDDLE;
	maxWidth = 0;
	wrap = false;
	maxLines = 0;
	textDynNum = 0;
	textScroll = SCROLL::NONE;
	textScrollPos = 0;
	textScrollInitialDelay = TEXT_SCROLL_INITIAL_DELAY;
	textScrollDelay = TEXT_SCROLL_DELAY;

	alignmentHor = ALIGN_H::CENTRE;
	alignmentVert = ALIGN_V::MIDDLE;

	if(t)
	{
		origText = strdup(t);
		text = charToWideChar(gettext(t));
	}

	for(int i=0; i < 20; i++)
		textDyn[i] = nullptr;
}

/**
 * Constructor for the GuiText class, uses presets
 */
GuiText::GuiText(const char * t)
{
	origText = nullptr;
	text = nullptr;
	size = presetSize;
	color = presetColor;
	alpha = presetColor.a;
	style = presetStyle;
	maxWidth = presetMaxWidth;
	wrap = false;
	maxLines = 0;
	textDynNum = 0;
	textScroll = SCROLL::NONE;
	textScrollPos = 0;
	textScrollInitialDelay = TEXT_SCROLL_INITIAL_DELAY;
	textScrollDelay = TEXT_SCROLL_DELAY;

	alignmentHor = presetAlignmentHor;
	alignmentVert = presetAlignmentVert;

	if(t)
	{
		origText = strdup(t);
		text = charToWideChar(gettext(t));
	}

	for(int i=0; i < 20; i++)
		textDyn[i] = nullptr;
}

/**
 * Destructor for the GuiText class.
 */
GuiText::~GuiText()
{
	if(origText)
		free(origText);
	if(text)
		delete[] text;

	if(textDynNum > 0)
	{
		for(int i=0; i < textDynNum; i++)
			if(textDyn[i])
				delete[] textDyn[i];
	}
}

void GuiText::SetText(const char * t)
{
	if(origText)
		free(origText);
	if(text)
		delete[] text;

	if(textDynNum > 0)
	{
		for(int i=0; i < textDynNum; i++)
			if(textDyn[i])
				delete[] textDyn[i];
	}

	origText = nullptr;
	text = nullptr;
	textDynNum = 0;
	textScrollPos = 0;
	textScrollInitialDelay = TEXT_SCROLL_INITIAL_DELAY;

	if(t)
	{
		origText = strdup(t);
		text = charToWideChar(gettext(t));
	}
}

void GuiText::SetWText(wchar_t * t)
{
	if(origText)
		free(origText);
	if(text)
		delete[] text;

	if(textDynNum > 0)
	{
		for(int i=0; i < textDynNum; i++)
			if(textDyn[i])
				delete[] textDyn[i];
	}

	origText = nullptr;
	text = nullptr;
	textDynNum = 0;
	textScrollPos = 0;
	textScrollInitialDelay = TEXT_SCROLL_INITIAL_DELAY;

	if(t)
		text = wcsdup(t);
}

int GuiText::GetLength()
{
	if(!text)
		return 0;

	return wcslen(text);
}

void GuiText::SetPresets(int sz, GXColor c, int w, u16 s, ALIGN_H h, ALIGN_V v)
{
	presetSize = sz;
	presetColor = c;
	presetStyle = s;
	presetMaxWidth = w;
	presetAlignmentHor = h;
	presetAlignmentVert = v;
}

void GuiText::SetFontSize(int s)
{
	size = s;
}

void GuiText::SetMaxWidth(int width)
{
	maxWidth = width;

	for(int i=0; i < textDynNum; i++)
	{
		if(textDyn[i])
		{
			delete[] textDyn[i];
			textDyn[i] = nullptr;
		}
	}

	textDynNum = 0;
}

int GuiText::GetTextWidth()
{
	if(!text)
		return 0;

	if(currentSize != size)
	{
		ChangeFontSize(size);

		if(!fontSystem[size])
			fontSystem[size] = new FreeTypeGX(size);

		currentSize = size;
	}
	return fontSystem[size]->getWidth(text);
}

void GuiText::SetWrap(bool w, int width, int lines)
{
	wrap = w;
	maxWidth = width;
	maxLines = lines;

	for(int i=0; i < textDynNum; i++)
	{
		if(textDyn[i])
		{
			delete[] textDyn[i];
			textDyn[i] = nullptr;
		}
	}

	textDynNum = 0;
	// RiftWii: new lines, so a vertical scroll starts again from the top.
	textScrollPos = 0;
	textScrollInitialDelay = TEXT_SCROLL_INITIAL_DELAY;
}

void GuiText::SetScroll(SCROLL s)
{
	if(textScroll == s)
		return;

	for(int i=0; i < textDynNum; i++)
	{
		if(textDyn[i])
		{
			delete[] textDyn[i];
			textDyn[i] = nullptr;
		}
	}

	textDynNum = 0;

	textScroll = s;
	textScrollPos = 0;
	textScrollInitialDelay = TEXT_SCROLL_INITIAL_DELAY;
	textScrollDelay = TEXT_SCROLL_DELAY;
}

void GuiText::SetColor(GXColor c)
{
	color = c;
	alpha = c.a;
}

void GuiText::SetStyle(u16 s)
{
	style = s;
}

void GuiText::SetAlignment(ALIGN_H hor, ALIGN_V vert)
{
	style = 0;

	switch(hor)
	{
		case ALIGN_H::LEFT:
			style |= FTGX_JUSTIFY_LEFT;
			break;
		case ALIGN_H::RIGHT:
			style |= FTGX_JUSTIFY_RIGHT;
			break;
		default:
			style |= FTGX_JUSTIFY_CENTER;
			break;
	}
	switch(vert)
	{
		case ALIGN_V::TOP:
			style |= FTGX_ALIGN_TOP;
			break;
		case ALIGN_V::BOTTOM:
			style |= FTGX_ALIGN_BOTTOM;
			break;
		default:
			style |= FTGX_ALIGN_MIDDLE;
			break;
	}

	alignmentHor = hor;
	alignmentVert = vert;
}

void GuiText::ResetText()
{
	if(!origText)
		return;
	if(text)
		delete[] text;

	text = charToWideChar(gettext(origText));

	for(int i=0; i < textDynNum; i++)
	{
		if(textDyn[i])
		{
			delete[] textDyn[i];
			textDyn[i] = nullptr;
		}
	}

	textDynNum = 0;
	currentSize = 0;
}

/**
 * Draw the text on screen
 */
// RiftWii: a line made at the whole text's length, cut to its own (a long
// note's 20 lines took 20 times the text, about 96 KB for 1200 characters).
static wchar_t *FitLine(wchar_t *line)
{
	wchar_t *fit = new wchar_t[wcslen(line) + 1];
	wcscpy(fit, line);
	delete[] line;
	return fit;
}

// RiftWii: `line` ending in "...", shortened until it fits `width`.
static wchar_t *EndWithDots(wchar_t *line, FreeTypeGX *font, int width)
{
	size_t len = wcslen(line);
	wchar_t *cut = new wchar_t[len + 4];
	wcscpy(cut, line);
	wcscat(cut, L"...");
	while(len > 0 && font->getWidth(cut) > width)
	{
		cut[--len] = 0;
		wcscat(cut, L"...");
	}
	delete[] line;
	return cut;
}

// RiftWii: where Japanese (and Chinese) text may be wrapped. It has no
// spaces, and breaking after any character split words in two on narrow
// cards: a line now ends at a good place when one fits (after a run of
// hiragana, as after a particle; after closing punctuation; between
// katakana and kanji), inside a word only when none does, and never
// before what may not start a line (small kana, the long-vowel mark,
// closing punctuation) or after an opening bracket.
namespace {

enum class CjkClass { Other, Hiragana, Katakana, Ideograph, Opening, Closing, NoStart };

CjkClass ClassOf(wchar_t c)
{
	switch (c) {
	case 0x3041: case 0x3043: case 0x3045: case 0x3047: case 0x3049: case 0x3063: case 0x3083: case 0x3085:
	case 0x3087: case 0x308E: case 0x30A1: case 0x30A3: case 0x30A5: case 0x30A7: case 0x30A9: case 0x30C3:
	case 0x30E3: case 0x30E5: case 0x30E7: case 0x30EE: case 0x30F5: case 0x30F6: case 0x30FC: case 0x309D:
	case 0x309E: case 0x30FD: case 0x30FE: case 0x3005:
		return CjkClass::NoStart;
	case 0x3001: case 0x3002: case 0x30FB: case 0x3009: case 0x300B: case 0x300D: case 0x300F: case 0x3011:
	case 0x3015: case 0xFF09: case 0xFF01: case 0xFF1F: case 0xFF0C: case 0xFF0E: case 0xFF1A: case 0xFF1B:
	case 0xFF3D: case 0xFF5D:
		return CjkClass::Closing;
	case 0x3008: case 0x300A: case 0x300C: case 0x300E: case 0x3010: case 0x3014: case 0xFF08: case 0xFF3B:
	case 0xFF5B:
		return CjkClass::Opening;
	default:
		break;
	}
	if (c >= 0x3040 && c <= 0x309F) return CjkClass::Hiragana;
	if ((c >= 0x30A0 && c <= 0x30FF) || (c >= 0xFF66 && c <= 0xFF9F)) return CjkClass::Katakana;
	if ((c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF) || (c >= 0xF900 && c <= 0xFAFF))
		return CjkClass::Ideograph;
	return CjkClass::Other;
}

// A break after `a` when `b` follows: 2 a good one, 1 inside a word (only
// if nothing better fits), 0 never.
int BreakAfter(wchar_t a, wchar_t b)
{
	const CjkClass ca = ClassOf(a), cb = ClassOf(b);
	if (cb == CjkClass::NoStart || cb == CjkClass::Closing || ca == CjkClass::Opening) return 0;
	if (ca == CjkClass::Closing) return 2;
	if (ca == CjkClass::Hiragana && cb != CjkClass::Hiragana) return 2;
	if ((ca == CjkClass::Katakana || ca == CjkClass::NoStart) && cb == CjkClass::Ideograph) return 2;
	if (ca == CjkClass::Ideograph && cb == CjkClass::Katakana) return 2;
	return 1;
}

}  // namespace

// RiftWii: the wrapping, from Draw, so GetLineCount can wrap before the
// first draw (a popup spaces its parts for the lines its text takes).
void GuiText::WrapLines(u32 textlen)
{
	int n = 0;
	u32 ch = 0;
	int linenum = 0;
	int lastSpace = -1;
	int lastSpaceIndex = -1;
	// RiftWii: a break inside a CJK word, kept for when no better one
	// (lastSpace) fits on the line.
	int lastAny = -1;
	int lastAnyIndex = -1;
	int building = -1; // RiftWii: the line still at the text's length

	while(ch < textlen && linenum < 20)
	{
		if(n == 0)
		{
			// RiftWii: the line before is finished; it keeps only its own length.
			if(building >= 0)
				textDyn[building] = FitLine(textDyn[building]);
			textDyn[linenum] = new wchar_t[textlen + 1];
			textDyn[linenum][0] = 0;
			building = linenum;
		}

		// RiftWii: a newline ends the line (after the usual break
		// at the last space when the line is too long already).
		if(text[ch] == '\n')
		{
			if(lastSpace >= 0 && fontSystem[currentSize]->getWidth(textDyn[linenum]) > maxWidth)
			{
				textDyn[linenum][lastSpaceIndex] = 0;
				ch = lastSpace;
			}
			++linenum;
			n = 0;
			lastSpace = -1;
			lastSpaceIndex = -1;
			lastAny = -1;
			lastAnyIndex = -1;
			++ch;
			continue;
		}

		textDyn[linenum][n] = text[ch];
		textDyn[linenum][n+1] = 0;

		// RiftWii: a line may also break after a CJK character
		// (Japanese has no spaces); that character is kept.
		const bool cjk = text[ch] >= 0x2E80;
		if(text[ch] == ' ' || cjk || ch == textlen-1)
		{
			if(fontSystem[currentSize]->getWidth(textDyn[linenum]) > maxWidth)
			{
				// RiftWii: the last good break, else one inside a word.
				if(lastSpace < 0 && lastAny >= 0)
				{
					lastSpace = lastAny;
					lastSpaceIndex = lastAnyIndex;
				}
				if(lastSpace >= 0)
				{
					textDyn[linenum][lastSpaceIndex] = 0; // discard space (or keep the CJK character), and everything after
					ch = lastSpace; // go backwards to the last break
					lastSpace = -1; // we have used this space
					lastSpaceIndex = -1;
				}
				lastAny = -1;
				lastAnyIndex = -1;
				++linenum;
				n = -1;
			}
			else if(ch == textlen-1)
			{
				++linenum;
			}
		}
		if(text[ch] == ' ' && n >= 0)
		{
			lastSpace = ch;
			lastSpaceIndex = n;
		}
		// RiftWii: between other letters and Japanese ("3D/パッケージ") is
		// a good place too, unless what follows may not start a line.
		else if(!cjk && n >= 0 && ch + 1 < textlen && text[ch + 1] >= 0x2E80 &&
		        ClassOf(text[ch + 1]) != CjkClass::NoStart && ClassOf(text[ch + 1]) != CjkClass::Closing)
		{
			lastSpace = ch;
			lastSpaceIndex = n + 1;
		}
		else if(cjk && n >= 0 && ch + 1 < textlen)
		{
			const int quality = BreakAfter(text[ch], text[ch + 1]);
			if(quality == 2)
			{
				lastSpace = ch;
				lastSpaceIndex = n + 1;
			}
			else if(quality == 1)
			{
				lastAny = ch;
				lastAnyIndex = n + 1;
			}
		}
		++ch;
		++n;
	}
	if(building >= 0)
		textDyn[building] = FitLine(textDyn[building]);
	textDynNum = linenum;
	// RiftWii: past maxLines the last line shown ends in "..." (not when
	// the lines scroll: they are all shown in turn).
	// Every line stays allocated (and is freed) as before.
	if(maxLines > 0 && textDynNum > maxLines && textScroll != SCROLL::VERTICAL)
		textDyn[maxLines-1] = EndWithDots(textDyn[maxLines-1], fontSystem[currentSize], maxWidth);
	// RiftWii: a scrolling text longer than the 20 lines kept says so in its
	// last line, rather than losing its end without a sign.
	else if(textScroll == SCROLL::VERTICAL && ch < textlen && textDynNum > 0)
		textDyn[textDynNum-1] = EndWithDots(textDyn[textDynNum-1], fontSystem[currentSize], maxWidth);
}

// RiftWii: the lines the text takes at its own size, wrapped and capped at
// maxLines as Draw shows it; 1 for text that doesn't wrap, 0 for none.
int GuiText::GetLineCount()
{
	if(!text)
		return 0;
	if(!wrap || maxWidth == 0)
		return 1;
	if(currentSize != size)
	{
		ChangeFontSize(size);
		if(!fontSystem[size])
			fontSystem[size] = new FreeTypeGX(size);
		currentSize = size;
	}
	if(textDynNum == 0)
		WrapLines(wcslen(text));
	return (maxLines > 0 && textDynNum > maxLines) ? maxLines : textDynNum;
}

void GuiText::Draw()
{
	if(!text)
		return;

	if(!this->IsVisible())
		return;

	GXColor c = color;
	c.a = this->GetAlpha();

	int newSize = size*this->GetScale();

	if(newSize > MAX_FONT_SIZE)
		newSize = MAX_FONT_SIZE;

	if(newSize != currentSize)
	{
		ChangeFontSize(newSize);
		if(!fontSystem[newSize])
			fontSystem[newSize] = new FreeTypeGX(newSize);
		currentSize = newSize;
	}

	if(maxWidth == 0)
	{
		fontSystem[currentSize]->drawText(this->GetLeft(), this->GetTop(), text, c, style);
		this->UpdateEffects();
		return;
	}

	u32 textlen = wcslen(text);

	if(wrap)
	{
		if(textDynNum == 0)
		{
			WrapLines(textlen);
		}

		const int shown = (maxLines > 0 && textDynNum > maxLines) ? maxLines : textDynNum;
		// RiftWii: SCROLL::VERTICAL shows the lines `shown` at a time, from
		// textScrollPos. A tick is 30 frames (0.5 s at 60 Hz, 0.6 s at 50):
		// it holds at the top for TEXT_SCROLL_INITIAL_DELAY ticks (about 3 s),
		// rolls up a line every 3 ticks, holds about 3 s at the end, then
		// starts again from the top. SetText and SetWrap start it over.
		int first = 0;
		if(textScroll == SCROLL::VERTICAL && shown < textDynNum)
		{
			const int last = textDynNum - shown;
			if(textScrollPos > last)
				textScrollPos = last;
			if(FrameTimer % 30 == 0)
			{
				if(textScrollInitialDelay > 0)
				{
					--textScrollInitialDelay;
				}
				else if(textScrollPos < last)
				{
					++textScrollPos;
					textScrollInitialDelay = textScrollPos == last ? TEXT_SCROLL_INITIAL_DELAY : 2;
				}
				else
				{
					textScrollPos = 0;
					textScrollInitialDelay = TEXT_SCROLL_INITIAL_DELAY;
				}
			}
			first = textScrollPos;
		}
		int lineheight = newSize + 6;
		int voffset = 0;

		if(alignmentVert == ALIGN_V::MIDDLE)
			voffset = (lineheight >> 1) * (1-shown);

		int left = this->GetLeft();
		int top  = this->GetTop() + voffset;

		for(int i=0; i < shown; ++i)
			fontSystem[currentSize]->drawText(left, top+i*lineheight, textDyn[first+i], c, style);
	}
	else
	{
		if(textDynNum == 0)
		{
			textDynNum = 1;
			int len = wcslen(text);
			if(textScroll != SCROLL::HORIZONTAL && fontSystem[currentSize]->getWidth(text) > maxWidth)
			{
				// RiftWii: a line too long for its width ends in "..."
				// (it was cut off mid-word with no sign of it).
				textDyn[0] = new wchar_t[len + 4];
				wcscpy(textDyn[0], text);
				wcscat(textDyn[0], L"...");
				while(len > 0 && fontSystem[currentSize]->getWidth(textDyn[0]) > maxWidth)
				{
					textDyn[0][--len] = 0;
					while(len > 0 && textDyn[0][len-1] == L' ')
						textDyn[0][--len] = 0;
					wcscat(textDyn[0], L"...");
				}
			}
			else
			{
				textDyn[0] = wcsdup(text);
				while(fontSystem[currentSize]->getWidth(textDyn[0]) > maxWidth)
					textDyn[0][--len] = 0;
			}
		}

		if(textScroll == SCROLL::HORIZONTAL)
		{
			if(fontSystem[currentSize]->getWidth(text) > maxWidth && (FrameTimer % textScrollDelay == 0))
			{
				if(textScrollInitialDelay)
				{
					--textScrollInitialDelay;
				}
				else
				{
					++textScrollPos;
					if((u32)textScrollPos > textlen-1)
					{
						textScrollPos = 0;
						textScrollInitialDelay = TEXT_SCROLL_INITIAL_DELAY;
					}

					wcscpy(textDyn[0], &text[textScrollPos]);
					u32 dynlen = wcslen(textDyn[0]);

					if(dynlen+2 < textlen)
					{
						textDyn[0][dynlen] = ' ';
						textDyn[0][dynlen+1] = ' ';
						textDyn[0][dynlen+2] = 0;
						dynlen += 2;
					}

					if(fontSystem[currentSize]->getWidth(textDyn[0]) > maxWidth)
					{
						while(fontSystem[currentSize]->getWidth(textDyn[0]) > maxWidth)
							textDyn[0][--dynlen] = 0;
					}
					else
					{
						int i = 0;

						while(fontSystem[currentSize]->getWidth(textDyn[0]) < maxWidth && dynlen+1 < textlen)
						{
							textDyn[0][dynlen] = text[i++];
							textDyn[0][++dynlen] = 0;
						}

						if(fontSystem[currentSize]->getWidth(textDyn[0]) > maxWidth)
							textDyn[0][dynlen-2] = 0;
						else
							textDyn[0][dynlen-1] = 0;
					}
				}
			}
		}
		fontSystem[currentSize]->drawText(this->GetLeft(), this->GetTop(), textDyn[0], c, style);
	}
	this->UpdateEffects();
}
