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

// XXX: editTail must always be called after editHead.
static wchar_t tail;
const wchar_t *editHead(void) {
	tail = *line.ptr;
	*line.ptr = L'\0';
	return line.buf;
}
const wchar_t *editTail(void) {
	if (tail) *line.ptr = tail;
	*line.end = L'\0';
	tail = L'\0';
	return line.ptr;
}

static void left(void) {
	if (line.ptr > line.buf) line.ptr--;
}
static void right(void) {
	if (line.ptr < line.end) line.ptr++;
}

static void backWord(void) {
	left();
	wchar_t *word = wcsnrchr(line.buf, line.ptr - line.buf, L' ');
	line.ptr = (word ? &word[1] : line.buf);
}
static void foreWord(void) {
	right();
	wchar_t *word = wcsnchr(line.ptr, line.end - line.ptr, L' ');
	line.ptr = (word ? word : line.end);
}

static void insert(wchar_t ch) {
	if (line.end == &line.buf[BUF_LEN - 1]) return;
	if (line.ptr != line.end) {
		wmemmove(line.ptr + 1, line.ptr, line.end - line.ptr);
	}
	*line.ptr++ = ch;
	line.end++;
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

static void killBackWord(void) {
	wchar_t *from = line.ptr;
	backWord();
	wmemmove(line.ptr, from, line.end - from);
	line.end -= from - line.ptr;
}
static void killForeWord(void) {
	wchar_t *from = line.ptr;
	foreWord();
	wmemmove(from, line.ptr, line.end - line.ptr);
	line.end -= line.ptr - from;
	line.ptr = from;
}

static char *prefix;
static void complete(struct Tag tag) {
	if (!line.tab) {
		line.tab = wcsnrchr(line.buf, line.ptr - line.buf, L' ');
		line.tab = (line.tab ? &line.tab[1] : line.buf);
		prefix = awcsntombs(line.tab, line.ptr - line.tab);
		if (!prefix) err(EX_DATAERR, "awcstombs");
	}

	const char *next = tabNext(tag, prefix);
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

static void enter(struct Tag tag) {
	if (line.end == line.buf) return;
	*line.end = L'\0';
	char *str = awcstombs(line.buf);
	if (!str) err(EX_DATAERR, "awcstombs");
	input(tag, str);
	free(str);
	line.ptr = line.buf;
	line.end = line.buf;
}

void edit(struct Tag tag, enum Edit op, wchar_t ch) {
	switch (op) {
		break; case EDIT_LEFT:  reject(); left();
		break; case EDIT_RIGHT: reject(); right();
		break; case EDIT_HOME:  reject(); line.ptr = line.buf;
		break; case EDIT_END:   reject(); line.ptr = line.end;

		break; case EDIT_BACK_WORD: reject(); backWord();
		break; case EDIT_FORE_WORD: reject(); foreWord();

		break; case EDIT_INSERT:    accept(); insert(ch);
		break; case EDIT_BACKSPACE: reject(); backspace();
		break; case EDIT_DELETE:    reject(); delete();

		break; case EDIT_KILL_BACK_WORD: reject(); killBackWord();
		break; case EDIT_KILL_FORE_WORD: reject(); killForeWord();
		break; case EDIT_KILL_LINE:      reject(); line.end = line.ptr;

		break; case EDIT_COMPLETE: complete(tag);

		break; case EDIT_ENTER: accept(); enter(tag);
	}
}
