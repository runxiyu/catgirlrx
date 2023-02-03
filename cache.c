/* Copyright (C) 2020, 2022  June McEnroe <june@causal.agency>
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
#include <string.h>
#include <sysexits.h>

#include "chat.h"

static const struct Entry DefaultEntry = { .color = Default };

static struct Entry *entryAlloc(uint id, const char *key) {
	struct Entry *entry = malloc(sizeof(*entry));
	if (!entry) err(EX_OSERR, "malloc");
	*entry = DefaultEntry;
	entry->id = id;
	entry->key = strdup(key);
	if (!entry->key) err(EX_OSERR, "strdup");
	return entry;
}

static void entryFree(struct Entry *entry) {
	free(entry->key);
	free(entry);
}

static uint gen;
static struct Entry *head;
static struct Entry *tail;

static struct Entry *detach(struct Entry *entry) {
	if (entry->prev) entry->prev->next = entry->next;
	if (entry->next) entry->next->prev = entry->prev;
	if (head == entry) head = entry->next;
	if (tail == entry) tail = entry->prev;
	entry->prev = NULL;
	entry->next = NULL;
	return entry;
}

static struct Entry *prepend(struct Entry *entry) {
	entry->prev = NULL;
	entry->next = head;
	if (head) head->prev = entry;
	head = entry;
	tail = (tail ?: entry);
	return entry;
}

static struct Entry *append(struct Entry *entry) {
	entry->next = NULL;
	entry->prev = tail;
	if (tail) tail->next = entry;
	tail = entry;
	head = (head ?: entry);
	return entry;
}

static struct Entry *find(uint id, const char *key) {
	for (struct Entry *entry = head; entry; entry = entry->next) {
		if (entry->id != id) continue;
		if (strcmp(entry->key, key)) continue;
		return entry;
	}
	return NULL;
}

static struct Entry *insert(bool touch, uint id, const char *key) {
	struct Entry *entry = find(id, key);
	if (entry && touch) {
		return prepend(detach(entry));
	} else if (entry) {
		return entry;
	} else if (touch) {
		return prepend(entryAlloc(id, key));
	} else {
		return append(entryAlloc(id, key));
	}
}

const struct Entry *cacheGet(uint id, const char *key) {
	struct Entry *entry = find(id, key);
	return (entry ?: &DefaultEntry);
}

struct Entry *cacheInsert(bool touch, uint id, const char *key) {
	return insert(touch, id, key);
}

void cacheReplace(bool touch, const char *old, const char *new) {
	struct Entry *next = NULL;
	for (struct Entry *entry = head; entry; entry = next) {
		next = entry->next;
		if (strcmp(entry->key, old)) continue;
		free(entry->key);
		entry->key = strdup(new);
		if (!entry->key) err(EX_OSERR, "strdup");
		if (touch) prepend(detach(entry));
	}
}

void cacheRemove(uint id, const char *key) {
	gen++;
	struct Entry *next = NULL;
	for (struct Entry *entry = head; entry; entry = next) {
		next = entry->next;
		if (id && entry->id != id) continue;
		if (strcmp(entry->key, key)) continue;
		detach(entry);
		entryFree(entry);
		if (id) break;
	}
}

void cacheClear(uint id) {
	gen++;
	struct Entry *next = NULL;
	for (struct Entry *entry = head; entry; entry = next) {
		next = entry->next;
		if (entry->id != id) continue;
		detach(entry);
		entryFree(entry);
	}
}

const char *cacheComplete(struct Cursor *curs, uint id, const char *prefix) {
	size_t len = strlen(prefix);
	if (curs->gen != gen) curs->entry = NULL;
	for (
		curs->gen = gen, curs->entry = (curs->entry ? curs->entry->next : head);
		curs->entry;
		curs->entry = curs->entry->next
	) {
		if (curs->entry->id && curs->entry->id != id) continue;
		if (strncasecmp(curs->entry->key, prefix, len)) continue;
		return curs->entry->key;
	}
	return NULL;
}

const char *cacheSearch(struct Cursor *curs, uint id, const char *substr) {
	if (curs->gen != gen) curs->entry = NULL;
	for (
		curs->gen = gen, curs->entry = (curs->entry ? curs->entry->next : head);
		curs->entry;
		curs->entry = curs->entry->next
	) {
		if (curs->entry->id && curs->entry->id != id) continue;
		if (!strstr(curs->entry->key, substr)) continue;
		return curs->entry->key;
	}
	return NULL;
}

const char *cacheNextKey(struct Cursor *curs, uint id) {
	if (curs->gen != gen) curs->entry = NULL;
	for (
		curs->gen = gen, curs->entry = (curs->entry ? curs->entry->next : head);
		curs->entry;
		curs->entry = curs->entry->next
	) {
		if (curs->entry->id != id) continue;
		return curs->entry->key;
	}
	return NULL;
}

uint cacheNextID(struct Cursor *curs, const char *key) {
	if (curs->gen != gen) curs->entry = NULL;
	for (
		curs->gen = gen, curs->entry = (curs->entry ? curs->entry->next : head);
		curs->entry;
		curs->entry = curs->entry->next
	) {
		if (!curs->entry->id) continue;
		if (strcmp(curs->entry->key, key)) continue;
		return curs->entry->id;
	}
	return None;
}

void cacheTouch(struct Cursor *curs) {
	if (curs->gen == gen && curs->entry) {
		prepend(detach(curs->entry));
	}
}
