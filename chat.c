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

#define _XOPEN_SOURCE_EXTENDED

#include <curses.h>
#include <err.h>
#include <errno.h>
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
#include <wchar.h>
#include <wctype.h>

#define err(...) do { endwin(); err(__VA_ARGS__); } while (0)
#define errx(...) do { endwin(); errx(__VA_ARGS__); } while (0)

static wchar_t *wcssep(wchar_t **stringp, const wchar_t *delim) {
	wchar_t *orig = *stringp;
	if (!orig) return NULL;
	size_t i = wcscspn(orig, delim);
	*stringp = NULL;
	if (orig[i]) {
		orig[i] = '\0';
		*stringp = &orig[i + 1];
	}
	return orig;
}

static void curse(void) {
	setlocale(LC_CTYPE, "");
	initscr();
	keypad(stdscr, true);
	start_color();
	use_default_colors();
	for (short pair = 0; pair < 077; ++pair) {
		if (pair < 010) {
			init_pair(1 + pair, pair, -1);
		} else {
			init_pair(1 + pair, pair & 007, (pair & 070) >> 3);
		}
	}
}

static const int TOPIC_COLS = 512;
static const int CHAT_LINES = 100;
static const int INPUT_COLS = 512;
static struct {
	WINDOW *topic;
	WINDOW *chat;
	WINDOW *input;
} ui;

static void uiInit(void) {
	ui.topic = newpad(2, TOPIC_COLS);
	mvwhline(ui.topic, 1, 0, ACS_HLINE, TOPIC_COLS);

	ui.chat = newpad(CHAT_LINES, COLS);
	wsetscrreg(ui.chat, 0, CHAT_LINES - 1);
	scrollok(ui.chat, true);
	wmove(ui.chat, CHAT_LINES - (LINES - 4) - 1, 0);

	ui.input = newpad(2, INPUT_COLS);
	mvwhline(ui.input, 0, 0, ACS_HLINE, INPUT_COLS);
	wmove(ui.input, 1, 0);
	cbreak();
	noecho();
}

static void uiDraw(void) {
	pnoutrefresh(ui.topic, 0, 0, 0, 0, 1, COLS - 1);
	pnoutrefresh(
		ui.chat,
		CHAT_LINES - (LINES - 4), 0,
		2, 0, LINES - 1, COLS - 1
	);
	pnoutrefresh(
		ui.input,
		0, 0,
		LINES - 2, 0,
		LINES - 1, COLS - 1
	);
	doupdate();
}

static const struct {
	attr_t attr;
	short color;
} MIRC_COLORS[16] = {
	{ A_BOLD,   COLOR_WHITE },   // white
	{ A_NORMAL, COLOR_BLACK },   // black
	{ A_NORMAL, COLOR_BLUE },    // blue
	{ A_NORMAL, COLOR_GREEN },   // green
	{ A_BOLD,   COLOR_RED },     // red
	{ A_NORMAL, COLOR_RED },     // "brown"
	{ A_NORMAL, COLOR_MAGENTA }, // magenta
	{ A_NORMAL, COLOR_YELLOW },  // "orange"
	{ A_BOLD,   COLOR_YELLOW },  // yellow
	{ A_BOLD,   COLOR_GREEN },   // light green
	{ A_NORMAL, COLOR_CYAN },    // cyan
	{ A_BOLD,   COLOR_CYAN },    // light cyan
	{ A_BOLD,   COLOR_BLUE },    // light blue
	{ A_BOLD,   COLOR_MAGENTA }, // "pink"
	{ A_BOLD,   COLOR_BLACK },   // grey
	{ A_NORMAL, COLOR_WHITE },   // light grey
};

static void uiAdd(WINDOW *win, const char *str) {
	attr_t attr = A_NORMAL;
	short colorPair = -1;
	attr_t colorAttr = A_NORMAL;
	for (;;) {
		size_t cc = strcspn(str, "\2\3\35\37");
		wattr_set(win, attr | colorAttr, 1 + colorPair, NULL);
		waddnstr(win, str, cc);
		if (!str[cc]) break;

		str = &str[cc];
		switch (*str++) {
			break; case '\2':  attr ^= A_BOLD;
			break; case '\35': attr ^= A_ITALIC;
			break; case '\37': attr ^= A_UNDERLINE;
			break; case '\3': {
				short fg = 0;
				short bg = 0;

				size_t fgLen = strspn(str, "0123456789");
				if (!fgLen) {
					colorPair = -1;
					colorAttr = A_NORMAL;
					break;
				}

				if (fgLen > 2) fgLen = 2;
				for (size_t i = 0; i < fgLen; ++i) {
					fg *= 10;
					fg += str[i] - '0';
				}
				str = &str[fgLen];

				size_t bgLen = (str[0] == ',') ? strspn(&str[1], "0123456789") : 0;
				if (bgLen > 2) bgLen = 2;
				for (size_t i = 0; i < bgLen; ++i) {
					bg *= 10;
					bg += str[1 + i] - '0';
				}
				if (bgLen) str = &str[1 + bgLen];

				if (colorPair == -1) colorPair = 0;
				colorPair = (colorPair & 070) | MIRC_COLORS[fg].color;
				colorAttr = MIRC_COLORS[fg].attr;
				if (bgLen) {
					colorPair = (colorPair & 007) | (MIRC_COLORS[bg].color << 3);
				}
			}
		}
	}
}

static void uiTopic(const char *topic) {
	wmove(ui.topic, 0, 0);
	wclrtoeol(ui.topic);
	uiAdd(ui.topic, topic);
}
static void uiChat(const char *line) {
	waddch(ui.chat, '\n');
	uiAdd(ui.chat, line);
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

static int color(const char *s) {
	if (!s) return 0;
	int x = 0;
	for (; s[0]; ++s) {
		x ^= s[0];
	}
	x &= 15;
	return (x == 1) ? 0 : x;
}

static struct {
	int sock;
	struct tls *tls;
	bool verbose;
	char *nick;
	char *user;
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

static char *prift(char **prefix) {
	return strsep(prefix, "!@");
}
static char *shift(char **params) {
	char *rest = *params;
	if (!rest) errx(EX_PROTOCOL, "expected param");
	if (rest[0] == ':') {
		*params = NULL;
		return &rest[1];
	}
	return strsep(params, " ");
}

static void handlePing(char *prefix, char *params) {
	(void)prefix;
	clientFmt("PONG %s\r\n", params);
}

static void handle001(char *prefix, char *params) {
	(void)prefix;
	char *nick = shift(&params);
	if (strcmp(nick, client.nick)) {
		free(client.nick);
		client.nick = strdup(nick);
	}
	clientFmt("JOIN %s\r\n", client.chan);
}

static void handleJoin(char *prefix, char *params) {
	char *nick = prift(&prefix);
	char *user = prift(&prefix);
	char *chan = shift(&params);
	if (!strcmp(nick, client.nick) && strcmp(user, client.user)) {
		free(client.user);
		client.user = strdup(user);
	}
	uiFmt(
		"\3%d%s\3 arrives in \3%d%s\3",
		color(user), nick, color(chan), chan
	);
}
static void handlePart(char *prefix, char *params) {
	char *nick = prift(&prefix);
	char *user = prift(&prefix);
	char *chan = shift(&params);
	char *mesg = shift(&params);
	uiFmt(
		"\3%d%s\3 leaves \3%d%s\3, \"%s\"",
		color(user), nick, color(chan), chan, mesg
	);
}
static void handleQuit(char *prefix, char *params) {
	char *nick = prift(&prefix);
	char *user = prift(&prefix);
	char *mesg = shift(&params);
	char *quot = (mesg[0] == '"') ? "" : "\"";
	uiFmt(
		"\3%d%s\3 leaves, %s%s%s",
		color(user), nick, quot, mesg, quot
	);
}
static void handleKick(char *prefix, char *params) {
	char *nick = prift(&prefix);
	char *user = prift(&prefix);
	char *chan = shift(&params);
	char *kick = shift(&params);
	char *mesg = shift(&params);
	uiFmt(
		"\3%d%s\3 kicks \3%d%s\3 out of \3%d%s\3, \"%s\"",
		color(user), nick, color(kick), kick, color(chan), chan, mesg
	);
}

static void handle332(char *prefix, char *params) {
	(void)prefix;
	shift(&params);
	char *chan = shift(&params);
	char *topic = shift(&params);
	uiFmt(
		"The sign in \3%d%s\3 reads, \"%s\"",
		color(chan), chan, topic
	);
	uiTopic(topic);
}
static void handleTopic(char *prefix, char *params) {
	char *nick = prift(&prefix);
	char *user = prift(&prefix);
	char *chan = shift(&params);
	char *topic = shift(&params);
	uiFmt(
		"\3%d%s\3 places a new sign in \3%d%s\3, \"%s\"",
		color(user), nick, color(chan), chan, topic
	);
	uiTopic(topic);
}

static void handle366(char *prefix, char *params) {
	(void)prefix;
	shift(&params);
	char *chan = shift(&params);
	clientFmt("WHO %s\r\n", chan);
}

static char whoBuf[4096];
static size_t whoLen;
static void handle352(char *prefix, char *params) {
	(void)prefix;
	shift(&params);
	shift(&params);
	char *user = shift(&params);
	shift(&params);
	shift(&params);
	char *nick = shift(&params);
	whoLen += snprintf(
		&whoBuf[whoLen], sizeof(whoBuf) - whoLen,
		"%s\3%d%s\3",
		(whoLen ? ", " : ""), color(user), nick
	);
}
static void handle315(char *prefix, char *params) {
	(void)prefix;
	shift(&params);
	char *chan = shift(&params);
	whoLen = 0;
	uiFmt(
		"In \3%d%s\3 are %s",
		color(chan), chan, whoBuf
	);
}

static void handleNick(char *prefix, char *params) {
	char *prev = prift(&prefix);
	char *user = prift(&prefix);
	char *next = shift(&params);
	if (!strcmp(user, client.user)) {
		free(client.nick);
		client.nick = strdup(next);
	}
	uiFmt(
		"\3%d%s\3 is now known as \3%d%s\3",
		color(user), prev, color(user), next
	);
}

static void handlePrivmsg(char *prefix, char *params) {
	char *nick = prift(&prefix);
	char *user = prift(&prefix);
	shift(&params);
	char *mesg = shift(&params);
	if (mesg[0] == '\1') {
		strsep(&mesg, " ");
		uiFmt("* \3%d%s\3 %s", color(user), nick, strsep(&mesg, "\1"));
	} else {
		uiFmt("<\3%d%s\3> %s", color(user), nick, mesg);
	}
}
static void handleNotice(char *prefix, char *params) {
	char *nick = prift(&prefix);
	char *user = prift(&prefix);
	char *chan = shift(&params);
	char *mesg = shift(&params);
	if (strcmp(client.chan, chan)) return;
	uiFmt("-\3%d%s\3- %s", color(user), nick, mesg);
}

static const struct {
	const char *command;
	Handler handler;
} HANDLERS[] = {
	{ "001", handle001 },
	{ "315", handle315 },
	{ "332", handle332 },
	{ "352", handle352 },
	{ "366", handle366 },
	{ "JOIN", handleJoin },
	{ "KICK", handleKick },
	{ "NICK", handleNick },
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

static void privmsg(bool action, const wchar_t *mesg) {
	char *line;
	int send;
	asprintf(
		&line, ":%s!%s %nPRIVMSG %s :%s%ls%s",
		client.nick, client.user, &send, client.chan,
		(action ? "\1ACTION " : ""), mesg, (action ? "\1" : "")
	);
	if (!line) err(EX_OSERR, "asprintf");
	clientFmt("%s\r\n", &line[send]);
	handle(line);
	free(line);
}

typedef void (*Command)(wchar_t *params);

static void inputMe(wchar_t *params) {
	privmsg(true, params ? params : L"");
}

static void inputNick(wchar_t *params) {
	wchar_t *nick = wcssep(&params, L" ");
	if (nick) {
		clientFmt("NICK %ls\r\n", nick);
	} else {
		uiFmt("/nick requires a name");
	}
}

static void inputQuit(wchar_t *params) {
	if (params) {
		clientFmt("QUIT :%ls\r\n", params);
	} else {
		clientFmt("QUIT :Goodbye\r\n");
	}
}

static const struct {
	const wchar_t *command;
	Command handler;
} COMMANDS[] = {
	{ L"me", inputMe },
	{ L"nick", inputNick },
	{ L"quit", inputQuit },
};
static const size_t COMMANDS_LEN = sizeof(COMMANDS) / sizeof(COMMANDS[0]);

static void input(wchar_t *input) {
	if (input[0] != '/') {
		privmsg(false, input);
		return;
	}
	input++;
	wchar_t *command = wcssep(&input, L" ");
	for (size_t i = 0; i < COMMANDS_LEN; ++i) {
		if (wcscmp(command, COMMANDS[i].command)) continue;
		COMMANDS[i].handler(input);
		return;
	}
	uiFmt("/%ls isn't a recognized command", command);
}

static void uiRead(void) {
	static wchar_t buf[512];
	static size_t len;

	wint_t ch;
	wget_wch(ui.input, &ch);
	switch (ch) {
		break; case '\b': case '\177': {
			if (len) len--;
		}
		break; case '\n': {
			if (!len) break;
			buf[len] = '\0';
			input(buf);
			len = 0;
		}
		break; default: {
			if (iswprint(ch)) buf[len++] = ch;
		}
	}
	wmove(ui.input, 1, 0);
	waddnwstr(ui.input, buf, len);
	wclrtoeol(ui.input);
}

static void webirc(const char *pass) {
	const char *ssh = getenv("SSH_CLIENT");
	if (!ssh) return;
	int len = strchrnul(ssh, ' ') - ssh;
	clientFmt(
		"WEBIRC %s %s %.*s %.*s\r\n",
		pass, client.nick, len, ssh, len, ssh
	);
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
	client.user = strdup(client.nick);

	erase();
	uiInit();
	uiChat("Traveling...");
	uiDraw();

	struct tls_config *config = tls_config_new();
	error = tls_config_set_ciphers(config, "compat");
	if (error) errx(EX_SOFTWARE, "tls_config: %s", tls_config_error(config));

	client.tls = tls_client();
	if (!client.tls) errx(EX_SOFTWARE, "tls_client");

	error = tls_configure(client.tls, config);
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

	client.sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
	if (client.sock < 0) err(EX_OSERR, "socket");

	error = connect(client.sock, ai->ai_addr, ai->ai_addrlen);
	if (error) err(EX_UNAVAILABLE, "connect");
	freeaddrinfo(ai);

	error = tls_connect_socket(client.tls, client.sock, host);
	if (error) err(EX_PROTOCOL, "tls_connect");

	if (webPass) webirc(webPass);
	clientFmt("NICK %s\r\n", client.nick);
	clientFmt("USER %s 0 * :%s\r\n", client.user, client.nick);

	struct pollfd fds[2] = {
		{ .fd = STDIN_FILENO, .events = POLLIN },
		{ .fd = client.sock,  .events = POLLIN },
	};
	for (;;) {
		int nfds = poll(fds, 2, -1);
		if (nfds < 0 && errno == EINTR) continue;
		if (nfds < 0) err(EX_IOERR, "poll");

		if (fds[0].revents) uiRead();
		if (fds[1].revents) clientRead();
		uiDraw();
	}
}
