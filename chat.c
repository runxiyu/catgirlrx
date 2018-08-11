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
#include <sys/wait.h>

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

static union {
	struct {
		struct pollfd ui;
		struct pollfd irc;
		struct pollfd pipe;
	};
	struct pollfd fds[3];
} fds = {
	.ui   = { .events = POLLIN, .fd = STDIN_FILENO },
	.irc  = { .events = POLLIN },
	.pipe = { .events = 0 },
};

void spawn(char *const argv[]) {
	if (fds.pipe.events) {
		uiLog(TAG_DEFAULT, L"spawn: existing pipe");
		return;
	}

	int rw[2];
	int error = pipe(rw);
	if (error) err(EX_OSERR, "pipe");

	pid_t pid = fork();
	if (pid < 0) err(EX_OSERR, "fork");
	if (!pid) {
		close(rw[0]);
		close(STDIN_FILENO);
		dup2(rw[1], STDOUT_FILENO);
		dup2(rw[1], STDERR_FILENO);
		close(rw[1]);
		execvp(argv[0], argv);
		perror(argv[0]);
		exit(EX_CONFIG);
	}

	close(rw[1]);
	fds.pipe.fd = rw[0];
	fds.pipe.events = POLLIN;
}

static void pipeRead(void) {
	char buf[256];
	ssize_t len = read(fds.pipe.fd, buf, sizeof(buf) - 1);
	if (len < 0) err(EX_IOERR, "read");
	if (len) {
		buf[len] = '\0';
		len = strcspn(buf, "\n");
		uiFmt(TAG_DEFAULT, "%.*s", (int)len, buf);
	} else {
		close(fds.pipe.fd);
		fds.pipe.events = 0;
		fds.pipe.revents = 0;
	}
}

static void eventLoop(void) {
	for (;;) {
		uiDraw();

		int n = poll(fds.fds, (fds.pipe.events ? 3 : 2), -1);
		if (n < 0) {
			if (errno != EINTR) err(EX_IOERR, "poll");
			uiRead();
			continue;
		}

		if (fds.ui.revents) uiRead();
		if (fds.irc.revents) ircRead();
		if (fds.pipe.revents) pipeRead();
	}
}

static void sigchld(int sig) {
	(void)sig;
	int status;
	pid_t pid = wait(&status);
	if (pid < 0) err(EX_OSERR, "wait");
	if (WIFEXITED(status) && WEXITSTATUS(status)) {
		uiFmt(TAG_DEFAULT, "spawn: exit %d", WEXITSTATUS(status));
	} else if (WIFSIGNALED(status)) {
		uiFmt(TAG_DEFAULT, "spawn: signal %d", WTERMSIG(status));
	}
}

static void sigint(int sig) {
	(void)sig;
	input(TAG_DEFAULT, "/quit");
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
			break; case 'j': selfJoin(optarg);
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
	uiLog(TAG_DEFAULT, L"Traveling...");
	uiDraw();

	fds.irc.fd = ircConnect(host, port, pass, webirc);
	free(host);

	signal(SIGINT, sigint);
	signal(SIGCHLD, sigchld);
	eventLoop();
}
