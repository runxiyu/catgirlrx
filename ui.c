/* Copyright (C) 2018  Curtis McEnroe <june@causal.agency>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define _XOPEN_SOURCE_EXTENDED

#include <curses.h>
#include <err.h>
#include <locale.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <wchar.h>
#include <wctype.h>

#ifndef A_ITALIC
#define A_ITALIC A_NORMAL
#endif

#include "chat.h"
#undef uiFmt

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define CTRL(c)   ((c) ^ 0100)

static void colorInit(void) {
	start_color();
	use_default_colors();
	if (COLORS >= 16) {
		for (short pair = 0; pair < 0x100; ++pair) {
			if (pair < 0x10) {
				init_pair(1 + pair, pair, -1);
			} else {
				init_pair(1 + pair, pair & 0x0F, (pair & 0xF0) >> 4);
			}
		}
	} else {
		for (short pair = 0; pair < 0100; ++pair) {
			if (pair < 010) {
				init_pair(1 + pair, pair, -1);
			} else {
				init_pair(1 + pair, pair & 007, (pair & 070) >> 3);
			}
		}
	}
}

static attr_t attr8(short pair) {
	if (COLORS >= 16 || pair < 0) return A_NORMAL;
	return (pair & 0x08) ? A_BOLD : A_NORMAL;
}
static short pair8(short pair) {
	if (COLORS >= 16 || pair < 0) return pair;
	return (pair & 0x70) >> 1 | (pair & 0x07);
}

struct View {
	struct Tag tag;
	WINDOW *topic;
	WINDOW *log;
	int scroll;
	int unread;
	bool hot;
	bool mark;
	struct View *prev;
	struct View *next;
};

static struct {
	struct View *head;
	struct View *tail;
	struct View *tags[TagsLen];
} views;

static void viewAppend(struct View *view) {
	if (views.tail) views.tail->next = view;
	view->prev = views.tail;
	view->next = NULL;
	if (!views.head) views.head = view;
	views.tail = view;
	views.tags[view->tag.id] = view;
}

static void viewRemove(struct View *view) {
	if (view->prev) view->prev->next = view->next;
	if (view->next) view->next->prev = view->prev;
	if (views.head == view) views.head = view->next;
	if (views.tail == view) views.tail = view->prev;
	views.tags[view->tag.id] = NULL;
}

static const int LogLines = 256;

static int logHeight(const struct View *view) {
	return LINES - (view->topic ? 2 : 0) - 2;
}
static int lastLogLine(void) {
	return LogLines - 1;
}
static int lastLine(void) {
	return LINES - 1;
}
static int lastCol(void) {
	return COLS - 1;
}

static struct View *viewTag(struct Tag tag) {
	struct View *view = views.tags[tag.id];
	if (view) return view;

	view = calloc(1, sizeof(*view));
	if (!view) err(EX_OSERR, "calloc");

	view->tag = tag;
	view->log = newpad(LogLines, COLS);
	wsetscrreg(view->log, 0, lastLogLine());
	scrollok(view->log, true);
	wmove(view->log, lastLogLine() - logHeight(view) + 2, 0);
	view->scroll = LogLines;
	view->mark = true;

	viewAppend(view);
	return view;
}

static void viewResize(void) {
	for (struct View *view = views.head; view; view = view->next) {
		wresize(view->log, LogLines, COLS);
		wmove(view->log, lastLogLine(), lastCol());
	}
}

static void viewClose(struct View *view) {
	viewRemove(view);
	if (view->topic) delwin(view->topic);
	delwin(view->log);
	free(view);
}

static void viewMark(struct View *view) {
	view->mark = true;
}
static void viewUnmark(struct View *view) {
	view->unread = 0;
	view->hot = false;
	view->mark = false;
}

static struct {
	bool hide;
	struct View *view;
	WINDOW *status;
	WINDOW *input;
} ui;

static void uiShow(void) {
	ui.hide = false;
	termMode(TermFocus, true);
}

void uiHide(void) {
	ui.hide = true;
	termMode(TermFocus, false);
	endwin();
}

void uiDraw(void) {
	if (ui.hide) return;

	if (ui.view->topic) {
		pnoutrefresh(
			ui.view->topic,
			0, 0,
			0, 0,
			1, lastCol()
		);
	}

	pnoutrefresh(
		ui.view->log,
		ui.view->scroll - logHeight(ui.view), 0,
		(ui.view->topic ? 2 : 0), 0,
		lastLine() - 2, lastCol()
	);

	int _, x;
	getyx(ui.status, _, x);
	pnoutrefresh(
		ui.status,
		0, MAX(0, x - lastCol() - 1),
		lastLine() - 1, 0,
		lastLine() - 1, lastCol()
	);

	getyx(ui.input, _, x);
	pnoutrefresh(
		ui.input,
		0, MAX(0, x - lastCol() + 3),
		lastLine(), 0,
		lastLine(), lastCol()
	);

	doupdate();
}

static void uiRedraw(void) {
	clearok(curscr, true);
}

static const short IRCColors[] = {
	[IRCWhite]      = 8 + COLOR_WHITE,
	[IRCBlack]      = 0 + COLOR_BLACK,
	[IRCBlue]       = 0 + COLOR_BLUE,
	[IRCGreen]      = 0 + COLOR_GREEN,
	[IRCRed]        = 8 + COLOR_RED,
	[IRCBrown]      = 0 + COLOR_RED,
	[IRCMagenta]    = 0 + COLOR_MAGENTA,
	[IRCOrange]     = 0 + COLOR_YELLOW,
	[IRCYellow]     = 8 + COLOR_YELLOW,
	[IRCLightGreen] = 8 + COLOR_GREEN,
	[IRCCyan]       = 0 + COLOR_CYAN,
	[IRCLightCyan]  = 8 + COLOR_CYAN,
	[IRCLightBlue]  = 8 + COLOR_BLUE,
	[IRCPink]       = 8 + COLOR_MAGENTA,
	[IRCGray]       = 8 + COLOR_BLACK,
	[IRCLightGray]  = 0 + COLOR_WHITE,
};

static const wchar_t *parseColor(short *pair, const wchar_t *str) {
	short fg = 0;
	size_t fgLen = MIN(wcsspn(str, L"0123456789"), 2);
	if (!fgLen) { *pair = -1; return str; }
	for (size_t i = 0; i < fgLen; ++i) {
		fg = fg * 10 + (str[i] - L'0');
	}
	str = &str[fgLen];

	short bg = 0;
	size_t bgLen = 0;
	if (str[0] == L',') bgLen = MIN(wcsspn(&str[1], L"0123456789"), 2);
	for (size_t i = 0; i < bgLen; ++i) {
		bg = bg * 10 + (str[1 + i] - L'0');
	}
	if (bgLen) str = &str[1 + bgLen];

	if (*pair == -1) *pair = 0;
	*pair = (*pair & 0xF0) | IRCColors[fg & 0x0F];
	if (bgLen) *pair = (*pair & 0x0F) | (IRCColors[bg & 0x0F] << 4);

	return str;
}

static void wordWrap(WINDOW *win, const wchar_t *str) {
	size_t len = wcscspn(str, L" ");
	size_t width = 1;
	for (size_t i = 0; i < len; ++i) {
		if (iswprint(str[i])) width += wcwidth(str[i]);
	}

	int _, x, xMax;
	getyx(win, _, x);
	getmaxyx(win, _, xMax);

	if (width >= (size_t)(xMax - x)) {
		waddch(win, '\n');
	} else {
		waddch(win, ' ');
	}
}

static const wchar_t IRCCodes[] = {
	L' ',
	IRCBold,
	IRCColor,
	IRCReverse,
	IRCReset,
	IRCItalic,
	IRCUnderline,
	L'\0',
};

static void addIRC(WINDOW *win, const wchar_t *str) {
	attr_t attr = A_NORMAL;
	short pair = -1;
	for (;;) {
		size_t cc = wcscspn(str, IRCCodes);
		wattr_set(win, attr | attr8(pair), 1 + pair8(pair), NULL);
		waddnwstr(win, str, cc);
		if (!str[cc]) break;

		str = &str[cc];
		switch (*str++) {
			break; case L' ':          wordWrap(win, str);
			break; case IRCBold:      attr ^= A_BOLD;
			break; case IRCItalic:    attr ^= A_ITALIC;
			break; case IRCUnderline: attr ^= A_UNDERLINE;
			break; case IRCReverse:   attr ^= A_REVERSE;
			break; case IRCColor:     str = parseColor(&pair, str);
			break; case IRCReset:     attr = A_NORMAL; pair = -1;
		}
	}
}

static void uiStatus(void) {
	mvwhline(ui.status, 0, 0, ACS_HLINE, COLS);
	mvwaddch(ui.status, 0, COLS, ACS_RTEE);

	int num = 0;
	int count = 0;
	for (const struct View *view = views.head; view; view = view->next, ++num) {
		if (!view->unread) continue;
		bool status = (view->tag.id == TagStatus.id);

		int unread;
		wchar_t *str;
		int len = aswprintf(
			&str, L",\3%02d%d\3%s%s%n(%d)",
			(view->hot ? IRCYellow : IRCWhite), num,
			&status[":"], (status ? "" : view->tag.name),
			&unread, view->unread
		);
		if (len < 0) err(EX_OSERR, "aswprintf");
		if (view->unread == 1) str[unread] = L'\0';

		addIRC(ui.status, count ? str : &str[1]);
		free(str);
		count++;
	}

	waddch(ui.status, count ? ACS_LTEE : '\b');
	waddch(ui.status, ACS_HLINE);
}

static void uiView(struct View *view) {
	termTitle(view->tag.name);
	if (view->topic) touchwin(view->topic);
	touchwin(view->log);
	viewMark(ui.view);
	viewUnmark(view);
	ui.view = view;
	uiStatus();
}

static void uiClose(struct View *view) {
	if (ui.view == view) {
		if (view->next) {
			uiView(view->next);
		} else if (view->prev) {
			uiView(view->prev);
		} else {
			return;
		}
	}
	viewClose(view);
}

void uiViewTag(struct Tag tag) {
	uiView(viewTag(tag));
}

void uiCloseTag(struct Tag tag) {
	uiClose(viewTag(tag));
}

void uiViewNum(int num) {
	if (num < 0) {
		for (struct View *view = views.tail; view; view = view->prev) {
			if (++num) continue;
			uiView(view);
			break;
		}
	} else {
		for (struct View *view = views.head; view; view = view->next) {
			if (num--) continue;
			uiView(view);
			break;
		}
	}
}

static const int ColsMax = 512;

void uiTopic(struct Tag tag, const char *topic) {
	struct View *view = viewTag(tag);
	if (!view->topic) {
		view->topic = newpad(2, ColsMax);
		mvwhline(view->topic, 1, 0, ACS_HLINE, ColsMax);
	}
	wchar_t *wcs = ambstowcs(topic);
	if (!wcs) err(EX_DATAERR, "ambstowcs");
	wmove(view->topic, 0, 0);
	addIRC(view->topic, wcs);
	wclrtoeol(view->topic);
	free(wcs);
}

void uiLog(struct Tag tag, enum UIHeat heat, const wchar_t *line) {
	struct View *view = viewTag(tag);
	waddch(view->log, '\n');
	if (view->mark && heat > UICold) {
		if (!view->unread++) waddch(view->log, '\n');
		if (heat > UIWarm) {
			view->hot = true;
			beep(); // TODO: Notification.
		}
		uiStatus();
	}
	addIRC(view->log, line);
}

void uiFmt(struct Tag tag, enum UIHeat heat, const wchar_t *format, ...) {
	wchar_t *wcs;
	va_list ap;
	va_start(ap, format);
	vaswprintf(&wcs, format, ap);
	va_end(ap);
	if (!wcs) err(EX_OSERR, "vaswprintf");
	uiLog(tag, heat, wcs);
	free(wcs);
}

void uiInit(void) {
	setlocale(LC_CTYPE, "");
	initscr();
	cbreak();
	noecho();

	colorInit();
	termInit();

	ui.status = newpad(1, ColsMax);
	ui.input = newpad(1, ColsMax);
	keypad(ui.input, true);
	nodelay(ui.input, true);

	ui.view = viewTag(TagStatus);
	uiViewTag(TagStatus);
	uiStatus();
	uiShow();
}

void uiExit(void) {
	uiHide();
	printf(
		"This program is AGPLv3 free software!\n"
		"The source is available at <" SOURCE_URL ">.\n"
	);
}

static void logScrollUp(int lines) {
	int height = logHeight(ui.view);
	if (ui.view->scroll == height) return;
	if (ui.view->scroll == LogLines) viewMark(ui.view);
	ui.view->scroll = MAX(ui.view->scroll - lines, height);
}
static void logScrollDown(int lines) {
	if (ui.view->scroll == LogLines) return;
	ui.view->scroll = MIN(ui.view->scroll + lines, LogLines);
	if (ui.view->scroll == LogLines) viewUnmark(ui.view);
}
static void logPageUp(void) {
	logScrollUp(logHeight(ui.view) / 2);
}
static void logPageDown(void) {
	logScrollDown(logHeight(ui.view) / 2);
}

static bool keyChar(wchar_t ch) {
	if (ch < 0200) {
		enum TermEvent event = termEvent((char)ch);
		switch (event) {
			break; case TermFocusIn:  viewUnmark(ui.view);
			break; case TermFocusOut: viewMark(ui.view);
			break; default: {}
		}
		if (event) {
			uiStatus();
			return false;
		}
	}

	static bool meta;
	if (ch == L'\33') {
		meta = true;
		return false;
	}

	if (meta) {
		bool update = true;
		switch (ch) {
			break; case L'b':  edit(ui.view->tag, EditBackWord, 0);
			break; case L'f':  edit(ui.view->tag, EditForeWord, 0);
			break; case L'\b': edit(ui.view->tag, EditKillBackWord, 0);
			break; case L'd':  edit(ui.view->tag, EditKillForeWord, 0);
			break; default: {
				update = false;
				if (ch < L'0' || ch > L'9') break;
				uiViewNum(ch - L'0');
			}
		}
		meta = false;
		return update;
	}

	if (ch == L'\177') ch = L'\b';
	switch (ch) {
		break; case CTRL(L'L'): uiRedraw(); return false;

		break; case CTRL(L'A'): edit(ui.view->tag, EditHome, 0);
		break; case CTRL(L'B'): edit(ui.view->tag, EditLeft, 0);
		break; case CTRL(L'D'): edit(ui.view->tag, EditDelete, 0);
		break; case CTRL(L'E'): edit(ui.view->tag, EditEnd, 0);
		break; case CTRL(L'F'): edit(ui.view->tag, EditRight, 0);
		break; case CTRL(L'K'): edit(ui.view->tag, EditKillLine, 0);
		break; case CTRL(L'W'): edit(ui.view->tag, EditKillBackWord, 0);

		break; case CTRL(L'C'): edit(ui.view->tag, EditInsert, IRCColor);
		break; case CTRL(L'N'): edit(ui.view->tag, EditInsert, IRCReset);
		break; case CTRL(L'O'): edit(ui.view->tag, EditInsert, IRCBold);
		break; case CTRL(L'R'): edit(ui.view->tag, EditInsert, IRCColor);
		break; case CTRL(L'T'): edit(ui.view->tag, EditInsert, IRCItalic);
		break; case CTRL(L'U'): edit(ui.view->tag, EditInsert, IRCUnderline);
		break; case CTRL(L'V'): edit(ui.view->tag, EditInsert, IRCReverse);

		break; case L'\b': edit(ui.view->tag, EditBackspace, 0);
		break; case L'\t': edit(ui.view->tag, EditComplete, 0);
		break; case L'\n': edit(ui.view->tag, EditEnter, 0);

		break; default: {
			if (!iswprint(ch)) return false;
			edit(ui.view->tag, EditInsert, ch);
		}
	}
	return true;
}

static bool keyCode(wchar_t ch) {
	switch (ch) {
		break; case KEY_RESIZE:    viewResize(); return false;
		break; case KEY_SLEFT:     logScrollUp(1); return false;
		break; case KEY_SRIGHT:    logScrollDown(1); return false;
		break; case KEY_PPAGE:     logPageUp(); return false;
		break; case KEY_NPAGE:     logPageDown(); return false;
		break; case KEY_LEFT:      edit(ui.view->tag, EditLeft, 0);
		break; case KEY_RIGHT:     edit(ui.view->tag, EditRight, 0);
		break; case KEY_HOME:      edit(ui.view->tag, EditHome, 0);
		break; case KEY_END:       edit(ui.view->tag, EditEnd, 0);
		break; case KEY_DC:        edit(ui.view->tag, EditDelete, 0);
		break; case KEY_BACKSPACE: edit(ui.view->tag, EditBackspace, 0);
		break; case KEY_ENTER:     edit(ui.view->tag, EditEnter, 0);
	}
	return true;
}

void uiRead(void) {
	uiShow();

	bool update = false;
	int ret;
	wint_t ch;
	while (ERR != (ret = wget_wch(ui.input, &ch))) {
		if (ret == KEY_CODE_YES) {
			update |= keyCode(ch);
		} else {
			update |= keyChar(ch);
		}
	}
	if (!update) return;

	int y, x;
	wmove(ui.input, 0, 0);
	addIRC(ui.input, editHead());
	getyx(ui.input, y, x);
	addIRC(ui.input, editTail());
	wclrtoeol(ui.input);
	wmove(ui.input, y, x);
}
