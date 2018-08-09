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

#include <err.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sysexits.h>
#include <wchar.h>
#include <wctype.h>

#include "chat.h"

enum { BUF_LEN = 512 };
static struct {
	wchar_t buf[BUF_LEN];
	wchar_t *ptr;
	wchar_t *end;
	wchar_t *tab;
} line = {
	.ptr = line.buf,
	.end = line.buf,
};

static wchar_t tail;
const wchar_t *editHead(void) {
	tail = *line.ptr;
	*line.ptr = L'\0';
	return line.buf;
}
const wchar_t *editTail(void) {
	*line.ptr = tail;
	*line.end = L'\0';
	return line.ptr;
}

static void left(void) {
	if (line.ptr > line.buf) line.ptr--;
}
static void right(void) {
	if (line.ptr < line.end) line.ptr++;
}
static void home(void) {
	line.ptr = line.buf;
}
static void end(void) {
	line.ptr = line.end;
}
static void kill(void) {
	line.end = line.ptr;
}

static void backspace(void) {
	if (line.ptr == line.buf) return;
	if (line.ptr != line.end) {
		wmemmove(line.ptr - 1, line.ptr, line.end - line.ptr);
	}
	line.ptr--;
	line.end--;
}
static void delete(void) {
	if (line.ptr == line.end) return;
	right();
	backspace();
}

static void insert(wchar_t ch) {
	if (line.end == &line.buf[BUF_LEN - 1]) return;
	if (line.ptr != line.end) {
		wmemmove(line.ptr + 1, line.ptr, line.end - line.ptr);
	}
	*line.ptr++ = ch;
	line.end++;
}

static void enter(void) {
	if (line.end == line.buf) return;
	*line.end = L'\0';
	char *str = awcstombs(line.buf);
	if (!str) err(EX_DATAERR, "awcstombs");
	input(str);
	free(str);
	line.ptr = line.buf;
	line.end = line.buf;
}

static char *prefix;
static void complete(void) {
	if (!line.tab) {
		editHead();
		line.tab = wcsrchr(line.buf, L' ');
		line.tab = (line.tab ? &line.tab[1] : line.buf);
		prefix = awcstombs(line.tab);
		if (!prefix) err(EX_DATAERR, "awcstombs");
		editTail();
	}

	const char *next = tabNext(prefix);
	if (!next) return;

	wchar_t *wcs = ambstowcs(next);
	if (!wcs) err(EX_DATAERR, "ambstowcs");

	size_t i = 0;
	for (; wcs[i] && line.ptr > &line.tab[i]; ++i) {
		line.tab[i] = wcs[i];
	}
	while (line.ptr > &line.tab[i]) {
		backspace();
	}
	for (; wcs[i]; ++i) {
		insert(wcs[i]);
	}
	free(wcs);

	size_t pos = line.tab - line.buf;
	if (!pos && line.tab[0] != L'/') {
		insert(L':');
	} else if (pos >= 2) {
		if (line.buf[pos - 2] == L':' || line.buf[pos - 2] == L',') {
			line.buf[pos - 2] = L',';
			insert(L':');
		}
	}
	insert(L' ');
}

static void accept(void) {
	if (!line.tab) return;
	line.tab = NULL;
	free(prefix);
	tabAccept();
}
static void reject(void) {
	if (!line.tab) return;
	line.tab = NULL;
	free(prefix);
	tabReject();
}

static bool editMeta(wchar_t ch) {
	(void)ch;
	return false;
}

static bool editCtrl(wchar_t ch) {
	switch (ch) {
		break; case L'B': reject(); left();
		break; case L'F': reject(); right();
		break; case L'A': reject(); home();
		break; case L'E': reject(); end();
		break; case L'D': reject(); delete();
		break; case L'K': reject(); kill();

		break; case L'C': accept(); insert(IRC_COLOR);
		break; case L'N': accept(); insert(IRC_RESET);
		break; case L'O': accept(); insert(IRC_BOLD);
		break; case L'R': accept(); insert(IRC_COLOR);
		break; case L'T': accept(); insert(IRC_ITALIC);
		break; case L'V': accept(); insert(IRC_REVERSE);

		break; default: return false;
	}
	return true;
}

bool edit(bool meta, bool ctrl, wchar_t ch) {
	if (meta) return editMeta(ch);
	if (ctrl) return editCtrl(ch);
	switch (ch) {
		break; case L'\t': complete();
		break; case L'\b': reject(); backspace();
		break; case L'\n': accept(); enter();
		break; default: {
			if (!iswprint(ch)) return false;
			accept();
			insert(ch);
		}
	}
	return true;
}
