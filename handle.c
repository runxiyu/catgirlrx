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
#include <time.h>

#include "chat.h"

static char *paramField(char **params) {
	char *rest = *params;
	if (rest[0] == ':') {
		*params = NULL;
		return &rest[1];
	}
	return strsep(params, " ");
}

static void parse(
	char *prefix, char **nick, char **user, char **host,
	char *params, size_t req, size_t opt, /* (char **) */ ...
) {
	char *field;
	if (prefix) {
		field = strsep(&prefix, "!");
		if (nick) *nick = field;
		field = strsep(&prefix, "@");
		if (user) *user = (field && field[0] == '~' ? &field[1] : field);
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

static char *dequote(char *mesg) {
	if (mesg[0] == '"') mesg = &mesg[1];
	size_t len = strlen(mesg);
	if (mesg[len - 1] == '"') mesg[len - 1] = '\0';
	return mesg;
}

typedef void (*Handler)(char *prefix, char *params);

static void handlePing(char *prefix, char *params) {
	(void)prefix;
	ircFmt("PONG %s\r\n", params);
}

static void handleError(char *prefix, char *params) {
	char *mesg;
	parse(prefix, NULL, NULL, NULL, params, 1, 0, &mesg);
	if (self.quit) {
		uiExit();
		exit(EX_OK);
	} else {
		errx(EX_PROTOCOL, "%s", mesg);
	}
}

static void handleCap(char *prefix, char *params) {
	char *subc, *list;
	parse(prefix, NULL, NULL, NULL, params, 3, 0, NULL, &subc, &list);
	if (!strcmp(subc, "ACK") && self.auth) {
		size_t len = strlen(self.auth);
		byte plain[1 + len];
		plain[0] = 0;
		for (size_t i = 0; i < len; ++i) {
			plain[1 + i] = (self.auth[i] == ':' ? 0 : self.auth[i]);
		}
		char *b64 = base64(plain, sizeof(plain));
		ircFmt("AUTHENTICATE PLAIN\r\n");
		ircFmt("AUTHENTICATE %s\r\n", b64);
		free(b64);
	}
	ircFmt("CAP END\r\n");
}

static void handleErrorErroneousNickname(char *prefix, char *params) {
	char *mesg;
	parse(prefix, NULL, NULL, NULL, params, 3, 0, NULL, NULL, &mesg);
	uiFmt(TagStatus, UIHot, "You can't use that name here: \"%s\"", mesg);
	uiLog(TagStatus, UICold, L"Type /nick <name> to choose a new one");
}

static void handleReplyWelcome(char *prefix, char *params) {
	char *nick;
	parse(prefix, NULL, NULL, NULL, params, 1, 0, &nick);

	if (strcmp(nick, self.nick)) {
		free(self.nick);
		self.nick = strdup(nick);
		if (!self.nick) err(EX_OSERR, "strdup");
	}
	if (self.join) ircFmt("JOIN %s\r\n", self.join);
	tabTouch(TagStatus, self.nick);

	uiLog(TagStatus, UICold, L"You have arrived");
}

static void handleReplyMOTD(char *prefix, char *params) {
	char *mesg;
	parse(prefix, NULL, NULL, NULL, params, 2, 0, NULL, &mesg);
	if (mesg[0] == '-' && mesg[1] == ' ') mesg = &mesg[2];

	urlScan(TagStatus, mesg);
	uiFmt(TagStatus, UICold, "%s", mesg);
}

static enum IRCColor whoisColor;
static void handleReplyWhoisUser(char *prefix, char *params) {
	char *nick, *user, *host, *real;
	parse(
		prefix, NULL, NULL, NULL,
		params, 6, 0, NULL, &nick, &user, &host, NULL, &real
	);
	whoisColor = formatColor(user[0] == '~' ? &user[1] : user);
	uiFmt(
		TagStatus, UIWarm,
		"\3%d%s\3 is %s@%s, \"%s\"",
		whoisColor, nick, user, host, real
	);
}

static void handleReplyWhoisServer(char *prefix, char *params) {
	char *nick, *serv, *info;
	parse(prefix, NULL, NULL, NULL, params, 4, 0, NULL, &nick, &serv, &info);
	uiFmt(
		TagStatus, UIWarm,
		"\3%d%s\3 is connected to %s, \"%s\"",
		whoisColor, nick, serv, info
	);
}

static void handleReplyWhoisOperator(char *prefix, char *params) {
	char *nick, *oper;
	parse(prefix, NULL, NULL, NULL, params, 3, 0, NULL, &nick, &oper);
	uiFmt(TagStatus, UIWarm, "\3%d%s\3 %s", whoisColor, nick, oper);
}

static void handleReplyWhoisIdle(char *prefix, char *params) {
	char *nick, *idle, *sign;
	parse(prefix, NULL, NULL, NULL, params, 4, 0, NULL, &nick, &idle, &sign);
	time_t time = strtoul(sign, NULL, 10);
	const char *at = ctime(&time);
	unsigned long secs  = strtoul(idle, NULL, 10);
	unsigned long mins  = secs / 60; secs %= 60;
	unsigned long hours = mins / 60; mins %= 60;
	uiFmt(
		TagStatus, UIWarm,
		"\3%d%s\3 signed on at %.24s and has been idle for %02lu:%02lu:%02lu",
		whoisColor, nick, at, hours, mins, secs
	);
}

static void handleReplyWhoisChannels(char *prefix, char *params) {
	char *nick, *chans;
	parse(prefix, NULL, NULL, NULL, params, 3, 0, NULL, &nick, &chans);
	uiFmt(TagStatus, UIWarm, "\3%d%s\3 is in %s", whoisColor, nick, chans);
}

static void handleErrorNoSuchNick(char *prefix, char *params) {
	char *nick, *mesg;
	parse(prefix, NULL, NULL, NULL, params, 3, 0, NULL, &nick, &mesg);
	uiFmt(TagStatus, UIWarm, "%s, \"%s\"", mesg, nick);
}

static void handleJoin(char *prefix, char *params) {
	char *nick, *user, *chan;
	parse(prefix, &nick, &user, NULL, params, 1, 0, &chan);
	struct Tag tag = tagFor(chan);

	if (!strcmp(nick, self.nick)) {
		tabTouch(TagNone, chan);
		uiViewTag(tag);
		logReplay(tag);
	}
	tabTouch(tag, nick);

	uiFmt(
		tag, UICold,
		"\3%d%s\3 arrives in \3%d%s\3",
		formatColor(user), nick, formatColor(chan), chan
	);
	logFmt(tag, NULL, "%s arrives in %s", nick, chan);
}

static void handlePart(char *prefix, char *params) {
	char *nick, *user, *chan, *mesg;
	parse(prefix, &nick, &user, NULL, params, 1, 1, &chan, &mesg);
	struct Tag tag = tagFor(chan);

	if (!strcmp(nick, self.nick)) {
		tabClear(tag);
	} else {
		tabRemove(tag, nick);
	}

	if (mesg) {
		urlScan(tag, mesg);
		uiFmt(
			tag, UICold,
			"\3%d%s\3 leaves \3%d%s\3, \"%s\"",
			formatColor(user), nick, formatColor(chan), chan, dequote(mesg)
		);
		logFmt(tag, NULL, "%s leaves %s, \"%s\"", nick, chan, dequote(mesg));
	} else {
		uiFmt(
			tag, UICold,
			"\3%d%s\3 leaves \3%d%s\3",
			formatColor(user), nick, formatColor(chan), chan
		);
		logFmt(tag, NULL, "%s leaves %s", nick, chan);
	}
}

static void handleKick(char *prefix, char *params) {
	char *nick, *user, *chan, *kick, *mesg;
	parse(prefix, &nick, &user, NULL, params, 2, 1, &chan, &kick, &mesg);
	struct Tag tag = tagFor(chan);
	bool kicked = !strcmp(kick, self.nick);

	if (kicked) {
		tabClear(tag);
	} else {
		tabRemove(tag, kick);
	}

	if (mesg) {
		urlScan(tag, mesg);
		uiFmt(
			tag, (kicked ? UIHot : UICold),
			"\3%d%s\3 kicks \3%d%s\3 out of \3%d%s\3, \"%s\"",
			formatColor(user), nick,
			formatColor(kick), kick,
			formatColor(chan), chan,
			dequote(mesg)
		);
		logFmt(
			tag, NULL,
			"%s kicks %s out of %s, \"%s\"", nick, kick, chan, dequote(mesg)
		);
	} else {
		uiFmt(
			tag, (kicked ? UIHot : UICold),
			"\3%d%s\3 kicks \3%d%s\3 out of \3%d%s\3",
			formatColor(user), nick,
			formatColor(kick), kick,
			formatColor(chan), chan
		);
		logFmt(tag, NULL, "%s kicks %s out of %s", nick, kick, chan);
	}
}

static void handleQuit(char *prefix, char *params) {
	char *nick, *user, *mesg;
	parse(prefix, &nick, &user, NULL, params, 0, 1, &mesg);

	struct Tag tag;
	while (TagNone.id != (tag = tabTag(nick)).id) {
		tabRemove(tag, nick);

		if (mesg) {
			urlScan(tag, mesg);
			uiFmt(
				tag, UICold,
				"\3%d%s\3 leaves, \"%s\"",
				formatColor(user), nick, dequote(mesg)
			);
			logFmt(tag, NULL, "%s leaves, \"%s\"", nick, dequote(mesg));
		} else {
			uiFmt(tag, UICold, "\3%d%s\3 leaves", formatColor(user), nick);
			logFmt(tag, NULL, "%s leaves", nick);
		}
	}
}

static void handleReplyTopic(char *prefix, char *params) {
	char *chan, *topic;
	parse(prefix, NULL, NULL, NULL, params, 3, 0, NULL, &chan, &topic);
	struct Tag tag = tagFor(chan);

	urlScan(tag, topic);
	uiFmt(
		tag, UICold,
		"The sign in \3%d%s\3 reads, \"%s\"",
		formatColor(chan), chan, topic
	);
	logFmt(tag, NULL, "The sign in %s reads, \"%s\"", chan, topic);
}

static void handleTopic(char *prefix, char *params) {
	char *nick, *user, *chan, *topic;
	parse(prefix, &nick, &user, NULL, params, 2, 0, &chan, &topic);
	struct Tag tag = tagFor(chan);

	if (strcmp(nick, self.nick)) tabTouch(tag, nick);

	urlScan(tag, topic);
	uiFmt(
		tag, UICold,
		"\3%d%s\3 places a new sign in \3%d%s\3, \"%s\"",
		formatColor(user), nick, formatColor(chan), chan, topic
	);
	logFmt(tag, NULL, "%s places a new sign in %s, \"%s\"", nick, chan, topic);
}

static void handleReplyEndOfNames(char *prefix, char *params) {
	char *chan;
	parse(prefix, NULL, NULL, NULL, params, 2, 0, NULL, &chan);
	ircFmt("WHO %s\r\n", chan);
}

static struct {
	char buf[4096];
	size_t len;
} who;

static void handleReplyWho(char *prefix, char *params) {
	char *chan, *user, *nick;
	parse(
		prefix, NULL, NULL, NULL,
		params, 6, 0, NULL, &chan, &user, NULL, NULL, &nick
	);
	if (user[0] == '~') user = &user[1];
	struct Tag tag = tagFor(chan);

	tabAdd(tag, nick);

	size_t cap = sizeof(who.buf) - who.len;
	int len = snprintf(
		&who.buf[who.len], cap,
		"%s\3%d%s\3",
		(who.len ? ", " : ""), formatColor(user), nick
	);
	if ((size_t)len < cap) who.len += len;
}

static void handleReplyEndOfWho(char *prefix, char *params) {
	char *chan;
	parse(prefix, NULL, NULL, NULL, params, 2, 0, NULL, &chan);
	struct Tag tag = tagFor(chan);

	uiFmt(
		tag, UICold,
		"In \3%d%s\3 are %s",
		formatColor(chan), chan, who.buf
	);
	who.len = 0;
}

static void handleNick(char *prefix, char *params) {
	char *prev, *user, *next;
	parse(prefix, &prev, &user, NULL, params, 1, 0, &next);

	if (!strcmp(prev, self.nick)) {
		free(self.nick);
		self.nick = strdup(next);
		if (!self.nick) err(EX_OSERR, "strdup");
		uiPrompt();
	}

	struct Tag tag;
	while (TagNone.id != (tag = tabTag(prev)).id) {
		tabReplace(tag, prev, next);

		uiFmt(
			tag, UICold,
			"\3%d%s\3 is now known as \3%d%s\3",
			formatColor(user), prev, formatColor(user), next
		);
		logFmt(tag, NULL, "%s is now known as %s", prev, next);
	}
}

static void handleCTCP(struct Tag tag, char *nick, char *user, char *mesg) {
	mesg = &mesg[1];
	char *ctcp = strsep(&mesg, " ");
	char *params = strsep(&mesg, "\1");
	if (strcmp(ctcp, "ACTION")) return;

	if (strcmp(nick, self.nick)) tabTouch(tag, nick);

	urlScan(tag, params);
	bool ping = strcmp(nick, self.nick) && isPing(params);
	uiFmt(
		tag, (ping ? UIHot : UIWarm),
		"%c\3%d* %s\17 %s",
		ping["\17\26"], formatColor(user), nick, params
	);
	logFmt(tag, NULL, "* %s %s", nick, params);
}

static void handlePrivmsg(char *prefix, char *params) {
	char *nick, *user, *chan, *mesg;
	parse(prefix, &nick, &user, NULL, params, 2, 0, &chan, &mesg);
	bool direct = !strcmp(chan, self.nick);
	struct Tag tag = (direct ? tagFor(nick) : tagFor(chan));
	if (mesg[0] == '\1') {
		handleCTCP(tag, nick, user, mesg);
		return;
	}

	bool me = !strcmp(nick, self.nick);
	if (!me) tabTouch(tag, nick);

	urlScan(tag, mesg);
	bool hot = !me && (direct || isPing(mesg));
	bool ping = !me && isPing(mesg);
	uiFmt(
		tag, (hot ? UIHot : UIWarm),
		"%c\3%d%c%s%c\17 %s",
		ping["\17\26"], formatColor(user), me["<("], nick, me[">)"], mesg
	);
	logFmt(tag, NULL, "<%s> %s", nick, mesg);
}

static void handleNotice(char *prefix, char *params) {
	char *nick, *user, *chan, *mesg;
	parse(prefix, &nick, &user, NULL, params, 2, 0, &chan, &mesg);
	struct Tag tag = TagStatus;
	if (user) tag = (strcmp(chan, self.nick) ? tagFor(chan) : tagFor(nick));

	if (strcmp(nick, self.nick)) tabTouch(tag, nick);

	urlScan(tag, mesg);
	bool ping = strcmp(nick, self.nick) && isPing(mesg);
	uiFmt(
		tag, (ping ? UIHot : UIWarm),
		"%c\3%d-%s-\17 %s",
		ping["\17\26"], formatColor(user), nick, mesg
	);
	logFmt(tag, NULL, "-%s- %s", nick, mesg);
}

static const struct {
	const char *command;
	Handler handler;
} Handlers[] = {
	{ "001", handleReplyWelcome },
	{ "311", handleReplyWhoisUser },
	{ "312", handleReplyWhoisServer },
	{ "313", handleReplyWhoisOperator },
	{ "315", handleReplyEndOfWho },
	{ "317", handleReplyWhoisIdle },
	{ "319", handleReplyWhoisChannels },
	{ "332", handleReplyTopic },
	{ "352", handleReplyWho },
	{ "366", handleReplyEndOfNames },
	{ "372", handleReplyMOTD },
	{ "375", handleReplyMOTD },
	{ "401", handleErrorNoSuchNick },
	{ "432", handleErrorErroneousNickname },
	{ "433", handleErrorErroneousNickname },
	{ "CAP", handleCap },
	{ "ERROR", handleError },
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
static const size_t HandlersLen = sizeof(Handlers) / sizeof(Handlers[0]);

void handle(char *line) {
	char *prefix = NULL;
	if (line[0] == ':') {
		prefix = strsep(&line, " ") + 1;
		if (!line) errx(EX_PROTOCOL, "unexpected eol");
	}
	char *command = strsep(&line, " ");
	for (size_t i = 0; i < HandlersLen; ++i) {
		if (strcmp(command, Handlers[i].command)) continue;
		Handlers[i].handler(prefix, line);
		break;
	}
}
