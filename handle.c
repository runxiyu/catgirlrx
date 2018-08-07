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

typedef void (*Handler)(char *prefix, char *params);

static char *prift(char **prefix) {
	return strsep(prefix, "!@");
}

static char *shift(char **params) {
	char *rest = *params;
	if (!rest) errx(EX_PROTOCOL, "unexpected eol");
	if (rest[0] == ':') {
		*params = NULL;
		return &rest[1];
	}
	return strsep(params, " ");
}

static void handlePing(char *prefix, char *params) {
	(void)prefix;
	ircFmt("PONG %s\r\n", params);
}

static void handle432(char *prefix, char *params) {
	(void)prefix;
	shift(&params);
	shift(&params);
	char *mesg = shift(&params);
	uiLog(L"You can't use that name here");
	uiFmt(L"Sheriff says, \"%s\"", mesg);
	uiLog(L"Type /nick <name> to choose a new one");
}

static void handle001(char *prefix, char *params) {
	(void)prefix;
	char *nick = shift(&params);
	if (strcmp(nick, chat.nick)) {
		free(chat.nick);
		chat.nick = strdup(nick);
	}
	ircFmt("JOIN %s\r\n", chat.chan);
}

static void handleJoin(char *prefix, char *params) {
	char *nick = prift(&prefix);
	char *user = prift(&prefix);
	char *chan = shift(&params);
	if (!strcmp(nick, chat.nick) && strcmp(user, chat.user)) {
		free(chat.user);
		chat.user = strdup(user);
	}
	uiFmt(
		L"\3%d%s\3 arrives in \3%d%s\3",
		color(user), nick, color(chan), chan
	);
}

static void handlePart(char *prefix, char *params) {
	char *nick = prift(&prefix);
	char *user = prift(&prefix);
	char *chan = shift(&params);
	if (params) {
		char *mesg = shift(&params);
		uiFmt(
			L"\3%d%s\3 leaves \3%d%s\3, \"%s\"",
			color(user), nick, color(chan), chan, mesg
		);
	} else {
		uiFmt(
			L"\3%d%s\3 leaves \3%d%s\3",
			color(user), nick, color(chan), chan
		);
	}
}

static void handleQuit(char *prefix, char *params) {
	char *nick = prift(&prefix);
	char *user = prift(&prefix);
	if (params) {
		char *mesg = shift(&params);
		char *quot = (mesg[0] == '"') ? "" : "\"";
		uiFmt(
			L"\3%d%s\3 leaves, %s%s%s",
			color(user), nick, quot, mesg, quot
		);
	} else {
		uiFmt(L"\3%d%s\3 leaves", color(user), nick);
	}
}

static void handleKick(char *prefix, char *params) {
	char *nick = prift(&prefix);
	char *user = prift(&prefix);
	char *chan = shift(&params);
	char *kick = shift(&params);
	char *mesg = shift(&params);
	uiFmt(
		L"\3%d%s\3 kicks \3%d%s\3 out of \3%d%s\3, \"%s\"",
		color(user), nick, color(kick), kick, color(chan), chan, mesg
	);
}

static void handle332(char *prefix, char *params) {
	(void)prefix;
	shift(&params);
	char *chan = shift(&params);
	char *topic = shift(&params);
	uiFmt(
		L"The sign in \3%d%s\3 reads, \"%s\"",
		color(chan), chan, topic
	);
	uiTopicStr(topic);
}

static void handleTopic(char *prefix, char *params) {
	char *nick = prift(&prefix);
	char *user = prift(&prefix);
	char *chan = shift(&params);
	char *topic = shift(&params);
	uiFmt(
		L"\3%d%s\3 places a new sign in \3%d%s\3, \"%s\"",
		color(user), nick, color(chan), chan, topic
	);
	uiTopicStr(topic);
}

static void handle366(char *prefix, char *params) {
	(void)prefix;
	shift(&params);
	char *chan = shift(&params);
	ircFmt("WHO %s\r\n", chan);
}

static struct {
	char buf[4096];
	size_t len;
} who;

static void handle352(char *prefix, char *params) {
	(void)prefix;
	shift(&params);
	shift(&params);
	char *user = shift(&params);
	shift(&params);
	shift(&params);
	char *nick = shift(&params);
	size_t cap = sizeof(who.buf) - who.len;
	int len = snprintf(
		&who.buf[who.len], cap,
		"%s\3%d%s\3",
		(who.len ? ", " : ""), color(user), nick
	);
	if ((size_t)len < cap) who.len += len;
}

static void handle315(char *prefix, char *params) {
	(void)prefix;
	shift(&params);
	char *chan = shift(&params);
	who.len = 0;
	uiFmt(
		L"In \3%d%s\3 are %s",
		color(chan), chan, who.buf
	);
}

static void handleNick(char *prefix, char *params) {
	char *prev = prift(&prefix);
	char *user = prift(&prefix);
	char *next = shift(&params);
	if (!strcmp(user, chat.user)) {
		free(chat.nick);
		chat.nick = strdup(next);
	}
	uiFmt(
		L"\3%d%s\3 is now known as \3%d%s\3",
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
		uiFmt(L"* \3%d%s\3 %s", color(user), nick, strsep(&mesg, "\1"));
	} else {
		uiFmt(L"<\3%d%s\3> %s", color(user), nick, mesg);
	}
}

static void handleNotice(char *prefix, char *params) {
	char *nick = prift(&prefix);
	char *user = prift(&prefix);
	char *chan = shift(&params);
	char *mesg = shift(&params);
	if (strcmp(chat.chan, chan)) return;
	uiFmt(L"-\3%d%s\3- %s", color(user), nick, mesg);
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
