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

static void remove(struct Entry *entry) {
	if (entry->prev) entry->prev->next = entry->next;
	if (entry->next) entry->next->prev = entry->prev;
	if (head == entry) head = entry->next;
}

void tabTouch(const char *word) {
	for (struct Entry *entry = head; entry; entry = entry->next) {
		if (strcmp(entry->word, word)) continue;
		if (head == entry) return;
		remove(entry);
		prepend(entry);
		return;
	}

	struct Entry *entry = malloc(sizeof(*entry));
	if (!entry) err(EX_OSERR, "malloc");
	entry->word = strdup(word);
	prepend(entry);
}

void tabRemove(const char *word) {
	for (struct Entry *entry = head; entry; entry = entry->next) {
		if (strcmp(entry->word, word)) continue;
		remove(entry);
		free(entry->word);
		free(entry);
		return;
	}
}

void tabReplace(const char *prev, const char *next) {
	tabTouch(prev);
	free(head->word);
	head->word = strdup(next);
}
