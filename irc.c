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

int ircConnect(void) {
	int error;

	struct tls_config *config = tls_config_new();
	error = tls_config_set_ciphers(config, "compat");
	if (error) errx(EX_SOFTWARE, "tls_config");

	client = tls_client();
	if (!client) errx(EX_SOFTWARE, "tls_client");

	error = tls_configure(client, config);
	if (error) errx(EX_SOFTWARE, "tls_configure: %s", tls_error(client));
	tls_config_free(config);

	struct addrinfo *head;
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM,
		.ai_protocol = IPPROTO_TCP,
	};
	error = getaddrinfo(self.host, self.port, &hints, &head);
	if (error) errx(EX_NOHOST, "getaddrinfo: %s", gai_strerror(error));

	int sock = -1;
	for (struct addrinfo *ai = head; ai; ai = ai->ai_next) {
		sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (sock < 0) err(EX_OSERR, "socket");

		error = connect(sock, ai->ai_addr, ai->ai_addrlen);
		if (!error) break;

		close(sock);
		sock = -1;
	}
	if (sock < 0) err(EX_UNAVAILABLE, "connect");
	freeaddrinfo(head);

	error = fcntl(sock, F_SETFD, FD_CLOEXEC);
	if (error) err(EX_IOERR, "fcntl");

	error = tls_connect_socket(client, sock, self.host);
	if (error) errx(EX_PROTOCOL, "tls_connect: %s", tls_error(client));

	const char *ssh = getenv("SSH_CLIENT");
	if (self.webp && ssh) {
		int len = strlen(ssh);
		const char *sp = strchr(ssh, ' ');
		if (sp) len = sp - ssh;
		ircFmt(
			"WEBIRC %s %s %.*s %.*s\r\n",
			self.webp, self.user, len, ssh, len, ssh
		);
	}

	if (self.auth) ircFmt("CAP REQ :sasl\r\n");
	if (self.pass) ircFmt("PASS :%s\r\n", self.pass);
	ircFmt("NICK %s\r\n", self.nick);
	ircFmt("USER %s 0 * :%s\r\n", self.user, self.real);

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
	int len =  vasprintf(&buf, format, ap);
	va_end(ap);
	if (!buf) err(EX_OSERR, "vasprintf");
	if (self.raw) {
		uiFmt(TagRaw, UICold, "\3%d<<<\3 %.*s", IRCWhite, len - 2, buf);
	}
	ircWrite(buf, len);
	free(buf);
}

void ircQuit(const char *mesg) {
	ircFmt("QUIT :%s\r\n", mesg);
	self.quit = true;
}

void ircRead(void) {
	static char buf[4096];
	static size_t len;

	ssize_t read;
retry:
	read = tls_read(client, &buf[len], sizeof(buf) - len);
	if (read == TLS_WANT_POLLIN || read == TLS_WANT_POLLOUT) goto retry;
	if (read < 0) errx(EX_IOERR, "tls_read: %s", tls_error(client));
	if (!read) {
		if (!self.quit) errx(EX_PROTOCOL, "unexpected eof");
		uiExit(EX_OK);
	}
	len += read;

	char *crlf;
	char *line = buf;
	while (NULL != (crlf = memmem(line, &buf[len] - line, "\r\n", 2))) {
		crlf[0] = '\0';
		if (self.raw) {
			uiFmt(TagRaw, UICold, "\3%d>>>\3 %s", IRCGray, line);
		}
		handle(line);
		line = &crlf[2];
	}

	len -= line - buf;
	memmove(buf, line, len);
}
