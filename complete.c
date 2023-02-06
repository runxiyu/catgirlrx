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
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include "chat.h"

struct Node {
	uint id;
	char *str;
	enum Color color;
	uint bits;
	struct Node *prev;
	struct Node *next;
};

static uint gen;
static struct Node *head;
static struct Node *tail;

static struct Node *alloc(uint id, const char *str, enum Color color) {
	struct Node *node = calloc(1, sizeof(*node));
	if (!node) err(EX_OSERR, "calloc");
	node->id = id;
	node->str = strdup(str);
	if (!node->str) err(EX_OSERR, "strdup");
	node->color = color;
	node->bits = 0;
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

static struct Node *find(uint id, const char *str) {
	for (struct Node *node = head; node; node = node->next) {
		if (node->id == id && !strcmp(node->str, str)) return node;
	}
	return NULL;
}

void completePush(uint id, const char *str, enum Color color) {
	struct Node *node = find(id, str);
	if (node) {
		if (color != Default) node->color = color;
	} else {
		append(alloc(id, str, color));
	}
}

void completePull(uint id, const char *str, enum Color color) {
	struct Node *node = find(id, str);
	if (node) {
		if (color != Default) node->color = color;
		prepend(detach(node));
	} else {
		prepend(alloc(id, str, color));
	}
}

void completeReplace(const char *old, const char *new) {
	struct Node *next = NULL;
	for (struct Node *node = head; node; node = next) {
		next = node->next;
		if (strcmp(node->str, old)) continue;
		free(node->str);
		node->str = strdup(new);
		if (!node->str) err(EX_OSERR, "strdup");
		prepend(detach(node));
	}
}

void completeRemove(uint id, const char *str) {
	struct Node *next = NULL;
	for (struct Node *node = head; node; node = next) {
		next = node->next;
		if (id && node->id != id) continue;
		if (str && strcmp(node->str, str)) continue;
		detach(node);
		free(node->str);
		free(node);
	}
	gen++;
}

enum Color completeColor(uint id, const char *str) {
	struct Node *node = find(id, str);
	return (node ? node->color : Default);
}

uint *completeBits(uint id, const char *str) {
	struct Node *node = find(id, str);
	return (node ? &node->bits : NULL);
}

const char *completePrefix(struct Cursor *curs, uint id, const char *prefix) {
	size_t len = strlen(prefix);
	if (curs->gen != gen) curs->node = NULL;
	for (
		curs->gen = gen, curs->node = (curs->node ? curs->node->next : head);
		curs->node;
		curs->node = curs->node->next
	) {
		if (curs->node->id && curs->node->id != id) continue;
		if (!strncasecmp(curs->node->str, prefix, len)) return curs->node->str;
	}
	return NULL;
}

const char *completeSubstr(struct Cursor *curs, uint id, const char *substr) {
	if (curs->gen != gen) curs->node = NULL;
	for (
		curs->gen = gen, curs->node = (curs->node ? curs->node->next : head);
		curs->node;
		curs->node = curs->node->next
	) {
		if (curs->node->id && curs->node->id != id) continue;
		if (strstr(curs->node->str, substr)) return curs->node->str;
	}
	return NULL;
}

const char *completeEach(struct Cursor *curs, uint id) {
	if (curs->gen != gen) curs->node = NULL;
	for (
		curs->gen = gen, curs->node = (curs->node ? curs->node->next : head);
		curs->node;
		curs->node = curs->node->next
	) {
		if (curs->node->id == id) return curs->node->str;
	}
	return NULL;
}

uint completeEachID(struct Cursor *curs, const char *str) {
	if (curs->gen != gen) curs->node = NULL;
	for (
		curs->gen = gen, curs->node = (curs->node ? curs->node->next : head);
		curs->node;
		curs->node = curs->node->next
	) {
		if (!curs->node->id) continue;
		if (!strcmp(curs->node->str, str)) return curs->node->id;
	}
	return None;
}

void completeAccept(struct Cursor *curs) {
	if (curs->gen == gen && curs->node) {
		prepend(detach(curs->node));
	}
	curs->node = NULL;
}

void completeReject(struct Cursor *curs) {
	curs->node = NULL;
}
