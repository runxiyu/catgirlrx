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

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include "chat.h"

struct Node {
	size_t id;
	char *str;
	enum Color color;
	struct Node *prev;
	struct Node *next;
};

static struct Node *alloc(size_t id, const char *str, enum Color color) {
	struct Node *node = malloc(sizeof(*node));
	if (!node) err(EX_OSERR, "malloc");
	node->id = id;
	node->str = strdup(str);
	if (!node->str) err(EX_OSERR, "strdup");
	node->color = color;
	node->prev = NULL;
	node->next = NULL;
	return node;
}

static struct Node *head;
static struct Node *tail;

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
	if (!tail) tail = node;
	return node;
}

static struct Node *append(struct Node *node) {
	node->next = NULL;
	node->prev = tail;
	if (tail) tail->next = node;
	tail = node;
	if (!head) head = node;
	return node;
}

static struct Node *find(size_t id, const char *str) {
	for (struct Node *node = head; node; node = node->next) {
		if (node->id == id && !strcmp(node->str, str)) return node;
	}
	return NULL;
}

void completeAdd(size_t id, const char *str, enum Color color) {
	if (!find(id, str)) append(alloc(id, str, color));
}

void completeTouch(size_t id, const char *str, enum Color color) {
	struct Node *node = find(id, str);
	if (node && node->color != color) node->color = color;
	prepend(node ? detach(node) : alloc(id, str, color));
}

static struct Node *match;

const char *complete(size_t id, const char *prefix) {
	for (match = (match ? match->next : head); match; match = match->next) {
		if (match->id && match->id != id) continue;
		if (strncasecmp(match->str, prefix, strlen(prefix))) continue;
		return match->str;
	}
	return NULL;
}

void completeAccept(void) {
	if (match) prepend(detach(match));
	match = NULL;
}

void completeReject(void) {
	match = NULL;
}
