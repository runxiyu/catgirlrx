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
#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <time.h>

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

const struct Line *bufferSoft(const struct Buffer *buffer, size_t i) {
	return linesLine(&buffer->soft, i);
}

const struct Line *bufferHard(const struct Buffer *buffer, size_t i) {
	return linesLine(&buffer->hard, i);
}

static void flow(struct Lines *hard, int cols, const struct Line *soft) {
	(void)hard;
	(void)cols;
	(void)soft;
}

void bufferPush(
	struct Buffer *buffer, int cols,
	enum Heat heat, time_t time, const char *str
) {
	struct Line *soft = &buffer->soft.lines[buffer->soft.len++ % BufferCap];
	free(soft->str);
	soft->heat = heat;
	soft->time = time;
	soft->str = strdup(str);
	if (!soft->str) err(EX_OSERR, "strdup");
	flow(&buffer->hard, cols, soft);
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
