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

#include <assert.h>
#include <err.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sysexits.h>
#include <tls.h>
#include <unistd.h>

#include "chat.h"

struct tls *client;

static byte *readFile(size_t *len, FILE *file) {
	struct stat stat;
	int error = fstat(fileno(file), &stat);
	if (error) err(EX_IOERR, "fstat");

	byte *buf = malloc(stat.st_size);
	if (!buf) err(EX_OSERR, "malloc");

	rewind(file);
	*len = fread(buf, 1, stat.st_size, file);
	if (ferror(file)) err(EX_IOERR, "fread");

	return buf;
}

void ircConfig(bool insecure, FILE *cert, FILE *priv) {
	struct tls_config *config = tls_config_new();
	if (!config) errx(EX_SOFTWARE, "tls_config_new");

	int error = tls_config_set_ciphers(config, "compat");
	if (error) {
		errx(
			EX_SOFTWARE, "tls_config_set_ciphers: %s",
			tls_config_error(config)
		);
	}

	if (insecure) {
		tls_config_insecure_noverifycert(config);
		tls_config_insecure_noverifyname(config);
	}

	if (cert) {
		size_t len;
		byte *buf = readFile(&len, cert);
		error = tls_config_set_cert_mem(config, buf, len);
		if (error) {
			errx(
				EX_CONFIG, "tls_config_set_cert_mem: %s",
				tls_config_error(config)
			);
		}
		if (priv) {
			free(buf);
			buf = readFile(&len, priv);
		}
		error = tls_config_set_key_mem(config, buf, len);
		if (error) {
			errx(
				EX_CONFIG, "tls_config_set_key_mem: %s",
				tls_config_error(config)
			);
		}
		explicit_bzero(buf, len);
		free(buf);
	}

	client = tls_client();
	if (!client) errx(EX_SOFTWARE, "tls_client");

	error = tls_configure(client, config);
	if (error) errx(EX_SOFTWARE, "tls_configure: %s", tls_error(client));
	tls_config_free(config);
}

int ircConnect(const char *bindHost, const char *host, const char *port) {
	assert(client);

	int error;
	int sock = -1;
	struct addrinfo *head;
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM,
		.ai_protocol = IPPROTO_TCP,
	};

	if (bindHost) {
		error = getaddrinfo(bindHost, NULL, &hints, &head);
		if (error) errx(EX_NOHOST, "%s: %s", host, gai_strerror(error));

		for (struct addrinfo *ai = head; ai; ai = ai->ai_next) {
			sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
			if (sock < 0) err(EX_OSERR, "socket");

			error = bind(sock, ai->ai_addr, ai->ai_addrlen);
			if (!error) {
				hints.ai_family = ai->ai_family;
				break;
			}

			close(sock);
			sock = -1;
		}
		if (sock < 0) err(EX_UNAVAILABLE, "%s", bindHost);
		freeaddrinfo(head);
	}

	error = getaddrinfo(host, port, &hints, &head);
	if (error) errx(EX_NOHOST, "%s:%s: %s", host, port, gai_strerror(error));

	for (struct addrinfo *ai = head; ai; ai = ai->ai_next) {
		if (sock < 0) {
			sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
			if (sock < 0) err(EX_OSERR, "socket");
		}

		error = connect(sock, ai->ai_addr, ai->ai_addrlen);
		if (!error) break;

		close(sock);
		sock = -1;
	}
	if (sock < 0) err(EX_UNAVAILABLE, "%s:%s", host, port);
	freeaddrinfo(head);

	error = tls_connect_socket(client, sock, host);
	if (error) errx(EX_PROTOCOL, "tls_connect: %s", tls_error(client));

	error = tls_handshake(client);
	if (error) errx(EX_PROTOCOL, "tls_handshake: %s", tls_error(client));

	return sock;
}

static void debug(char dir, const char *line) {
	if (!self.debug) return;
	size_t len = strcspn(line, "\r\n");
	uiFormat(
		Debug, Cold, NULL, "\3%d%c%c\3\t%.*s",
		Gray, dir, dir, (int)len, line
	);
	if (!isatty(STDERR_FILENO)) {
		fprintf(stderr, "%c%c %.*s\n", dir, dir, (int)len, line);
	}
}

void ircSend(const char *ptr, size_t len) {
	assert(client);
	while (len) {
		ssize_t ret = tls_write(client, ptr, len);
		if (ret == TLS_WANT_POLLIN || ret == TLS_WANT_POLLOUT) continue;
		if (ret < 0) errx(EX_IOERR, "tls_write: %s", tls_error(client));
		ptr += ret;
		len -= ret;
	}
}

void ircFormat(const char *format, ...) {
	char buf[1024];
	va_list ap;
	va_start(ap, format);
	int len = vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	assert((size_t)len < sizeof(buf));
	debug('<', buf);
	ircSend(buf, len);
}

static const char *TagNames[TagCap] = {
#define X(name, id) [id] = name,
	ENUM_TAG
#undef X
};

static void unescape(char *tag) {
	for (;;) {
		tag = strchr(tag, '\\');
		if (!tag) break;
		switch (tag[1]) {
			break; case ':': tag[1] = ';';
			break; case 's': tag[1] = ' ';
			break; case 'r': tag[1] = '\r';
			break; case 'n': tag[1] = '\n';
		}
		memmove(tag, &tag[1], strlen(&tag[1]) + 1);
		if (tag[0]) tag = &tag[1];
	}
}

static struct Message parse(char *line) {
	struct Message msg = { .cmd = NULL };

	if (line[0] == '@') {
		char *tags = 1 + strsep(&line, " ");
		while (tags) {
			char *tag = strsep(&tags, ";");
			char *key = strsep(&tag, "=");
			for (size_t i = 0; i < TagCap; ++i) {
				if (strcmp(key, TagNames[i])) continue;
				unescape(tag);
				msg.tags[i] = tag;
				break;
			}
		}
	}

	if (line[0] == ':') {
		char *origin = 1 + strsep(&line, " ");
		msg.nick = strsep(&origin, "!");
		msg.user = strsep(&origin, "@");
		msg.host = origin;
	}

	msg.cmd = strsep(&line, " ");
	for (size_t i = 0; line && i < ParamCap; ++i) {
		if (line[0] == ':') {
			msg.params[i] = &line[1];
			break;
		}
		msg.params[i] = strsep(&line, " ");
	}

	return msg;
}

void ircRecv(void) {
	static char buf[8191 + 512];
	static size_t len = 0;

	assert(client);
	ssize_t ret = tls_read(client, &buf[len], sizeof(buf) - len);
	if (ret == TLS_WANT_POLLIN || ret == TLS_WANT_POLLOUT) return;
	if (ret < 0) errx(EX_IOERR, "tls_read: %s", tls_error(client));
	if (!ret) errx(EX_PROTOCOL, "server closed connection");
	len += ret;

	char *crlf;
	char *line = buf;
	for (;;) {
		crlf = memmem(line, &buf[len] - line, "\r\n", 2);
		if (!crlf) break;
		*crlf = '\0';
		debug('>', line);
		handle(parse(line));
		line = crlf + 2;
	}

	len -= line - buf;
	memmove(buf, line, len);
}

void ircClose(void) {
	int error = tls_close(client);
	if (error) errx(EX_IOERR, "tls_close: %s", tls_error(client));
	tls_free(client);
}
