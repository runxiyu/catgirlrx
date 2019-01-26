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
	bool wait;
	int pipe;
} child = {
	.pipe = -1,
};

void eventWait(const char *argv[static 2]) {
	uiHide();
	pid_t pid = fork();
	if (pid < 0) err(EX_OSERR, "fork");
	if (!pid) {
		execvp(argv[0], (char *const *)argv);
		err(EX_CONFIG, "%s", argv[0]);
	}
	child.wait = true;
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
	child.wait = false;
}

void eventPipe(const char *argv[static 2]) {
	if (child.pipe > 0) {
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
	child.pipe = rw[0];
}

static void childRead(void) {
	char buf[256];
	ssize_t len = read(child.pipe, buf, sizeof(buf) - 1);
	if (len < 0) err(EX_IOERR, "read");
	if (len) {
		buf[len] = '\0';
		buf[strcspn(buf, "\n")] = '\0';
		uiFmt(TagStatus, UIHot, "event: %s", buf);
	} else {
		close(child.pipe);
		child.pipe = -1;
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
	sigaction(SIGHUP, &action, NULL);

	struct sigaction curses;
	sigaction(SIGWINCH, &action, &curses);
	assert(!(curses.sa_flags & SA_SIGINFO));

	uiFmt(TagStatus, UICold, "Traveling to %s...", self.host);
	uiDraw();
	int irc = ircConnect();

	for (;;) {
		if (sig[SIGCHLD]) childWait();
		if (sig[SIGHUP]) ircQuit("zzz");
		if (sig[SIGINT]) {
			signal(SIGINT, SIG_DFL);
			ircQuit("Goodbye");
		}
		if (sig[SIGWINCH]) {
			curses.sa_handler(SIGWINCH);
			uiRead();
			uiDraw();
		}
		sig[SIGCHLD] = sig[SIGHUP] = sig[SIGINT] = sig[SIGWINCH] = 0;

		struct pollfd fds[3] = {
			{ .events = POLLIN, .fd = irc },
			{ .events = POLLIN, .fd = STDIN_FILENO },
			{ .events = POLLIN, .fd = child.pipe },
		};
		if (child.wait) fds[1].events = 0;
		if (child.pipe < 0) fds[2].events = 0;

		int nfds = poll(fds, 3, -1);
		if (nfds < 0) {
			if (errno == EINTR) continue;
			err(EX_IOERR, "poll");
		}

		if (fds[0].revents) ircRead();
		if (fds[1].revents) uiRead();
		if (fds[2].revents) childRead();

		uiDraw();
	}
}
