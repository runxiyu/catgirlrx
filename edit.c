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

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "chat.h"

enum { Cap = 512 };
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

static void reserve(size_t index, size_t count) {
	if (len + count > Cap) return;
	memmove(&buf[index + count], &buf[index], sizeof(*buf) * (len - index));
	len += count;
}

static void delete(size_t index, size_t count) {
	if (index + count > len) return;
	memmove(
		&buf[index], &buf[index + count], sizeof(*buf) * (len - index - count)
	);
	len -= count;
}

static struct {
	size_t pos;
	size_t pre;
	size_t len;
} tab;

static void tabComplete(size_t id) {
	if (!tab.len) {
		tab.pos = pos;
		while (tab.pos && buf[tab.pos - 1] != L' ') tab.pos--;
		if (tab.pos == pos) return;
		tab.pre = pos - tab.pos;
		tab.len = tab.pre;
	}

	char mbs[MB_LEN_MAX * tab.pre + 1];
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

	wchar_t wcs[strlen(comp) + 1];
	n = mbstowcs(wcs, comp, sizeof(wcs));
	assert(n != (size_t)-1);
	if (tab.pos + n + 2 > Cap) {
		completeReject();
		tab.len = 0;
		return;
	}

	delete(tab.pos, tab.len);
	if (wcs[0] != L'/' && !tab.pos) {
		tab.len = n + 2;
		reserve(tab.pos, tab.len);
		buf[tab.pos + n + 0] = L':';
		buf[tab.pos + n + 1] = L' ';
	} else if (
		tab.pos >= 2 && (buf[tab.pos - 2] == L':' || buf[tab.pos - 2] == L',')
	) {
		tab.len = n + 2;
		reserve(tab.pos, tab.len);
		buf[tab.pos - 2] = L',';
		buf[tab.pos + n + 0] = L':';
		buf[tab.pos + n + 1] = L' ';
	} else {
		tab.len = n + 1;
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

void edit(size_t id, enum Edit op, wchar_t ch) {
	size_t init = pos;
	switch (op) {
		break; case EditHead: pos = 0;
		break; case EditTail: pos = len;
		break; case EditPrev: if (pos) pos--;
		break; case EditNext: if (pos < len) pos++;

		break; case EditDeletePrev: if (pos) delete(--pos, 1);
		break; case EditDeleteNext: delete(pos, 1);
		break; case EditKillPrev: delete(0, pos); pos = 0;
		break; case EditKillNext: delete(pos, len - pos);

		break; case EditInsert: {
			reserve(pos, 1);
			if (pos < Cap) buf[pos++] = ch;
		}
		break; case EditComplete: {
			tabComplete(id);
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
