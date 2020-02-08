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

#ifndef A_ITALIC
#define A_ITALIC A_NORMAL
#endif

#define BOTTOM (LINES - 1)
#define RIGHT (COLS - 1)
#define WINDOW_LINES (LINES - 2)

static WINDOW *status;
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

struct Window {
	size_t id;
	struct Buffer buffer;
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
	window = calloc(1, sizeof(*window));
	if (!window) err(EX_OSERR, "malloc");

	window->id = id;
	window->pad = newpad(BufferCap, COLS);
	scrollok(window->pad, true);
	wmove(window->pad, BufferCap - 1, 0);
	window->scroll = BufferCap;
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
	return (fg >= COLORS ? A_BOLD : A_NORMAL);
}

static short colorPair(short fg, short bg) {
	if (bg == -1) return 1 + fg;
	fg %= COLORS;
	bg %= COLORS;
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

void uiShow(void) {
	putp(EnterFocusMode);
	putp(EnterPasteMode);
	fflush(stdout);
}

void uiHide(void) {
	putp(ExitFocusMode);
	putp(ExitPasteMode);
	endwin();
}

static void disableFlowControl(void) {
	struct termios term;
	int error = tcgetattr(STDOUT_FILENO, &term);
	if (error) err(EX_OSERR, "tcgetattr");
	term.c_iflag &= ~IXON;
	term.c_cc[VSUSP] = _POSIX_VDISABLE;
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
	X(KeyMetaM, "\33m") \
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
	disableFlowControl();
	def_prog_mode();
	atexit(errExit);

	if (!to_status_line && !strncmp(termname(), "xterm", 5)) {
		to_status_line = "\33]2;";
		from_status_line = "\7";
	}
#define X(id, seq) define_key(seq, id);
	ENUM_KEY
#undef X

	colorInit();
	status = newwin(1, COLS, 0, 0);
	input = newpad(1, 512);
	keypad(input, true);
	nodelay(input, true);
	windows.active = windowFor(Network);
	uiShow();
}

void uiDraw(void) {
	wnoutrefresh(status);
	pnoutrefresh(
		windows.active->pad,
		windows.active->scroll - WINDOW_LINES, 0,
		1, 0,
		BOTTOM - 1, RIGHT
	);
	int y, x;
	getyx(input, y, x);
	pnoutrefresh(
		input,
		0, (x + 1 > RIGHT ? x + 1 - RIGHT : 0),
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
			style.attr | colorAttr(mapColor(style.fg)),
			colorPair(mapColor(style.fg), mapColor(style.bg)),
			NULL
		);
		waddnstr(status, str, len);
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
		statusAdd(buf);
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
	putp(to_status_line);
	putp(buf);
	putp(from_status_line);
	fflush(stdout);
}

static void unmark(void) {
	windows.active->heat = Cold;
	windows.active->unread = 0;
	windows.active->mark = false;
	statusUpdate();
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

static void wordWrap(WINDOW *win, const char *str) {
	int y, x, width;
	getmaxyx(win, y, width);

	size_t len;
	int align = 0;
	struct Style style = Reset;
	while (*str) {
		if (*str == '\t' && !align) {
			waddch(win, ' ');
			getyx(win, y, align);
			str++;
		} else if (*str == ' ') {
			getyx(win, y, x);
			const char *word = &str[strspn(str, " ")];
			if (width - x - 1 <= wordWidth(word)) {
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
			style.attr | colorAttr(mapColor(style.fg)),
			colorPair(mapColor(style.fg), mapColor(style.bg)),
			NULL
		);
		waddnstr(win, str, len);
		str += len;
	}
}

void uiWrite(size_t id, enum Heat heat, const time_t *src, const char *str) {
	struct Window *window = windowFor(id);
	time_t clock = (src ? *src : time(NULL));
	bufferPush(&window->buffer, clock, str);

	waddch(window->pad, '\n');
	if (window->mark && heat > Cold) {
		if (!window->unread++) {
			waddch(window->pad, '\n');
		}
		window->heat = heat;
		statusUpdate();
	}
	wordWrap(window->pad, str);
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
	wmove(window->pad, BufferCap - 1, 0);
	size_t len = window->buffer.len;
	for (size_t i = (len > BufferCap ? len - BufferCap : 0); i < len; ++i) {
		waddch(window->pad, '\n');
		wordWrap(window->pad, window->buffer.lines[i % BufferCap]);
	}
}

static void resize(void) {
	int height, width;
	getmaxyx(windows.active->pad, height, width);
	if (width == COLS) return;
	for (struct Window *window = windows.head; window; window = window->next) {
		wresize(window->pad, BufferCap, COLS);
		reflow(window);
	}
	statusUpdate();
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
			style->attr | colorAttr(mapColor(style->fg)),
			colorPair(mapColor(style->fg), mapColor(style->bg)),
			NULL
		);
		waddnstr(input, str, len);
		str += len;
	}
}

static void inputUpdate(void) {
	size_t id = windows.active->id;
	const char *nick = self.nick;
	const char *head = editHead();
	const char *skip = NULL;
	const char *pre = "";
	const char *suf = " ";
	struct Style style = { .fg = self.color, .bg = Default };
	struct Style reset = Reset;
	if (NULL != (skip = commandIsPrivmsg(id, head))) {
		pre = "<";
		suf = "> ";
	} else if (NULL != (skip = commandIsNotice(id, head))) {
		pre = "-";
		suf = "- ";
		reset.fg = LightGray;
	} else if (NULL != (skip = commandIsAction(id, head))) {
		style.attr |= A_ITALIC;
		pre = "* ";
		reset.attr |= A_ITALIC;
	} else if (id == Debug) {
		skip = head;
		style.fg = Gray;
		pre = "<<";
		nick = NULL;
	}

	int y, x;
	wmove(input, 0, 0);
	if (skip) {
		wattr_set(
			input,
			style.attr | colorAttr(mapColor(style.fg)),
			colorPair(mapColor(style.fg), mapColor(style.bg)),
			NULL
		);
		waddstr(input, pre);
		if (nick) waddstr(input, nick);
		waddstr(input, suf);
	}
	style = reset;
	inputAdd(&style, (skip ? skip : head));
	getyx(input, y, x);
	inputAdd(&style, editTail());
	wclrtoeol(input);
	wmove(input, y, x);
}

static void windowShow(struct Window *window) {
	touchwin(window->pad);
	windows.other = windows.active;
	windows.active = window;
	windows.other->mark = true;
	inputUpdate();
	unmark();
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

static void keyCode(int code) {
	size_t id = windows.active->id;
	switch (code) {
		break; case KEY_RESIZE:  resize();
		break; case KeyFocusIn:  unmark();
		break; case KeyFocusOut: windows.active->mark = true;
		break; case KeyPasteOn:; // TODO
		break; case KeyPasteOff:; // TODO

		break; case KeyMetaM: waddch(windows.active->pad, '\n');

		break; case KEY_BACKSPACE: edit(id, EditErase, 0);
		break; case KEY_END: edit(id, EditEnd, 0);
		break; case KEY_ENTER: edit(id, EditEnter, 0);
		break; case KEY_HOME: edit(id, EditHome, 0);
		break; case KEY_LEFT: edit(id, EditLeft, 0);
		break; case KEY_RIGHT: edit(id, EditRight, 0);
		
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
		break; case L'?': edit(id, EditErase, 0);
		break; case L'A': edit(id, EditHome, 0);
		break; case L'E': edit(id, EditEnd, 0);
		break; case L'H': edit(id, EditErase, 0);
		break; case L'I': edit(id, EditComplete, 0);
		break; case L'J': edit(id, EditEnter, 0);
		break; case L'L': clearok(curscr, true);
		break; case L'U': edit(id, EditKill, 0);
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
