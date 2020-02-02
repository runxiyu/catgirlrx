/* Copyright (C) 2020  C. McEnroe <june@causal.agency>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <assert.h>
#include <ctype.h>
#include <curses.h>
#include <err.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>

#include "chat.h"

#ifndef A_ITALIC
#define A_ITALIC A_UNDERLINE
#endif

#define BOTTOM (LINES - 1)
#define RIGHT (COLS - 1)
#define WINDOW_LINES (LINES - 2)

static short colorPairs;

static void colorInit(void) {
	start_color();
	use_default_colors();
	for (short pair = 0; pair < 16; ++pair) {
		init_pair(1 + pair, pair % COLORS, -1);
	}
	colorPairs = 17;
}

static attr_t colorAttr(short fg) {
	return (fg >= COLORS ? A_BOLD : A_NORMAL);
}

static short colorPair(short fg, short bg) {
	if (bg == -1) return 1 + fg;
	for (short pair = 17; pair < colorPairs; ++pair) {
		short f, b;
		pair_content(pair, &f, &b);
		if (f == fg && b == bg) return pair;
	}
	init_pair(colorPairs, fg % COLORS, bg % COLORS);
	return colorPairs++;
}

enum {
	InputCols = 512,
	PadLines = 512,
};

static WINDOW *status;
static WINDOW *input;

struct Window {
	size_t id;
	WINDOW *pad;
	enum Heat heat;
	int unread;
	int scroll;
	bool mark;
	struct Window *prev;
	struct Window *next;
};

static struct {
	struct Window *active;
	struct Window *other;
	struct Window *head;
	struct Window *tail;
} windows;

static void windowAdd(struct Window *window) {
	if (windows.tail) windows.tail->next = window;
	window->prev = windows.tail;
	window->next = NULL;
	windows.tail = window;
	if (!windows.head) windows.head = window;
}

static void windowRemove(struct Window *window) {
	if (window->prev) window->prev->next = window->next;
	if (window->next) window->next->prev = window->prev;
	if (windows.head == window) windows.head = window->next;
	if (windows.tail == window) windows.tail = window->prev;
}

static struct Window *windowFor(size_t id) {
	struct Window *window;
	for (window = windows.head; window; window = window->next) {
		if (window->id == id) return window;
	}
	window = malloc(sizeof(*window));
	if (!window) err(EX_OSERR, "malloc");
	window->id = id;
	window->pad = newpad(PadLines, COLS);
	scrollok(window->pad, true);
	wmove(window->pad, PadLines - 1, 0);
	window->heat = Cold;
	window->unread = 0;
	window->scroll = PadLines;
	window->mark = true;
	windowAdd(window);
	return window;
}

static void errExit(int eval) {
	(void)eval;
	reset_shell_mode();
}

void uiInit(void) {
	initscr();
	cbreak();
	noecho();
	termInit();
	termNoFlow();
	def_prog_mode();
	err_set_exit(errExit);
	colorInit();
	status = newwin(1, COLS, 0, 0);
	input = newpad(1, InputCols);
	keypad(input, true);
	nodelay(input, true);
	windows.active = windowFor(Network);
}

void uiDraw(void) {
	wnoutrefresh(status);
	pnoutrefresh(
		windows.active->pad,
		windows.active->scroll - WINDOW_LINES, 0,
		1, 0,
		BOTTOM - 1, RIGHT
	);
	// TODO: Input scrolling.
	pnoutrefresh(
		input,
		0, 0,
		BOTTOM, 0,
		BOTTOM, RIGHT
	);
	doupdate();
}

struct Style {
	attr_t attr;
	enum Color fg, bg;
};
static const struct Style Reset = { A_NORMAL, Default, Default };

static short mapColor(enum Color color) {
	switch (color) {
		break; case White:      return 8 + COLOR_WHITE;
		break; case Black:      return 0 + COLOR_BLACK;
		break; case Blue:       return 0 + COLOR_BLUE;
		break; case Green:      return 0 + COLOR_GREEN;
		break; case Red:        return 8 + COLOR_RED;
		break; case Brown:      return 0 + COLOR_RED;
		break; case Magenta:    return 0 + COLOR_MAGENTA;
		break; case Orange:     return 0 + COLOR_YELLOW;
		break; case Yellow:     return 8 + COLOR_YELLOW;
		break; case LightGreen: return 8 + COLOR_GREEN;
		break; case Cyan:       return 0 + COLOR_CYAN;
		break; case LightCyan:  return 8 + COLOR_CYAN;
		break; case LightBlue:  return 8 + COLOR_BLUE;
		break; case Pink:       return 8 + COLOR_MAGENTA;
		break; case Gray:       return 8 + COLOR_BLACK;
		break; case LightGray:  return 0 + COLOR_WHITE;
		break; default:         return -1;
	}
}

static void styleParse(struct Style *style, const char **str, size_t *len) {
	switch (**str) {
		break; case '\2':  (*str)++; style->attr ^= A_BOLD;
		break; case '\17': (*str)++; *style = Reset;
		break; case '\26': (*str)++; style->attr ^= A_REVERSE;
		break; case '\35': (*str)++; style->attr ^= A_ITALIC;
		break; case '\37': (*str)++; style->attr ^= A_UNDERLINE;
		break; case '\3': {
			(*str)++;
			if (!isdigit(**str)) {
				style->fg = Default;
				style->bg = Default;
				break;
			}
			style->fg = *(*str)++ - '0';
			if (isdigit(**str)) style->fg = style->fg * 10 + *(*str)++ - '0';
			if ((*str)[0] != ',' || !isdigit((*str)[1])) break;
			(*str)++;
			style->bg = *(*str)++ - '0';
			if (isdigit(**str)) style->bg = style->bg * 10 + *(*str)++ - '0';
		}
	}
	*len = strcspn(*str, "\2\3\17\26\35\37");
}

static int wordWidth(const char *str) {
	size_t len = strcspn(str, " ");
	int width = 0;
	while (len) {
		wchar_t wc;
		int n = mbtowc(&wc, str, len);
		if (n < 1) return width + len;
		width += (iswprint(wc) ? wcwidth(wc) : 0);
		str += n;
		len -= n;
	}
	return width;
}

static void styleAdd(WINDOW *win, const char *str, bool show) {
	int y, x, width;
	getmaxyx(win, y, width);

	size_t len;
	struct Style style = Reset;
	while (*str) {
		if (*str == ' ') {
			getyx(win, y, x);
			const char *word = &str[strspn(str, " ")];
			if (width - x - 1 <= wordWidth(word)) {
				waddch(win, '\n');
				str = word;
			}
		}

		const char *code = str;
		styleParse(&style, &str, &len);
		if (show) {
			wattr_set(win, A_BOLD | A_REVERSE, 0, NULL);
			switch (*code) {
				break; case '\2':  waddch(win, 'B');
				break; case '\3':  waddch(win, 'C');
				break; case '\17': waddch(win, 'O');
				break; case '\26': waddch(win, 'R');
				break; case '\35': waddch(win, 'I');
				break; case '\37': waddch(win, 'U');
			}
			if (str - code > 1) waddnstr(win, &code[1], str - &code[1]);
		}

		size_t sp = strspn(str, " ");
		sp += strcspn(&str[sp], " ");
		if (sp < len) len = sp;

		wattr_set(
			win,
			style.attr | colorAttr(mapColor(style.fg)),
			colorPair(mapColor(style.fg), mapColor(style.bg)),
			NULL
		);
		waddnstr(win, str, len);
		str += len;
	}
}

static void statusUpdate(void) {
	wmove(status, 0, 0);
	int num;
	const struct Window *window;
	for (num = 0, window = windows.head; window; ++num, window = window->next) {
		if (!window->unread && window != windows.active) continue;
		int unread;
		char buf[256];
		snprintf(
			buf, sizeof(buf), "\3%d%s %d %s %n(\3%02d%d\3%d) ",
			idColors[window->id], (window == windows.active ? "\26" : ""),
			num, idNames[window->id],
			&unread, (window->heat > Warm ? White : idColors[window->id]),
			window->unread,
			idColors[window->id]
		);
		if (!window->unread) buf[unread] = '\0';
		styleAdd(status, buf, true);
	}
	wclrtoeol(status);

	int unread;
	char buf[256];
	snprintf(
		buf, sizeof(buf), "%s %s%n (%d)",
		self.network, idNames[windows.active->id],
		&unread, windows.active->unread
	);
	if (!windows.active->unread) buf[unread] = '\0';
	termTitle(buf);
}

void uiShowID(size_t id) {
	struct Window *window = windowFor(id);
	window->heat = Cold;
	window->unread = 0;
	window->mark = false;
	if (windows.active) windows.active->mark = true;
	windows.other = windows.active;
	windows.active = window;
	touchwin(window->pad);
	statusUpdate();
}

void uiWrite(size_t id, enum Heat heat, const struct tm *time, const char *str) {
	(void)time;
	struct Window *window = windowFor(id);
	waddch(window->pad, '\n');
	styleAdd(window->pad, str, true);
}

void uiFormat(
	size_t id, enum Heat heat, const struct tm *time, const char *format, ...
) {
	char buf[1024];
	va_list ap;
	va_start(ap, format);
	int len = vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	assert((size_t)len < sizeof(buf));
	uiWrite(id, heat, time, buf);
}
