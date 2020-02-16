/* Copyright (C) 2020  C. McEnroe <june@causal.agency>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sysexits.h>
#include <unistd.h>

#include "chat.h"

#ifndef OPENSSL_BIN
#define OPENSSL_BIN "openssl"
#endif

static void genCert(const char *path) {
	const char *name = strrchr(path, '/');
	name = (name ? &name[1] : path);
	char subj[256];
	snprintf(subj, sizeof(subj), "/CN=%.*s", (int)strcspn(name, "."), name);
	umask(0066);
	execlp(
		OPENSSL_BIN, "openssl", "req",
		"-x509", "-new", "-newkey", "rsa:4096", "-sha256", "-days", "3650",
		"-nodes", "-subj", subj, "-out", path, "-keyout", path,
		NULL
	);
	err(EX_UNAVAILABLE, "openssl");
}

char *idNames[IDCap] = {
	[None] = "<none>",
	[Debug] = "<debug>",
	[Network] = "<network>",
};

enum Color idColors[IDCap] = {
	[None] = Black,
	[Debug] = Green,
	[Network] = Gray,
};

uint idNext = Network + 1;

struct Network network;
struct Self self = { .color = Default };

static const char *save;
static void exitSave(void) {
	int error = uiSave(save);
	if (error) {
		warn("%s", save);
		_exit(EX_IOERR);
	}
}

uint32_t hashInit;

int utilPipe[2] = { -1, -1 };
int execPipe[2] = { -1, -1 };

static void utilRead(void) {
	char buf[1024];
	ssize_t len = read(utilPipe[0], buf, sizeof(buf) - 1);
	if (len < 0) err(EX_IOERR, "read");
	if (!len) return;
	buf[len] = '\0';
	char *ptr = buf;
	while (ptr) {
		char *line = strsep(&ptr, "\n");
		if (line[0]) uiFormat(Network, Warm, NULL, "%s", line);
	}
}

static void execRead(void) {
	char buf[1024];
	ssize_t len = read(execPipe[0], buf, sizeof(buf) - 1);
	if (len < 0) err(EX_IOERR, "read");
	if (!len) return;
	buf[len] = '\0';
	char *ptr = buf;
	while (ptr) {
		char *line = strsep(&ptr, "\n");
		if (line[0]) command(execID, line);
	}
}

static volatile sig_atomic_t signals[NSIG];
static void signalHandler(int signal) {
	signals[signal] = 1;
}

int main(int argc, char *argv[]) {
	setlocale(LC_CTYPE, "");

	bool insecure = false;
	const char *bind = NULL;
	const char *host = NULL;
	const char *port = "6697";
	const char *cert = NULL;
	const char *priv = NULL;

	bool sasl = false;
	const char *pass = NULL;
	const char *nick = NULL;
	const char *user = NULL;
	const char *real = NULL;

	const char *Opts = "!C:H:N:O:RS:a:c:eg:h:j:k:n:p:r:s:u:vw:";
	const struct option LongOpts[] = {
		{ "insecure", no_argument, NULL, '!' },
		{ "copy", required_argument, NULL, 'C' },
		{ "hash", required_argument, NULL, 'H' },
		{ "notify", required_argument, NULL, 'N' },
		{ "open", required_argument, NULL, 'O' },
		{ "restrict", no_argument, NULL, 'R' },
		{ "bind", required_argument, NULL, 'S' },
		{ "sasl-plain", required_argument, NULL, 'a' },
		{ "cert", required_argument, NULL, 'c' },
		{ "sasl-external", no_argument, NULL, 'e' },
		{ "host", required_argument, NULL, 'h' },
		{ "join", required_argument, NULL, 'j' },
		{ "priv", required_argument, NULL, 'k' },
		{ "nick", required_argument, NULL, 'n' },
		{ "port", required_argument, NULL, 'p' },
		{ "real", required_argument, NULL, 'r' },
		{ "save", required_argument, NULL, 's' },
		{ "user", required_argument, NULL, 'u' },
		{ "debug", no_argument, NULL, 'v' },
		{ "pass", required_argument, NULL, 'w' },
		{0},
	};

	int opt;
	while (0 < (opt = getopt_config(argc, argv, Opts, LongOpts, NULL))) {
		switch (opt) {
			break; case '!': insecure = true;
			break; case 'C': utilPush(&urlCopyUtil, optarg);
			break; case 'H': hashInit = strtoul(optarg, NULL, 0);
			break; case 'N': utilPush(&uiNotifyUtil, optarg);
			break; case 'O': utilPush(&urlOpenUtil, optarg);
			break; case 'R': self.restricted = true;
			break; case 'S': bind = optarg;
			break; case 'a': sasl = true; self.plain = optarg;
			break; case 'c': cert = optarg;
			break; case 'e': sasl = true;
			break; case 'g': genCert(optarg);
			break; case 'h': host = optarg;
			break; case 'j': self.join = optarg;
			break; case 'k': priv = optarg;
			break; case 'n': nick = optarg;
			break; case 'p': port = optarg;
			break; case 'r': real = optarg;
			break; case 's': save = optarg;
			break; case 'u': user = optarg;
			break; case 'v': self.debug = true;
			break; case 'w': pass = optarg;
			break; default:  return EX_USAGE;
		}
	}
	if (!host) errx(EX_USAGE, "host required");

	if (!nick) nick = getenv("USER");
	if (!nick) errx(EX_CONFIG, "USER unset");
	if (!user) user = nick;
	if (!real) real = nick;

	set(&network.name, host);
	set(&network.chanTypes, "#&");
	set(&network.prefixes, "@+");
	set(&network.prefixModes, "ov");
	set(&network.listModes, "b");
	set(&network.paramModes, "k");
	set(&network.setParamModes, "l");
	set(&network.channelModes, "imnpst");
	set(&self.nick, "*");
	commandComplete();

	FILE *certFile = NULL;
	FILE *privFile = NULL;
	if (cert) {
		certFile = configOpen(cert, "r");
		if (!certFile) return EX_NOINPUT;
	}
	if (priv) {
		privFile = configOpen(priv, "r");
		if (!privFile) return EX_NOINPUT;
	}
	ircConfig(insecure, certFile, privFile);
	if (certFile) fclose(certFile);
	if (privFile) fclose(privFile);

	uiInit();
	if (save) {
		uiLoad(save);
		atexit(exitSave);
	}
	uiShowID(Network);
	uiFormat(
		Network, Cold, NULL,
		"\3%dcatgirl\3\tis GPLv3 fwee softwawe ^w^  "
		"code is avaiwable fwom https://git.causal.agency/catgirl",
		Pink
	);
	uiFormat(Network, Cold, NULL, "Traveling...");
	uiDraw();
	
	int irc = ircConnect(bind, host, port);
	if (pass) ircFormat("PASS :%s\r\n", pass);
	if (sasl) ircFormat("CAP REQ :sasl\r\n");
	ircFormat("CAP LS\r\n");
	ircFormat("NICK :%s\r\n", nick);
	ircFormat("USER %s 0 * :%s\r\n", user, real);

	signal(SIGHUP, signalHandler);
	signal(SIGINT, signalHandler);
	signal(SIGTERM, signalHandler);
	signal(SIGCHLD, signalHandler);
	sig_t cursesWinch = signal(SIGWINCH, signalHandler);

	fcntl(irc, F_SETFD, FD_CLOEXEC);
	if (!self.restricted) {
		int error = pipe(utilPipe);
		if (error) err(EX_OSERR, "pipe");

		error = pipe(execPipe);
		if (error) err(EX_OSERR, "pipe");

		fcntl(utilPipe[0], F_SETFD, FD_CLOEXEC);
		fcntl(utilPipe[1], F_SETFD, FD_CLOEXEC);
		fcntl(execPipe[0], F_SETFD, FD_CLOEXEC);
		fcntl(execPipe[1], F_SETFD, FD_CLOEXEC);
	}

	struct pollfd fds[] = {
		{ .events = POLLIN, .fd = STDIN_FILENO },
		{ .events = POLLIN, .fd = irc },
		{ .events = POLLIN, .fd = utilPipe[0] },
		{ .events = POLLIN, .fd = execPipe[0] },
	};
	while (!self.quit) {
		int nfds = poll(fds, (self.restricted ? 2 : ARRAY_LEN(fds)), -1);
		if (nfds < 0 && errno != EINTR) err(EX_IOERR, "poll");
		if (nfds > 0) {
			if (fds[0].revents) uiRead();
			if (fds[1].revents) ircRecv();
			if (fds[2].revents) utilRead();
			if (fds[3].revents) execRead();
		}

		if (signals[SIGHUP]) self.quit = "zzz";
		if (signals[SIGINT] || signals[SIGTERM]) break;

		if (signals[SIGCHLD]) {
			signals[SIGCHLD] = 0;
			int status;
			while (0 < waitpid(-1, &status, WNOHANG)) {
				if (WIFEXITED(status) && WEXITSTATUS(status)) {
					uiFormat(
						Network, Warm, NULL,
						"Process exits with status %d", WEXITSTATUS(status)
					);
				} else if (WIFSIGNALED(status)) {
					uiFormat(
						Network, Warm, NULL,
						"Process terminates from %s",
						strsignal(WTERMSIG(status))
					);
				}
			}
			uiShow();
		}

		if (signals[SIGWINCH]) {
			signals[SIGWINCH] = 0;
			cursesWinch(SIGWINCH);
			// XXX: For some reason, calling uiDraw() here is the only way to
			// get uiRead() to properly receive KEY_RESIZE.
			uiDraw();
			uiRead();
		}

		uiDraw();
	}

	if (self.quit) {
		ircFormat("QUIT :%s\r\n", self.quit);
	} else {
		ircFormat("QUIT\r\n");
	}
	struct Message msg = {
		.nick = self.nick,
		.user = self.user,
		.cmd = "QUIT",
		.params[0] = self.quit,
	};
	handle(msg);

	ircClose();
	uiHide();
}
