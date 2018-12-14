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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include "chat.h"

static void privmsg(struct Tag tag, bool action, const char *mesg) {
	if (tag.id == TagStatus.id || tag.id == TagRaw.id) return;
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

static char *
param(struct Tag tag, const char *command, char **params, const char *name) {
	char *param = strsep(params, " ");
	if (param) return param;
	uiFmt(tag, UIHot, "%s requires a %s", command, name);
	return NULL;
}

typedef void (*Handler)(struct Tag tag, char *params);

static void inputRaw(struct Tag tag, char *params) {
	(void)tag;
	if (!params || !self.raw) {
		self.raw ^= true;
		uiFmt(
			TagRaw, UIWarm, "Raw view is %s",
			self.raw ? "enabled" : "disabled"
		);
	}
	if (params) ircFmt("%s\r\n", params);
}

static void inputMe(struct Tag tag, char *params) {
	privmsg(tag, true, params ? params : "");
}

static void inputNick(struct Tag tag, char *params) {
	(void)tag;
	char *nick = param(tag, "/nick", &params, "name");
	if (!nick) return;
	ircFmt("NICK %s\r\n", nick);
}

static void inputJoin(struct Tag tag, char *params) {
	(void)tag;
	char *chan = param(tag, "/join", &params, "channel");
	if (!chan) return;
	ircFmt("JOIN %s\r\n", chan);
}

static void inputPart(struct Tag tag, char *params) {
	if (params) {
		ircFmt("PART %s :%s\r\n", tag.name, params);
	} else {
		ircFmt("PART %s :Goodbye\r\n", tag.name);
	}
}

static void inputQuery(struct Tag tag, char *params) {
	(void)tag;
	char *nick = param(tag, "/query", &params, "nick");
	if (!nick) return;
	tabTouch(TagNone, nick);
	uiViewTag(tagFor(nick));
	logReplay(tagFor(nick));
}

static void inputWho(struct Tag tag, char *params) {
	(void)params;
	ircFmt("WHO %s\r\n", tag.name);
}

static void inputWhois(struct Tag tag, char *params) {
	(void)tag;
	char *nick = param(tag, "/whois", &params, "nick");
	if (!nick) return;
	ircFmt("WHOIS %s\r\n", nick);
}

static void inputTopic(struct Tag tag, char *params) {
	if (params) {
		ircFmt("TOPIC %s :%s\r\n", tag.name, params);
	} else {
		ircFmt("TOPIC %s\r\n", tag.name);
	}
}

static void inputQuit(struct Tag tag, char *params) {
	(void)tag;
	if (params) {
		ircQuit(params);
	} else {
		ircQuit("Goodbye");
	}
}

static void inputURL(struct Tag tag, char *params) {
	(void)params;
	urlList(tag);
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

static void inputView(struct Tag tag, char *params) {
	(void)tag;
	char *view = param(tag, "/view", &params, "name or number");
	if (!view) return;
	int num = strtol(view, &view, 0);
	if (!view[0]) {
		uiViewNum(num);
	} else {
		struct Tag tag = tagFind(view);
		if (tag.id != TagNone.id) {
			uiViewTag(tag);
		} else {
			uiFmt(tag, UIHot, "No view for %s", view);
		}
	}
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

static const struct {
	const char *command;
	Handler handler;
} Commands[] = {
	{ "/close", inputClose },
	{ "/help", inputMan },
	{ "/join", inputJoin },
	{ "/man", inputMan },
	{ "/me", inputMe },
	{ "/names", inputWho },
	{ "/nick", inputNick },
	{ "/open", inputOpen },
	{ "/part", inputPart },
	{ "/query", inputQuery },
	{ "/quit", inputQuit },
	{ "/raw", inputRaw },
	{ "/topic", inputTopic },
	{ "/url", inputURL },
	{ "/view", inputView },
	{ "/who", inputWho },
	{ "/whois", inputWhois },
};
static const size_t CommandsLen = sizeof(Commands) / sizeof(Commands[0]);

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
		} else {
			privmsg(tag, false, input);
		}
		return;
	}

	char *word = strsep(&input, " ");
	if (input && !input[0]) input = NULL;

	char *trail;
	strtol(&word[1], &trail, 0);
	if (!trail[0]) {
		inputView(tag, &word[1]);
		return;
	}

	const char *command = word;
	const char *uniq = tabNext(TagNone, command);
	if (uniq && uniq == tabNext(TagNone, command)) {
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
	uiFmt(tag, UICold, "%s isn't a recognized command", command);
}

void inputTab(void) {
	for (size_t i = 0; i < CommandsLen; ++i) {
		tabTouch(TagNone, Commands[i].command);
	}
}
