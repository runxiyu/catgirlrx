/* Copyright (C) 2018  C. McEnroe <june@causal.agency>
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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include "chat.h"

static void privmsg(struct Tag tag, bool action, const char *mesg) {
	char *line;
	int send;
	asprintf(
		&line, ":%s!%s %nPRIVMSG %s :%s%s%s",
		self.nick, self.user, &send, tag.name,
		(action ? "\1ACTION " : ""), mesg, (action ? "\1" : "")
	);
	if (!line) err(EX_OSERR, "asprintf");
	ircFmt("%s\r\n", &line[send]);
	handle(line);
	free(line);
}

typedef void Handler(struct Tag tag, char *params);

static void inputJoin(struct Tag tag, char *params) {
	char *chan = strsep(&params, " ");
	char *key = strsep(&params, " ");
	if (key) {
		ircFmt("JOIN %s %s\r\n", chan, key);
	} else {
		ircFmt("JOIN %s\r\n", chan ? chan : tag.name);
	}
}

static void inputMe(struct Tag tag, char *params) {
	privmsg(tag, true, params ? params : "");
}

static void inputNick(struct Tag tag, char *params) {
	char *nick = strsep(&params, " ");
	if (nick) {
		ircFmt("NICK :%s\r\n", nick);
	} else {
		uiLog(tag, UIHot, L"/nick requires a nickname");
	}
}

static void inputPart(struct Tag tag, char *params) {
	ircFmt("PART %s :%s\r\n", tag.name, params ? params : "Goodbye");
}

static void inputQuery(struct Tag tag, char *params) {
	char *nick = strsep(&params, " ");
	if (nick) {
		tabTouch(TagNone, nick);
		uiShowTag(tagFor(nick));
		logReplay(tagFor(nick));
	} else {
		uiLog(tag, UIHot, L"/query requires a nickname");
	}
}

static void inputQuit(struct Tag tag, char *params) {
	(void)tag;
	ircQuit(params ? params : "Goodbye");
}

static void inputTopic(struct Tag tag, char *params) {
	if (params) {
		ircFmt("TOPIC %s :%s\r\n", tag.name, params);
	} else {
		ircFmt("TOPIC %s\r\n", tag.name);
	}
}

static void inputWho(struct Tag tag, char *params) {
	(void)params;
	ircFmt("WHO :%s\r\n", tag.name);
}

static void inputWhois(struct Tag tag, char *params) {
	char *nick = strsep(&params, " ");
	if (nick) {
		ircFmt("WHOIS :%s\r\n", nick);
	} else {
		uiLog(tag, UIHot, L"/whois requires a nick");
	}
}

static void inputZNC(struct Tag tag, char *params) {
	(void)tag;
	ircFmt("ZNC %s\r\n", params ? params : "");
}

static void inputClose(struct Tag tag, char *params) {
	(void)params;
	uiCloseTag(tag);
	tabRemove(TagNone, tag.name);
}

static void inputMan(struct Tag tag, char *params) {
	(void)tag;
	(void)params;
	eventWait((const char *[]) { "man", "1", "catgirl", NULL });
}

static void inputMove(struct Tag tag, char *params) {
	char *num = strsep(&params, " ");
	if (num) {
		uiMoveTag(tag, strtol(num, NULL, 0), num[0] == '+' || num[0] == '-');
	} else {
		uiLog(tag, UIHot, L"/move requires a number");
	}
}

static void inputOpen(struct Tag tag, char *params) {
	if (params && !isdigit(params[0])) {
		urlOpenMatch(tag, params);
	} else {
		size_t at = (params ? strtoul(strsep(&params, "-,"), NULL, 0) : 1);
		size_t to = (params ? strtoul(params, NULL, 0) : at);
		urlOpenRange(tag, at - 1, to);
	}
}

static void inputRaw(struct Tag tag, char *params) {
	(void)tag;
	if (!self.raw || !params) {
		self.raw ^= true;
		uiFmt(
			TagRaw, UIWarm, "%s window is %s",
			TagRaw.name, (self.raw ? "enabled" : "disabled")
		);
	}
	if (params) ircFmt("%s\r\n", params);
}

static void inputURL(struct Tag tag, char *params) {
	(void)params;
	urlList(tag);
}

static void inputWindow(struct Tag tag, char *params) {
	char *word = strsep(&params, " ");
	if (!word) {
		uiLog(tag, UIHot, L"/window requires a name or number");
		return;
	}
	bool relative = (word[0] == '+' || word[0] == '-');
	char *trail;
	int num = strtol(word, &trail, 0);
	if (!trail[0]) {
		uiShowNum(num, relative);
	} else {
		struct Tag name = tagFind(word);
		if (name.id != TagNone.id) {
			uiShowTag(name);
		} else {
			uiFmt(tag, UIHot, "No window for %s", word);
		}
	}
}

static const struct {
	const char *command;
	Handler *handler;
} Commands[] = {
	{ "/close", inputClose },
	{ "/help", inputMan },
	{ "/join", inputJoin },
	{ "/man", inputMan },
	{ "/me", inputMe },
	{ "/move", inputMove },
	{ "/names", inputWho },
	{ "/nick", inputNick },
	{ "/open", inputOpen },
	{ "/part", inputPart },
	{ "/query", inputQuery },
	{ "/quit", inputQuit },
	{ "/raw", inputRaw },
	{ "/topic", inputTopic },
	{ "/url", inputURL },
	{ "/who", inputWho },
	{ "/whois", inputWhois },
	{ "/window", inputWindow },
	{ "/znc", inputZNC },
};
static const size_t CommandsLen = sizeof(Commands) / sizeof(Commands[0]);

void inputTab(void) {
	for (size_t i = 0; i < CommandsLen; ++i) {
		tabTouch(TagNone, Commands[i].command);
	}
}

void input(struct Tag tag, char *input) {
	bool slash = (input[0] == '/');
	if (slash) {
		char *space = strchr(&input[1], ' ');
		char *extra = strchr(&input[1], '/');
		if (extra && (!space || extra < space)) slash = false;
	}

	if (!slash) {
		if (tag.id == TagRaw.id) {
			ircFmt("%s\r\n", input);
		} else if (tag.id != TagStatus.id) {
			privmsg(tag, false, input);
		}
		return;
	}

	char *word = strsep(&input, " ");
	if (input && !input[0]) input = NULL;

	char *trail;
	strtol(&word[1], &trail, 0);
	if (!trail[0]) {
		inputWindow(tag, &word[1]);
		return;
	}

	const char *command = word;
	const char *uniq = tabNext(TagNone, command);
	if (uniq && tabNext(TagNone, command) == uniq) {
		command = uniq;
		tabAccept();
	} else {
		tabReject();
	}

	for (size_t i = 0; i < CommandsLen; ++i) {
		if (strcasecmp(command, Commands[i].command)) continue;
		Commands[i].handler(tag, input);
		return;
	}
	uiFmt(tag, UIHot, "%s isn't a recognized command", command);
}
