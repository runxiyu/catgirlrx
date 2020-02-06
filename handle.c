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

#include <err.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include "chat.h"

static const char *CapNames[] = {
#define X(name, id) [id##Bit] = name,
	ENUM_CAP
#undef X
};

static enum Cap capParse(const char *list) {
	enum Cap caps = 0;
	while (*list) {
		enum Cap cap = 0;
		size_t len = strcspn(list, " ");
		for (size_t i = 0; i < ARRAY_LEN(CapNames); ++i) {
			if (len != strlen(CapNames[i])) continue;
			if (strncmp(list, CapNames[i], len)) continue;
			cap = 1 << i;
			break;
		}
		caps |= cap;
		list += len;
		if (*list) list++;
	}
	return caps;
}

static const char *capList(enum Cap caps) {
	static char buf[1024];
	buf[0] = '\0';
	for (size_t i = 0; i < ARRAY_LEN(CapNames); ++i) {
		if (caps & (1 << i)) {
			if (buf[0]) strlcat(buf, " ", sizeof(buf));
			strlcat(buf, CapNames[i], sizeof(buf));
		}
	}
	return buf;
}

static void require(struct Message *msg, bool origin, size_t len) {
	if (origin) {
		if (!msg->nick) errx(EX_PROTOCOL, "%s missing origin", msg->cmd);
		if (!msg->user) msg->user = msg->nick;
		if (!msg->host) msg->host = msg->user;
	}
	for (size_t i = 0; i < len; ++i) {
		if (msg->params[i]) continue;
		errx(EX_PROTOCOL, "%s missing parameter %zu", msg->cmd, 1 + i);
	}
}

static const time_t *tagTime(const struct Message *msg) {
	static time_t time;
	struct tm tm;
	if (!msg->tags[TagTime]) return NULL;
	if (!strptime(msg->tags[TagTime], "%FT%T", &tm)) return NULL;
	time = timegm(&tm);
	return &time;
}

typedef void Handler(struct Message *msg);

static void handleErrorNicknameInUse(struct Message *msg) {
	if (self.nick) return;
	require(msg, false, 2);
	ircFormat("NICK :%s_\r\n", msg->params[1]);
}

static void handleErrorErroneousNickname(struct Message *msg) {
	require(msg, false, 3);
	errx(EX_CONFIG, "%s: %s", msg->params[1], msg->params[2]);
}

static void handleCap(struct Message *msg) {
	require(msg, false, 3);
	enum Cap caps = capParse(msg->params[2]);
	if (!strcmp(msg->params[1], "LS")) {
		caps &= ~CapSASL;
		if (caps) {
			ircFormat("CAP REQ :%s\r\n", capList(caps));
		} else {
			if (!(self.caps & CapSASL)) ircFormat("CAP END\r\n");
		}
	} else if (!strcmp(msg->params[1], "ACK")) {
		self.caps |= caps;
		if (caps & CapSASL) {
			ircFormat("AUTHENTICATE %s\r\n", (self.plain ? "PLAIN" : "EXTERNAL"));
		}
		if (!(self.caps & CapSASL)) ircFormat("CAP END\r\n");
	} else if (!strcmp(msg->params[1], "NAK")) {
		errx(EX_CONFIG, "server does not support %s", msg->params[2]);
	}
}

static void handleAuthenticate(struct Message *msg) {
	(void)msg;
	if (!self.plain) {
		ircFormat("AUTHENTICATE +\r\n");
		return;
	}

	byte buf[299];
	size_t len = 1 + strlen(self.plain);
	if (sizeof(buf) < len) errx(EX_CONFIG, "SASL PLAIN is too long");
	buf[0] = 0;
	for (size_t i = 0; self.plain[i]; ++i) {
		buf[1 + i] = (self.plain[i] == ':' ? 0 : self.plain[i]);
	}

	char b64[BASE64_SIZE(sizeof(buf))];
	base64(b64, buf, len);
	ircFormat("AUTHENTICATE ");
	ircSend(b64, BASE64_SIZE(len));
	ircFormat("\r\n");

	explicit_bzero(b64, sizeof(b64));
	explicit_bzero(buf, sizeof(buf));
	explicit_bzero(self.plain, strlen(self.plain));
}

static void handleReplyLoggedIn(struct Message *msg) {
	(void)msg;
	ircFormat("CAP END\r\n");
}

static void handleErrorSASLFail(struct Message *msg) {
	require(msg, false, 2);
	errx(EX_CONFIG, "%s", msg->params[1]);
}

static void handleReplyWelcome(struct Message *msg) {
	require(msg, false, 1);
	set(&self.nick, msg->params[0]);
	if (self.join) ircFormat("JOIN %s\r\n", self.join);
}

static void handleReplyISupport(struct Message *msg) {
	for (size_t i = 1; i < ParamCap; ++i) {
		if (!msg->params[i]) break;
		char *key = strsep(&msg->params[i], "=");
		if (!msg->params[i]) continue;
		if (!strcmp(key, "NETWORK")) {
			set(&self.network, msg->params[i]);
			uiFormat(
				Network, Cold, tagTime(msg),
				"You arrive in %s", msg->params[i]
			);
		} else if (!strcmp(key, "CHANTYPES")) {
			set(&self.chanTypes, msg->params[i]);
		} else if (!strcmp(key, "PREFIX")) {
			strsep(&msg->params[i], ")");
			if (!msg->params[i]) continue;
			set(&self.prefixes, msg->params[i]);
		}
	}
}

static void handleReplyMOTD(struct Message *msg) {
	require(msg, false, 2);
	char *line = msg->params[1];
	if (!strncmp(line, "- ", 2)) {
		uiFormat(Network, Cold, tagTime(msg), "\3%d-\3\t%s", Gray, &line[2]);
	} else {
		uiFormat(Network, Cold, tagTime(msg), "%s", line);
	}
}

static void handleJoin(struct Message *msg) {
	require(msg, true, 1);
	size_t id = idFor(msg->params[0]);
	if (self.nick && !strcmp(msg->nick, self.nick)) {
		if (!self.user) {
			set(&self.user, msg->user);
			self.color = hash(msg->user);
		}
		idColors[id] = hash(msg->params[0]);
		uiShowID(id);
	}
	uiFormat(
		id, Cold, tagTime(msg),
		"\3%02d%s\3\tarrives in \3%02d%s\3",
		hash(msg->user), msg->nick, hash(msg->params[0]), msg->params[0]
	);
}

static void handlePart(struct Message *msg) {
	require(msg, true, 1);
	uiFormat(
		idFor(msg->params[0]), Cold, tagTime(msg),
		"\3%02d%s\3\tleaves \3%02d%s\3%s%s",
		hash(msg->user), msg->nick, hash(msg->params[0]), msg->params[0],
		(msg->params[1] ? ": " : ""),
		(msg->params[1] ? msg->params[1] : "")
	);
}

static void handleReplyNoTopic(struct Message *msg) {
	require(msg, false, 2);
	uiFormat(
		idFor(msg->params[1]), Cold, tagTime(msg),
		"There is no sign in \3%02d%s\3",
		hash(msg->params[1]), msg->params[1]
	);
}

static void handleReplyTopic(struct Message *msg) {
	require(msg, false, 3);
	uiFormat(
		idFor(msg->params[1]), Cold, tagTime(msg),
		"The sign in \3%02d%s\3 reads: %s",
		hash(msg->params[1]), msg->params[1], msg->params[2]
	);
}

static void handleTopic(struct Message *msg) {
	require(msg, true, 2);
	if (msg->params[1][0]) {
		uiFormat(
			idFor(msg->params[0]), Warm, tagTime(msg),
			"\3%02d%s\3\tplaces a new sign in \3%02d%s\3: %s",
			hash(msg->user), msg->nick, hash(msg->params[0]), msg->params[0],
			msg->params[1]
		);
	} else {
		uiFormat(
			idFor(msg->params[0]), Warm, tagTime(msg),
			"\3%02d%s\3\tremoves the sign in \3%02d%s\3",
			hash(msg->user), msg->nick, hash(msg->params[0]), msg->params[0]
		);
	}
}

static bool isAction(struct Message *msg) {
	if (strncmp(msg->params[1], "\1ACTION ", 8)) return false;
	msg->params[1] += 8;
	size_t len = strlen(msg->params[1]);
	if (msg->params[1][len - 1] == '\1') msg->params[1][len - 1] = '\0';
	return true;
}

static void handlePrivmsg(struct Message *msg) {
	require(msg, true, 2);
	bool query = !strchr(self.chanTypes, msg->params[0][0]);
	bool network = strchr(msg->nick, '.');
	bool mine = self.nick && !strcmp(msg->nick, self.nick);
	size_t id;
	if (query && network) {
		id = Network;
	} else if (query && !mine) {
		id = idFor(msg->nick);
		idColors[id] = hash(msg->user);
	} else {
		id = idFor(msg->params[0]);
	}

	bool notice = (msg->cmd[0] == 'N');
	bool action = isAction(msg);
	uiFormat(
		id, Warm, tagTime(msg),
		"\3%d%s%s%s\3\t%s",
		hash(msg->user),
		(action ? "* " : notice ? "-" : "<"),
		msg->nick,
		(action ? "" : notice ? "-" : ">"),
		msg->params[1]
	);
}

static void handlePing(struct Message *msg) {
	require(msg, false, 1);
	ircFormat("PONG :%s\r\n", msg->params[0]);
}

static void handleError(struct Message *msg) {
	require(msg, false, 1);
	errx(EX_UNAVAILABLE, "%s", msg->params[0]);
}

static const struct Handler {
	const char *cmd;
	Handler *fn;
} Handlers[] = {
	{ "001", handleReplyWelcome },
	{ "005", handleReplyISupport },
	{ "331", handleReplyNoTopic },
	{ "332", handleReplyTopic },
	{ "372", handleReplyMOTD },
	{ "432", handleErrorErroneousNickname },
	{ "433", handleErrorNicknameInUse },
	{ "900", handleReplyLoggedIn },
	{ "904", handleErrorSASLFail },
	{ "905", handleErrorSASLFail },
	{ "906", handleErrorSASLFail },
	{ "AUTHENTICATE", handleAuthenticate },
	{ "CAP", handleCap },
	{ "ERROR", handleError },
	{ "JOIN", handleJoin },
	{ "NOTICE", handlePrivmsg },
	{ "PART", handlePart },
	{ "PING", handlePing },
	{ "PRIVMSG", handlePrivmsg },
	{ "TOPIC", handleTopic },
};

static int compar(const void *cmd, const void *_handler) {
	const struct Handler *handler = _handler;
	return strcmp(cmd, handler->cmd);
}

void handle(struct Message msg) {
	if (!msg.cmd) return;
	const struct Handler *handler = bsearch(
		msg.cmd, Handlers, ARRAY_LEN(Handlers), sizeof(*handler), compar
	);
	if (handler) handler->fn(&msg);
}
