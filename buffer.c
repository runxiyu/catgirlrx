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
 * combining it with OpenSSL (or a modified version of that library),
 * containing parts covered by the terms of the OpenSSL License and the
 * original SSLeay license, the licensors of this Program grant you
 * additional permission to convey the resulting work. Corresponding
 * Source for a non-source form of such a combination shall include the
 * source code for the parts of OpenSSL used as well as that of the
 * covered work.
 */

#include <err.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <time.h>
#include <wchar.h>

#include "chat.h"

struct Lines {
	size_t len;
	struct Line lines[BufferCap];
};
_Static_assert(!(BufferCap & (BufferCap - 1)), "BufferCap is power of two");

struct Buffer {
	struct Lines soft;
	struct Lines hard;
};

struct Buffer *bufferAlloc(void) {
	struct Buffer *buffer = calloc(1, sizeof(*buffer));
	if (!buffer) err(EX_OSERR, "calloc");
	return buffer;
}

void bufferFree(struct Buffer *buffer) {
	for (size_t i = 0; i < BufferCap; ++i) {
		free(buffer->soft.lines[i].str);
		free(buffer->hard.lines[i].str);
	}
	free(buffer);
}

static const struct Line *linesLine(const struct Lines *lines, size_t i) {
	const struct Line *line = &lines->lines[(lines->len + i) % BufferCap];
	return (line->str ? line : NULL);
}

static struct Line *linesNext(struct Lines *lines) {
	struct Line *line = &lines->lines[lines->len++ % BufferCap];
	free(line->str);
	return line;
}

const struct Line *bufferSoft(const struct Buffer *buffer, size_t i) {
	return linesLine(&buffer->soft, i);
}

const struct Line *bufferHard(const struct Buffer *buffer, size_t i) {
	return linesLine(&buffer->hard, i);
}

enum { StyleCap = 10 };
static void styleCat(struct Cat *cat, struct Style style) {
	catf(
		cat, "%s%s%s%s",
		(style.attr & Bold ? (const char []) { B, '\0' } : ""),
		(style.attr & Reverse ? (const char []) { R, '\0' } : ""),
		(style.attr & Italic ? (const char []) { I, '\0' } : ""),
		(style.attr & Underline ? (const char []) { U, '\0' } : "")
	);
	if (style.fg != Default || style.bg != Default) {
		catf(cat, "\3%02d,%02d", style.fg, style.bg);
	}
}

static const wchar_t ZWS = L'\u200B';
static const wchar_t ZWNJ = L'\u200C';

static int flow(struct Lines *hard, int cols, const struct Line *soft) {
	int flowed = 1;

	struct Line *line = linesNext(hard);
	line->heat = soft->heat;
	line->time = soft->time;
	line->str = strdup(soft->str);
	if (!line->str) err(EX_OSERR, "strdup");

	int width = 0;
	int align = 0;
	char *wrap = NULL;
	struct Style style = StyleDefault;
	for (char *str = line->str; *str;) {
		size_t len = styleParse(&style, (const char **)&str);
		if (!len) continue;

		bool tab = (*str == '\t' && !align);
		if (tab) *str = ' ';

		wchar_t wc = L'\0';
		int n = mbtowc(&wc, str, len);
		if (n < 0) {
			n = 1;
			width++;
		} else if (wc == ZWS || wc == ZWNJ) {
			// XXX: ncurses likes to render these as spaces when they should be
			// zero-width, so just remove them entirely.
			memmove(str, &str[n], strlen(&str[n]) + 1);
			continue;
		} else if (wc < L' ') {
			// XXX: ncurses will render these as "^A".
			width += 2;
		} else if (wcwidth(wc) > 0) {
			width += wcwidth(wc);
		}

		if (width <= cols) {
			if (tab && width < cols) align = width;
			if (iswspace(wc) && !tab) wrap = str;
			if (*str == '-') wrap = &str[1];
			str += n;
			continue;
		}

		if (!wrap) wrap = str;
		n = mbtowc(&wc, wrap, strlen(wrap));
		if (n < 0) {
			n = 1;
		} else if (!iswspace(wc)) {
			n = 0;
		}

		flowed++;
		line = linesNext(hard);
		line->heat = soft->heat;
		line->time = soft->time;

		size_t cap = StyleCap + align + strlen(&wrap[n]) + 1;
		line->str = malloc(cap);
		if (!line->str) err(EX_OSERR, "malloc");

		struct Cat cat = { line->str, cap, 0 };
		styleCat(&cat, style);
		str = &line->str[cat.len];
		catf(&cat, "%*s%n%s", align, "", &width, &wrap[n]);
		str += width;

		*wrap = '\0';
		wrap = NULL;
	}

	return flowed;
}

int bufferPush(
	struct Buffer *buffer, int cols,
	enum Heat heat, time_t time, const char *str
) {
	struct Line *soft = linesNext(&buffer->soft);
	soft->heat = heat;
	soft->time = time;
	soft->str = strdup(str);
	if (!soft->str) err(EX_OSERR, "strdup");
	return flow(&buffer->hard, cols, soft);
}

void bufferReflow(struct Buffer *buffer, int cols) {
	buffer->hard.len = 0;
	for (size_t i = 0; i < BufferCap; ++i) {
		free(buffer->hard.lines[i].str);
		buffer->hard.lines[i].str = NULL;
	}
	for (size_t i = 0; i < BufferCap; ++i) {
		const struct Line *soft = bufferSoft(buffer, i);
		if (soft) flow(&buffer->hard, cols, soft);
	}
}
