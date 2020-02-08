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
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>

#include "chat.h"

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

size_t idNext = Network + 1;

struct Self self = { .color = Default };

static volatile sig_atomic_t signals[NSIG];
static void signalHandler(int signal) {
	signals[signal] = 1;
}

int main(int argc, char *argv[]) {
	setlocale(LC_CTYPE, "");

	bool insecure = false;
	const char *host = NULL;
	const char *port = "6697";
	const char *cert = NULL;
	const char *priv = NULL;

	bool sasl = false;
	const char *pass = NULL;
	const char *nick = NULL;
	const char *user = NULL;
	const char *real = NULL;

	const char *Opts = "!a:c:eh:j:k:n:p:r:u:vw:";
	const struct option LongOpts[] = {
		{ "insecure", no_argument, NULL, '!' },
		{ "sasl-plain", required_argument, NULL, 'a' },
		{ "cert", required_argument, NULL, 'c' },
		{ "sasl-external", no_argument, NULL, 'e' },
		{ "host", required_argument, NULL, 'h' },
		{ "join", required_argument, NULL, 'j' },
		{ "priv", required_argument, NULL, 'k' },
		{ "nick", required_argument, NULL, 'n' },
		{ "port", required_argument, NULL, 'p' },
		{ "real", required_argument, NULL, 'r' },
		{ "user", required_argument, NULL, 'u' },
		{ "debug", no_argument, NULL, 'v' },
		{ "pass", required_argument, NULL, 'w' },
		{0},
	};

	int opt;
	while (0 < (opt = getopt_config(argc, argv, Opts, LongOpts, NULL))) {
		switch (opt) {
			break; case '!': insecure = true;
			break; case 'a': sasl = true; self.plain = optarg;
			break; case 'c': cert = optarg;
			break; case 'e': sasl = true;
			break; case 'h': host = optarg;
			break; case 'j': self.join = optarg;
			break; case 'k': priv = optarg;
			break; case 'n': nick = optarg;
			break; case 'p': port = optarg;
			break; case 'r': real = optarg;
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

	set(&self.network, host);
	set(&self.chanTypes, "#&");
	set(&self.prefixes, "@+");
	commandComplete();

	FILE *certFile = NULL;
	FILE *privFile = NULL;
	if (cert) {
		certFile = configOpen(cert, "r");
		if (!certFile) err(EX_NOINPUT, "%s", cert);
	}
	if (priv) {
		privFile = configOpen(priv, "r");
		if (!privFile) err(EX_NOINPUT, "%s", priv);
	}
	ircConfig(insecure, certFile, privFile);
	if (certFile) fclose(certFile);
	if (privFile) fclose(privFile);

	uiInit();
	uiShowID(Network);
	uiFormat(Network, Cold, NULL, "Traveling...");
	uiDraw();
	
	int irc = ircConnect(host, port);
	if (pass) ircFormat("PASS :%s\r\n", pass);
	if (sasl) ircFormat("CAP REQ :sasl\r\n");
	ircFormat("CAP LS\r\n");
	ircFormat("NICK :%s\r\n", nick);
	ircFormat("USER %s 0 * :%s\r\n", user, real);

	signal(SIGHUP, signalHandler);
	signal(SIGINT, signalHandler);
	signal(SIGTERM, signalHandler);
	sig_t cursesWinch = signal(SIGWINCH, signalHandler);

	struct pollfd fds[2] = {
		{ .events = POLLIN, .fd = STDIN_FILENO },
		{ .events = POLLIN, .fd = irc },
	};
	while (!self.quit) {
		int nfds = poll(fds, 2, -1);
		if (nfds < 0 && errno != EINTR) err(EX_IOERR, "poll");

		if (signals[SIGHUP]) self.quit = "zzz";
		if (signals[SIGINT] || signals[SIGTERM]) break;
		if (signals[SIGWINCH]) {
			signals[SIGWINCH] = 0;
			cursesWinch(SIGWINCH);
			// XXX: For some reason, calling uiDraw() here is the only way to
			// get uiRead() to properly receive KEY_RESIZE.
			uiDraw();
			uiRead();
		}

		if (nfds > 0 && fds[0].revents) uiRead();
		if (nfds > 0 && fds[1].revents) ircRecv();
		uiDraw();
	}

	if (self.quit) {
		ircFormat("QUIT :%s\r\n", self.quit);
	} else {
		ircFormat("QUIT\r\n");
	}
	uiHide();
}
