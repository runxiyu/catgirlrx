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

struct Node {
	uint id;
	char *key;
	enum Color color;
	struct Node *prev;
	struct Node *next;
};

static uint gen;
static struct Node *head;
static struct Node *tail;

static struct Node *alloc(uint id, const char *key) {
	struct Node *node = calloc(1, sizeof(*node));
	if (!node) err(EX_OSERR, "calloc");
	node->id = id;
	node->key = strdup(key);
	node->color = Default;
	if (!node->key) err(EX_OSERR, "strdup");
	return node;
}

static struct Node *detach(struct Node *node) {
	if (node->prev) node->prev->next = node->next;
	if (node->next) node->next->prev = node->prev;
	if (head == node) head = node->next;
	if (tail == node) tail = node->prev;
	node->prev = NULL;
	node->next = NULL;
	return node;
}

static struct Node *prepend(struct Node *node) {
	node->prev = NULL;
	node->next = head;
	if (head) head->prev = node;
	head = node;
	tail = (tail ?: node);
	return node;
}

static struct Node *append(struct Node *node) {
	node->next = NULL;
	node->prev = tail;
	if (tail) tail->next = node;
	tail = node;
	head = (head ?: node);
	return node;
}

static struct Node *find(uint id, const char *key) {
	for (struct Node *node = head; node; node = node->next) {
		if (node->id != id) continue;
		if (strcmp(node->key, key)) continue;
		return node;
	}
	return NULL;
}

static struct Node *insert(bool touch, uint id, const char *key) {
	struct Node *node = find(id, key);
	if (node && touch) {
		return prepend(detach(node));
	} else if (node) {
		return node;
	} else if (touch) {
		return prepend(alloc(id, key));
	} else {
		return append(alloc(id, key));
	}
}

void cacheInsert(bool touch, uint id, const char *key) {
	insert(touch, id, key);
}

void cacheInsertColor(bool touch, uint id, const char *key, enum Color color) {
	insert(touch, id, key)->color = color;
}

void cacheReplace(bool touch, const char *old, const char *new) {
	struct Node *next = NULL;
	for (struct Node *node = head; node; node = next) {
		next = node->next;
		if (strcmp(node->key, old)) continue;
		free(node->key);
		node->key = strdup(new);
		if (!node->key) err(EX_OSERR, "strdup");
		if (touch) prepend(detach(node));
	}
}

enum Color cacheColor(uint id, const char *key) {
	struct Node *node = find(id, key);
	return (node ? node->color : Default);
}

const char *cacheComplete(struct Cursor *curs, uint id, const char *prefix) {
	size_t len = strlen(prefix);
	if (curs->gen != gen) curs->node = NULL;
	for (
		curs->gen = gen, curs->node = (curs->node ? curs->node->next : head);
		curs->node;
		curs->node = curs->node->next
	) {
		if (curs->node->id && curs->node->id != id) continue;
		if (strncasecmp(curs->node->key, prefix, len)) continue;
		return curs->node->key;
	}
	return NULL;
}

const char *cacheSearch(struct Cursor *curs, uint id, const char *substr) {
	if (curs->gen != gen) curs->node = NULL;
	for (
		curs->gen = gen, curs->node = (curs->node ? curs->node->next : head);
		curs->node;
		curs->node = curs->node->next
	) {
		if (curs->node->id && curs->node->id != id) continue;
		if (!strstr(curs->node->key, substr)) continue;
		return curs->node->key;
	}
	return NULL;
}

uint cacheID(struct Cursor *curs, const char *key) {
	if (curs->gen != gen) curs->node = NULL;
	for (
		curs->gen = gen, curs->node = (curs->node ? curs->node->next : head);
		curs->node;
		curs->node = curs->node->next
	) {
		if (!curs->node->id) continue;
		if (strcmp(curs->node->key, key)) continue;
		return curs->node->id;
	}
	return None;
}

void cacheAccept(struct Cursor *curs) {
	if (curs->gen == gen && curs->node) {
		prepend(detach(curs->node));
	}
	curs->node = NULL;
}

void cacheReject(struct Cursor *curs) {
	curs->node = NULL;
}

void cacheRemove(uint id, const char *key) {
	gen++;
	struct Node *next = NULL;
	for (struct Node *node = head; node; node = next) {
		next = node->next;
		if (id && node->id != id) continue;
		if (strcmp(node->key, key)) continue;
		detach(node);
		free(node->key);
		free(node);
		if (id) break;
	}
}

void cacheClear(uint id) {
	gen++;
	struct Node *next = NULL;
	for (struct Node *node = head; node; node = next) {
		next = node->next;
		if (node->id != id) continue;
		detach(node);
		free(node->key);
		free(node);
	}
}
