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
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include "chat.h"

static int color(const char *s) {
	if (!s) return 0;
	int x = 0;
	for (; s[0]; ++s) {
		x ^= s[0];
	}
	x &= 15;
	return (x == 1) ? 0 : x;
}

static char *paramField(char **params) {
	char *rest = *params;
	if (rest[0] == ':') {
		*params = NULL;
		return &rest[1];
	}
	return strsep(params, " ");
}

static void shift(
	char *prefix, char **nick, char **user, char **host,
	char *params, size_t req, size_t opt, /* (char **) */ ...
) {
	char *field;
	if (prefix) {
		field = strsep(&prefix, "!");
		if (nick) *nick = field;
		field = strsep(&prefix, "@");
		if (user) *user = field;
		if (host) *host = prefix;
	}

	va_list ap;
	va_start(ap, opt);
	for (size_t i = 0; i < req; ++i) {
		if (!params) {
			va_end(ap);
			errx(EX_PROTOCOL, "%zu params required, found %zu", req, i);
		}
		field = paramField(&params);
		char **param = va_arg(ap, char **);
		if (param) *param = field;
	}
	for (size_t i = 0; i < opt; ++i) {
		char **param = va_arg(ap, char **);
		if (params) {
			*param = paramField(&params);
		} else {
			*param = NULL;
		}
	}
	va_end(ap);
}

typedef void (*Handler)(char *prefix, char *params);

static void handlePing(char *prefix, char *params) {
	(void)prefix;
	ircFmt("PONG %s\r\n", params);
}

static void handle432(char *prefix, char *params) {
	char *mesg;
	shift(prefix, NULL, NULL, NULL, params, 3, 0, NULL, NULL, &mesg);
	uiLog(L"You can't use that name here");
	uiFmt("Sheriff says, \"%s\"", mesg);
	uiLog(L"Type /nick <name> to choose a new one");
}

static void handle001(char *prefix, char *params) {
	char *nick;
	shift(prefix, NULL, NULL, NULL, params, 1, 0, &nick);
	if (strcmp(nick, chat.nick)) {
		free(chat.nick);
		chat.nick = strdup(nick);
	}
	ircFmt("JOIN %s\r\n", chat.join);
}

static void handleJoin(char *prefix, char *params) {
	char *nick, *user, *chan;
	shift(prefix, &nick, &user, NULL, params, 1, 0, &chan);
	uiFmt(
		"\3%d%s\3 arrives in \3%d%s\3",
		color(user), nick, color(chan), chan
	);
	if (!strcmp(nick, chat.nick) && strcmp(user, chat.user)) {
		free(chat.user);
		chat.user = strdup(user);
	}
	tabTouch(nick);
}

static void handlePart(char *prefix, char *params) {
	char *nick, *user, *chan, *mesg;
	shift(prefix, &nick, &user, NULL, params, 1, 1, &chan, &mesg);
	if (mesg) {
		uiFmt(
			"\3%d%s\3 leaves \3%d%s\3, \"%s\"",
			color(user), nick, color(chan), chan, mesg
		);
	} else {
		uiFmt(
			"\3%d%s\3 leaves \3%d%s\3",
			color(user), nick, color(chan), chan
		);
	}
	tabRemove(nick);
}

static void handleQuit(char *prefix, char *params) {
	char *nick, *user, *mesg;
	shift(prefix, &nick, &user, NULL, params, 0, 1, &mesg);
	if (mesg) {
		char *quot = (mesg[0] == '"') ? "" : "\"";
		uiFmt(
			"\3%d%s\3 leaves, %s%s%s",
			color(user), nick, quot, mesg, quot
		);
	} else {
		uiFmt("\3%d%s\3 leaves", color(user), nick);
	}
	tabRemove(nick);
}

static void handleKick(char *prefix, char *params) {
	char *nick, *user, *chan, *kick, *mesg;
	shift(prefix, &nick, &user, NULL, params, 2, 1, &chan, &kick, &mesg);
	if (mesg) {
		uiFmt(
			"\3%d%s\3 kicks \3%d%s\3 out of \3%d%s\3, \"%s\"",
			color(user), nick, color(kick), kick, color(chan), chan, mesg
		);
	} else {
		uiFmt(
			"\3%d%s\3 kicks \3%d%s\3 out of \3%d%s\3",
			color(user), nick, color(kick), kick, color(chan), chan
		);
	}
	tabRemove(nick);
}

static void handle332(char *prefix, char *params) {
	char *chan, *topic;
	shift(prefix, NULL, NULL, NULL, params, 3, 0, NULL, &chan, &topic);
	uiFmt(
		"The sign in \3%d%s\3 reads, \"%s\"",
		color(chan), chan, topic
	);
	urlScan(topic);
	uiTopicStr(topic);
}

static void handleTopic(char *prefix, char *params) {
	char *nick, *user, *chan, *topic;
	shift(prefix, &nick, &user, NULL, params, 2, 0, &chan, &topic);
	uiFmt(
		"\3%d%s\3 places a new sign in \3%d%s\3, \"%s\"",
		color(user), nick, color(chan), chan, topic
	);
	urlScan(topic);
	uiTopicStr(topic);
}

static void handle366(char *prefix, char *params) {
	char *chan;
	shift(prefix, NULL, NULL, NULL, params, 2, 0, NULL, &chan);
	ircFmt("WHO %s\r\n", chan);
}

static struct {
	char buf[4096];
	size_t len;
} who;

static void handle352(char *prefix, char *params) {
	char *user, *nick;
	shift(
		prefix, NULL, NULL, NULL,
		params, 6, 0, NULL, NULL, &user, NULL, NULL, &nick
	);
	size_t cap = sizeof(who.buf) - who.len;
	int len = snprintf(
		&who.buf[who.len], cap,
		"%s\3%d%s\3",
		(who.len ? ", " : ""), color(user), nick
	);
	if ((size_t)len < cap) who.len += len;
	tabTouch(nick);
}

static void handle315(char *prefix, char *params) {
	char *chan;
	shift(prefix, NULL, NULL, NULL, params, 2, 0, NULL, &chan);
	uiFmt(
		"In \3%d%s\3 are %s",
		color(chan), chan, who.buf
	);
	who.len = 0;
}

static void handleNick(char *prefix, char *params) {
	char *prev, *user, *next;
	shift(prefix, &prev, &user, NULL, params, 1, 0, &next);
	uiFmt(
		"\3%d%s\3 is now known as \3%d%s\3",
		color(user), prev, color(user), next
	);
	if (!strcmp(user, chat.user)) {
		free(chat.nick);
		chat.nick = strdup(next);
	}
	tabReplace(prev, next);
}

static void handleCTCP(char *nick, char *user, char *mesg) {
	mesg = &mesg[1];
	char *ctcp = strsep(&mesg, " ");
	char *params = strsep(&mesg, "\1");
	if (strcmp(ctcp, "ACTION")) return;
	uiFmt(
		"\3%d* %s\3 %s",
		color(user), nick, params
	);
	if (strcmp(user, chat.user)) tabTouch(nick);
	urlScan(params);
}

static void handlePrivmsg(char *prefix, char *params) {
	char *nick, *user, *mesg;
	shift(prefix, &nick, &user, NULL, params, 2, 0, NULL, &mesg);
	if (mesg[0] == '\1') {
		handleCTCP(nick, user, mesg);
		return;
	}
	bool self = !strcmp(user, chat.user);
	bool ping = !strncasecmp(mesg, chat.nick, strlen(chat.nick));
	uiFmt(
		"%c\3%d%c%s%c\17 %s",
		ping["\17\26"], color(user), self["<("], nick, self[">)"], mesg
	);
	if (!self) tabTouch(nick);
	if (ping) uiBeep();
	urlScan(mesg);
}

static void handleNotice(char *prefix, char *params) {
	char *nick, *user, *chan, *mesg;
	shift(prefix, &nick, &user, NULL, params, 2, 0, &chan, &mesg);
	if (strcmp(chan, chat.join)) return;
	uiFmt(
		"\3%d-%s-\3 %s",
		color(user), nick, mesg
	);
	tabTouch(nick);
	urlScan(mesg);
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
	{ "432", handle432 },
	{ "433", handle432 },
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

void handle(char *line) {
	char *prefix = NULL;
	if (line[0] == ':') {
		prefix = strsep(&line, " ") + 1;
		if (!line) errx(EX_PROTOCOL, "unexpected eol");
	}
	char *command = strsep(&line, " ");
	for (size_t i = 0; i < HANDLERS_LEN; ++i) {
		if (strcmp(command, HANDLERS[i].command)) continue;
		HANDLERS[i].handler(prefix, line);
		break;
	}
}
