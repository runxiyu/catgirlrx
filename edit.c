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

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>

#include "chat.h"

enum { Cap = 1024 };
static wchar_t buf[Cap];
static size_t len;
static size_t pos;

char *editBuffer(size_t *mbsPos) {
	static char mbs[MB_LEN_MAX * Cap];

	const wchar_t *ptr = buf;
	size_t mbsLen = wcsnrtombs(mbs, &ptr, pos, sizeof(mbs) - 1, NULL);
	assert(mbsLen != (size_t)-1);
	if (mbsPos) *mbsPos = mbsLen;

	ptr = &buf[pos];
	size_t n = wcsnrtombs(
		&mbs[mbsLen], &ptr, len - pos, sizeof(mbs) - mbsLen - 1, NULL
	);
	assert(n != (size_t)-1);
	mbsLen += n;

	mbs[mbsLen] = '\0';
	return mbs;
}

static struct {
	wchar_t buf[Cap];
	size_t len;
} cut;

static bool reserve(size_t index, size_t count) {
	if (len + count > Cap) return false;
	memmove(&buf[index + count], &buf[index], sizeof(*buf) * (len - index));
	len += count;
	return true;
}

static void delete(bool copy, size_t index, size_t count) {
	if (index + count > len) return;
	if (copy) {
		memcpy(cut.buf, &buf[index], sizeof(*buf) * count);
		cut.len = count;
	}
	memmove(
		&buf[index], &buf[index + count], sizeof(*buf) * (len - index - count)
	);
	len -= count;
}

static const struct {
	const wchar_t *name;
	const wchar_t *string;
} Macros[] = {
	{ L"\\bear", L"ʕっ•ᴥ•ʔっ" },
	{ L"\\blush", L"（˶′◡‵˶）" },
	{ L"\\cool", L"(⌐■_■)" },
	{ L"\\flip", L"(╯°□°）╯︵ ┻━┻" },
	{ L"\\gary", L"ᕕ( ᐛ )ᕗ" },
	{ L"\\hug", L"（っ・∀・）っ" },
	{ L"\\lenny", L"( ͡° ͜ʖ ͡°)" },
	{ L"\\look", L"ಠ_ಠ" },
	{ L"\\shrug", L"¯\\_(ツ)_/¯" },
	{ L"\\unflip", L"┬─┬ノ(º_ºノ)" },
	{ L"\\wave", L"ヾ(＾∇＾)" },
};

void editCompleteAdd(void) {
	char mbs[256];
	for (size_t i = 0; i < ARRAY_LEN(Macros); ++i) {
		size_t n = wcstombs(mbs, Macros[i].name, sizeof(mbs));
		assert(n != (size_t)-1);
		completeAdd(None, mbs, Default);
	}
}

static void macroExpand(void) {
	size_t macro = pos;
	while (macro && !iswspace(buf[macro - 1])) macro--;
	if (macro == pos) return;
	for (size_t i = 0; i < ARRAY_LEN(Macros); ++i) {
		if (wcsncmp(Macros[i].name, &buf[macro], pos - macro)) continue;
		delete(false, macro, pos - macro);
		pos = macro;
		size_t expand = wcslen(Macros[i].string);
		if (reserve(macro, expand)) {
			wcsncpy(&buf[macro], Macros[i].string, expand);
			pos += expand;
		}
	}
}

static struct {
	size_t pos;
	size_t pre;
	size_t len;
} tab;

static void tabComplete(uint id) {
	if (!tab.len) {
		tab.pos = pos;
		while (tab.pos && !iswspace(buf[tab.pos - 1])) tab.pos--;
		if (tab.pos == pos) return;
		tab.pre = pos - tab.pos;
		tab.len = tab.pre;
	}

	char mbs[MB_LEN_MAX * Cap];
	const wchar_t *ptr = &buf[tab.pos];
	size_t n = wcsnrtombs(mbs, &ptr, tab.pre, sizeof(mbs) - 1, NULL);
	assert(n != (size_t)-1);
	mbs[n] = '\0';

	const char *comp = complete(id, mbs);
	if (!comp) comp = complete(id, mbs);
	if (!comp) {
		tab.len = 0;
		return;
	}

	wchar_t wcs[Cap];
	n = mbstowcs(wcs, comp, Cap);
	assert(n != (size_t)-1);
	if (tab.pos + n + 2 > Cap) {
		completeReject();
		tab.len = 0;
		return;
	}

	delete(false, tab.pos, tab.len);
	tab.len = n;
	if (wcs[0] == L'\\') {
		reserve(tab.pos, tab.len);
	} else if (wcs[0] != L'/' && !tab.pos) {
		tab.len += 2;
		reserve(tab.pos, tab.len);
		buf[tab.pos + n + 0] = L':';
		buf[tab.pos + n + 1] = L' ';
	} else if (tab.pos >= 2 && buf[tab.pos - 2] == L':') {
		tab.len += 2;
		reserve(tab.pos, tab.len);
		buf[tab.pos - 2] = L',';
		buf[tab.pos + n + 0] = L':';
		buf[tab.pos + n + 1] = L' ';
	} else {
		tab.len++;
		reserve(tab.pos, tab.len);
		buf[tab.pos + n] = L' ';
	}
	memcpy(&buf[tab.pos], wcs, sizeof(*wcs) * n);
	pos = tab.pos + tab.len;
}

static void tabAccept(void) {
	completeAccept();
	tab.len = 0;
}

static void tabReject(void) {
	completeReject();
	tab.len = 0;
}

void edit(uint id, enum Edit op, wchar_t ch) {
	size_t init = pos;
	switch (op) {
		break; case EditHead: pos = 0;
		break; case EditTail: pos = len;
		break; case EditPrev: if (pos) pos--;
		break; case EditNext: if (pos < len) pos++;
		break; case EditPrevWord: {
			if (pos) pos--;
			while (pos && !iswspace(buf[pos - 1])) pos--;
		}
		break; case EditNextWord: {
			if (pos < len) pos++;
			while (pos < len && !iswspace(buf[pos])) pos++;
		}

		break; case EditDeleteHead: delete(true, 0, pos); pos = 0;
		break; case EditDeleteTail: delete(true, pos, len - pos);
		break; case EditDeletePrev: if (pos) delete(false, --pos, 1);
		break; case EditDeleteNext: delete(false, pos, 1);
		break; case EditDeletePrevWord: {
			if (!pos) break;
			size_t word = pos - 1;
			while (word && !iswspace(buf[word - 1])) word--;
			delete(true, word, pos - word);
			pos = word;
		}
		break; case EditDeleteNextWord: {
			if (pos == len) break;
			size_t word = pos + 1;
			while (word < len && !iswspace(buf[word])) word++;
			delete(true, pos, word - pos);
		}
		break; case EditPaste: {
			if (reserve(pos, cut.len)) {
				memcpy(&buf[pos], cut.buf, sizeof(*buf) * cut.len);
				pos += cut.len;
			}
		}

		break; case EditTranspose: {
			if (!pos || len < 2) break;
			if (pos == len) pos--;
			wchar_t t = buf[pos - 1];
			buf[pos - 1] = buf[pos];
			buf[pos++] = t;
		}
		break; case EditCollapse: {
			size_t ws;
			for (pos = 0; pos < len;) {
				for (; pos < len && !iswspace(buf[pos]); ++pos);
				for (ws = pos; ws < len && iswspace(buf[ws]); ++ws);
				if (pos && ws < len) {
					delete(false, pos, ws - pos - 1);
					buf[pos++] = L' ';
				} else {
					delete(false, pos, ws - pos);
				}
			}
		}

		break; case EditInsert: {
			if (reserve(pos, 1)) {
				buf[pos++] = ch;
			}
		}
		break; case EditComplete: {
			tabComplete(id);
			return;
		}
		break; case EditExpand: {
			macroExpand();
			tabAccept();
			return;
		}
		break; case EditEnter: {
			tabAccept();
			command(id, editBuffer(NULL));
			len = pos = 0;
			return;
		}
	}

	if (pos < init) {
		tabReject();
	} else {
		tabAccept();
	}
}
