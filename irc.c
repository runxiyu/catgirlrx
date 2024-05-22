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

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <tls.h>
#include <unistd.h>

#include "chat.h"

static struct tls *client;
static struct tls_config *config;

void ircConfig(
	bool insecure, const char *trust, const char *cert, const char *priv
) {
	int error = 0;
	char buf[PATH_MAX];

	config = tls_config_new();
	if (!config) errx(1, "tls_config_new");

	if (insecure) {
		tls_config_insecure_noverifycert(config);
		tls_config_insecure_noverifyname(config);
	}
	if (trust) {
		tls_config_insecure_noverifyname(config);
		for (int i = 0; configPath(buf, sizeof(buf), trust, i); ++i) {
			error = tls_config_set_ca_file(config, buf);
			if (!error) break;
		}
		if (error) errx(1, "%s: %s", trust, tls_config_error(config));
	}

	// Explicitly load the default CA cert file on OpenBSD now so it doesn't
	// need to be unveiled. Other systems might use a CA directory, so avoid
	// changing the default behavior.
#ifdef __OpenBSD__
	if (!insecure && !trust) {
		const char *ca = tls_default_ca_cert_file();
		error = tls_config_set_ca_file(config, ca);
		if (error) errx(1, "%s: %s", ca, tls_config_error(config));
	}
#endif

	if (cert) {
		for (int i = 0; configPath(buf, sizeof(buf), cert, i); ++i) {
			if (priv) {
				error = tls_config_set_cert_file(config, buf);
			} else {
				error = tls_config_set_keypair_file(config, buf, buf);
			}
			if (!error) break;
		}
		if (error) errx(1, "%s: %s", cert, tls_config_error(config));
	}
	if (priv) {
		for (int i = 0; configPath(buf, sizeof(buf), priv, i); ++i) {
			error = tls_config_set_key_file(config, buf);
			if (!error) break;
		}
		if (error) errx(1, "%s: %s", priv, tls_config_error(config));
	}

	client = tls_client();
	if (!client) errx(1, "tls_client");

	error = tls_configure(client, config);
	if (error) errx(1, "tls_configure: %s", tls_error(client));
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
		if (error) errx(1, "%s: %s", bindHost, gai_strerror(error));

		for (struct addrinfo *ai = head; ai; ai = ai->ai_next) {
			sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
			if (sock < 0) err(1, "socket");

			error = bind(sock, ai->ai_addr, ai->ai_addrlen);
			if (!error) {
				hints.ai_family = ai->ai_family;
				break;
			}

			close(sock);
			sock = -1;
		}
		if (sock < 0) err(1, "%s", bindHost);
		freeaddrinfo(head);
	}

	error = getaddrinfo(host, port, &hints, &head);
	if (error) errx(1, "%s:%s: %s", host, port, gai_strerror(error));

	for (struct addrinfo *ai = head; ai; ai = ai->ai_next) {
		if (sock < 0) {
			sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
			if (sock < 0) err(1, "socket");
		}

		error = connect(sock, ai->ai_addr, ai->ai_addrlen);
		if (!error) break;
		if (error && errno == EINTR) break; // connect continues asynchronously

		close(sock);
		sock = -1;
	}
	if (sock < 0) err(69, "%s:%s", host, port);
	freeaddrinfo(head);

	fcntl(sock, F_SETFD, FD_CLOEXEC);
	error = tls_connect_socket(client, sock, host);
	if (error) errx(1, "tls_connect: %s", tls_error(client));

	return sock;
}

void ircHandshake(void) {
	int error;
	do {
		error = tls_handshake(client);
	} while (error == TLS_WANT_POLLIN || error == TLS_WANT_POLLOUT);
	if (error) errx(1, "tls_handshake: %s", tls_error(client));

	tls_config_clear_keys(config);
}

void ircPrintCert(void) {
	size_t len;
	ircHandshake();
	const byte *pem = tls_peer_cert_chain_pem(client, &len);
	printf("subject= %s\n", tls_peer_cert_subject(client));
	fwrite(pem, len, 1, stdout);
}

enum { MessageCap = 8191 + 512 };

static void debug(const char *pre, const char *line) {
	if (!self.debug) return;
	size_t len = strcspn(line, "\r\n");
	uiFormat(
		Debug, Cold, NULL, "\3%02d%s\3\t%.*s",
		Gray, pre, (int)len, line
	);
	if (!isatty(STDERR_FILENO)) {
		fprintf(stderr, "%s %.*s\n", pre, (int)len, line);
	}
}

void ircSend(const char *ptr, size_t len) {
	assert(client);
	while (len) {
		ssize_t ret = tls_write(client, ptr, len);
		if (ret == TLS_WANT_POLLIN || ret == TLS_WANT_POLLOUT) continue;
		if (ret < 0) errx(1, "tls_write: %s", tls_error(client));
		ptr += ret;
		len -= ret;
	}
}

void ircFormat(const char *format, ...) {
	char buf[MessageCap];
	va_list ap;
	va_start(ap, format);
	int len = vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	assert((size_t)len < sizeof(buf));
	debug("<<", buf);
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
			for (uint i = 0; i < TagCap; ++i) {
				if (strcmp(key, TagNames[i])) continue;
				if (tag) {
					unescape(tag);
					msg.tags[i] = tag;
				} else {
					msg.tags[i] = "";
				}
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
	for (uint i = 0; line && i < ParamCap; ++i) {
		if (line[0] == ':') {
			msg.params[i] = &line[1];
			break;
		}
		msg.params[i] = strsep(&line, " ");
	}

	return msg;
}

void ircRecv(void) {
	static char buf[MessageCap];
	static size_t len = 0;

	assert(client);
	ssize_t ret = tls_read(client, &buf[len], sizeof(buf) - len);
	if (ret == TLS_WANT_POLLIN || ret == TLS_WANT_POLLOUT) return;
	if (ret < 0) errx(1, "tls_read: %s", tls_error(client));
	if (!ret) errx(69, "server closed connection");
	len += ret;

	char *crlf;
	char *line = buf;
	for (;;) {
		crlf = memmem(line, &buf[len] - line, "\r\n", 2);
		if (!crlf) break;
		*crlf = '\0';
		debug(">>", line);
		struct Message msg = parse(line);
		handle(&msg);
		line = crlf + 2;
	}

	len -= line - buf;
	memmove(buf, line, len);
}

void ircClose(void) {
	tls_close(client);
	tls_free(client);
}
