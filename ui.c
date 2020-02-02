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
#include <curses.h>
#include <err.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <time.h>

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
	init_pair(colorPairs, fg, bg);
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
	wsetscrreg(window->pad, 0, PadLines - 1);
	scrollok(window->pad, true);
	wmove(window->pad, PadLines - 1, 0);
	window->heat = Cold;
	window->unread = 0;
	window->scroll = PadLines;
	window->mark = true;
	windowAdd(window);
	return window;
}

void uiInit(void) {
	initscr();
	cbreak();
	noecho();
	termInit();
	termNoFlow();
	def_prog_mode();
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

void uiWrite(size_t id, enum Heat heat, const struct tm *time, const char *str) {
	(void)time;
	struct Window *window = windowFor(id);
	waddch(window->pad, '\n');
	waddstr(window->pad, str);
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
