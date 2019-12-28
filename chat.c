/* Copyright (C) 2018  C. McEnroe <june@causal.agency>
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

#define _WITH_GETLINE

#include <err.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "chat.h"

static char *dupe(const char *str) {
	char *dup = strdup(str);
	if (!dup) err(EX_OSERR, "strdup");
	return dup;
}

static char *prompt(const char *prompt) {
	char *line = NULL;
	size_t cap;
	for (;;) {
		printf("%s", prompt);
		fflush(stdout);

		ssize_t len = getline(&line, &cap, stdin);
		if (ferror(stdin)) err(EX_IOERR, "getline");
		if (feof(stdin)) exit(EX_OK);
		if (len < 2) continue;

		line[len - 1] = '\0';
		return line;
	}
}

int main(int argc, char *argv[]) {
	setlocale(LC_CTYPE, "");

	int opt;
	while (0 < (opt = getopt(argc, argv, "!NPRa:h:j:k:l:n:p:r:u:vw:"))) {
		switch (opt) {
			break; case '!': self.insecure = true;
			break; case 'N': self.notify = true;
			break; case 'P': self.nick = prompt("Name: ");
			break; case 'R': self.limit = true;
			break; case 'a': self.auth = dupe(optarg);
			break; case 'h': self.host = dupe(optarg);
			break; case 'j': self.join = dupe(optarg);
			break; case 'k': self.keys = dupe(optarg);
			break; case 'l': logOpen(optarg);
			break; case 'n': self.nick = dupe(optarg);
			break; case 'p': self.port = dupe(optarg);
			break; case 'r': self.real = dupe(optarg);
			break; case 'u': self.user = dupe(optarg);
			break; case 'v': self.raw = true;
			break; case 'w': self.pass = dupe(optarg);
			break; default:  return EX_USAGE;
		}
	}

	if (!self.nick) {
		const char *user = getenv("USER");
		if (!user) errx(EX_USAGE, "USER unset");
		self.nick = dupe(user);
	}

	if (!self.host) self.host = prompt("Host: ");
	if (!self.port) self.port = dupe("6697");
	if (!self.user) self.user = dupe(self.nick);
	if (!self.real) self.real = dupe(self.nick);

	inputTab();
	uiInit();
	eventLoop();
}
