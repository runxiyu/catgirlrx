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

struct Window {
	struct Tag tag;
	WINDOW *log;
	int scroll;
	bool hot, mark;
	uint unread;
	struct Window *prev;
	struct Window *next;
};

static struct {
	bool hide;
	WINDOW *status;
	WINDOW *input;
	struct Window *window;
} ui;

void uiShow(void) {
	ui.hide = false;
	termMode(TermFocus, true);
	uiDraw();
}

void uiHide(void) {
	ui.hide = true;
	termMode(TermFocus, false);
	endwin();
}

void uiInit(void) {
	setlocale(LC_CTYPE, "");
	initscr();
	cbreak();
	noecho();

	colorInit();
	termInit();

	ui.status = newwin(1, COLS, 0, 0);
	ui.input = newpad(1, 512);
	keypad(ui.input, true);
	nodelay(ui.input, true);

	uiWindowTag(TagStatus);
	uiShow();
}

void uiExit(void) {
	uiHide();
	printf(
		"This program is AGPLv3 Free Software!\n"
		"The source is available at <" SOURCE_URL ">.\n"
	);
}

static int lastLine(void) {
	return LINES - 1;
}
static int lastCol(void) {
	return COLS - 1;
}
static int logHeight(void) {
	return LINES - 2;
}

static int _;

void uiDraw(void) {
	if (ui.hide) return;
	wnoutrefresh(ui.status);
	pnoutrefresh(
		ui.window->log,
		ui.window->scroll - logHeight(), 0,
		1, 0,
		lastLine() - 1, lastCol()
	);
	int x;
	getyx(ui.input, _, x);
	pnoutrefresh(
		ui.input,
		0, MAX(0, x - lastCol() + 3),
		lastLine(), 0,
		lastLine(), lastCol()
	);
	doupdate();
}

static const short Colors[] = {
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

static void addFormat(WINDOW *win, const struct Format *format) {
	attr_t attr = A_NORMAL;
	if (format->bold)      attr |= A_BOLD;
	if (format->italic)    attr |= A_ITALIC;
	if (format->underline) attr |= A_UNDERLINE;
	if (format->reverse)   attr |= A_REVERSE;

	short pair = -1;
	if (format->fg != IRCDefault) pair = Colors[format->fg];
	if (format->bg != IRCDefault) pair |= Colors[format->bg] << 4;

	wattr_set(win, attr | attr8(pair), 1 + pair8(pair), NULL);
	waddnwstr(win, format->str, format->len);
}

static int printWidth(const wchar_t *str, size_t len) {
	int width = 0;
	for (size_t i = 0; i < len; ++i) {
		if (iswprint(str[i])) width += wcwidth(str[i]);
	}
	return width;
}

static int addWrap(WINDOW *win, const wchar_t *str) {
	int lines = 0;

	struct Format format = { .str = str };
	formatReset(&format);
	while (formatParse(&format, NULL)) {
		size_t word = 1 + wcscspn(&format.str[1], L" ");
		if (word < format.len) format.len = word;

		int x, xMax;
		getyx(win, _, x);
		getmaxyx(win, _, xMax);
		if (xMax - x - 1 < printWidth(format.str, word)) {
			if (format.str[0] == L' ') {
				format.str++;
				format.len--;
			}
			waddch(win, '\n');
			lines++;
		}

		addFormat(win, &format);
	}
	return lines;
}

static struct {
	struct Window *head;
	struct Window *tail;
	struct Window *tag[TagsLen];
} windows;

static void uiTitle(const struct Window *win) {
	int unread;
	char *str;
	int len = asprintf(
		&str, "%s%n (%u)", win->tag.name, &unread, win->unread
	);
	if (len < 0) err(EX_OSERR, "asprintf");
	if (!win->unread) str[unread] = '\0';
	termTitle(str);
	free(str);
}

static void uiStatus(void) {
	wmove(ui.status, 0, 0);
	int num = 0;
	for (const struct Window *win = windows.head; win; win = win->next, ++num) {
		if (!win->unread && ui.window != win) continue;
		if (ui.window == win) uiTitle(win);
		int unread;
		wchar_t *str;
		int len = aswprintf(
			&str, L"%c %d %s %n(\3%02d%u\3) ",
			(ui.window == win ? IRCReverse : IRCReset),
			num, win->tag.name,
			&unread, (win->hot ? IRCYellow : IRCDefault), win->unread
		);
		if (len < 0) err(EX_OSERR, "aswprintf");
		if (!win->unread) str[unread] = L'\0';
		addWrap(ui.status, str);
		free(str);
	}
	// TODO: Put window's topic in the rest of the status area.
	wclrtoeol(ui.status);
}

static void windowAppend(struct Window *win) {
	if (windows.tail) windows.tail->next = win;
	win->prev = windows.tail;
	win->next = NULL;
	windows.tail = win;
	if (!windows.head) windows.head = win;
	windows.tag[win->tag.id] = win;
}

static void windowRemove(struct Window *win) {
	if (win->prev) win->prev->next = win->next;
	if (win->next) win->next->prev = win->prev;
	if (windows.head == win) windows.head = win->next;
	if (windows.tail == win) windows.tail = win->prev;
	windows.tag[win->tag.id] = NULL;
}

static const int LogLines = 512;

static struct Window *windowTag(struct Tag tag) {
	struct Window *win = windows.tag[tag.id];
	if (win) return win;

	win = calloc(1, sizeof(*win));
	if (!win) err(EX_OSERR, "calloc");

	win->tag = tag;
	win->mark = true;
	win->scroll = LogLines;
	win->log = newpad(LogLines, COLS);
	wsetscrreg(win->log, 0, LogLines - 1);
	scrollok(win->log, true);
	wmove(win->log, LogLines - 1, 0);

	windowAppend(win);
	return win;
}

static void windowClose(struct Window *win) {
	windowRemove(win);
	delwin(win->log);
	free(win);
}

static void uiResize(void) {
	wresize(ui.status, 1, COLS);
	for (struct Window *win = windows.head; win; win = win->next) {
		wresize(win->log, LogLines, COLS);
		wmove(win->log, LogLines - 1, lastCol());
	}
}

static void windowUnmark(struct Window *win) {
	win->mark = false;
	win->unread = 0;
	win->hot = false;
	uiStatus();
}

static void uiWindow(struct Window *win) {
	touchwin(win->log);
	if (ui.window) ui.window->mark = true;
	windowUnmark(win);
	ui.window = win;
	uiStatus();
	uiPrompt();
}

void uiWindowTag(struct Tag tag) {
	uiWindow(windowTag(tag));
}

void uiWindowNum(int num) {
	if (num < 0) {
		for (struct Window *win = windows.tail; win; win = win->prev) {
			if (++num) continue;
			uiWindow(win);
			break;
		}
	} else {
		for (struct Window *win = windows.head; win; win = win->next) {
			if (num--) continue;
			uiWindow(win);
			break;
		}
	}
}

void uiCloseTag(struct Tag tag) {
	struct Window *win = windowTag(tag);
	if (ui.window == win) {
		if (win->next) {
			uiWindow(win->next);
		} else if (win->prev) {
			uiWindow(win->prev);
		} else {
			return;
		}
	}
	windowClose(win);
}

static void notify(struct Tag tag, const wchar_t *line) {
	beep();
	if (!self.notify) return;

	char buf[256];
	size_t cap = sizeof(buf);

	struct Format format = { .str = line };
	formatReset(&format);
	while (formatParse(&format, NULL)) {
		int len = snprintf(
			&buf[sizeof(buf) - cap], cap,
			"%.*ls", (int)format.len, format.str
		);
		if (len < 0) err(EX_OSERR, "snprintf");
		if ((size_t)len >= cap) break;
		cap -= len;
	}

	eventPipe((const char *[]) { "notify-send", tag.name, buf, NULL });
}

void uiLog(struct Tag tag, enum UIHeat heat, const wchar_t *line) {
	struct Window *win = windowTag(tag);
	int lines = 1;
	waddch(win->log, '\n');

	if (win->mark && heat > UICold) {
		if (!win->unread++) {
			lines++;
			waddch(win->log, '\n');
		}
		if (heat > UIWarm) {
			win->hot = true;
			notify(tag, line);
		}
		uiStatus();
	}

	lines += addWrap(win->log, line);
	if (win->scroll != LogLines) win->scroll -= lines;
}

void uiFmt(struct Tag tag, enum UIHeat heat, const wchar_t *format, ...) {
	wchar_t *str;
	va_list ap;
	va_start(ap, format);
	vaswprintf(&str, format, ap);
	va_end(ap);
	if (!str) err(EX_OSERR, "vaswprintf");
	uiLog(tag, heat, str);
	free(str);
}

static void scrollUp(int lines) {
	if (ui.window->scroll == logHeight()) return;
	if (ui.window->scroll == LogLines) ui.window->mark = true;
	ui.window->scroll = MAX(ui.window->scroll - lines, logHeight());
}
static void scrollDown(int lines) {
	if (ui.window->scroll == LogLines) return;
	ui.window->scroll = MIN(ui.window->scroll + lines, LogLines);
	if (ui.window->scroll == LogLines) windowUnmark(ui.window);
}

static void keyCode(wchar_t ch) {
	switch (ch) {
		break; case KEY_RESIZE:    uiResize();
		break; case KEY_SLEFT:     scrollUp(1);
		break; case KEY_SRIGHT:    scrollDown(1);
		break; case KEY_PPAGE:     scrollUp(logHeight() / 2);
		break; case KEY_NPAGE:     scrollDown(logHeight() / 2);
		break; case KEY_LEFT:      edit(ui.window->tag, EditLeft, 0);
		break; case KEY_RIGHT:     edit(ui.window->tag, EditRight, 0);
		break; case KEY_HOME:      edit(ui.window->tag, EditHome, 0);
		break; case KEY_END:       edit(ui.window->tag, EditEnd, 0);
		break; case KEY_DC:        edit(ui.window->tag, EditDelete, 0);
		break; case KEY_BACKSPACE: edit(ui.window->tag, EditBackspace, 0);
		break; case KEY_ENTER:     edit(ui.window->tag, EditEnter, 0);
	}
}

#define CTRL(ch) ((ch) ^ 0100)

static void keyChar(wchar_t ch) {
	if (ch < 0200) {
		enum TermEvent event = termEvent((char)ch);
		switch (event) {
			break; case TermFocusIn:  windowUnmark(ui.window);
			break; case TermFocusOut: ui.window->mark = true;
			break; default: {}
		}
		if (event) return;
	}

	static bool meta;
	if (ch == L'\33') {
		meta = true;
		return;
	}

	if (ch == L'\177') ch = L'\b';

	if (meta) {
		meta = false;
		switch (ch) {
			break; case L'b':  edit(ui.window->tag, EditBackWord, 0);
			break; case L'f':  edit(ui.window->tag, EditForeWord, 0);
			break; case L'\b': edit(ui.window->tag, EditKillBackWord, 0);
			break; case L'd':  edit(ui.window->tag, EditKillForeWord, 0);
			break; case L'm':  uiLog(ui.window->tag, UICold, L"");
			break; default: {
				if (ch >= L'0' && ch <= L'9') uiWindowNum(ch - L'0');
			}
		}
		return;
	}

	switch (ch) {
		break; case CTRL(L'L'): clearok(curscr, true);

		break; case CTRL(L'A'): edit(ui.window->tag, EditHome, 0);
		break; case CTRL(L'B'): edit(ui.window->tag, EditLeft, 0);
		break; case CTRL(L'D'): edit(ui.window->tag, EditDelete, 0);
		break; case CTRL(L'E'): edit(ui.window->tag, EditEnd, 0);
		break; case CTRL(L'F'): edit(ui.window->tag, EditRight, 0);
		break; case CTRL(L'K'): edit(ui.window->tag, EditKillLine, 0);
		break; case CTRL(L'W'): edit(ui.window->tag, EditKillBackWord, 0);

		break; case CTRL(L'C'): edit(ui.window->tag, EditInsert, IRCColor);
		break; case CTRL(L'N'): edit(ui.window->tag, EditInsert, IRCReset);
		break; case CTRL(L'O'): edit(ui.window->tag, EditInsert, IRCBold);
		break; case CTRL(L'R'): edit(ui.window->tag, EditInsert, IRCColor);
		break; case CTRL(L'T'): edit(ui.window->tag, EditInsert, IRCItalic);
		break; case CTRL(L'U'): edit(ui.window->tag, EditInsert, IRCUnderline);
		break; case CTRL(L'V'): edit(ui.window->tag, EditInsert, IRCReverse);

		break; case L'\b': edit(ui.window->tag, EditBackspace, 0);
		break; case L'\t': edit(ui.window->tag, EditComplete, 0);
		break; case L'\n': edit(ui.window->tag, EditEnter, 0);

		break; default: {
			if (iswprint(ch)) edit(ui.window->tag, EditInsert, ch);
		}
	}
}

static bool isAction(struct Tag tag, const wchar_t *input) {
	if (tag.id == TagStatus.id || tag.id == TagRaw.id) return false;
	return !wcsncasecmp(input, L"/me ", 4);
}

// FIXME: This duplicates logic from input.c for wcs.
static bool isCommand(struct Tag tag, const wchar_t *input) {
	if (tag.id == TagStatus.id || tag.id == TagRaw.id) return true;
	if (input[0] != L'/') return false;
	const wchar_t *space = wcschr(&input[1], L' ');
	const wchar_t *extra = wcschr(&input[1], L'/');
	return !extra || (space && extra > space);
}

void uiPrompt(void) {
	const wchar_t *input = editHead();

	// TODO: Avoid reformatting these on every read.
	wchar_t *prompt = NULL;
	int len = 0;
	if (isAction(ui.window->tag, input) && editTail() >= &input[4]) {
		input = &input[4];
		len = aswprintf(
			&prompt, L"\3%d* %s\3 ",
			formatColor(self.user), self.nick
		);
	} else if (!isCommand(ui.window->tag, input)) {
		len = aswprintf(
			&prompt, L"\3%d<%s>\3 ",
			formatColor(self.user), self.nick
		);
	}
	if (len < 0) err(EX_OSERR, "aswprintf");

	wmove(ui.input, 0, 0);
	if (prompt) addWrap(ui.input, prompt);
	free(prompt);

	int x = 0;
	struct Format format = { .str = input };
	formatReset(&format);
	while (formatParse(&format, editTail())) {
		if (format.split) getyx(ui.input, _, x);
		addFormat(ui.input, &format);
	}

	wclrtoeol(ui.input);
	wmove(ui.input, 0, x);
}

void uiRead(void) {
	if (ui.hide) uiShow();
	int ret;
	wint_t ch;
	while (ERR != (ret = wget_wch(ui.input, &ch))) {
		if (ret == KEY_CODE_YES) {
			keyCode(ch);
		} else {
			keyChar(ch);
		}
	}
	uiPrompt();
}
