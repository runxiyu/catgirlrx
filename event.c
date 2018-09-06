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
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sysexits.h>
#include <unistd.h>
#include <errno.h>

#include "chat.h"

static union {
	struct {
		struct pollfd ui;
		struct pollfd irc;
		struct pollfd pipe;
	};
	struct pollfd fds[3];
} fds;

void eventSpawn(char *const argv[]) {
	if (fds.pipe.events) {
		uiLog(TagStatus, UIHot, L"eventSpawn: existing pipe");
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
		uiFmt(TagStatus, UIHot, "eventSpawn: %.*s", (int)len, buf);
	} else {
		close(fds.pipe.fd);
		memset(&fds.pipe, 0, sizeof(fds.pipe));
	}
}

static void sigchld(int sig) {
	(void)sig;
	int status;
	pid_t pid = wait(&status);
	if (pid < 0) err(EX_OSERR, "wait");
	if (WIFEXITED(status) && WEXITSTATUS(status)) {
		uiFmt(TagStatus, UIHot, "eventSpawn: exit %d", WEXITSTATUS(status));
	} else if (WIFSIGNALED(status)) {
		uiFmt(TagStatus, UIHot, "eventSpawn: singal %d", WTERMSIG(status));
	}
}

static void sigint(int sig) {
	(void)sig;
	input(TagStatus, "/quit");
	uiExit();
	exit(EX_OK);
}

void eventLoop(int ui, int irc) {
	signal(SIGINT, sigint);
	signal(SIGCHLD, sigchld);

	fds.ui.fd = ui;
	fds.irc.fd = irc;
	fds.ui.events = POLLIN;
	fds.irc.events = POLLIN;

	for (;;) {
		uiDraw();

		int ready = poll(fds.fds, (fds.pipe.events ? 3 : 2), -1);
		if (ready < 0) {
			if (errno != EINTR) err(EX_IOERR, "poll");
			uiRead();
			continue;
		}

		if (fds.ui.revents) uiRead();
		if (fds.irc.revents) ircRead();
		if (fds.pipe.revents) pipeRead();
	}
}
