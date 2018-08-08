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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <wchar.h>
#include <wctype.h>

#include "chat.h"
#undef uiFmt

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define CTRL(c)   ((c) & 037)
#define UNCTRL(c) ((c) + '@')

#ifndef A_ITALIC
#define A_ITALIC A_NORMAL
#endif

static void focusEnable(void) {
	printf("\33[?1004h");
	fflush(stdout);
}
static void focusDisable(void) {
	printf("\33[?1004l");
	fflush(stdout);
}

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

static const int TOPIC_COLS = 512;
static const int INPUT_COLS = 512;
static const int LOG_LINES = 100;

static int lastLine(void) {
	return LINES - 1;
}
static int lastCol(void) {
	return COLS - 1;
}
static int logHeight(void) {
	return LINES - 4;
}

static struct {
	WINDOW *topic;
	WINDOW *log;
	WINDOW *input;
	int scroll;
	bool mark;
} ui;

void uiInit(void) {
	setlocale(LC_CTYPE, "");
	initscr();
	cbreak();
	noecho();

	focusEnable();
	colorInit();

	ui.topic = newpad(2, TOPIC_COLS);
	mvwhline(ui.topic, 1, 0, ACS_HLINE, TOPIC_COLS);

	ui.log = newpad(LOG_LINES, COLS + 1);
	wsetscrreg(ui.log, 0, LOG_LINES - 1);
	scrollok(ui.log, true);
	wmove(ui.log, LOG_LINES - logHeight() - 1, 0);
	ui.scroll = LOG_LINES;

	ui.input = newpad(2, INPUT_COLS);
	mvwhline(ui.input, 0, 0, ACS_HLINE, INPUT_COLS);
	wmove(ui.input, 1, 0);

	keypad(ui.input, true);
	nodelay(ui.input, true);
}

static void uiResize(void) {
	wresize(ui.log, LOG_LINES, COLS);
	wmove(ui.log, LOG_LINES - 1, COLS - 1);
}

void uiHide(void) {
	focusDisable();
	endwin();
	printf(
		"This program is AGPLv3 free software!\n"
		"The source is available at <" SOURCE_URL ">.\n"
	);
}

void uiDraw(void) {
	pnoutrefresh(
		ui.topic,
		0, 0,
		0, 0,
		1, lastCol()
	);
	pnoutrefresh(
		ui.log,
		ui.scroll - logHeight(), 0,
		2, 0,
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

void uiBeep(void) {
	beep(); // always be beeping
}

static const short IRC_COLORS[16] = {
	8 + COLOR_WHITE,   // white
	0 + COLOR_BLACK,   // black
	0 + COLOR_BLUE,    // blue
	0 + COLOR_GREEN,   // green
	8 + COLOR_RED,     // red
	0 + COLOR_RED,     // brown
	0 + COLOR_MAGENTA, // magenta
	0 + COLOR_YELLOW,  // orange
	8 + COLOR_YELLOW,  // yellow
	8 + COLOR_GREEN,   // light green
	0 + COLOR_CYAN,    // cyan
	8 + COLOR_CYAN,    // light cyan
	8 + COLOR_BLUE,    // light blue
	8 + COLOR_MAGENTA, // pink
	8 + COLOR_BLACK,   // gray
	0 + COLOR_WHITE,   // light gray
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

void uiTopic(const wchar_t *topic) {
	wmove(ui.topic, 0, 0);
	addIRC(ui.topic, topic);
	wclrtoeol(ui.topic);
}

void uiTopicStr(const char *topic) {
	wchar_t *wcs = ambstowcs(topic);
	if (!wcs) err(EX_DATAERR, "ambstowcs");
	uiTopic(wcs);
	free(wcs);
}

void uiLog(const wchar_t *line) {
	waddch(ui.log, '\n');

	if (ui.mark) {
		ui.mark = false;
		wattr_set(ui.log, attr8(IRC_COLORS[14]), 1 + pair8(IRC_COLORS[14]), NULL);
		whline(ui.log, ACS_HLINE, COLS);
		int y, _;
		getyx(ui.log, y, _);
		wmove(ui.log, y, COLS);
		waddch(ui.log, '\n');
	}

	addIRC(ui.log, line);
}

void uiFmt(const wchar_t *format, ...) {
	wchar_t *buf;
	va_list ap;
	va_start(ap, format);
	vaswprintf(&buf, format, ap);
	va_end(ap);
	if (!buf) err(EX_OSERR, "vaswprintf");
	uiLog(buf);
	free(buf);
}

static void logUp(void) {
	if (ui.scroll == logHeight()) return;
	if (ui.scroll == LOG_LINES) ui.mark = true;
	ui.scroll = MAX(ui.scroll - logHeight() / 2, logHeight());
}
static void logDown(void) {
	if (ui.scroll == LOG_LINES) return;
	ui.scroll = MIN(ui.scroll + logHeight() / 2, LOG_LINES);
	if (ui.scroll == LOG_LINES) ui.mark = false;
}

static bool keyChar(wint_t ch) {
	static bool esc, csi;
	bool update = false;
	switch (ch) {
		break; case CTRL('L'): uiRedraw();
		break; case CTRL('['): esc = true; return false;
		break; case L'\b':     update = edit(esc, false, L'\b');
		break; case L'\177':   update = edit(esc, false, L'\b');
		break; case L'\t':     update = edit(esc, false, L'\t');
		break; case L'\n':     update = edit(esc, false, L'\n');
		break; default: {
			if (esc && ch == L'[') {
				csi = true;
				return false;
			} else if (csi) {
				if (ch == L'O') ui.mark = true;
				if (ch == L'I') ui.mark = false;
			} else if (iswcntrl(ch)) {
				update = edit(esc, true, UNCTRL(ch));
			} else {
				update = edit(esc, false, ch);
			}
		}
	}
	esc = false;
	csi = false;
	return update;
}

static bool keyCode(wint_t ch) {
	switch (ch) {
		break; case KEY_RESIZE:    uiResize();
		break; case KEY_PPAGE:     logUp();
		break; case KEY_NPAGE:     logDown();
		break; case KEY_LEFT:      return edit(false, true, 'B');
		break; case KEY_RIGHT:     return edit(false, true, 'F');
		break; case KEY_HOME:      return edit(false, true, 'A');
		break; case KEY_END:       return edit(false, true, 'E');
		break; case KEY_DC:        return edit(false, true, 'D');
		break; case KEY_BACKSPACE: return edit(false, false, '\b');
		break; case KEY_ENTER:     return edit(false, false, '\n');
	}
	return false;
}

void uiRead(void) {
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

	wmove(ui.input, 1, 0);
	addIRC(ui.input, editHead());

	int y, x;
	getyx(ui.input, y, x);

	addIRC(ui.input, editTail());

	wclrtoeol(ui.input);
	wmove(ui.input, y, x);
}
