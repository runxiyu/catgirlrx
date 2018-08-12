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

#define CTRL(c)   ((c) & 037)

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

static const int LOG_LINES   = 256;
static const int TOPIC_COLS  = 512;
static const int INPUT_COLS  = 512;

static struct View {
	struct Tag tag;
	WINDOW *topic;
	WINDOW *log;
	int scroll;
	bool mark;
	struct View *prev;
	struct View *next;
} *viewHead, *viewTail;

static void viewAppend(struct View *view) {
	if (viewTail) viewTail->next = view;
	view->prev = viewTail;
	view->next = NULL;
	viewTail = view;
	if (!viewHead) viewHead = view;
}

static int logHeight(const struct View *view) {
	return LINES - (view->topic ? 2 : 0) - 2;
}
static int lastLogLine(void) {
	return LOG_LINES - 1;
}
static int lastLine(void) {
	return LINES - 1;
}
static int lastCol(void) {
	return COLS - 1;
}

static struct {
	bool hide;
	WINDOW *input;
	struct View *view;
	struct View *tags[TAGS_LEN];
} ui;

static struct View *viewTag(struct Tag tag) {
	struct View *view = ui.tags[tag.id];
	if (view) return view;

	view = calloc(1, sizeof(*view));
	if (!view) err(EX_OSERR, "calloc");

	view->tag = tag;
	view->log = newpad(LOG_LINES, COLS);
	wsetscrreg(view->log, 0, lastLogLine());
	scrollok(view->log, true);
	wmove(view->log, lastLogLine() - logHeight(view) + 2, 0);
	view->scroll = LOG_LINES;

	viewAppend(view);
	ui.tags[tag.id] = view;
	return view;
}

void uiInit(void) {
	setlocale(LC_CTYPE, "");
	initscr();
	cbreak();
	noecho();

	colorInit();
	termMode(TERM_FOCUS, true);

	ui.input = newpad(2, INPUT_COLS);
	mvwhline(ui.input, 0, 0, ACS_HLINE, INPUT_COLS);
	wmove(ui.input, 1, 0);
	keypad(ui.input, true);
	nodelay(ui.input, true);

	ui.view = viewTag(TAG_STATUS);
}

void uiHide(void) {
	ui.hide = true;
	endwin();
}

void uiExit(void) {
	uiHide();
	termMode(TERM_FOCUS, false);
	printf(
		"This program is AGPLv3 free software!\n"
		"The source is available at <" SOURCE_URL ">.\n"
	);
}

static void uiResize(void) {
	for (struct View *view = viewHead; view; view = view->next) {
		wresize(view->log, LOG_LINES, COLS);
		wmove(view->log, lastLogLine(), lastCol());
	}
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
	getyx(ui.input, _, x);
	pnoutrefresh(
		ui.input,
		0, MAX(0, x - lastCol() + 3),
		lastLine() - 1, 0,
		lastLine(), lastCol()
	);
	doupdate();
}

static void uiRedraw(void) {
	clearok(curscr, true);
}

static void uiView(struct View *view) {
	if (view->topic) touchwin(view->topic);
	touchwin(view->log);
	view->mark = false;
	ui.view->mark = true;
	ui.view = view;
}

void uiViewTag(struct Tag tag) {
	uiView(viewTag(tag));
}

void uiViewNum(int num) {
	if (num < 0) {
		for (struct View *view = viewTail; view; view = view->prev) {
			if (++num) continue;
			uiView(view);
			break;
		}
	} else {
		for (struct View *view = viewHead; view; view = view->next) {
			if (num--) continue;
			uiView(view);
			break;
		}
	}
}

static const short IRC_COLORS[] = {
	[IRC_WHITE]       = 8 + COLOR_WHITE,
	[IRC_BLACK]       = 0 + COLOR_BLACK,
	[IRC_BLUE]        = 0 + COLOR_BLUE,
	[IRC_GREEN]       = 0 + COLOR_GREEN,
	[IRC_RED]         = 8 + COLOR_RED,
	[IRC_BROWN]       = 0 + COLOR_RED,
	[IRC_MAGENTA]     = 0 + COLOR_MAGENTA,
	[IRC_ORANGE]      = 0 + COLOR_YELLOW,
	[IRC_YELLOW]      = 8 + COLOR_YELLOW,
	[IRC_LIGHT_GREEN] = 8 + COLOR_GREEN,
	[IRC_CYAN]        = 0 + COLOR_CYAN,
	[IRC_LIGHT_CYAN]  = 8 + COLOR_CYAN,
	[IRC_LIGHT_BLUE]  = 8 + COLOR_BLUE,
	[IRC_PINK]        = 8 + COLOR_MAGENTA,
	[IRC_GRAY]        = 8 + COLOR_BLACK,
	[IRC_LIGHT_GRAY]  = 0 + COLOR_WHITE,
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
	*pair = (*pair & 0xF0) | IRC_COLORS[fg & 0x0F];
	if (bgLen) *pair = (*pair & 0x0F) | (IRC_COLORS[bg & 0x0F] << 4);

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

static const wchar_t IRC_CODES[] = {
	L' ',
	IRC_BOLD,
	IRC_COLOR,
	IRC_REVERSE,
	IRC_RESET,
	IRC_ITALIC,
	IRC_UNDERLINE,
	L'\0',
};

static void addIRC(WINDOW *win, const wchar_t *str) {
	attr_t attr = A_NORMAL;
	short pair = -1;
	for (;;) {
		size_t cc = wcscspn(str, IRC_CODES);
		wattr_set(win, attr | attr8(pair), 1 + pair8(pair), NULL);
		waddnwstr(win, str, cc);
		if (!str[cc]) break;

		str = &str[cc];
		switch (*str++) {
			break; case L' ':          wordWrap(win, str);
			break; case IRC_BOLD:      attr ^= A_BOLD;
			break; case IRC_ITALIC:    attr ^= A_ITALIC;
			break; case IRC_UNDERLINE: attr ^= A_UNDERLINE;
			break; case IRC_REVERSE:   attr ^= A_REVERSE;
			break; case IRC_COLOR:     str = parseColor(&pair, str);
			break; case IRC_RESET:     attr = A_NORMAL; pair = -1;
		}
	}
}

void uiTopic(struct Tag tag, const char *topic) {
	struct View *view = viewTag(tag);
	if (!view->topic) {
		view->topic = newpad(2, TOPIC_COLS);
		mvwhline(view->topic, 1, 0, ACS_HLINE, TOPIC_COLS);
	}
	wchar_t *wcs = ambstowcs(topic);
	if (!wcs) err(EX_DATAERR, "ambstowcs");
	wmove(view->topic, 0, 0);
	addIRC(view->topic, wcs);
	wclrtoeol(view->topic);
	free(wcs);
}

void uiLog(struct Tag tag, const wchar_t *line) {
	struct View *view = viewTag(tag);
	waddch(view->log, '\n');
	if (view->mark) {
		waddch(view->log, '\n');
		view->mark = false;
	}
	addIRC(view->log, line);
}

void uiFmt(struct Tag tag, const wchar_t *format, ...) {
	wchar_t *wcs;
	va_list ap;
	va_start(ap, format);
	vaswprintf(&wcs, format, ap);
	va_end(ap);
	if (!wcs) err(EX_OSERR, "vaswprintf");
	uiLog(tag, wcs);
	free(wcs);
}

static void logScrollUp(int lines) {
	int height = logHeight(ui.view);
	if (ui.view->scroll == height) return;
	if (ui.view->scroll == LOG_LINES) ui.view->mark = true;
	ui.view->scroll = MAX(ui.view->scroll - lines, height);
}
static void logScrollDown(int lines) {
	if (ui.view->scroll == LOG_LINES) return;
	ui.view->scroll = MIN(ui.view->scroll + lines, LOG_LINES);
	if (ui.view->scroll == LOG_LINES) ui.view->mark = false;
}
static void logPageUp(void) {
	logScrollUp(logHeight(ui.view) / 2);
}
static void logPageDown(void) {
	logScrollDown(logHeight(ui.view) / 2);
}

static bool keyChar(wchar_t ch) {
	if (iswascii(ch)) {
		enum TermEvent event = termEvent((char)ch);
		switch (event) {
			break; case TERM_FOCUS_IN:  ui.view->mark = false;
			break; case TERM_FOCUS_OUT: ui.view->mark = true;
			break; default: {}
		}
		if (event) return false;
	}

	static bool meta;
	if (ch == L'\33') {
		meta = true;
		return false;
	}

	if (meta) {
		bool update = true;
		switch (ch) {
			break; case L'b':  edit(ui.view->tag, EDIT_BACK_WORD, 0);
			break; case L'f':  edit(ui.view->tag, EDIT_FORE_WORD, 0);
			break; case L'\b': edit(ui.view->tag, EDIT_KILL_BACK_WORD, 0);
			break; case L'd':  edit(ui.view->tag, EDIT_KILL_FORE_WORD, 0);
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

		break; case CTRL(L'A'): edit(ui.view->tag, EDIT_HOME, 0);
		break; case CTRL(L'B'): edit(ui.view->tag, EDIT_LEFT, 0);
		break; case CTRL(L'D'): edit(ui.view->tag, EDIT_DELETE, 0);
		break; case CTRL(L'E'): edit(ui.view->tag, EDIT_END, 0);
		break; case CTRL(L'F'): edit(ui.view->tag, EDIT_RIGHT, 0);
		break; case CTRL(L'K'): edit(ui.view->tag, EDIT_KILL_LINE, 0);
		break; case CTRL(L'W'): edit(ui.view->tag, EDIT_KILL_BACK_WORD, 0);

		break; case CTRL(L'C'): edit(ui.view->tag, EDIT_INSERT, IRC_COLOR);
		break; case CTRL(L'N'): edit(ui.view->tag, EDIT_INSERT, IRC_RESET);
		break; case CTRL(L'O'): edit(ui.view->tag, EDIT_INSERT, IRC_BOLD);
		break; case CTRL(L'R'): edit(ui.view->tag, EDIT_INSERT, IRC_COLOR);
		break; case CTRL(L'T'): edit(ui.view->tag, EDIT_INSERT, IRC_ITALIC);
		break; case CTRL(L'U'): edit(ui.view->tag, EDIT_INSERT, IRC_UNDERLINE);
		break; case CTRL(L'V'): edit(ui.view->tag, EDIT_INSERT, IRC_REVERSE);

		break; case L'\b': edit(ui.view->tag, EDIT_BACKSPACE, 0);
		break; case L'\t': edit(ui.view->tag, EDIT_COMPLETE, 0);
		break; case L'\n': edit(ui.view->tag, EDIT_ENTER, 0);

		break; default: {
			if (!iswprint(ch)) return false;
			edit(ui.view->tag, EDIT_INSERT, ch);
		}
	}
	return true;
}

static bool keyCode(wchar_t ch) {
	switch (ch) {
		break; case KEY_RESIZE:    uiResize(); return false;
		break; case KEY_SLEFT:     logScrollUp(1); return false;
		break; case KEY_SRIGHT:    logScrollDown(1); return false;
		break; case KEY_PPAGE:     logPageUp(); return false;
		break; case KEY_NPAGE:     logPageDown(); return false;
		break; case KEY_LEFT:      edit(ui.view->tag, EDIT_LEFT, 0);
		break; case KEY_RIGHT:     edit(ui.view->tag, EDIT_RIGHT, 0);
		break; case KEY_HOME:      edit(ui.view->tag, EDIT_HOME, 0);
		break; case KEY_END:       edit(ui.view->tag, EDIT_END, 0);
		break; case KEY_DC:        edit(ui.view->tag, EDIT_DELETE, 0);
		break; case KEY_BACKSPACE: edit(ui.view->tag, EDIT_BACKSPACE, 0);
		break; case KEY_ENTER:     edit(ui.view->tag, EDIT_ENTER, 0);
	}
	return true;
}

void uiRead(void) {
	ui.hide = false;

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
	wmove(ui.input, 1, 0);
	addIRC(ui.input, editHead());
	getyx(ui.input, y, x);
	addIRC(ui.input, editTail());
	wclrtoeol(ui.input);
	wmove(ui.input, y, x);
}
