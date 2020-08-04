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
 *
 * Additional permission under GNU GPL version 3 section 7:
 *
 * If you modify this Program, or any covered work, by linking or
 * combining it with OpenSSL (or a modified version of that library),
 * containing parts covered by the terms of the OpenSSL License and the
 * original SSLeay license, the licensors of this Program grant you
 * additional permission to convey the resulting work. Corresponding
 * Source for a non-source form of such a combination shall include the
 * source code for the parts of OpenSSL used as well as that of the
 * covered work.
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
	struct Cat cat = { buf, sizeof(buf), 0 };
	for (size_t i = 0; i < ARRAY_LEN(CapNames); ++i) {
		if (caps & (1 << i)) {
			catf(&cat, "%s%s", (buf[0] ? " " : ""), CapNames[i]);
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
		if (caps & CapConsumer && self.pos) {
			ircFormat("CAP REQ %s=%zu\r\n", CapNames[CapConsumerBit], self.pos);
			caps &= ~CapConsumer;
		}
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
		if (!strcmp(key, "NETWORK")) {
			if (!msg->params[i]) continue;
			set(&network.name, msg->params[i]);
			uiFormat(
				Network, Cold, tagTime(msg),
				"You arrive in %s", msg->params[i]
			);
		} else if (!strcmp(key, "USERLEN")) {
			if (!msg->params[i]) continue;
			network.userLen = strtoul(msg->params[i], NULL, 10);
		} else if (!strcmp(key, "HOSTLEN")) {
			if (!msg->params[i]) continue;
			network.hostLen = strtoul(msg->params[i], NULL, 10);
		} else if (!strcmp(key, "CHANTYPES")) {
			if (!msg->params[i]) continue;
			set(&network.chanTypes, msg->params[i]);
		} else if (!strcmp(key, "PREFIX")) {
			strsep(&msg->params[i], "(");
			char *modes = strsep(&msg->params[i], ")");
			char *prefixes = msg->params[i];
			if (!modes || !prefixes || strlen(modes) != strlen(prefixes)) {
				errx(EX_PROTOCOL, "invalid PREFIX value");
			}
			set(&network.prefixModes, modes);
			set(&network.prefixes, prefixes);
		} else if (!strcmp(key, "CHANMODES")) {
			char *list = strsep(&msg->params[i], ",");
			char *param = strsep(&msg->params[i], ",");
			char *setParam = strsep(&msg->params[i], ",");
			char *channel = strsep(&msg->params[i], ",");
			if (!list || !param || !setParam || !channel) {
				errx(EX_PROTOCOL, "invalid CHANMODES value");
			}
			set(&network.listModes, list);
			set(&network.paramModes, param);
			set(&network.setParamModes, setParam);
			set(&network.channelModes, channel);
		} else if (!strcmp(key, "EXCEPTS")) {
			network.excepts = (msg->params[i] ?: "e")[0];
		} else if (!strcmp(key, "INVEX")) {
			network.invex = (msg->params[i] ?: "I")[0];
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

static void handleErrorNoMOTD(struct Message *msg) {
	(void)msg;
}

static void handleJoin(struct Message *msg) {
	require(msg, true, 1);
	uint id = idFor(msg->params[0]);
	if (!strcmp(msg->nick, self.nick)) {
		if (!self.user || strcmp(self.user, msg->user)) {
			set(&self.user, msg->user);
			self.color = hash(msg->user);
		}
		if (!self.host || strcmp(self.host, msg->host)) {
			set(&self.host, msg->host);
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
		id, ignoreCheck(Cold, id, msg), tagTime(msg),
		"\3%02d%s\3\t%s%s%sarrives in \3%02d%s\3",
		hash(msg->user), msg->nick,
		(msg->params[2] ? "(" : ""),
		(msg->params[2] ?: ""),
		(msg->params[2] ? ") " : ""),
		hash(msg->params[0]), msg->params[0]
	);
	logFormat(id, tagTime(msg), "%s arrives in %s", msg->nick, msg->params[0]);
}

static void handleChghost(struct Message *msg) {
	require(msg, true, 2);
	if (strcmp(msg->nick, self.nick)) return;
	if (!self.user || strcmp(self.user, msg->params[0])) {
		set(&self.user, msg->params[0]);
		self.color = hash(msg->params[0]);
	}
	if (!self.host || strcmp(self.host, msg->params[1])) {
		set(&self.host, msg->params[1]);
	}
}

static void handlePart(struct Message *msg) {
	require(msg, true, 1);
	uint id = idFor(msg->params[0]);
	if (!strcmp(msg->nick, self.nick)) {
		completeClear(id);
	}
	completeRemove(id, msg->nick);
	enum Heat heat = ignoreCheck(Cold, id, msg);
	if (heat > Ice) urlScan(id, msg->nick, msg->params[1]);
	uiFormat(
		id, heat, tagTime(msg),
		"\3%02d%s\3\tleaves \3%02d%s\3%s%s",
		hash(msg->user), msg->nick, hash(msg->params[0]), msg->params[0],
		(msg->params[1] ? ": " : ""), (msg->params[1] ?: "")
	);
	logFormat(
		id, tagTime(msg), "%s leaves %s%s%s",
		msg->nick, msg->params[0],
		(msg->params[1] ? ": " : ""), (msg->params[1] ?: "")
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
		(msg->params[2] ? ": " : ""), (msg->params[2] ?: "")
	);
	logFormat(
		id, tagTime(msg), "%s kicks %s out of %s%s%s",
		msg->nick, msg->params[1], msg->params[0],
		(msg->params[2] ? ": " : ""), (msg->params[2] ?: "")
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
	for (uint id; (id = completeID(msg->nick));) {
		if (!strcmp(idNames[id], msg->nick)) {
			set(&idNames[id], msg->params[0]);
		}
		uiFormat(
			id, ignoreCheck(Cold, id, msg), tagTime(msg),
			"\3%02d%s\3\tis now known as \3%02d%s\3",
			hash(msg->user), msg->nick, hash(msg->user), msg->params[0]
		);
		if (id == Network) continue;
		logFormat(
			id, tagTime(msg), "%s is now known as %s",
			msg->nick, msg->params[0]
		);
	}
	completeReplace(None, msg->nick, msg->params[0]);
}

static void handleQuit(struct Message *msg) {
	require(msg, true, 0);
	for (uint id; (id = completeID(msg->nick));) {
		enum Heat heat = ignoreCheck(Cold, id, msg);
		if (heat > Ice) urlScan(id, msg->nick, msg->params[0]);
		uiFormat(
			id, heat, tagTime(msg),
			"\3%02d%s\3\tleaves%s%s",
			hash(msg->user), msg->nick,
			(msg->params[0] ? ": " : ""), (msg->params[0] ?: "")
		);
		if (id == Network) continue;
		logFormat(
			id, tagTime(msg), "%s leaves%s%s",
			msg->nick,
			(msg->params[0] ? ": " : ""), (msg->params[0] ?: "")
		);
	}
	completeRemove(None, msg->nick);
}

static void handleInvite(struct Message *msg) {
	require(msg, true, 2);
	if (!strcmp(msg->params[0], self.nick)) {
		uiFormat(
			Network, ignoreCheck(Hot, Network, msg), tagTime(msg),
			"\3%02d%s\3\tinvites you to \3%02d%s\3",
			hash(msg->user), msg->nick, hash(msg->params[1]), msg->params[1]
		);
	} else {
		uint id = idFor(msg->params[1]);
		uiFormat(
			id, Cold, tagTime(msg),
			"\3%02d%s\3\tinvites %s to \3%02d%s\3",
			hash(msg->user), msg->nick,
			msg->params[0],
			hash(msg->params[1]), msg->params[1]
		);
		logFormat(
			id, tagTime(msg), "%s invites %s to %s",
			msg->nick, msg->params[0], msg->params[1]
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
	struct Cat cat = { buf, sizeof(buf), 0 };
	while (msg->params[3]) {
		char *name = strsep(&msg->params[3], " ");
		char *prefixes = strsep(&name, "!");
		char *nick = &prefixes[strspn(prefixes, network.prefixes)];
		char *user = strsep(&name, "@");
		enum Color color = (user ? hash(user) : Default);
		completeAdd(id, nick, color);
		if (!replies.names) continue;
		catf(&cat, "%s\3%02d%s\3", (buf[0] ? ", " : ""), color, prefixes);
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

static void topicComplete(uint id, const char *topic) {
	char buf[512];
	const char *prev = complete(id, "/topic ");
	if (prev) {
		snprintf(buf, sizeof(buf), "%s", prev);
		completeRemove(id, buf);
	}
	if (topic) {
		snprintf(buf, sizeof(buf), "/topic %s", topic);
		completeAdd(id, buf, Default);
	}
}

static void handleReplyTopic(struct Message *msg) {
	require(msg, false, 3);
	uint id = idFor(msg->params[1]);
	topicComplete(id, msg->params[2]);
	if (!replies.topic) return;
	replies.topic--;
	urlScan(id, NULL, msg->params[2]);
	uiFormat(
		id, Cold, tagTime(msg),
		"The sign in \3%02d%s\3 reads: %s",
		hash(msg->params[1]), msg->params[1], msg->params[2]
	);
	logFormat(
		id, tagTime(msg), "The sign in %s reads: %s",
		msg->params[1], msg->params[2]
	);
}

static void handleTopic(struct Message *msg) {
	require(msg, true, 2);
	uint id = idFor(msg->params[0]);
	if (msg->params[1][0]) {
		topicComplete(id, msg->params[1]);
		urlScan(id, msg->nick, msg->params[1]);
		uiFormat(
			id, Warm, tagTime(msg),
			"\3%02d%s\3\tplaces a new sign in \3%02d%s\3: %s",
			hash(msg->user), msg->nick, hash(msg->params[0]), msg->params[0],
			msg->params[1]
		);
		logFormat(
			id, tagTime(msg), "%s places a new sign in %s: %s",
			msg->nick, msg->params[0], msg->params[1]
		);
	} else {
		topicComplete(id, NULL);
		uiFormat(
			id, Warm, tagTime(msg),
			"\3%02d%s\3\tremoves the sign in \3%02d%s\3",
			hash(msg->user), msg->nick, hash(msg->params[0]), msg->params[0]
		);
		logFormat(
			id, tagTime(msg), "%s removes the sign in %s",
			msg->nick, msg->params[0]
		);
	}
}

static const char *UserModes[256] = {
	['O'] = "local oper",
	['i'] = "invisible",
	['o'] = "oper",
	['r'] = "registered",
	['w'] = "wallops",
};

static void handleReplyUserModeIs(struct Message *msg) {
	require(msg, false, 2);
	if (!replies.mode) return;
	replies.mode--;

	char buf[1024] = "";
	struct Cat cat = { buf, sizeof(buf), 0 };
	for (char *ch = msg->params[1]; *ch; ++ch) {
		if (*ch == '+') continue;
		const char *name = UserModes[(byte)*ch];
		catf(&cat, ", +%c%s%s", *ch, (name ? " " : ""), (name ?: ""));
	}
	uiFormat(
		Network, Warm, tagTime(msg),
		"\3%02d%s\3\tis %s",
		self.color, self.nick, (buf[0] ? &buf[2] : "modeless")
	);
}

static const char *ChanModes[256] = {
	['a'] = "protected",
	['h'] = "halfop",
	['i'] = "invite-only",
	['k'] = "key",
	['l'] = "client limit",
	['m'] = "moderated",
	['n'] = "no external messages",
	['o'] = "operator",
	['q'] = "founder",
	['s'] = "secret",
	['t'] = "protected topic",
	['v'] = "voice",
};

static void handleReplyChannelModeIs(struct Message *msg) {
	require(msg, false, 3);
	if (!replies.mode) return;
	replies.mode--;

	uint param = 3;
	char buf[1024] = "";
	struct Cat cat = { buf, sizeof(buf), 0 };
	for (char *ch = msg->params[2]; *ch; ++ch) {
		if (*ch == '+') continue;
		const char *name = ChanModes[(byte)*ch];
		if (
			strchr(network.paramModes, *ch) ||
			strchr(network.setParamModes, *ch)
		) {
			assert(param < ParamCap);
			catf(
				&cat, ", +%c%s%s %s",
				*ch, (name ? " " : ""), (name ?: ""),
				msg->params[param++]
			);
		} else {
			catf(
				&cat, ", +%c%s%s",
				*ch, (name ? " " : ""), (name ?: "")
			);
		}
	}
	uiFormat(
		idFor(msg->params[1]), Cold, tagTime(msg),
		"\3%02d%s\3\tis %s",
		hash(msg->params[1]), msg->params[1],
		(buf[0] ? &buf[2] : "modeless")
	);
}

static void handleMode(struct Message *msg) {
	require(msg, true, 2);

	if (!strchr(network.chanTypes, msg->params[0][0])) {
		bool set = true;
		for (char *ch = msg->params[1]; *ch; ++ch) {
			if (*ch == '+') { set = true; continue; }
			if (*ch == '-') { set = false; continue; }
			const char *name = UserModes[(byte)*ch];
			uiFormat(
				Network, Warm, tagTime(msg),
				"\3%02d%s\3\t%ssets \3%02d%s\3 %c%c%s%s",
				hash(msg->user), msg->nick,
				(set ? "" : "un"),
				self.color, msg->params[0],
				set["-+"], *ch, (name ? " " : ""), (name ?: "")
			);
		}
		return;
	}

	uint id = idFor(msg->params[0]);
	bool set = true;
	uint i = 2;
	for (char *ch = msg->params[1]; *ch; ++ch) {
		if (*ch == '+') { set = true; continue; }
		if (*ch == '-') { set = false; continue; }

		const char *verb = (set ? "sets" : "unsets");
		const char *name = ChanModes[(byte)*ch];
		if (*ch == network.excepts) name = "except";
		if (*ch == network.invex) name = "invite";
		const char *mode = (const char[]) {
			set["-+"], *ch, (name ? ' ' : '\0'), '\0'
		};
		if (!name) name = "";

		if (strchr(network.prefixModes, *ch)) {
			if (i >= ParamCap || !msg->params[i]) {
				errx(EX_PROTOCOL, "MODE missing %s parameter", mode);
			}
			char *nick = msg->params[i++];
			char prefix = network.prefixes[
				strchr(network.prefixModes, *ch) - network.prefixModes
			];
			uiFormat(
				id, Cold, tagTime(msg),
				"\3%02d%s\3\t%s \3%02d%c%s\3 %s%s in \3%02d%s\3",
				hash(msg->user), msg->nick, verb,
				completeColor(id, nick), prefix, nick,
				mode, name, hash(msg->params[0]), msg->params[0]
			);
			logFormat(
				id, tagTime(msg), "%s %s %c%s %s%s in %s",
				msg->nick, verb, prefix, nick, mode, name, msg->params[0]
			);
		}

		if (strchr(network.listModes, *ch)) {
			if (i >= ParamCap || !msg->params[i]) {
				errx(EX_PROTOCOL, "MODE missing %s parameter", mode);
			}
			char *mask = msg->params[i++];
			if (*ch == 'b') {
				verb = (set ? "bans" : "unbans");
				uiFormat(
					id, Cold, tagTime(msg),
					"\3%02d%s\3\t%s %c%c %s from \3%02d%s\3",
					hash(msg->user), msg->nick, verb, set["-+"], *ch, mask,
					hash(msg->params[0]), msg->params[0]
				);
				logFormat(
					id, tagTime(msg), "%s %s %c%c %s from %s",
					msg->nick, verb, set["-+"], *ch, mask, msg->params[0]
				);
			} else {
				verb = (set ? "adds" : "removes");
				const char *to = (set ? "to" : "from");
				uiFormat(
					id, Cold, tagTime(msg),
					"\3%02d%s\3\t%s %s %s the \3%02d%s\3 %s%s list",
					hash(msg->user), msg->nick, verb, mask, to,
					hash(msg->params[0]), msg->params[0], mode, name
				);
				logFormat(
					id, tagTime(msg), "%s %s %s %s the %s %s%s list",
					msg->nick, verb, mask, to, msg->params[0], mode, name
				);
			}
		}

		if (strchr(network.paramModes, *ch)) {
			if (i >= ParamCap || !msg->params[i]) {
				errx(EX_PROTOCOL, "MODE missing %s parameter", mode);
			}
			char *param = msg->params[i++];
			uiFormat(
				id, Cold, tagTime(msg),
				"\3%02d%s\3\t%s \3%02d%s\3 %s%s %s",
				hash(msg->user), msg->nick, verb,
				hash(msg->params[0]), msg->params[0], mode, name, param
			);
			logFormat(
				id, tagTime(msg), "%s %s %s %s%s %s",
				msg->nick, verb, msg->params[0], mode, name, param
			);
		}

		if (strchr(network.setParamModes, *ch) && set) {
			if (i >= ParamCap || !msg->params[i]) {
				errx(EX_PROTOCOL, "MODE missing %s parameter", mode);
			}
			char *param = msg->params[i++];
			uiFormat(
				id, Cold, tagTime(msg),
				"\3%02d%s\3\t%s \3%02d%s\3 %s%s %s",
				hash(msg->user), msg->nick, verb,
				hash(msg->params[0]), msg->params[0], mode, name, param
			);
			logFormat(
				id, tagTime(msg), "%s %s %s %s%s %s",
				msg->nick, verb, msg->params[0], mode, name, param
			);
		} else if (strchr(network.setParamModes, *ch)) {
			uiFormat(
				id, Cold, tagTime(msg),
				"\3%02d%s\3\t%s \3%02d%s\3 %s%s",
				hash(msg->user), msg->nick, verb,
				hash(msg->params[0]), msg->params[0], mode, name
			);
			logFormat(
				id, tagTime(msg), "%s %s %s %s%s",
				msg->nick, verb, msg->params[0], mode, name
			);
		}

		if (strchr(network.channelModes, *ch)) {
			uiFormat(
				id, Cold, tagTime(msg),
				"\3%02d%s\3\t%s \3%02d%s\3 %s%s",
				hash(msg->user), msg->nick, verb,
				hash(msg->params[0]), msg->params[0], mode, name
			);
			logFormat(
				id, tagTime(msg), "%s %s %s %s%s",
				msg->nick, verb, msg->params[0], mode, name
			);
		}
	}
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
		"%s", (msg->params[4] ?: msg->params[3])
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
	time_t time = strtol((msg->params[3] ?: ""), NULL, 10);
	strftime(signon, sizeof(signon), "%F %T", localtime(&time));
	uiFormat(
		Network, Warm, tagTime(msg),
		"\3%02d%s\3\tis idle for %lu %s%s%s%s",
		completeColor(Network, msg->params[1]), msg->params[1],
		idle, unit, (idle != 1 ? "s" : ""),
		(msg->params[3] ? ", signed on " : ""), (msg->params[3] ? signon : "")
	);
}

static void handleReplyWhoisChannels(struct Message *msg) {
	require(msg, false, 3);
	if (!replies.whois) return;
	char buf[1024] = "";
	struct Cat cat = { buf, sizeof(buf), 0 };
	while (msg->params[2]) {
		char *channel = strsep(&msg->params[2], " ");
		char *name = &channel[strspn(channel, network.prefixes)];
		catf(&cat, "%s\3%02d%s\3", (buf[0] ? ", " : ""), hash(name), channel);
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
		msg->params[2], (msg->params[3] ? " " : ""), (msg->params[3] ?: "")
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
	logFormat(
		id, tagTime(msg), "%s is away: %s",
		msg->params[1], msg->params[2]
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
		char b = (match[len] ?: ' ');
		if ((isspace(a) || ispunct(a)) && (isspace(b) || ispunct(b))) {
			return true;
		}
		match = &match[len];
	}
	return false;
}

static const char *colorMentions(uint id, struct Message *msg) {
	char *split = strstr(msg->params[1], ": ");
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
	struct Cat cat = { buf, sizeof(buf), 0 };
	while (*mention) {
		size_t skip = strspn(mention, ",<> ");
		catf(&cat, "%.*s", (int)skip, mention);
		mention += skip;

		size_t len = strcspn(mention, ",<> ");
		char punct = mention[len];
		mention[len] = '\0';
		enum Color color = completeColor(id, mention);
		if (color != Default) {
			catf(&cat, "\3%02d%s\3", color, mention);
		} else {
			catf(&cat, "%s", mention);
		}
		mention[len] = punct;
		mention += len;
	}
	catf(&cat, "%c", delimit);
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
	enum Heat heat = ignoreCheck((mention || query ? Hot : Warm), id, msg);
	if (heat > Ice) urlScan(id, msg->nick, msg->params[1]);
	if (notice) {
		if (id != Network) {
			logFormat(id, tagTime(msg), "-%s- %s", msg->nick, msg->params[1]);
		}
		uiFormat(
			id, ignoreCheck(Warm, id, msg), tagTime(msg),
			"\3%d-%s-\3%d\t%s",
			hash(msg->user), msg->nick, LightGray, msg->params[1]
		);
	} else if (action) {
		logFormat(id, tagTime(msg), "* %s %s", msg->nick, msg->params[1]);
		const char *mentions = colorMentions(id, msg);
		uiFormat(
			id, heat, tagTime(msg),
			"%s\35\3%d* %s\17\35\t%s%s",
			(mention ? "\26" : ""), hash(msg->user), msg->nick,
			mentions, msg->params[1]
		);
	} else {
		logFormat(id, tagTime(msg), "<%s> %s", msg->nick, msg->params[1]);
		const char *mentions = colorMentions(id, msg);
		uiFormat(
			id, heat, tagTime(msg),
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
	{ "221", handleReplyUserModeIs },
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
	{ "324", handleReplyChannelModeIs },
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
	{ "422", handleErrorNoMOTD },
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
	{ "CHGHOST", handleChghost },
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

void handle(struct Message *msg) {
	if (!msg->cmd) return;
	if (msg->tags[TagPos]) {
		self.pos = strtoull(msg->tags[TagPos], NULL, 10);
	}
	const struct Handler *handler = bsearch(
		msg->cmd, Handlers, ARRAY_LEN(Handlers), sizeof(*handler), compar
	);
	if (handler) {
		handler->fn(msg);
	} else if (strcmp(msg->cmd, "400") >= 0 && strcmp(msg->cmd, "599") <= 0) {
		handleErrorGeneric(msg);
	}
}
