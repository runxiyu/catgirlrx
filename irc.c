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
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sysexits.h>
#include <tls.h>
#include <unistd.h>

#include "chat.h"

static struct tls *client;

static void webirc(const char *pass) {
	const char *ssh = getenv("SSH_CLIENT");
	if (!ssh) return;
	int len = strlen(ssh);
	const char *sp = strchr(ssh, ' ');
	if (sp) len = sp - ssh;
	ircFmt(
		"WEBIRC %s %s %.*s %.*s\r\n",
		pass, chat.user, len, ssh, len, ssh
	);
}

int ircConnect(const char *host, const char *port, const char *webPass) {
	int error;

	struct tls_config *config = tls_config_new();
	error = tls_config_set_ciphers(config, "compat");
	if (error) errx(EX_SOFTWARE, "tls_config: %s", tls_config_error(config));

	client = tls_client();
	if (!client) errx(EX_SOFTWARE, "tls_client");

	error = tls_configure(client, config);
	if (error) errx(EX_SOFTWARE, "tls_configure");
	tls_config_free(config);

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

	error = fcntl(sock, F_SETFD, FD_CLOEXEC);
	if (error) err(EX_IOERR, "fcntl");

	error = connect(sock, ai->ai_addr, ai->ai_addrlen);
	if (error) err(EX_UNAVAILABLE, "connect");
	freeaddrinfo(ai);

	error = tls_connect_socket(client, sock, host);
	if (error) err(EX_PROTOCOL, "tls_connect");

	if (webPass) webirc(webPass);
	ircFmt("NICK %s\r\n", chat.nick);
	ircFmt("USER %s 0 * :%s\r\n", chat.user, chat.nick);

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
	if (chat.verbose) uiFmt("<<< %.*s", len - 2, buf);
	ircWrite(buf, len);
	free(buf);
}

static char buf[4096];
static size_t len;

void ircRead(void) {
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
		if (chat.verbose) uiFmt(">>> %s", line);
		handle(line);
		line = &crlf[2];
	}

	len -= line - buf;
	memmove(buf, line, len);
}
