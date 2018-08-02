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

#include <curses.h>
#include <err.h>
#include <locale.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sysexits.h>
#include <tls.h>
#include <unistd.h>

static void curse(void) {
	setlocale(LC_CTYPE, "");
	initscr();
	keypad(stdscr, true);
	start_color();
	assume_default_colors(-1, -1);
}

static void writeAll(struct tls *client, const char *ptr, size_t len) {
	while (len) {
		ssize_t ret = tls_write(client, ptr, len);
		if (ret == TLS_WANT_POLLIN || ret == TLS_WANT_POLLOUT) continue;
		if (ret < 0) errx(EX_IOERR, "tls_write: %s", tls_error(client));
		ptr += ret;
		len -= ret;
	}
}

static void command(struct tls *client, const char *format, ...) {
	char *buf;
	va_list ap;
	va_start(ap, format);
	int len = vasprintf(&buf, format, ap);
	va_end(ap);
	if (!buf) err(EX_OSERR, "vasprintf");
	writeAll(client, buf, len);
	free(buf);
}

static void handle(struct tls *client, const char *line) {
	addstr(line);
	addch('\n');
	refresh();
}

static void readClient(struct tls *client) {
	static char buf[4096];
	static size_t fill;

	ssize_t size = tls_read(client, buf + fill, sizeof(buf) - fill);
	if (size < 0) errx(EX_IOERR, "tls_read: %s", tls_error(client));
	fill += size;

	char *end;
	char *line = buf;
	while ((end = strnstr(line, "\r\n", buf + fill - line))) {
		end[0] = '\0';
		handle(client, line);
		line = &end[2];
	}

	fill -= line - buf;
	memmove(buf, line, fill);
}

static void readInput(void) {
}

static void webirc(struct tls *client, const char *pass, const char *user) {
	const char *ssh = getenv("SSH_CLIENT");
	if (!ssh) return;
	int len = strchrnul(ssh, ' ') - ssh;
	command(client, "WEBIRC %s %s %.*s %.*s\r\n", pass, user, len, ssh, len, ssh);
}

int main(int argc, char *argv[]) {
	int error;

	const char *host = NULL;
	const char *port = "6697";
	const char *join = NULL;
	const char *nick = NULL;
	const char *webPass = NULL;

	int opt;
	while (0 < (opt = getopt(argc, argv, "h:j:n:p:w:"))) {
		switch (opt) {
			break; case 'h': host = optarg;
			break; case 'j': join = optarg;
			break; case 'n': nick = optarg;
			break; case 'p': port = optarg;
			break; case 'w': webPass = optarg;
			break; default:  return EX_USAGE;
		}
	}

	curse();

	char hostBuf[64] = {0};
	char joinBuf[64] = {0};
	char nickBuf[16] = {0};
	if (!host) {
		addstr("Host: ");
		getnstr(hostBuf, sizeof(hostBuf) - 1);
		host = hostBuf;
	}
	if (!join) {
		addstr("Join: ");
		getnstr(joinBuf, sizeof(joinBuf) - 1);
		join = joinBuf;
	}
	if (!nick) {
		addstr("Nick: ");
		getnstr(nickBuf, sizeof(nickBuf) - 1);
		nick = nickBuf;
	}

	struct addrinfo *ai;
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM,
		.ai_protocol = IPPROTO_TCP,
	};
	error = getaddrinfo(host, port, &hints, &ai);
	if (error) errx(EX_NOHOST, "getaddrinfo: %s", gai_strerror(error));

	int sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
	if (sock < 0) err(EX_OSERR, "socket");

	error = connect(sock, ai->ai_addr, ai->ai_addrlen);
	if (error) err(EX_UNAVAILABLE, "connect");
	freeaddrinfo(ai);

	struct tls *client = tls_client();
	if (!client) errx(EX_OSERR, "tls_client");

	struct tls_config *config = tls_config_new();
	error = tls_configure(client, config);
	if (error) errx(EX_OSERR, "tls_configure");
	tls_config_free(config);

	error = tls_connect_socket(client, sock, host);
	if (error) err(EX_PROTOCOL, "tls_connect");

	if (webPass) webirc(client, webPass, nick);
	command(client, "NICK %s\r\n", nick);
	command(client, "USER %s x x :%s\r\n", nick, nick);

	struct pollfd fds[2] = {
		{ .fd = STDIN_FILENO, .events = POLLIN },
		{ .fd = sock, .events = POLLIN },
	};
	while (0 < poll(fds, 2, -1)) {
		if (fds[0].revents) readInput();
		if (fds[1].revents) readClient(client);
	}
	err(EX_IOERR, "poll");
}
