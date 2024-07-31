/* Copyright (C) 2020  June McEnroe <june@causal.agency>
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
#include <inttypes.h>
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
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <tls.h>
#include <unistd.h>

#ifdef __FreeBSD__
#include <capsicum_helpers.h>
#endif

char *readpassphrase(const char *prompt, char *buf, size_t bufsiz, int flags);

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
	err(127, "openssl");
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
	int error = uiSave();
	if (error) {
		warn("%s", save);
		_exit(1);
	}
}

uint execID;
int execPipe[2] = { -1, -1 };
int utilPipe[2] = { -1, -1 };

static void execRead(void) {
	char buf[1024];
	ssize_t len = read(execPipe[0], buf, sizeof(buf) - 1);
	if (len < 0) err(1, "read");
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
	if (len < 0) err(1, "read");
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

static void parsePlain(char *str) {
	self.plainUser = strsep(&str, ":");
	if (!str) errx(1, "SASL PLAIN missing colon");
	self.plainPass = str;
}

static volatile sig_atomic_t signals[NSIG];
static void signalHandler(int signal) {
	signals[signal] = 1;
}

static void sandboxEarly(bool log);
static void sandboxLate(int irc);

#if defined __OpenBSD__

static char *promisesInitial;
static char promises[64] = "stdio tty";

static void sandboxEarly(bool log) {
	char *ptr = &promises[strlen(promises)];
	char *end = &promises[sizeof(promises)];

	if (log) {
		char buf[PATH_MAX];
		int error = unveil(dataPath(buf, sizeof(buf), "log", 0), "wc");
		if (error) err(1, "unveil");
		ptr = seprintf(ptr, end, " wpath cpath");
	}

	if (!self.restricted) {
		int error = unveil("/", "x");
		if (error) err(1, "unveil");
		ptr = seprintf(ptr, end, " proc exec");
	}

	promisesInitial = ptr;
	ptr = seprintf(ptr, end, " inet dns");
	int error = pledge(promises, NULL);
	if (error) err(1, "pledge");
}

static void sandboxLate(int irc) {
	(void)irc;
	*promisesInitial = '\0';
	int error = pledge(promises, NULL);
	if (error) err(1, "pledge");
}

#elif defined __FreeBSD__

static void sandboxEarly(bool log) {
	(void)log;
}

static void sandboxLate(int irc) {
	if (!self.restricted) return;

	// Rights are also limited in uiLoad() and logOpen().
	cap_rights_t rights;
	int error = 0
		|| caph_limit_stdin()
		|| caph_rights_limit(
			STDOUT_FILENO, cap_rights_init(&rights, CAP_WRITE, CAP_IOCTL)
		)
		|| caph_limit_stderr()
		|| caph_rights_limit(
			irc, cap_rights_init(&rights, CAP_SEND, CAP_RECV, CAP_EVENT)
		);
	if (error) err(1, "cap_rights_limit");

	// caph_cache_tzdata(3) doesn't load UTC info, which we need for
	// certificate verification. gmtime(3) does.
	caph_cache_tzdata();
	gmtime(&(time_t) { time(NULL) });

	error = cap_enter();
	if (error) err(1, "cap_enter");
}

#else
static void sandboxEarly(bool log) {
	(void)log;
}
static void sandboxLate(int irc) {
	(void)irc;
}
#endif

int main(int argc, char *argv[]) {
	setlocale(LC_CTYPE, "");

	bool insecure = false;
	bool printCert = false;
	const char *bind = NULL;
	const char *host = NULL;
	const char *port = "6697";
	const char *trust = NULL;
	const char *cert = NULL;
	const char *priv = NULL;

	bool log = false;
	bool sasl = false;
	char *pass = NULL;
	const char *user = NULL;
	const char *real = NULL;

	struct option options[] = {
		{ .val = '!', .name = "insecure", no_argument },
		{ .val = 'C', .name = "copy", required_argument },
		{ .val = 'H', .name = "hash", required_argument },
		{ .val = 'I', .name = "highlight", required_argument },
		{ .val = 'N', .name = "notify", required_argument },
		{ .val = 'O', .name = "open", required_argument },
		{ .val = 'R', .name = "restrict", no_argument },
		{ .val = 'S', .name = "bind", required_argument },
		{ .val = 'T', .name = "timestamp", optional_argument },
		{ .val = 'a', .name = "sasl-plain", required_argument },
		{ .val = 'c', .name = "cert", required_argument },
		{ .val = 'e', .name = "sasl-external", no_argument },
		{ .val = 'g', .name = "generate", required_argument },
		{ .val = 'h', .name = "host", required_argument },
		{ .val = 'i', .name = "ignore", required_argument },
		{ .val = 'j', .name = "join", required_argument },
		{ .val = 'k', .name = "priv", required_argument },
		{ .val = 'l', .name = "log", no_argument },
		{ .val = 'm', .name = "mode", required_argument },
		{ .val = 'n', .name = "nick", required_argument },
		{ .val = 'o', .name = "print-chain", no_argument },
		{ .val = 'p', .name = "port", required_argument },
		{ .val = 'q', .name = "quiet", no_argument },
		{ .val = 'r', .name = "real", required_argument },
		{ .val = 's', .name = "save", required_argument },
		{ .val = 't', .name = "trust", required_argument },
		{ .val = 'u', .name = "user", required_argument },
		{ .val = 'v', .name = "debug", no_argument },
		{ .val = 'w', .name = "pass", required_argument },
		{0},
	};
	char opts[3 * ARRAY_LEN(options)];
	for (size_t i = 0, j = 0; i < ARRAY_LEN(options); ++i) {
		opts[j++] = options[i].val;
		if (options[i].has_arg != no_argument) opts[j++] = ':';
		if (options[i].has_arg == optional_argument) opts[j++] = ':';
	}

	for (int opt; 0 < (opt = getopt_config(argc, argv, opts, options, NULL));) {
		switch (opt) {
			break; case '!': insecure = true;
			break; case 'C': utilPush(&urlCopyUtil, optarg);
			break; case 'H': parseHash(optarg);
			break; case 'I': filterAdd(Hot, optarg);
			break; case 'N': utilPush(&uiNotifyUtil, optarg);
			break; case 'O': utilPush(&urlOpenUtil, optarg);
			break; case 'R': self.restricted = true;
			break; case 'S': bind = optarg;
			break; case 'T': {
				windowTime.enable = true;
				if (optarg) windowTime.format = optarg;
			}
			break; case 'a': sasl = true; parsePlain(optarg);
			break; case 'c': cert = optarg;
			break; case 'e': sasl = true;
			break; case 'g': genCert(optarg);
			break; case 'h': host = optarg;
			break; case 'i': filterAdd(Ice, optarg);
			break; case 'j': self.join = optarg;
			break; case 'k': priv = optarg;
			break; case 'l': log = true; logOpen();
			break; case 'm': self.mode = optarg;
			break; case 'n': {
				for (uint i = 0; i < ARRAY_LEN(self.nicks); ++i) {
					self.nicks[i] = strsep(&optarg, " ");
				}
			}
			break; case 'o': printCert = true;
			break; case 'p': port = optarg;
			break; case 'q': windowThreshold = Warm;
			break; case 'r': real = optarg;
			break; case 's': save = optarg;
			break; case 't': trust = optarg;
			break; case 'u': user = optarg;
			break; case 'v': self.debug = true;
			break; case 'w': pass = optarg;
			break; default:  return 1;
		}
	}
	if (!host) errx(1, "host required");

	if (printCert) {
#ifdef __OpenBSD__
		int error = pledge("stdio inet dns", NULL);
		if (error) err(1, "pledge");
#endif
		ircConfig(true, NULL, NULL, NULL);
		ircConnect(bind, host, port);
		ircPrintCert();
		ircClose();
		return 0;
	}

	if (!self.nicks[0]) self.nicks[0] = getenv("USER");
	if (!self.nicks[0]) errx(1, "USER unset");
	if (!user) user = self.nicks[0];
	if (!real) real = self.nicks[0];

	if (pass && !pass[0]) {
		char *buf = malloc(512);
		if (!buf) err(1, "malloc");
		pass = readpassphrase("Server password: ", buf, 512, 0);
		if (!pass) errx(1, "unable to read passphrase");
	}

	if (self.plainPass && !self.plainPass[0]) {
		char *buf = malloc(512);
		if (!buf) err(1, "malloc");
		self.plainPass = readpassphrase("Account password: ", buf, 512, 0);
		if (!self.plainPass) errx(1, "unable to read passphrase");
	}

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

	inputCompletion();

	ircConfig(insecure, trust, cert, priv);

	uiInit();
	sig_t cursesWinch = signal(SIGWINCH, signalHandler);
	if (save) {
		uiLoad(save);
		atexit(exitSave);
	}
	windowShow(windowFor(Network));
	uiFormat(
		Network, Cold, NULL,
		"\3%dcatgirlrx\3\tis free software; you can distribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. "
		"This is a soft fork of June's original catgirl. "
		"Code is available fwom https://git.sr.ht/~runxiyu/catgirlrx.",
		Pink
	);
	uiFormat(Network, Cold, NULL, "Traveling...");
	uiDraw();

	sandboxEarly(log);
	int irc = ircConnect(bind, host, port);
	sandboxLate(irc);

	ircHandshake();
	if (pass) {
		ircFormat("PASS :");
		ircSend(pass, strlen(pass));
		ircFormat("\r\n");
		explicit_bzero(pass, strlen(pass));
	}
	if (sasl) ircFormat("CAP REQ :sasl\r\n");
	ircFormat("CAP LS\r\n");
	ircFormat("NICK %s\r\n", self.nicks[0]);
	ircFormat("USER %s 0 * :%s\r\n", user, real);

	// Avoid disabling VINTR until main loop.
	inputInit();
	signal(SIGHUP, signalHandler);
	signal(SIGINT, signalHandler);
	signal(SIGALRM, signalHandler);
	signal(SIGTERM, signalHandler);
	signal(SIGCHLD, signalHandler);

	if (!self.restricted) {
		int error = pipe(utilPipe) || pipe(execPipe);
		if (error) err(1, "pipe");

		fcntl(utilPipe[0], F_SETFD, FD_CLOEXEC);
		fcntl(utilPipe[1], F_SETFD, FD_CLOEXEC);
		fcntl(execPipe[0], F_SETFD, FD_CLOEXEC);
		fcntl(execPipe[1], F_SETFD, FD_CLOEXEC);
	}

	bool ping = false;
	struct pollfd fds[] = {
		{ .events = POLLIN, .fd = STDIN_FILENO },
		{ .events = POLLIN, .fd = irc },
		{ .events = POLLIN, .fd = utilPipe[0] },
		{ .events = POLLIN, .fd = execPipe[0] },
	};
	while (!self.quit) {
		int nfds = poll(fds, (self.restricted ? 2 : ARRAY_LEN(fds)), -1);
		if (nfds < 0 && errno != EINTR) err(1, "poll");
		if (nfds > 0) {
			if (fds[0].revents) inputRead();
			if (fds[1].revents) ircRecv();
			if (fds[2].revents) utilRead();
			if (fds[3].revents) execRead();
		}

		if (signals[SIGHUP]) self.quit = "zzz";
		if (signals[SIGINT] || signals[SIGTERM]) break;

		if (nfds > 0 && fds[1].revents) {
			ping = false;
			struct itimerval timer = {
				.it_value.tv_sec = 2 * 60,
				.it_interval.tv_sec = 30,
			};
			int error = setitimer(ITIMER_REAL, &timer, NULL);
			if (error) err(1, "setitimer");
		}
		if (signals[SIGALRM]) {
			signals[SIGALRM] = 0;
			if (ping) {
				errx(69, "ping timeout");
			} else {
				ircFormat("PING nyaa\r\n");
				ping = true;
			}
		}

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
			// doupdate(3) needs to be called for KEY_RESIZE to be picked up.
			uiDraw();
			inputRead();
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
