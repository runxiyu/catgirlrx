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

void selfNick(const char *nick) {
	free(self.nick);
	self.nick = strdup(nick);
	if (!self.nick) err(EX_OSERR, "strdup");
}
void selfUser(const char *user) {
	free(self.user);
	self.user = strdup(user);
	if (!self.user) err(EX_OSERR, "strdup");
}
void selfJoin(const char *join) {
	free(self.join);
	self.join = strdup(join);
	if (!self.join) err(EX_OSERR, "strdup");
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
	const char *port = "6697";
	const char *pass = NULL;
	const char *webirc = NULL;

	int opt;
	while (0 < (opt = getopt(argc, argv, "W:h:j:l:n:p:u:vw:"))) {
		switch (opt) {
			break; case 'W': webirc = optarg;
			break; case 'h': host = strdup(optarg);
			break; case 'j': selfJoin(optarg);
			break; case 'l': logOpen(optarg);
			break; case 'n': selfNick(optarg);
			break; case 'p': port = optarg;
			break; case 'u': selfUser(optarg);
			break; case 'v': self.verbose = true;
			break; case 'w': pass = optarg;
			break; default:  return EX_USAGE;
		}
	}

	if (!host) host = prompt("Host: ");
	if (!self.nick) self.nick = prompt("Name: ");
	if (!self.user) selfUser(self.nick);

	inputTab();

	uiInit();
	uiLog(TagStatus, UIWarm, L"Traveling...");
	uiDraw();

	int irc = ircConnect(host, port, pass, webirc);
	free(host);

	eventLoop(STDIN_FILENO, irc);
}
