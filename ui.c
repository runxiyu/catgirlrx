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
 *
 * Additional permission under GNU GPL version 3 section 7:
 *
 * If you modify this Program, or any covered work, by linking or
 * combining it with LibreSSL (or a modified version of that library),
 * containing parts covered by the terms of the OpenSSL License and the
 * original SSLeay license, the licensors of this Program grant you
 * additional permission to convey the resulting work. Corresponding
 * Source for a non-source form of such a combination shall include the
 * source code for the parts of LibreSSL used as well as that of the
 * covered work.
 */

#define _XOPEN_SOURCE_EXTENDED

#include <assert.h>
#include <ctype.h>
#include <curses.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
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

enum {
	StatusLines = 1,
	WindowLines = 1024,
	MarkerLines = 1,
	SplitLines = 5,
	InputLines = 1,
	InputCols = 1024,
};

#define BOTTOM (LINES - 1)
#define RIGHT (COLS - 1)
#define MAIN_LINES (LINES - StatusLines - InputLines)

static WINDOW *status;
static WINDOW *marker;
static WINDOW *input;

struct Line {
	enum Heat heat;
	time_t time;
	char *str;
};

enum { BufferCap = 1024 };
struct Buffer {
	size_t len;
	struct Line lines[BufferCap];
};
static_assert(!(BufferCap & (BufferCap - 1)), "BufferCap is power of two");

static void bufferPush(
	struct Buffer *buffer, enum Heat heat, time_t time, const char *str
) {
	struct Line *line = &buffer->lines[buffer->len++ % BufferCap];
	free(line->str);
	line->heat = heat;
	line->time = time;
	line->str = strdup(str);
	if (!line->str) err(EX_OSERR, "strdup");
}

static struct Line bufferLine(const struct Buffer *buffer, size_t i) {
	return buffer->lines[(buffer->len + i) % BufferCap];
}

struct Window {
	uint id;
	WINDOW *pad;
	int scroll;
	bool mark;
	bool mute;
	bool ignore;
	enum Heat heat;
	uint unreadHard;
	uint unreadSoft;
	uint unreadWarm;
	struct Buffer buffer;
};

static struct {
	struct Window *ptrs[IDCap];
	uint len;
	uint show;
	uint swap;
	uint user;
} windows;

static uint windowPush(struct Window *window) {
	assert(windows.len < IDCap);
	windows.ptrs[windows.len] = window;
	return windows.len++;
}

static uint windowInsert(uint num, struct Window *window) {
	assert(windows.len < IDCap);
	assert(num <= windows.len);
	memmove(
		&windows.ptrs[num + 1],
		&windows.ptrs[num],
		sizeof(*windows.ptrs) * (windows.len - num)
	);
	windows.ptrs[num] = window;
	windows.len++;
	return num;
}

static struct Window *windowRemove(uint num) {
	assert(num < windows.len);
	struct Window *window = windows.ptrs[num];
	windows.len--;
	memmove(
		&windows.ptrs[num],
		&windows.ptrs[num + 1],
		sizeof(*windows.ptrs) * (windows.len - num)
	);
	return window;
}

static uint windowFor(uint id) {
	for (uint num = 0; num < windows.len; ++num) {
		if (windows.ptrs[num]->id == id) return num;
	}

	struct Window *window = calloc(1, sizeof(*window));
	if (!window) err(EX_OSERR, "malloc");

	window->id = id;
	window->pad = newpad(WindowLines, COLS);
	if (!window->pad) err(EX_OSERR, "newpad");
	scrollok(window->pad, true);
	wmove(window->pad, WindowLines - 1, 0);
	window->mark = true;
	window->ignore = true;

	return windowPush(window);
}

static void windowFree(struct Window *window) {
	for (size_t i = 0; i < BufferCap; ++i) {
		free(window->buffer.lines[i].str);
	}
	delwin(window->pad);
	free(window);
}

static short colorPairs;

static void colorInit(void) {
	start_color();
	use_default_colors();
	if (!COLORS) return;
	for (short pair = 0; pair < 16; ++pair) {
		init_pair(1 + pair, pair % COLORS, -1);
	}
	colorPairs = 17;
}

static attr_t colorAttr(short fg) {
	if (!COLORS) return (fg > 0 ? A_BOLD : A_NORMAL);
	if (fg != COLOR_BLACK && fg % COLORS == COLOR_BLACK) return A_BOLD;
	if (COLORS > 8) return A_NORMAL;
	return (fg / COLORS & 1 ? A_BOLD : A_NORMAL);
}

static short colorPair(short fg, short bg) {
	if (!COLORS) return 0;
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

#define ENUM_KEY \
	X(KeyMeta0, "\0330", "\33)") \
	X(KeyMeta1, "\0331", "\33!") \
	X(KeyMeta2, "\0332", "\33@") \
	X(KeyMeta3, "\0333", "\33#") \
	X(KeyMeta4, "\0334", "\33$") \
	X(KeyMeta5, "\0335", "\33%") \
	X(KeyMeta6, "\0336", "\33^") \
	X(KeyMeta7, "\0337", "\33&") \
	X(KeyMeta8, "\0338", "\33*") \
	X(KeyMeta9, "\0339", "\33(") \
	X(KeyMetaA, "\33a", NULL) \
	X(KeyMetaB, "\33b", NULL) \
	X(KeyMetaD, "\33d", NULL) \
	X(KeyMetaF, "\33f", NULL) \
	X(KeyMetaL, "\33l", NULL) \
	X(KeyMetaM, "\33m", NULL) \
	X(KeyMetaQ, "\33q", NULL) \
	X(KeyMetaU, "\33u", NULL) \
	X(KeyMetaV, "\33v", NULL) \
	X(KeyMetaEnter, "\33\r", "\33\n") \
	X(KeyMetaGt, "\33>", "\33.") \
	X(KeyMetaLt, "\33<", "\33,") \
	X(KeyMetaEqual, "\33=", "\33+") \
	X(KeyMetaMinus, "\33-", "\33_") \
	X(KeyMetaSlash, "\33/", "\33?") \
	X(KeyFocusIn, "\33[I", NULL) \
	X(KeyFocusOut, "\33[O", NULL) \
	X(KeyPasteOn, "\33[200~", NULL) \
	X(KeyPasteOff, "\33[201~", NULL)

enum {
	KeyMax = KEY_MAX,
#define X(id, seq, alt) id,
	ENUM_KEY
#undef X
};

// Gain use of C-q, C-s, C-c, C-z, C-y, C-v, C-o.
static void acquireKeys(void) {
	struct termios term;
	int error = tcgetattr(STDOUT_FILENO, &term);
	if (error) err(EX_OSERR, "tcgetattr");
	term.c_iflag &= ~IXON;
	term.c_cc[VINTR] = _POSIX_VDISABLE;
	term.c_cc[VSUSP] = _POSIX_VDISABLE;
#ifdef VDSUSP
	term.c_cc[VDSUSP] = _POSIX_VDISABLE;
#endif
	term.c_cc[VLNEXT] = _POSIX_VDISABLE;
	term.c_cc[VDISCARD] = _POSIX_VDISABLE;
	error = tcsetattr(STDOUT_FILENO, TCSADRAIN, &term);
	if (error) err(EX_OSERR, "tcsetattr");
}

// XXX: Assuming terminals will be fine with these even if they're unsupported,
// since they're "private" modes.
static const char *EnterFocusMode = "\33[?1004h";
static const char *ExitFocusMode  = "\33[?1004l";
static const char *EnterPasteMode = "\33[?2004h";
static const char *ExitPasteMode  = "\33[?2004l";

static void errExit(void) {
	putp(ExitFocusMode);
	putp(ExitPasteMode);
	reset_shell_mode();
}

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

#define X(id, seq, alt) define_key(seq, id); if (alt) define_key(alt, id);
	ENUM_KEY
#undef X

	status = newwin(StatusLines, COLS, 0, 0);
	if (!status) err(EX_OSERR, "newwin");

	marker = newwin(
		MarkerLines, COLS,
		LINES - InputLines - SplitLines - MarkerLines, 0
	);
	wbkgd(marker, ACS_BULLET);

	input = newpad(InputLines, InputCols);
	if (!input) err(EX_OSERR, "newpad");
	keypad(input, true);
	nodelay(input, true);

	windowFor(Network);
	uiShow();
}

static bool hidden = true;
static bool waiting;

static char title[256];
static char prevTitle[sizeof(title)];

void uiDraw(void) {
	if (hidden) return;
	wnoutrefresh(status);
	const struct Window *window = windows.ptrs[windows.show];
	if (!window->scroll) {
		pnoutrefresh(
			window->pad,
			WindowLines - MAIN_LINES, 0,
			StatusLines, 0,
			BOTTOM - InputLines, RIGHT
		);
	} else {
		pnoutrefresh(
			window->pad,
			WindowLines - window->scroll - MAIN_LINES + MarkerLines, 0,
			StatusLines, 0,
			BOTTOM - InputLines - SplitLines - MarkerLines, RIGHT
		);
		touchwin(marker);
		wnoutrefresh(marker);
		pnoutrefresh(
			window->pad,
			WindowLines - SplitLines, 0,
			LINES - InputLines - SplitLines, 0,
			BOTTOM - InputLines, RIGHT
		);
	}
	int y, x;
	getyx(input, y, x);
	pnoutrefresh(
		input,
		0, (x + 1 > RIGHT ? x + 1 - RIGHT : 0),
		LINES - InputLines, 0,
		BOTTOM, RIGHT
	);
	(void)y;
	doupdate();

	if (!to_status_line) return;
	if (!strcmp(title, prevTitle)) return;
	strcpy(prevTitle, title);
	putp(to_status_line);
	putp(title);
	putp(from_status_line);
	fflush(stdout);
}

struct Style {
	attr_t attr;
	enum Color fg, bg;
};
static const struct Style Reset = { A_NORMAL, Default, Default };

static const short Colors[ColorCap] = {
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

static size_t styleParse(struct Style *style, const char **str) {
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
	return strcspn(*str, (const char[]) { B, C, O, R, I, U, '\0' });
}

static void statusAdd(const char *str) {
	struct Style style = Reset;
	while (*str) {
		size_t len = styleParse(&style, &str);
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
	struct {
		uint unread;
		enum Heat heat;
	} others = { 0, Cold };

	wmove(status, 0, 0);
	for (uint num = 0; num < windows.len; ++num) {
		const struct Window *window = windows.ptrs[num];
		if (num != windows.show) {
			if (window->heat < Warm) continue;
			if (window->mute && window->heat < Hot) continue;
		}
		if (num != windows.show) {
			others.unread += window->unreadWarm;
			if (window->heat > others.heat) others.heat = window->heat;
		}
		char buf[256] = "";
		catf(
			buf, sizeof(buf), "\3%d%s %u ",
			idColors[window->id], (num == windows.show ? "\26" : ""), num
		);
		if (!window->ignore || window->mute) {
			catf(
				buf, sizeof(buf), "%s%s ",
				&"-"[window->ignore], &"="[!window->mute]
			);
		}
		catf(buf, sizeof(buf), "%s ", idNames[window->id]);
		if (window->mark && window->unreadWarm) {
			catf(
				buf, sizeof(buf), "\3%d+%d\3%d%s",
				(window->heat > Warm ? White : idColors[window->id]),
				window->unreadWarm, idColors[window->id],
				(window->scroll ? "" : " ")
			);
		}
		if (window->scroll) {
			catf(buf, sizeof(buf), "~%d ", window->scroll);
		}
		statusAdd(buf);
	}
	wclrtoeol(status);

	const struct Window *window = windows.ptrs[windows.show];
	snprintf(title, sizeof(title), "%s %s", network.name, idNames[window->id]);
	if (window->mark && window->unreadWarm) {
		catf(
			title, sizeof(title), " +%d%s",
			window->unreadWarm, &"!"[window->heat < Hot]
		);
	}
	if (others.unread) {
		catf(
			title, sizeof(title), " (+%d%s)",
			others.unread, &"!"[others.heat < Hot]
		);
	}
}

static void mark(struct Window *window) {
	if (window->scroll) return;
	window->mark = true;
	window->unreadHard = 0;
	window->unreadWarm = 0;
}

static void unmark(struct Window *window) {
	if (!window->scroll) {
		window->mark = false;
		window->heat = Cold;
	}
	statusUpdate();
}

void uiShow(void) {
	if (!hidden) return;
	prevTitle[0] = '\0';
	putp(EnterFocusMode);
	putp(EnterPasteMode);
	fflush(stdout);
	hidden = false;
	unmark(windows.ptrs[windows.show]);
}

void uiHide(void) {
	if (hidden) return;
	mark(windows.ptrs[windows.show]);
	hidden = true;
	putp(ExitFocusMode);
	putp(ExitPasteMode);
	endwin();
}

static void windowScroll(struct Window *window, int n) {
	mark(window);
	window->scroll += n;
	if (window->scroll > WindowLines - MAIN_LINES) {
		window->scroll = WindowLines - MAIN_LINES;
	}
	if (window->scroll < 0) window->scroll = 0;
	unmark(window);
}

static void windowScrollPage(struct Window *window, int n) {
	windowScroll(window, n * (MAIN_LINES - SplitLines - MarkerLines - 1));
}

static void windowScrollUnread(struct Window *window) {
	window->scroll = 0;
	windowScroll(window, window->unreadSoft - MAIN_LINES);
}

static int wordWidth(const char *str) {
	size_t len = strcspn(str, " \t");
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

// XXX: ncurses likes to render zero-width characters as spaces...
static int waddnstrnzw(WINDOW *win, const char *str, int len) {
	wchar_t wc;
	while (len) {
		int n = mbtowc(&wc, str, len);
		if (n < 1) return waddnstr(win, str, len);
		if (wcwidth(wc)) waddnstr(win, str, n);
		str += n;
		len -= n;
	}
	return OK;
}

static int wordWrap(WINDOW *win, const char *str) {
	int y, x, width;
	getmaxyx(win, y, width);
	waddch(win, '\n');

	int lines = 1;
	int align = 0;
	struct Style style = Reset;
	while (*str) {
		char ch = *str;
		if (ch == ' ' || ch == '\t') {
			getyx(win, y, x);
			const char *word = &str[strspn(str, " \t")];
			if (width - x - 1 <= wordWidth(word)) {
				lines += 1 + (align + wordWidth(word)) / width;
				waddch(win, '\n');
				getyx(win, y, x);
				wmove(win, y, align);
				str = word;
			} else {
				waddch(win, (align ? ch : ' '));
				str++;
			}
		}
		if (ch == '\t' && !align) {
			getyx(win, y, align);
		}

		size_t len = styleParse(&style, &str);
		size_t ws = strcspn(str, " \t");
		if (ws < len) len = ws;

		wattr_set(
			win,
			style.attr | colorAttr(Colors[style.fg]),
			colorPair(Colors[style.fg], Colors[style.bg]),
			NULL
		);
		waddnstrnzw(win, str, len);
		str += len;
	}
	return lines;
}

struct Util uiNotifyUtil;
static void notify(uint id, const char *str) {
	if (!uiNotifyUtil.argc) return;

	struct Util util = uiNotifyUtil;
	utilPush(&util, idNames[id]);
	char buf[1024] = "";
	while (*str) {
		struct Style style = Reset;
		size_t len = styleParse(&style, &str);
		catf(buf, sizeof(buf), "%.*s", (int)len, str);
		str += len;
	}
	utilPush(&util, buf);

	pid_t pid = fork();
	if (pid < 0) err(EX_OSERR, "fork");
	if (pid) return;

	close(STDIN_FILENO);
	dup2(utilPipe[1], STDOUT_FILENO);
	dup2(utilPipe[1], STDERR_FILENO);
	execvp(util.argv[0], (char *const *)util.argv);
	warn("%s", util.argv[0]);
	_exit(EX_CONFIG);
}

void uiWrite(uint id, enum Heat heat, const time_t *src, const char *str) {
	struct Window *window = windows.ptrs[windowFor(id)];
	time_t ts = (src ? *src : time(NULL));
	bufferPush(&window->buffer, heat, ts, str);
	if (heat < Cold && window->ignore) return;

	int lines = 0;
	if (!window->unreadHard++) window->unreadSoft = 0;
	if (window->mark && heat > Cold) {
		if (!window->unreadWarm++) {
			lines++;
			waddch(window->pad, '\n');
		}
		if (heat > window->heat) window->heat = heat;
		statusUpdate();
	}

	lines += wordWrap(window->pad, str);
	window->unreadSoft += lines;
	if (window->scroll) windowScroll(window, lines);

	if (window->mark && heat > Warm) {
		beep();
		notify(id, str);
	}
}

void uiFormat(
	uint id, enum Heat heat, const time_t *time, const char *format, ...
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
	wmove(window->pad, 0, 0);

	int flowed = 0;
	window->unreadSoft = 0;
	for (size_t i = 0; i < BufferCap; ++i) {
		struct Line line = bufferLine(&window->buffer, i);
		if (!line.str) continue;
		if (line.heat < Cold && window->ignore) continue;
		int lines = 0;
		if (i == (size_t)(BufferCap - window->unreadHard)) {
			waddch(window->pad, '\n');
			lines++;
		}
		lines += wordWrap(window->pad, line.str);
		if (i >= (size_t)(BufferCap - window->unreadHard)) {
			window->unreadSoft += lines;
		}
		flowed += lines;
	}

	if (flowed < WindowLines) {
		wscrl(window->pad, -(WindowLines - 1 - flowed));
		wmove(window->pad, WindowLines - 1, RIGHT);
	}
}

static void resize(void) {
	mvwin(marker, LINES - InputLines - SplitLines - MarkerLines, 0);
	int height, width;
	getmaxyx(windows.ptrs[0]->pad, height, width);
	if (width == COLS) return;
	for (uint num = 0; num < windows.len; ++num) {
		wresize(windows.ptrs[num]->pad, BufferCap, COLS);
		reflow(windows.ptrs[num]);
	}
	(void)height;
	statusUpdate();
}

static void bufferList(const struct Buffer *buffer) {
	uiHide();
	waiting = true;

	for (size_t i = 0; i < BufferCap; ++i) {
		struct Line line = bufferLine(buffer, i);
		if (!line.str) continue;

		struct tm *tm = localtime(&line.time);
		if (!tm) err(EX_OSERR, "localtime");

		char buf[sizeof("00:00:00")];
		strftime(buf, sizeof(buf), "%T", tm);
		vid_attr(colorAttr(Colors[Gray]), colorPair(Colors[Gray], -1), NULL);
		printf("[%s] ", buf);

		bool align = false;
		struct Style style = Reset;
		while (*line.str) {
			if (*line.str == '\t') {
				printf("%c", (align ? '\t' : ' '));
				align = true;
				line.str++;
			}

			size_t len = styleParse(&style, (const char **)&line.str);
			size_t tab = strcspn(line.str, "\t");
			if (tab < len) len = tab;

			vid_attr(
				style.attr | colorAttr(Colors[style.fg]),
				colorPair(Colors[style.fg], Colors[style.bg]),
				NULL
			);
			printf("%.*s", (int)len, line.str);
			line.str += len;
		}
		printf("\n");
	}
}

static void inputAdd(struct Style *style, const char *str) {
	while (*str) {
		const char *code = str;
		size_t len = styleParse(style, &str);
		wattr_set(input, A_BOLD | A_REVERSE, 0, NULL);
		switch (*code) {
			break; case B: waddch(input, 'B');
			break; case C: waddch(input, 'C');
			break; case O: waddch(input, 'O');
			break; case R: waddch(input, 'R');
			break; case I: waddch(input, 'I');
			break; case U: waddch(input, 'U');
			break; case '\n': waddch(input, 'N');
		}
		if (str - code > 1) waddnstr(input, &code[1], str - &code[1]);
		if (str[0] == '\n') {
			str++;
			len--;
		}
		size_t nl = strcspn(str, "\n");
		if (nl < len) len = nl;
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
	size_t pos;
	char *buf = editBuffer(&pos);
	uint id = windows.ptrs[windows.show]->id;

	const char *prefix = "";
	const char *prompt = self.nick;
	const char *suffix = "";
	const char *skip = buf;
	struct Style stylePrompt = { .fg = self.color, .bg = Default };
	struct Style styleInput = Reset;

	const char *privmsg = commandIsPrivmsg(id, buf);
	const char *notice = commandIsNotice(id, buf);
	const char *action = commandIsAction(id, buf);
	if (privmsg) {
		prefix = "<"; suffix = "> ";
		skip = privmsg;
	} else if (notice) {
		prefix = "-"; suffix = "- ";
		styleInput.fg = LightGray;
		skip = notice;
	} else if (action) {
		prefix = "* "; suffix = " ";
		stylePrompt.attr |= A_ITALIC;
		styleInput.attr |= A_ITALIC;
		skip = action;
	} else if (id == Debug && buf[0] != '/') {
		prompt = "<< ";
		stylePrompt.fg = Gray;
	} else {
		prompt = "";
	}
	if (skip > &buf[pos]) {
		prefix = prompt = suffix = "";
		skip = buf;
	}

	int y, x;
	wmove(input, 0, 0);
	wattr_set(
		input,
		stylePrompt.attr | colorAttr(Colors[stylePrompt.fg]),
		colorPair(Colors[stylePrompt.fg], Colors[stylePrompt.bg]),
		NULL
	);
	waddstr(input, prefix);
	waddstr(input, prompt);
	waddstr(input, suffix);
	struct Style style = styleInput;
	char p = buf[pos];
	buf[pos] = '\0';
	inputAdd(&style, skip);
	getyx(input, y, x);
	buf[pos] = p;
	inputAdd(&style, &buf[pos]);
	wclrtoeol(input);
	wmove(input, y, x);
}

static void windowShow(uint num) {
	touchwin(windows.ptrs[num]->pad);
	windows.swap = windows.show;
	windows.show = num;
	windows.user = num;
	mark(windows.ptrs[windows.swap]);
	unmark(windows.ptrs[windows.show]);
	inputUpdate();
}

void uiShowID(uint id) {
	windowShow(windowFor(id));
}

void uiShowNum(uint num) {
	if (num < windows.len) windowShow(num);
}

void uiMoveID(uint id, uint num) {
	struct Window *window = windowRemove(windowFor(id));
	if (num < windows.len) {
		windowShow(windowInsert(num, window));
	} else {
		windowShow(windowPush(window));
	}
}

static void windowClose(uint num) {
	if (windows.ptrs[num]->id == Network) return;
	struct Window *window = windowRemove(num);
	completeClear(window->id);
	windowFree(window);
	if (windows.swap >= num) windows.swap--;
	if (windows.show == num) {
		windowShow(windows.swap);
		windows.swap = windows.show;
	} else if (windows.show > num) {
		windows.show--;
	}
	statusUpdate();
}

void uiCloseID(uint id) {
	windowClose(windowFor(id));
}

void uiCloseNum(uint num) {
	if (num < windows.len) windowClose(num);
}

static void toggleIgnore(struct Window *window) {
	window->ignore ^= true;
	reflow(window);
	statusUpdate();
}

static void showAuto(void) {
	uint minHot = UINT_MAX, numHot;
	uint minWarm = UINT_MAX, numWarm;
	for (uint num = 0; num < windows.len; ++num) {
		struct Window *window = windows.ptrs[num];
		if (window->heat >= Hot) {
			if (window->unreadWarm >= minHot) continue;
			minHot = window->unreadWarm;
			numHot = num;
		}
		if (window->heat >= Warm && !window->mute) {
			if (window->unreadWarm >= minWarm) continue;
			minWarm = window->unreadWarm;
			numWarm = num;
		}
	}
	uint user = windows.user;
	if (minHot < UINT_MAX) {
		windowShow(numHot);
		windows.user = user;
	} else if (minWarm < UINT_MAX) {
		windowShow(numWarm);
		windows.user = user;
	} else if (user != windows.show) {
		windowShow(user);
	}
}

static void keyCode(int code) {
	struct Window *window = windows.ptrs[windows.show];
	uint id = window->id;
	switch (code) {
		break; case KEY_RESIZE:  resize();
		break; case KeyFocusIn:  unmark(window);
		break; case KeyFocusOut: mark(window);

		break; case KeyMetaEnter: edit(id, EditInsert, L'\n');
		break; case KeyMetaEqual: window->mute ^= true; statusUpdate();
		break; case KeyMetaMinus: toggleIgnore(window);
		break; case KeyMetaSlash: windowShow(windows.swap);

		break; case KeyMetaGt: windowScroll(window, -WindowLines);
		break; case KeyMetaLt: windowScroll(window, +WindowLines);

		break; case KeyMeta0 ... KeyMeta9: uiShowNum(code - KeyMeta0);
		break; case KeyMetaA: showAuto();
		break; case KeyMetaB: edit(id, EditPrevWord, 0);
		break; case KeyMetaD: edit(id, EditDeleteNextWord, 0);
		break; case KeyMetaF: edit(id, EditNextWord, 0);
		break; case KeyMetaL: bufferList(&window->buffer);
		break; case KeyMetaM: waddch(window->pad, '\n');
		break; case KeyMetaQ: edit(id, EditCollapse, 0);
		break; case KeyMetaU: windowScrollUnread(window);
		break; case KeyMetaV: windowScrollPage(window, +1);

		break; case KEY_BACKSPACE: edit(id, EditDeletePrev, 0);
		break; case KEY_DC: edit(id, EditDeleteNext, 0);
		break; case KEY_DOWN: windowScroll(window, -1);
		break; case KEY_END: edit(id, EditTail, 0);
		break; case KEY_ENTER: edit(id, EditEnter, 0);
		break; case KEY_HOME: edit(id, EditHead, 0);
		break; case KEY_LEFT: edit(id, EditPrev, 0);
		break; case KEY_NPAGE: windowScrollPage(window, -1);
		break; case KEY_PPAGE: windowScrollPage(window, +1);
		break; case KEY_RIGHT: edit(id, EditNext, 0);
		break; case KEY_SEND: windowScroll(window, -WindowLines);
		break; case KEY_SHOME: windowScroll(window, +WindowLines);
		break; case KEY_UP: windowScroll(window, +1);
	}
}

static void keyCtrl(wchar_t ch) {
	struct Window *window = windows.ptrs[windows.show];
	uint id = window->id;
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
		break; case L'N': uiShowNum(windows.show + 1);
		break; case L'P': uiShowNum(windows.show - 1);
		break; case L'T': edit(id, EditTranspose, 0);
		break; case L'U': edit(id, EditDeleteHead, 0);
		break; case L'V': windowScrollPage(window, -1);
		break; case L'W': edit(id, EditDeletePrevWord, 0);
		break; case L'X': edit(id, EditExpand, 0);
		break; case L'Y': edit(id, EditPaste, 0);
	}
}

static void keyStyle(wchar_t ch) {
	uint id = windows.ptrs[windows.show]->id;
	switch (iswcntrl(ch) ? ch ^ L'@' : (wchar_t)towupper(ch)) {
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

	wint_t ch;
	static bool paste, style;
	for (int ret; ERR != (ret = wget_wch(input, &ch));) {
		if (ret == KEY_CODE_YES && ch == KeyPasteOn) {
			paste = true;
		} else if (ret == KEY_CODE_YES && ch == KeyPasteOff) {
			paste = false;
		} else if (paste) {
			edit(windows.ptrs[windows.show]->id, EditInsert, ch);
		} else if (ret == KEY_CODE_YES) {
			keyCode(ch);
		} else if (ch == (L'Z' ^ L'@')) {
			style = true;
			continue;
		} else if (style) {
			keyStyle(ch);
		} else if (iswcntrl(ch)) {
			keyCtrl(ch);
		} else {
			edit(windows.ptrs[windows.show]->id, EditInsert, ch);
		}
		style = false;
	}
	inputUpdate();
}

static const time_t Signatures[] = {
	0x6C72696774616301, // no heat, unread, unreadWarm
	0x6C72696774616302, // no self.pos
	0x6C72696774616303, // no buffer line heat
	0x6C72696774616304, // no mute
	0x6C72696774616305,
};

static size_t signatureVersion(time_t signature) {
	for (size_t i = 0; i < ARRAY_LEN(Signatures); ++i) {
		if (signature == Signatures[i]) return i;
	}
	err(EX_DATAERR, "unknown file signature %jX", (uintmax_t)signature);
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

	if (writeTime(file, Signatures[4])) return -1;
	if (writeTime(file, self.pos)) return -1;
	for (uint num = 0; num < windows.len; ++num) {
		const struct Window *window = windows.ptrs[num];
		if (writeString(file, idNames[window->id])) return -1;
		if (writeTime(file, window->mute)) return -1;
		if (writeTime(file, window->heat)) return -1;
		if (writeTime(file, window->unreadHard)) return -1;
		if (writeTime(file, window->unreadWarm)) return -1;
		for (size_t i = 0; i < BufferCap; ++i) {
			struct Line line = bufferLine(&window->buffer, i);
			if (!line.str) continue;
			if (writeTime(file, line.time)) return -1;
			if (writeTime(file, line.heat)) return -1;
			if (writeString(file, line.str)) return -1;
		}
		if (writeTime(file, 0)) return -1;
	}
	return fclose(file);
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

	time_t signature;
	fread(&signature, sizeof(signature), 1, file);
	if (ferror(file)) err(EX_IOERR, "fread");
	if (feof(file)) {
		fclose(file);
		return;
	}
	size_t version = signatureVersion(signature);

	if (version > 1) {
		self.pos = readTime(file);
	}

	char *buf = NULL;
	size_t cap = 0;
	while (0 < readString(file, &buf, &cap)) {
		struct Window *window = windows.ptrs[windowFor(idFor(buf))];
		if (version > 3) window->mute = readTime(file);
		if (version > 0) {
			window->heat = readTime(file);
			window->unreadHard = readTime(file);
			window->unreadWarm = readTime(file);
		}
		for (;;) {
			time_t time = readTime(file);
			if (!time) break;
			enum Heat heat = (version > 2 ? readTime(file) : Cold);
			readString(file, &buf, &cap);
			bufferPush(&window->buffer, heat, time, buf);
		}
		reflow(window);
	}

	free(buf);
	fclose(file);
}
