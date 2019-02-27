/* Copyright (C) 2018, 2019  C. McEnroe <june@causal.agency>
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
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <wchar.h>
#include <wctype.h>

#ifndef A_ITALIC
#define A_ITALIC A_UNDERLINE
#endif

#include "chat.h"
#undef uiFmt

#define CTRL(ch) ((ch) & 037)
enum { Esc = L'\33', Del = L'\177' };

static const int LogLines = 512;

static int lastLine(void) {
	return LINES - 1;
}
static int lastCol(void) {
	return COLS - 1;
}
static int logHeight(void) {
	return LINES - 2;
}

struct Window {
	struct Tag tag;
	WINDOW *log;
	bool hot;
	bool mark;
	int scroll;
	uint unread;
	struct Window *prev;
	struct Window *next;
};

static struct {
	struct Window *active;
	struct Window *other;
	struct Window *head;
	struct Window *tail;
	struct Window *tag[TagsLen];
} windows;

static void windowAppend(struct Window *win) {
	if (windows.tail) windows.tail->next = win;
	win->prev = windows.tail;
	win->next = NULL;
	windows.tail = win;
	if (!windows.head) windows.head = win;
	windows.tag[win->tag.id] = win;
}

static void windowRemove(struct Window *win) {
	windows.tag[win->tag.id] = NULL;
	if (win->prev) win->prev->next = win->next;
	if (win->next) win->next->prev = win->prev;
	if (windows.head == win) windows.head = win->next;
	if (windows.tail == win) windows.tail = win->prev;
}

static struct Window *windowFor(struct Tag tag) {
	struct Window *win = windows.tag[tag.id];
	if (win) {
		win->tag = tag;
		return win;
	}

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

static void windowResize(struct Window *win) {
	wresize(win->log, LogLines, COLS);
	wmove(win->log, LogLines - 1, lastCol());
}

static void windowMark(struct Window *win) {
	win->mark = true;
}
static void windowUnmark(struct Window *win) {
	win->mark = false;
	win->unread = 0;
	win->hot = false;
}

static void windowShow(struct Window *win) {
	if (windows.active) windowMark(windows.active);
	if (win) {
		touchwin(win->log);
		windowUnmark(win);
	}
	windows.other = windows.active;
	windows.active = win;
}

static void windowClose(struct Window *win) {
	if (windows.active == win) windowShow(win->next ? win->next : win->prev);
	if (windows.other == win) windows.other = NULL;
	windowRemove(win);
	delwin(win->log);
	free(win);
}

static void windowScroll(struct Window *win, int lines) {
	if (lines < 0) {
		if (win->scroll == logHeight()) return;
		if (win->scroll == LogLines) windowMark(win);
		win->scroll = MAX(win->scroll + lines, logHeight());
	} else {
		if (win->scroll == LogLines) return;
		win->scroll = MIN(win->scroll + lines, LogLines);
		if (win->scroll == LogLines) windowUnmark(win);
	}
}

static void colorInit(void) {
	start_color();
	use_default_colors();
	if (COLORS < 16) {
		for (short pair = 0; pair < 0100; ++pair) {
			if (pair < 010) {
				init_pair(1 + pair, pair, -1);
			} else {
				init_pair(1 + pair, pair & 007, (pair & 070) >> 3);
			}
		}
	} else {
		for (short pair = 0; pair < 0x100; ++pair) {
			if (pair < 0x10) {
				init_pair(1 + pair, pair, -1);
			} else {
				init_pair(1 + pair, pair & 0x0F, (pair & 0xF0) >> 4);
			}
		}
	}
}

static attr_t colorAttr(short color) {
	if (color < 0) return A_NORMAL;
	if (COLORS < 16 && (color & 0x08)) return A_BOLD;
	return A_NORMAL;
}
static short colorPair(short color) {
	if (color < 0) return 0;
	if (COLORS < 16) return 1 + ((color & 0x70) >> 1 | (color & 0x07));
	return 1 + color;
}

static struct {
	bool hide;
	WINDOW *status;
	WINDOW *input;
} ui;

void uiInit(void) {
	initscr();
	cbreak();
	noecho();
	termInit();
	termNoFlow();
	def_prog_mode();
	colorInit();
	ui.status = newwin(1, COLS, 0, 0);
	ui.input = newpad(1, 512);
	keypad(ui.input, true);
	nodelay(ui.input, true);
	uiShow();
}

static void uiResize(void) {
	wresize(ui.status, 1, COLS);
	for (struct Window *win = windows.head; win; win = win->next) {
		windowResize(win);
	}
}

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

void uiExit(int status) {
	uiHide();
	printf(
		"This program is AGPLv3 Free Software!\n"
		"Code is available from <" SOURCE_URL ">.\n"
	);
	exit(status);
}

static int _;
void uiDraw(void) {
	if (ui.hide) return;
	wnoutrefresh(ui.status);
	if (windows.active) {
		pnoutrefresh(
			windows.active->log,
			windows.active->scroll - logHeight(), 0,
			1, 0,
			lastLine() - 1, lastCol()
		);
	}
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

	short color = -1;
	if (format->fg != IRCDefault) color = Colors[format->fg];
	if (format->bg != IRCDefault) color |= Colors[format->bg] << 4;

	wattr_set(win, attr | colorAttr(color), colorPair(color), NULL);
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

static void title(const struct Window *win) {
	int unread;
	char *str;
	int len = asprintf(&str, "%s%n (%u)", win->tag.name, &unread, win->unread);
	if (len < 0) err(EX_OSERR, "asprintf");
	if (!win->unread) str[unread] = '\0';
	termTitle(str);
	free(str);
}

static void uiStatus(void) {
	wmove(ui.status, 0, 0);
	int num = 0;
	for (const struct Window *win = windows.head; win; win = win->next, ++num) {
		if (!win->unread && windows.active != win) continue;
		if (windows.active == win) title(win);
		int unread;
		wchar_t *str;
		int len = aswprintf(
			&str, L"%c\3%d %d %s %n(\3%02d%u\3%d) ",
			(windows.active == win ? IRCReverse : IRCReset), colorFor(win->tag),
			num, win->tag.name,
			&unread, (win->hot ? IRCWhite : colorFor(win->tag)), win->unread,
			colorFor(win->tag)
		);
		if (len < 0) err(EX_OSERR, "aswprintf");
		if (!win->unread) str[unread] = L'\0';
		addWrap(ui.status, str);
		free(str);
	}
	wclrtoeol(ui.status);
}

static void uiShowWindow(struct Window *win) {
	windowShow(win);
	uiStatus();
	uiPrompt(false);
}

void uiShowTag(struct Tag tag) {
	uiShowWindow(windowFor(tag));
}

void uiShowNum(int num, bool relative) {
	struct Window *win = (relative ? windows.active : windows.head);
	if (num < 0) {
		for (; win; win = win->prev) if (!num++) break;
	} else {
		for (; win; win = win->next) if (!num--) break;
	}
	if (win) uiShowWindow(win);
}

static void uiShowAuto(void) {
	struct Window *unread = NULL;
	struct Window *hot;
	for (hot = windows.head; hot; hot = hot->next) {
		if (hot->hot) break;
		if (!unread && hot->unread) unread = hot;
	}
	if (!hot && !unread) return;
	uiShowWindow(hot ? hot : unread);
}

void uiCloseTag(struct Tag tag) {
	windowClose(windowFor(tag));
	uiStatus();
	uiPrompt(false);
}

static void notify(struct Tag tag, const wchar_t *str) {
	beep();
	if (!self.notify) return;

	size_t len = 0;
	char buf[256];
	struct Format format = { .str = str };
	formatReset(&format);
	while (formatParse(&format, NULL)) {
		int n = snprintf(
			&buf[len], sizeof(buf) - len,
			"%.*ls", (int)format.len, format.str
		);
		if (n < 0) err(EX_OSERR, "snprintf");
		len += n;
		if (len >= sizeof(buf)) break;
	}
	eventPipe((const char *[]) { "notify-send", tag.name, buf, NULL });
}

void uiLog(struct Tag tag, enum UIHeat heat, const wchar_t *str) {
	struct Window *win = windowFor(tag);
	int lines = 1;
	waddch(win->log, '\n');
	if (win->mark && heat > UICold) {
		if (!win->unread++) {
			lines++;
			waddch(win->log, '\n');
		}
		if (heat > UIWarm) {
			win->hot = true;
			notify(tag, str);
		}
		uiStatus();
	}
	lines += addWrap(win->log, str);
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

static void keyCode(wchar_t code) {
	if (code == KEY_RESIZE) uiResize();
	struct Window *win = windows.active;
	if (!win) return;
	switch (code) {
		break; case KEY_UP:        windowScroll(win, -1);
		break; case KEY_DOWN:      windowScroll(win, +1);
		break; case KEY_PPAGE:     windowScroll(win, -(logHeight() - 1));
		break; case KEY_NPAGE:     windowScroll(win, +(logHeight() - 1));
		break; case KEY_LEFT:      edit(win->tag, EditLeft, 0);
		break; case KEY_RIGHT:     edit(win->tag, EditRight, 0);
		break; case KEY_HOME:      edit(win->tag, EditHome, 0);
		break; case KEY_END:       edit(win->tag, EditEnd, 0);
		break; case KEY_DC:        edit(win->tag, EditDelete, 0);
		break; case KEY_BACKSPACE: edit(win->tag, EditBackspace, 0);
		break; case KEY_ENTER:     edit(win->tag, EditEnter, 0);
		break; default: return;
	}
	uiStatus();
}

static void keyMeta(wchar_t ch) {
	struct Window *win = windows.active;
	if (ch >= L'0' && ch <= L'9') uiShowNum(ch - L'0', false);
	if (ch == L'a') uiShowAuto();
	if (ch == L'/' && windows.other) uiShowWindow(windows.other);
	if (!win) return;
	switch (ch) {
		break; case L'b':  edit(win->tag, EditBackWord, 0);
		break; case L'f':  edit(win->tag, EditForeWord, 0);
		break; case L'\b': edit(win->tag, EditKillBackWord, 0);
		break; case L'd':  edit(win->tag, EditKillForeWord, 0);
		break; case L'l':  uiHide(); logList(win->tag);
		break; case L'm':  uiLog(win->tag, UICold, L"");
	}
}

static void keyChar(wchar_t ch) {
	struct Window *win = windows.active;
	if (ch == CTRL(L'L')) clearok(curscr, true);
	if (!win) return;
	switch (ch) {
		break; case CTRL(L'N'): uiShowNum(+1, true);
		break; case CTRL(L'P'): uiShowNum(-1, true);

		break; case CTRL(L'A'): edit(win->tag, EditHome, 0);
		break; case CTRL(L'B'): edit(win->tag, EditLeft, 0);
		break; case CTRL(L'D'): edit(win->tag, EditDelete, 0);
		break; case CTRL(L'E'): edit(win->tag, EditEnd, 0);
		break; case CTRL(L'F'): edit(win->tag, EditRight, 0);
		break; case CTRL(L'K'): edit(win->tag, EditKillLine, 0);
		break; case CTRL(L'W'): edit(win->tag, EditKillBackWord, 0);

		break; case CTRL(L'C'): edit(win->tag, EditInsert, IRCColor);
		break; case CTRL(L'O'): edit(win->tag, EditInsert, IRCBold);
		break; case CTRL(L'R'): edit(win->tag, EditInsert, IRCColor);
		break; case CTRL(L'S'): edit(win->tag, EditInsert, IRCReset);
		break; case CTRL(L'T'): edit(win->tag, EditInsert, IRCItalic);
		break; case CTRL(L'U'): edit(win->tag, EditInsert, IRCUnderline);
		break; case CTRL(L'V'): edit(win->tag, EditInsert, IRCReverse);

		break; case L'\b': edit(win->tag, EditBackspace, 0);
		break; case L'\t': edit(win->tag, EditComplete, 0);
		break; case L'\n': edit(win->tag, EditEnter, 0);

		break; default: if (iswprint(ch)) edit(win->tag, EditInsert, ch);
	}
}

void uiRead(void) {
	if (ui.hide) uiShow();
	static bool meta;
	int ret;
	wint_t ch;
	enum TermEvent event;
	while (ERR != (ret = wget_wch(ui.input, &ch))) {
		if (ret == KEY_CODE_YES) {
			keyCode(ch);
		} else if (ch < 0200 && (event = termEvent((char)ch))) {
			struct Window *win = windows.active;
			switch (event) {
				break; case TermFocusIn:  if (win) windowUnmark(win);
				break; case TermFocusOut: if (win) windowMark(win);
				break; default: {}
			}
			uiStatus();
		} else if (ch == Esc) {
			meta = true;
			continue;
		} else if (meta) {
			keyMeta(ch == Del ? '\b' : ch);
		} else {
			keyChar(ch == Del ? '\b' : ch);
		}
		meta = false;
	}
	uiPrompt(false);
}

static bool isAction(struct Tag tag, const wchar_t *input) {
	if (tag.id == TagStatus.id || tag.id == TagRaw.id) return false;
	return !wcsncasecmp(input, L"/me ", 4);
}

static bool isCommand(struct Tag tag, const wchar_t *input) {
	if (tag.id == TagStatus.id || tag.id == TagRaw.id) return true;
	if (input[0] != L'/') return false;
	const wchar_t *space = wcschr(&input[1], L' ');
	const wchar_t *extra = wcschr(&input[1], L'/');
	return !extra || (space && extra > space);
}

void uiPrompt(bool nickChanged) {
	static wchar_t *promptMesg;
	static wchar_t *promptAction;
	if (nickChanged || !promptMesg || !promptAction) {
		free(promptMesg);
		free(promptAction);
		enum IRCColor color = colorGen(self.user);
		int len = aswprintf(&promptMesg, L"\3%d<%s>\3 ", color, self.nick);
		if (len < 0) err(EX_OSERR, "aswprintf");
		len = aswprintf(&promptAction, L"\3%d* %s\3 ", color, self.nick);
		if (len < 0) err(EX_OSERR, "aswprintf");
	}

	const wchar_t *input = editHead();

	wmove(ui.input, 0, 0);
	if (windows.active) {
		if (isAction(windows.active->tag, input) && editTail() >= &input[4]) {
			input = &input[4];
			addWrap(ui.input, promptAction);
		} else if (!isCommand(windows.active->tag, input)) {
			addWrap(ui.input, promptMesg);
		}
	}

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
