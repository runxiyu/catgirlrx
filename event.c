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

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>
#include <sys/wait.h>
#include <sysexits.h>
#include <unistd.h>

#include "chat.h"

static struct {
	bool quit;
	bool wait;
	bool susp;
	int irc;
	int pipe;
} event = {
	.irc = -1,
	.pipe = -1,
};

void eventQuit(void) {
	event.quit = true;
}

void eventWait(const char *argv[static 2]) {
	uiHide();
	pid_t pid = fork();
	if (pid < 0) err(EX_OSERR, "fork");
	if (!pid) {
		execvp(argv[0], (char *const *)argv);
		err(EX_CONFIG, "%s", argv[0]);
	}
	event.wait = true;
}

static void childWait(void) {
	uiShow();
	int status;
	pid_t pid = wait(&status);
	if (pid < 0) err(EX_OSERR, "wait");
	if (WIFEXITED(status) && WEXITSTATUS(status)) {
		uiFmt(TagStatus, UIHot, "event: exit %d", WEXITSTATUS(status));
	} else if (WIFSIGNALED(status)) {
		uiFmt(
			TagStatus, UIHot,
			"event: signal %s", strsignal(WTERMSIG(status))
		);
	}
	event.wait = false;
}

void eventPipe(const char *argv[static 2]) {
	if (event.pipe > 0) {
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
	event.pipe = rw[0];
}

static void pipeRead(void) {
	char buf[256];
	ssize_t len = read(event.pipe, buf, sizeof(buf) - 1);
	if (len < 0) err(EX_IOERR, "read");
	if (len) {
		buf[len] = '\0';
		buf[strcspn(buf, "\n")] = '\0';
		uiFmt(TagStatus, UIHot, "event: %s", buf);
	} else {
		close(event.pipe);
		event.pipe = -1;
	}
}

static sig_atomic_t sig[NSIG];
static void handler(int n) {
	sig[n] = 1;
}

noreturn void eventLoop(void) {
	sigset_t mask;
	sigemptyset(&mask);
	struct sigaction action = {
		.sa_handler = handler,
		.sa_mask = mask,
		.sa_flags = SA_RESTART | SA_NOCLDSTOP,
	};
	sigaction(SIGCHLD, &action, NULL);
	sigaction(SIGINT, &action, NULL);
	sigaction(SIGTSTP, &action, NULL);

	struct sigaction curses;
	sigaction(SIGWINCH, &action, &curses);
	assert(!(curses.sa_flags & SA_SIGINFO));

	event.irc = ircConnect();
	for (;;) {
		if (sig[SIGCHLD]) childWait();
		if (sig[SIGINT]) {
			signal(SIGINT, SIG_DFL);
			ircFmt("QUIT :Goodbye\r\n");
			event.quit = true;
		}
		if (sig[SIGTSTP]) {
			signal(SIGTSTP, SIG_DFL);
			ircFmt("QUIT :zzz\r\n");
			event.susp = true;
		}
		if (sig[SIGWINCH]) {
			curses.sa_handler(SIGWINCH);
			uiRead();
		}
		sig[SIGCHLD] = sig[SIGINT] = sig[SIGTSTP] = sig[SIGWINCH] = 0;

		nfds_t nfds = 0;
		struct pollfd fds[3] = {
			{ .events = POLLIN },
			{ .events = POLLIN },
			{ .events = POLLIN },
		};
		if (!event.wait) fds[nfds++].fd = STDIN_FILENO;
		if (event.irc > 0) fds[nfds++].fd = event.irc;
		if (event.pipe > 0) fds[nfds++].fd = event.pipe;

		int ready = poll(fds, nfds, -1);
		if (ready < 0) {
			if (errno == EINTR) continue;
			err(EX_IOERR, "poll");
		}

		for (nfds_t i = 0; i < nfds; ++i) {
			if (!fds[i].revents) continue;
			if (fds[i].fd == STDIN_FILENO) uiRead();
			if (fds[i].fd == event.pipe) pipeRead();
			if (fds[i].fd == event.irc) {
				if (ircRead()) continue;
				event.irc = -1;
				// TODO: Handle unintended disconnects.
				if (event.quit) uiExit();
				if (event.susp) {
					uiHide();
					raise(SIGTSTP);
					sigaction(SIGTSTP, &action, NULL);
					uiShow();
					event.irc = ircConnect();
					event.susp = false;
				}
			}
		}
		uiDraw();
	}
}
