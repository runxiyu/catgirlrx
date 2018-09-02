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
	struct Tag tag;
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

static struct Entry *find(struct Tag tag, const char *word) {
	for (struct Entry *entry = head; entry; entry = entry->next) {
		if (entry->tag.id != tag.id) continue;
		if (strcmp(entry->word, word)) continue;
		return entry;
	}
	return NULL;
}

static void add(struct Tag tag, const char *word) {
	struct Entry *entry = malloc(sizeof(*entry));
	if (!entry) err(EX_OSERR, "malloc");

	entry->tag = tag;
	entry->word = strdup(word);
	if (!entry->word) err(EX_OSERR, "strdup");

	prepend(entry);
}

void tabTouch(struct Tag tag, const char *word) {
	struct Entry *entry = find(tag, word);
	if (entry) {
		touch(entry);
	} else {
		add(tag, word);
	}
}

void tabAdd(struct Tag tag, const char *word) {
	if (!find(tag, word)) add(tag, word);
}

void tabReplace(struct Tag tag, const char *prev, const char *next) {
	struct Entry *entry = find(tag, prev);
	if (!entry) return;
	touch(entry);
	free(entry->word);
	entry->word = strdup(next);
	if (!entry->word) err(EX_OSERR, "strdup");
}

static struct Entry *iter;

void tabRemove(struct Tag tag, const char *word) {
	for (struct Entry *entry = head; entry; entry = entry->next) {
		if (entry->tag.id != tag.id) continue;
		if (strcmp(entry->word, word)) continue;
		if (iter == entry) iter = entry->prev;
		unlink(entry);
		free(entry->word);
		free(entry);
		return;
	}
}

void tabClear(struct Tag tag) {
	for (struct Entry *entry = head; entry; entry = entry->next) {
		if (entry->tag.id != tag.id) continue;
		if (iter == entry) iter = entry->prev;
		unlink(entry);
		free(entry->word);
		free(entry);
	}
}

struct Tag tabTag(const char *word) {
	struct Entry *start = (iter ? iter->next : head);
	for (struct Entry *entry = start; entry; entry = entry->next) {
		if (strcmp(entry->word, word)) continue;
		iter = entry;
		return entry->tag;
	}
	iter = NULL;
	return TagNone;
}

const char *tabNext(struct Tag tag, const char *prefix) {
	size_t len = strlen(prefix);
	struct Entry *start = (iter ? iter->next : head);
	for (struct Entry *entry = start; entry; entry = entry->next) {
		if (entry->tag.id != TagNone.id && entry->tag.id != tag.id) continue;
		if (strncasecmp(entry->word, prefix, len)) continue;
		iter = entry;
		return entry->word;
	}
	if (!iter) return NULL;
	iter = NULL;
	return tabNext(tag, prefix);
}

void tabAccept(void) {
	if (iter) touch(iter);
	iter = NULL;
}

void tabReject(void) {
	iter = NULL;
}
