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

#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "chat.h"

static const char *Schemes[] = {
	"https:",
	"http:",
	"ftp:",
};
static const size_t SchemesLen = sizeof(Schemes) / sizeof(Schemes[0]);

struct Entry {
	size_t tag;
	char *url;
};

enum { RingLen = 32 };
static_assert(!(RingLen & (RingLen - 1)), "power of two RingLen");

static struct {
	struct Entry buf[RingLen];
	size_t end;
} ring;

static void ringPush(struct Tag tag, const char *url, size_t len) {
	free(ring.buf[ring.end].url);
	ring.buf[ring.end].tag = tag.id;
	ring.buf[ring.end].url = strndup(url, len);
	if (!ring.buf[ring.end].url) err(EX_OSERR, "strndup");
	ring.end = (ring.end + 1) & (RingLen - 1);
}

static struct Entry ringEntry(size_t i) {
	return ring.buf[(ring.end + i) & (RingLen - 1)];
}

void urlScan(struct Tag tag, const char *str) {
	while (str[0]) {
		size_t len = 1;
		for (size_t i = 0; i < SchemesLen; ++i) {
			if (strncmp(str, Schemes[i], strlen(Schemes[i]))) continue;
			len = strcspn(str, " >\"");
			ringPush(tag, str, len);
		}
		str = &str[len];
	}
}

void urlList(struct Tag tag) {
	uiHide();
	for (size_t i = 0; i < RingLen; ++i) {
		struct Entry entry = ringEntry(i);
		if (!entry.url || entry.tag != tag.id) continue;
		printf("%s\n", entry.url);
	}
}

void urlOpenMatch(struct Tag tag, const char *substr) {
	for (size_t i = RingLen - 1; i < RingLen; --i) {
		struct Entry entry = ringEntry(i);
		if (!entry.url || entry.tag != tag.id) continue;
		if (!strstr(entry.url, substr)) continue;
		char *argv[] = { "open", entry.url, NULL };
		eventPipe(argv);
		break;
	}
}

void urlOpenRange(struct Tag tag, size_t at, size_t to) {
	size_t argc = 1;
	char *argv[2 + RingLen] = { "open" };
	size_t tagIndex = 0;
	for (size_t i = RingLen - 1; i < RingLen; --i) {
		struct Entry entry = ringEntry(i);
		if (!entry.url || entry.tag != tag.id) continue;
		if (tagIndex >= at && tagIndex < to) argv[argc++] = entry.url;
		tagIndex++;
	}
	argv[argc] = NULL;
	if (argc > 1) eventPipe(argv);
}
