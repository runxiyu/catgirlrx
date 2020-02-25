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

static void require(struct Message *msg, bool origin, uint len) {
	if (origin) {
		if (!msg->nick) errx(EX_PROTOCOL, "%s missing origin", msg->cmd);
		if (!msg->user) msg->user = msg->nick;
		if (!msg->host) msg->host = msg->user;
	}
	for (uint i = 0; i < len; ++i) {
		if (msg->params[i]) continue;
		errx(EX_PROTOCOL, "%s missing parameter %u", msg->cmd, 1 + i);
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

static void handleErrorGeneric(struct Message *msg) {
	require(msg, false, 2);
	if (msg->params[2]) {
		size_t len = strlen(msg->params[2]);
		if (msg->params[2][len - 1] == '.') msg->params[2][len - 1] = '\0';
		uiFormat(
			Network, Warm, tagTime(msg),
			"%s: %s", msg->params[2], msg->params[1]
		);
	} else {
		uiFormat(
			Network, Warm, tagTime(msg),
			"%s", msg->params[1]
		);
	}
}

static void handleErrorNicknameInUse(struct Message *msg) {
	require(msg, false, 2);
	if (!strcmp(self.nick, "*")) {
		ircFormat("NICK :%s_\r\n", msg->params[1]);
	} else {
		handleErrorGeneric(msg);
	}
}

static void handleErrorErroneousNickname(struct Message *msg) {
	require(msg, false, 3);
	if (!strcmp(self.nick, "*")) {
		errx(EX_CONFIG, "%s: %s", msg->params[1], msg->params[2]);
	} else {
		handleErrorGeneric(msg);
	}
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

#define BASE64_SIZE(len) (1 + ((len) + 2) / 3 * 4)

static void base64(char *dst, const byte *src, size_t len) {
	static const char Base64[64] = {
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
	};
	size_t i = 0;
	while (len > 2) {
		dst[i++] = Base64[0x3F & (src[0] >> 2)];
		dst[i++] = Base64[0x3F & (src[0] << 4 | src[1] >> 4)];
		dst[i++] = Base64[0x3F & (src[1] << 2 | src[2] >> 6)];
		dst[i++] = Base64[0x3F & src[2]];
		src += 3;
		len -= 3;
	}
	if (len) {
		dst[i++] = Base64[0x3F & (src[0] >> 2)];
		if (len > 1) {
			dst[i++] = Base64[0x3F & (src[0] << 4 | src[1] >> 4)];
			dst[i++] = Base64[0x3F & (src[1] << 2)];
		} else {
			dst[i++] = Base64[0x3F & (src[0] << 4)];
			dst[i++] = '=';
		}
		dst[i++] = '=';
	}
	dst[i] = '\0';
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
	completeTouch(Network, self.nick, Default);
	if (self.join) {
		uint count = 1;
		for (const char *ch = self.join; *ch && *ch != ' '; ++ch) {
			if (*ch == ',') count++;
		}
		ircFormat("JOIN %s\r\n", self.join);
		replies.join += count;
		replies.topic += count;
		replies.names += count;
	}
}

static void handleReplyISupport(struct Message *msg) {
	for (uint i = 1; i < ParamCap; ++i) {
		if (!msg->params[i]) break;
		char *key = strsep(&msg->params[i], "=");
		if (!msg->params[i]) continue;
		if (!strcmp(key, "NETWORK")) {
			set(&network.name, msg->params[i]);
			uiFormat(
				Network, Cold, tagTime(msg),
				"You arrive in %s", msg->params[i]
			);
		} else if (!strcmp(key, "CHANTYPES")) {
			set(&network.chanTypes, msg->params[i]);
		} else if (!strcmp(key, "PREFIX")) {
			strsep(&msg->params[i], "(");
			set(&network.prefixModes, strsep(&msg->params[i], ")"));
			set(&network.prefixes, msg->params[i]);
			assert(strlen(network.prefixes) == strlen(network.prefixModes));
		} else if (!strcmp(key, "CHANMODES")) {
			set(&network.listModes, strsep(&msg->params[i], ","));
			set(&network.paramModes, strsep(&msg->params[i], ","));
			set(&network.setParamModes, strsep(&msg->params[i], ","));
			set(&network.channelModes, strsep(&msg->params[i], ","));
		} else if (!strcmp(key, "EXCEPTS")) {
			network.excepts = (msg->params[i] ? msg->params[i][0] : 'e');
		} else if (!strcmp(key, "INVEX")) {
			network.invex = (msg->params[i] ? msg->params[i][0] : 'I');
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
	uint id = idFor(msg->params[0]);
	if (!strcmp(msg->nick, self.nick)) {
		if (!self.user) {
			set(&self.user, msg->user);
			self.color = hash(msg->user);
		}
		idColors[id] = hash(msg->params[0]);
		completeTouch(None, msg->params[0], idColors[id]);
		if (replies.join) {
			uiShowID(id);
			replies.join--;
		}
	}
	completeTouch(id, msg->nick, hash(msg->user));
	if (msg->params[2] && !strcasecmp(msg->params[2], msg->nick)) {
		msg->params[2] = NULL;
	}
	uiFormat(
		id, Cold, tagTime(msg),
		"\3%02d%s\3\t%s%s%sarrives in \3%02d%s\3",
		hash(msg->user), msg->nick,
		(msg->params[2] ? "(" : ""),
		(msg->params[2] ? msg->params[2] : ""),
		(msg->params[2] ? ") " : ""),
		hash(msg->params[0]), msg->params[0]
	);
}

static void handlePart(struct Message *msg) {
	require(msg, true, 1);
	uint id = idFor(msg->params[0]);
	if (!strcmp(msg->nick, self.nick)) {
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
	uint id = idFor(msg->params[0]);
	bool kicked = !strcmp(msg->params[1], self.nick);
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
	if (!strcmp(msg->nick, self.nick)) {
		set(&self.nick, msg->params[0]);
		uiRead(); // Update prompt.
	}
	uint id;
	while (None != (id = completeID(msg->nick))) {
		if (!strcmp(idNames[id], msg->nick)) {
			set(&idNames[id], msg->params[0]);
		}
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
	uint id;
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

static void handleInvite(struct Message *msg) {
	require(msg, true, 2);
	if (!strcmp(msg->params[0], self.nick)) {
		uiFormat(
			Network, Hot, tagTime(msg),
			"\3%02d%s\3\tinvites you to \3%02d%s\3",
			hash(msg->user), msg->nick, hash(msg->params[1]), msg->params[1]
		);
	} else {
		uiFormat(
			idFor(msg->params[1]), Cold, tagTime(msg),
			"\3%02d%s\3\tinvites %s to \3%02d%s\3",
			hash(msg->user), msg->nick,
			msg->params[0],
			hash(msg->params[1]), msg->params[1]
		);
	}
}

static void handleReplyInviting(struct Message *msg) {
	require(msg, false, 3);
	if (self.caps & CapInviteNotify) return;
	struct Message invite = {
		.nick = self.nick,
		.user = self.user,
		.cmd = "INVITE",
		.params[0] = msg->params[1],
		.params[1] = msg->params[2],
	};
	handleInvite(&invite);
}

static void handleErrorUserOnChannel(struct Message *msg) {
	require(msg, false, 4);
	uint id = idFor(msg->params[2]);
	uiFormat(
		id, Cold, tagTime(msg),
		"\3%02d%s\3 is already in \3%02d%s\3",
		completeColor(id, msg->params[1]), msg->params[1],
		hash(msg->params[2]), msg->params[2]
	);
}

static void handleReplyNames(struct Message *msg) {
	require(msg, false, 4);
	uint id = idFor(msg->params[2]);
	char buf[1024] = "";
	while (msg->params[3]) {
		char *name = strsep(&msg->params[3], " ");
		char *prefixes = strsep(&name, "!");
		char *nick = &prefixes[strspn(prefixes, network.prefixes)];
		char *user = strsep(&name, "@");
		enum Color color = (user ? hash(user) : Default);
		completeAdd(id, nick, color);
		if (!replies.names) continue;
		catf(
			buf, sizeof(buf), "%s\3%02d%s\3",
			(buf[0] ? ", " : ""), color, prefixes
		);
	}
	if (!replies.names) return;
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
	uint id = idFor(msg->params[1]);
	urlScan(id, NULL, msg->params[2]);
	uiFormat(
		id, Cold, tagTime(msg),
		"The sign in \3%02d%s\3 reads: %s",
		hash(msg->params[1]), msg->params[1], msg->params[2]
	);
}

static void handleTopic(struct Message *msg) {
	require(msg, true, 2);
	uint id = idFor(msg->params[0]);
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

static void handleMode(struct Message *msg) {
	require(msg, true, 2);
	if (!strchr(network.chanTypes, msg->params[0][0])) {
		// TODO: User mode changes.
		return;
	}

	uint id = idFor(msg->params[0]);
	bool set = false;
	uint param = 2;
	char buf[1024] = "";
	for (char *ch = msg->params[1]; *ch; ++ch) {
		if (*ch == '+') {
			set = true;
		} else if (*ch == '-') {
			set = false;

		} else if (strchr(network.prefixModes, *ch)) {
			assert(param < ParamCap);
			char *nick = msg->params[param++];
			char *mode = strchr(network.prefixModes, *ch);
			char prefix = network.prefixes[mode - network.prefixModes];
			catf(
				buf, sizeof(buf), ", %s \3%02d%c%s\3",
				(set ? "grants" : "revokes"),
				completeColor(id, nick), prefix, nick
			);

		} else if (strchr(network.listModes, *ch)) {
			assert(param < ParamCap);
			char *mask = msg->params[param++];
			if (*ch == 'b') {
				catf(
					buf, sizeof(buf), ", %s %s from \3%02d%s\3",
					(set ? "bans" : "unbans"),
					mask,
					hash(msg->params[0]), msg->params[0]
				);
				continue;
			}
			const char *list = (const char[]) { *ch, '\0' };
			if (*ch == network.excepts) list = "except";
			if (*ch == network.invex) list = "invite";
			catf(
				buf, sizeof(buf), ", %s %s %s the \3%02d%s\3 %s list",
				(set ? "adds" : "removes"),
				mask,
				(set ? "to" : "from"),
				hash(msg->params[0]), msg->params[0],
				list
			);

		} else if (strchr(network.paramModes, *ch)) {
			// TODO
		} else if (strchr(network.setParamModes, *ch)) {
			// TODO
		} else {
			// TODO
		}
	}
	if (strlen(buf) < 2) return;
	uiFormat(
		id, Cold, tagTime(msg),
		"\3%02d%s\3\t%s", hash(msg->user), msg->nick, &buf[2]
	);
}

static void handleErrorChanopPrivsNeeded(struct Message *msg) {
	require(msg, false, 3);
	uiFormat(
		idFor(msg->params[1]), Cold, tagTime(msg),
		"%s", msg->params[2]
	);
}

static void handleErrorUserNotInChannel(struct Message *msg) {
	require(msg, false, 4);
	uiFormat(
		idFor(msg->params[2]), Cold, tagTime(msg),
		"%s\tis not in \3%02d%s\3",
		msg->params[1], hash(msg->params[2]), msg->params[2]
	);
}

static void handleErrorBanListFull(struct Message *msg) {
	require(msg, false, 4);
	uiFormat(
		idFor(msg->params[1]), Cold, tagTime(msg),
		"%s", (msg->params[4] ? msg->params[4] : msg->params[3])
	);
}

static void handleReplyBanList(struct Message *msg) {
	require(msg, false, 3);
	if (!replies.ban) return;
	uint id = idFor(msg->params[1]);
	if (msg->params[3] && msg->params[4]) {
		char since[sizeof("0000-00-00 00:00:00")];
		time_t time = strtol(msg->params[4], NULL, 10);
		strftime(since, sizeof(since), "%F %T", localtime(&time));
		uiFormat(
			id, Cold, tagTime(msg),
			"Banned from \3%02d%s\3 since %s by \3%02d%s\3: %s",
			hash(msg->params[1]), msg->params[1],
			since, completeColor(id, msg->params[3]), msg->params[3],
			msg->params[2]
		);
	} else {
		uiFormat(
			id, Cold, tagTime(msg),
			"Banned from \3%02d%s\3: %s",
			hash(msg->params[1]), msg->params[1], msg->params[2]
		);
	}
}

static void handleReplyEndOfBanList(struct Message *msg) {
	(void)msg;
	if (replies.ban) replies.ban--;
}

static void onList(const char *list, struct Message *msg) {
	uint id = idFor(msg->params[1]);
	if (msg->params[3] && msg->params[4]) {
		char since[sizeof("0000-00-00 00:00:00")];
		time_t time = strtol(msg->params[4], NULL, 10);
		strftime(since, sizeof(since), "%F %T", localtime(&time));
		uiFormat(
			id, Cold, tagTime(msg),
			"On the \3%02d%s\3 %s list since %s by \3%02d%s\3: %s",
			hash(msg->params[1]), msg->params[1], list,
			since, completeColor(id, msg->params[3]), msg->params[3],
			msg->params[2]
		);
	} else {
		uiFormat(
			id, Cold, tagTime(msg),
			"On the \3%02d%s\3 %s list: %s",
			hash(msg->params[1]), msg->params[1], list, msg->params[2]
		);
	}
}

static void handleReplyExceptList(struct Message *msg) {
	require(msg, false, 3);
	if (!replies.excepts) return;
	onList("except", msg);
}

static void handleReplyEndOfExceptList(struct Message *msg) {
	(void)msg;
	if (replies.excepts) replies.excepts--;
}

static void handleReplyInviteList(struct Message *msg) {
	require(msg, false, 3);
	if (!replies.invex) return;
	onList("invite", msg);
}

static void handleReplyEndOfInviteList(struct Message *msg) {
	(void)msg;
	if (replies.invex) replies.invex--;
}

static void handleReplyList(struct Message *msg) {
	require(msg, false, 4);
	if (!replies.list) return;
	uiFormat(
		Network, Warm, tagTime(msg),
		"In \3%02d%s\3 are %ld under the banner: %s",
		hash(msg->params[1]), msg->params[1],
		strtol(msg->params[2], NULL, 10),
		msg->params[3]
	);
}

static void handleReplyListEnd(struct Message *msg) {
	(void)msg;
	if (!replies.list) return;
	replies.list--;
}

static void handleReplyWhoisUser(struct Message *msg) {
	require(msg, false, 6);
	if (!replies.whois) return;
	completeTouch(Network, msg->params[1], hash(msg->params[2]));
	uiFormat(
		Network, Warm, tagTime(msg),
		"\3%02d%s\3\tis %s!%s@%s (%s)",
		hash(msg->params[2]), msg->params[1],
		msg->params[1], msg->params[2], msg->params[3], msg->params[5]
	);
}

static void handleReplyWhoisServer(struct Message *msg) {
	require(msg, false, 4);
	if (!replies.whois) return;
	uiFormat(
		Network, Warm, tagTime(msg),
		"\3%02d%s\3\tis connected to %s (%s)",
		completeColor(Network, msg->params[1]), msg->params[1],
		msg->params[2], msg->params[3]
	);
}

static void handleReplyWhoisIdle(struct Message *msg) {
	require(msg, false, 3);
	if (!replies.whois) return;
	unsigned long idle = strtoul(msg->params[2], NULL, 10);
	const char *unit = "second";
	if (idle / 60) {
		idle /= 60; unit = "minute";
		if (idle / 60) {
			idle /= 60; unit = "hour";
			if (idle / 24) {
				idle /= 24; unit = "day";
			}
		}
	}
	char signon[sizeof("0000-00-00 00:00:00")];
	time_t time = (msg->params[3] ? strtol(msg->params[3], NULL, 10) : 0);
	strftime(signon, sizeof(signon), "%F %T", localtime(&time));
	uiFormat(
		Network, Warm, tagTime(msg),
		"\3%02d%s\3\tis idle for %lu %s%s%s%s",
		completeColor(Network, msg->params[1]), msg->params[1],
		idle, unit, (idle != 1 ? "s" : ""),
		(msg->params[3] ? ", signed on " : ""),
		(msg->params[3] ? signon : "")
	);
}

static void handleReplyWhoisChannels(struct Message *msg) {
	require(msg, false, 3);
	if (!replies.whois) return;
	char buf[1024] = "";
	while (msg->params[2]) {
		char *channel = strsep(&msg->params[2], " ");
		char *name = &channel[strspn(channel, network.prefixes)];
		catf(
			buf, sizeof(buf), "%s\3%02d%s\3",
			(buf[0] ? ", " : ""), hash(name), channel
		);
	}
	uiFormat(
		Network, Warm, tagTime(msg),
		"\3%02d%s\3\tis in %s",
		completeColor(Network, msg->params[1]), msg->params[1], buf
	);
}

static void handleReplyWhoisGeneric(struct Message *msg) {
	require(msg, false, 3);
	if (!replies.whois) return;
	if (msg->params[3]) {
		msg->params[0] = msg->params[2];
		msg->params[2] = msg->params[3];
		msg->params[3] = msg->params[0];
	}
	uiFormat(
		Network, Warm, tagTime(msg),
		"\3%02d%s\3\t%s%s%s",
		completeColor(Network, msg->params[1]), msg->params[1],
		msg->params[2],
		(msg->params[3] ? " " : ""),
		(msg->params[3] ? msg->params[3] : "")
	);
}

static void handleReplyEndOfWhois(struct Message *msg) {
	require(msg, false, 2);
	if (!replies.whois) return;
	if (strcmp(msg->params[1], self.nick)) {
		completeRemove(Network, msg->params[1]);
	}
	replies.whois--;
}

static void handleReplyAway(struct Message *msg) {
	require(msg, false, 3);
	// Might be part of a WHOIS response.
	uint id;
	if (completeColor(Network, msg->params[1]) != Default) {
		id = Network;
	} else {
		id = idFor(msg->params[1]);
	}
	uiFormat(
		id, Warm, tagTime(msg),
		"\3%02d%s\3\tis away: %s",
		completeColor(id, msg->params[1]), msg->params[1], msg->params[2]
	);
}

static void handleReplyNowAway(struct Message *msg) {
	require(msg, false, 2);
	if (!replies.away) return;
	uiFormat(Network, Warm, tagTime(msg), "%s", msg->params[1]);
	replies.away--;
}

static bool isAction(struct Message *msg) {
	if (strncmp(msg->params[1], "\1ACTION ", 8)) return false;
	msg->params[1] += 8;
	size_t len = strlen(msg->params[1]);
	if (msg->params[1][len - 1] == '\1') msg->params[1][len - 1] = '\0';
	return true;
}

static bool isMention(const struct Message *msg) {
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

static const char *colorMentions(uint id, struct Message *msg) {
	char *split = strchr(msg->params[1], ':');
	if (!split) {
		split = strchr(msg->params[1], ' ');
		if (split) split = strchr(&split[1], ' ');
	}
	if (!split) split = &msg->params[1][strlen(msg->params[1])];
	for (char *ch = msg->params[1]; ch < split; ++ch) {
		if (iscntrl(*ch)) return "";
	}
	char delimit = *split;
	char *mention = msg->params[1];
	msg->params[1] = (delimit ? &split[1] : split);
	*split = '\0';

	static char buf[1024];
	buf[0] = '\0';
	while (*mention) {
		size_t skip = strspn(mention, ",<> ");
		catf(buf, sizeof(buf), "%.*s", (int)skip, mention);
		mention += skip;

		size_t len = strcspn(mention, ",<> ");
		char punct = mention[len];
		mention[len] = '\0';
		enum Color color = completeColor(id, mention);
		if (color != Default) {
			catf(buf, sizeof(buf), "\3%02d%s\3", color, mention);
		} else {
			catf(buf, sizeof(buf), "%s", mention);
		}
		mention[len] = punct;
		mention += len;
	}
	catf(buf, sizeof(buf), "%c", delimit);
	return buf;
}

static void handlePrivmsg(struct Message *msg) {
	require(msg, true, 2);
	bool query = !strchr(network.chanTypes, msg->params[0][0]);
	bool server = strchr(msg->nick, '.');
	bool mine = !strcmp(msg->nick, self.nick);
	uint id;
	if (query && server) {
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
			"\3%d-%s-\3%d\t%s",
			hash(msg->user), msg->nick, LightGray, msg->params[1]
		);
	} else if (action) {
		const char *mentions = colorMentions(id, msg);
		uiFormat(
			id, (mention || query ? Hot : Warm), tagTime(msg),
			"%s\35\3%d* %s\17\35\t%s%s",
			(mention ? "\26" : ""), hash(msg->user), msg->nick,
			mentions, msg->params[1]
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
	{ "276", handleReplyWhoisGeneric },
	{ "301", handleReplyAway },
	{ "305", handleReplyNowAway },
	{ "306", handleReplyNowAway },
	{ "307", handleReplyWhoisGeneric },
	{ "311", handleReplyWhoisUser },
	{ "312", handleReplyWhoisServer },
	{ "313", handleReplyWhoisGeneric },
	{ "317", handleReplyWhoisIdle },
	{ "318", handleReplyEndOfWhois },
	{ "319", handleReplyWhoisChannels },
	{ "322", handleReplyList },
	{ "323", handleReplyListEnd },
	{ "330", handleReplyWhoisGeneric },
	{ "331", handleReplyNoTopic },
	{ "332", handleReplyTopic },
	{ "341", handleReplyInviting },
	{ "346", handleReplyInviteList },
	{ "347", handleReplyEndOfInviteList },
	{ "348", handleReplyExceptList },
	{ "349", handleReplyEndOfExceptList },
	{ "353", handleReplyNames },
	{ "366", handleReplyEndOfNames },
	{ "367", handleReplyBanList },
	{ "368", handleReplyEndOfBanList },
	{ "372", handleReplyMOTD },
	{ "378", handleReplyWhoisGeneric },
	{ "379", handleReplyWhoisGeneric },
	{ "432", handleErrorErroneousNickname },
	{ "433", handleErrorNicknameInUse },
	{ "441", handleErrorUserNotInChannel },
	{ "443", handleErrorUserOnChannel },
	{ "478", handleErrorBanListFull },
	{ "482", handleErrorChanopPrivsNeeded },
	{ "671", handleReplyWhoisGeneric },
	{ "900", handleReplyLoggedIn },
	{ "904", handleErrorSASLFail },
	{ "905", handleErrorSASLFail },
	{ "906", handleErrorSASLFail },
	{ "AUTHENTICATE", handleAuthenticate },
	{ "CAP", handleCap },
	{ "ERROR", handleError },
	{ "INVITE", handleInvite },
	{ "JOIN", handleJoin },
	{ "KICK", handleKick },
	{ "MODE", handleMode },
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
	if (handler) {
		handler->fn(&msg);
		return;
	}
	if (strcmp(msg.cmd, "400") >= 0 && strcmp(msg.cmd, "599") <= 0) {
		handleErrorGeneric(&msg);
	}
}
