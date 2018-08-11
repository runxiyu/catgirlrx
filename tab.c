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

static struct Entry {
	size_t tag;
	char *word;
	struct Entry *prev;
	struct Entry *next;
} *head;

static void prepend(struct Entry *entry) {
	entry->prev = NULL;
	entry->next = head;
	if (head) head->prev = entry;
	head = entry;
}

static void unlink(struct Entry *entry) {
	if (entry->prev) entry->prev->next = entry->next;
	if (entry->next) entry->next->prev = entry->prev;
	if (head == entry) head = entry->next;
}

static void touch(struct Entry *entry) {
	if (head == entry) return;
	unlink(entry);
	prepend(entry);
}

void tabTouch(struct Tag tag, const char *word) {
	for (struct Entry *entry = head; entry; entry = entry->next) {
		if (entry->tag != tag.id) continue;
		if (strcmp(entry->word, word)) continue;
		touch(entry);
		return;
	}

	struct Entry *entry = malloc(sizeof(*entry));
	if (!entry) err(EX_OSERR, "malloc");

	entry->tag = tag.id;
	entry->word = strdup(word);
	if (!entry->word) err(EX_OSERR, "strdup");

	prepend(entry);
}

void tabReplace(const char *prev, const char *next) {
	for (struct Entry *entry = head; entry; entry = entry->next) {
		if (strcmp(entry->word, prev)) continue;
		free(entry->word);
		entry->word = strdup(next);
		if (!entry->word) err(EX_OSERR, "strdup");
	}
}

static struct Entry *match;

void tabRemove(struct Tag tag, const char *word) {
	for (struct Entry *entry = head; entry; entry = entry->next) {
		if (tag.id != TAG_ALL.id && entry->tag != tag.id) continue;
		if (strcmp(entry->word, word)) continue;
		unlink(entry);
		if (match == entry) match = entry->prev;
		free(entry->word);
		free(entry);
		return;
	}
}

void tabClear(struct Tag tag) {
	for (struct Entry *entry = head; entry; entry = entry->next) {
		if (entry->tag != tag.id) continue;
		unlink(entry);
		if (match == entry) match = entry->prev;
		free(entry->word);
		free(entry);
	}
}

const char *tabNext(struct Tag tag, const char *prefix) {
	size_t len = strlen(prefix);
	struct Entry *start = (match ? match->next : head);
	for (struct Entry *entry = start; entry; entry = entry->next) {
		if (entry->tag != TAG_DEFAULT.id && entry->tag != tag.id) continue;
		if (strncasecmp(entry->word, prefix, len)) continue;
		match = entry;
		return entry->word;
	}
	if (!match) return NULL;
	match = NULL;
	return tabNext(tag, prefix);
}

void tabAccept(void) {
	if (match) touch(match);
	match = NULL;
}

void tabReject(void) {
	match = NULL;
}
