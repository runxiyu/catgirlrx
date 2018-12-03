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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sysexits.h>
#include <tls.h>
#include <unistd.h>

#include "chat.h"

static struct {
	char *host;
	char *port;
	char *pass;
	char *webirc;
	int sock;
	struct tls_config *config;
	struct tls *client;
} irc = {
	.sock = -1,
};

void ircInit(char *host, char *port, char *pass, char *webirc) {
	irc.host = host;
	irc.port = port;
	irc.pass = pass;
	irc.webirc = webirc;

	irc.config = tls_config_new();
	int error = tls_config_set_ciphers(irc.config, "compat");
	if (error) errx(EX_SOFTWARE, "tls_config");

	irc.client = tls_client();
	if (!irc.client) errx(EX_SOFTWARE, "tls_client");
}

int ircConnect(void) {
	int error;

	tls_reset(irc.client);
	error = tls_configure(irc.client, irc.config);
	if (error) errx(EX_SOFTWARE, "tls_configure: %s", tls_error(irc.client));

	uiFmt(TagStatus, UICold, "Traveling to %s", irc.host);

	struct addrinfo *head;
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM,
		.ai_protocol = IPPROTO_TCP,
	};
	error = getaddrinfo(irc.host, irc.port, &hints, &head);
	if (error) errx(EX_NOHOST, "getaddrinfo: %s", gai_strerror(error));

	for (struct addrinfo *ai = head; ai; ai = ai->ai_next) {
		irc.sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (irc.sock < 0) err(EX_OSERR, "socket");

		error = connect(irc.sock, ai->ai_addr, ai->ai_addrlen);
		if (!error) break;

		close(irc.sock);
		irc.sock = -1;
	}
	if (irc.sock < 0) err(EX_UNAVAILABLE, "connect");
	freeaddrinfo(head);

	error = fcntl(irc.sock, F_SETFD, FD_CLOEXEC);
	if (error) err(EX_IOERR, "fcntl");

	error = tls_connect_socket(irc.client, irc.sock, irc.host);
	if (error) errx(EX_PROTOCOL, "tls_connect: %s", tls_error(irc.client));

	const char *ssh = getenv("SSH_CLIENT");
	if (irc.webirc && ssh) {
		int len = strlen(ssh);
		const char *sp = strchr(ssh, ' ');
		if (sp) len = sp - ssh;
		ircFmt(
			"WEBIRC %s %s %.*s %.*s\r\n",
			irc.webirc, self.user, len, ssh, len, ssh
		);
	}

	/// FIXME
	if (self.user[0] == '~') selfUser(&self.user[1]);

	if (irc.pass) ircFmt("PASS :%s\r\n", irc.pass);
	ircFmt("NICK %s\r\n", self.nick);
	ircFmt("USER %s 0 * :%s\r\n", self.user, self.real);

	return irc.sock;
}

void ircWrite(const char *ptr, size_t len) {
	while (len) {
		ssize_t ret = tls_write(irc.client, ptr, len);
		if (ret == TLS_WANT_POLLIN || ret == TLS_WANT_POLLOUT) continue;
		if (ret < 0) errx(EX_IOERR, "tls_write: %s", tls_error(irc.client));
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

static void disconnect(void) {
	int error = tls_close(irc.client);
	if (error) errx(EX_IOERR, "tls_close: %s", tls_error(irc.client));
	error = close(irc.sock);
	if (error) err(EX_IOERR, "close");
}

bool ircRead(void) {
	static char buf[4096];
	static size_t len;

	ssize_t read = tls_read(irc.client, &buf[len], sizeof(buf) - len);
	if (read < 0) errx(EX_IOERR, "tls_read: %s", tls_error(irc.client));
	if (!read) {
		disconnect();
		len = 0;
		return false;
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
	return true;
}
