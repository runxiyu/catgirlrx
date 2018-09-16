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
#include <errno.h>
#include <fcntl.h>
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

#include "chat.h"

static int connectRace(const char *host, const char *port) {
	int error;
	struct addrinfo *head;
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM,
		.ai_protocol = IPPROTO_TCP,
	};
	error = getaddrinfo(host, port, &hints, &head);
	if (error) errx(EX_NOHOST, "getaddrinfo: %s", gai_strerror(error));

	nfds_t len = 0;
	enum { SocksLen = 16 };
	struct pollfd socks[SocksLen];
	for (struct addrinfo *ai = head; ai; ai = ai->ai_next) {
		if (len == SocksLen) break;

		socks[len].events = POLLOUT;
		socks[len].fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (socks[len].fd < 0) err(EX_OSERR, "socket");

		error = fcntl(socks[len].fd, F_SETFL, O_NONBLOCK);
		if (error) err(EX_OSERR, "fcntl");

		error = connect(socks[len].fd, ai->ai_addr, ai->ai_addrlen);
		if (error && errno != EINPROGRESS && errno != EINTR) {
			close(socks[len].fd);
			continue;
		}

		len++;
	}
	if (!len) err(EX_UNAVAILABLE, "connect");
	freeaddrinfo(head);

	int ready = poll(socks, len, -1);
	if (ready < 0) err(EX_IOERR, "poll");

	int sock = -1;
	for (nfds_t i = 0; i < len; ++i) {
		if ((socks[i].revents & POLLOUT) && sock < 0) {
			sock = socks[i].fd;
		} else {
			close(socks[i].fd);
		}
	}
	if (sock < 0) errx(EX_UNAVAILABLE, "no socket became writable");

	error = fcntl(sock, F_SETFL, 0);
	if (error) err(EX_IOERR, "fcntl");

	return sock;
}

static struct tls *client;

static void webirc(const char *pass) {
	const char *ssh = getenv("SSH_CLIENT");
	if (!ssh) return;
	int len = strlen(ssh);
	const char *sp = strchr(ssh, ' ');
	if (sp) len = sp - ssh;
	ircFmt(
		"WEBIRC %s %s %.*s %.*s\r\n",
		pass, self.user, len, ssh, len, ssh
	);
}

int ircConnect(
	const char *host, const char *port, const char *pass, const char *webPass
) {
	int error;

	struct tls_config *config = tls_config_new();
	error = tls_config_set_ciphers(config, "compat");
	if (error) errx(EX_SOFTWARE, "tls_config: %s", tls_config_error(config));

	client = tls_client();
	if (!client) errx(EX_SOFTWARE, "tls_client");

	error = tls_configure(client, config);
	if (error) errx(EX_SOFTWARE, "tls_configure");
	tls_config_free(config);

	int sock = connectRace(host, port);

	error = fcntl(sock, F_SETFD, FD_CLOEXEC);
	if (error) err(EX_IOERR, "fcntl");

	error = tls_connect_socket(client, sock, host);
	if (error) errx(EX_PROTOCOL, "tls_connect: %s", tls_error(client));

	if (webPass) webirc(webPass);
	if (pass) ircFmt("PASS :%s\r\n", pass);
	ircFmt("NICK %s\r\n", self.nick);
	ircFmt("USER %s 0 * :%s\r\n", self.user, self.nick);

	return sock;
}

void ircWrite(const char *ptr, size_t len) {
	while (len) {
		ssize_t ret = tls_write(client, ptr, len);
		if (ret == TLS_WANT_POLLIN || ret == TLS_WANT_POLLOUT) continue;
		if (ret < 0) errx(EX_IOERR, "tls_write: %s", tls_error(client));
		ptr += ret;
		len -= ret;
	}
}

void ircFmt(const char *format, ...) {
	char *buf;
	va_list ap;
	va_start(ap, format);
	int len = vasprintf(&buf, format, ap);
	va_end(ap);
	if (!buf) err(EX_OSERR, "vasprintf");
	if (self.verbose) {
		uiFmt(
			TagVerbose, UICold,
			"\3%d<<<\3 %.*s", IRCWhite, len - 2, buf
		);
	}
	ircWrite(buf, len);
	free(buf);
}

void ircRead(void) {
	static char buf[4096];
	static size_t len;

	ssize_t read = tls_read(client, &buf[len], sizeof(buf) - len);
	if (read < 0) errx(EX_IOERR, "tls_read: %s", tls_error(client));
	if (!read) {
		uiExit();
		exit(EX_OK);
	}
	len += read;

	char *crlf, *line = buf;
	while ((crlf = strnstr(line, "\r\n", &buf[len] - line))) {
		crlf[0] = '\0';
		if (self.verbose) {
			uiFmt(
				TagVerbose, UICold,
				"\3%d>>>\3 %s", IRCGray, line
			);
		}
		handle(line);
		line = &crlf[2];
	}

	len -= line - buf;
	memmove(buf, line, len);
}
