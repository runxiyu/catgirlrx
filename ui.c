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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <wchar.h>
#include <wctype.h>

#include "chat.h"

#ifndef A_ITALIC
#define A_ITALIC A_NORMAL
#endif

#define CTRL(c) ((c) & 037)

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

static const int TOPIC_COLS = 512;
static const int INPUT_COLS = 512;
static const int LOG_LINES = 100;

static struct {
	WINDOW *topic;
	WINDOW *log;
	WINDOW *input;
	int scroll;
	size_t cursor;
} ui;

static int lastLine(void) {
	return LINES - 1;
}
static int lastCol(void) {
	return COLS - 1;
}
static int logHeight(void) {
	return LINES - 4;
}

void uiInit(void) {
	setlocale(LC_CTYPE, "");
	initscr();
	cbreak();
	noecho();

	start_color();
	use_default_colors();
	for (short pair = 0; pair < 077; ++pair) {
		if (pair < 010) {
			init_pair(1 + pair, pair, -1);
		} else {
			init_pair(1 + pair, pair & 007, (pair & 070) >> 3);
		}
	}

	ui.topic = newpad(2, TOPIC_COLS);
	mvwhline(ui.topic, 1, 0, ACS_HLINE, TOPIC_COLS);

	ui.log = newpad(LOG_LINES, COLS);
	wsetscrreg(ui.log, 0, LOG_LINES - 1);
	scrollok(ui.log, true);
	wmove(ui.log, LOG_LINES - logHeight() - 1, 0);
	ui.scroll = LOG_LINES;

	ui.input = newpad(2, INPUT_COLS);
	mvwhline(ui.input, 0, 0, ACS_HLINE, INPUT_COLS);
	wmove(ui.input, 1, ui.cursor);

	keypad(ui.input, true);
	nodelay(ui.input, true);
}

static void uiResize(void) {
	wresize(ui.log, LOG_LINES, COLS);
	wmove(ui.log, LOG_LINES - 1, COLS - 1);
}

void uiHide(void) {
	endwin();
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
	pnoutrefresh(
		ui.input,
		0, MAX(0, ui.cursor - lastCol() + 1),
		lastLine() - 1, 0,
		lastLine(), lastCol()
	);
	doupdate();
}

static const struct AttrColor {
	attr_t attr;
	short pair;
} MIRC_COLORS[16] = {
	{ A_BOLD,   COLOR_WHITE },   // white
	{ A_NORMAL, COLOR_BLACK },   // black
	{ A_NORMAL, COLOR_BLUE },    // blue
	{ A_NORMAL, COLOR_GREEN },   // green
	{ A_BOLD,   COLOR_RED },     // red
	{ A_NORMAL, COLOR_RED },     // "brown"
	{ A_NORMAL, COLOR_MAGENTA }, // magenta
	{ A_NORMAL, COLOR_YELLOW },  // "orange"
	{ A_BOLD,   COLOR_YELLOW },  // yellow
	{ A_BOLD,   COLOR_GREEN },   // light green
	{ A_NORMAL, COLOR_CYAN },    // cyan
	{ A_BOLD,   COLOR_CYAN },    // light cyan
	{ A_BOLD,   COLOR_BLUE },    // light blue
	{ A_BOLD,   COLOR_MAGENTA }, // "pink"
	{ A_BOLD,   COLOR_BLACK },   // grey
	{ A_NORMAL, COLOR_WHITE },   // light grey
};

static const char *parseColor(struct AttrColor *color, const char *str) {
	short fg = 0;
	size_t fgLen = MIN(strspn(str, "0123456789"), 2);
	if (!fgLen) {
		color->attr = A_NORMAL;
		color->pair = -1;
		return str;
	}
	for (size_t i = 0; i < fgLen; ++i) {
		fg *= 10;
		fg += str[i] - '0';
	}
	str = &str[fgLen];

	short bg = 0;
	size_t bgLen = 0;
	if (str[0] == ',') {
		bgLen = MIN(strspn(&str[1], "0123456789"), 2);
	}
	for (size_t i = 0; i < bgLen; ++i) {
		bg *= 10;
		bg += str[1 + i] - '0';
	}
	if (bgLen) str = &str[1 + bgLen];

	if (color->pair == -1) color->pair = 0;
	color->attr = MIRC_COLORS[fg].attr;
	color->pair = (color->pair & 070) | MIRC_COLORS[fg].pair;
	if (bgLen) {
		color->pair = (color->pair & 007) | (MIRC_COLORS[bg].pair << 3);
	}

	return str;
}

static void uiAdd(WINDOW *win, const char *str) {
	attr_t attr = A_NORMAL;
	struct AttrColor color = { A_NORMAL, -1 };
	for (;;) {
		size_t cc = strcspn(str, "\2\3\35\37");
		wattr_set(win, attr | color.attr, 1 + color.pair, NULL);
		waddnstr(win, str, cc);
		if (!str[cc]) break;

		str = &str[cc];
		switch (*str++) {
			break; case '\2': attr ^= A_BOLD;
			break; case '\3': str = parseColor(&color, str);
			break; case '\35': attr ^= A_ITALIC;
			break; case '\37': attr ^= A_UNDERLINE;
		}
	}
}

void uiTopic(const char *topic) {
	wmove(ui.topic, 0, 0);
	wclrtoeol(ui.topic);
	uiAdd(ui.topic, topic);
}

void uiLog(const char *line) {
	waddch(ui.log, '\n');
	uiAdd(ui.log, line);
}

void uiFmt(const char *format, ...) {
	char *buf;
	va_list ap;
	va_start(ap, format);
	vasprintf(&buf, format, ap);
	va_end(ap);
	if (!buf) err(EX_OSERR, "vasprintf");
	uiLog(buf);
	free(buf);
}

static void scrollUp(void) {
	if (ui.scroll == logHeight()) return;
	ui.scroll = MAX(ui.scroll - logHeight() / 2, logHeight());
}
static void scrollDown(void) {
	if (ui.scroll == LOG_LINES) return;
	ui.scroll = MIN(ui.scroll + logHeight() / 2, LOG_LINES);
}

static struct {
	wchar_t buf[512];
	size_t len;
} line;
static const size_t BUF_LEN = sizeof(line.buf) / sizeof(line.buf[0]);

static void moveLeft(void) {
	if (ui.cursor) ui.cursor--;
}
static void moveRight(void) {
	if (ui.cursor < line.len) ui.cursor++;
}
static void moveHome(void) {
	ui.cursor = 0;
}
static void moveEnd(void) {
	ui.cursor = line.len;
}

static void insert(wchar_t ch) {
	if (!iswprint(ch)) return;
	if (line.len == BUF_LEN - 1) return;
	if (ui.cursor == line.len) {
		line.buf[line.len] = ch;
	} else {
		wmemmove(
			&line.buf[ui.cursor + 1],
			&line.buf[ui.cursor],
			line.len - ui.cursor
		);
		line.buf[ui.cursor] = ch;
	}
	line.len++;
	ui.cursor++;
}

static void backspace(void) {
	if (!ui.cursor) return;
	if (ui.cursor != line.len) {
		wmemmove(
			&line.buf[ui.cursor - 1],
			&line.buf[ui.cursor],
			line.len - ui.cursor
		);
	}
	line.len--;
	ui.cursor--;
}

static void delete(void) {
	if (ui.cursor == line.len) return;
	moveRight();
	backspace();
}

static void kill(void) {
	line.len = ui.cursor;
}

static void enter(void) {
	if (!line.len) return;
	line.buf[line.len] = '\0';
	input(line.buf);
	line.len = 0;
	ui.cursor = 0;
}

static void keyChar(wint_t ch) {
	switch (ch) {
		break; case CTRL('B'): moveLeft();
		break; case CTRL('F'): moveRight();
		break; case CTRL('A'): moveHome();
		break; case CTRL('E'): moveEnd();
		break; case CTRL('D'): delete();
		break; case CTRL('K'): kill();
		break; case '\b':      backspace();
		break; case '\177':    backspace();
		break; case '\n':      enter();
		break; default:        insert(ch);
	}
}

static void keyCode(wint_t ch) {
	switch (ch) {
		break; case KEY_RESIZE:    uiResize();
		break; case KEY_PPAGE:     scrollUp();
		break; case KEY_NPAGE:     scrollDown();
		break; case KEY_LEFT:      moveLeft();
		break; case KEY_RIGHT:     moveRight();
		break; case KEY_HOME:      moveHome();
		break; case KEY_END:       moveEnd();
		break; case KEY_BACKSPACE: backspace();
		break; case KEY_DC:        delete();
		break; case KEY_ENTER:     enter();
	}
}

void uiRead(void) {
	int ret;
	wint_t ch;
	while (ERR != (ret = wget_wch(ui.input, &ch))) {
		if (ret == KEY_CODE_YES) {
			keyCode(ch);
		} else {
			keyChar(ch);
		}
	}
	wmove(ui.input, 1, 0);
	waddnwstr(ui.input, line.buf, line.len);
	wclrtoeol(ui.input);
	wmove(ui.input, 1, ui.cursor);
}
