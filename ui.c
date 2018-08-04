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

#define MAX(a, b) ((a) > (b) ? (a) : (b))

static const int TOPIC_COLS = 512;
static const int CHAT_LINES = 100;
static const int INPUT_COLS = 512;

static struct {
	WINDOW *topic;
	WINDOW *chat;
	WINDOW *input;
	size_t cursor;
} ui;

void uiInit(void) {
	setlocale(LC_CTYPE, "");
	initscr();
	cbreak();
	noecho();
	keypad(stdscr, true);

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

	ui.chat = newpad(CHAT_LINES, COLS);
	wsetscrreg(ui.chat, 0, CHAT_LINES - 1);
	scrollok(ui.chat, true);
	wmove(ui.chat, CHAT_LINES - (LINES - 4) - 1, 0);

	ui.input = newpad(2, INPUT_COLS);
	mvwhline(ui.input, 0, 0, ACS_HLINE, INPUT_COLS);
	wmove(ui.input, 1, ui.cursor);
	nodelay(ui.input, true);
}

static void uiResize(void) {
	wresize(ui.chat, CHAT_LINES, COLS);
	wmove(ui.chat, CHAT_LINES - 1, COLS - 1);
}

void uiHide(void) {
	endwin();
}

void uiDraw(void) {
	int lastCol = COLS - 1;
	int lastLine = LINES - 1;

	pnoutrefresh(ui.topic, 0, 0, 0, 0, 1, lastCol);
	pnoutrefresh(
		ui.chat,
		CHAT_LINES - (lastLine - 4), 0,
		2, 0, lastLine, lastCol
	);
	pnoutrefresh(
		ui.input,
		0, MAX(0, ui.cursor - lastCol),
		lastLine - 1, 0,
		lastLine, lastCol
	);
	doupdate();
}

static const struct {
	attr_t attr;
	short color;
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

static void uiAdd(WINDOW *win, const char *str) {
	attr_t attr = A_NORMAL;
	short colorPair = -1;
	attr_t colorAttr = A_NORMAL;
	for (;;) {
		size_t cc = strcspn(str, "\2\3\35\37");
		wattr_set(win, attr | colorAttr, 1 + colorPair, NULL);
		waddnstr(win, str, cc);
		if (!str[cc]) break;

		str = &str[cc];
		switch (*str++) {
			break; case '\2':  attr ^= A_BOLD;
			break; case '\35': attr ^= A_ITALIC;
			break; case '\37': attr ^= A_UNDERLINE;
			break; case '\3': {
				short fg = 0;
				short bg = 0;

				size_t fgLen = strspn(str, "0123456789");
				if (!fgLen) {
					colorPair = -1;
					colorAttr = A_NORMAL;
					break;
				}

				if (fgLen > 2) fgLen = 2;
				for (size_t i = 0; i < fgLen; ++i) {
					fg *= 10;
					fg += str[i] - '0';
				}
				str = &str[fgLen];

				size_t bgLen = (str[0] == ',') ? strspn(&str[1], "0123456789") : 0;
				if (bgLen > 2) bgLen = 2;
				for (size_t i = 0; i < bgLen; ++i) {
					bg *= 10;
					bg += str[1 + i] - '0';
				}
				if (bgLen) str = &str[1 + bgLen];

				if (colorPair == -1) colorPair = 0;
				colorPair = (colorPair & 070) | MIRC_COLORS[fg].color;
				colorAttr = MIRC_COLORS[fg].attr;
				if (bgLen) {
					colorPair = (colorPair & 007) | (MIRC_COLORS[bg].color << 3);
				}
			}
		}
	}
}

void uiTopic(const char *topic) {
	wmove(ui.topic, 0, 0);
	wclrtoeol(ui.topic);
	uiAdd(ui.topic, topic);
}

void uiChat(const char *line) {
	waddch(ui.chat, '\n');
	uiAdd(ui.chat, line);
}

void uiFmt(const char *format, ...) {
	char *buf;
	va_list ap;
	va_start(ap, format);
	vasprintf(&buf, format, ap);
	va_end(ap);
	if (!buf) err(EX_OSERR, "vasprintf");
	uiChat(buf);
	free(buf);
}

void uiRead(void) {
	static wchar_t buf[512];
	static size_t len;

	wint_t ch;
	while (wget_wch(ui.input, &ch) != ERR) {
		switch (ch) {
			break; case KEY_RESIZE: uiResize();
			break; case '\b': case '\177': {
				if (len) len--;
			}
			break; case '\n': {
				if (!len) break;
				buf[len] = '\0';
				input(buf);
				len = 0;
			}
			break; default: {
				// TODO: Check overflow
				if (iswprint(ch)) buf[len++] = ch;
			}
		}
	}
	wmove(ui.input, 1, 0);
	waddnwstr(ui.input, buf, len);
	wclrtoeol(ui.input);
	ui.cursor = len;
}
