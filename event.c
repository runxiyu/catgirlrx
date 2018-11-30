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

static struct {
	bool wait;
	bool pipe;
	int fd;
} spawn;

void eventWait(const char *argv[static 2]) {
	uiHide();
	pid_t pid = fork();
	if (pid < 0) err(EX_OSERR, "fork");
	if (!pid) {
		execvp(argv[0], (char *const *)argv);
		err(EX_CONFIG, "%s", argv[0]);
	}
	spawn.wait = true;
}

void eventPipe(const char *argv[static 2]) {
	if (spawn.pipe) {
		uiLog(TagStatus, UIHot, L"event: existing pipe");
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
		execvp(argv[0], (char *const *)argv);
		perror(argv[0]);
		exit(EX_CONFIG);
	}

	close(rw[1]);
	spawn.fd = rw[0];
	spawn.pipe = true;
}

static void pipeRead(void) {
	char buf[256];
	ssize_t len = read(spawn.fd, buf, sizeof(buf) - 1);
	if (len < 0) err(EX_IOERR, "read");
	if (len) {
		buf[len] = '\0';
		len = strcspn(buf, "\n");
		uiFmt(TagStatus, UIHot, "event: %.*s", (int)len, buf);
	} else {
		close(spawn.fd);
		spawn.pipe = false;
	}
}

static void sigchld(int sig) {
	(void)sig;
	int status;
	pid_t pid = wait(&status);
	if (pid < 0) err(EX_OSERR, "wait");
	if (WIFEXITED(status) && WEXITSTATUS(status)) {
		uiFmt(TagStatus, UIHot, "event: exit %d", WEXITSTATUS(status));
	} else if (WIFSIGNALED(status)) {
		uiFmt(TagStatus, UIHot, "event: signal %d", WTERMSIG(status));
	}
	spawn.wait = false;
}

static void sigint(int sig) {
	(void)sig;
	input(TagStatus, "/quit");
	uiExit();
	exit(EX_OK);
}

void eventLoop(void) {
	signal(SIGINT, sigint);
	signal(SIGCHLD, sigchld);

	int irc = ircConnect();

	struct pollfd fds[3] = {
		{ irc, POLLIN, 0 },
		{ STDIN_FILENO, POLLIN, 0 },
		{ -1, POLLIN, 0 },
	};
	for (;;) {
		nfds_t nfds = 2;
		if (spawn.wait) nfds = 1;
		if (spawn.pipe) {
			fds[2].fd = spawn.fd;
			nfds = 3;
		}

		int ready = poll(fds, nfds, -1);
		if (ready < 0) {
			if (errno != EINTR) err(EX_IOERR, "poll");
			uiRead();
			uiDraw();
			continue;
		}

		if (fds[0].revents) ircRead();
		if (nfds > 1 && fds[1].revents) uiRead();
		if (nfds > 2 && fds[2].revents) pipeRead();

		if (nfds > 1) uiDraw();
	}
}
