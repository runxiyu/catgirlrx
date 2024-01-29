/* Copyright (C) 2020  June McEnroe <june@causal.agency>
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
 * combining it with OpenSSL (or a modified version of that library),
 * containing parts covered by the terms of the OpenSSL License and the
 * original SSLeay license, the licensors of this Program grant you
 * additional permission to convey the resulting work. Corresponding
 * Source for a non-source form of such a combination shall include the
 * source code for the parts of OpenSSL used as well as that of the
 * covered work.
 */

#define _XOPEN_SOURCE_EXTENDED

#include <assert.h>
#include <curses.h>
#include <err.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <time.h>

#include "chat.h"

#define MAIN_LINES (LINES - StatusLines - InputLines)

static struct Window {
	uint id;
	int scroll;
	bool mark;
	bool mute;
	bool time;
	enum Heat thresh;
	enum Heat heat;
	uint unreadSoft;
	uint unreadHard;
	uint unreadWarm;
	struct Buffer *buffer;
} *windows[IDCap];

static uint count;
static uint show;
static uint swap;
static uint user;

static uint windowPush(struct Window *window) {
	assert(count < IDCap);
	windows[count] = window;
	return count++;
}

static uint windowInsert(uint num, struct Window *window) {
	assert(count < IDCap);
	assert(num <= count);
	memmove(
		&windows[num + 1],
		&windows[num],
		sizeof(*windows) * (count - num)
	);
	windows[num] = window;
	count++;
	return num;
}

static struct Window *windowRemove(uint num) {
	assert(num < count);
	struct Window *window = windows[num];
	count--;
	memmove(
		&windows[num],
		&windows[num + 1],
		sizeof(*windows) * (count - num)
	);
	return window;
}

static void windowFree(struct Window *window) {
	completeRemove(None, idNames[window->id]);
	bufferFree(window->buffer);
	free(window);
}

enum Heat windowThreshold = Cold;
struct Time windowTime = { .format = "%X" };

uint windowFor(uint id) {
	for (uint num = 0; num < count; ++num) {
		if (windows[num]->id == id) return num;
	}

	struct Window *window = calloc(1, sizeof(*window));
	if (!window) err(EX_OSERR, "malloc");

	window->id = id;
	window->mark = true;
	window->time = windowTime.enable;
	if (id == Network || id == Debug) {
		window->thresh = Cold;
	} else {
		window->thresh = windowThreshold;
	}
	window->buffer = bufferAlloc();
	completePush(None, idNames[id], idColors[id]);

	return windowPush(window);
}

enum { TimeCap = 64 };

void windowInit(void) {
	char fmt[TimeCap];
	char buf[TimeCap];
	styleStrip(fmt, sizeof(fmt), windowTime.format);

	struct tm *time = localtime(&(time_t) { -22100400 });
	size_t len = strftime(buf, sizeof(buf), fmt, time);
	if (!len) errx(EX_CONFIG, "invalid timestamp format: %s", fmt);

	int y;
	waddstr(uiMain, buf);
	waddch(uiMain, ' ');
	getyx(uiMain, y, windowTime.width);
	(void)y;

	windowFor(Network);
}

static int styleAdd(WINDOW *win, struct Style init, const char *str) {
	struct Style style = init;
	while (*str) {
		size_t len = styleParse(&style, &str);
		wattr_set(win, uiAttr(style), uiPair(style), NULL);
		if (waddnstr(win, str, len) == ERR)
			return -1;
		str += len;
	}
	return 0;
}

static void statusUpdate(void) {
	struct {
		uint unread;
		enum Heat heat;
	} others = { 0, Cold };

	wmove(uiStatus, 0, 0);
	for (uint num = 0; num < count; ++num) {
		const struct Window *window = windows[num];
		if (num != show && !window->scroll && !inputPending(window->id)) {
			if (window->heat < Warm) continue;
			if (window->mute && window->heat < Hot) continue;
		}
		if (num != show) {
			others.unread += window->unreadWarm;
			if (window->heat > others.heat) others.heat = window->heat;
		}
		char buf[256], *end = &buf[sizeof(buf)];
		char *ptr = seprintf(
			buf, end, "\3%d%s %u%s%s %s ",
			idColors[window->id], (num == show ? "\26" : ""),
			num, window->thresh[(const char *[]) { "-", "", "+", "++" }],
			&"="[!window->mute], idNames[window->id]
		);
		if (window->mark && window->unreadWarm) {
			ptr = seprintf(
				ptr, end, "\3%d+%d\3%d ",
				(window->heat > Warm ? White : idColors[window->id]),
				window->unreadWarm, idColors[window->id]
			);
		}
		if (window->scroll) {
			ptr = seprintf(ptr, end, "~%d ", window->scroll);
		}
		if (num != show && inputPending(window->id)) {
			ptr = seprintf(ptr, end, "@ ");
		}
		if (styleAdd(uiStatus, StyleDefault, buf) < 0) break;
	}
	wclrtoeol(uiStatus);

	const struct Window *window = windows[show];
	char *end = &uiTitle[sizeof(uiTitle)];
	char *ptr = seprintf(
		uiTitle, end, "%s %s", network.name, idNames[window->id]
	);
	if (window->mark && window->unreadWarm) {
		ptr = seprintf(
			ptr, end, " +%d%s", window->unreadWarm, &"!"[window->heat < Hot]
		);
	}
	if (others.unread) {
		ptr = seprintf(
			ptr, end, " (+%d%s)", others.unread, &"!"[others.heat < Hot]
		);
	}
}

static size_t windowTop(const struct Window *window) {
	size_t top = BufferCap - MAIN_LINES - window->scroll;
	if (window->scroll) top += MarkerLines;
	return top;
}

static size_t windowBottom(const struct Window *window) {
	size_t bottom = BufferCap - (window->scroll ?: 1);
	if (window->scroll) bottom -= SplitLines + MarkerLines;
	return bottom;
}

static void mainAdd(int y, bool time, const struct Line *line) {
	int ny, nx;
	wmove(uiMain, y, 0);
	if (!line || !line->str[0]) {
		wclrtoeol(uiMain);
		return;
	}
	if (time && line->time) {
		char buf[TimeCap];
		strftime(buf, sizeof(buf), windowTime.format, localtime(&line->time));
		struct Style init = { .fg = Gray, .bg = Default };
		styleAdd(uiMain, init, buf);
		waddch(uiMain, ' ');
	} else if (time) {
		whline(uiMain, ' ', windowTime.width);
		wmove(uiMain, y, windowTime.width);
	}
	styleAdd(uiMain, StyleDefault, line->str);
	getyx(uiMain, ny, nx);
	if (ny != y) return;
	wclrtoeol(uiMain);
	(void)nx;
}

static void mainUpdate(void) {
	const struct Window *window = windows[show];

	int y = 0;
	int marker = MAIN_LINES - SplitLines - MarkerLines;
	for (size_t i = windowTop(window); i < BufferCap; ++i) {
		mainAdd(y++, window->time, bufferHard(window->buffer, i));
		if (window->scroll && y == marker) break;
	}
	if (!window->scroll) return;

	y = MAIN_LINES - SplitLines;
	for (size_t i = BufferCap - SplitLines; i < BufferCap; ++i) {
		mainAdd(y++, window->time, bufferHard(window->buffer, i));
	}
	wattr_set(uiMain, A_NORMAL, 0, NULL);
	mvwhline(uiMain, marker, 0, ACS_BULLET, COLS);
}

void windowUpdate(void) {
	statusUpdate();
	mainUpdate();
}

void windowBare(void) {
	uiHide();
	inputWait();

	const struct Window *window = windows[show];
	const struct Line *line = bufferHard(window->buffer, windowBottom(window));

	uint num = 0;
	if (line) num = line->num;
	for (size_t i = 0; i < BufferCap; ++i) {
		line = bufferSoft(window->buffer, i);
		if (!line) continue;
		if (line->num > num) break;
		if (!line->str[0]) {
			printf("\n");
			continue;
		}

		char buf[TimeCap];
		struct Style style = { .fg = Gray, .bg = Default };
		strftime(buf, sizeof(buf), windowTime.format, localtime(&line->time));
		vid_attr(uiAttr(style), uiPair(style), NULL);
		printf("%s ", buf);

		bool align = false;
		style = StyleDefault;
		for (const char *str = line->str; *str;) {
			if (*str == '\t') {
				printf("%c", (align ? '\t' : ' '));
				align = true;
				str++;
			}

			size_t len = styleParse(&style, &str);
			size_t tab = strcspn(str, "\t");
			if (tab < len) len = tab;

			vid_attr(uiAttr(style), uiPair(style), NULL);
			printf("%.*s", (int)len, str);
			str += len;
		}
		printf("\n");
	}
}

static void mark(struct Window *window) {
	if (window->scroll) return;
	window->mark = true;
	window->unreadSoft = 0;
	window->unreadWarm = 0;
}

static void unmark(struct Window *window) {
	if (!window->scroll) {
		window->mark = false;
		window->heat = Cold;
	}
	statusUpdate();
}

static void scrollN(struct Window *window, int n) {
	mark(window);
	window->scroll += n;
	if (window->scroll > BufferCap - MAIN_LINES) {
		window->scroll = BufferCap - MAIN_LINES;
	}
	if (window->scroll < 0) window->scroll = 0;
	unmark(window);
	if (window == windows[show]) mainUpdate();
}

static void scrollTo(struct Window *window, int top) {
	window->scroll = 0;
	scrollN(window, top - MAIN_LINES + MarkerLines);
}

static int windowCols(const struct Window *window) {
	return COLS - (window->time ? windowTime.width : 0);
}

bool windowWrite(uint id, enum Heat heat, const time_t *src, const char *str) {
	struct Window *window = windows[windowFor(id)];
	time_t ts = (src ? *src : time(NULL));

	if (heat >= window->thresh) {
		if (!window->unreadSoft++) window->unreadHard = 0;
	}
	if (window->mark && heat > Cold) {
		if (!window->unreadWarm++) {
			int lines = bufferPush(
				window->buffer, windowCols(window),
				window->thresh, Warm, ts, ""
			);
			if (window->scroll) scrollN(window, lines);
			if (window->unreadSoft > 1) {
				window->unreadSoft++;
				window->unreadHard += lines;
			}
		}
		if (heat > window->heat) window->heat = heat;
		statusUpdate();
	}
	int lines = bufferPush(
		window->buffer, windowCols(window),
		window->thresh, heat, ts, str
	);
	window->unreadHard += lines;
	if (window->scroll) scrollN(window, lines);
	if (window == windows[show]) mainUpdate();

	return window->mark && heat > Warm;
}

static void reflow(struct Window *window) {
	uint num = 0;
	const struct Line *line = bufferHard(window->buffer, windowTop(window));
	if (line) num = line->num;
	window->unreadHard = bufferReflow(
		window->buffer, windowCols(window),
		window->thresh, window->unreadSoft
	);
	if (!window->scroll || !num) return;
	for (size_t i = 0; i < BufferCap; ++i) {
		line = bufferHard(window->buffer, i);
		if (!line || line->num != num) continue;
		scrollTo(window, BufferCap - i);
		break;
	}
}

void windowResize(void) {
	for (uint num = 0; num < count; ++num) {
		reflow(windows[num]);
	}
	windowUpdate();
}

uint windowID(void) {
	return windows[show]->id;
}

uint windowNum(void) {
	return show;
}

void windowShow(uint num) {
	if (num >= count) return;
	if (num != show) {
		swap = show;
		mark(windows[swap]);
	}
	show = num;
	user = num;
	unmark(windows[show]);
	mainUpdate();
	inputUpdate();
}

void windowAuto(void) {
	uint minHot = UINT_MAX, numHot = 0;
	uint minWarm = UINT_MAX, numWarm = 0;
	for (uint num = 0; num < count; ++num) {
		struct Window *window = windows[num];
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
	uint oldUser = user;
	if (minHot < UINT_MAX) {
		windowShow(numHot);
		user = oldUser;
	} else if (minWarm < UINT_MAX) {
		windowShow(numWarm);
		user = oldUser;
	} else if (user != show) {
		windowShow(user);
	}
}

void windowSwap(void) {
	windowShow(swap);
}

void windowMove(uint from, uint to) {
	if (from >= count) return;
	struct Window *window = windowRemove(from);
	if (to < count) {
		windowShow(windowInsert(to, window));
	} else {
		windowShow(windowPush(window));
	}
}

void windowClose(uint num) {
	if (num >= count) return;
	if (windows[num]->id == Network) return;
	struct Window *window = windowRemove(num);
	completeRemove(window->id, NULL);
	windowFree(window);
	if (swap >= num) swap--;
	if (show == num) {
		windowShow(swap);
		swap = show;
	} else if (show > num) {
		show--;
		mainUpdate();
	}
	statusUpdate();
}

void windowList(void) {
	for (uint num = 0; num < count; ++num) {
		const struct Window *window = windows[num];
		uiFormat(
			Network, Warm, NULL, "\3%02d%u %s",
			idColors[window->id], num, idNames[window->id]
		);
	}
}

void windowMark(void) {
	mark(windows[show]);
}

void windowUnmark(void) {
	unmark(windows[show]);
}

void windowToggleMute(void) {
	windows[show]->mute ^= true;
	statusUpdate();
}

void windowToggleTime(void) {
	windows[show]->time ^= true;
	reflow(windows[show]);
	windowUpdate();
	inputUpdate();
}

void windowToggleThresh(int n) {
	struct Window *window = windows[show];
	if (n > 0 && window->thresh == Hot) return;
	if (n < 0 && window->thresh == Ice) {
		window->thresh = Cold;
	} else {
		window->thresh += n;
	}
	reflow(window);
	windowUpdate();
}

bool windowTimeEnable(void) {
	return windows[show]->time;
}

void windowScroll(enum Scroll by, int n) {
	struct Window *window = windows[show];
	switch (by) {
		break; case ScrollOne: {
			scrollN(window, n);
		}
		break; case ScrollPage: {
			scrollN(window, n * (MAIN_LINES - SplitLines - MarkerLines - 1));
		}
		break; case ScrollAll: {
			if (n < 0) {
				scrollTo(window, 0);
				break;
			}
			for (size_t i = 0; i < BufferCap; ++i) {
				if (!bufferHard(window->buffer, i)) continue;
				scrollTo(window, BufferCap - i);
				break;
			}
		}
		break; case ScrollUnread: {
			scrollTo(window, window->unreadHard);
		}
		break; case ScrollHot: {
			for (size_t i = windowTop(window) + n; i < BufferCap; i += n) {
				const struct Line *line = bufferHard(window->buffer, i);
				const struct Line *prev = bufferHard(window->buffer, i - 1);
				if (!line || line->heat < Hot) continue;
				if (prev && prev->heat > Warm) continue;
				scrollTo(window, BufferCap - i);
				break;
			}
		}
	}
}

void windowSearch(const char *str, int dir) {
	struct Window *window = windows[show];
	for (size_t i = windowTop(window) + dir; i < BufferCap; i += dir) {
		const struct Line *line = bufferHard(window->buffer, i);
		if (!line || !strcasestr(line->str, str)) continue;
		scrollTo(window, BufferCap - i);
		break;
	}
}

static int writeTime(FILE *file, time_t time) {
	return (fwrite(&time, sizeof(time), 1, file) ? 0 : -1);
}

static int writeString(FILE *file, const char *str) {
	return (fwrite(str, strlen(str) + 1, 1, file) ? 0 : -1);
}

int windowSave(FILE *file) {
	int error;
	for (uint num = 0; num < count; ++num) {
		const struct Window *window = windows[num];
		error = 0
			|| writeString(file, idNames[window->id])
			|| writeTime(file, window->mute)
			|| writeTime(file, window->time)
			|| writeTime(file, window->thresh)
			|| writeTime(file, window->heat)
			|| writeTime(file, window->unreadSoft)
			|| writeTime(file, window->unreadWarm);
		if (error) return error;
		for (size_t i = 0; i < BufferCap; ++i) {
			const struct Line *line = bufferSoft(window->buffer, i);
			if (!line) continue;
			error = 0
				|| writeTime(file, line->time)
				|| writeTime(file, line->heat)
				|| writeString(file, line->str);
			if (error) return error;
		}
		error = writeTime(file, 0);
		if (error) return error;
	}
	return writeString(file, "");
}

static time_t readTime(FILE *file) {
	time_t time;
	fread(&time, sizeof(time), 1, file);
	if (ferror(file)) err(EX_IOERR, "fread");
	if (feof(file)) errx(EX_DATAERR, "unexpected end of save file");
	return time;
}

static ssize_t readString(FILE *file, char **buf, size_t *cap) {
	ssize_t len = getdelim(buf, cap, '\0', file);
	if (len < 0 && !feof(file)) err(EX_IOERR, "getdelim");
	return len;
}

void windowLoad(FILE *file, size_t version) {
	size_t cap = 0;
	char *buf = NULL;
	while (0 < readString(file, &buf, &cap) && buf[0]) {
		struct Window *window = windows[windowFor(idFor(buf))];
		if (version > 3) window->mute = readTime(file);
		if (version > 6) window->time = readTime(file);
		if (version > 5) window->thresh = readTime(file);
		if (version > 0) {
			window->heat = readTime(file);
			window->unreadSoft = readTime(file);
			window->unreadWarm = readTime(file);
		}
		for (;;) {
			time_t time = readTime(file);
			if (!time) break;
			enum Heat heat = (version > 2 ? readTime(file) : Cold);
			readString(file, &buf, &cap);
			bufferPush(window->buffer, COLS, window->thresh, heat, time, buf);
		}
		reflow(window);
	}
	free(buf);
}
