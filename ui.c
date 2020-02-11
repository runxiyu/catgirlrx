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

#define _XOPEN_SOURCE_EXTENDED

#include <assert.h>
#include <ctype.h>
#include <curses.h>
#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <term.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#include "chat.h"

// Annoying stuff from <term.h>:
#undef lines
#undef tab

#ifndef A_ITALIC
#define A_ITALIC A_NORMAL
#endif

#define BOTTOM (LINES - 1)
#define RIGHT (COLS - 1)
#define PAGE_LINES (LINES - 2)

static WINDOW *status;
static WINDOW *marker;
static WINDOW *input;

enum { BufferCap = 512 };
struct Buffer {
	time_t times[BufferCap];
	char *lines[BufferCap];
	size_t len;
};
static_assert(!(BufferCap & (BufferCap - 1)), "BufferCap is power of two");

static void bufferPush(struct Buffer *buffer, time_t time, const char *line) {
	size_t i = buffer->len++ % BufferCap;
	free(buffer->lines[i]);
	buffer->times[i] = time;
	buffer->lines[i] = strdup(line);
	if (!buffer->lines[i]) err(EX_OSERR, "strdup");
}

static time_t bufferTime(const struct Buffer *buffer, size_t i) {
	return buffer->times[(buffer->len + i) % BufferCap];
}
static const char *bufferLine(const struct Buffer *buffer, size_t i) {
	return buffer->lines[(buffer->len + i) % BufferCap];
}

enum { WindowLines = BufferCap };
struct Window {
	size_t id;
	struct Buffer buffer;
	WINDOW *pad;
	int scroll;
	bool mark;
	enum Heat heat;
	int unreadCount;
	int unreadLines;
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
	window = calloc(1, sizeof(*window));
	if (!window) err(EX_OSERR, "malloc");

	window->id = id;
	window->pad = newpad(WindowLines, COLS);
	if (!window->pad) err(EX_OSERR, "newpad");
	scrollok(window->pad, true);
	wmove(window->pad, WindowLines - 1, 0);
	window->mark = true;

	windowAdd(window);
	return window;
}

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
	if (fg != COLOR_BLACK && fg % COLORS == COLOR_BLACK) return A_BOLD;
	if (COLORS > 8) return A_NORMAL;
	return (fg / COLORS & 1 ? A_BOLD : A_NORMAL);
}

static short colorPair(short fg, short bg) {
	fg %= COLORS;
	bg %= COLORS;
	if (bg == -1 && fg < 16) return 1 + fg;
	for (short pair = 17; pair < colorPairs; ++pair) {
		short f, b;
		pair_content(pair, &f, &b);
		if (f == fg && b == bg) return pair;
	}
	init_pair(colorPairs, fg, bg);
	return colorPairs++;
}

// XXX: Assuming terminals will be fine with these even if they're unsupported,
// since they're "private" modes.
static const char *EnterFocusMode = "\33[?1004h";
static const char *ExitFocusMode  = "\33[?1004l";
static const char *EnterPasteMode = "\33[?2004h";
static const char *ExitPasteMode  = "\33[?2004l";

// Gain use of C-q, C-s, C-c, C-z, C-y, C-o.
static void acquireKeys(void) {
	struct termios term;
	int error = tcgetattr(STDOUT_FILENO, &term);
	if (error) err(EX_OSERR, "tcgetattr");
	term.c_iflag &= ~IXON;
	term.c_cc[VINTR] = _POSIX_VDISABLE;
	term.c_cc[VSUSP] = _POSIX_VDISABLE;
	term.c_cc[VDSUSP] = _POSIX_VDISABLE;
	term.c_cc[VDISCARD] = _POSIX_VDISABLE;
	error = tcsetattr(STDOUT_FILENO, TCSADRAIN, &term);
	if (error) err(EX_OSERR, "tcsetattr");
}

static void errExit(void) {
	reset_shell_mode();
}

#define ENUM_KEY \
	X(KeyMeta0, "\0330") \
	X(KeyMeta1, "\0331") \
	X(KeyMeta2, "\0332") \
	X(KeyMeta3, "\0333") \
	X(KeyMeta4, "\0334") \
	X(KeyMeta5, "\0335") \
	X(KeyMeta6, "\0336") \
	X(KeyMeta7, "\0337") \
	X(KeyMeta8, "\0338") \
	X(KeyMeta9, "\0339") \
	X(KeyMetaA, "\33a") \
	X(KeyMetaB, "\33b") \
	X(KeyMetaD, "\33d") \
	X(KeyMetaF, "\33f") \
	X(KeyMetaL, "\33l") \
	X(KeyMetaM, "\33m") \
	X(KeyMetaU, "\33u") \
	X(KeyMetaSlash, "\33/") \
	X(KeyFocusIn, "\33[I") \
	X(KeyFocusOut, "\33[O") \
	X(KeyPasteOn, "\33[200~") \
	X(KeyPasteOff, "\33[201~")

enum {
	KeyMax = KEY_MAX,
#define X(id, seq) id,
	ENUM_KEY
#undef X
};

void uiInit(void) {
	initscr();
	cbreak();
	noecho();
	acquireKeys();
	def_prog_mode();
	atexit(errExit);
	colorInit();

	if (!to_status_line && !strncmp(termname(), "xterm", 5)) {
		to_status_line = "\33]2;";
		from_status_line = "\7";
	}

#define X(id, seq) define_key(seq, id);
	ENUM_KEY
#undef X

	status = newwin(1, COLS, 0, 0);
	if (!status) err(EX_OSERR, "newwin");

	marker = newwin(1, COLS, LINES - 2, 0);
	short fg = 8 + COLOR_BLACK;
	wbkgd(marker, '~' | colorAttr(fg) | COLOR_PAIR(colorPair(fg, -1)));

	input = newpad(1, 512);
	if (!input) err(EX_OSERR, "newpad");
	keypad(input, true);
	nodelay(input, true);

	windows.active = windowFor(Network);
	uiShow();
}

static bool hidden;
static bool waiting;

static char title[256];
static char prevTitle[sizeof(title)];

void uiDraw(void) {
	if (hidden) return;
	wnoutrefresh(status);
	struct Window *window = windows.active;
	pnoutrefresh(
		window->pad,
		WindowLines - window->scroll - PAGE_LINES + !!window->scroll, 0,
		1, 0,
		BOTTOM - 1 - !!window->scroll, RIGHT
	);
	if (window->scroll) {
		touchwin(marker);
		wnoutrefresh(marker);
	}
	int y, x;
	getyx(input, y, x);
	pnoutrefresh(
		input,
		0, (x + 1 > RIGHT ? x + 1 - RIGHT : 0),
		BOTTOM, 0,
		BOTTOM, RIGHT
	);
	doupdate();

	if (!to_status_line) return;
	if (!strcmp(title, prevTitle)) return;
	strcpy(prevTitle, title);
	putp(to_status_line);
	putp(title);
	putp(from_status_line);
	fflush(stdout);
}

void uiShow(void) {
	prevTitle[0] = '\0';
	putp(EnterFocusMode);
	putp(EnterPasteMode);
	fflush(stdout);
	hidden = false;
}

void uiHide(void) {
	hidden = true;
	putp(ExitFocusMode);
	putp(ExitPasteMode);
	endwin();
}

struct Style {
	attr_t attr;
	enum Color fg, bg;
};
static const struct Style Reset = { A_NORMAL, Default, Default };

static const short Colors[100] = {
	[Default]    = -1,
	[White]      = 8 + COLOR_WHITE,
	[Black]      = 0 + COLOR_BLACK,
	[Blue]       = 0 + COLOR_BLUE,
	[Green]      = 0 + COLOR_GREEN,
	[Red]        = 8 + COLOR_RED,
	[Brown]      = 0 + COLOR_RED,
	[Magenta]    = 0 + COLOR_MAGENTA,
	[Orange]     = 0 + COLOR_YELLOW,
	[Yellow]     = 8 + COLOR_YELLOW,
	[LightGreen] = 8 + COLOR_GREEN,
	[Cyan]       = 0 + COLOR_CYAN,
	[LightCyan]  = 8 + COLOR_CYAN,
	[LightBlue]  = 8 + COLOR_BLUE,
	[Pink]       = 8 + COLOR_MAGENTA,
	[Gray]       = 8 + COLOR_BLACK,
	[LightGray]  = 0 + COLOR_WHITE,
	52, 94, 100, 58, 22, 29, 23, 24, 17, 54, 53, 89,
	88, 130, 142, 64, 28, 35, 30, 25, 18, 91, 90, 125,
	124, 166, 184, 106, 34, 49, 37, 33, 19, 129, 127, 161,
	196, 208, 226, 154, 46, 86, 51, 75, 21, 171, 201, 198,
	203, 215, 227, 191, 83, 122, 87, 111, 63, 177, 207, 205,
	217, 223, 229, 193, 157, 158, 159, 153, 147, 183, 219, 212,
	16, 233, 235, 237, 239, 241, 244, 247, 250, 254, 231,
};

enum { B = '\2', C = '\3', O = '\17', R = '\26', I = '\35', U = '\37' };

static void styleParse(struct Style *style, const char **str, size_t *len) {
	switch (**str) {
		break; case B: (*str)++; style->attr ^= A_BOLD;
		break; case O: (*str)++; *style = Reset;
		break; case R: (*str)++; style->attr ^= A_REVERSE;
		break; case I: (*str)++; style->attr ^= A_ITALIC;
		break; case U: (*str)++; style->attr ^= A_UNDERLINE;
		break; case C: {
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
	*len = strcspn(*str, (const char[]) { B, C, O, R, I, U, '\0' });
}

static void statusAdd(const char *str) {
	size_t len;
	struct Style style = Reset;
	while (*str) {
		styleParse(&style, &str, &len);
		wattr_set(
			status,
			style.attr | colorAttr(Colors[style.fg]),
			colorPair(Colors[style.fg], Colors[style.bg]),
			NULL
		);
		waddnstr(status, str, len);
		str += len;
	}
}

static void statusUpdate(void) {
	int otherUnread = 0;
	enum Heat otherHeat = Cold;
	wmove(status, 0, 0);

	int num;
	const struct Window *window;
	for (num = 0, window = windows.head; window; ++num, window = window->next) {
		if (!window->heat && window != windows.active) continue;
		if (window != windows.active) {
			otherUnread += window->unreadCount;
			if (window->heat > otherHeat) otherHeat = window->heat;
		}
		int trunc;
		char buf[256];
		snprintf(
			buf, sizeof(buf), "\3%d%s %d %s %n(\3%02d%d\3%d) ",
			idColors[window->id], (window == windows.active ? "\26" : ""),
			num, idNames[window->id],
			&trunc, (window->heat > Warm ? White : idColors[window->id]),
			window->unreadCount,
			idColors[window->id]
		);
		if (!window->mark || !window->unreadCount) buf[trunc] = '\0';
		statusAdd(buf);
	}
	wclrtoeol(status);

	window = windows.active;
	snprintf(title, sizeof(title), "%s %s", self.network, idNames[window->id]);
	if (window->mark && window->unreadCount) {
		snprintf(
			&title[strlen(title)], sizeof(title) - strlen(title),
			" (%d%s)", window->unreadCount, (window->heat > Warm ? "!" : "")
		);
	}
	if (otherUnread) {
		snprintf(
			&title[strlen(title)], sizeof(title) - strlen(title),
			" (+%d%s)", otherUnread, (otherHeat > Warm ? "!" : "")
		);
	}
}

static void mark(struct Window *window) {
	if (window->scroll) return;
	window->mark = true;
	window->unreadCount = 0;
	window->unreadLines = 0;
}

static void unmark(struct Window *window) {
	if (!window->scroll) {
		window->mark = false;
		window->heat = Cold;
	}
	statusUpdate();
}

static void windowScroll(struct Window *window, int n) {
	mark(window);
	window->scroll += n;
	if (window->scroll > WindowLines - PAGE_LINES) {
		window->scroll = WindowLines - PAGE_LINES;
	}
	if (window->scroll < 0) window->scroll = 0;
	unmark(window);
}

static void windowScrollUnread(struct Window *window) {
	window->scroll = 0;
	windowScroll(window, window->unreadLines - PAGE_LINES + 1);
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

static int wordWrap(WINDOW *win, const char *str) {
	int y, x, width;
	getmaxyx(win, y, width);

	size_t len;
	int lines = 0;
	int align = 0;
	struct Style style = Reset;
	while (*str) {
		if (*str == '\t') {
			if (align) {
				waddch(win, '\t');
				str++;
			} else {
				waddch(win, ' ');
				getyx(win, y, align);
				str++;
			}
		} else if (*str == ' ') {
			getyx(win, y, x);
			const char *word = &str[strspn(str, " ")];
			if (width - x - 1 <= wordWidth(word)) {
				lines += 1 + (align + wordWidth(word)) / width;
				waddch(win, '\n');
				getyx(win, y, x);
				wmove(win, y, align);
				str = word;
			} else {
				waddch(win, ' ');
				str++;
			}
		}

		styleParse(&style, &str, &len);
		size_t ws = strcspn(str, "\t ");
		if (ws < len) len = ws;

		wattr_set(
			win,
			style.attr | colorAttr(Colors[style.fg]),
			colorPair(Colors[style.fg], Colors[style.bg]),
			NULL
		);
		waddnstr(win, str, len);
		str += len;
	}
	return lines;
}

void uiWrite(size_t id, enum Heat heat, const time_t *src, const char *str) {
	struct Window *window = windowFor(id);
	time_t clock = (src ? *src : time(NULL));
	bufferPush(&window->buffer, clock, str);

	int lines = 1;
	waddch(window->pad, '\n');
	if (window->mark && heat > Cold) {
		if (!window->unreadCount++) {
			lines++;
			waddch(window->pad, '\n');
		}
		if (window->heat < heat) window->heat = heat;
		statusUpdate();
	}
	lines += wordWrap(window->pad, str);
	window->unreadLines += lines;
	if (window->scroll) windowScroll(window, lines);
	if (heat > Warm) beep();
}

void uiFormat(
	size_t id, enum Heat heat, const time_t *time, const char *format, ...
) {
	char buf[1024];
	va_list ap;
	va_start(ap, format);
	int len = vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	assert((size_t)len < sizeof(buf));
	uiWrite(id, heat, time, buf);
}

static void reflow(struct Window *window) {
	werase(window->pad);
	wmove(window->pad, WindowLines - 1, 0);
	window->unreadLines = 0;
	for (size_t i = 0; i < BufferCap; ++i) {
		const char *line = bufferLine(&window->buffer, i);
		if (!line) continue;
		waddch(window->pad, '\n');
		if (i >= (size_t)(BufferCap - window->unreadCount)) {
			window->unreadLines += 1 + wordWrap(window->pad, line);
		} else {
			wordWrap(window->pad, line);
		}
	}
}

static void resize(void) {
	mvwin(marker, LINES - 2, 0);
	int height, width;
	getmaxyx(windows.active->pad, height, width);
	if (width == COLS) return;
	for (struct Window *window = windows.head; window; window = window->next) {
		wresize(window->pad, BufferCap, COLS);
		reflow(window);
	}
	statusUpdate();
}

static void bufferList(const struct Buffer *buffer) {
	uiHide();
	waiting = true;
	for (size_t i = 0; i < BufferCap; ++i) {
		time_t time = bufferTime(buffer, i);
		const char *line = bufferLine(buffer, i);
		if (!line) continue;

		struct tm *tm = localtime(&time);
		if (!tm) continue;
		char buf[sizeof("[00:00:00]")];
		strftime(buf, sizeof(buf), "[%T]", tm);
		vid_attr(colorAttr(Colors[Gray]), colorPair(Colors[Gray], -1), NULL);
		printf("%s ", buf);

		size_t len;
		bool align = false;
		struct Style style = Reset;
		while (*line) {
			if (*line == '\t') {
				printf("%c", (align ? '\t' : ' '));
				align = true;
				line++;
			}
			styleParse(&style, &line, &len);
			size_t tab = strcspn(line, "\t");
			if (tab < len) len = tab;
			vid_attr(
				style.attr | colorAttr(Colors[style.fg]),
				colorPair(Colors[style.fg], Colors[style.bg]),
				NULL
			);
			if (len) printf("%.*s", (int)len, line);
			line += len;
		}
		printf("\n");
	}
}

static void inputAdd(struct Style *style, const char *str) {
	size_t len;
	while (*str) {
		const char *code = str;
		styleParse(style, &str, &len);
		wattr_set(input, A_BOLD | A_REVERSE, 0, NULL);
		switch (*code) {
			break; case B: waddch(input, 'B');
			break; case C: waddch(input, 'C');
			break; case O: waddch(input, 'O');
			break; case R: waddch(input, 'R');
			break; case I: waddch(input, 'I');
			break; case U: waddch(input, 'U');
		}
		if (str - code > 1) waddnstr(input, &code[1], str - &code[1]);
		wattr_set(
			input,
			style->attr | colorAttr(Colors[style->fg]),
			colorPair(Colors[style->fg], Colors[style->bg]),
			NULL
		);
		waddnstr(input, str, len);
		str += len;
	}
}

static void inputUpdate(void) {
	size_t id = windows.active->id;
	size_t pos;
	char *buf = editBuffer(&pos);

	const char *skip = NULL;
	struct Style init = { .fg = self.color, .bg = Default };
	struct Style rest = Reset;
	const char *prefix = "";
	const char *prompt = (self.nick ? self.nick : "");
	const char *suffix = "";
	if (NULL != (skip = commandIsPrivmsg(id, buf))) {
		prefix = "<"; suffix = "> ";
	} else if (NULL != (skip = commandIsNotice(id, buf))) {
		prefix = "-"; suffix = "- ";
		rest.fg = LightGray;
	} else if (NULL != (skip = commandIsAction(id, buf))) {
		init.attr |= A_ITALIC;
		prefix = "* "; suffix = " ";
		rest.attr |= A_ITALIC;
	} else if (id == Debug) {
		skip = buf;
		init.fg = Gray;
		prompt = "<< ";
	} else {
		prompt = "";
	}
	if (skip && skip > &buf[pos]) {
		skip = NULL;
		prefix = prompt = suffix = "";
	}

	int y, x;
	wmove(input, 0, 0);
	wattr_set(
		input,
		init.attr | colorAttr(Colors[init.fg]),
		colorPair(Colors[init.fg], Colors[init.bg]),
		NULL
	);
	waddstr(input, prefix);
	waddstr(input, prompt);
	waddstr(input, suffix);
	struct Style style = rest;
	char p = buf[pos];
	buf[pos] = '\0';
	inputAdd(&style, (skip ? skip : buf));
	getyx(input, y, x);
	buf[pos] = p;
	inputAdd(&style, &buf[pos]);
	wclrtoeol(input);
	wmove(input, y, x);
}

static void windowShow(struct Window *window) {
	if (!window) return;
	touchwin(window->pad);
	windows.other = windows.active;
	windows.active = window;
	mark(windows.other);
	unmark(windows.active);
	inputUpdate();
}

void uiShowID(size_t id) {
	windowShow(windowFor(id));
}

void uiShowNum(size_t num) {
	struct Window *window = windows.head;
	for (size_t i = 0; i < num; ++i) {
		window = window->next;
		if (!window) return;
	}
	windowShow(window);
}

static void windowClose(struct Window *window) {
	if (window->id == Network) return;
	if (windows.active == window) {
		if (windows.other && windows.other != window) {
			windowShow(windows.other);
		} else {
			windowShow(window->prev ? window->prev : window->next);
		}
	}
	if (windows.other == window) windows.other = NULL;
	windowRemove(window);
	for (size_t i = 0; i < BufferCap; ++i) {
		free(window->buffer.lines[i]);
	}
	delwin(window->pad);
	free(window);
	statusUpdate();
}

void uiCloseID(size_t id) {
	windowClose(windowFor(id));
}

void uiCloseNum(size_t num) {
	struct Window *window = windows.head;
	for (size_t i = 0; i < num; ++i) {
		window = window->next;
		if (!window) return;
	}
	windowClose(window);
}

static void showAuto(void) {
	static struct Window *other;
	if (windows.other != other) {
		other = windows.active;
	}
	for (struct Window *window = windows.head; window; window = window->next) {
		if (window->heat < Hot) continue;
		windowShow(window);
		windows.other = other;
		return;
	}
	for (struct Window *window = windows.head; window; window = window->next) {
		if (window->heat < Warm) continue;
		windowShow(window);
		windows.other = other;
		return;
	}
	windowShow(windows.other);
}

static void keyCode(int code) {
	struct Window *window = windows.active;
	size_t id = window->id;
	switch (code) {
		break; case KEY_RESIZE:  resize();
		break; case KeyFocusIn:  unmark(window);
		break; case KeyFocusOut: mark(window);
		break; case KeyPasteOn:; // TODO
		break; case KeyPasteOff:; // TODO

		break; case KeyMetaSlash: windowShow(windows.other);

		break; case KeyMetaA: showAuto();
		break; case KeyMetaB: edit(id, EditPrevWord, 0);
		break; case KeyMetaD: edit(id, EditDeleteNextWord, 0);
		break; case KeyMetaF: edit(id, EditNextWord, 0);
		break; case KeyMetaL: bufferList(&window->buffer);
		break; case KeyMetaM: waddch(window->pad, '\n');
		break; case KeyMetaU: windowScrollUnread(window);

		break; case KEY_BACKSPACE: edit(id, EditDeletePrev, 0);
		break; case KEY_DC: edit(id, EditDeleteNext, 0);
		break; case KEY_DOWN: windowScroll(window, -1);
		break; case KEY_END: edit(id, EditTail, 0);
		break; case KEY_ENTER: edit(id, EditEnter, 0);
		break; case KEY_HOME: edit(id, EditHead, 0);
		break; case KEY_LEFT: edit(id, EditPrev, 0);
		break; case KEY_NPAGE: windowScroll(window, -(PAGE_LINES - 2));
		break; case KEY_PPAGE: windowScroll(window, +(PAGE_LINES - 2));
		break; case KEY_RIGHT: edit(id, EditNext, 0);
		break; case KEY_UP: windowScroll(window, +1);
		
		break; default: {
			if (code >= KeyMeta0 && code <= KeyMeta9) {
				uiShowNum(code - KeyMeta0);
			}
		}
	}
}

static void keyCtrl(wchar_t ch) {
	size_t id = windows.active->id;
	switch (ch ^ L'@') {
		break; case L'?': edit(id, EditDeletePrev, 0);
		break; case L'A': edit(id, EditHead, 0);
		break; case L'B': edit(id, EditPrev, 0);
		break; case L'C': raise(SIGINT);
		break; case L'D': edit(id, EditDeleteNext, 0);
		break; case L'E': edit(id, EditTail, 0);
		break; case L'F': edit(id, EditNext, 0);
		break; case L'H': edit(id, EditDeletePrev, 0);
		break; case L'I': edit(id, EditComplete, 0);
		break; case L'J': edit(id, EditEnter, 0);
		break; case L'K': edit(id, EditDeleteTail, 0);
		break; case L'L': clearok(curscr, true);
		break; case L'N': windowShow(windows.active->next);
		break; case L'O': windowShow(windows.other);
		break; case L'P': windowShow(windows.active->prev);
		break; case L'U': edit(id, EditDeleteHead, 0);
		break; case L'W': edit(id, EditDeletePrevWord, 0);
		break; case L'Y': edit(id, EditPaste, 0);
	}
}

static void keyStyle(wchar_t ch) {
	size_t id = windows.active->id;
	switch (iswcntrl(ch) ? ch ^ L'@' : towupper(ch)) {
		break; case L'B': edit(id, EditInsert, B);
		break; case L'C': edit(id, EditInsert, C);
		break; case L'I': edit(id, EditInsert, I);
		break; case L'O': edit(id, EditInsert, O);
		break; case L'R': edit(id, EditInsert, R);
		break; case L'U': edit(id, EditInsert, U);
	}
}

void uiRead(void) {
	if (hidden) {
		if (waiting) {
			uiShow();
			flushinp();
			waiting = false;
		} else {
			return;
		}
	}

	int ret;
	wint_t ch;
	static bool style;
	while (ERR != (ret = wget_wch(input, &ch))) {
		if (ret == KEY_CODE_YES) {
			keyCode(ch);
		} else if (ch == (L'Z' ^ L'@')) {
			style = true;
			continue;
		} else if (style) {
			keyStyle(ch);
		} else if (iswcntrl(ch)) {
			keyCtrl(ch);
		} else {
			edit(windows.active->id, EditInsert, ch);
		}
		style = false;
	}
	inputUpdate();
}

static const size_t Signatures[] = {
	0x6C72696774616301,
};

static size_t signatureVersion(size_t signature) {
	for (size_t i = 0; i < ARRAY_LEN(Signatures); ++i) {
		if (signature == Signatures[i]) return i;
	}
	err(EX_DATAERR, "unknown file signature %zX", signature);
}

static int writeSize(FILE *file, size_t value) {
	return (fwrite(&value, sizeof(value), 1, file) ? 0 : -1);
}
static int writeTime(FILE *file, time_t time) {
	return (fwrite(&time, sizeof(time), 1, file) ? 0 : -1);
}
static int writeString(FILE *file, const char *str) {
	return (fwrite(str, strlen(str) + 1, 1, file) ? 0 : -1);
}

int uiSave(const char *name) {
	FILE *file = dataOpen(name, "w");
	if (!file) return -1;

	if (writeSize(file, Signatures[0])) return -1;
	const struct Window *window;
	for (window = windows.head; window; window = window->next) {
		if (writeString(file, idNames[window->id])) return -1;
		for (size_t i = 0; i < BufferCap; ++i) {
			time_t time = bufferTime(&window->buffer, i);
			const char *line = bufferLine(&window->buffer, i);
			if (!line) continue;
			if (writeTime(file, time)) return -1;
			if (writeString(file, line)) return -1;
		}
		if (writeTime(file, 0)) return -1;
	}
	return fclose(file);
}

static size_t readSize(FILE *file) {
	size_t value;
	fread(&value, sizeof(value), 1, file);
	if (ferror(file)) err(EX_IOERR, "fread");
	if (feof(file)) errx(EX_DATAERR, "unexpected eof");
	return value;
}
static time_t readTime(FILE *file) {
	time_t time;
	fread(&time, sizeof(time), 1, file);
	if (ferror(file)) err(EX_IOERR, "fread");
	if (feof(file)) errx(EX_DATAERR, "unexpected eof");
	return time;
}
static ssize_t readString(FILE *file, char **buf, size_t *cap) {
	ssize_t len = getdelim(buf, cap, '\0', file);
	if (len < 0 && !feof(file)) err(EX_IOERR, "getdelim");
	return len;
}

void uiLoad(const char *name) {
	FILE *file = dataOpen(name, "r");
	if (!file) {
		if (errno != ENOENT) exit(EX_NOINPUT);
		file = dataOpen(name, "w");
		if (!file) exit(EX_CANTCREAT);
		fclose(file);
		return;
	}

	size_t signature = readSize(file);
	signatureVersion(signature);

	char *buf = NULL;
	size_t cap = 0;
	while (0 < readString(file, &buf, &cap)) {
		struct Window *window = windowFor(idFor(buf));
		for (;;) {
			time_t time = readTime(file);
			if (!time) break;
			readString(file, &buf, &cap);
			bufferPush(&window->buffer, time, buf);
		}
		reflow(window);
		waddch(window->pad, '\n');
	}

	free(buf);
	fclose(file);
}
