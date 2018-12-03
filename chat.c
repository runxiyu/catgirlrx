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

#define _WITH_GETLINE

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "chat.h"

static void freedup(char **field, const char *str) {
	free(*field);
	*field = strdup(str);
	if (!*field) err(EX_OSERR, "strdup");
}

void selfNick(const char *nick) {
	freedup(&self.nick, nick);
}
void selfUser(const char *user) {
	freedup(&self.user, user);
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
	char *host = NULL;
	char *port = "6697";
	char *pass = NULL;
	char *webirc = NULL;

	int opt;
	while (0 < (opt = getopt(argc, argv, "NW:h:j:l:n:p:r:u:vw:"))) {
		switch (opt) {
			break; case 'N': self.notify = true;
			break; case 'W': webirc = strdup(optarg);
			break; case 'h': host = strdup(optarg);
			break; case 'j': freedup(&self.join, optarg);
			break; case 'l': logOpen(optarg);
			break; case 'n': selfNick(optarg);
			break; case 'p': port = strdup(optarg);
			break; case 'r': freedup(&self.real, optarg);
			break; case 'u': selfUser(optarg);
			break; case 'v': self.verbose = true;
			break; case 'w': pass = strdup(optarg);
			break; default:  return EX_USAGE;
		}
	}
	if (!port) err(EX_OSERR, "strdup");

	if (!host) host = prompt("Host: ");
	if (!self.nick) self.nick = prompt("Name: ");
	if (!self.user) selfUser(self.nick);
	if (!self.real) freedup(&self.real, self.nick);

	inputTab();
	uiInit();
	uiDraw();
	ircInit(host, port, pass, webirc);
	eventLoop();
}
