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

static void backWord(void) {
	left();
	editHead();
	wchar_t *word = wcsrchr(line.buf, ' ');
	editTail();
	line.ptr = (word ? &word[1] : line.buf);
}
static void foreWord(void) {
	right();
	editHead();
	editTail();
	wchar_t *word = wcschr(line.ptr, ' ');
	line.ptr = (word ? word : line.end);
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
static void killLine(void) {
	line.end = line.ptr;
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
	switch (ch) {
		break; case L'b':  reject(); backWord();
		break; case L'f':  reject(); foreWord();
		break; case L'\b': reject(); killBackWord();
		break; case L'd':  reject(); killForeWord();

		break; default: return false;
	}
	return true;
}

static bool editCtrl(wchar_t ch) {
	switch (ch) {
		break; case L'B': reject(); left();
		break; case L'F': reject(); right();
		break; case L'A': reject(); home();
		break; case L'E': reject(); end();
		break; case L'D': reject(); delete();
		break; case L'W': reject(); killBackWord();
		break; case L'K': reject(); killLine();

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
