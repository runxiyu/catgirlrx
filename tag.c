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
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include "chat.h"

const struct Tag TAG_ALL = { (size_t)-1, NULL };
const struct Tag TAG_DEFAULT = { 0, "(status)" };

static struct {
	char *name[TAGS_LEN];
	size_t len;
	size_t gap;
} tags = {
	.name = { "(status)" },
	.len = 1,
	.gap = 1,
};

static struct Tag Tag(size_t id) {
	return (struct Tag) { id, tags.name[id] };
}

struct Tag tagName(const char *name) {
	for (size_t id = 0; id < tags.len; ++id) {
		if (!tags.name[id] || strcmp(tags.name[id], name)) continue;
		return Tag(id);
	}
	return TAG_ALL;
}

struct Tag tagNum(size_t num) {
	if (num < tags.gap) return Tag(num);
	num -= tags.gap;
	for (size_t id = tags.gap; id < tags.len; ++id) {
		if (!tags.name[id]) continue;
		if (!num--) return Tag(id);
	}
	return TAG_ALL;
}

struct Tag tagFor(const char *name) {
	struct Tag tag = tagName(name);
	if (tag.name) return tag;

	size_t id = tags.gap;
	tags.name[id] = strdup(name);
	if (!tags.name[id]) err(EX_OSERR, "strdup");

	if (tags.gap == tags.len) {
		tags.gap++;
		tags.len++;
	} else {
		for (tags.gap++; tags.gap < tags.len; ++tags.gap) {
			if (!tags.name[tags.gap]) break;
		}
	}

	return Tag(id);
}
