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
		if (!params) errx(EX_PROTOCOL, "%zu params required, found %zu", req, i);
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

static bool isSelf(const char *nick, const char *user) {
	if (!user) return false;
	if (!strcmp(user, self.user)) return true;
	if (!strcmp(nick, self.nick)) {
		if (strcmp(user, self.user)) selfUser(user);
		return true;
	}
	return false;
}

typedef void (*Handler)(char *prefix, char *params);

static void handlePing(char *prefix, char *params) {
	(void)prefix;
	ircFmt("PONG %s\r\n", params);
}

static void handle432(char *prefix, char *params) {
	char *mesg;
	shift(prefix, NULL, NULL, NULL, params, 3, 0, NULL, NULL, &mesg);
	uiLog(TAG_DEFAULT, L"You can't use that name here");
	uiFmt(TAG_DEFAULT, "Sheriff says, \"%s\"", mesg);
	uiLog(TAG_DEFAULT, L"Type /nick <name> to choose a new one");
}

static void handle001(char *prefix, char *params) {
	char *nick;
	shift(prefix, NULL, NULL, NULL, params, 1, 0, &nick);
	if (strcmp(nick, self.nick)) selfNick(nick);
	tabTouch(TAG_DEFAULT, self.nick);
	if (self.join) ircFmt("JOIN %s\r\n", self.join);
	uiLog(TAG_DEFAULT, L"You have arrived");
}

static void handle372(char *prefix, char *params) {
	char *mesg;
	shift(prefix, NULL, NULL, NULL, params, 2, 0, NULL, &mesg);
	if (mesg[0] == '-' && mesg[1] == ' ') mesg = &mesg[2];
	uiFmt(TAG_DEFAULT, "%s", mesg);
}

static void handleJoin(char *prefix, char *params) {
	char *nick, *user, *chan;
	shift(prefix, &nick, &user, NULL, params, 1, 0, &chan);
	struct Tag tag = tagFor(chan);
	if (isSelf(nick, user)) {
		tabTouch(TAG_DEFAULT, chan);
		uiFocus(tag);
	} else {
		tabTouch(tag, nick);
	}
	uiFmt(
		tag, "\3%d%s\3 arrives in \3%d%s\3",
		color(user), nick, color(chan), chan
	);
}

static void handlePart(char *prefix, char *params) {
	char *nick, *user, *chan, *mesg;
	shift(prefix, &nick, &user, NULL, params, 1, 1, &chan, &mesg);
	struct Tag tag = tagFor(chan);
	(void)(isSelf(nick, user) ? tabClear(tag) : tabRemove(tag, nick));
	if (mesg) {
		uiFmt(
			tag, "\3%d%s\3 leaves \3%d%s\3, \"%s\"",
			color(user), nick, color(chan), chan, mesg
		);
	} else {
		uiFmt(
			tag, "\3%d%s\3 leaves \3%d%s\3",
			color(user), nick, color(chan), chan
		);
	}
}

static void handleKick(char *prefix, char *params) {
	char *nick, *user, *chan, *kick, *mesg;
	shift(prefix, &nick, &user, NULL, params, 2, 1, &chan, &kick, &mesg);
	struct Tag tag = tagFor(chan);
	(void)(isSelf(nick, user) ? tabClear(tag) : tabRemove(tag, nick));
	if (mesg) {
		uiFmt(
			tag, "\3%d%s\3 kicks \3%d%s\3 out of \3%d%s\3, \"%s\"",
			color(user), nick, color(kick), kick, color(chan), chan, mesg
		);
	} else {
		uiFmt(
			tag, "\3%d%s\3 kicks \3%d%s\3 out of \3%d%s\3",
			color(user), nick, color(kick), kick, color(chan), chan
		);
	}
}

static void handleQuit(char *prefix, char *params) {
	char *nick, *user, *mesg;
	shift(prefix, &nick, &user, NULL, params, 0, 1, &mesg);
	// TODO: Send to tags where nick is in tab.
	tabRemove(TAG_ALL, nick);
	if (mesg) {
		char *quot = (mesg[0] == '"') ? "" : "\"";
		uiFmt(
			TAG_DEFAULT, "\3%d%s\3 leaves, %s%s%s",
			color(user), nick, quot, mesg, quot
		);
	} else {
		uiFmt(TAG_DEFAULT, "\3%d%s\3 leaves", color(user), nick);
	}
}

static void handle332(char *prefix, char *params) {
	char *chan, *topic;
	shift(prefix, NULL, NULL, NULL, params, 3, 0, NULL, &chan, &topic);
	struct Tag tag = tagFor(chan);
	urlScan(tag, topic);
	uiTopic(tag, topic);
	uiFmt(
		tag, "The sign in \3%d%s\3 reads, \"%s\"",
		color(chan), chan, topic
	);
}

static void handleTopic(char *prefix, char *params) {
	char *nick, *user, *chan, *topic;
	shift(prefix, &nick, &user, NULL, params, 2, 0, &chan, &topic);
	struct Tag tag = tagFor(chan);
	if (!isSelf(nick, user)) tabTouch(tag, nick);
	urlScan(tag, topic);
	uiTopic(tag, topic);
	uiFmt(
		tag, "\3%d%s\3 places a new sign in \3%d%s\3, \"%s\"",
		color(user), nick, color(chan), chan, topic
	);
}

static void handle366(char *prefix, char *params) {
	char *chan;
	shift(prefix, NULL, NULL, NULL, params, 2, 0, NULL, &chan);
	ircFmt("WHO %s\r\n", chan);
}

// FIXME: Track tag?
static struct {
	char buf[4096];
	size_t len;
} who;

static void handle352(char *prefix, char *params) {
	char *chan, *user, *nick;
	shift(
		prefix, NULL, NULL, NULL,
		params, 6, 0, NULL, &chan, &user, NULL, NULL, &nick
	);
	struct Tag tag = tagFor(chan);
	if (!isSelf(nick, user)) tabTouch(tag, nick);
	size_t cap = sizeof(who.buf) - who.len;
	int len = snprintf(
		&who.buf[who.len], cap,
		"%s\3%d%s\3",
		(who.len ? ", " : ""), color(user), nick
	);
	if ((size_t)len < cap) who.len += len;
}

static void handle315(char *prefix, char *params) {
	char *chan;
	shift(prefix, NULL, NULL, NULL, params, 2, 0, NULL, &chan);
	struct Tag tag = tagFor(chan);
	uiFmt(
		tag, "In \3%d%s\3 are %s",
		color(chan), chan, who.buf
	);
	who.len = 0;
}

static void handleNick(char *prefix, char *params) {
	char *prev, *user, *next;
	shift(prefix, &prev, &user, NULL, params, 1, 0, &next);
	if (isSelf(prev, user)) selfNick(next);
	// TODO: Send to tags where prev is in tab.
	tabReplace(prev, next);
	uiFmt(
		TAG_DEFAULT, "\3%d%s\3 is now known as \3%d%s\3",
		color(user), prev, color(user), next
	);
}

static void handleCTCP(struct Tag tag, char *nick, char *user, char *mesg) {
	mesg = &mesg[1];
	char *ctcp = strsep(&mesg, " ");
	char *params = strsep(&mesg, "\1");
	if (strcmp(ctcp, "ACTION")) return;
	if (!isSelf(nick, user)) tabTouch(tag, nick);
	urlScan(tag, params);
	uiFmt(
		tag, "\3%d* %s\3 %s",
		color(user), nick, params
	);
}

static void handlePrivmsg(char *prefix, char *params) {
	char *nick, *user, *chan, *mesg;
	shift(prefix, &nick, &user, NULL, params, 2, 0, &chan, &mesg);
	struct Tag tag = (strcmp(chan, self.nick) ? tagFor(chan) : tagFor(nick));
	if (mesg[0] == '\1') {
		handleCTCP(tag, nick, user, mesg);
		return;
	}
	if (!isSelf(nick, user)) tabTouch(tag, nick);
	urlScan(tag, mesg);
	bool ping = !strncasecmp(mesg, self.nick, strlen(self.nick));
	bool self = isSelf(nick, user);
	uiFmt(
		tag, "%c\3%d%c%s%c\17 %s",
		ping["\17\26"], color(user), self["<("], nick, self[">)"], mesg
	);
	if (ping) uiBeep();
}

static void handleNotice(char *prefix, char *params) {
	char *nick, *user, *chan, *mesg;
	shift(prefix, &nick, &user, NULL, params, 2, 0, &chan, &mesg);
	struct Tag tag = TAG_DEFAULT;
	if (user) tag = (strcmp(chan, self.nick) ? tagFor(chan) : tagFor(nick));
	if (!isSelf(nick, user)) tabTouch(tag, nick);
	urlScan(tag, mesg);
	uiFmt(
		tag, "\3%d-%s-\3 %s",
		color(user), nick, mesg
	);
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
	{ "372", handle372 },
	{ "375", handle372 },
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
