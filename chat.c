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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sysexits.h>
#include <tls.h>
#include <unistd.h>

#define err(...) do { endwin(); err(__VA_ARGS__); } while (0)
#define errx(...) do { endwin(); errx(__VA_ARGS__); } while (0)

static void curse(void) {
	setlocale(LC_CTYPE, "");
	initscr();
	keypad(stdscr, true);
	start_color();
	assume_default_colors(-1, -1);
}

static const int CHAT_LINES = 100;
static struct {
	WINDOW *topic;
	WINDOW *chat;
	WINDOW *input;
} ui;

static void uiInit(void) {
	ui.topic = newwin(2, COLS, 0, 0);
	mvwhline(ui.topic, 1, 0, ACS_HLINE, COLS);

	ui.chat = newpad(CHAT_LINES, COLS);
	wsetscrreg(ui.chat, 0, CHAT_LINES - 1);
	scrollok(ui.chat, true);
	wmove(ui.chat, CHAT_LINES - (LINES - 4) - 1, 0);

	ui.input = newwin(2, COLS, LINES - 2, 0);
	mvwhline(ui.input, 0, 0, ACS_HLINE, COLS);
	wmove(ui.input, 1, 0);
}

static void uiDraw(void) {
	wnoutrefresh(ui.topic);
	pnoutrefresh(
		ui.chat,
		CHAT_LINES - (LINES - 4), 0,
		2, 0, LINES - 1, COLS - 1
	);
	wnoutrefresh(ui.input);
	doupdate();
}

static void uiTopic(const char *topic) {
	wmove(ui.topic, 0, 0);
	wclrtoeol(ui.topic);
	waddnstr(ui.topic, topic, COLS);
}
static void uiChat(const char *line) {
	waddch(ui.chat, '\n');
	waddstr(ui.chat, line);
}
static void uiFmt(const char *format, ...) {
	char *buf;
	va_list ap;
	va_start(ap, format);
	vasprintf(&buf, format, ap);
	va_end(ap);
	if (!buf) err(EX_OSERR, "vasprintf");
	uiChat(buf);
	free(buf);
}

static struct {
	int sock;
	struct tls *tls;
	bool verbose;
	char *nick;
	char *chan;
} client;

static void clientWrite(const char *ptr, size_t len) {
	while (len) {
		ssize_t ret = tls_write(client.tls, ptr, len);
		if (ret == TLS_WANT_POLLIN || ret == TLS_WANT_POLLOUT) continue;
		if (ret < 0) errx(EX_IOERR, "tls_write: %s", tls_error(client.tls));
		ptr += ret;
		len -= ret;
	}
}
static void clientFmt(const char *format, ...) {
	char *buf;
	va_list ap;
	va_start(ap, format);
	int len = vasprintf(&buf, format, ap);
	va_end(ap);
	if (!buf) err(EX_OSERR, "vasprintf");
	if (client.verbose) uiFmt("<<< %.*s", len - 2, buf);
	clientWrite(buf, len);
	free(buf);
}

typedef void (*Handler)(char *prefix, char *params);

static char *shift(char **params) {
	char *rest = *params;
	if (!rest) errx(EX_PROTOCOL, "expected param");
	if (rest[0] == ':') {
		*params = NULL;
		return &rest[1];
	}
	return strsep(params, " ");
}

static void handle001(char *prefix, char *params) {
	(void)prefix; (void)params;
	clientFmt("JOIN %s\r\n", client.chan);
}

static void handlePing(char *prefix, char *params) {
	(void)prefix;
	clientFmt("PONG %s\r\n", params);
}

static void handleJoin(char *prefix, char *params) {
	char *nick = strsep(&prefix, "!");
	char *chan = shift(&params);
	uiFmt("--> %s arrived in %s", nick, chan);
}
static void handlePart(char *prefix, char *params) {
	char *nick = strsep(&prefix, "!");
	char *chan = shift(&params);
	char *reason = shift(&params);
	uiFmt("<-- %s left %s, \"%s\"", nick, chan, reason);
}
static void handleQuit(char *prefix, char *params) {
	char *nick = strsep(&prefix, "!");
	char *reason = shift(&params);
	uiFmt("<-- %s left, \"%s\"", nick, reason);
}

static void handle332(char *prefix, char *params) {
	(void)prefix;
	shift(&params);
	char *chan = shift(&params);
	char *topic = shift(&params);
	uiTopic(topic);
	uiFmt("--- The sign in %s reads, \"%s\"", chan, topic);
}
static void handleTopic(char *prefix, char *params) {
	char *nick = strsep(&prefix, "!");
	char *chan = shift(&params);
	char *topic = shift(&params);
	uiTopic(topic);
	uiFmt("--- %s placed a new sign in %s, \"%s\"", nick, chan, topic);
}

static void handle353(char *prefix, char *params) {
	(void)prefix;
	shift(&params);
	shift(&params);
	char *chan = shift(&params);
	char *names = shift(&params);
	// TODO: Clean up names (add commas, remove sigils)
	uiFmt("--- In %s are %s", chan, names);
}

static void handlePrivmsg(char *prefix, char *params) {
	char *nick = strsep(&prefix, "!");
	shift(&params);
	char *message = shift(&params);
	uiFmt("<%s> %s", nick, message);
}
static void handleNotice(char *prefix, char *params) {
	char *nick = strsep(&prefix, "!");
	shift(&params);
	char *message = shift(&params);
	uiFmt("-%s- %s", nick, message);
}

static const struct {
	const char *command;
	Handler handler;
} HANDLERS[] = {
	{ "001", handle001 },
	{ "332", handle332 },
	{ "353", handle353 },
	{ "JOIN", handleJoin },
	{ "NOTICE", handleNotice },
	{ "PART", handlePart },
	{ "PING", handlePing },
	{ "PRIVMSG", handlePrivmsg },
	{ "QUIT", handleQuit },
	{ "TOPIC", handleTopic },
};
static const size_t HANDLERS_LEN = sizeof(HANDLERS) / sizeof(HANDLERS[0]);

static void handle(char *line) {
	char *prefix = NULL;
	if (line[0] == ':') {
		prefix = strsep(&line, " ") + 1;
		if (!line) errx(EX_PROTOCOL, "eol after prefix");
	}
	char *command = strsep(&line, " ");
	for (size_t i = 0; i < HANDLERS_LEN; ++i) {
		if (strcmp(command, HANDLERS[i].command)) continue;
		HANDLERS[i].handler(prefix, line);
		break;
	}
}

static void clientRead(void) {
	static char buf[4096];
	static size_t fill;

	ssize_t size = tls_read(client.tls, buf + fill, sizeof(buf) - fill);
	if (size < 0) errx(EX_IOERR, "tls_read: %s", tls_error(client.tls));
	fill += size;

	char *end, *line = buf;
	while ((end = strnstr(line, "\r\n", buf + fill - line))) {
		end[0] = '\0';
		if (client.verbose) uiFmt(">>> %s", line);
		handle(line);
		line = &end[2];
	}

	fill -= line - buf;
	memmove(buf, line, fill);
}

static void uiRead(void) {
	static char buf[256];
	static size_t fill;

	// TODO:
	int ch = wgetch(ui.input);
	if (ch == '\n') {
		buf[fill] = '\0';
		char *params;
		asprintf(&params, "%s :%s", client.chan, buf);
		if (!params) err(EX_OSERR, "asprintf");
		clientFmt("PRIVMSG %s\r\n", params);
		handlePrivmsg(client.nick, params);
		free(params);
		fill = 0;
		wmove(ui.input, 1, 0);
		wclrtoeol(ui.input);
	} else {
		buf[fill++] = ch;
		waddch(ui.input, ch);
	}
}

static void webirc(const char *pass) {
	const char *ssh = getenv("SSH_CLIENT");
	if (!ssh) return;
	int len = strchrnul(ssh, ' ') - ssh;
	clientFmt("WEBIRC %s %s %.*s %.*s\r\n", pass, client.nick, len, ssh, len, ssh);
}

int main(int argc, char *argv[]) {
	int error;

	const char *host = NULL;
	const char *port = "6697";
	const char *webPass = NULL;

	int opt;
	while (0 < (opt = getopt(argc, argv, "h:j:n:p:vw:"))) {
		switch (opt) {
			break; case 'h': host = optarg;
			break; case 'j': client.chan = strdup(optarg);
			break; case 'n': client.nick = strdup(optarg);
			break; case 'p': port = optarg;
			break; case 'v': client.verbose = true;
			break; case 'w': webPass = optarg;
			break; default:  return EX_USAGE;
		}
	}

	curse();

	char hostBuf[64] = {0};
	if (!host) {
		addstr("Host: ");
		getnstr(hostBuf, sizeof(hostBuf) - 1);
		host = hostBuf;
	}
	if (!client.chan) {
		char buf[64] = {0};
		addstr("Join: ");
		getnstr(buf, sizeof(buf) - 1);
		client.chan = strdup(buf);
	}
	if (!client.nick) {
		char buf[16] = {0};
		addstr("Name: ");
		getnstr(buf, sizeof(buf) - 1);
		client.nick = strdup(buf);
	}
	erase();
	cbreak();
	noecho();

	uiInit();
	uiChat("=== Traveling...");
	uiDraw();

	struct addrinfo *ai;
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM,
		.ai_protocol = IPPROTO_TCP,
	};
	error = getaddrinfo(host, port, &hints, &ai);
	if (error) errx(EX_NOHOST, "getaddrinfo: %s", gai_strerror(error));

	client.sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
	if (client.sock < 0) err(EX_OSERR, "socket");

	error = connect(client.sock, ai->ai_addr, ai->ai_addrlen);
	if (error) err(EX_UNAVAILABLE, "connect");
	freeaddrinfo(ai);

	client.tls = tls_client();
	if (!client.tls) errx(EX_OSERR, "tls_client");

	struct tls_config *config = tls_config_new();
	error = tls_configure(client.tls, config);
	if (error) errx(EX_OSERR, "tls_configure");
	tls_config_free(config);

	error = tls_connect_socket(client.tls, client.sock, host);
	if (error) err(EX_PROTOCOL, "tls_connect");

	if (webPass) webirc(webPass);
	clientFmt("NICK %s\r\n", client.nick);
	clientFmt("USER %s x x :%s\r\n", client.nick, client.nick);

	struct pollfd fds[2] = {
		{ .fd = STDIN_FILENO, .events = POLLIN },
		{ .fd = client.sock,  .events = POLLIN },
	};
	while (0 < poll(fds, 2, -1)) {
		if (fds[0].revents) uiRead();
		if (fds[1].revents) clientRead();
		uiDraw();
	}
	err(EX_IOERR, "poll");
}
