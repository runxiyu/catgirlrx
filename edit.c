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

char *editHead(void) {
	static char mbs[MB_LEN_MAX * Cap];
	const wchar_t *ptr = buf;
	size_t n = wcsnrtombs(mbs, &ptr, pos, sizeof(mbs) - 1, NULL);
	assert(n != (size_t)-1);
	mbs[n] = '\0';
	return mbs;
}

char *editTail(void) {
	static char mbs[MB_LEN_MAX * Cap];
	const wchar_t *ptr = &buf[pos];
	size_t n = wcsnrtombs(mbs, &ptr, len - pos, sizeof(mbs) - 1, NULL);
	assert(n != (size_t)-1);
	mbs[n] = '\0';
	return mbs;
}

void edit(size_t id, enum Edit op, wchar_t ch) {
	switch (op) {
		break; case EditKill: len = pos = 0;
		break; case EditInsert: {
			if (len == Cap) break;
			buf[pos++] = ch;
			len++;
		}
		break; case EditEnter: {
			pos = 0;
			command(id, editTail());
			len = 0;
		}
	}
}
