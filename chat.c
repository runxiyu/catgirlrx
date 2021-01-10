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
 *
 * Additional permission under GNU GPL version 3 section 7:
 *
 * If you modify this Program, or any covered work, by linking or
 * combining it with OpenSSL (or a modified version of that library),
 * containing parts covered by the terms of the OpenSSL License and the
 * original SSLeay license, the licensors of this Program grant you
 * additional permission to convey the resulting work. Corresponding
 * Source for a non-source form of such a combination shall include the
 * source code for the parts of OpenSSL used as well as that of the
 * covered work.
 */

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
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
	char subj[4 + NAME_MAX];
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

struct Network network = { .userLen = 9, .hostLen = 63 };
struct Self self = { .color = Default };

static const char *save;
static void exitSave(void) {
	int error = uiSave(save);
	if (error) {
		warn("%s", save);
		_exit(EX_IOERR);
	}
}

uint execID;
int execPipe[2] = { -1, -1 };
int utilPipe[2] = { -1, -1 };

static void execRead(void) {
	char buf[1024];
	ssize_t len = read(execPipe[0], buf, sizeof(buf) - 1);
	if (len < 0) err(EX_IOERR, "read");
	if (!len) return;
	buf[len] = '\0';
	for (char *ptr = buf; ptr;) {
		char *line = strsep(&ptr, "\r\n");
		if (line[0]) command(execID, line);
	}
}

static void utilRead(void) {
	char buf[1024];
	ssize_t len = read(utilPipe[0], buf, sizeof(buf) - 1);
	if (len < 0) err(EX_IOERR, "read");
	if (!len) return;
	buf[len] = '\0';
	for (char *ptr = buf; ptr;) {
		char *line = strsep(&ptr, "\r\n");
		if (line[0]) uiFormat(Network, Warm, NULL, "%s", line);
	}
}

uint32_t hashInit;
uint32_t hashBound = 75;

static void parseHash(char *str) {
	hashInit = strtoul(str, &str, 0);
	if (*str) hashBound = strtoul(&str[1], NULL, 0);
}

static volatile sig_atomic_t signals[NSIG];
static void signalHandler(int signal) {
	signals[signal] = 1;
}

int main(int argc, char *argv[]) {
	setlocale(LC_CTYPE, "");

#ifdef __OpenBSD__
	int error = pledge("stdio rpath wpath cpath inet dns tty proc exec", NULL);
	if (error) err(EX_OSERR, "pledge");
#endif

	bool insecure = false;
	const char *bind = NULL;
	const char *host = NULL;
	const char *port = "6697";
	const char *chain = NULL;
	const char *trust = NULL;
	const char *cert = NULL;
	const char *priv = NULL;

	bool sasl = false;
	const char *pass = NULL;
	const char *nick = NULL;
	const char *user = NULL;
	const char *real = NULL;

	struct option options[] = {
		{ .val = '!', .name = "insecure", no_argument },
		{ .val = 'C', .name = "copy", required_argument },
		{ .val = 'H', .name = "hash", required_argument },
		{ .val = 'N', .name = "notify", required_argument },
		{ .val = 'O', .name = "open", required_argument },
		{ .val = 'R', .name = "restrict", no_argument },
		{ .val = 'S', .name = "bind", required_argument },
		{ .val = 'a', .name = "sasl-plain", required_argument },
		{ .val = 'c', .name = "cert", required_argument },
		{ .val = 'e', .name = "sasl-external", no_argument },
		{ .val = 'g', .name = "generate", required_argument },
		{ .val = 'h', .name = "host", required_argument },
		{ .val = 'i', .name = "ignore", required_argument },
		{ .val = 'j', .name = "join", required_argument },
		{ .val = 'k', .name = "priv", required_argument },
		{ .val = 'l', .name = "log", no_argument },
		{ .val = 'n', .name = "nick", required_argument },
		{ .val = 'o', .name = "write-chain", required_argument },
		{ .val = 'p', .name = "port", required_argument },
		{ .val = 'r', .name = "real", required_argument },
		{ .val = 's', .name = "save", required_argument },
		{ .val = 't', .name = "trust", required_argument },
		{ .val = 'u', .name = "user", required_argument },
		{ .val = 'v', .name = "debug", no_argument },
		{ .val = 'w', .name = "pass", required_argument },
		{0},
	};
	char opts[2 * ARRAY_LEN(options)];
	for (size_t i = 0, j = 0; i < ARRAY_LEN(options); ++i) {
		opts[j++] = options[i].val;
		if (options[i].has_arg) opts[j++] = ':';
	}

	for (int opt; 0 < (opt = getopt_config(argc, argv, opts, options, NULL));) {
		switch (opt) {
			break; case '!': insecure = true;
			break; case 'C': utilPush(&urlCopyUtil, optarg);
			break; case 'H': parseHash(optarg);
			break; case 'N': utilPush(&uiNotifyUtil, optarg);
			break; case 'O': utilPush(&urlOpenUtil, optarg);
			break; case 'R': self.restricted = true;
			break; case 'S': bind = optarg;
			break; case 'a': sasl = true; self.plain = optarg;
			break; case 'c': cert = optarg;
			break; case 'e': sasl = true;
			break; case 'g': genCert(optarg);
			break; case 'h': host = optarg;
			break; case 'i': ignoreAdd(optarg);
			break; case 'j': self.join = optarg;
			break; case 'k': priv = optarg;
			break; case 'l': logEnable = true;
			break; case 'n': nick = optarg;
			break; case 'o': insecure = true; chain = optarg;
			break; case 'p': port = optarg;
			break; case 'r': real = optarg;
			break; case 's': save = optarg;
			break; case 't': trust = optarg;
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

	// Modes defined in RFC 1459:
	set(&network.chanTypes, "#&");
	set(&network.prefixes, "@+");
	set(&network.prefixModes, "ov");
	set(&network.listModes, "b");
	set(&network.paramModes, "k");
	set(&network.setParamModes, "l");
	set(&network.channelModes, "imnpst");

	set(&network.name, host);
	set(&self.nick, "*");

	editCompleteAdd();
	commandCompleteAdd();

	ircConfig(insecure, trust, cert, priv);

	uiInitEarly();
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
	if (chain) ircWriteChain(chain);
	if (pass) ircFormat("PASS :%s\r\n", pass);
	if (sasl) ircFormat("CAP REQ :sasl\r\n");
	ircFormat("CAP LS\r\n");
	ircFormat("NICK :%s\r\n", nick);
	ircFormat("USER %s 0 * :%s\r\n", user, real);

	uiInitLate();
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
			for (int status; 0 < waitpid(-1, &status, WNOHANG);) {
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
	handle(&msg);

	ircClose();
	logClose();
	uiHide();
}
