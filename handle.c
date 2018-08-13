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

#include <ctype.h>
#include <err.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include "chat.h"

// Adapted from <https://github.com/cbreeden/fxhash/blob/master/lib.rs>.
static int color(const char *str) {
	if (!str) return IRC_GRAY;
	uint32_t hash = 0;
	for (; str[0]; ++str) {
		hash = (hash << 5) | (hash >> 27);
		hash ^= str[0];
		hash *= 0x27220A95;
	}
	hash &= IRC_LIGHT_GRAY;
	return (hash == IRC_BLACK) ? IRC_GRAY : hash;
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
	if (!strcmp(nick, self.nick)) {
		if (strcmp(user, self.user)) selfUser(user);
		return true;
	}
	return false;
}

static bool isPing(const char *mesg) {
	size_t len = strlen(self.nick);
	const char *match = mesg;
	while (NULL != (match = strcasestr(match, self.nick))) {
		char b = (match > mesg ? *(match - 1) : ' ');
		char a = (match[len] ? match[len] : ' ');
		match = &match[len];
		if (!isspace(b) && !ispunct(b)) continue;
		if (!isspace(a) && !ispunct(a)) continue;
		return true;
	}
	return false;
}

typedef void (*Handler)(char *prefix, char *params);

static void handlePing(char *prefix, char *params) {
	(void)prefix;
	ircFmt("PONG %s\r\n", params);
}

static void handleReplyErroneousNickname(char *prefix, char *params) {
	char *mesg;
	shift(prefix, NULL, NULL, NULL, params, 3, 0, NULL, NULL, &mesg);
	uiLog(TAG_STATUS, L"You can't use that name here");
	uiFmt(TAG_STATUS, "Sheriff says, \"%s\"", mesg);
	uiLog(TAG_STATUS, L"Type /nick <name> to choose a new one");
}

static void handleReplyWelcome(char *prefix, char *params) {
	char *nick;
	shift(prefix, NULL, NULL, NULL, params, 1, 0, &nick);
	if (strcmp(nick, self.nick)) selfNick(nick);
	tabTouch(TAG_STATUS, self.nick);
	if (self.join) ircFmt("JOIN %s\r\n", self.join);
	uiLog(TAG_STATUS, L"You have arrived");
}

static void handleReplyMOTD(char *prefix, char *params) {
	char *mesg;
	shift(prefix, NULL, NULL, NULL, params, 2, 0, NULL, &mesg);
	if (mesg[0] == '-' && mesg[1] == ' ') mesg = &mesg[2];
	urlScan(TAG_STATUS, mesg);
	uiFmt(TAG_STATUS, "%s", mesg);
}

static void handleJoin(char *prefix, char *params) {
	char *nick, *user, *chan;
	shift(prefix, &nick, &user, NULL, params, 1, 0, &chan);
	struct Tag tag = tagFor(chan);
	if (isSelf(nick, user)) {
		tabTouch(TAG_NONE, chan);
		uiViewTag(tag);
	}
	tabTouch(tag, nick);
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
		urlScan(tag, mesg);
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
		urlScan(tag, mesg);
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
	char *quot = (mesg && mesg[0] == '"') ? "" : "\"";
	struct Tag tag;
	while (TAG_NONE.id != (tag = tabTag(nick)).id) {
		tabRemove(tag, nick);
		if (mesg) {
			urlScan(tag, mesg);
			uiFmt(
				tag, "\3%d%s\3 leaves, %s%s%s",
				color(user), nick, quot, mesg, quot
			);
		} else {
			uiFmt(tag, "\3%d%s\3 leaves", color(user), nick);
		}
	}
}

static void handleReplyTopic(char *prefix, char *params) {
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

static void handleReplyEndOfNames(char *prefix, char *params) {
	char *chan;
	shift(prefix, NULL, NULL, NULL, params, 2, 0, NULL, &chan);
	ircFmt("WHO %s\r\n", chan);
}

// FIXME: Track tag?
static struct {
	char buf[4096];
	size_t len;
} who;

static void handleReplyWho(char *prefix, char *params) {
	char *chan, *user, *nick;
	shift(
		prefix, NULL, NULL, NULL,
		params, 6, 0, NULL, &chan, &user, NULL, NULL, &nick
	);
	struct Tag tag = tagFor(chan);
	tabTouch(tag, nick);
	size_t cap = sizeof(who.buf) - who.len;
	int len = snprintf(
		&who.buf[who.len], cap,
		"%s\3%d%s\3",
		(who.len ? ", " : ""), color(user), nick
	);
	if ((size_t)len < cap) who.len += len;
}

static void handleReplyEndOfWho(char *prefix, char *params) {
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
	struct Tag tag;
	while (TAG_NONE.id != (tag = tabTag(prev)).id) {
		tabReplace(tag, prev, next);
		uiFmt(
			tag, "\3%d%s\3 is now known as \3%d%s\3",
			color(user), prev, color(user), next
		);
	}
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
	bool self = isSelf(nick, user);
	bool ping = !self && isPing(mesg);
	uiFmt(
		tag, "%c\3%d%c%s%c\17 %s",
		ping["\17\26"], color(user), self["<("], nick, self[">)"], mesg
	);
}

static void handleNotice(char *prefix, char *params) {
	char *nick, *user, *chan, *mesg;
	shift(prefix, &nick, &user, NULL, params, 2, 0, &chan, &mesg);
	struct Tag tag = TAG_STATUS;
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
	{ "001", handleReplyWelcome },
	{ "315", handleReplyEndOfWho },
	{ "332", handleReplyTopic },
	{ "352", handleReplyWho },
	{ "366", handleReplyEndOfNames },
	{ "372", handleReplyMOTD },
	{ "375", handleReplyMOTD },
	{ "432", handleReplyErroneousNickname },
	{ "433", handleReplyErroneousNickname },
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
