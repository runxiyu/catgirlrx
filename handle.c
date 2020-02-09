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
#include <ctype.h>
#include <err.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include "chat.h"

struct Replies replies;

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
	completeTouch(None, self.nick, Default);
	if (self.join) {
		size_t count = 1;
		for (const char *ch = self.join; *ch && *ch != ' '; ++ch) {
			if (*ch == ',') count++;
		}
		ircFormat("JOIN %s\r\n", self.join);
		replies.topic += count;
		replies.names += count;
	}
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
	urlScan(Network, msg->nick, line);
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
		completeTouch(None, msg->params[0], idColors[id]);
		uiShowID(id);
	}
	completeTouch(id, msg->nick, hash(msg->user));
	uiFormat(
		id, Cold, tagTime(msg),
		"\3%02d%s\3\tarrives in \3%02d%s\3",
		hash(msg->user), msg->nick, hash(msg->params[0]), msg->params[0]
	);
}

static void handlePart(struct Message *msg) {
	require(msg, true, 1);
	size_t id = idFor(msg->params[0]);
	if (self.nick && !strcmp(msg->nick, self.nick)) {
		completeClear(id);
	}
	completeRemove(id, msg->nick);
	urlScan(id, msg->nick, msg->params[1]);
	uiFormat(
		id, Cold, tagTime(msg),
		"\3%02d%s\3\tleaves \3%02d%s\3%s%s",
		hash(msg->user), msg->nick, hash(msg->params[0]), msg->params[0],
		(msg->params[1] ? ": " : ""),
		(msg->params[1] ? msg->params[1] : "")
	);
}

static void handleKick(struct Message *msg) {
	require(msg, true, 2);
	size_t id = idFor(msg->params[0]);
	bool kicked = self.nick && !strcmp(msg->params[1], self.nick);
	completeTouch(id, msg->nick, hash(msg->user));
	urlScan(id, msg->nick, msg->params[2]);
	uiFormat(
		id, (kicked ? Hot : Cold), tagTime(msg),
		"%s\3%02d%s\17\tkicks \3%02d%s\3 out of \3%02d%s\3%s%s",
		(kicked ? "\26" : ""),
		hash(msg->user), msg->nick,
		completeColor(id, msg->params[1]), msg->params[1],
		hash(msg->params[0]), msg->params[0],
		(msg->params[2] ? ": " : ""),
		(msg->params[2] ? msg->params[2] : "")
	);
	completeRemove(id, msg->params[1]);
	if (kicked) completeClear(id);
}

static void handleNick(struct Message *msg) {
	require(msg, true, 1);
	if (self.nick && !strcmp(msg->nick, self.nick)) {
		set(&self.nick, msg->params[0]);
	}
	size_t id;
	while (None != (id = completeID(msg->nick))) {
		uiFormat(
			id, Cold, tagTime(msg),
			"\3%02d%s\3\tis now known as \3%02d%s\3",
			hash(msg->user), msg->nick, hash(msg->user), msg->params[0]
		);
	}
	completeReplace(None, msg->nick, msg->params[0]);
}

static void handleQuit(struct Message *msg) {
	require(msg, true, 0);
	size_t id;
	while (None != (id = completeID(msg->nick))) {
		urlScan(id, msg->nick, msg->params[0]);
		uiFormat(
			id, Cold, tagTime(msg),
			"\3%02d%s\3\tleaves%s%s",
			hash(msg->user), msg->nick,
			(msg->params[0] ? ": " : ""),
			(msg->params[0] ? msg->params[0] : "")
		);
	}
	completeRemove(None, msg->nick);
}

static void handleReplyNames(struct Message *msg) {
	require(msg, false, 4);
	if (!replies.names) return;
	size_t id = idFor(msg->params[2]);
	char buf[1024];
	size_t len = 0;
	while (msg->params[3]) {
		char *name = strsep(&msg->params[3], " ");
		name += strspn(name, self.prefixes);
		char *nick = strsep(&name, "!");
		char *user = strsep(&name, "@");
		enum Color color = (user ? hash(user) : Default);
		completeAdd(id, nick, color);
		int n = snprintf(
			&buf[len], sizeof(buf) - len,
			"%s\3%02d%s\3", (len ? ", " : ""), color, nick
		);
		assert(n > 0 && len + n < sizeof(buf));
		len += n;
	}
	uiFormat(
		id, Cold, tagTime(msg),
		"In \3%02d%s\3 are %s",
		hash(msg->params[2]), msg->params[2], buf
	);
}

static void handleReplyEndOfNames(struct Message *msg) {
	(void)msg;
	if (replies.names) replies.names--;
}

static void handleReplyNoTopic(struct Message *msg) {
	require(msg, false, 2);
	if (!replies.topic) return;
	replies.topic--;
	uiFormat(
		idFor(msg->params[1]), Cold, tagTime(msg),
		"There is no sign in \3%02d%s\3",
		hash(msg->params[1]), msg->params[1]
	);
}

static void handleReplyTopic(struct Message *msg) {
	require(msg, false, 3);
	if (!replies.topic) return;
	replies.topic--;
	size_t id = idFor(msg->params[1]);
	urlScan(id, NULL, msg->params[2]);
	uiFormat(
		id, Cold, tagTime(msg),
		"The sign in \3%02d%s\3 reads: %s",
		hash(msg->params[1]), msg->params[1], msg->params[2]
	);
}

static void handleTopic(struct Message *msg) {
	require(msg, true, 2);
	size_t id = idFor(msg->params[0]);
	if (msg->params[1][0]) {
		urlScan(id, msg->nick, msg->params[1]);
		uiFormat(
			id, Warm, tagTime(msg),
			"\3%02d%s\3\tplaces a new sign in \3%02d%s\3: %s",
			hash(msg->user), msg->nick, hash(msg->params[0]), msg->params[0],
			msg->params[1]
		);
	} else {
		uiFormat(
			id, Warm, tagTime(msg),
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

static bool isMention(const struct Message *msg) {
	if (!self.nick) return false;
	size_t len = strlen(self.nick);
	const char *match = msg->params[1];
	while (NULL != (match = strcasestr(match, self.nick))) {
		char a = (match > msg->params[1] ? match[-1] : ' ');
		char b = (match[len] ? match[len] : ' ');
		if ((isspace(a) || ispunct(a)) && (isspace(b) || ispunct(b))) {
			return true;
		}
		match = &match[len];
	}
	return false;
}

static const char *colorMentions(size_t id, struct Message *msg) {
	char *mention;
	char final;
	if (strchr(msg->params[1], ':')) {
		mention = strsep(&msg->params[1], ":");
		final = ':';
	} else if (strchr(msg->params[1], ' ')) {
		mention = strsep(&msg->params[1], " ");
		final = ' ';
	} else {
		mention = msg->params[1];
		msg->params[1] = "";
		final = '\0';
	}

	static char buf[1024];
	size_t len = 0;
	while (*mention) {
		size_t skip = strspn(mention, ", ");
		int n = snprintf(
			&buf[len], sizeof(buf) - len,
			"%.*s", (int)skip, mention
		);
		assert(n >= 0 && len + n < sizeof(buf));
		len += n;
		mention += skip;

		size_t word = strcspn(mention, ", ");
		char punct = mention[word];
		mention[word] = '\0';

		n = snprintf(
			&buf[len], sizeof(buf) - len,
			"\3%02d%s\3", completeColor(id, mention), mention
		);
		assert(n > 0 && len + n < sizeof(buf));
		len += n;

		mention[word] = punct;
		mention += word;
	}
	assert(len + 1 < sizeof(buf));
	buf[len++] = final;
	buf[len] = '\0';
	return buf;
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
	bool mention = !mine && isMention(msg);
	if (!notice && !mine) completeTouch(id, msg->nick, hash(msg->user));
	urlScan(id, msg->nick, msg->params[1]);
	if (notice) {
		uiFormat(
			id, Warm, tagTime(msg),
			"%s\3%d-%s-\17\3%d\t%s",
			(mention ? "\26" : ""), hash(msg->user), msg->nick,
			LightGray, msg->params[1]
		);
	} else if (action) {
		uiFormat(
			id, (mention || query ? Hot : Warm), tagTime(msg),
			"%s\35\3%d* %s\17\35\t%s",
			(mention ? "\26" : ""), hash(msg->user), msg->nick, msg->params[1]
		);
	} else {
		const char *mentions = colorMentions(id, msg);
		uiFormat(
			id, (mention || query ? Hot : Warm), tagTime(msg),
			"%s\3%d<%s>\17\t%s%s",
			(mention ? "\26" : ""), hash(msg->user), msg->nick,
			mentions, msg->params[1]
		);
	}
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
	{ "353", handleReplyNames },
	{ "366", handleReplyEndOfNames },
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
	{ "KICK", handleKick },
	{ "NICK", handleNick },
	{ "NOTICE", handlePrivmsg },
	{ "PART", handlePart },
	{ "PING", handlePing },
	{ "PRIVMSG", handlePrivmsg },
	{ "QUIT", handleQuit },
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
