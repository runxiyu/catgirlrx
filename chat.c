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
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "chat.h"

static void sigint(int sig) {
	(void)sig;
	input("/quit");
	uiExit();
	exit(EX_OK);
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
	while (0 < (opt = getopt(argc, argv, "W:h:j:n:p:u:vw:"))) {
		switch (opt) {
			break; case 'W': webirc = optarg;
			break; case 'h': host = strdup(optarg);
			break; case 'j': chat.join = strdup(optarg);
			break; case 'n': chat.nick = strdup(optarg);
			break; case 'p': port = optarg;
			break; case 'u': chat.user = strdup(optarg);
			break; case 'v': chat.verbose = true;
			break; case 'w': pass = optarg;
			break; default:  return EX_USAGE;
		}
	}

	if (!host) host = prompt("Host: ");
	if (!chat.join) chat.join = prompt("Join: ");
	if (!chat.nick) chat.nick = prompt("Name: ");
	if (!chat.user) chat.user = strdup(chat.nick);

	inputTab();

	signal(SIGINT, sigint);
	uiInit();
	uiLog(L"Traveling...");
	uiDraw();

	int sock = ircConnect(host, port, pass, webirc);
	free(host);

	struct pollfd fds[2] = {
		{ .fd = STDIN_FILENO, .events = POLLIN },
		{ .fd = sock, .events = POLLIN },
	};
	for (;;) {
		int nfds = poll(fds, 2, -1);
		if (nfds < 0) {
			if (errno != EINTR) err(EX_IOERR, "poll");
			fds[0].revents = POLLIN;
			fds[1].revents = 0;
		}

		if (fds[0].revents) uiRead();
		if (fds[1].revents) ircRead();
		uiDraw();
	}
}
